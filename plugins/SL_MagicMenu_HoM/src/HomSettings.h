#pragma once

#include "Settings.h"

#include <cstdint>

// =============================================================================
// HOM SETTINGS - the Heart of Magic block's values from SL_MagicMenu.ini, with
// the defaults and clamps they had in the core's Settings.h at f83dc6f3 (moved
// here in HoM add-on plan Task 5; the ini reader is Task 6's). Header only, no
// game types: tools/magic-menu-tree-check and magic-menu-preview-check use the
// defaults and TreeContent, so their window is the one the block draws.
// =============================================================================

namespace HomSettings
{
    using Rect = Settings::Rect;

    struct Values
    {
        // [Keys] DirectInput scan codes. Chosen by the build-5 key scan of the installed mods
        // (docs/superpowers/specs/v5-key-scan.md): R (19) is Spell Organizer's remove-spell key in the magic menu and
        // N (49) Creature Framework's puppet key with no menu check; U and G are used by nothing live in the magic menu.
        std::uint32_t keyNext = 22;       // U: next learnable spell of the school (iNext)
        std::uint32_t keySetTarget = 34;  // G: what the node card's button does (iSetTarget)

        // [Layout] stage units, layout E2 (spec v6 L1): the tree window and the node card in the right column, the progress
        // card under the preview. Each block's height is its rect's (bottom - top); the core places it (Layout::Arrange)
        Rect tree{ 978, 67, 1238, 297 };      // fTree*
        Rect progress{ 648, 306, 969, 408 };  // fProgress*
        Rect nodeCard{ 978, 306, 1238, 623 }; // fNodeCard*
        // stage units per tree (world) unit, TreeData::View::scale (fTreeScale, 0.01..10). Spec v10 T2: one fixed scale; every
        // school's closest two nodes are >= 37.9 tree units apart (spell_tree.json), 28.4 at 0.75
        double treeScale = 0.75;
        // a multiplier on Heart of Magic's own mark sizes (fTreeMarkScale, 0.5..3; spec v11 T12: radii in HoM world units x
        // treeScale x this; 1 = HoM's). The largest drawn reach at 1 (a selected learnable spell's ring, 14.5 + its half line)
        // x 0.75 = 11.5 < 14.2, half the closest pair (CheckSpacing, tools/magic-menu-tree-check)
        double treeMarkScale = 1.0;
        int maxSteps = 1;   // iMaxSteps (1..6): link steps that count as linked to the centre: drawn in full and clickable even when locked
        int maxNodes = 60;  // iMaxNodes (1..200)

        // [Debug] fDebugExtraHeight (0..2000): stage units added to the node card block's height so its column overflows and the
        // column scroll can be tried in game (0 = no effect)
        double debugExtraHeight = 0.0;
    };

    inline constexpr double kTreeLegendH = 18.0;  // the legend strip at the bottom of the tree window

    struct Area
    {
        double cx{}, cy{};        // centre, in the panel's own coordinates
        double halfW{}, halfH{};  // half size
    };

    /// The tree window's content area: below the title, above the legend, inside the padding (Settings::kPanelTitleH / kPanelPad:
    /// every panel's, the host draws the title strip).
    inline Area TreeContent(const Rect& a_tree)
    {
        const double bodyH = a_tree.Height() - Settings::kPanelTitleH - kTreeLegendH;
        return { a_tree.Width() / 2, Settings::kPanelTitleH + bodyH / 2, a_tree.Width() / 2 - Settings::kPanelPad, bodyH / 2 - Settings::kPanelPad };
    }
}
