#include "GameClock.h"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace BlackFlame::GameClock
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto kMaxFrameSeconds = std::chrono::seconds(1);

        std::atomic<std::int64_t> g_unpausedTicks{ 0 };  // steady_clock ticks of unpaused frames
        std::atomic<bool> g_advancing{ false };
        Clock::time_point g_lastFrame{};  // the drain thread only
        bool g_haveLast = false;          // the drain thread only
    }

    void OnDrainFrame(bool a_paused) noexcept
    {
        const auto now = Clock::now();
        if (g_haveLast && !a_paused) {
            const auto delta = std::clamp<Clock::duration>(now - g_lastFrame, Clock::duration::zero(),
                std::chrono::duration_cast<Clock::duration>(kMaxFrameSeconds));
            g_unpausedTicks.fetch_add(delta.count(), std::memory_order_relaxed);
        }
        if (g_haveLast) {
            g_advancing.store(true, std::memory_order_relaxed);
        }
        g_lastFrame = now;
        g_haveLast = true;
    }

    double Seconds() noexcept
    {
        return std::chrono::duration<double>(Clock::duration(g_unpausedTicks.load(std::memory_order_relaxed))).count();
    }

    bool Advancing() noexcept
    {
        return g_advancing.load(std::memory_order_relaxed);
    }
}
