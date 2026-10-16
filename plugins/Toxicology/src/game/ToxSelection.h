#pragma once

// =============================================================================
// Toxicology - the poison the player's poison spells copy, and its icon
// =============================================================================
// Spec 5.2: the top favorited poison in the player's inventory (core
// PickTopPoison). Spec 5.5: the ESL's keyword HoMTox_MimicPoison sits on that
// poison's base form so I4 draws the mimic icon on it; it moves when the
// choice changes. Spec 12: the keyworded poison is remembered by FormID only.
//
// Threads: game main thread only, like ToxData.h - the SKSE messaging listener,
// SKSE tasks, and the menu sink (which hands the work to an SKSE task when an
// event arrives on another thread).
// =============================================================================

namespace RE
{
    class AlchemyItem;
}

namespace ToxGame
{
    // How long CurrentPoison keeps its answer before reading the inventory again (spec 5.2)
    inline constexpr double kSelectionCacheSeconds = 0.5;

    // The poison to copy: the player's top favorited poison (count > 0). Cached for
    // kSelectionCacheSeconds; a_forceRecompute reads the inventory now. Null without the
    // Toxicology ability, with the setting "enabled" false, or with no favorited poison.
    RE::AlchemyItem* CurrentPoison(bool a_forceRecompute = false);

    // Recomputes the choice. When the poison that should carry the icon keyword differs from
    // the one carrying it: removes the keyword from the old one, adds it to the new one (only
    // with "showIcon" on and the ESL loaded), then deals with I5's icon cache once.
    void OnSelectionMaybeChanged();

    // Before a save loads or a new game starts: takes the keyword off the poison carrying it
    // (looked up again by FormID; a player-made 0xFF form that no longer resolves is forgotten)
    // and forgets the cached choice, so the next call reads the inventory.
    void ResetSelection();

    // Registers the MenuOpenCloseEvent sink (InventoryMenu or FavoritesMenu opening ->
    // OnSelectionMaybeChanged). Call once at kDataLoaded, on the main thread.
    void RegisterMenuSink();
}
