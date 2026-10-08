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

    // Everything is drawn into "hom_overlay", a sibling clip above the card that copies the card's
    // place, scale and fade every frame: nothing is added to the card itself, whose own layout code
    // reads its size. Coordinates in the overlay are the card's own.

    // The item card's visible box in its own coordinates: its "background" clip, else getBounds of
    // the whole card; all 0 when neither can be read
    struct CardBounds
    {
        double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
        double Width() const { return xMax - xMin; }
    };
    CardBounds Bounds(RE::GFxValue& a_card);

    // The open magic menu's movie, or nullptr
    RE::GFxMovieView* OpenMagicMenuMovie();
    // The card the overlay follows, looked up again by its path
    std::optional<RE::GFxValue> CurrentCard(RE::GFxMovieView* a_movie);

    // The bookmark was clicked: close the menu, then open the panel once it has closed
    void RequestPanel();

    namespace Tab
    {
        // Draws the bookmark into the overlay (hom_bookmark), with its click, hover and glow
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_overlay, const CardBounds& a_box);
        // Where the bookmark starts on a card of this box (what lies right of it is under the bookmark)
        double LeftEdge(const CardBounds& a_box);
    }

    namespace SpellInfo
    {
        // Follows the selected spell and draws its XP ring and tag chips into the overlay
        void Attach(RE::GFxMovieView* a_movie, RE::GFxValue& a_overlay, RE::GFxValue& a_card);
    }
}
