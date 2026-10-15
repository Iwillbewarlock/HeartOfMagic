#pragma once

#include "Common.h"

// =============================================================================
// LAYER 4 - CASTING ART AND CASTING LIGHT IN THE PLAYER'S HANDS
// =============================================================================
// ActorMagicCaster::Update is not hooked (DMMF replaces it outright). Instead the
// buff ticker calls TickCasterArt on the game thread every 100 ms while stoked.
// For each hand that casts a fire spell, or holds one readied, it recolours (same
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

    /// Game thread, at kPreLoadGame / kNewGame: restores what it recoloured, forgets the hand state
    void ResetCasterArt();

    /// Game thread, when the buff flag changes: on - recolour the hands at once (readied fire spells
    /// too); off - put every recoloured hand-art property back
    void OnBuffChanged(bool a_on);

    /// Game thread: puts every recoloured hand-art property back; how many
    int RestoreHandArt();
}
