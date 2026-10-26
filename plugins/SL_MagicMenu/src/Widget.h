#pragma once

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

    /// Leaves the mode without saving (Esc / B, another menu on top). True when the mode was on
    bool CancelLayoutMode(const char* a_why);
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
