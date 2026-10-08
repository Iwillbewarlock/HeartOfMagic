#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - a bookmark on the magic menu's spell description box
// that opens the panel, and the selected spell's XP ring and tag chips.
// docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md
// =============================================================================

namespace MagicMenuBookmark
{
    // Registers the MagicMenu open/close sink and reads magicmenu_paths.json. kDataLoaded.
    void Install();
}
