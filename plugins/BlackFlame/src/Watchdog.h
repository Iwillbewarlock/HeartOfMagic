#pragma once

#include "Common.h"

// =============================================================================
// WATCHDOG (v2 fix C6) - keeps the log on disk and names where a stall sits
// =============================================================================
// A detached background thread (never joined, like the ticker: it holds nothing at
// exit; an atexit flag stops it before spdlog goes away) that, once a second:
//   - flushes our log (info lines are buffered otherwise; plugins/Common.h), so a
//     hang keeps the last lines;
//   - when the main-thread queue (MainThreadQueue.h; it runs on paused frames too) has
//     not finished a frame for 10 s (120 s while a load is under way), writes one
//     "STALL" block: the last 64 trace entries (START and END), every body started and
//     not ended (kind, thread, start) - where a thread is stuck in our code - and every
//     held TrackedMutex with its owner. Once per stall; "stall over" when a frame ends
//     again. Without the queue it measures from the last ticker body instead.
// It reads no engine data (D2: a console save froze the screen while the old check read
// UI::GameIsPaused from this thread). Measurement only: it changes nothing in the game.

namespace BlackFlame::Watchdog
{
    /// kDataLoaded, once
    void Start();

    /// kPreLoadGame: a load is under way (the stall limit is 120 s instead of 10 s until it ends)
    void NoteLoadStarted() noexcept;

    /// kPostLoadGame / kNewGame: the load ended
    void NoteLoadEnded() noexcept;

    /// While the self-test records: every info line is written through at once (flush_on info)
    void SetFlushEveryLine(bool a_on) noexcept;
}
