#pragma once

#include "Common.h"

// =============================================================================
// SCRIPT TAGS - references a script of the player's fire effect made or cast from
// =============================================================================
// Filled by the Papyrus native wrappers (PapyrusWrap.h). Two kinds, both by handle
// and short-lived:
//   - placed: what ObjectReference.PlaceAtMe returned. Recoloured by the ticker
//     once its 3D exists (an explosion's or hazard's light tinted too), within
//     kPlacedWindow; then forgotten.
//   - source: the akSource of Spell.Cast / Spell.RemoteCast. For kSourceWindow,
//     projectiles it shoots count as the player's (Attribution.h).
// Placed references also count as sources (a placed activator that casts later).

namespace BlackFlame
{
    void TagPlaced(RE::TESObjectREFR* a_ref);
    void TagSource(RE::TESObjectREFR* a_ref);

    /// From a VM thread: the tag is stored at once (no ordering race with the cast it belongs to) with
    /// the calling script's VM handle; it counts only once the game thread judged that caller a fire
    /// effect the player cast (at the first IsTaggedSource there, or the next tick), else it is dropped
    void TagPending(RE::TESObjectREFR* a_ref, bool a_placed, RE::VMHandle a_caller);

    /// Any thread: a live, confirmed source tag (or placed tag) names this reference; a pending one is
    /// judged here on the game thread and does not count elsewhere
    [[nodiscard]] bool IsTaggedSource(const RE::TESObjectREFR* a_ref);

    /// Ticker task: recolours placed references whose 3D exists, drops expired tags
    void TickScriptTags();

    [[nodiscard]] bool HasScriptTags() noexcept;

    /// At kPreLoadGame / kNewGame
    void ResetScriptTags();
}
