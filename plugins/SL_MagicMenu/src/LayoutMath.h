#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>


// =============================================================================
// LAYOUT MATH - the arithmetic of Widget.cpp's tree window and progress card,
// with no game types (spec v6, review round 1 #7): the widget feeds it the
// font's measured line metrics, the snapshot's states (as predicates) and the
// TreeData window; tools/magic-menu-preview-check runs these same functions on
// made-up cases. Stage units, y down. The Heart of Magic parts (badges, link
// boxes, the progress card's plan, the cards' needed heights): HomLayoutMath.h.
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

    /// A label's width beside a node at a_sx (from the window centre): a_most at most, no wider than the room from the label's
    /// start (a_gap from the node) to the window edge at a_edge on its side
    inline double LabelWidth(double a_sx, double a_gap, double a_edge, bool a_left, double a_most)
    {
        const double room = a_left ? a_sx - a_gap + a_edge : a_edge - a_sx - a_gap;
        return std::min(a_most, room);
    }

    /// A box in the tree window's coordinates (from its centre, y down): a node's own area, a label, an exit, the legend
    struct Box
    {
        double left{}, top{}, right{}, bottom{};

        [[nodiscard]] bool Overlaps(const Box& a_other) const
        {
            return left < a_other.right && a_other.left < right && top < a_other.bottom && a_other.top < bottom;
        }
    };

    inline double OverlapArea(const Box& a_lhs, const Box& a_rhs)
    {
        const double w = std::min(a_lhs.right, a_rhs.right) - std::max(a_lhs.left, a_rhs.left);
        const double h = std::min(a_lhs.bottom, a_rhs.bottom) - std::max(a_lhs.top, a_rhs.top);
        return w > 0 && h > 0 ? w * h : 0.0;
    }

    /// How far build 10's state mark reached from its centre: the diamond, the progress ring's gap and half its line (build 7
    /// T7.2). The tree window draws HoM's marks since build 11 (their reach: HomStyle::Reach, spec v11 T12 / R5); kept for
    /// tools/magic-menu-preview-check's label cases
    constexpr double MarkReach(double a_nodeR, double a_ringGap, double a_arcWidth)
    {
        return a_nodeR + a_ringGap + a_arcWidth / 2;
    }

    /// Where a label beside a node may go
    enum class Place
    {
        kRight,
        kLeft,
        kBelow,
        kAbove,
        kRightFar,  // the side, further out (past a node next to this one)
        kLeftFar,
    };

    struct PlacedBox
    {
        Place place{};
        Box box;
    };

    /// The places a label is tried at (spec v10 T5: every label, the centre's, the selected node's and the target's first), in
    /// order: its default side, the other side, below and above (centred on the node), then both sides a_step further out, up to
    /// a_farSteps times. A label a_w x a_h, a_gap from the node centre; side boxes centred on the node's height and clamped into
    /// the window as Widget.cpp PlaceLabel does; a place that does not fit in the window (|x| <= a_edgeX, |y| <= a_edgeY) is left
    /// out (the default side always stays: it is where the label was drawn).
    inline std::vector<PlacedBox> KeptLabelPlaces(double a_sx, double a_sy, double a_w, double a_h, double a_gap, bool a_defaultLeft,
        double a_edgeX, double a_edgeY, double a_step, int a_farSteps)
    {
        std::vector<PlacedBox> places;
        const double sideTop = std::clamp(a_sy - a_h / 2, -a_edgeY, std::max(-a_edgeY, a_edgeY - a_h));
        const auto side = [&](bool a_left, double a_extra) {
            const double from = a_left ? a_sx - a_gap - a_extra - a_w : a_sx + a_gap + a_extra;
            return Box{ from, sideTop, from + a_w, sideTop + a_h };
        };
        const double centredLeft = std::clamp(a_sx - a_w / 2, -a_edgeX, std::max(-a_edgeX, a_edgeX - a_w));
        const auto inside = [&](const Box& a_box) {
            return a_box.left >= -a_edgeX && a_box.right <= a_edgeX && a_box.top >= -a_edgeY && a_box.bottom <= a_edgeY;
        };
        const auto add = [&](Place a_place, const Box& a_box, bool a_always = false) {
            if (a_always || inside(a_box)) places.push_back({ a_place, a_box });
        };
        add(a_defaultLeft ? Place::kLeft : Place::kRight, side(a_defaultLeft, 0.0), true);
        add(a_defaultLeft ? Place::kRight : Place::kLeft, side(!a_defaultLeft, 0.0));
        add(Place::kBelow, { centredLeft, a_sy + a_gap, centredLeft + a_w, a_sy + a_gap + a_h });
        add(Place::kAbove, { centredLeft, a_sy - a_gap - a_h, centredLeft + a_w, a_sy - a_gap });
        for (int i = 1; i <= a_farSteps; ++i) {
            const double extra = a_step * static_cast<double>(i);
            add(a_defaultLeft ? Place::kLeftFar : Place::kRightFar, side(a_defaultLeft, extra));
            add(a_defaultLeft ? Place::kRightFar : Place::kLeftFar, side(!a_defaultLeft, extra));
        }
        return places;
    }

    /// Which place a label takes (build 6 review round 3, build 7 T7.2, spec v10 T5): each place measured against every box
    /// already reserved (drawn marks, badges, the legend, labels already placed). The first place with no overlap; when every
    /// place overlaps, none (clear false: the label is dropped, the tooltip names the spell); index and overlap then name the
    /// least covered place (the earlier on a tie), for the log.
    struct PlaceChoice
    {
        std::size_t index{};
        double overlap{};         // the chosen place's
        double firstOverlap{};    // the default place's, for the log
        bool clear{};             // a place with no overlap was found
    };

    inline PlaceChoice ChoosePlace(const std::vector<PlacedBox>& a_places, const std::vector<Box>& a_reserved)
    {
        PlaceChoice choice;
        for (std::size_t i = 0; i < a_places.size(); ++i) {
            double overlap = 0.0;
            for (const auto& box : a_reserved) overlap += OverlapArea(a_places[i].box, box);
            if (i == 0) {
                choice = { 0, overlap, overlap, false };
            } else if (overlap < choice.overlap) {
                choice.index = i;
                choice.overlap = overlap;
            }
            if (choice.overlap == 0.0) break;
        }
        choice.clear = !a_places.empty() && choice.overlap == 0.0;
        return choice;
    }

    // ---------------------------------------------------------------------
    // Blocks that fit their content (user 2026-10-30; the 3D preview box and the tree window keep their ini height): what each built-in card needs, worked out from the same values it draws
    // with. Every room is taken from the block's FULL height, so a block drawn at its fitted height (BlockRules::FittedHeight)
    // lays out the same lines and needs the same height again
    // ---------------------------------------------------------------------

    /// How many rows of a_rowH, a_pitch apart, fit in a_room (the node card's requirement rows, Widget.cpp DrawRows)
    inline std::size_t RowsFit(double a_room, double a_rowH, double a_pitch)
    {
        if (!(a_pitch > 0.0) || !std::isfinite(a_room)) return 0;
        return static_cast<std::size_t>(std::max(0.0, std::floor((a_room - (a_rowH - a_pitch)) / a_pitch)));
    }

    /// The height a_rows such rows take (the last one a whole field, the others a pitch each)
    inline double RowsHeight(std::size_t a_rows, double a_rowH, double a_pitch)
    {
        return a_rows == 0 ? 0.0 : a_rowH + a_pitch * static_cast<double>(a_rows - 1);
    }

    /// How many lines of a wrapped text field fit in a_room: at least one (Widget.cpp DrawHtmlText)
    inline int LinesFit(double a_room, double a_firstLineH, double a_lineH)
    {
        if (!(a_lineH > 0.0) || !std::isfinite(a_room)) return 1;
        return std::max(1, 1 + static_cast<int>(std::floor((a_room - a_firstLineH) / a_lineH)));
    }
}
