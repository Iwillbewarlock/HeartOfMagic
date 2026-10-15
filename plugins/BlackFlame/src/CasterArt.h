#pragma once

#include "Common.h"
#include "Recolor.h"

// =============================================================================
// LAYER 4 - CASTING ART AND CASTING LIGHT IN THE PLAYER'S HANDS
// =============================================================================
// ActorMagicCaster::Update is not hooked (DMMF replaces it outright). Instead the
// buff ticker's task calls TickCasterArt every 100 ms while stoked.
// For each hand that casts a fire spell, or holds one readied, it recolours (same
// idempotent rules as everywhere, Recolor.h) the casting art itself:
// ActorMagicCaster::castingArtData.attachedArt. Only when that is null it falls
// back, and says so in the log, to the caster's magicNode and the hand's magic
// node found by name in the 1st and 3rd person skeletons, and as the last resort
// to the player's ModelReferenceEffects of the caster's castingArt. The caster's
// light is set to config lightColor; both are put back when the buff ends.
// Callers: the ticker task and the SKSE message handler (buff change, load).
// That SKSE tasks run on the main thread is logged, not proven (the first
// in-game run saw them on several OS threads), so the shared lists are under a
// mutex and the scene-graph work runs outside it.

namespace BlackFlame
{
    /// From the ticker task while stoked, and at buff-on
    void TickCasterArt();

    /// At kPreLoadGame / kNewGame (message handler): restores what it recoloured, forgets the hand state
    void ResetCasterArt();

    /// When the buff flag changes (from RecomputeBuff's caller): on - recolour the hands at once (readied fire spells
    /// too); off - put every recoloured hand-art property back
    void OnBuffChanged(bool a_on);

    /// Puts every recoloured hand-art property and tinted casting light back; how many properties
    int RestoreHandArt();

    /// The observer that remembers recoloured properties for that restore (bound weapons use it too)
    [[nodiscard]] const RecolorObserver& HandArtObserver();

    /// Tints a light and remembers its old colour for that restore (light effects use it too)
    bool TintRememberedLight(RE::NiLight* a_light);
}
