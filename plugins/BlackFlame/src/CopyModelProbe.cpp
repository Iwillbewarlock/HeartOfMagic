#include "CopyModelProbe.h"
#include "CopyModelProbeCache.h"

#include "Config.h"
#include "CopyFreshness.h"
#include "CopyRequest.h"
#include "FireClosure.h"
#include "NifDump.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "MainThreadQueue.h"
#include "SehGuard.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // I6: the work is spread over main-thread drains, a slice each (about half a 60 fps frame: no visible hitch; one Demand
        // that takes longer than the slice still runs whole, the log names the largest slice), so it finishes in the
        // background while the player sits in the menu, and goes on during play if a save is loaded first (a copy not probed
        // yet stays enabled). The budget is the sum of the slices; past it the rest is left out (listed) and those copies stay
        constexpr auto kBudget = std::chrono::seconds(20);
        constexpr auto kSlice = std::chrono::milliseconds(8);
        constexpr int kCacheVersion = 1;

        TrackedMutex g_statusMutex;
        std::string g_status = "not started";  // under g_statusMutex (the tester file reads it)
        std::atomic<std::size_t> g_failedCopies{ 0 };  // W27: the failing copies found (drop or log)
        std::atomic<bool> g_refreshTried{ false };     // Y10: Finish got to its refresh (Stopped does not run it again)

        void SetStatus(std::string a_status)
        {
            std::lock_guard lock(g_statusMutex);
            g_status = std::move(a_status);
        }
        constexpr std::size_t kPathsPerLine = 8;  // the left-out list, a few paths a line

        struct Result
        {
            bool probed = false;
            bool ok = false;
            bool fault = false;
            int code = 0;
            bool node = false;
            long long ms = 0;
        };

        std::string Key(std::string_view a_path)
        {
            std::string out(a_path);
            std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::ranges::replace(out, '/', '\\');
            return out;
        }

        std::string MuzzleOf(const RE::TESForm* a_form)
        {
            const auto* projectile = a_form ? a_form->As<RE::BGSProjectile>() : nullptr;
            const char* path = projectile ? projectile->muzzleFlashModel.model.c_str() : nullptr;
            return path ? std::string(path) : std::string();
        }

        Result Demand(const std::string& a_path)
        {
            Result result;
            result.probed = true;
            RE::NiPointer<RE::NiNode> out;
            auto code = RE::BSResource::ErrorCode::kNone;
            const auto begin = Clock::now();
            result.fault = !GuardedKeep("copy model probe", [&]() { code = RE::BSModelDB::Demand(a_path.c_str(), out, {}); });
            result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
            result.code = static_cast<int>(code);
            result.node = out != nullptr;
            result.ok = !result.fault && code == RE::BSResource::ErrorCode::kNone && result.node;
            out.reset();  // our reference given back (the model DB keeps or frees its entry)
            return result;
        }

        // The exact string handed to BSModelDB::Demand, quoted, and what came back
        std::string Describe(const std::string& a_path, const Result& a_result)
        {
            return std::format("Demand(\"{}\"): {}error code {}, node {}, {} ms", a_path, a_result.fault ? "FAULT; " : "", a_result.code,
                a_result.node ? "yes" : "NULL", a_result.ms);
        }

        bool Dropping() { return GetConfig().copyModelProbe == ProbeMode::kDrop; }

        struct Use
        {
            const RE::TESForm* source = nullptr;
            const RE::TESForm* copy = nullptr;
            std::string copyPath;
            std::string sourcePath;  // the source's model in the same role (the control), may be empty
            bool muzzle = false;
        };

        // The whole run's state, kept between drains (main thread only)
        struct Run
        {
            std::vector<Use> uses;
            std::vector<std::string> order;          // distinct copy paths, first-use order
            std::map<std::string, Result> results;  // by Key(path): copy paths and the controls asked for
            std::size_t next = 0;                    // next index into order, then into uses (the safety net)
            bool pathsDone = false;
            Clock::duration spent{};
            int ok = 0;
            int failed = 0;
            std::vector<std::string> leftOut;
            std::vector<DisabledCopy> disabled;
            std::vector<RE::FormID> muzzlesReverted;
            std::map<RE::FormID, std::size_t> seen;  // copy -> index in disabled (a copy failing in model and muzzle: one entry)
            std::string cacheKey;                    // empty: no cache is written (the map's identity unknown)
            std::map<std::string, std::string> sourceOf;  // Key(copy path) -> the first use's source path (the control)
            Clock::time_point begun = Clock::now();
            int frames = 0;
            int faults = 0;  // Demand calls that faulted (GuardedKeep)
            int staleIndex = 0;  // failed Demand while the plain stream opens the file: a stale loose-file index (CRDW and the like)
            Clock::duration largestSlice{};
        };

        using ProbeCache::CachePath;
        using ProbeCache::CopyFromLocal;
        using ProbeCache::LocalHex;
        using ProbeCache::MapIdentity;

        // A complete probe's result, for later starts with the same map
        void WriteCache(const Run& a_run, const std::vector<DisabledCopy>& a_disabled, const std::vector<RE::FormID>& a_muzzles)
        {
            const auto path = CachePath();
            if (!path || a_run.cacheKey.empty()) {
                return;
            }
            nlohmann::json root;
            root["version"] = kCacheVersion;
            root["_comment"] = "Written by HeartOfMagic_BlackFlame after a complete copy model probe (results only: copies whose "
                               "model failed while the source's loaded); applied only with copyModelProbe \"drop\", while the "
                               "copies map is the one in 'key'. Delete it, or set copyModelProbeAlways true, to probe again.";
            root["key"] = a_run.cacheKey;
            root["paths"] = a_run.order.size();
            root["failed"] = a_run.failed;
            root["staleIndex"] = a_run.staleIndex;
            root["dropped"] = nlohmann::json::array();
            for (const auto& item : a_disabled) {
                root["dropped"].push_back({ { "copy", LocalHex(item.copy) }, { "text", item.text } });
            }
            root["muzzles"] = nlohmann::json::array();
            for (const auto id : a_muzzles) {
                root["muzzles"].push_back(LocalHex(id));
            }
            try {
                std::filesystem::create_directories(path->parent_path());
                const auto temp = std::filesystem::path(*path).concat(L".tmp");
                {
                    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                    out << root.dump(2) << "\n";
                    out.close();
                    if (!out) {
                        throw std::runtime_error("write failed");
                    }
                }
                std::filesystem::rename(temp, *path);
                logger::info("BlackFlame: copy model probe: result cached in {}", path->string());
            } catch (const std::exception& e) {
                logger::warn("BlackFlame: copy model probe: the result could not be cached ({}): the next start probes again", e.what());
            }
        }

        bool ApplyCacheChecked(const std::string& a_key);

        // The cached result when it was made for this copies map: applied at once (kDataLoaded), true. False: probe.
        // W23: a cache with a value of the wrong JSON type (hand-edited) makes nlohmann throw; that is a cache to ignore,
        // never a throw out of kDataLoaded (it ran before every hook install)
        bool ApplyCache(const std::string& a_key)
        {
            try {
                return ApplyCacheChecked(a_key);
            } catch (const std::exception& e) {
                logger::info("BlackFlame: copy model probe: the cached result is not readable ({}) - probing again in the "
                             "background", e.what());
                return false;
            }
        }

        bool ApplyCacheChecked(const std::string& a_key)
        {
            const auto path = CachePath();
            if (!path || a_key.empty()) {
                return false;
            }
            std::ifstream in(*path, std::ios::binary);
            if (!in) {
                logger::info("BlackFlame: copy model probe: no cached result - probing in the background");
                return false;
            }
            const auto root = nlohmann::json::parse(in, nullptr, false);
            if (root.is_discarded() || !root.is_object() || root.value("version", 0) != kCacheVersion ||
                root.value("key", std::string()) != a_key || root.value("failed", -1) != 0 || !root.contains("dropped") ||
                !root["dropped"].is_array() ||
                !root.contains("muzzles") || !root["muzzles"].is_array()) {
                logger::info("BlackFlame: copy model probe: the cached result is for another copies map or plugin version, or had "
                             "failures (their cause may lie outside the copies, e.g. a loose-file cache) - probing again in the "
                             "background");
                return false;
            }
            std::vector<DisabledCopy> disabled;
            std::vector<RE::FormID> muzzles;
            for (const auto& item : root["dropped"]) {
                const auto id = item.is_object() && item.contains("copy") && item["copy"].is_string() ?
                                    CopyFromLocal(item["copy"].get<std::string>()) : 0;
                if (id == 0) {
                    logger::info("BlackFlame: copy model probe: the cached result names a copy that is not there - probing again");
                    return false;
                }
                disabled.push_back({ id, std::string("(cached) ") + item.value("text", std::string()) });
            }
            for (const auto& item : root["muzzles"]) {
                const auto id = item.is_string() ? CopyFromLocal(item.get<std::string>()) : 0;
                if (id == 0) {
                    logger::info("BlackFlame: copy model probe: the cached result names a copy that is not there - probing again");
                    return false;
                }
                muzzles.push_back(id);
            }
            const bool drop = Dropping();
            g_failedCopies.store(disabled.size());
            for (const auto& item : disabled) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} {}: {}", item.copy,
                    drop ? "out of the swaps this session (stays original)" : "would be dropped (log only, it stays in the swaps)",
                    item.text);
            }
            logger::info("BlackFlame: copy NIF failed to load in game: {} cop{} {}, {} muzzle(s) {} (from the cached probe of this "
                         "copies map; nothing probed this start)", disabled.size(), disabled.size() == 1 ? "y" : "ies",
                drop ? "taken out of the swaps" : "would be dropped (copyModelProbe \"log\": none is)", muzzles.size(),
                drop ? "back to the source's" : "would go back");
            SetStatus(std::format("{} from the cache ({} failed copies, {} muzzles)", drop ? "drop" : "log only", disabled.size(),
                muzzles.size()));
            if (!drop) {
                DisableCopiesForSession({}, {});  // log only: the probe is done, nothing is taken out
                return true;
            }
            const bool changed = !disabled.empty() || !muzzles.empty();
            DisableCopiesForSession(std::move(disabled), std::move(muzzles));
            if (changed) {
                RefreshCopies("copy model probe (cached)");
            }
            return true;
        }

        bool OverBudget(const Run& a_run) { return a_run.spent > kBudget; }

        // A probed result for a_path: already probed (a copy path or an earlier control) or probed now (I5)
        const Result& ResultFor(Run& a_run, const std::string& a_path)
        {
            auto& slot = a_run.results[Key(a_path)];
            if (!slot.probed) {
                slot = Demand(a_path);
                a_run.faults += slot.fault ? 1 : 0;
            }
            return slot;
        }

        void Prepare(Run& a_run)
        {
            for (const auto& filled : FilledCopies()) {
                if (auto path = ModelPathOf(filled.copy); !path.empty()) {
                    a_run.uses.push_back({ filled.source, filled.copy, std::move(path), ModelPathOf(filled.source), false });
                }
                if (auto path = MuzzleOf(filled.copy); !path.empty()) {
                    a_run.uses.push_back({ filled.source, filled.copy, std::move(path), MuzzleOf(filled.source), true });
                }
            }
            for (const auto& use : a_run.uses) {
                if (a_run.results.try_emplace(Key(use.copyPath)).second) {
                    a_run.order.push_back(use.copyPath);
                    a_run.sourceOf.emplace(Key(use.copyPath), use.sourcePath);
                }
            }
        }

        // Does the engine's plain stream open the file? It does not go through the loose-file index that the model database's
        // lookup uses (a cache mod such as CRDW AE hooks that lookup, 70050 / 70164), so "stream opens, Demand error 1" means
        // that index does not know the file (probe-fail notes)
        bool StreamOpens(const std::string& a_path)
        {
            const std::string data = Key(a_path).starts_with("meshes\\") ? a_path : "meshes\\" + a_path;
            bool good = false;
            (void)GuardedKeep("copy model probe stream", [&]() {
                RE::BSResourceNiBinaryStream stream(data.c_str());
                good = stream.good();
            });
            return good;
        }

        // One copy path (left out when the budget ran out)
        void ProbePath(Run& a_run, const std::string& a_path)
        {
            if (OverBudget(a_run)) {
                a_run.leftOut.push_back(a_path);
                return;
            }
            const auto result = ResultFor(a_run, a_path);
            if (result.ok) {
                ++a_run.ok;
                return;
            }
            ++a_run.failed;
            // The control's string beside the copy's, so the path form can be read from one line
            const auto& source = a_run.sourceOf[Key(a_path)];
            std::string control = "no source path in that role";
            bool controlOk = false;
            if (!source.empty()) {
                if (OverBudget(a_run)) {
                    control = std::format("Demand(\"{}\") not run (budget)", source);
                } else {
                    const auto& sourceResult = ResultFor(a_run, source);
                    controlOk = sourceResult.ok;
                    control = Describe(source, sourceResult);
                }
            }
            const bool stream = StreamOpens(a_path);
            // W25: a stale index only when the control loaded (a failing control puts the probe itself in doubt)
            const bool stale = stream && controlOk && result.code == static_cast<int>(RE::BSResource::ErrorCode::kNotExist);
            a_run.staleIndex += stale ? 1 : 0;
            logger::warn("BlackFlame: copy model probe: FAILED copy {} | stream open: {}{} | control (source) {}",
                Describe(a_path, result), stream ? "yes" : "no",
                stale ? " - the file exists but the game's file index does not know it" : "", control);
        }

        // The safety net for one use: a failed copy path whose source path (the control) loads takes the copy out (its model)
        // or puts the source's muzzle back (its muzzle, I8)
        void Judge(Run& a_run, const Use& a_use)
        {
            const auto& result = a_run.results[Key(a_use.copyPath)];
            if (!result.probed || result.ok) {
                return;
            }
            const char* role = a_use.muzzle ? "muzzle" : "model";
            const auto copyId = a_use.copy->GetFormID();
            if (Key(a_use.copyPath) == Key(a_use.sourcePath)) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} is the source's own (the builder kept it), "
                             "nothing to fall back to: {}", copyId, role, Describe(a_use.copyPath, result));  // I5
                return;
            }
            std::string controlText = "no source model in that role";
            bool controlOk = false;
            if (!a_use.sourcePath.empty()) {
                const auto known = a_run.results.find(Key(a_use.sourcePath));
                if ((known == a_run.results.end() || !known->second.probed) && OverBudget(a_run)) {
                    logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} failed, the control was not checked (the {} s "
                                 "budget ran out): {}", copyId, role, std::chrono::duration_cast<std::chrono::seconds>(kBudget).count(),
                        Describe(a_use.copyPath, result));  // I6
                    return;
                }
                const auto& control = ResultFor(a_run, a_use.sourcePath);
                controlOk = control.ok;
                controlText = "source " + Describe(a_use.sourcePath, control);
            }
            const std::string text = std::format("{} {} (source {:08X}; control {})", role, Describe(a_use.copyPath, result),
                a_use.source->GetFormID(), controlText);
            if (!controlOk) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} failed but the source's did not load either - "
                             "not a copy fault: {}", copyId, role, text);
                return;
            }
            if (a_use.muzzle) {
                a_run.muzzlesReverted.push_back(copyId);
                logger::warn("BlackFlame: copy model probe: copy {:08X} keeps its swaps; its muzzle goes back to the source's: {}",
                    copyId, text);
                return;
            }
            if (const auto it = a_run.seen.find(copyId); it != a_run.seen.end()) {
                a_run.disabled[it->second].text += "; " + text;
                return;
            }
            a_run.seen.emplace(copyId, a_run.disabled.size());
            a_run.disabled.push_back({ copyId, text });
        }

        void Finish(Run& a_run)
        {
            const auto ms = [](Clock::duration a_duration) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(a_duration).count();
            };
            logger::info("BlackFlame: copy model probe: {} distinct copy model path(s) of {} copies: {} ok, {} failed ({} of them: file "
                         "exists but the game's file index does not know it), {} left out; {} ms of probing over {} frames (largest "
                         "slice {} ms), {} ms from start to end", a_run.order.size(), FilledCopies().size(), a_run.ok, a_run.failed,
                a_run.staleIndex, a_run.leftOut.size(), ms(a_run.spent), a_run.frames, ms(a_run.largestSlice),
                ms(Clock::now() - a_run.begun));
            if (a_run.staleIndex > 0 && a_run.ok > 0) {
                ReportStaleFileIndex(static_cast<std::size_t>(a_run.staleIndex));
            } else if (a_run.staleIndex > 0) {
                logger::warn("BlackFlame: copy model probe: {} copy NIF(s) look unknown to the file index, but no copy NIF loaded "
                             "at all - the probe itself is in doubt: no notice", a_run.staleIndex);  // W25
            }
            for (std::size_t i = 0; i < a_run.leftOut.size(); i += kPathsPerLine) {
                std::string line;
                for (std::size_t j = i; j < std::min(a_run.leftOut.size(), i + kPathsPerLine); ++j) {
                    line += (line.empty() ? "" : ", ") + a_run.leftOut[j];
                }
                logger::info("BlackFlame: copy model probe: not probed (the {} s budget ran out): {}",
                    std::chrono::duration_cast<std::chrono::seconds>(kBudget).count(), line);
            }
            const bool drop = Dropping();
            g_failedCopies.store(a_run.disabled.size());
            for (const auto& item : a_run.disabled) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} {}: {}", item.copy,
                    drop ? "out of the swaps this session (stays original)" : "would be dropped (log only, it stays in the swaps)",
                    item.text);
            }
            logger::info("BlackFlame: copy NIF failed to load in game: {} cop{} {}, {} muzzle(s) {}", a_run.disabled.size(),
                a_run.disabled.size() == 1 ? "y" : "ies",
                drop ? "taken out of the swaps" : "would be dropped (copyModelProbe \"log\": none is)", a_run.muzzlesReverted.size(),
                drop ? "back to the source's" : "would go back");
            const bool changed = drop && (!a_run.disabled.empty() || !a_run.muzzlesReverted.empty());
            // W14: only a complete probe without failures is cached: ApplyCache never reuses one with failures (their cause
            // may lie outside the copies), so its "dropped" and "muzzles" lists are empty by construction - the cache's only
            // effect is skipping the probe at the next start; the lists stay in the format for a later use
            if (a_run.leftOut.empty() && a_run.failed == 0) {
                WriteCache(a_run, a_run.disabled, a_run.muzzlesReverted);
            }
            SetStatus(std::format("{}, probed this start ({} failed copies, {} muzzles, {} left out)", drop ? "drop" : "log only",
                a_run.disabled.size(), a_run.muzzlesReverted.size(), a_run.leftOut.size()));
            if (!drop) {
                DisableCopiesForSession({}, {});  // log only: the probe is done, nothing is taken out
                return;
            }
            DisableCopiesForSession(std::move(a_run.disabled), std::move(a_run.muzzlesReverted));
            // I7: links other copies hold to a dropped copy go back to the original now, and a reverted muzzle is written.
            // J1: only from the main-thread queue (the AddTask fallback runs tasks on job threads); in play, pointers only
            if (!changed) {
                return;
            }
            if (!MainQueue::Installed()) {
                logger::info("BlackFlame: copy model probe: no main-thread queue - links to the dropped copies go back to the "
                             "source at the next load's refresh");
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            g_refreshTried.store(true);
            RefreshCopies("copy model probe", player && player->GetParentCell() != nullptr);
        }

        // One drain's slice; true when the run is over
        bool Step(Run& a_run)
        {
            const auto start = Clock::now();
            ++a_run.frames;
            struct SliceEnd  // the slice's length, however it ends
            {
                Run& run;
                Clock::time_point start;
                ~SliceEnd() { run.largestSlice = (std::max)(run.largestSlice, Clock::now() - start); }
            } sliceEnd{ a_run, start };
            while (Clock::now() - start < kSlice) {
                const auto before = Clock::now();
                if (!a_run.pathsDone) {
                    if (a_run.next < a_run.order.size()) {
                        ProbePath(a_run, a_run.order[a_run.next++]);
                    } else {
                        a_run.pathsDone = true;
                        a_run.next = 0;
                    }
                } else if (a_run.next < a_run.uses.size()) {
                    Judge(a_run, a_run.uses[a_run.next++]);
                } else {
                    return true;
                }
                a_run.spent += Clock::now() - before;
            }
            return false;
        }

        // J4: a probe that cannot go on still ends, nothing taken out, with the reason in the tester line
        void Stopped(const std::string& a_why)
        {
            if (CopyModelProbeDone()) {  // X19: Finish got as far as its result: say so, and do its last step
                const auto out = CopiesFailedToLoad().size();
                logger::warn("BlackFlame: copy model probe stopped after its result took effect ({} cop{} out): {}", out,
                    out == 1 ? "y" : "ies", a_why);
                SetStatus(std::format("probe stopped after its result ({} copies out): {}", out, a_why));
                if (Dropping() && out > 0 && MainQueue::Installed() && !g_refreshTried.exchange(true)) {  // Y10: once, guarded
                    const auto* player = RE::PlayerCharacter::GetSingleton();
                    (void)Guarded("copy model probe refresh after a stop",
                        [&]() { RefreshCopies("copy model probe", player && player->GetParentCell() != nullptr); });
                }
                return;
            }
            logger::warn("BlackFlame: copy model probe stopped: {} - no copy is taken out", a_why);
            SetStatus("probe stopped: " + a_why);
            DisableCopiesForSession({}, {});
        }

        void Post(std::shared_ptr<Run> a_run)
        {
            const bool queued = QueueTask("BlackFlameCopyModelProbe", [a_run]() {
                // W24: a hardware fault or a C++ exception still ends the probe (Stopped), so CopyModelProbeDone() becomes
                // true; DisableCopiesForSession keeps the first call, so a Finish that got that far keeps its result
                try {
                    bool done = true;
                    if (!Guarded("copy model probe run", [&]() { done = Step(*a_run); })) {
                        Stopped(std::format("a hardware fault in a slice (logged), {} Demand fault(s) before it", a_run->faults));
                        return;
                    }
                    if (!done) {
                        Post(a_run);
                    } else if (!Guarded("copy model probe finish", [&]() { Finish(*a_run); })) {
                        Stopped("a hardware fault while finishing (logged)");
                    }
                } catch (const std::exception& e) {
                    Stopped(std::format("an exception: {}", e.what()));
                } catch (...) {
                    Stopped("an exception of an unknown type");
                }
            });
            if (!queued) {
                Stopped("could not be queued for the main thread");
            }
        }
    }

    void QueueCopyModelProbe()
    {
        const auto& config = GetConfig();
        if (config.copyModelProbe == ProbeMode::kOff) {
            logger::info("BlackFlame: copy model probe: off (copyModelProbe \"off\")");
            SetStatus("off (copyModelProbe \"off\")");
            return;
        }
        logger::info("BlackFlame: copy model probe: mode \"{}\" ({})", ProbeModeName(config.copyModelProbe),
            config.copyModelProbe == ProbeMode::kDrop ? "a copy whose model fails while its source's loads leaves the swaps" :
                                                        "results logged only, no copy is taken out");
        auto run = std::make_shared<Run>();
        Prepare(*run);
        // W20: the key also carries the file-index state (CRDW.dll, every copy NIF's size and time): a cache made before
        // the index went stale is not reused, so the probe runs again and can report it
        if (auto map = MapIdentity(); !map.empty()) {
            run->cacheKey = map + " " + FileIndexState(run->order);
        }
        // A normal start with an unchanged copies map and file index skips the probe (X17: the key's file stat costs ms, logged).
        // Probe again when the map changed or there is no cache, and always under copyModelProbeAlways, selfTest or testerMode
        const bool forced = config.copyModelProbeAlways || config.selfTest || config.testerMode;
        if (!forced && ApplyCache(run->cacheKey)) {
            return;
        }
        if (forced) {
            logger::info("BlackFlame: copy model probe: probing in the background ({})",
                config.copyModelProbeAlways ? "copyModelProbeAlways" : "selfTest / testerMode");
        }
        SetStatus("probing");
        Post(std::move(run));
    }

    std::size_t CopyModelProbeFailedCopies() noexcept
    {
        return g_failedCopies.load();
    }

    std::string CopyModelProbeStatus()
    {
        std::lock_guard lock(g_statusMutex);
        return g_status;
    }
}
