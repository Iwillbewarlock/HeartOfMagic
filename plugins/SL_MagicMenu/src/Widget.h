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
}

namespace Input
{
    /// Registers the keyboard sink on BSInputDeviceManager (call at kDataLoaded).
    void Install();
}
