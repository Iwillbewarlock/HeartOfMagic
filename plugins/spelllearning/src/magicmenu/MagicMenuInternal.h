#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - shared between MagicMenuBookmark.cpp (menu events, card
// lookup, click), MagicMenuTab.cpp (the bookmark) and MagicMenuSpellInfo.cpp
// (XP ring and chips). All of it runs on the UI thread.
// =============================================================================

namespace MagicMenuBookmark
{
    // magicmenu_paths.json "titleFields": the item card's spell name text field, tried in order
    const std::vector<std::string>& TitleFields();

    // The open magic menu's movie, or nullptr
    RE::GFxMovieView* OpenMagicMenuMovie();

    // The bookmark was clicked: close the menu, then open the panel once it has closed
    void RequestPanel();

    namespace Tab
    {
        // Draws the bookmark into a_card (hom_bookmark), with its click, hover and glow
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card);
    }

    namespace SpellInfo
    {
        // Follows the selected spell and draws its XP ring and tag chips into a_card
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_card);
    }
}
