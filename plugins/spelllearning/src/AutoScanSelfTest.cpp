#include "Common.h"
#include "AutoScanInternal.h"
#include "JsonText.h"
#include "ScanRecord.h"
#include "ScanRecordRules.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "uimanager/StartupState.h"

#include <algorithm>
#include <format>
#include <future>

// =============================================================================
// SCAN AT GAME START - its dev self-test (selftest.json "autoScan"; AutoScan.h)
// =============================================================================
//
// The frame probe (the longest frame during the scan against the main menu's own),
// the checks after a finished run and their PASS/FAIL lines. The run itself:
// AutoScan.cpp.

namespace
{
    using namespace AutoScan::Internal;

    constexpr auto kProbeInterval = std::chrono::milliseconds(2);
    constexpr auto kOneShotWait = std::chrono::seconds(60);

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
        if (future.wait_for(kOneShotWait) != std::future_status::ready) return std::nullopt;
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
        run->generation = g_generation.load() - 1;
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
        run->generation = g_generation.load();
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
        g_generation.fetch_add(1);
        const auto result = PostStep(run);
        detail = std::format("{} steps, then the generation moved: {}", stepped,
            result == StepResult::Dropped ? "dropped" : "NOT dropped");
        return result == StepResult::Dropped && !run->output;
    }

    const char* CastersName(ScanRecordRules::Casters casters)
    {
        switch (casters) {
            case ScanRecordRules::Casters::kNotRun: return "not collected";
            case ScanRecordRules::Casters::kComplete: return "collected";
            case ScanRecordRules::Casters::kFailed: return "FAILED";
        }
        return "unknown";
    }
}

namespace AutoScan::Internal
{
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

    std::string ComparableDump(nlohmann::json output)
    {
        output["scanTimestamp"] = "";
        return JsonText::Dump(output, 2);
    }

    bool FrameProbe::Start()
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

    void FrameProbe::SetWindow(int window)
    {
        std::lock_guard lock(m_data->mutex);
        m_data->window = window;
    }

    void FrameProbe::Stop()
    {
        m_data->stop.store(true);
        if (m_thread.joinable()) m_thread.join();
        SetWindow(2);   // probe tasks still queued count in neither window
    }

    ProbeWindow FrameProbe::Window(int window)
    {
        std::lock_guard lock(m_data->mutex);
        return m_data->windows[window];
    }

    void TestCounts::Report(const char* check, bool passed, const std::string& detail)
    {
        passed ? ++pass : ++fail;
        logger::info("{} {} {} - {}", kTestPrefix, check, passed ? "PASS" : "FAIL", detail);
    }

    void TestCounts::Skip(const char* check, const std::string& detail)
    {
        ++skip;
        logger::info("{} {} SKIP - {}", kTestPrefix, check, detail);
    }

    void ReportSelfTest(const SelfTestRun& run, FrameProbe& probe)
    {
        const auto& [settings, loadOrder, wrote, incomplete, treeBefore, treeRecordBefore, seeded, probing, forcedSteps,
            chunkedDump] = run;
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
                treeAfter, treeRecordBefore, treeRecordAfter, seeded ? " (from after the seen list seeded)" : "",
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
}
