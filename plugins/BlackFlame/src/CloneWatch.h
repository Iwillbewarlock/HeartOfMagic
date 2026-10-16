#pragma once

#include "Common.h"

// =============================================================================
// CLONE WATCH - diagnostics for a copy hit art that never gets its 3D (cause B, Flame Cloak)
// =============================================================================
// A ModelReferenceEffect reads its art once (ctor 34653) and attaches the 3D only when its
// BGSArtObjectCloneTask (effect +0xC0) is done: clone result +0x80 non-null and state +0x0C == 5
// (disassembly 16880 / 16883 / 0x25d200, the same on 1.6.1170 and 1.7.104; CommonLib has no type for
// the task). A null Clone3D never completes, and the effect lives on without 3D. Two causes are open:
// the model load fails (H-B1) or it is slow (H-B2). The copy model probe (CopyModelProbe.h) demands every copy
// NIF once at the main menu; this watch shows the clone task itself:
//   - for kWatchTime after the GetHitEffectArt hook hands out a copy art, every ticker tick
//     reads the clone task of the player's live effect with that art (state, pending count, queue count,
//     art, clone result) inside the ProcessLists lock, plain reads under a fault guard, no engine call; a
//     line when they change (capped), and one when artObject3D gets set, with the time it took.

namespace BlackFlame
{
    /// Any thread (the GetHitEffectArt hook): the copy art was handed out now; watch the player's effect with it
    void ArmCloneWatch(const RE::BGSArtObject* a_copy);

    /// Ticker task (GameThread.h): one read of the watched effect's clone task
    void TickCloneWatch();

    /// Any thread: the watch runs (the ticker then stays at 100 ms)
    [[nodiscard]] bool CloneWatchActive() noexcept;

    /// Message handler, at kPreLoadGame / kNewGame: stops the watch
    void ResetCloneWatch();

}
