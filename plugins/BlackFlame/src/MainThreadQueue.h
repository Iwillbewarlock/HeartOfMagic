#pragma once

#include "Common.h"

#include <cstdint>
#include <functional>

// =============================================================================
// MAIN-THREAD QUEUE (v2 fix D1) - our own per-frame drain on the main thread
// =============================================================================
// SKSE's AddTask is drained in two places (SKSE Hooks_Threads.cpp, research-mainthread notes):
// Main::Update +0xB9C/+0xBAE (main thread, but only while the game is paused) and inside the
// BSJobs "Post process" job (39092 -> 36588), which runs on job worker threads during normal play,
// at the same time as the main thread's part of Main::Update. Run 11:11 showed it: 6 of 415 task
// bodies on the main thread (paused frames), the rest on 7 worker threads.
//
// So every engine change of ours runs from this queue instead: a write_call<5> over the E8 at
// Main::Update (SE 35565 / AE 36564) +0x748 (SE) / +0xC26 (AE before 1.7.99) / +0xC38 (1.7.99 and
// later), the site Open Animation Replacer, Precision and True Directional Movement hook. It is
// after the frame's job wait (35409), so every job list of the frame has finished, and it runs on
// every Main::Update, paused or not, on the main thread. The thunk calls the original first, then
// drains: the pending list swapped out under a TrackedMutex, each task run outside the lock with
// its own try/catch and trace scope.
//
// Checked before the write: the byte is E8, its target is executable and inside the game image or
// an SKSE/CommonLib trampoline stub (another plugin's redirect, chained), and on AE the site sits
// 0x37 bytes before the function's end (pdata; equal on 1.6.1170 and 1.7.104). Otherwise nothing
// is written and Post falls back to SKSE's AddTask (logged once).

namespace BlackFlame::MainQueue
{
    /// SKSEPlugin_Load, after SKSE::Init (the trampoline): checks the site and writes the call
    void Install();

    [[nodiscard]] bool Installed() noexcept;

    /// The trace kind of a drain that ran tasks (ThreadTrace.h; G1: drains without tasks are not recorded)
    inline constexpr const char* kDrainKind = "main-thread drain";

    /// Any thread: runs a_task on the main thread at the end of the next Main::Update (or through SKSE's
    /// AddTask when the hook is not installed). a_name is a string literal (thread logs and the trace).
    /// False when nothing was queued (G3: a kind stopped for the session) - a caller holding a "queued" flag clears it.
    bool Post(const char* a_name, std::function<void()> a_task);

    /// Whether a drain is running now (any thread; G1)
    [[nodiscard]] bool Draining() noexcept;

    /// G1: at the entry of a job-side hook body (Launch, explosion creation, GetImpactData and Explosion::Update in a copy
    /// window): counts the call when a drain is running and the caller is not the main thread
    void NoteJobSideHook() noexcept;

    struct Stats
    {
        std::uint64_t frames = 0;         // drains run (one per Main::Update)
        std::uint64_t pausedFrames = 0;   // of those, with the game paused (UI::GameIsPaused, read in the drain)
        std::uint64_t bodies = 0;         // tasks run from the queue
        std::uint64_t bodiesOffMain = 0;  // of those, on another thread than Main::threadID (0 when the hook is right)
        std::uint64_t overlaps = 0;       // a drain began while another was running (0 when the hook is right)
        std::uint64_t fallbacks = 0;      // posts sent to SKSE's AddTask because the hook is not installed
        std::uint64_t jobHooksDuringDrain = 0;  // G1: job-side hook bodies entered on a worker thread during a drain
        std::uint32_t thread = 0;         // the drain's thread (the first frame's)
        std::uint32_t mainThreadId = 0;   // RE::Main::threadID read in the first frame
    };

    [[nodiscard]] Stats GetStats() noexcept;

    /// steady_clock ticks of the end of the last drain, 0 before the first (the watchdog's stall signal)
    [[nodiscard]] std::int64_t LastFrameEnd() noexcept;

    /// Whether the last drain saw the game paused (written by the drain; read by the watchdog for its message)
    [[nodiscard]] bool LastFramePaused() noexcept;

    /// Tasks waiting for the next drain
    [[nodiscard]] std::size_t Pending() noexcept;
}
