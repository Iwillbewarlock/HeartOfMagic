#pragma once

#include "Common.h"

// =============================================================================
// LAYER 3 - HAZARDS (ground fire) of the player's fire spells (config hazard)
// =============================================================================
// Set3D (0x6C) and Initialize (0xA2) on the Hazard vtable (Load3D is too early).
// While stoked, a hazard with 3D is queued once (by handle) and handled on the game
// thread: when its ownerActor is the player and its BGSHazard's spell is fire
// (FireSet IsFireHazard), its 3D is recoloured and its light tinted. Geometry under
// a BSMasterParticleSystem is left alone (shared by every emitter of that kind:
// an NPC's fire too); vanilla fire hazards are mostly AddOn nodes emitting into
// such a shared system, so on them little or nothing changes (the log counts both).
// An owner not set yet is waited for up to kOwnerWait.

namespace BlackFlame
{
    void InstallHazardHook();

    /// Game thread (ticker and the hook's task): handles the queued hazards
    void TickHazards();

    /// Any thread: hazards are queued (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingHazards() noexcept;

    /// Game thread, at kPreLoadGame / kNewGame: forgets the queue and the handled hazards
    void ResetHazardState();
}
