#pragma once

#include "BlockRegistry.h"
#include "Layout.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// WIDGET - everything MagicMenuRewrite draws into the magic menu: one clip
// _root.mmx_root on the menu movie with the board, the 3D preview box, our
// spell card (or SkyUI's item card moved into its block), the layout mode
// and the add-on blocks (BlockApi.h; since HoM add-on plan Task 5 the
// spell tree, progress and node card are the Heart of Magic add-on's). Built by
// a UI task when the menu opens; the movie (and the clip) die when it closes.
// One frame handler on mmx_root does the per-frame work (selection watch, item
// card fit, the add-ons' calls, the preview gates). It runs where the movie's
// frame handlers run; the thread is logged on the first call of each menu open.
// =============================================================================

namespace Widget
{
    /// Registers the magic menu's open/close sink (call at kDataLoaded).
    void Install();

    /// Blocks plan Task 6 (D9): one add-on block registration - an SKSE message's data and size (BlockApi::Registration) from
    /// the plugin a_sender. Any thread. Accepted until the first magic menu open; logged, with "SelfTest: L6 addon <id>
    /// registered" / "... refused <reason>". The outcome is what the sender is answered (BlockApi::Result)
    BlockRegistry::Outcome RegisterBlock(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// A block as the settings tab lists it: its final id, its registered title ("" for the built-in ones and an add-on block
    /// with no title strip), the plugin that registered it and the retired id it took over ("" = none; [Blocks] sHidden may name it)
    struct BlockLabel
    {
        std::string id, title, sender, legacyId;
    };

    /// 2026-10-31 (the settings tab): every block the menu knows now - the built-in preview and spell card, then the accepted
    /// add-on blocks in registration order. Any thread (a copy taken under the registry's lock; no game call)
    std::vector<BlockLabel> KnownBlocks();

    /// Block API v2: an add-on's kMessageRedraw ('SLMD', data = the block's final id) from a_sender. Any thread: the id is
    /// queued and its block drawn again on the menu's next frame; dropped while the magic menu is closed. An id that is not a
    /// registered block is logged (once per id) and ignored there
    void RequestAddonRedraw(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v2: an add-on's kMessageHeight ('SLMH', data = BlockApi::HeightRequest) from a_sender. Any thread: read and
    /// queued (the last height per id counts); on the menu's next frame the block takes that height (up to its registered one)
    /// and the columns are laid out again. Dropped while the magic menu is closed; a bad message is logged and ignored
    void RequestAddonHeight(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: an add-on's kMessageShowSpell ('SLMS', data = BlockApi::ShowSpellRequest) from a_sender. Any thread: read and
    /// queued with the list selection's sequence now; on the menu's next frame the newest request (of any block) is served as a
    /// click on the spell in the add-on's block, unless the list selection changed since (the player's newer choice wins) or
    /// the layout mode is on; the frame's other requests are dropped. Dropped while the magic menu is closed; a bad message is
    /// logged and ignored
    void RequestAddonShowSpell(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: an add-on's kMessageVisible ('SLMV', data = BlockApi::VisibleRequest) from a_sender. Any thread: read and
    /// queued (the last per id); on the menu's next frame the block is hidden (its place kept, no frame call) or shown again.
    /// Every menu opens with its blocks shown. Dropped while the magic menu is closed; a bad message is logged and ignored
    void RequestAddonVisible(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3 (2026-10-31): an add-on's kMessageKeyHint ('SLMK', data = BlockApi::KeyHint) from a_sender. Any thread, menu open
    /// or closed: read, its id checked against the registered blocks, kept for the session (KeyHints.h: the hint in SkyUI's key-hint
    /// strip while its block shows). A bad message is logged and ignored
    void RequestAddonKeyHint(std::string_view a_sender, const void* a_data, std::uint32_t a_dataLen);

    /// Block API v3: a keyboard down (DirectInput scan code) that passed the input's gate (the menu open, no text entry, the
    /// layout mode off, not a key the mode owns). Any thread: queued, and on the menu's next frame each v3 block's key
    /// callback gets it (none while the layout mode is on by then). Dropped while the magic menu is closed
    void QueueAddonKey(std::uint32_t a_scanCode);

    /// [Debug] bDebugFillerBlock: registers the test block Layout::kDebugFillerId (300 high) through RegisterBlock (call at kDataLoaded); it joins the left column after the spell card
    void RegisterDebugFiller();

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

    /// User 2026-10-31: the layout reset key's down ([Keys] iLayoutReset) in the mode, queued for the menu's next frame in order
    /// with the other requests: the default layout, as the gear's reset button did (saved when the mode is left with the toggle).
    /// Nothing while the mode is off
    void LayoutResetKey(const char* a_source);

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
