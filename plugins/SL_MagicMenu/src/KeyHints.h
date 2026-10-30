#pragma once

#include "BlockRulesV3.h"

#include <string_view>
#include <vector>

// =============================================================================
// KEY HINTS - our keys in SkyUI's own bottom key-hint strip (user 2026-10-31,
// "단축키 표시"): the layout key (K) of the host and the add-ons' 'SLMK' hints
// (BlockApi.h kMessageKeyHint; the HoM block's U and G), as buttons of
// _root.Menu_mc.bottomBar.buttonPanel, so they look like SkyUI's "Equip" /
// "Search" (the key's icon + the label). Done the way STB Hotkey System adds
// its hints: SkyUI's Menu_mc.updateBottomBar (MagicMenu.as:193-219, the one
// place that clears and fills the panel) is wrapped on the menu object, and
// our buttons are added after SkyUI's (and STB's) each time it runs. SkyUI's
// panel makes 6 buttons (bottombar.swf's maxButtons, SkyUI's and Untarnished
// UI's); when that is not enough, more are made the way the panel's own
// constructor makes them (ButtonPanel.as:41-45). Each frame our buttons are
// shown or hidden (a tab, the layout mode, gamepad buttons) without SkyUI's
// being made again. Widget.cpp's strip move counts them as part of the strip
// (they are the panel's children). UI thread only, but Set (any thread).
// =============================================================================

namespace KeyHints
{
    /// An add-on's 'SLMK' hint for a registered block (Widget.cpp checked the id). Any thread: kept for the session (an open
    /// menu shows it from its next frame). Logged (a refused one, the first ones accepted)
    void Set(std::string_view a_sender, const BlockRules::KeyHintRead& a_hint);

    /// A new magic menu (AttachWidget, UI thread): this menu's state starts over (the wrap goes on the new movie)
    void OnMenuOpen();

    /// The menu closes (any thread): the wrap does nothing more; this menu's counts logged
    void OnMenuClose();

    /// Once per UI frame of the open menu, before the strip is placed (Widget.cpp PlaceStripAndGear): the wrap installed (in
    /// the first frames), our buttons made again when the hint set changed and shown or hidden for a_blocks (every add-on in
    /// registration order and whether it shows now) and a_layoutMode
    void Frame(RE::GFxMovieView* a_movie, const std::vector<BlockRules::HintBlock>& a_blocks, bool a_layoutMode);
}
