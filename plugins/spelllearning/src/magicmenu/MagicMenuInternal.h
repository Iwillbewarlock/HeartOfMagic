#pragma once

// =============================================================================
// MAGIC MENU BOOKMARK - shared between MagicMenuBookmark.cpp (menu events, the
// widget, click), MagicMenuTab.cpp (the leather clip) and MagicMenuSpellInfo.cpp
// (the selected spell's XP ring and tags). All of it is a widget on the magic
// menu movie's _root that follows the item card's top-right corner every frame;
// nothing is added to the menu's own clips (attaching inside the item card moved
// the menu's layout, game runs 1-2, 2026-10-08/09). It runs where SKSE UI tasks and the movie's frame
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
        // The part of the clip above the fold line, at this width
        double FoldHeight(double a_width);
        // Draws the clip (hom_bookmark) into a_parent (hom_widget), its top-left at (a_x, a_y), with its click, hover and glow
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width);
        // The widget was hidden: drop a hover the mouse can no longer end
        void Unhover(RE::GFxValue& a_widget);
    }

    namespace SpellInfo
    {
        // The spell box's height in stage units (MagicMenuBookmark.cpp places it by this)
        inline constexpr double kBoxH = 64.0;
        // Follows the selected spell and draws its XP ring, name and tags in a box at (a_x, a_y), a_width
        // wide; a_unit is one stage unit (1.0; kept as a parameter so the box scales with it)
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, double a_x, double a_y, double a_width, double a_unit);
    }
}
