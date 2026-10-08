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

    // UIManager::HidePanel: true once when the panel was opened from the bookmark (then reopen the menu)
    bool TakeReturnFlag();
    // A save load or the main menu: the panel closes without going back to the menu
    void ClearReturnFlag();
}
