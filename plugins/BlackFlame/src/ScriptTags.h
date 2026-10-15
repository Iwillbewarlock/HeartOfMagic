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
    /// the calling script's VM handle; it counts only once a task or the ticker judged that caller a fire
    /// effect the player cast (ResolvePending, TickScriptTags), else it is dropped. Hooks never judge:
    /// that needs the VM handle policy and a hook may hold engine locks.
    void TagPending(RE::TESObjectREFR* a_ref, bool a_placed, RE::VMHandle a_caller);

    enum class TagState
    {
        kNone,
        kPending,
        kConfirmed
    };

    /// Any thread, never judges
    [[nodiscard]] TagState TagStateOf(const RE::TESObjectREFR* a_ref);

    /// From a task or the ticker only (never a hook): judges a pending tag on this reference now; true
    /// when it is (now) confirmed
    bool ResolvePending(const RE::TESObjectREFR* a_ref);

    /// Once per layer (a string literal): a decision was deferred because only a pending tag matched
    void LogDeferredOnce(const char* a_layer);

    /// Any thread, never judges: a live, confirmed source tag (or placed tag) names this reference; a
    /// pending one does not count (yet)
    [[nodiscard]] bool IsTaggedSource(const RE::TESObjectREFR* a_ref);

    /// Ticker task: recolours placed references whose 3D exists, drops expired tags
    void TickScriptTags();

    [[nodiscard]] bool HasScriptTags() noexcept;

    /// At kPreLoadGame / kNewGame
    void ResetScriptTags();
}
