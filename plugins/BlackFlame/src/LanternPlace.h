#pragma once

#include "Common.h"

// =============================================================================
// LANTERN PLACE - the lantern, Luah's journal and the smoke on the alchemy lab of Ansilvund02 (design 2026-10-21, section 4)
// =============================================================================
// No CELL record: 13 plugins override Ansilvund02's header in TAKEALOOK, a CELL override from our ESL would fight them.
// So the objects are created at run time: TESCellFullyLoadedEvent for cell 01F358 (Skyrim.esm), and the player's cell
// after a game load (checked on the tick until the lab's 3D is there, 10 s at most), queue a game-thread task. While
// HoMBF_LanternPlaced (GLOB 0x816) is 0 and the lab ref 0C0090 passes its checks, the three refs are created at their
// final position (lab position + config offset turned by the lab's z angle, forcePersist), the tick waits for the
// lantern's and the journal's 3D (10 s at most, else everything made is deleted and the next load of the cell tries
// again), keyframes them and sets the global to 1. Each later load of the cell keyframes them again (the motion type is
// not known to survive a save). Picking the lantern up (TESContainerChangedEvent from the world to the player) disables
// the smoke in the player's cell. Off without the lantern records, with lanternPlace false, or without the main-thread
// queue (DisableEngineWrites).

namespace BlackFlame
{
    /// kDataLoaded, after LoadForms: the cell, lab and container sinks
    void InitLanternPlace();

    /// kPostLoadGame: arms the check of the player's cell on the next ticks
    void OnLanternGameLoaded();

    /// Ticker task: the placed refs waiting for their 3D, the post-load check
    void TickLanternPlace();

    /// Any thread: something waits on the tick (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingLanternPlace() noexcept;
}
