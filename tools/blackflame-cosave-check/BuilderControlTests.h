#pragma once

// =============================================================================
// BUILDER CONTROL TESTS - the throttle's CPU rate and the builder's exit code in words (BuilderControl.h)
// =============================================================================
// The DLL's copies of the builder's exit codes are pinned to the builder's own (GreyBuild.h, GreyBudget.h: both include only
// the standard library). Also (review E2): the throttle's hand-off when the launcher's lock is busy (RunOrHandOff,
// TakeOnce, on a mutex whose try_lock the test decides and on a std::mutex another thread holds) and the live-builder check
// (BuilderLogProbe.h: the decision, and real files in the system temp folder opened as the builder holds its log). Included
// by main.cpp.

#include "../../plugins/BlackFlame/src/BuilderControl.h"
#include "../../plugins/BlackFlame/src/BuilderLogProbe.h"
#include "../blackflame-greybuild/GreyBudget.h"
#include "../blackflame-greybuild/GreyBuild.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace BuilderControlTests
{
    namespace BC = BlackFlame::BuilderControl;

    static_assert(BC::kExitOk == static_cast<std::uint32_t>(GreyBuild::kExitOk), "the builder's kExitOk moved");
    static_assert(BC::kExitFail == static_cast<std::uint32_t>(GreyBuild::kExitFail), "the builder's kExitFail moved");
    static_assert(BC::kExitUsage == static_cast<std::uint32_t>(GreyBuild::kExitUsage), "the builder's kExitUsage moved");
    static_assert(BC::kExitParentGone == static_cast<std::uint32_t>(GreyBuild::kExitParentGone), "the builder's kExitParentGone moved");

    template <class Check>
    void Rate(Check&& a_check, std::uint32_t a_cpus, std::uint32_t a_expected, const std::string& a_what)
    {
        const auto rate = BC::CpuRateForOneCpu(a_cpus);
        a_check(rate == a_expected && rate != 0,
            "cpu rate: " + a_what + " (" + std::to_string(a_cpus) + " logical CPUs -> " + std::to_string(rate) + "/10000)");
    }

    template <class Check>
    void Exit(Check&& a_check, std::uint32_t a_code, const std::string& a_expected, const std::string& a_what)
    {
        const auto text = BC::ExitCodeText(a_code);
        a_check(text == a_expected, "exit code: " + a_what + " ('" + text + "')");
    }

    // A mutex whose try_lock the test decides (no thread needed); counts what was asked of it
    struct FakeMutex
    {
        bool free = true;
        int locks = 0;
        int unlocks = 0;
        bool try_lock()
        {
            locks += free ? 1 : 0;
            return free;
        }
        void lock() { ++locks; }
        void unlock() { ++unlocks; }
    };

    template <class Check>
    void HandOffs(Check&& a_check)
    {
        using BC::HandOff;
        FakeMutex freeLock;
        int ranHere = 0;
        int submitted = 0;
        bool unlockedBeforeSlowPart = false;
        const auto here = BC::RunOrHandOff(
            freeLock,
            [&](std::unique_lock<FakeMutex>& a_lock) {
                ++ranHere;
                a_lock.unlock();
                unlockedBeforeSlowPart = freeLock.unlocks == 1;
            },
            [&]() { return ++submitted, true; });
        a_check(here == HandOff::kRanHere && ranHere == 1 && submitted == 0 && unlockedBeforeSlowPart && freeLock.unlocks == 1,
            "throttle hand-off: a free lock - the job runs on the caller's thread and the lock is released before its slow part");

        FakeMutex busy;
        busy.free = false;
        ranHere = submitted = 0;
        const auto handed = BC::RunOrHandOff(busy, [&](std::unique_lock<FakeMutex>&) { ++ranHere; }, [&]() { return ++submitted, true; });
        a_check(handed == HandOff::kHandedOff && ranHere == 0 && submitted == 1 && busy.locks == 0 && busy.unlocks == 0,
            "throttle hand-off: a busy lock - never waited on, the job handed to another thread");
        const auto refused = BC::RunOrHandOff(busy, [&](std::unique_lock<FakeMutex>&) { ++ranHere; }, [&]() { return ++submitted, false; });
        a_check(refused == HandOff::kPending && ranHere == 0 && submitted == 2,
            "throttle hand-off: a busy lock and the hand-off refused - pending, not run and not dropped silently");

        std::atomic<bool> requested{ false };
        bool done = false;
        const bool notAsked = BC::TakeOnce(requested, done);
        requested.store(true);
        const bool first = BC::TakeOnce(requested, done);
        const bool consumed = !requested.load();
        requested.store(true);
        const bool second = BC::TakeOnce(requested, done);
        a_check(!notAsked && first && consumed && !second && done && !requested.load(),
            "throttle hand-off: the request is taken once (not before it is made, never a second time; consumed either way)");

        // A real std::mutex held by another thread: RunOrHandOff returns while it is still held
        std::mutex real;
        std::atomic<bool> holding{ false };
        std::atomic<bool> release{ false };
        std::thread holder([&]() {
            std::lock_guard<std::mutex> lock(real);
            holding.store(true);
            while (!release.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        while (!holding.load()) {
            std::this_thread::yield();
        }
        ranHere = submitted = 0;
        const auto start = std::chrono::steady_clock::now();
        const auto live = BC::RunOrHandOff(real, [&](std::unique_lock<std::mutex>&) { ++ranHere; }, [&]() { return ++submitted, true; });
        const auto took = std::chrono::steady_clock::now() - start;
        const bool stillHeld = !release.load();
        release.store(true);
        holder.join();
        a_check(live == HandOff::kHandedOff && ranHere == 0 && submitted == 1 && stillHeld && took < std::chrono::milliseconds(100),
            "throttle hand-off: a std::mutex another thread holds - handed off at once (" +
                std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(took).count()) + " us), no wait");
    }

    template <class Check>
    void LogProbe(Check&& a_check)
    {
        namespace P = BlackFlame::BuilderLogProbe;
        a_check(P::FromError(ERROR_SUCCESS) == P::State::kFree && P::FromError(ERROR_FILE_NOT_FOUND) == P::State::kNoLog &&
                    P::FromError(ERROR_PATH_NOT_FOUND) == P::State::kNoLog &&
                    P::FromError(ERROR_SHARING_VIOLATION) == P::State::kHeld && P::FromError(ERROR_LOCK_VIOLATION) == P::State::kHeld &&
                    P::FromError(ERROR_ACCESS_DENIED) == P::State::kUnknown,
            "live builder: Windows errors read as no log (2, 3), free (0), held (32, 33), unknown (5)");
        a_check(P::MayStart(P::State::kNoLog) && P::MayStart(P::State::kFree) && P::MayStart(P::State::kUnknown) &&
                    !P::MayStart(P::State::kHeld),
            "live builder: only a held builder.log keeps a second builder from starting");

        std::error_code ec;
        const auto folder = std::filesystem::temp_directory_path(ec) /
                            ("blackflame-cosave-check-probe-" + std::to_string(::GetCurrentProcessId()));
        std::filesystem::create_directories(folder, ec);
        const auto log = folder / "builder.log";
        DWORD error = 0;
        const auto none = P::Probe(log, error);
        a_check(none == P::State::kNoLog, "live builder: no builder.log - no log (Windows error " + std::to_string(error) + ")");
        std::ofstream(log, std::ios::binary) << "an earlier builder's output\n";
        const auto ended = P::Probe(log, error);
        // As the builder holds it: the handle the game makes in MakeLog and the child inherits
        const HANDLE builder = ::CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto held = builder != INVALID_HANDLE_VALUE ? P::Probe(log, error) : P::State::kUnknown;
        const DWORD heldError = error;
        if (builder != INVALID_HANDLE_VALUE) {
            ::CloseHandle(builder);
        }
        // A reader that shares write (a log viewer, tail): not a live builder
        const HANDLE viewer = ::CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto viewed = viewer != INVALID_HANDLE_VALUE ? P::Probe(log, error) : P::State::kUnknown;
        if (viewer != INVALID_HANDLE_VALUE) {
            ::CloseHandle(viewer);
        }
        const auto after = P::Probe(log, error);
        std::string kept;
        {
            std::ifstream back(log, std::ios::binary);
            std::getline(back, kept);
        }
        a_check(ended == P::State::kFree && held == P::State::kHeld && heldError == ERROR_SHARING_VIOLATION &&
                    viewed == P::State::kFree && after == P::State::kFree && kept == "an earlier builder's output",
            "live builder: a real builder.log - free when no one writes it, held (Windows error " + std::to_string(heldError) +
                ") while a handle opened as the builder's is open, free beside a viewer, free again after; the probe never "
                "changes the file");
        std::filesystem::remove_all(folder, ec);
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        Rate(a_check, 1, 10000, "one CPU is the whole machine");
        Rate(a_check, 4, 2500, "4");
        Rate(a_check, 16, 625, "16 (this machine)");
        Rate(a_check, 64, 156, "64 (rounded down)");
        Rate(a_check, 3, 3333, "3 (rounded down)");
        Rate(a_check, 0, 10000, "0 read as one CPU, never a rate of 0");
        Rate(a_check, 20000, 1, "more CPUs than 10000: 1, never 0");

        Exit(a_check, 0, "0 (ok: every object has a line)", "0 ok");
        Exit(a_check, 1, "1 (failed: the run could not go on; builder.log says why)", "kExitFail");
        Exit(a_check, 2, "2 (refused: bad arguments or a request that is not version 5)", "kExitUsage");
        Exit(a_check, 3, "3 (stopped: the parent process is gone)", "kExitParentGone");
        Exit(a_check, 0xC0000005u, "0xC0000005 (terminated or crashed)", "an access violation");
        Exit(a_check, 4, "0x00000004 (terminated or crashed)", "an unknown small code");
        Exit(a_check, 0xFFFFFFFFu, "0xFFFFFFFF (terminated or crashed)", "the largest code");

        HandOffs(a_check);
        LogProbe(a_check);
    }
}
