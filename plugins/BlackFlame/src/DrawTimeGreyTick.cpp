#include "DrawTimeGreyShared.h"

#include "GameThread.h"
#include "SehGuard.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

// =============================================================================
// DRAW-TIME GREY - the 100 ms ticker and the tick's fault handling (DrawTimeGrey.h)
// =============================================================================
// A detached thread posts one tick task at a time to our main-thread queue; the task walks the set
// (DrawTimeGreySet.cpp TickSet). A faulted or throwing tick empties the set and is tried again;
// kMaxConsecutiveTickFaults in a row turn the tick and the binds off for the session.

namespace BlackFlame::DrawTime
{
    namespace
    {
        constexpr auto kTickInterval = std::chrono::milliseconds(100);
        constexpr int kMaxConsecutiveTickFaults = 3;  // then the tick (and the binds) are off for the session
        constexpr int kMaxFaultWarns = 10;            // tick-fault warning lines per session

        std::atomic<bool> g_tickQueued{ false };
        std::atomic<bool> g_tickerStarted{ false };
        std::atomic<bool> g_tickOff{ false };
        int g_consecutiveTickFaults = 0;  // main thread only (RunTick)
        int g_faultWarns = 0;             // main thread only (RunTick)

        // Runs a_work under GuardedKeep with every C++ exception caught and logged (a queue task must not let one out)
        template <class F>
        bool RunCaught(const char* a_step, F&& a_work)
        {
            try {
                return GuardedKeep(a_step, std::forward<F>(a_work));
            } catch (const std::exception& e) {
                logger::error("BlackFlame: draw-time grey: {} threw: {}", a_step, e.what());
            } catch (...) {
                logger::error("BlackFlame: draw-time grey: {} threw an exception of an unknown type", a_step);
            }
            return false;
        }

        void TurnTickOff(bool a_cleared)
        {
            g_tickOff.store(true);
            StopBinding("the rewalk tick is off");  // no snapshot loads or lookups per pass from now on
            logger::error("BlackFlame: draw-time grey: the tick failed {} times in a row - rewalks off for the session "
                          "({}); the 5 s summary stops here",
                g_consecutiveTickFaults,
                a_cleared ? "the set was emptied" : "the clear failed too: the last set is held, but nothing binds");
        }

        void RunTick()
        {
            g_tickQueued.store(false);
            if (g_tickOff.load()) {
                return;
            }
            if (RunCaught("draw-time grey tick", [] { TickSet(); })) {
                g_consecutiveTickFaults = 0;
                return;
            }
            GetCounters().failures.fetch_add(1, std::memory_order_relaxed);
            bool cleared = false;
            if (!RunCaught("draw-time grey clear after a fault", [&cleared] { cleared = ClearSetAfterFault(); })) {
                cleared = false;  // the guard skipped the body's result
            }
            if (!cleared) {
                GetCounters().failures.fetch_add(1, std::memory_order_relaxed);
            }
            if (++g_consecutiveTickFaults >= kMaxConsecutiveTickFaults) {
                TurnTickOff(cleared);
            } else if (g_faultWarns < kMaxFaultWarns) {
                ++g_faultWarns;
                logger::warn("BlackFlame: draw-time grey: a tick failed ({} of {} in a row) - the set {}; the next tick "
                             "walks again",
                    g_consecutiveTickFaults, kMaxConsecutiveTickFaults, cleared ? "was emptied" : "could not be emptied");
            }
        }

        // Detached, never joined (as BuffState.cpp's ticker): it only sleeps and posts; nothing leaves it (an exception
        // out of a thread function would end the game)
        void TickerLoop() noexcept
        {
            for (;;) {
                try {
                    std::this_thread::sleep_for(kTickInterval);
                } catch (...) {
                }
                PostTick();
            }
        }
    }

    void PostTick() noexcept
    {
        if (g_tickOff.load() || g_tickQueued.exchange(true)) {
            return;
        }
        try {
            if (QueueTask("BlackFlameDrawTimeTick", [] { RunTick(); })) {
                return;
            }
        } catch (const std::exception& e) {
            try {
                logger::error("BlackFlame: draw-time grey: posting the tick threw: {}", e.what());
            } catch (...) {
            }
        } catch (...) {
        }
        g_tickQueued.store(false);  // a failed post: the next one can go out
    }

    void StartSetTicker()
    {
        if (g_tickerStarted.exchange(true)) {
            return;
        }
        std::thread(&TickerLoop).detach();
        logger::info("BlackFlame: draw-time grey: rewalk ticker started ({} ms)", kTickInterval.count());
    }
}
