#pragma once

#include <algorithm>
#include <cmath>

// =============================================================================
// LAYOUT MATH - the arithmetic of the text rows, with no game types (spec v6,
// review round 1 #7): the widget feeds it the font's measured line metrics;
// tools/magic-menu-preview-check runs these same functions on made-up cases.
// Stage units, y down. HoM add-on plan Task 5: the tree window's label
// placement and the node card's rows went to the Heart of Magic block's
// HomLayoutMath.h with its badges, link boxes and the cards' plans.
// =============================================================================

namespace LayoutMath
{
    /// A text field's height for a size and line count: the font's first line, a step per further line, and the gutter
    /// (AS2 text fields keep 2 units free on each side)
    struct TextMetrics
    {
        double lineFirst{};
        double linePitch{};
        double gutter{};

        [[nodiscard]] double FieldH(double a_size, int a_lines = 1) const
        {
            return a_size * (lineFirst + linePitch * static_cast<double>(std::max(1, a_lines) - 1)) + gutter;
        }
    };

    /// The largest size up to a_size, in a_step steps down to a_min, whose a_lines-line field fits a_room (a_min when none does)
    inline double SizeFor(const TextMetrics& a_metrics, double a_size, int a_lines, double a_room, double a_min, double a_step)
    {
        double size = a_size;
        while (size - a_step >= a_min && a_metrics.FieldH(size, a_lines) > a_room) size -= a_step;
        return size;
    }

    // ---------------------------------------------------------------------
    // Blocks that fit their content (user 2026-10-30; the 3D preview box keeps its ini height): what a built-in block needs,
    // worked out from the same values it draws with. Every room is taken from the block's FULL height, so a block drawn at its
    // fitted height (BlockRules::FittedHeight) lays out the same lines and needs the same height again
    // ---------------------------------------------------------------------

    /// How many lines of a wrapped text field fit in a_room: at least one (Widget.cpp DrawHtmlText)
    inline int LinesFit(double a_room, double a_firstLineH, double a_lineH)
    {
        if (!(a_lineH > 0.0) || !std::isfinite(a_room)) return 1;
        return std::max(1, 1 + static_cast<int>(std::floor((a_room - a_firstLineH) / a_lineH)));
    }
}
