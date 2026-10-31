#pragma once

#include "Settings.h"

#include <optional>

// =============================================================================
// PREVIEW - build 4 P1 (spec 2026-10-25-magic-menu-proto-v4.md): the game's
// spell preview is placed at our preview box and every model up is fitted to
// it (method after Inventory Reforged's ItemView PlacePreview/FitPreview):
//   place  once per box/visible rect: the box's middle as a world point at the
//          game's own preview depth (itemPos / itemPosCopy, itemScale(Copy)).
//   fit    every frame, every loaded model: measured by what the camera sees
//          of it (vertices, else its bound sphere once that has settled; again
//          when the game turns or rescales it), then grown and moved so that
//          fills the box at the model's own depth.
// Build 5 Q3 (spec 2026-10-25-magic-menu-proto-v5.md) adds, around the
// over-pass Render(), in this order: give back the model we hid -> place/fit
// -> scissor to the box -> Render() -> scissor restored -> hide the current
// selection's model again, so the game's own pass (before the movie) draws
// nothing of it (bHideGamePass) and the over-pass draws nothing outside the
// box (bClipPreview). The arithmetic is in PreviewMath.h (no game types;
// tools/magic-menu-preview-check runs it).
// "The box" is the preview panel's content area (below its title band).
// Self-test lines start with "SelfTest: P1" (PASS, FAIL or BOUND; a "P1
// summary" at every close) and "SelfTest: Q3" (clip, hide, anim; a "Q3
// summary" at every close).
// Threads: OnFrame (the menu's frame handler, any thread) only stores the box,
// the visible rect and the selection; OnPostDisplay runs on the main thread
// inside MagicMenu::PostDisplay. The game's place is restored on menu close.
// A model we hid is given back before each over-pass while it is the
// selection's (not while the box is not shown), and every model we hid that is
// still up comes back at close and at the next menu's reset.
// Blocks plan Task 3 (D2): the panels sit in scrolled columns. The scissor and
// the alpha save/restore take the box cut to the preview column's viewport
// (FrameInfo::columnClip); a frame not fitted (zooming, not placed yet) is
// scissored to the column's viewport, so the column clips whatever the fit;
// when none of the box shows, Render() and the alpha save are skipped and the
// game's own pass stays hidden ("SelfTest: L2 preview clip", judged on frames
// the column cuts: PASS when the scissor differs from the box's own pixels, lies
// inside the column's and the column clip matches the mask read back). With bClipPreview=0 nothing is
// clipped, as before.
// Nothing is changed while Inventory Reforged is active in this menu, with
// bMovePreview=0 (place/fit, hide and clip), or on Skyrim VR; while the
// preview zooms or is not placed yet the over-pass is not scissored.
// =============================================================================

namespace Preview
{
    /// What the frame handler measured on the movie's own thread, for the main-thread place, fit, hide and clip
    struct FrameInfo
    {
        RE::GRectF visible;      // the movie's visible stage rect
        double pixelsPerUnit{};  // screen pixels per stage unit (viewport height / visible height)
        Settings::Rect box;      // the preview box's content area (stage, on screen: its column's scroll applied), Settings::PanelContent
        // blocks plan Task 3 (D2): the preview column's viewport (stage). The model's place and fit keep the whole box; only the
        // scissor and the alpha save/restore take box cut to it (Layout::VisibleBox), and nothing is drawn when none of the box
        // shows. nullopt: no column (the scissor is the box, as before)
        std::optional<Settings::Rect> columnClip;
        // with bSelfTest: the preview column's mask as Scaleform reads it back (getBounds in _root, stage), for SelfTest L2 preview
        // clip to compare with columnClip each frame (the border can move it); nullopt: not read
        std::optional<Settings::Rect> columnMask;
        RE::GRectF viewport;     // the movie's viewport (GViewport left/top/width/height) in pixels of its buffer
        RE::GRectF boxScreen;    // the box's corners through GFxMovieView::TranslateToScreen (screen pixels)
        std::int32_t bufferW{}, bufferH{};  // the viewport's buffer (GViewport bufferWidth/Height): what its pixels are of
        RE::FormID selected{};   // the list's selected item (any form: a spell, power or shout); 0 = none
        RE::FormID treeSpell{};  // the tree card's spell whose 3D P3 asked for; 0 = none
    };

    /// A new magic menu; a_irActive: Inventory Reforged runs its panel in this menu (nothing is changed then).
    void OnMenuOpen(bool a_irActive);

    /// Every frame of the magic menu movie (any thread): keeps the latest box, visible rect and selection.
    void OnFrame(const FrameInfo& a_frame);

    /// MagicMenu::PostDisplay, after the menu's own. a_active false (the preview box is not shown, the menu closes, the
    /// call is for another movie): changes nothing; what it hid stays hidden (no box to draw it in; the close gives it
    /// back). Else: gives back the selection's model it hid, places and fits (a_fit), and with a_overPass draws the
    /// preview again after the movie (clipped to the box) and hides the current selection's model from the game's next
    /// own pass. True when it called Inventory3DManager::Render(). a_noBox: this menu's movie is up but its tab shows no
    /// preview box (nothing chosen; Active Effects before build 16), or one whose box shows no 3D (Powers with
    /// bHidePowers3D=1, spec v13 B1: the caller passes a_active false):
    /// every model up is hidden while it lasts, given back when the box shows or at close. a_hide3D: the tab is Powers -
    /// hidden there even with bMovePreview=0 (review v13 #3; without the box otherwise bMovePreview=0 changes nothing).
    /// a_blockHidden (fix round 1, GPT P2): [Blocks] sHidden hides the preview block this menu - treated as bMovePreview=0, so the
    /// game's own 3D stays on the tabs without the box and what the Powers hide hid is given back when it ends (PreviewMath::HidesForTab)
    bool OnPostDisplay(RE::Inventory3DManager& a_manager, bool a_fit, bool a_overPass, bool a_active, bool a_noBox, bool a_hide3D, bool a_blockHidden);

    /// Puts the game's preview place back and gives back what it hid (no-op when nothing was changed); main thread.
    void OnMenuClose();
}
