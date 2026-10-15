#pragma once

#include "Common.h"

// =============================================================================
// LAYER 2 - PROJECTILES (and their lights)
// =============================================================================
// Projectile::Handle3DLoaded (SE/AE vfunc 0xC0) hooked on the Missile, Flame,
// Cone, Beam, Grenade and Barrier projectile vtables, one saved original each.
// After the original: a fire spell's projectile shot by the player while stoked
// has its 3D recoloured (Recolor.h) and its light tinted. Which thread calls
// Handle3DLoaded is not known: on the game thread the recolour runs at once,
// elsewhere it is deferred to a game-thread task by handle (logged once).
// Also logs, read only, the material pointers of the first few player and NPC
// fire projectiles, to see in game whether instances share materials.

namespace BlackFlame
{
    void InstallProjectileHooks();
}
