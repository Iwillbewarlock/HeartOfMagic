#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - HIT SHADER AND HIT ART, where the engine asks for them
// =============================================================================
// Every hit shader and hit art an active effect puts on an actor comes from its
// ActiveEffectReferenceEffectController: GetHitEffectShader (vtable 0x0D) and
// GetHitEffectArt (0x0C). Both are hooked. While the player is stoked, for an
// effect the player cast whose MGEF is a fire effect of a fire spell (FireSet
// IsBurnable: a scroll, staff or enchantment sharing the MGEF does not count):
//   - GetHitEffectShader returns our grey HoMBF_BurnShader instead of the engine's
//     (config burnShader), so the engine makes the hit shader from ours: instant
//     hits, concentration and cloaks (caster = target = player) alike, no event,
//     no record changed, nothing to finish. NPC effects get the engine's shader.
//   - GetHitEffectArt returns the engine's art unchanged; the art's 3D is
//     recoloured once it exists (config hitArt, HitArt.h).
// What decides is the state when the engine asks: a shader or art made while
// stoked stays grey after the buff ends; one made before the buff stays as it was.

namespace BlackFlame
{
    /// After the fire sets are built (kDataLoaded): hooks 0x0D when config burnShader, 0x0C when config hitArt
    void InstallHitEffectHooks();

    /// At kPreLoadGame / kNewGame: forgets the cached player handle (read again on the next call)
    void ResetHitEffectState();
}
