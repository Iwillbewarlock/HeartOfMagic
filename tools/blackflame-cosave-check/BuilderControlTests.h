#pragma once

// =============================================================================
// BUILDER CONTROL TESTS - the throttle's CPU rate and the builder's exit code in words (BuilderControl.h)
// =============================================================================
// The DLL's copies of the builder's exit codes are pinned to the builder's own (GreyBuild.h, GreyBudget.h: both include only
// the standard library). Also (review E2): the throttle's hand-off when the launcher's lock is busy (RunOrHandOff,
// TakeOnce, on a mutex whose try_lock the test decides and on a std::mutex another thread holds) and, round 2, the request
// folder's mutex (BuilderFolderMutex.h: the name's normalisation pinned, the decision, and real named mutexes made twice
// as two game processes would). Included by main.cpp.

#include "../../plugins/BlackFlame/src/BuilderControl.h"
#include "../../plugins/BlackFlame/src/BuilderFolderMutex.h"
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

        // TakeThrottle: a direct take (the free lock, the pool thread) never touches the flag; a pending one consumes it;
        // once a session either way
        std::atomic<bool> flag{ false };
        bool taken = false;
        const bool direct = BC::TakeThrottle(false, flag, taken);
        const bool directAgain = BC::TakeThrottle(false, flag, taken);
        const bool flagUntouched = !flag.load();
        flag.store(true);
        const bool pendingAfter = BC::TakeThrottle(true, flag, taken);
        a_check(direct && !directAgain && flagUntouched && !pendingAfter && !flag.load() && taken,
            "throttle take: a direct take runs once and sets no flag; a request published after it is consumed, not run again");
        bool fresh = false;
        std::atomic<bool> unset{ false };
        const bool pendingUnset = BC::TakeThrottle(true, unset, fresh);
        unset.store(true);
        const bool pendingSet = BC::TakeThrottle(true, unset, fresh);
        const bool directLater = BC::TakeThrottle(false, unset, fresh);
        a_check(!pendingUnset && pendingSet && !unset.load() && !directLater && fresh,
            "throttle take: a pending take needs the published request, runs once, and a later direct take does not run again");

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
    void FolderMutex(Check&& a_check)
    {
        namespace M = BlackFlame::BuilderFolderMutex;
        namespace CC = BlackFlame::CopyContract;
        // the name: the normalised path's FNV-1a 64; case, '/', a trailing separator and "." segments do not change it
        const std::filesystem::path folder = L"C:\\Games\\Skyrim\\SKSE\\HeartOfMagic_BlackFlame";
        const std::string normal = "c:\\games\\skyrim\\skse\\heartofmagic_blackflame";
        const auto expected = std::wstring(M::kNamePrefix) + [&]() {
            const auto key = CC::Hex16(CC::Fnv64(normal));
            return std::wstring(key.begin(), key.end());
        }();
        a_check(M::NormalisedFolder(folder) == normal && M::MutexName(folder) == expected,
            "folder mutex: the name is Local\\HoMBlackFlame_Builder_ and the FNV-1a 64 of the lower-case path (" + normal + ")");
        const bool same = M::MutexName(L"c:/games/SKYRIM/skse/HeartOfMagic_BlackFlame/") == expected &&
                          M::MutexName(L"C:\\Games\\Skyrim\\.\\SKSE\\x\\..\\HeartOfMagic_BlackFlame\\") == expected;
        const bool other = M::MutexName(L"C:\\Games\\Skyrim\\SKSE\\HeartOfMagic_BlackFlame2") != expected;
        const bool unicode = M::MutexName(L"C:\\Spiele\\\u00DCBER") == M::MutexName(L"c:\\spiele\\\u00FCber") &&
                             M::NormalisedFolder(L"C:\\Spiele\\\u00DCBER") == "c:\\spiele\\\xC3\xBC" "ber";
        a_check(same && other && unicode,
            "folder mutex: '/', case (non-ASCII too: U+00DC as U+00FC), a trailing separator and . or .. segments give the same name; "
            "another folder another");
        // the decision
        a_check(M::FromCreate(true, ERROR_SUCCESS) == M::Claim::kOwned && M::FromCreate(true, ERROR_ALREADY_EXISTS) == M::Claim::kOtherGame &&
                    M::FromCreate(false, ERROR_ACCESS_DENIED) == M::Claim::kOtherGame &&
                    M::FromCreate(false, ERROR_INVALID_HANDLE) == M::Claim::kUnknown && M::MayStart(M::Claim::kOwned) &&
                    M::MayStart(M::Claim::kUnknown) && !M::MayStart(M::Claim::kOtherGame),
            "folder mutex: made - owned; there already (183) or another integrity level's (5) - another game, no builder; any other "
            "failure - not excluded, the builder starts");
        // real named mutexes, made as two game processes on one folder would (a second create in one process sees the
        // first one's object the same way)
        std::error_code ec;
        const auto real = std::filesystem::temp_directory_path(ec) /
                          ("blackflame-cosave-check-mutex-" + std::to_string(::GetCurrentProcessId()));
        HANDLE first = nullptr;
        HANDLE second = nullptr;
        HANDLE third = nullptr;
        DWORD error = 0;
        const auto firstClaim = M::Acquire(real, first, error);
        const auto secondClaim = M::Acquire(real.wstring() + L"\\", second, error);
        const DWORD secondError = error;
        if (first) {
            ::CloseHandle(first);
        }
        const auto thirdClaim = M::Acquire(real, third, error);
        if (third) {
            ::CloseHandle(third);
        }
        a_check(firstClaim == M::Claim::kOwned && first && secondClaim == M::Claim::kOtherGame && !second &&
                    secondError == ERROR_ALREADY_EXISTS && thirdClaim == M::Claim::kOwned && third,
            "folder mutex: a real one - the first game owns it, a second on the same folder finds it (Windows error " +
                std::to_string(secondError) + ", its handle closed), and once the first is gone a game owns it again");
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
        FolderMutex(a_check);
    }
}
