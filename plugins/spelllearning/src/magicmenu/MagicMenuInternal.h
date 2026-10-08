#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - shared between MagicMenuBookmark.cpp (menu events, card
// lookup, click), MagicMenuTab.cpp (the bookmark) and MagicMenuSpellInfo.cpp
// (XP ring and chips). All of it runs where SKSE UI tasks and the menu movie's frame
// handlers run (the game's main loop as far as is known - not verified in the game)
// =============================================================================

namespace MagicMenuBookmark
{
    // magicmenu_paths.json "titleFields": the item card's spell name text field, tried in order
    const std::vector<std::string>& TitleFields();

    // The item card's box in its own coordinates (children's _x/_y); all 0 when it cannot be read
    struct CardBounds
    {
        double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
        double Width() const { return xMax - xMin; }
    };
    CardBounds Bounds(RE::GFxValue& a_card);

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
