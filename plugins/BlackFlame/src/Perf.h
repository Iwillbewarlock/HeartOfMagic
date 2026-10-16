#pragma once

#include "Common.h"

#include <atomic>
#include <cstdint>
#include <string>

// =============================================================================
// PERF - our own cost per heartbeat window (perf audit, item 2)
// =============================================================================
// Hot hooks add their call count and the time spent in OUR part (not the engine's original) to relaxed atomic counters;
// nothing is logged per call. The heartbeat (MainThreadQueue.cpp, every 5 s) takes the window's totals and resets them.
// W16: the entries overlap (LogLimit runs inside the hooks, the drain holds the tasks), so they are not to be added up;
// the drain that logs the heartbeat counts its own time in the next window (its scope ends after the line).
// Timer: QueryPerformanceCounter (one call at each end).

namespace BlackFlame::Perf
{
    enum Counter : std::size_t
    {
        kDrain,            // the main-thread queue's drain (all our tasks of the frame)
        kExplosionUpdate,  // Explosion::Update hook, our parts before and after the original
        kHazardQueue,      // HazardHook Queue (Set3D / Initialize)
        kPapyrusStoked,    // native wrappers (PlaceAtMe, Cast, RemoteCast) while stoked: the caller is looked at
        kPapyrusIdle,      // the same while not stoked: one atomic read
        kLaunch,           // Projectile::Launch detour, our parts
        kHitShader,        // GetHitEffectShader (0x0D)
        kHitArt,           // GetHitEffectArt (0x0C)
        kLogLimit,         // LogLimit::Allow / FirstTime / NotYet
        kCount
    };

    [[nodiscard]] std::int64_t Now() noexcept;

    /// Adds one call and a_ticks (QPC ticks) to a_counter; any thread
    void Add(Counter a_counter, std::int64_t a_ticks) noexcept;

    /// RAII: one call, the time from construction to destruction
    class Scope
    {
    public:
        explicit Scope(Counter a_counter) noexcept :
            m_counter(a_counter), m_start(Now())
        {}
        ~Scope() { Add(m_counter, Now() - m_start); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Counter m_counter;
        std::int64_t m_start;
    };

    /// The window's totals as one text ("drain 12.3 ms, explosion update 40 calls 0.21 ms, ..."), then reset; main thread
    [[nodiscard]] std::string TakeWindow();
}
