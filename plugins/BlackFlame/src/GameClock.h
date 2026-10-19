#pragma once

#include <cstdint>

// =============================================================================
// GAME CLOCK (2026-10-20) - seconds of unpaused play this session, for the corpse mark's window
// =============================================================================
// FEC's Papyrus waits stop while a menu pauses the game (run 6: a chain went on 27 s after the
// last hit), so the corpse window counts only unpaused time. The main-thread queue's drain
// (MainThreadQueue.cpp, every Main::Update, main thread) calls OnDrainFrame with UI::GameIsPaused;
// the steady-clock time since the previous drain is added when the frame was not paused, one
// frame's share capped at kMaxFrameSeconds (a hitch or a load screen frame adds no more). It is
// monotonic for the session (never reset at a load: the fire hits it is compared with are).
// Not scaled by the game's time multipliers (slow time counts as real time). Without the
// main-thread queue it never advances: Advancing() is false and the corpse mark refuses.

namespace BlackFlame::GameClock
{
    /// Main thread, the drain only (one writer): a_paused is UI::GameIsPaused for this frame
    void OnDrainFrame(bool a_paused) noexcept;

    /// Any thread: unpaused seconds so far
    [[nodiscard]] double Seconds() noexcept;

    /// Any thread: at least two drains ran (one unpaused interval could be measured)
    [[nodiscard]] bool Advancing() noexcept;
}
