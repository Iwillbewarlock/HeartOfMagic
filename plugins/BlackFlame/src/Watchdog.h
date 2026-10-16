#pragma once

#include "Common.h"

// =============================================================================
// WATCHDOG (v2 fix C6) - keeps the log on disk and names where a stall sits
// =============================================================================
// A detached background thread (never joined, like the ticker: it holds nothing at
// exit) that, once a second:
//   - flushes our log (info lines are buffered otherwise; plugins/Common.h), so a
//     hang keeps the last lines;
//   - when no ticker body (BlackFlameTick) has finished for kStallSeconds while no
//     load is under way and the game is not paused, writes one "STALL" block: the
//     last 64 trace entries (START and END), every body started and not ended
//     (kind, thread, start) - where a thread is stuck in our code - and every held
//     TrackedMutex with its owner. Once per stall; "stall over" when a tick ends again.
// Measurement only: it changes nothing in the game.

namespace BlackFlame::Watchdog
{
    /// kDataLoaded, once
    void Start();

    /// kPreLoadGame: a load is under way (no stall is reported until it ends)
    void NoteLoadStarted() noexcept;

    /// kPostLoadGame / kNewGame: the load ended
    void NoteLoadEnded() noexcept;

    /// While the self-test records: every info line is written through at once (flush_on info)
    void SetFlushEveryLine(bool a_on) noexcept;
}
