#pragma once

#include "Common.h"

// =============================================================================
// LAYER 4 - CASTING ART AND CASTING LIGHT IN THE PLAYER'S HANDS
// =============================================================================
// ActorMagicCaster::Update is not hooked (DMMF replaces it outright). Instead the
// buff ticker calls TickCasterArt on the game thread every 100 ms while stoked.
// For each hand whose caster's currentSpell is a fire spell it recolours (same
// idempotent rules as everywhere, Recolor.h):
//   - the caster's magicNode subtree,
//   - the magic node of that hand found by name in the 1st and the 3rd person
//     skeleton (magicNode may point at only one of the two),
//   - the 3D of the player's ModelReferenceEffects whose art is the caster's
//     castingArt,
// and sets the caster's light to config lightColor. Which of these places held
// effect geometry is not known before the in-game test: one line per cast says.

namespace BlackFlame
{
    /// Game thread only (called by the buff ticker while stoked).
    void TickCasterArt();
}
