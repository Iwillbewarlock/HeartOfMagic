#pragma once

#include "Common.h"

// =============================================================================
// LANTERN PLACE - the lantern, Luah's journal and the smoke on the table (UpperTable01) of Ansilvund02 (design 2026-10-21, section 4)
// =============================================================================
// No CELL record: 13 plugins override Ansilvund02's header in TAKEALOOK, a CELL override from our ESL would fight them.
// So the objects are created at run time: TESCellFullyLoadedEvent for cell 01F358 (Skyrim.esm), and the player's cell
// after a game load (checked on the tick until the table's 3D is there, 10 s at most), queue a game-thread task. While
// HoMBF_LanternPlaced (GLOB 0x816) is 0 and the table ref 03DFA0 passes its checks, the three refs are created at their
// final position (table position + config offset turned by the table's z angle, forcePersist) and the global becomes 1 in the
// same task: the placement is done at creation (a part-way failure keeps what was made and still sets it; only when
// nothing was made does it stay 0). The keyframe is best effort: the made lantern and journal, and at each later load of
// the cell the lantern or journal of ours (base object, not disabled, in the cell, within 64 units of its configured
// place), are keyframed through TESObjectREFR::SetMotionType once their 3D is there (10 s at most); a failure only logs.
// Picking the lantern up (TESContainerChangedEvent from the world to the player) disables the smoke in the player's cell
// (and sets the global, which matters only for a console AddItem before any visit). Off without the lantern records,
// with lanternPlace false, or without the main-thread queue (DisableEngineWrites).

namespace BlackFlame
{
    /// kDataLoaded, after LoadForms: the cell, table and container sinks
    void InitLanternPlace();

    /// kPostLoadGame: arms the check of the player's cell on the next ticks
    void OnLanternGameLoaded();

    /// Ticker task: the refs waiting for their 3D to be keyframed, the post-load check
    void TickLanternPlace();

    /// Any thread: something waits on the tick (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingLanternPlace() noexcept;
}
