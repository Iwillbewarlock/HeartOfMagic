#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - a widget in the magic menu: a leather clip that opens
// the panel, and the selected spell's XP ring, name and tags.
// docs/superpowers/specs/2026-10-08-magic-menu-bookmark-design.md
// =============================================================================

namespace MagicMenuBookmark
{
    // Registers the MagicMenu open/close sink. kDataLoaded.
    void Install();

    // UIManager::HidePanel: true once when the panel was opened from the bookmark (then reopen the menu)
    bool TakeReturnFlag();
    // A new game or save load (UIManager::EnsureFocusReleased): the panel closes without going back to the menu
    void ClearReturnFlag();
}
