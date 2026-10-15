#pragma once

#include "Common.h"

// =============================================================================
// BUFF STATE - is the player's Stoke buff ("흑염 달구기") active
// =============================================================================
// One atomic flag, written only on the game thread (RecomputeBuff); hooks on any
// thread only read it (IsStoked). It is recomputed when an active effect is
// applied to or removed from the player, at kPostLoadGame / kNewGame, and by the
// ticker: every kTickInterval while the flag is on, every kIdleRecheck while off
// (a save loaded with the buff on is caught even if no effect event arrives).

namespace BlackFlame
{
    [[nodiscard]] bool IsStoked() noexcept;

    /// Game thread only. Reads the player's active effects; logs a change of the flag.
    void RecomputeBuff(const char* a_reason);

    /// Any thread: queues one RecomputeBuff task (a second request while one is queued is dropped).
    void RequestRecomputeBuff(const char* a_reason);

    /// Starts the detached ticker thread (once; later calls do nothing).
    void StartTicker();
}
