#include "Common.h"
#include "AutoScan.h"
#include "JsonText.h"
#include "LoadOrderFingerprint.h"
#include "ScanRecord.h"
#include "ScanRecordRules.h"
#include "SpellScanner.h"
#include "SpellTomes.h"
#include "ThreadUtils.h"
#include "librarian/Librarian.h"
#include "tomereach/TomeReach.h"
#include "uimanager/StartupState.h"
#include "uimanager/TreeSaveCheck.h"
#include "uimanager/UIManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

// The scan at game start (AutoScan.h; docs/ARCHITECTURE.md, "Scan at game start").
//
//   main menu (game thread)  Begin: the load order
//   waiter thread            the tome reach pass, the setting, the tree, the record,
//                            the panel scans' turn (UIManager::TryBeginOtherScan)
//   game thread, one a frame FullScanCollector::Step, a slice of kSliceDefault at most;
//                            the waiter posts each step after the last one answered,
//                            and a step runs only once the engine's frame time
//                            (GetDurationOfApplicationRunTime) moved on from the last
//   waiter thread            the text, ClassifyScan (catalog + scan record), the dump
//   waiter + game thread     the tree graft (StartupState.h, RunTreeGraftAtMainMenu), after
//                            the scan or when the catalog already was the load order's
//
// Every step checks the generation the form reads started in: a save load or a
// new game (OnGameChanging) drops them, and they start again at once (kAttempts
// in all; then the next main menu). A save loaded before they start does not
// matter: the steps run during play as they do at the main menu.

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto kTomeReachWait = std::chrono::seconds(30);   // between "still waiting" lines; the wait has no limit
    constexpr double kSliceDefaultMs = 4.0;          // game thread time of one step, at most (the marks step excepted)
    constexpr double kSliceMinMs = 0.5;
    constexpr double kSliceMaxMs = 50.0;
    constexpr double kHitchBudgetDefaultMs = 25.0;   // self-test: the longest frame may exceed the main menu's by this much
    constexpr auto kStepAnswerPoll = std::chrono::milliseconds(250);   // how often a wait for a step looks at the generation
    constexpr int kStillWaitingEvery = 40;                             // polls between "still waiting" lines (10 s)
    constexpr int kAttempts = 3;                                       // form reads started again after a load, at most
    constexpr auto kNextFrameWait = std::chrono::milliseconds(1);
    // A frame time that does not move for this long (the counter not where it is
    // expected to be): the step runs anyway and is counted as forced
    constexpr auto kFrameStuckLimit = std::chrono::milliseconds(100);
    constexpr auto kProbeInterval = std::chrono::milliseconds(2);
    constexpr auto kBaseline = std::chrono::seconds(3);

    constexpr const char* kConfigFile = "Data/SKSE/Plugins/SpellLearning/config.json";
    constexpr const char* kSelfTestFile = "Data/SKSE/Plugins/SpellLearning/selftest.json";
    constexpr const char* kTreeFile = "Data/SKSE/Plugins/SpellLearning/spell_tree.json";
    constexpr const char* kTreeRecordFile = "Data/SKSE/Plugins/SpellLearning/spell_tree_loadorder.json";
    constexpr const char* kCatalogFile = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    constexpr const char* kPrefix = "AutoScan:";
    constexpr const char* kTestPrefix = "AutoScan SELFTEST:";

    enum class State { Idle, Running, Done };

    std::atomic<State> s_state{ State::Idle };
    std::atomic<std::uint32_t> s_generation{ 0 };

    struct Settings
    {
        bool selfTest = false;
        double sliceMs = kSliceDefaultMs;
        double budgetMs = kHitchBudgetDefaultMs;
    };

    double ClampedNumber(const nlohmann::json& object, const char* key, double fallback, double low, double high)
    {
        const auto found = object.find(key);
        if (found == object.end()) return fallback;
        if (!found->is_number()) {
            logger::warn("{} selftest.json autoScan.{} {} is not a number - using {}", kPrefix, key, found->dump(), fallback);
            return fallback;
        }
        return std::clamp(found->get<double>(), low, high);
    }

    // selftest.json { "autoScan": { "enabled": true, "sliceMs": 4, "budgetMs": 25 } };
    // anything else: no self-test, the default slice
    Settings ReadSettings()
    {
        Settings settings;
        std::ifstream file(kSelfTestFile, std::ios::binary);
        if (!file.is_open()) return settings;
        const auto document = nlohmann::json::parse(file, nullptr, false);
        if (document.is_discarded() || !document.is_object()) return settings;
        const auto section = document.find("autoScan");
        if (section == document.end() || !section->is_object()) return settings;
        const auto enabled = section->find("enabled");
        if (enabled == section->end() || !enabled->is_boolean() || !enabled->get<bool>()) return settings;
        settings.selfTest = true;
        settings.sliceMs = ClampedNumber(*section, "sliceMs", kSliceDefaultMs, kSliceMinMs, kSliceMaxMs);
        settings.budgetMs = ClampedNumber(*section, "budgetMs", kHitchBudgetDefaultMs, 0.0, 1000.0);
        return settings;
    }

    enum class Setting { On, Off, Unreadable };

    // "Build Spell Tree at Start" (autoBuildSetting.js, config.json "autoBuild"):
    // "off" means nothing at game start. No file is the default (on); a file
    // that does not parse is the player's choice unknown, so nothing runs.
    Setting ReadAutoBuildSetting()
    {
        std::ifstream file(kConfigFile, std::ios::binary);
        if (!file.is_open()) {
            // Only a file that is not there is the default; one there but not opened (locked, no access) is unknown
            std::error_code ec;
            const bool exists = std::filesystem::exists(kConfigFile, ec);
            return !exists && !ec ? Setting::On : Setting::Unreadable;
        }
        const auto config = nlohmann::json::parse(file, nullptr, false);
        if (config.is_discarded() || !config.is_object()) return Setting::Unreadable;
        const auto value = config.find("autoBuild");
        return value != config.end() && value->is_string() && value->get<std::string>() == "off" ? Setting::Off : Setting::On;
    }

    std::optional<std::string> ReadFileText(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream text;
        text << file.rdbuf();
        if (file.bad()) return std::nullopt;
        return std::move(text).str();
    }

    // FNV-1a 64 of a file, "missing" when there is none (self-test: the tree files unchanged)
    std::string FileFingerprint(const char* path)
    {
        const auto text = ReadFileText(path);
        if (!text) return "missing";
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (const unsigned char c : *text) {
            hash ^= c;
            hash *= 0x100000001b3ULL;
        }
        return std::format("{:016x}/{}", hash, text->size());
    }

    double MsSince(Clock::time_point started)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    // =========================================================================
    // FRAME PROBE (self-test only): the longest frame, from the engine's frame time
    // =========================================================================

    struct ProbeWindow
    {
        std::uint32_t frames = 0;      // frame time changes seen
        std::uint32_t longestFrameMs = 0;
        double longestWaitMs = 0.0;    // a probe task's wait for the game thread
    };

    struct ProbeData
    {
        std::mutex mutex;
        std::atomic<bool> stop{ false };
        int window = 0;                // 0 baseline, 1 scan, 2 after
        std::optional<std::uint32_t> lastStamp;
        ProbeWindow windows[3];
    };

    class FrameProbe
    {
    public:
        FrameProbe() : m_data(std::make_shared<ProbeData>()) {}
        FrameProbe(const FrameProbe&) = delete;
        FrameProbe& operator=(const FrameProbe&) = delete;
        ~FrameProbe() { Stop(); }

        bool Start()
        {
            try {
                m_thread = std::thread([data = m_data]() {
                    try {
                        while (!data->stop.load()) {
                            const auto posted = Clock::now();
                            AddTaskToGameThread("AutoScanProbe", [data, posted]() {
                                const std::uint32_t stamp = RE::GetDurationOfApplicationRunTime();
                                const double waited = MsSince(posted);
                                std::lock_guard lock(data->mutex);
                                auto& window = data->windows[data->window];
                                window.longestWaitMs = std::max(window.longestWaitMs, waited);
                                if (data->lastStamp && stamp != *data->lastStamp) {
                                    ++window.frames;
                                    window.longestFrameMs = std::max(window.longestFrameMs, stamp - *data->lastStamp);
                                }
                                data->lastStamp = stamp;
                            });
                            std::this_thread::sleep_for(kProbeInterval);
                        }
                    } catch (...) {
                        logger::error("{} the frame probe stopped (unknown error)", kTestPrefix);
                    }
                });
                return true;
            } catch (const std::system_error& e) {
                logger::warn("{} no thread for the frame probe ({})", kTestPrefix, e.what());
                return false;
            }
        }

        void SetWindow(int window)
        {
            std::lock_guard lock(m_data->mutex);
            m_data->window = window;
        }

        void Stop()
        {
            m_data->stop.store(true);
            if (m_thread.joinable()) m_thread.join();
            SetWindow(2);   // probe tasks still queued count in neither window
        }

        ProbeWindow Window(int window)
        {
            std::lock_guard lock(m_data->mutex);
            return m_data->windows[window];
        }

    private:
        std::shared_ptr<ProbeData> m_data;
        std::thread m_thread;
    };

    // =========================================================================
    // THE STEPS
    // =========================================================================

    enum class StepResult { NotYet, Stepped, Finished, Dropped, Failed };

    // One run's game thread side. The collector is created, stepped and finished
    // on the game thread only; the waiter thread only waits on each answer.
    struct Run
    {
        std::uint32_t generation = 0;
        std::atomic<bool> abandoned{ false };   // the waiter gave up on a step: a late one does nothing
        std::chrono::microseconds slice{};
        std::optional<std::uint32_t> lastStamp;
        Clock::time_point stampWaitStarted{};
        bool waitingForFrame = false;
        std::size_t forcedSteps = 0;
        std::size_t frameWaits = 0;
        std::unique_ptr<SpellScanner::FullScanCollector> collector;
        std::shared_ptr<nlohmann::json> output;
        std::size_t steps = 0;
        double longestStepMs = 0.0;
        std::string longestStepPhase;
    };

    // Game thread
    StepResult StepOnGameThread(Run& run)
    {
        if (run.abandoned.load() || s_generation.load() != run.generation) return StepResult::Dropped;

        const std::uint32_t stamp = RE::GetDurationOfApplicationRunTime();
        if (run.lastStamp && stamp == *run.lastStamp) {
            // Still the frame of the last step
            if (!run.waitingForFrame) {
                run.waitingForFrame = true;
                run.stampWaitStarted = Clock::now();
            }
            if (Clock::now() - run.stampWaitStarted < kFrameStuckLimit) {
                ++run.frameWaits;
                return StepResult::NotYet;
            }
            ++run.forcedSteps;
        }
        run.waitingForFrame = false;
        run.lastStamp = stamp;

        if (!run.collector) {
            SpellScanner::ScanConfig config;
            config.fields = SpellScanner::FieldsForPreset("full");
            run.collector = std::make_unique<SpellScanner::FullScanCollector>(std::move(config));
        }
        const bool done = run.collector->Step(run.slice);
        if (!done) return StepResult::Stepped;

        run.steps = run.collector->Steps();
        run.longestStepMs = run.collector->LongestStepMs();
        run.longestStepPhase = run.collector->LongestStepPhase();
        run.output = std::make_shared<nlohmann::json>(run.collector->Finish());
        run.collector.reset();
        return StepResult::Finished;
    }

    // Waiter thread: one step on the game thread, and its answer
    StepResult PostStep(const std::shared_ptr<Run>& run)
    {
        auto answer = std::make_shared<std::promise<StepResult>>();
        auto future = answer->get_future();
        AddTaskToGameThread("AutoScanStep", [run, answer = std::move(answer)]() {
            try {
                answer->set_value(StepOnGameThread(*run));
            } catch (const std::exception& e) {
                logger::error("{} a step failed: {}", kPrefix, e.what());
                answer->set_value(StepResult::Failed);
            } catch (...) {
                logger::error("{} a step failed (unknown error)", kPrefix);
                answer->set_value(StepResult::Failed);
            }
        });
        // The game thread can be away for long (a window in the background, a load):
        // wait for as long as the session is the run's, give up as soon as it is not
        int polls = 0;
        while (future.wait_for(kStepAnswerPoll) != std::future_status::ready) {
            if (s_generation.load() != run->generation) {
                // The run stays alive in the task if it runs later; marked, that step does nothing
                run->abandoned.store(true);
                return StepResult::Dropped;
            }
            if (++polls % kStillWaitingEvery == 0) {
                logger::info("{} still waiting for the game thread to run a step", kPrefix);
            }
        }
        try {
            return future.get();
        } catch (...) {
            // The task was dropped without running (broken_promise)
            return StepResult::Failed;
        }
    }

    // Holds the panel scans' turn for the run; lets it go however the run ends
    class ScanTurn
    {
    public:
        ScanTurn() = default;
        ScanTurn(const ScanTurn&) = delete;
        ScanTurn& operator=(const ScanTurn&) = delete;
        ~ScanTurn() { Release(); }

        bool Take()
        {
            auto* ui = UIManager::GetSingleton();
            m_held = ui && ui->TryBeginOtherScan();
            return m_held;
        }

        void Release() noexcept
        {
            if (!m_held) return;
            m_held = false;
            if (auto* ui = UIManager::GetSingleton()) ui->EndOtherScan();
        }

    private:
        bool m_held = false;
    };

    // =========================================================================
    // SELF-TEST
    // =========================================================================

    struct TestCounts
    {
        int pass = 0;
        int fail = 0;
        int skip = 0;

        void Report(const char* check, bool passed, const std::string& detail)
        {
            passed ? ++pass : ++fail;
            logger::info("{} {} {} - {}", kTestPrefix, check, passed ? "PASS" : "FAIL", detail);
        }

        void Skip(const char* check, const std::string& detail)
        {
            ++skip;
            logger::info("{} {} SKIP - {}", kTestPrefix, check, detail);
        }
    };

    // A dump with the scan timestamp blanked: two scans of one load order compare equal
    std::string ComparableDump(nlohmann::json output)
    {
        output["scanTimestamp"] = "";
        return JsonText::Dump(output, 2);
    }

    // The same scan in one game thread task, as the panel's CollectScan reads it
    // (a frozen game for its length: after the frame probe stopped)
    std::optional<std::string> OneShotDump()
    {
        auto answer = std::make_shared<std::promise<std::shared_ptr<nlohmann::json>>>();
        auto future = answer->get_future();
        AddTaskToGameThread("AutoScanSelfTestOneShot", [answer]() {
            try {
                SpellScanner::ScanConfig config;
                config.fields = SpellScanner::FieldsForPreset("full");
                answer->set_value(std::make_shared<nlohmann::json>(SpellScanner::CollectAllSpells(config)));
            } catch (...) {
                answer->set_exception(std::current_exception());
            }
        });
        if (future.wait_for(std::chrono::seconds(60)) != std::future_status::ready) return std::nullopt;
        try {
            return ComparableDump(std::move(*future.get()));
        } catch (...) {
            return std::nullopt;
        }
    }

    // A run whose generation is behind the session's: its step must drop it
    // before it reads a form
    bool StaleRunIsDropped()
    {
        auto run = std::make_shared<Run>();
        run->generation = s_generation.load() - 1;
        run->slice = std::chrono::microseconds(1000);
        const auto result = PostStep(run);
        return result == StepResult::Dropped && !run->collector && !run->output;
    }

    // A run in the middle of its form reads when the generation moves on (what a
    // save load does): the next step drops it, and nothing is written
    bool RunDroppedMidway(std::string& detail)
    {
        constexpr int kStepsBefore = 3;
        auto run = std::make_shared<Run>();
        run->generation = s_generation.load();
        run->slice = std::chrono::microseconds(1000);
        int stepped = 0;
        while (stepped < kStepsBefore) {
            const auto result = PostStep(run);
            if (result == StepResult::Stepped) {
                ++stepped;
            } else if (result != StepResult::NotYet) {
                detail = "the run ended before the generation moved";
                return false;
            }
            std::this_thread::sleep_for(kNextFrameWait);
        }
        s_generation.fetch_add(1);
        const auto result = PostStep(run);
        detail = std::format("{} steps, then the generation moved: {}", stepped,
            result == StepResult::Dropped ? "dropped" : "NOT dropped");
        return result == StepResult::Dropped && !run->output;
    }

    // =========================================================================
    // THE RUN (waiter thread)
    // =========================================================================

    const char* CastersName(ScanRecordRules::Casters casters)
    {
        switch (casters) {
            case ScanRecordRules::Casters::kNotRun: return "not collected";
            case ScanRecordRules::Casters::kComplete: return "collected";
            case ScanRecordRules::Casters::kFailed: return "FAILED";
        }
        return "unknown";
    }

    // Why neither the scan nor the tree graft runs this session, or nothing: they go on
    // (a catalog that is the load order's already skips only the scan, unless a due graft
    // needs the scan's dump, RunScan)
    std::optional<std::string> Precondition()
    {
        switch (ReadAutoBuildSetting()) {
            case Setting::Off: return std::string("\"Build Spell Tree at Start\" is Off");
            case Setting::Unreadable: return std::string("config.json could not be read - the setting is unknown");
            case Setting::On: break;
        }
        const auto tree = ReadFileText(kTreeFile);
        if (!tree || !TreeSaveCheck::HasSpellNodes(*tree)) {
            return std::string("no tree with spells - left to the panel's build at game start");
        }
        return std::nullopt;
    }

    // The form reads: steps until done, dropped by a save load or a new game, or stopped
    StepResult Collect(const std::shared_ptr<Run>& run)
    {
        while (true) {
            const StepResult result = PostStep(run);
            if (result == StepResult::Finished || result == StepResult::Dropped || result == StepResult::Failed) {
                return result;
            }
            std::this_thread::sleep_for(kNextFrameWait);
        }
    }

    // Returns the state the session is left in: Done, or Idle to try at the next main menu
    State RunScan(const LoadOrderFingerprint::Value loadOrder)
    {
        const auto started = Clock::now();
        const Settings settings = ReadSettings();

        // The tome pairs first, as the panel's StartScan does: a new generation restarts the pass this scan then
        // waits for, instead of restarting it in the scan's first step and leaving the scan without its marks
        constexpr auto kRebuildWait = std::chrono::seconds(10);
        if (!RunOnGameThreadAndWait("SpellTomesBeforeAutoScan", [] {
                SpellTomes::Rebuild(SpellTomes::kReasonScan);
                return true;
            }, kRebuildWait)) {
            logger::warn("{} the tome pairs were not read again before the scan (game thread did not answer) - a new "
                         "generation may restart the tome reach pass during it", kPrefix);
        }
        // No limit: a scan before the pass ends carries no tomeUnreachable marks and is not
        // recorded, so on a load order whose pass always took longer than a fixed limit every
        // start would scan again. The pass always ends (TomeReach.cpp: Finish on every path).
        // A save loaded meanwhile does not stop the wait: the form reads run during play too.
        for (int waits = 1; TomeReach::OutcomeKey().empty(); ++waits) {
            if (!TomeReach::WaitUntilDone(kTomeReachWait) && TomeReach::OutcomeKey().empty()) {
                logger::info("{} still waiting for the tome reach pass ({} s)", kPrefix,
                    waits * std::chrono::duration_cast<std::chrono::seconds>(kTomeReachWait).count());
            }
        }

        // A journal an earlier graft left is settled whatever runs this session (it only finishes files it wrote)
        ResolveTreeGraftJournal(loadOrder);
        if (const auto reason = Precondition()) {
            logger::info("{} {}", kPrefix, *reason);
            if (settings.selfTest) logger::info("{} not run: {}", kTestPrefix, *reason);
            return State::Done;
        }
        // A graft due without the catalog scan's dump scans instead: the skip would stop it every start (Ruling 22)
        if (!settings.selfTest && ScanRecord::CatalogMatches(loadOrder) && !TreeGraftNeedsTheScan(loadOrder)) {
            logger::info("{} skipped - load order unchanged ({}, scan fingerprint {}), the catalog is its scan's", kPrefix,
                loadOrder.hash, loadOrder.scan);
            // A graft an earlier start did not make (the catalog already is this load order's: no previous one)
            RunTreeGraftAtMainMenu(loadOrder, std::nullopt);
            return State::Done;
        }
        if (settings.selfTest) logger::info("{} on (selftest.json) - runs whatever the scan record says", kTestPrefix);

        const std::string treeBefore = settings.selfTest ? FileFingerprint(kTreeFile) : std::string();
        std::string treeRecordBefore = settings.selfTest ? FileFingerprint(kTreeRecordFile) : std::string();

        FrameProbe probe;
        bool probing = false;
        if (settings.selfTest && probe.Start()) {
            probing = true;
            std::this_thread::sleep_for(kBaseline);
            probe.SetWindow(1);
        }

        ScanTurn turn;
        if (!turn.Take()) {
            logger::info("{} a scan is running - tried again at the next main menu", kPrefix);
            return State::Idle;
        }

        logger::info("{} load order {} (scan fingerprint {}, {} plugins, {} spell forms) is not the catalog's - "
                     "scanning every spell (steps of {:.1f} ms, one a frame)", kPrefix, loadOrder.hash, loadOrder.scan,
            loadOrder.plugins, loadOrder.spells, settings.sliceMs);

        // The generation is the one the form reads start in: a save loaded before
        // them is no reason to drop them, one loaded during them is. A dropped run
        // starts again at once - its first step waits until the load is done
        std::shared_ptr<Run> run;
        StepResult result = StepResult::NotYet;
        const auto collectStarted = Clock::now();
        for (int attempt = 1; attempt <= kAttempts; ++attempt) {
            run = std::make_shared<Run>();
            run->generation = s_generation.load();
            run->slice = std::chrono::microseconds(static_cast<long long>(settings.sliceMs * 1000.0));
            result = Collect(run);
            if (result != StepResult::Dropped || attempt == kAttempts) break;
            logger::info("{} the form reads were dropped (a save load or a new game) - starting again ({} of {})",
                kPrefix, attempt + 1, kAttempts);
        }
        const double collectMs = MsSince(collectStarted);
        if (probing) probe.Stop();

        if (result != StepResult::Finished) {
            const std::string why = result == StepResult::Dropped
                                        ? std::format("dropped {} times (save loads or new games)", kAttempts)
                                        : std::string("stopped (see above)");
            logger::info("{} the form reads were {} after {:.0f} ms - tried again at the next main menu", kPrefix, why,
                collectMs);
            if (settings.selfTest) {
                TestCounts counts;
                counts.Report("RUN", false, "the form reads were " + why);
                logger::info("{} done - {} pass, {} fail, {} skip (the other checks need a finished run)", kTestPrefix,
                    counts.pass, counts.fail, counts.skip);
            }
            return State::Idle;
        }
        logger::info("{} form reads done in {:.0f} ms: {} steps, the longest {:.1f} ms ({}), {} waits for the next "
                     "frame, {} steps forced", kPrefix, collectMs, run->steps, run->longestStepMs, run->longestStepPhase,
            run->frameWaits, run->forcedSteps);

        // Worker side: strings and files only, no game form (as PrepareScanText)
        auto output = std::move(run->output);
        const std::size_t forcedSteps = run->forcedSteps;
        const std::uint32_t generation = run->generation;   // the form reads' own
        run.reset();
        const auto textStarted = Clock::now();
        std::string chunkedDump;
        if (settings.selfTest) chunkedDump = ComparableDump(*output);
        // A scan that could not mark everything still writes the catalog (better than the old one) but
        // no record: the next game start scans again
        const std::string incomplete = ScanRecordRules::IncompleteOf(*output);
        const std::string scanPairs = ScanRecordRules::TomePairsOf(*output);
        std::string text = JsonText::Dump(*output, 2);
        output.reset();
        // The catalog this scan replaces: the tree record's seen list when it has none
        auto previous = ReadCatalogForTreeGraft(loadOrder);
        // The seen list seeded is no graft's write: the TREE claim compares the record from here
        if (settings.selfTest && previous.seeded) treeRecordBefore = FileFingerprint(kTreeRecordFile);
        std::string recordId;   // the scan record's writeId: its dump goes only into that record
        const bool wrote = Librarian::ClassifyScan(text, true, [&loadOrder, &incomplete, &scanPairs, &recordId]() {
            recordId = ScanRecord::CatalogWritten(true, &loadOrder, incomplete, scanPairs);
        });
        const std::string dumpPath = SpellScanner::WriteScanOutput(text);
        // Both files are this scan's job: without the dump the next start does it again
        if (wrote && dumpPath.empty()) ScanRecord::Forget("spell_scan_output.json could not be written");
        // The tree graft reads the dump only when it is this scan's (a panel scan records no dump)
        if (wrote && !dumpPath.empty() && !recordId.empty()) ScanRecord::DumpWritten(recordId, text);
        turn.Release();
        logger::info("{} {} in {:.0f} ms; {} {} (total {:.1f} s since the main menu)", kPrefix,
            wrote ? "catalog written" : "catalog NOT written (see Librarian above)", MsSince(textStarted),
            dumpPath.empty() ? "spell_scan_output.json NOT written" : "scan output written to", dumpPath,
            MsSince(started) / 1000.0);

        if (wrote && !incomplete.empty()) {
            logger::warn("{} the catalog was written but not recorded ({}) - the next game start scans again",
                kPrefix, incomplete);
        }

        if (wrote && s_generation.load() != generation) {
            // Spell Research read the old catalog at that load; it reads the file again at the next one
            logger::info("{} a save was loaded during the scan - Spell Research takes the new catalog at the next "
                         "save load", kPrefix);
        }
        text.clear();
        text.shrink_to_fit();
        RunTreeGraftAtMainMenu(loadOrder, previous.catalog);
        previous.catalog.reset();

        if (settings.selfTest) {
            TestCounts counts;
            // A complete scan leaves a record that matches, an incomplete one none
            const bool matches = ScanRecord::CatalogMatches(loadOrder);
            counts.Report("RUN", wrote && matches == incomplete.empty(),
                std::format("catalog written {}, record matches scan fingerprint {} {} ({})", wrote, loadOrder.scan,
                    matches, incomplete.empty() ? "complete scan" : "incomplete scan: " + incomplete));
            counts.Report("COMPLETE", incomplete.empty(),
                std::format("vampire casters {}, {}", CastersName(SpellScanner::VampireCastersState()),
                    incomplete.empty() ? std::string("every mark made") : incomplete));
            const std::string treeAfter = FileFingerprint(kTreeFile);
            const std::string treeRecordAfter = FileFingerprint(kTreeRecordFile);
            // Unchanged, or changed by a graft whose checks passed (the tree-graft self-test has the rest)
            const auto graft = LastTreeGraft();
            const bool unchanged = treeAfter == treeBefore && treeRecordAfter == treeRecordBefore;
            counts.Report("TREE", unchanged || (graft.wrote && graft.passed),
                std::format("spell_tree.json {} -> {}, spell_tree_loadorder.json {} -> {}{}; graft: {}{}", treeBefore,
                    treeAfter, treeRecordBefore, treeRecordAfter, previous.seeded ? " (from after the seen list seeded)" : "",
                    graft.wrote ? "wrote, " : "", graft.detail));
            if (probing) {
                const auto baseline = probe.Window(0);
                const auto scan = probe.Window(1);
                if (baseline.frames == 0 || scan.frames == 0) {
                    counts.Skip("HITCH", std::format("the engine's frame time did not move ({} baseline, {} scan frames)",
                        baseline.frames, scan.frames));
                } else {
                    const double limit = baseline.longestFrameMs + settings.budgetMs;
                    // A forced step may have shared a frame with the one before it
                    counts.Report("HITCH", scan.longestFrameMs <= limit && forcedSteps == 0,
                        std::format("longest frame {} ms during the scan ({} frames, longest probe wait {:.1f} ms) "
                                    "against {} ms at the main menu before it ({} frames) + {:.0f} ms budget; "
                                    "{} steps forced",
                            scan.longestFrameMs, scan.frames, scan.longestWaitMs, baseline.longestFrameMs,
                            baseline.frames, settings.budgetMs, forcedSteps));
                }
            } else {
                counts.Skip("HITCH", "no frame probe thread");
            }
            const auto oneShot = OneShotDump();
            counts.Report("SAME", oneShot && *oneShot == chunkedDump,
                oneShot ? std::format("steps {} bytes, one task {} bytes{}", chunkedDump.size(), oneShot->size(),
                              *oneShot == chunkedDump ? ", equal" : ", DIFFERENT")
                        : std::string("the one-task scan did not answer"));
            counts.Report("DROP", StaleRunIsDropped(), "a run behind the session's generation reads no form");
            const std::string catalogBefore = FileFingerprint(kCatalogFile);
            std::string midway;
            const bool dropped = RunDroppedMidway(midway);
            const std::string catalogAfter = FileFingerprint(kCatalogFile);
            counts.Report("DROP-MIDWAY", dropped && catalogAfter == catalogBefore,
                std::format("{}; spell_catalog.json {} -> {}", midway, catalogBefore, catalogAfter));
            logger::info("{} done - {} pass, {} fail, {} skip", kTestPrefix, counts.pass, counts.fail, counts.skip);
        }
        return State::Done;
    }

    // Game thread: the first main menu of a session (or the next after a run that did not finish)
    void Begin()
    {
        State expected = State::Idle;
        if (!s_state.compare_exchange_strong(expected, State::Running)) return;

        try {
            const auto& loadOrder = LoadOrderFingerprint::Current();
            if (loadOrder.hash.empty()) {
                logger::warn("{} the load order is unknown - no scan at game start this session", kPrefix);
                s_state = State::Done;
                return;
            }
            std::thread([loadOrder]() {
                // Nothing may leave a thread function: it would end the game (std::terminate)
                State next = State::Idle;
                try {
                    next = RunScan(loadOrder);
                } catch (const std::exception& e) {
                    logger::error("{} the scan failed: {} - tried again at the next main menu", kPrefix, e.what());
                } catch (...) {
                    logger::error("{} the scan failed (unknown error) - tried again at the next main menu", kPrefix);
                }
                s_state = next;
            }).detach();
        } catch (const std::exception& e) {
            logger::warn("{} the scan could not start ({}) - tried again at the next main menu", kPrefix, e.what());
            s_state = State::Idle;
        } catch (...) {
            logger::warn("{} the scan could not start - tried again at the next main menu", kPrefix);
            s_state = State::Idle;
        }
    }

    class MainMenuWatch final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MainMenuWatch* GetSingleton()
        {
            static MainMenuWatch singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (event && event->opening && event->menuName == RE::MainMenu::MENU_NAME) {
                AddTaskToGameThread("AutoScanMainMenu", []() { Begin(); });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        MainMenuWatch(const MainMenuWatch&) = delete;
        MainMenuWatch& operator=(const MainMenuWatch&) = delete;

    private:
        MainMenuWatch() = default;
    };
}

namespace AutoScan
{
    void Install()
    {
        static bool s_installed = false;
        if (s_installed) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::warn("{} no UI to watch the main menu - no scan at game start", kPrefix);
            return;
        }
        ui->AddEventSink<RE::MenuOpenCloseEvent>(MainMenuWatch::GetSingleton());
        s_installed = true;
        // The data can finish loading with the main menu already up: its opening went by
        if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
            AddTaskToGameThread("AutoScanMainMenu", []() { Begin(); });
        }
    }

    void OnGameChanging() noexcept
    {
        s_generation.fetch_add(1);
    }
}
