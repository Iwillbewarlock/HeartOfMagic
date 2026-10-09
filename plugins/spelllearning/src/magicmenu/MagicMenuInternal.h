#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - shared between MagicMenuBookmark.cpp (menu events, the
// widget, click), MagicMenuTab.cpp (the leather clip) and MagicMenuSpellInfo.cpp
// (the selected spell's XP ring and tags). All of it is a widget on the magic
// menu movie's _root at the visible area's top right; nothing is added to the
// menu's own clips (attaching to the item card moved the menu's layout, game
// runs 1-2, 2026-10-08/09). It runs where SKSE UI tasks and the movie's frame
// handlers run (the game's main thread in the run of 2026-10-08).
// =============================================================================

namespace MagicMenuBookmark
{
    // The open magic menu's movie, or nullptr
    RE::GFxMovieView* OpenMagicMenuMovie();

    // The bookmark was clicked: close the menu, then open the panel once it has closed
    void RequestPanel();

    namespace Tab
    {
        // The clip's height (fold included) at this width
        double Height(double a_width);
        // Draws the clip into a_parent (hom_bookmark), its top-left at (a_x, a_y), with its click, hover and glow
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width);
    }

    namespace SpellInfo
    {
        // Follows the selected spell and draws its XP ring, name and tags in a box at (a_x, a_y), a_width
        // wide; a_unit is one unit of a 720-high screen in the movie's coordinates
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width, double a_unit);
    }
}
