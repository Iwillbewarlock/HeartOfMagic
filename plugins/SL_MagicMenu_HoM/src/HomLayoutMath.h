#pragma once

#include "LayoutMath.h"
#include "TreeData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// =============================================================================
// HOM LAYOUT MATH - the Heart of Magic parts of LayoutMath.h (HoM add-on plan
// Tasks 3 and 5): the tree window's label placement (Task 5: the core had no
// other user), its off-window badges and link boxes, the progress card's plan,
// the node card's requirement rows and the two cards' needed heights. Still
// namespace LayoutMath, no game types; tools/magic-menu-preview-check and
// magic-menu-tree-check run them on made-up cases. Stage units, y down.
// =============================================================================

namespace LayoutMath
{
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

    /// The off-window badges (spec v10 T7): the centre's linked neighbours outside the window (TreeData::Window::outside, each
    /// node once) per 45-degree sector of their screen direction from the centre (y down: sector 0 right, 2 down, 4 left, 6 up)
    struct Exits
    {
        std::array<int, 8> sectors{};
        int total = 0;
    };

    inline Exits Badges(const std::vector<TreeData::PlacedNode>& a_outside)
    {
        constexpr int kSectors = 8;
        constexpr double kSector = 2 * std::numbers::pi / kSectors;
        Exits exits;
        for (const auto& placed : a_outside) {
            const int sector = (static_cast<int>(std::lround(std::atan2(placed.sy, placed.sx) / kSector)) % kSectors + kSectors) % kSectors;
            ++exits.sectors[static_cast<std::size_t>(sector)];
            ++exits.total;
        }
        return exits;
    }

    /// Small boxes along a link line from (a_ax, a_ay) to (a_bx, a_by), a_step apart, a_half wide each way, leaving a_skip at
    /// both ends (the nodes' own boxes): so a label can keep off a line, not only off its ends. O(length / a_step).
    inline std::vector<Box> LinkBoxes(double a_ax, double a_ay, double a_bx, double a_by, double a_skip, double a_step, double a_half)
    {
        std::vector<Box> boxes;
        const double length = std::hypot(a_bx - a_ax, a_by - a_ay);
        if (!(a_step > 0) || length <= 2 * a_skip) return boxes;
        for (double t = a_skip; t <= length - a_skip; t += a_step) {
            const double x = a_ax + (a_bx - a_ax) * t / length, y = a_ay + (a_by - a_ay) * t / length;
            boxes.push_back({ x - a_half, y - a_half, x + a_half, y + a_half });
        }
        return boxes;
    }

    /// The progress card with a learning target (Widget.cpp RedrawProgress): the school title (moved down by the row-2 baseline
    /// offset), the target line, the bar row, the next-step hint. What gives way when the card is short, in this order:
    /// the hint shrinks (down to a_in.minSize), then the baseline offset goes, then the hint goes, then the title goes
    /// (review round 1 #2: the offset used to go before the hint shrank).
    struct ProgressIn
    {
        double cardH{}, cardTop{}, cardBottom{};  // the card's height, the first line's top, the room kept under the last line
        double titleSize{}, targetSize{}, xpSize{}, barH{}, hintSize{};
        double minSize{}, sizeStep{};             // the smallest a text is shrunk to, and the step
        double offset{};                          // the row-2 baseline offset asked for
    };

    struct ProgressPlan
    {
        double offset{};       // kept (0 when it gave way)
        bool offsetDropped{};
        bool title{};          // the school title stays
        double titleY{};
        double targetY{}, targetH{};
        double barRowY{}, barRowH{};
        bool hint{};           // the hint line is drawn
        double hintY{}, hintSize{};    // the hint at the largest size that fits (it is drawn only when minSize fits)
        double bottom{};       // the lowest y anything may reach (cardH - cardBottom)
    };

    inline ProgressPlan PlanProgress(const TextMetrics& a_metrics, const ProgressIn& a_in)
    {
        ProgressPlan plan;
        plan.bottom = a_in.cardH - a_in.cardBottom;
        plan.offset = a_in.offset;
        plan.title = true;
        plan.targetH = a_metrics.FieldH(a_in.targetSize);
        plan.barRowH = std::max(a_in.barH, a_metrics.FieldH(a_in.xpSize));
        const double titleH = a_metrics.FieldH(a_in.titleSize), body = plan.targetH + plan.barRowH, smallest = a_metrics.FieldH(a_in.minSize);
        double y = a_in.cardTop + plan.offset + titleH;
        if (plan.offset > 0 && y + body + smallest > plan.bottom) {
            y -= plan.offset;
            plan.offset = 0.0;
            plan.offsetDropped = true;
        }
        plan.titleY = a_in.cardTop + plan.offset;
        if (y + body > plan.bottom) {
            plan.title = false;
            y = a_in.cardTop;
        }
        plan.targetY = y;
        y += plan.targetH;
        plan.barRowY = y;
        y += plan.barRowH;
        plan.hintY = y;
        plan.hint = smallest <= plan.bottom - y;
        if (plan.hint) plan.hintSize = SizeFor(a_metrics, a_in.hintSize, 1, plan.bottom - y, a_in.minSize, a_in.sizeStep);
        return plan;
    }

    // Blocks that fit their content (LayoutMath.h): the progress and node cards' needed heights (the node card's rows: RowsFit /
    // RowsHeight, above)

    /// The progress card's needed height for a plan: the hint's bottom (or the bar row's when the hint was left out) and the
    /// room kept under the last line
    inline double ProgressNeeded(const TextMetrics& a_metrics, const ProgressIn& a_in, const ProgressPlan& a_plan)
    {
        return a_plan.hintY + (a_plan.hint ? a_metrics.FieldH(a_plan.hintSize) : 0.0) + a_in.cardBottom;
    }

    /// The node card's needed height: its requirement rows from a_rowsTop, the rule gap, and the bottom part that hangs from the
    /// card's bottom (bottom rule, XP line, button: a_bottomPart high)
    inline double NodeCardNeeded(double a_rowsTop, double a_rowsH, double a_ruleGap, double a_bottomPart)
    {
        return a_rowsTop + a_rowsH + a_ruleGap + a_bottomPart;
    }
}
