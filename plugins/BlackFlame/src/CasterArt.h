#pragma once

#include "Common.h"

// =============================================================================
// LAYER 4 - CASTING ART AND CASTING LIGHT IN THE PLAYER'S HANDS
// =============================================================================
// ActorMagicCaster::Update is not hooked (DMMF replaces it outright). Instead the
// buff ticker calls TickCasterArt on the game thread every 100 ms while stoked.
// For each hand whose caster's currentSpell is a fire spell it recolours (same
// idempotent rules as everywhere, Recolor.h) the casting art itself:
// ActorMagicCaster::castingArtData.attachedArt. Only when that is null it falls
// back, and says so in the log, to the caster's magicNode and the hand's magic
// node found by name in the 1st and 3rd person skeletons, and as the last resort
// to the player's ModelReferenceEffects of the caster's castingArt. The caster's
// light is set to config lightColor.

namespace BlackFlame
{
    /// Game thread only (called by the buff ticker while stoked).
    void TickCasterArt();
}
