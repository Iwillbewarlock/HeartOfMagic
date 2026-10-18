#pragma once

// =============================================================================
// MAIN-THREAD PROOF - the rule that lets the main-thread drains publish (MenuWatch.h; no game types)
// =============================================================================
// Made in a drain, before any publish step: our main-thread queue is installed and the thread of its first drain is
// RE::Main::threadID. The kDataLoaded thread is not part of it: in game (1.6.1170, MO2, 2026-10-18) kDataLoaded came on
// thread 11504 while Main::threadID and every drain and menu event were 23672. blackflame-cosave-check tests it.

#include <cstdint>

namespace BlackFlame::MainThreadProof
{
    enum class Verdict
    {
        kPending,  // no drain yet, or Main::threadID not set yet: decided at a later drain
        kProven,
        kRefused,
    };

    /// a_frames: drains run so far; a_firstDrainThread: the first drain's thread; a_mainThreadId: RE::Main::threadID now
    [[nodiscard]] constexpr Verdict Decide(bool a_queueInstalled, std::uint64_t a_frames, std::uint32_t a_firstDrainThread,
        std::uint32_t a_mainThreadId) noexcept
    {
        if (!a_queueInstalled) {
            return Verdict::kRefused;
        }
        if (a_frames == 0 || a_mainThreadId == 0) {
            return Verdict::kPending;
        }
        return a_firstDrainThread == a_mainThreadId ? Verdict::kProven : Verdict::kRefused;
    }
}
