#pragma once

#include "Common.h"

// =============================================================================
// BUFF STATE - is the player's buff "갈망" (Longing: the stoke spell, or the worn lantern) active
// =============================================================================
// One atomic flag, written by RecomputeBuff (a task) and cleared by ResetBuffFlag
// (the message handler, at a load); hooks on any thread only read it (IsStoked). It is recomputed when an active effect is
// applied to or removed from the player, at kPostLoadGame / kNewGame, and by the
// ticker: every kTickInterval while the flag is on, every kIdleRecheck while off
// (a save loaded with the buff on is caught even if no effect event arrives).

namespace BlackFlame
{
    [[nodiscard]] bool IsStoked() noexcept;

    /// Game thread: the player has an active, not dispelled HoMBF_LanternEffect (0x810), read now (not the cached flag)
    [[nodiscard]] bool PlayerHasLanternEffect();

    /// From a task (RequestRecomputeBuff). Reads the player's active effects; logs a change of the flag.
    void RecomputeBuff(const char* a_reason);

    /// At kPreLoadGame / kNewGame: the flag goes off without OnBuffChanged (the layers' reset puts
    /// the hand art back); the next recompute sets it from the loaded game
    void ResetBuffFlag();

    /// Any thread: queues one RecomputeBuff task (a second request while one is queued is dropped).
    void RequestRecomputeBuff(const char* a_reason);

    /// Starts the detached ticker thread (once; later calls do nothing).
    void StartTicker();
}
