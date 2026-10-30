#pragma once

#include "BlockRegistry.h"
#include "Layout.h"

#include <cstdint>
#include <string_view>

// =============================================================================
// WIDGET - everything SL_MagicMenu draws into the magic menu: one clip
// _root.mmx_root on the menu movie with the preview box, the spell-tree window,
// the progress card and the node card (spec 2 and the 2026-10-24 user layout),
// plus SkyUI's item card moved under the progress card. Built by a UI task when
// the menu opens; the movie (and the clip) die when it closes. One frame
// handler on mmx_root does the per-frame work (selection watch, item card fit,
// redraws, the preview gates). It runs where the movie's frame handlers run;
// the thread is logged on the first call of each menu open.
// =============================================================================

namespace Widget
{
    /// Reads spell_tree.json and resolves its FormIDs (call at kDataLoaded). False when there is no tree.
    bool LoadTree();

    /// Registers the magic menu's open/close sink (call at kDataLoaded).
    void Install();

    /// Blocks plan Task 6 (D9): one add-on block registration - an SKSE message's data and size (BlockApi::Registration) from
    /// the plugin a_sender. Any thread. Accepted until the first magic menu open; logged, with "SelfTest: L6 addon <id>
    /// registered" / "... refused <reason>". The outcome is what the sender is answered (BlockApi::Result)
    BlockRegistry::Outcome RegisterBlock(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v2: an add-on's kMessageRedraw ('SLMD', data = the block's final id) from a_sender. Any thread: the id is
    /// queued and its block drawn again on the menu's next frame; dropped while the magic menu is closed. An id that is not a
    /// registered block is logged (once per id) and ignored there
    void RequestAddonRedraw(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v2: an add-on's kMessageHeight ('SLMH', data = BlockApi::HeightRequest) from a_sender. Any thread: read and
    /// queued (the last height per id counts); on the menu's next frame the block takes that height (up to its registered one)
    /// and the columns are laid out again. Dropped while the magic menu is closed; a bad message is logged and ignored
    void RequestAddonHeight(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: an add-on's kMessageShowSpell ('SLMS', data = BlockApi::ShowSpellRequest) from a_sender. Any thread: read and
    /// queued (the newest per id) with the list selection's sequence now; on the menu's next frame it is served as a click on
    /// the spell in the add-on's block, unless the list selection changed since (the player's newer choice wins) or the layout
    /// mode is on. Dropped while the magic menu is closed; a bad message is logged and ignored
    void RequestAddonShowSpell(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: an add-on's kMessageVisible ('SLMV', data = BlockApi::VisibleRequest) from a_sender. Any thread: read and
    /// queued (the last per id); on the menu's next frame the block is hidden (its place kept, no frame call) or shown again.
    /// Every menu opens with its blocks shown. Dropped while the magic menu is closed; a bad message is logged and ignored
    void RequestAddonVisible(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: a keyboard down (DirectInput scan code) that passed the input's gate (the menu open, no text entry, the
    /// layout mode off, not a key the mode owns). Any thread: queued, and on the menu's next frame each v3 block's key
    /// callback gets it (none while the layout mode is on by then). Dropped while the magic menu is closed
    void QueueAddonKey(std::uint32_t a_scanCode);

    /// [Debug] bDebugFillerBlock: registers the test block Layout::kDebugFillerId (300 high) through RegisterBlock (call at kDataLoaded); it joins the left column after the spell card
    void RegisterDebugFiller();

    /// The next-learnable key ([Keys] iNext): centre and select the next learnable spell of the school (done on the next frame).
    void RequestNext();

    /// The set-target key ([Keys] iSetTarget): what the node card's "Learn this next" button does (done on the next frame).
    void RequestSetTarget();

    /// The open magic menu's movie, or nullptr.
    RE::GFxMovieView* OpenMagicMenuMovie();

    // Blocks plan Task 4: the layout mode as the input boundary sees it. Any thread: they read and flip atomics and queue the
    // UI work for the menu's next frame (the frame handler does it)

    /// The layout mode is on (from the accepted toggle until the leave was decided)
    bool LayoutModeOn();

    /// The toggle press (the key, the gamepad button): with the mode on it leaves the mode with a save; off, it enters the mode
    /// unless something refuses (logged "Layout: mode refused - <reason>"; a_heldOther: another button is held). True when
    /// the press was taken (the hook keeps it from the menu), false when refused (passed through untouched)
    bool ToggleLayoutMode(const char* a_source, bool a_heldOther);

    /// Leaves the mode without saving (another menu on top). True when the mode was on
    bool CancelLayoutMode(const char* a_why);

    /// The ToggleLayoutMode source the gamepad button passes (entered with it, the first block has the focus at once)
    inline constexpr const char* kGamepadToggleSource = "the gamepad button";

    /// Blocks plan Task 5: a key of the layout mode's focus model (D-pad / arrows, A / Enter, B / Esc, LB / RB, [ / ] or PageUp /
    /// PageDown), queued for the menu's next frame in order with the toggle's requests (B / Esc there: drop the pick, or with no
    /// pick leave the mode as Cancel). Nothing while the mode is off
    void LayoutKey(Layout::Nav a_nav, const char* a_source);

    /// The right stick's y while the mode keeps it (0 = neutral): the focused block's column scrolls by it each frame
    void LayoutStick(float a_y);

    /// The game window lost the focus (Main's gameActive; off for the session when it read false at a menu open). Any thread
    bool FocusLost();
}

namespace Input
{
    /// Registers the keyboard sink on BSInputDeviceManager and hooks MenuControls::ProcessEvent (call at kDataLoaded).
    void Install();

    /// The MenuControls hook is on (the layout mode needs it)
    bool HookInstalled();

    /// Keyboard / gamepad buttons held now, as the hook last saw them (the lock button refuses the mode while one is held)
    int HeldCount();

    /// SelfTest L5's counts, per magic menu (ResetCounters at its open): events judged with the mode off, those kept although
    /// the mode was off and they were neither the toggle nor an owned tail (want 0), owned tails kept, toggles taken, button
    /// downs kept in the mode, keyboard downs passed with the mode off
    struct Counters
    {
        std::uint64_t eventsOff{}, strayOff{}, ownedOff{}, toggles{}, downsOn{}, keyboardDownsOff{};
    };
    void ResetCounters();
    Counters ReadCounters();

    /// The route-parity check: a keyboard down of this scan code passed the hook with the mode off this menu
    bool KeyboardDownSeenOff(std::uint32_t a_code);
}
