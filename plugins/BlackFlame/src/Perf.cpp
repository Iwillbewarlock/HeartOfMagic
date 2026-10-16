#include "Perf.h"

#include <array>
#include <format>

namespace BlackFlame::Perf
{
    namespace
    {
        constexpr std::array<const char*, kCount> kNames{ "drain", "explosion update", "hazard queue", "papyrus (stoked)",
            "papyrus (not stoked)", "launch", "hit shader", "hit art", "log limit" };

        std::array<std::atomic<std::uint64_t>, kCount> g_calls{};
        std::array<std::atomic<std::int64_t>, kCount> g_ticks{};

        std::int64_t Frequency() noexcept
        {
            static const std::int64_t frequency = [] {
                std::int64_t value = 0;
                return REX::W32::QueryPerformanceFrequency(&value) && value > 0 ? value : 1;
            }();
            return frequency;
        }
    }

    std::int64_t Now() noexcept
    {
        std::int64_t value = 0;
        (void)REX::W32::QueryPerformanceCounter(&value);
        return value;
    }

    void Add(Counter a_counter, std::int64_t a_ticks) noexcept
    {
        g_calls[a_counter].fetch_add(1, std::memory_order_relaxed);
        g_ticks[a_counter].fetch_add(a_ticks, std::memory_order_relaxed);
    }

    std::string TakeWindow()
    {
        const double msPerTick = 1000.0 / static_cast<double>(Frequency());
        std::string out;
        for (std::size_t i = 0; i < kCount; ++i) {
            const auto calls = g_calls[i].exchange(0, std::memory_order_relaxed);
            const auto ticks = g_ticks[i].exchange(0, std::memory_order_relaxed);
            if (calls == 0 && i != kDrain) {
                continue;
            }
            out += std::format("{}{} {} calls {:.2f} ms", out.empty() ? "" : ", ", kNames[i], calls,
                static_cast<double>(ticks) * msPerTick);
        }
        return out;
    }
}
