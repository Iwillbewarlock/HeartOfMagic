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
// Task 3): the tree window's off-window badges and link boxes, the progress
// card's plan and the progress and node cards' needed heights. Still namespace
// LayoutMath, no game types; tools/magic-menu-preview-check and
// magic-menu-tree-check run them on made-up cases. Stage units, y down.
// =============================================================================

namespace LayoutMath
{
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

    // Blocks that fit their content (LayoutMath.h): the progress and node cards' needed heights

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
