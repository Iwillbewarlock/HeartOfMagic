// =============================================================================
// OldCopies - removes the copies\<key16>\ folders of older generations, once a session (OldCopies.h)
// =============================================================================

#include "OldCopies.h"

#include "BuildProgress.h"
#include "Config.h"
#include "CopyGroups.h"
#include "FileIndex.h"
#include "MainThreadQueue.h"
#include "MenuWatch.h"
#include "OldCopiesFiles.h"
#include "OldCopiesKeepsets.h"
#include "OldCopiesRule.h"
#include "OldCopiesVfs.h"
#include "SingleLaunch.h"
#include "SlotAssign.h"
#include "SlotLedger.h"
#include "SlotLedgerFiles.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace BlackFlame::OldCopies
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace Rule = OldCopiesRule;
        namespace Files = OldCopiesFiles;
        namespace Keepsets = OldCopiesKeepsets;
        using Clock = std::chrono::steady_clock;
        using Vfs::Guard;

        constexpr const char* kTaskName = "old copies";
        constexpr auto kRecheck = std::chrono::seconds(1);  // the gate is looked at this often while it waits (Requeue)
        constexpr std::size_t kMaxFailureLines = 20;
        constexpr std::size_t kMaxKeepsetWarnings = 3;      // NoteLedgerWritten's failures logged a session
        constexpr double kMiB = 1024.0 * 1024.0;
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";

        std::atomic<bool> g_armed{ false };
        std::mutex g_keepsetLock;  // every keep set and registry file access (OldCopiesKeepsets.h is not thread safe)
        bool g_registered = false;           // under g_keepsetLock: this epoch is in the registry
        std::size_t g_keepsetWarnings = 0;   // under g_keepsetLock
        std::atomic<std::uint64_t> g_ledgerWrites{ 0 };  // NoteLedgerWritten calls, counted under g_keepsetLock (Decide reads it)
        std::string g_waitingFor;            // main thread only (the poll)

        std::string Text(const fs::path& a_path) { return SlotLedgerFiles::PathText(a_path); }

        fs::path KeepsetFolder()
        {
            const auto staging = BuildProgress::StagingFolder();
            return staging.empty() ? fs::path() : staging / Keepsets::kFolderName;
        }

        // Where the ledger mirrors are (SlotLedger.cpp Load), empty without an SKSE log folder
        fs::path MirrorFolder()
        {
            const auto log = SKSE::log::log_directory();
            return log ? *log / kFolderName : fs::path();
        }

        enum class Gate : std::uint8_t
        {
            kGo,
            kWait,
            kSkip,
        };

        struct GateResult
        {
            Gate gate = Gate::kSkip;
            std::string why;
        };

        // OldCopies.h "when"; main thread
        GateResult CheckGate()
        {
            if (!MenuWatch::PublishOpen()) {
                return { Gate::kSkip, "a load began first (" + MenuWatch::CloseReason() + ")" +
                                          (g_waitingFor.empty() ? "" : "; it was waiting for " + g_waitingFor) };
            }
            if (!MenuWatch::DrainMayPublish()) {
                return MenuWatch::ProofPending() ? GateResult{ Gate::kWait, "the main-thread proof" } :
                                                   GateResult{ Gate::kSkip, "no main-thread proof (" + MenuWatch::ProofText() + ")" };
            }
            if (SingleLaunch::OtherGameHoldsFolder()) {
                return { Gate::kSkip, "another game process holds the request folder" };
            }
            const auto status = SlotLedger::GetStatus();
            if (!status.available) {
                return { Gate::kSkip, "no ledger this session (" + status.why + ")" };
            }
            if (status.totalLoss || status.restoredFromBackup) {
                return { Gate::kSkip, status.totalLoss ? "the ledger was rebuilt this session (no ledger file read)" :
                                                         "the ledger was restored from a backup or the mirror this session" };
            }
            if (status.saveFailures > 0) {  // final: a failed write is never undone
                return { Gate::kSkip, std::format("{} ledger write(s) failed this session (main or the mirror)", status.saveFailures) };
            }
            if (!CopyGroups::GetProgress().settled) {
                return { Gate::kWait, "publication to settle" };
            }
            // review round 1 (reviewer 6): no BuilderLauncher::Running() here (it takes the launcher's lock and may run the
            // builder's throttle on the caller's thread). CaughtUp (atomic) is set only after the worker saw the builder not
            // running and then read progress.jsonl to its end (BuildProgress.cpp Worker); the builder starts once a session
            if (!BuildProgress::CaughtUp()) {
                return { Gate::kWait, "the builder to end and the progress worker to read its last lines" };
            }
            if (const auto bad = BuildProgress::GetStats().badLines; bad > 0) {
                return { Gate::kSkip, std::format("progress.jsonl had {} refused line(s)", bad) };
            }
            // review round 1 (Gemini): only now, publication's final write done (settled), is "no write" final
            if (status.saves == 0) {
                return { Gate::kSkip, "the ledger was not written this session" };
            }
            return { Gate::kGo, "" };
        }

        // This session's request keys (every assigned slot's, usable or not) and the effect shader palettes' keys
        std::vector<std::uint64_t> RequestKeys()
        {
            std::vector<std::uint64_t> keys;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                for (const auto* key : { &slot.modelKey, &slot.muzzleKey, &slot.txstKey }) {
                    if (*key) {
                        keys.push_back((*key)->key);
                    }
                }
            }
            for (const auto* list : { &SingleLaunch::Inputs().efsh.palettes, &SingleLaunch::Inputs().efshBase.palettes }) {
                for (const auto& entry : *list) {  // contract 8: the base textures' copies are this session's too
                    if (entry.key) keys.push_back(*entry.key);
                }
            }
            return keys;
        }

        // What the slot forms were published with this session (main thread fields)
        void AddPublishedNow(Rule::KeySet& a_keep)
        {
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                Rule::AddKeysOfText(a_keep, slot.publishedModel);
                Rule::AddKeysOfText(a_keep, slot.publishedMuzzle);
                for (const auto& texture : slot.publishedTextures) {
                    Rule::AddKeysOfText(a_keep, texture);
                }
                if (const auto key = Rule::FolderKey(slot.publishedTxst)) {
                    a_keep.insert(*key);
                }
            }
        }

        // Each key's progress line followed (OldCopiesRule::AddRequestClosure), seeded with every key a_keep holds and a_more
        void AddClosure(Rule::KeySet& a_keep, const std::vector<std::uint64_t>& a_more)
        {
            std::vector<std::uint64_t> seeds(a_keep.begin(), a_keep.end());
            seeds.insert(seeds.end(), a_more.begin(), a_more.end());
            Rule::AddRequestClosure(a_keep, seeds, [](std::uint64_t a_key) -> std::optional<CopyContract::ProgressLine> {
                auto object = BuildProgress::Find(a_key);
                return object ? std::optional(std::move(object->line)) : std::nullopt;
            });
        }

        // Every ledger mirror in the log folder; false with a_why when one is there but does not read
        bool AddMirrors(Rule::KeySet& a_keep, std::size_t& a_read, std::string& a_why)
        {
            const auto folder = MirrorFolder();
            if (folder.empty()) {
                a_why = "no SKSE log folder (the ledger mirrors cannot be looked at)";
                return false;
            }
            const auto mirrors = Keepsets::ReadMirrors(folder);
            if (!mirrors.ok) {
                a_why = mirrors.why;
                return false;
            }
            for (const auto& ledger : mirrors.ledgers) {
                Rule::AddLedger(a_keep, ledger);
            }
            a_read = mirrors.ledgers.size();
            return true;
        }

        struct Tally
        {
            std::size_t folders = 0;
            std::uint64_t bytes = 0;
            void Add(const Files::Listing& a_listing)
            {
                ++folders;
                bytes += a_listing.bytes;
            }
        };

        struct Totals
        {
            Tally data;
            Tally staging;
            Tally dataCounted;          // under MO2 with its overwrite folder unknown: Data counted, not removed
            std::size_t kept = 0;
            std::size_t notOurs = 0;
            std::size_t notPlain = 0;   // a link, a file, or a folder holding something other than plain files
            std::size_t recent = 0;     // within the grace
            std::size_t notOverwrite = 0;
            std::size_t overLimit = 0;
            std::size_t failed = 0;
            bool stoppedByLoad = false;
            bool stoppedByOtherGame = false;  // another game process came to hold the request folder mid-cleanup
            std::vector<std::string> notes;  // roots refused or not listed
        };

        struct Context
        {
            Rule::KeySet own;  // this session's keep set (written as this epoch's keep set file)
            std::vector<std::uint64_t> request;
            std::uint64_t epoch = 0;
            std::uint64_t writesAtCopy = 0;  // g_ledgerWrites read just before the ledger was copied (AddKeepsets)
            std::size_t ledgerSlots = 0;
            std::optional<fs::path> data;
            fs::path staging;
            bool dryRun = false;
            std::uint32_t graceDays = 0;
        };

        fs::path CopiesUnder(const fs::path& a_base, const wchar_t* a_root)
        {
            return (a_base / a_root / L"heartofmagic" / L"blackflame" / L"copies").lexically_normal();
        }

        struct Side
        {
            fs::path root;
            std::vector<std::string> names;
            bool usable = false;  // safe (present or not)
        };

        Side OpenSide(const fs::path& a_base, const wchar_t* a_sub, Totals& a_totals)
        {
            Side side{ CopiesUnder(a_base, a_sub) };
            bool present = false;
            std::string why;
            if (!Files::RootSafe(a_base, side.root, present, why)) {
                a_totals.notes.push_back(why);
                return side;
            }
            std::error_code ec;
            for (auto it = present ? fs::directory_iterator(side.root, ec) : fs::directory_iterator(); !ec && it != fs::directory_iterator();
                 it.increment(ec)) {
                side.names.push_back(Text(it->path().filename()));
            }
            if (ec) {
                a_totals.notes.push_back("'" + Text(side.root) + "' could not be listed: " + ec.message());
                return side;
            }
            side.usable = true;
            return side;
        }

        // One folder listed: false (counted) when it is no plain folder of files or does not list
        bool List(const fs::path& a_root, const fs::path& a_folder, Files::Listing& a_out, const char* a_where, Totals& a_totals)
        {
            std::string why;
            const auto listed = Rule::ContainedIn(a_root, a_folder) ? Files::ListFolder(a_folder, a_out, why) : Files::Listed::kNotPlain;
            if (listed == Files::Listed::kNotPlain) {
                ++a_totals.notPlain;
            } else if (listed == Files::Listed::kFailed && a_totals.failed++ < kMaxFailureLines) {
                logger::warn("BlackFlame: old copies: '{}' ({}) not removed: {}", Text(a_folder), a_where, why);
            }
            return listed == Files::Listed::kPlain;
        }

        bool Remove(const fs::path& a_folder, const Files::Listing& a_listing, const char* a_where, Tally& a_tally, Totals& a_totals)
        {
            std::uint64_t bytes = 0;
            std::string why;
            const bool removed = Files::RemoveListed(a_folder, a_listing, bytes, why);
            a_tally.bytes += bytes;  // the files that did go
            if (removed) {
                ++a_tally.folders;
            } else if (a_totals.failed++ < kMaxFailureLines) {
                logger::warn("BlackFlame: old copies: '{}' ({}) not removed: {}", Text(a_folder), a_where, why);
            }
            return removed;
        }

        // One subtree (meshes or textures), key by key: the staging folder first, then the Data folder, and the Data folder
        // only after its staging folder went (review round 1, reviewer 4: a staging copy left without its Data copy would be
        // copied back in at the next start). A refused staging root leaves the subtree whole; a refused Data root, staging only
        void CleanSubtree(const wchar_t* a_sub, const Context& a_context, const Rule::KeySet& a_keep, Guard& a_guard, std::size_t& a_budget,
            Totals& a_totals)
        {
            const auto staging = OpenSide(a_context.staging, a_sub, a_totals);
            if (!staging.usable) {
                return;
            }
            const auto data = a_context.data ? OpenSide(*a_context.data, a_sub, a_totals) : Side{};
            const auto plan = Rule::PlanKeys(data.usable ? data.names : std::vector<std::string>{}, staging.names, a_keep);
            a_totals.kept += plan.kept;
            a_totals.notOurs += plan.notOurs;
            const auto now = Files::NowTicks();
            for (const auto& candidate : plan.remove) {
                if (!MenuWatch::PublishOpen()) {  // review round 1 (reviewer 5): nothing is removed once a load began
                    a_totals.stoppedByLoad = true;
                    return;
                }
                // review round 2 (reviewer 3): looked at again before each key. The flag is the kDataLoaded claim's (this game
                // then holds the folder's mutex for its lifetime, so a later game cannot take it): a guard, not a live probe
                if (SingleLaunch::OtherGameHoldsFolder()) {
                    a_totals.stoppedByOtherGame = true;
                    return;
                }
                const auto stagingFolder = staging.root / candidate.name;
                const auto dataFolder = data.root / candidate.name;
                Files::Listing inStaging;
                Files::Listing inData;
                if ((candidate.inStaging && !List(staging.root, stagingFolder, inStaging, "staging", a_totals)) ||
                    (candidate.inData && !List(data.root, dataFolder, inData, "Data", a_totals))) {
                    continue;
                }
                if (Rule::WithinGrace((std::max)(inStaging.newest, inData.newest), now, a_context.graceDays)) {
                    ++a_totals.recent;
                    continue;
                }
                bool dataGoes = candidate.inData && (!a_guard.vfs || a_guard.overwrite);
                if (candidate.inData && a_guard.vfs && !a_guard.overwrite) {
                    a_totals.dataCounted.Add(inData);
                } else if (dataGoes && a_guard.vfs && !Vfs::InOverwrite(a_guard, dataFolder, inData)) {
                    dataGoes = false;
                    ++a_totals.notOverwrite;
                }
                const std::size_t folders = (candidate.inStaging ? 1 : 0) + (dataGoes ? 1 : 0);
                if (folders == 0) {
                    continue;
                }
                if (!Rule::TakeBudget(a_budget, folders)) {
                    ++a_totals.overLimit;
                    continue;
                }
                if (a_context.dryRun) {
                    if (candidate.inStaging) {
                        a_totals.staging.Add(inStaging);
                    }
                    if (dataGoes) {
                        a_totals.data.Add(inData);
                    }
                    continue;
                }
                if (candidate.inStaging && !Remove(stagingFolder, inStaging, "staging", a_totals.staging, a_totals)) {
                    continue;  // its Data folder stays with it
                }
                if (dataGoes) {
                    (void)Remove(dataFolder, inData, "Data", a_totals.data, a_totals);
                }
            }
        }

        // This epoch's keep set file and the registry, then every keep set: false (logged) when the cleanup must not run
        bool AddKeepsets(const Context& a_context, Rule::KeySet& a_keep, std::size_t& a_files, std::size_t& a_registered)
        {
            const auto folder = KeepsetFolder();
            std::lock_guard lock(g_keepsetLock);
            std::string note;
            std::string why;
            // review round 2 (reviewer 2): a ledger write after the copy merged keys this keep set may lack: merge, not replace
            const auto writes = g_ledgerWrites.load();
            const bool merge = Keepsets::MergeOwnKeepset(a_context.writesAtCopy, writes);
            if (merge) {
                logger::info("BlackFlame: old copies: {} ledger write(s) after the keep set was taken: merged into its keep set file",
                    writes - a_context.writesAtCopy);
            }
            if (!Keepsets::WriteKeepset(folder, a_context.epoch, a_context.own, merge, note, why)) {
                logger::warn("BlackFlame: old copies: not cleaned this session: this ledger's keep set file could not be written: {}", why);
                return false;
            }
            if (!g_registered && !Keepsets::Register(folder, a_context.epoch, why)) {
                logger::warn("BlackFlame: old copies: not cleaned this session: this ledger's epoch could not be registered: {}", why);
                return false;
            }
            g_registered = true;
            auto all = Keepsets::ReadAll(folder);
            if (!all.ok) {
                logger::warn("BlackFlame: old copies: not cleaned this session: {}", all.why);
                return false;
            }
            a_keep.insert(all.keys.begin(), all.keys.end());
            a_files = all.files;
            a_registered = all.registered;
            return true;
        }

        void Clean(Context a_context)
        {
            ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                const auto start = Clock::now();
                AddClosure(a_context.own, a_context.request);
                Rule::KeySet keep = a_context.own;
                std::size_t keepsets = 0;
                std::size_t registered = 0;
                std::size_t mirrors = 0;
                std::string why;
                if (!AddKeepsets(a_context, keep, keepsets, registered)) {
                    return;
                }
                if (!AddMirrors(keep, mirrors, why)) {
                    logger::warn("BlackFlame: old copies: not cleaned this session: {}", why);
                    return;
                }
                AddClosure(keep, {});  // review round 1 (Gemini): every kept key's lines followed, not only the request's
                logger::info("BlackFlame: old copies: {} key(s) kept ({} this session's from {} ledger slots and {} request key(s); {} "
                             "keep set file(s) of {} registered epoch(s); {} mirror(s)); {}", keep.size(), a_context.own.size(),
                    a_context.ledgerSlots, a_context.request.size(), keepsets, registered, mirrors,
                    a_context.dryRun ? "counting (dry run)" : "cleaning");
                Guard guard = a_context.data ? Vfs::MakeGuard(*a_context.data, keep) : Guard{};
                Totals totals;
                std::size_t budget = Rule::kMaxFoldersPerSession;
                for (const auto* sub : { L"meshes", L"textures" }) {
                    if (!totals.stoppedByLoad && !totals.stoppedByOtherGame) {
                        CleanSubtree(sub, a_context, keep, guard, budget, totals);
                    }
                }
                std::string notes;
                for (const auto& note : totals.notes) {
                    notes += "; " + note;
                }
                const auto counted = totals.dataCounted.folders == 0 ? std::string() :
                                         std::format(" (and {} ({:.1f} MiB) counted only: {})", totals.dataCounted.folders,
                                             totals.dataCounted.bytes / kMiB, guard.why);
                logger::info("BlackFlame: old copies{}: {} folders ({:.1f} MiB) {} from Data{}, {} ({:.1f} MiB) from staging, {} kept; "
                             "skipped: {} not key folders, {} not plain folders of files, {} within the {}-day grace, {} Data folders "
                             "outside MO2's overwrite folder, {} over the {} a session, {} failed{}{} ({} ms)",
                    a_context.dryRun ? " (dry run, cleanOldCopies false)" : "", totals.data.folders, totals.data.bytes / kMiB,
                    a_context.dryRun ? "would be removed" : "removed", counted, totals.staging.folders, totals.staging.bytes / kMiB,
                    totals.kept, totals.notOurs, totals.notPlain, totals.recent, a_context.graceDays, totals.notOverwrite,
                    totals.overLimit, Rule::kMaxFoldersPerSession, totals.failed,
                    totals.stoppedByLoad      ? "; stopped: a load began (" + MenuWatch::CloseReason() + ")" :
                    totals.stoppedByOtherGame ? std::string("; stopped: another game process holds the request folder") :
                                                std::string(),
                    notes,
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
            } catch (const std::exception& e) {
                logger::error("BlackFlame: old copies: the cleanup threw ({}): stopped", e.what());
            } catch (...) {
                logger::error("BlackFlame: old copies: the cleanup threw an exception of an unknown type: stopped");
            }
        }

        // Main thread, the gate open: what only the main thread may read, then the rest on its own thread
        void Decide()
        {
            // read before the copy: a write the copy misses always shows as a changed count (AddKeepsets then merges)
            const auto writesAtCopy = g_ledgerWrites.load();
            const auto ledger = SlotLedger::Copy();  // after publication's final write (same drain as the settle)
            if (!Rule::HasObjects(ledger)) {
                logger::info("BlackFlame: old copies: not cleaned this session: the ledger holds no published copy");
                return;
            }
            Context context;
            context.staging = BuildProgress::StagingFolder().lexically_normal();
            if (context.staging.empty()) {
                logger::info("BlackFlame: old copies: not cleaned this session: no staging folder (no SKSE log folder)");
                return;
            }
            Rule::AddLedger(context.own, ledger);
            AddPublishedNow(context.own);
            context.request = RequestKeys();
            context.epoch = ledger.epoch;
            context.writesAtCopy = writesAtCopy;
            context.ledgerSlots = ledger.entries.size();
            if (const auto data = FileIndex::DataFolder()) {
                context.data = data->lexically_normal();
            }
            context.dryRun = !GetConfig().cleanOldCopies;
            context.graceDays = GetConfig().oldCopiesGraceDays;
            std::thread(&Clean, std::move(context)).detach();  // detached: never joined at unload
        }

        void Poll();

        // The next poll posted kRecheck from now by a short-lived thread (MainThreadQueue has no delayed post), so a waiting
        // poll is one queue task a second, not one a frame, in the main-thread trace ring (review round 2, reviewer)
        void Requeue()
        {
            try {
                std::thread([]() {
                    std::this_thread::sleep_for(kRecheck);
                    if (!MainQueue::Post(kTaskName, &Poll)) {
                        logger::warn("BlackFlame: old copies: the poll could not be queued again: not cleaned this session");
                    }
                }).detach();
            } catch (const std::system_error& e) {
                logger::warn("BlackFlame: old copies: the poll's timer thread did not start ({}): not cleaned this session", e.what());
            }
        }

        void Poll()
        {
            try {
                const auto gate = CheckGate();
                if (gate.gate == Gate::kWait) {
                    if (g_waitingFor != gate.why) {
                        logger::info("BlackFlame: old copies: waiting for {}", gate.why);
                        g_waitingFor = gate.why;
                    }
                    Requeue();
                } else if (gate.gate == Gate::kSkip) {
                    logger::info("BlackFlame: old copies: not cleaned this session: {}", gate.why);
                } else {
                    Decide();
                }
            } catch (const std::exception& e) {
                logger::error("BlackFlame: old copies: the poll threw ({}): not cleaned this session", e.what());
            } catch (...) {
                logger::error("BlackFlame: old copies: the poll threw an exception of an unknown type: not cleaned this session");
            }
        }
    }

    void Arm()
    {
        if (g_armed.exchange(true)) {
            return;
        }
        if (!MainQueue::Post(kTaskName, &Poll)) {
            logger::warn("BlackFlame: old copies: the poll could not be queued: not cleaned this session");
        }
    }

    bool NoteSessionStart(std::uint64_t a_epoch)
    {
        try {
            const auto keepsets = KeepsetFolder();
            const auto folder = MirrorFolder();
            if (keepsets.empty() || folder.empty()) {
                return true;  // no keep set folder: no cleanup either, nothing to hold
            }
            const auto mirrors = Keepsets::ReadMirrors(folder);
            if (!mirrors.ok) {
                logger::warn("BlackFlame: old copies: at start: {} ({} mirror(s) unread): mirror held, no cleanup", mirrors.why, mirrors.bad);
            }
            std::lock_guard lock(g_keepsetLock);
            const auto adopted = Keepsets::AdoptMirrors(keepsets, mirrors.ledgers, a_epoch);
            for (const auto& note : adopted.notes) {
                logger::warn("BlackFlame: old copies: at start: {}", note);
            }
            logger::info("BlackFlame: old copies: at start: {} mirror(s) read, {} of another ledger epoch registered with a keep set "
                         "from their keys (this epoch {})", mirrors.ledgers.size(), adopted.epochs, CopyContract::Hex16(a_epoch));
            return mirrors.ok && adopted.failed == 0;
        } catch (const std::exception& e) {
            logger::error("BlackFlame: old copies: the mirrors' adoption at start threw: {}", e.what());
        } catch (...) {
            logger::error("BlackFlame: old copies: the mirrors' adoption at start threw an exception of an unknown type");
        }
        return false;
    }

    void NoteLedgerWritten(const SlotLedgerModel::Ledger& a_ledger)
    {
        try {
            std::lock_guard lock(g_keepsetLock);
            g_ledgerWrites.fetch_add(1);  // first: AddKeepsets merges after any write, whether or not its keep set was written
            const auto folder = KeepsetFolder();
            if (folder.empty()) {
                return;
            }
            Rule::KeySet keys;
            Rule::AddLedger(keys, a_ledger);
            std::string note;
            std::string why;
            const bool written = Keepsets::WriteKeepset(folder, a_ledger.epoch, std::move(keys), true, note, why);
            const bool registered = written && (g_registered || Keepsets::Register(folder, a_ledger.epoch, why));
            g_registered = g_registered || registered;
            if (!registered && g_keepsetWarnings++ < kMaxKeepsetWarnings) {
                logger::warn("BlackFlame: old copies: epoch {} {}: {}", CopyContract::Hex16(a_ledger.epoch),
                    written ? "not registered" : "keep set file not written", why);
            } else if (!note.empty()) {
                logger::info("BlackFlame: old copies: epoch {}: {}", CopyContract::Hex16(a_ledger.epoch), note);
            }
        } catch (const std::exception& e) {
            logger::error("BlackFlame: old copies: the keep set update threw: {}", e.what());
        } catch (...) {
            logger::error("BlackFlame: old copies: the keep set update threw an exception of an unknown type");
        }
    }
}
