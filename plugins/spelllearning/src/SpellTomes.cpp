#include "SpellTomes.h"
#include "AsciiText.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"
#include "SpellTomeHook.h"
#include "SpellTomePairs.h"
#include "ThreadUtils.h"
#include "librarian/SpellResearchPatch.h"
#include "tomereach/TomeReach.h"
#include "uimanager/UIManager.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// SpellTomes - the snapshot of tome pairs and the one SpellOf (SpellTomes.h)
// =============================================================================

namespace SpellTomes
{
    namespace
    {
        namespace Pairs = SpellTomePairs;
        namespace SRPatch = Librarian::SpellResearchPatch;

        constexpr const char* kPatchFile = "Data/SKSE/Plugins/SpellResearchSynthesizer/SynthesizedPatch.json";
        // A patch of every spell of a large load order is a few MB; far past that it is not one.
        // Skipped, it counts as found but unusable: the dev self-test's sr-paired-tomes FAILs on purpose
        constexpr std::uintmax_t kMaxPatchBytes = 64ull * 1024 * 1024;

        struct Snapshot
        {
            std::unordered_map<Pairs::FormId, Pairs::Entry> paired;   // book that teaches nothing -> its spell
            Status status;
        };

        // Read from any thread, swapped on the game thread
        std::atomic<std::shared_ptr<const Snapshot>> g_snapshot;

        // Game thread only
        bool g_patchRead = false;
        std::optional<Pairs::PatchRead> g_patch;                 // nullopt: no file
        std::vector<Pairs::LivePart> g_lastLive;                 // the last readable state of SR's lists
        bool g_liveRead = false;
        // The last full scan's generation (0 = none) and paired tomes. Scans collect on the game
        // thread (Papyrus RunScan too, PapyrusAPI.cpp); atomics only so any thread may read them
        std::atomic<std::uint64_t> g_scanGeneration = 0;
        std::atomic<std::size_t> g_scanPaired = 0;

        std::optional<Pairs::PatchRead> ReadPatchFile()
        {
            const std::filesystem::path path(kPatchFile);
            std::error_code error;
            if (!std::filesystem::exists(path, error)) return std::nullopt;
            if (const auto size = std::filesystem::file_size(path, error); !error && size > kMaxPatchBytes) {
                logger::warn("SpellTomes: '{}' is {} bytes, over the {} read - skipped, Spell Research's lists are used, "
                             "unverified", PathText::Utf8(path), size, kMaxPatchBytes);
                return Pairs::PatchRead{};
            }
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                logger::warn("SpellTomes: '{}' exists but cannot be opened - Spell Research's lists are used, unverified",
                    PathText::Utf8(path));
                return Pairs::PatchRead{};
            }
            const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto read = Pairs::ReadPatch(bytes);
            for (const auto& problem : read.problems) logger::warn("SpellTomes: {}: {}", kPatchFile, problem);
            if (!read.usable) {
                logger::warn("SpellTomes: '{}' is not usable - Spell Research's lists are used, unverified. Run Spell "
                             "Research Synthesizer again to write it anew", kPatchFile);
            } else {
                logger::info("SpellTomes: {} - {} new spells, {} with a tome, {} without, {} malformed", kPatchFile,
                    read.newSpells, read.entries.size(), read.noTome, read.malformed);
            }
            return read;
        }

        // One tier list's parts as ids: the plugin's entries, then the ones scripts added
        void AddParts(const RE::BGSListForm* books, const RE::BGSListForm* spells, std::vector<Pairs::LivePart>& out)
        {
            if (!books || !spells) return;
            Pairs::LivePart record;
            for (const auto* form : books->forms) record.books.push_back(form ? form->GetFormID() : 0);
            for (const auto* form : spells->forms) record.spells.push_back(form ? form->GetFormID() : 0);
            out.push_back(std::move(record));
            Pairs::LivePart script;
            script.script = true;
            if (books->scriptAddedTempForms) script.books.assign(books->scriptAddedTempForms->begin(), books->scriptAddedTempForms->end());
            if (spells->scriptAddedTempForms) script.spells.assign(spells->scriptAddedTempForms->begin(), spells->scriptAddedTempForms->end());
            out.push_back(std::move(script));
        }

        // SR's lists when they can be read now; otherwise the last readable state stays
        void ReadLive()
        {
            SRPatch::TomeLists lists{};
            if (SRPatch::ReadTomeLists(lists) != SRPatch::TomeListState::kReady) return;
            std::vector<Pairs::LivePart> parts;
            for (const auto& [books, spells] : lists) AddParts(books, spells, parts);
            g_lastLive = std::move(parts);
            g_liveRead = true;
        }

        // "plugin|local id" -> FormID (0: not there). The load order does not change in a
        // session, and LookupForm walks every file by name: each id is looked up once.
        std::unordered_map<std::string, Pairs::FormId> g_resolved;

        Pairs::FormId Resolve(const Pairs::FormRef& ref)
        {
            const std::string key = std::format("{}|{}", ref.plugin, ref.localId);
            if (const auto it = g_resolved.find(key); it != g_resolved.end()) return it->second;
            auto* dh = RE::TESDataHandler::GetSingleton();
            const auto* file = dh ? dh->LookupModByName(ref.plugin) : nullptr;
            // VR has no light plugin index (CommonLib's LookupFormID: 24 bits there)
            const bool fits = file && Pairs::LocalIdFits(ref.localId, file->IsLight() && !REL::Module::IsVR());
            auto* form = fits ? dh->LookupForm(ref.localId, ref.plugin) : nullptr;
            const Pairs::FormId id = form ? form->GetFormID() : 0;
            g_resolved.emplace(key, id);
            return id;
        }

        Pairs::Facts GameFacts()
        {
            Pairs::Facts facts;
            facts.resolve = Resolve;
            facts.book = [](Pairs::FormId id) {
                Pairs::BookFacts out;
                auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(id);
                if (!book) return out;
                out.isBook = true;
                auto* spell = book->TeachesSpell() ? book->GetSpell() : nullptr;
                out.taughtSpell = spell ? spell->GetFormID() : 0;
                const auto* winner = book->GetFile(-1);
                out.winner = winner ? std::string(winner->GetFilename()) : std::string("(no file)");
                return out;
            };
            facts.isSpell = [](Pairs::FormId id) { return RE::TESForm::LookupByID<RE::SpellItem>(id) != nullptr; };
            return facts;
        }

        // Books whose TeachesSpell flag a mod cleared after load (SpellScribe through SkyPatcher). The
        // engine set data.teaches.spell from the record while the flag was on, and SkyPatcher's
        // removeFlags clears only the flag bit (its code: `and byte ptr [book+0x110], 0xFB`), so the
        // pointer is still the spell. A book that never had the flag holds an actor value or a raw id
        // there: only a pointer that is one of the game's spells counts, compared, never dereferenced.
        std::vector<Pairs::ClearedTome> ClearedFlagTomes()
        {
            std::vector<Pairs::ClearedTome> out;
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return out;
            std::unordered_set<const RE::SpellItem*> spells;
            for (const auto* spell : dh->GetFormArray<RE::SpellItem>()) {
                if (spell) spells.insert(spell);
            }
            for (const auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
                if (!book || book->TeachesSpell()) continue;
                const auto* spell = book->data.teaches.spell;
                if (spell && spells.contains(spell)) out.push_back({ book->GetFormID(), spell->GetFormID() });
            }
            return out;
        }

        std::size_t CountOwnSpellTomes()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return 0;
            std::size_t count = 0;
            for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
                if (book && book->TeachesSpell() && book->GetSpell()) ++count;
            }
            return count;
        }

        std::string Describe(const Status& s)
        {
            const auto& c = s.counts;
            const std::size_t live = c.liveRecord + c.liveScript;
            return std::format("SpellTomes: {} tomes - {} by the book's own spell, {} by Spell Research pairs "
                "(records {}, script {}{}), {} by SynthesizedPatch.json ({}), {} by a flag cleared at runtime "
                "(experimental, {} of them also in the JSON); {} pairs skipped ({} unresolved, {} not a "
                "book and a spell, {} teach a spell themselves of which {} another, {} list pairs against an earlier "
                "pair, {} outside the JSON, {} twice in the JSON, {} JSON pairs against a cleared-flag book, "
                "{} list parts of unequal length) [generation {}]",
                s.ownSpell + c.Paired(), s.ownSpell, live, c.liveRecord, c.liveScript,
                live ? ", unverified" : "", c.patch, s.patchUsable ? "read" : (s.patchFound ? "unusable" : "none"),
                c.cleared, c.clearedInPatch,
                c.unresolved + c.notBookOrSpell + c.teaches + c.liveConflicts + c.liveOutsidePatch + c.patchConflicts +
                    c.clearedConflicts,
                c.unresolved, c.notBookOrSpell, c.teaches, c.teachesOther, c.liveConflicts,
                c.liveOutsidePatch, c.patchConflicts, c.clearedConflicts, c.unequalParts, s.generation);
        }

        // Which plugin made the JSON's tomes teach nothing: SpellResearchSynthesizer.esp run
        // standalone, the Synthesis group's plugin (Synthesis.esp by default) otherwise
        void LogPatchWinners(const Pairs::Result& result)
        {
            if (result.patchByWinner.empty()) return;
            std::string line;
            for (const auto& [winner, count] : result.patchByWinner) {
                line += std::format("{}{} {}", line.empty() ? "" : ", ", winner, count);
            }
            logger::info("SpellTomes: JSON tomes by winning plugin: {}", line);
        }

        // Which plugins the books with a flag cleared at runtime come from (the record's first file)
        void LogClearedByPlugin(const Pairs::Result& result)
        {
            std::map<std::string, std::size_t> byPlugin;
            for (const auto& [bookId, entry] : result.pairs) {
                if (entry.source != Pairs::Source::kClearedFlag) continue;
                const auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId);
                const auto* file = book ? book->GetFile(0) : nullptr;
                ++byPlugin[file ? std::string(file->GetFilename()) : std::string("(no file)")];
            }
            if (byPlugin.empty()) return;
            std::string line;
            for (const auto& [plugin, count] : byPlugin) line += std::format("{}{} {}", line.empty() ? "" : ", ", plugin, count);
            logger::info("SpellTomes: books with the flag cleared at runtime (experimental), by plugin: {}", line);
        }

        // Told on a later task: at a save load NotifySaveGameLoaded ends kPostLoadGame, after
        // this, and inGame is UIManager's own state then (a save or a new game: a retry must hold)
        void TellPanel(std::uint64_t generation, std::size_t paired, bool stale, std::string_view reason)
        {
            AddTaskToGameThread("SpellTomesChanged", [generation, paired, stale, why = std::string(reason)] {
                auto* ui = UIManager::GetSingleton();
                const nlohmann::json payload = { { "generation", generation }, { "pairedTomes", paired },
                                                 { "stale", stale }, { "inGame", ui->IsInGame() }, { "reason", why } };
                ui->NotifySpellTomesChanged(JsonText::Dump(payload));
            });
        }

        // A new generation: everything that kept an answer about tomes drops it
        void OnPairsChanged(const Snapshot& now, std::string_view reason)
        {
            SpellTomeHook::GetSingleton()->InvalidateTomeInventoryCache();
            TomeReach::Restart(now.status.generation);   // before the groups: they read the pass's answer
            SpellScanner::InvalidateDuplicateGroups();
            SpellScanner::PrepareTreeSpellForTomeAfterPass();
            const std::uint64_t generation = now.status.generation;
            // A rebuild for a scan: that scan reads these pairs (it notes them when it is done)
            const std::uint64_t scanned = g_scanGeneration.load();
            const bool stale = reason != kReasonScan && scanned != 0 && scanned != generation &&
                now.paired.size() > g_scanPaired.load();
            logger::info("SpellTomes: {} - the pairs changed (generation {}, {} paired tomes{}) - tome reach runs again",
                reason, generation, now.paired.size(), stale ? ", the last full scan had fewer: stale" : "");
            TellPanel(generation, now.paired.size(), stale, reason);
        }

        void RebuildOnGameThread(std::string_view reason)
        {
            if (!g_patchRead) {
                g_patch = ReadPatchFile();
                g_patchRead = true;
            }
            ReadLive();
            const Pairs::Result result = Pairs::Build({ g_patch ? &*g_patch : nullptr, g_lastLive, ClearedFlagTomes() },
                GameFacts());

            auto next = std::make_shared<Snapshot>();
            next->paired = result.pairs;
            const auto previous = g_snapshot.load();
            if (previous && Pairs::SamePairs(previous->paired, next->paired)) return;

            Status& status = next->status;
            status.generation = previous ? previous->status.generation + 1 : 1;
            status.ownSpell = CountOwnSpellTomes();
            status.paired = next->paired.size();
            status.counts = result.counts;
            status.patchFound = g_patch.has_value();
            status.patchUsable = g_patch && g_patch->usable;
            status.liveRead = g_liveRead;
            g_snapshot.store(next);

            logger::info("{} ({})", Describe(status), reason);
            for (const auto& note : result.notes) logger::info("SpellTomes: {}", note);
            LogPatchWinners(result);
            LogClearedByPlugin(result);
            if (previous) OnPairsChanged(*next, reason);
        }
    }

    RE::SpellItem* SpellOf(RE::TESObjectBOOK* book)
    {
        if (!book) return nullptr;
        if (book->TeachesSpell()) {
            if (auto* spell = book->GetSpell()) return spell;
        }
        const auto snapshot = g_snapshot.load();
        if (!snapshot) return nullptr;
        const auto it = snapshot->paired.find(book->GetFormID());
        return it == snapshot->paired.end() ? nullptr : RE::TESForm::LookupByID<RE::SpellItem>(it->second.spell);
    }

    void ForEachPair(const std::function<void(RE::FormID book, RE::FormID spell)>& visit)
    {
        const auto snapshot = g_snapshot.load();
        if (!snapshot) return;
        for (const auto& [book, entry] : snapshot->paired) visit(book, entry.spell);
    }

    bool IsPairedTome(RE::FormID book)
    {
        const auto snapshot = g_snapshot.load();
        return snapshot && snapshot->paired.contains(book);
    }

    void ForEachTome(const std::function<void(RE::TESObjectBOOK*, RE::SpellItem*)>& visit)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;
        for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
            if (auto* spell = SpellOf(book)) visit(book, spell);
        }
    }

    void Rebuild(std::string_view reason)
    {
        if (!IsOnGameThread()) {
            AddTaskToGameThread("SpellTomesRebuild", [why = std::string(reason)] { Rebuild(why); });
            return;
        }
        try {
            RebuildOnGameThread(reason);
        } catch (const std::exception& e) {
            logger::error("SpellTomes: {} - the pairs could not be read again ({}), the last ones stay", reason, e.what());
            // None yet: an empty generation 0, so the next good read is a change and is passed on
            if (!g_snapshot.load()) g_snapshot.store(std::make_shared<const Snapshot>());
        }
    }

    std::uint64_t Generation()
    {
        const auto snapshot = g_snapshot.load();
        return snapshot ? snapshot->status.generation : 0;
    }

    bool CurrentStatus(Status& out)
    {
        const auto snapshot = g_snapshot.load();
        if (!snapshot) return false;
        out = snapshot->status;
        return true;
    }

    std::string StatusLine()
    {
        const auto snapshot = g_snapshot.load();
        return snapshot ? Describe(snapshot->status) : std::string("SpellTomes: not read yet");
    }

    void NoteFullScan()
    {
        const auto snapshot = g_snapshot.load();
        g_scanGeneration = snapshot ? snapshot->status.generation : 0;
        g_scanPaired = snapshot ? snapshot->paired.size() : 0;
    }

    void NoteFullScanOf(std::uint64_t generation, std::size_t paired)
    {
        g_scanGeneration = generation;
        g_scanPaired = paired;
        const auto snapshot = g_snapshot.load();
        if (!snapshot || snapshot->status.generation == generation) return;
        // The pairs changed while the scan ran: OnPairsChanged judged that change against the scan before, so it is
        // judged again here against the pairs this scan had
        const bool stale = generation != 0 && snapshot->paired.size() > paired;
        logger::warn("SpellTomes: the pairs changed during a scan (generation {} with {} paired tomes, now {} with {}){}",
            generation, paired, snapshot->status.generation, snapshot->paired.size(), stale ? " - the scan is stale" : "");
        if (stale) TellPanel(snapshot->status.generation, snapshot->paired.size(), true, "scan");
    }
}
