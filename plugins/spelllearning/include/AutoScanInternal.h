#pragma once

#include "ElapsedMs.h"
#include "LoadOrderFingerprint.h"
#include "SpellScanner.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

// =============================================================================
// SCAN AT GAME START - what AutoScan.cpp (the run) and AutoScanSelfTest.cpp (its
// dev self-test) share. Not part of AutoScan.h: only those two include it.
// =============================================================================

namespace AutoScan::Internal
{
    using Clock = std::chrono::steady_clock;

    inline constexpr double kSliceDefaultMs = 4.0;          // game thread time of one step, at most (the marks step excepted)
    inline constexpr double kHitchBudgetDefaultMs = 25.0;   // self-test: the longest frame may exceed the main menu's by this much
    inline constexpr auto kNextFrameWait = std::chrono::milliseconds(1);

    inline constexpr const char* kPrefix = "AutoScan:";
    inline constexpr const char* kTestPrefix = "AutoScan SELFTEST:";

    /// The session's generation: a save load or a new game moves it (OnGameChanging); a run's
    /// steps check it before they read a form
    inline std::atomic<std::uint32_t> g_generation{ 0 };

    /// selftest.json's autoScan section (ReadSettings, AutoScan.cpp)
    struct Settings
    {
        bool selfTest = false;
        double sliceMs = kSliceDefaultMs;
        double budgetMs = kHitchBudgetDefaultMs;
    };

    std::optional<std::string> ReadFileText(const std::filesystem::path& path);

    // -------------------------------------------------------------------------
    // The steps (AutoScan.cpp)
    // -------------------------------------------------------------------------

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

    /// Waiter thread: one step on the game thread, and its answer
    StepResult PostStep(const std::shared_ptr<Run>& run);

    // -------------------------------------------------------------------------
    // The self-test (AutoScanSelfTest.cpp)
    // -------------------------------------------------------------------------

    /// FNV-1a 64 of a file and its size, "missing" when there is none (the tree files unchanged)
    std::string FileFingerprint(const std::filesystem::path& path);

    /// A dump with the scan timestamp blanked: two scans of one load order compare equal
    std::string ComparableDump(nlohmann::json output);

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

    /// The longest frame, from the engine's frame time (self-test only)
    class FrameProbe
    {
    public:
        FrameProbe() : m_data(std::make_shared<ProbeData>()) {}
        FrameProbe(const FrameProbe&) = delete;
        FrameProbe& operator=(const FrameProbe&) = delete;
        ~FrameProbe() { Stop(); }

        bool Start();
        void SetWindow(int window);
        void Stop();
        ProbeWindow Window(int window);

    private:
        std::shared_ptr<ProbeData> m_data;
        std::thread m_thread;
    };

    /// The PASS/FAIL lines' counts
    struct TestCounts
    {
        int pass = 0;
        int fail = 0;
        int skip = 0;

        void Report(const char* check, bool passed, const std::string& detail);
        void Skip(const char* check, const std::string& detail);
    };

    /// What a finished run hands the self-test's report
    struct SelfTestRun
    {
        const Settings& settings;
        const LoadOrderFingerprint::Value& loadOrder;
        bool wrote;                      // the catalog was written
        const std::string& incomplete;   // the marks the scan could not make (empty: none)
        const std::string& treeBefore;   // FileFingerprint of the tree and its record before
        const std::string& treeRecordBefore;
        bool seeded;                     // the seen list was seeded (treeRecordBefore is from after it)
        bool probing;                    // the frame probe ran
        std::size_t forcedSteps;
        const std::string& chunkedDump;  // ComparableDump of the run's output
    };

    /// The self-test's lines after a finished run: RUN, COMPLETE, TREE, HITCH, SAME, DROP, DROP-MIDWAY
    void ReportSelfTest(const SelfTestRun& run, FrameProbe& probe);
}
