#pragma once

#include "Common.h"

#include <string_view>

// =============================================================================
// PerkAdapterPatch - the game side of the perk adapters (PerkAdapters.h)
//
// Reads the adapter files, plans with the librarian's catalog and the load
// order, and adds the keywords to the effects in memory. Nothing is written to
// plugin files or saves: removing the mod removes the keywords.
//
// Game thread only - it walks and edits forms the game reads unlocked. Both
// callers run there: kDataLoaded, and the scan (a game-thread task).
// =============================================================================

namespace Librarian::PerkAdapters
{
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
}
