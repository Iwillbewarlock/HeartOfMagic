#pragma once

#include "Common.h"

#include <string_view>

// =============================================================================
// PerkAdapterPatch - the game side of the perk adapters (PerkAdapters.h)
//
// Reads the adapter files, plans with the librarian's catalog and the load
// order, and adds the keywords to the effects (and spells to perk FormLists,
// and half-cost perks to spells that have none) in memory. Nothing is written
// to plugin files or saves: removing the mod removes them.
//
// Game thread only - it walks and edits forms the game reads unlocked. Both
// callers run there: kDataLoaded, and the scan (a game-thread task).
// =============================================================================

namespace Librarian::PerkAdapters
{
    // The kDataLoaded run. Perk FormLists are only edited then: a list's array
    // can move when it grows, and in play perk conditions read lists from other
    // threads. A scan in play still plans the lists and reports what it would
    // change; the lists follow at the next game start.
    inline constexpr std::string_view kDataLoadedReason = "data loaded";

    // Plans and applies. A run brings the effects to its plan: what an earlier
    // run added and the plan no longer wants is taken off, so a changed catalog
    // or adapter file can also withdraw a keyword. Writes
    // perk_adapters_report.json next to the catalog. Takes everything back when
    // the feature is switched off, no catalog exists yet, or no file applies.
    void Apply(std::string_view reason);

    // Whether this keyword on this effect was added here rather than written in
    // a plugin. The scanner leaves those out, so a scan records the load order
    // as the plugins wrote it and the librarian never classifies from its own
    // additions.
    [[nodiscard]] bool IsInjected(RE::FormID effect, RE::FormID keyword);

    // The spell's half-cost perk as its plugin wrote it: null when the perk it
    // carries now is one the adapters filled in. Every reader that files a spell
    // by its own record (scan, tier, copies) asks this instead of data.castingPerk.
    // Game thread only, like Apply: every caller found runs there (Papyrus and
    // passive learning hop to it); the public C++ API's GetRequiredXP is the one
    // a plugin could call from elsewhere, and it already reads unlocked state.
    [[nodiscard]] RE::BGSPerk* OriginalCastingPerk(const RE::SpellItem* spell);
}
