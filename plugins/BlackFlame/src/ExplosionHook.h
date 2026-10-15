#pragma once

#include "Common.h"

// =============================================================================
// LAYER 3 - EXPLOSIONS (and their lights)
// =============================================================================
// Explosion has no spell member and no 3D-loaded virtual: TESObjectREFR::Set3D
// (vfunc 0x6C) is hooked on the Explosion vtable. After the original, with a
// non-null 3D: an explosion whose base form is in the fire explosion set
// (FireSet.h) and whose owner or cause actor is the player, while stoked, has
// its 3D recoloured and its light tinted. When the explosion's magicCaster has a
// current spell that is not a fire spell (a staff, a scroll), it is left alone;
// with no magicCaster spell the base form and the attribution decide. A light
// that does not exist yet at Set3D is tinted once more on the next ticker tick.
// Thread handling as for projectiles.

namespace BlackFlame
{
    void InstallExplosionHook();

    /// Game thread, every ticker tick: lights missing at Set3D, tinted one tick late.
    void TickExplosionLights();
}
