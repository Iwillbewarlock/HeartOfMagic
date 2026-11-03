#pragma once

#include "LayoutMath.h"
#include "TreeData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <vector>

// =============================================================================
// CHOICE RULES - the progress card's "pick the next spell" rows (plan 2026-11-03,
// progress-next-pick): with no learning target in the active school the card lists
// the spells the next-learnable key walks (Spells::Learnable with a tree place) as
// rows; a row click sets that spell as the learning target the way the node card's
// button and the G key do. No game types: tools/magic-menu-hom-block-check runs
// these on made-up trees and rooms.
// Order: the direct children (structural tree links) of the school's last target
// seen this game session - most likely the spell just mastered - first, then the
// rest; inside each group Spells::Learnable's order (tier, name, FormID).
// Rows: at most maxChoices spell rows; more candidates than rows add one "N more"
// line under them. The rows fit the card's full (registered) height and the
// column's visible room under the card (Context::columnRoom), and at least one row
// is always shown.
// =============================================================================

namespace ChoiceRules
{
    inline constexpr int kDefaultMaxChoices = 4;  // [Progress] iMaxChoices
    inline constexpr int kMinMaxChoices = 1, kMaxMaxChoices = 8;

    /// The nodes one structural link below any of a_parents (a_parents' own copies of the spell: its node and "_dup_" twins), each once
    inline std::set<int> ChildrenOf(const TreeData::Tree& a_tree, const std::vector<int>& a_parents)
    {
        std::set<int> children;
        const auto nodes = static_cast<int>(a_tree.nodes.size());
        for (const int parent : a_parents) {
            if (parent < 0 || parent >= nodes || static_cast<std::size_t>(parent) >= a_tree.adjacent.size()) continue;
            for (const int index : a_tree.adjacent[static_cast<std::size_t>(parent)]) {
                if (index < 0 || static_cast<std::size_t>(index) >= a_tree.edges.size()) continue;
                const auto& edge = a_tree.edges[static_cast<std::size_t>(index)];
                if (edge.kind == TreeData::EdgeKind::kStructural && edge.from == parent && edge.to != parent) children.insert(edge.to);
            }
        }
        return children;
    }

    /// The rows' order: a_candidates (in Spells::Learnable's tier, name, FormID order) with a_children first, each group keeping
    /// that order. No last target (a_children empty): a_candidates as they are
    inline std::vector<int> Order(const std::vector<int>& a_candidates, const std::set<int>& a_children)
    {
        std::vector<int> ordered = a_candidates;
        std::ranges::stable_partition(ordered, [&](int a_node) { return a_children.contains(a_node); });
        return ordered;
    }

    struct PlanIn
    {
        int candidates{};    // the spells that can be picked
        int maxChoices{};    // the most spell rows ([Progress] iMaxChoices)
        double rowsTop{};    // the first row's top (card coordinates)
        double rowH{};       // one row's field height (every row and the "more" line)
        double bottom{};     // the lowest y a line may reach (RowsBottom)
    };

    struct Plan
    {
        int rows{};           // spell rows shown
        int more{};           // candidates not shown as a row
        bool moreLine{};      // the "N more" line is drawn under the rows
        double end{};         // the last line's bottom (card coordinates)
    };

    inline constexpr double kRoomTolerance = 0.01;  // stage units: a line ending this close past the bottom still fits

    /// How many lines of a_in.rowH fit between a_in.rowsTop and a_in.bottom (0 when none)
    inline int LinesFit(const PlanIn& a_in)
    {
        if (!(a_in.rowH > 0.0) || !std::isfinite(a_in.bottom) || !std::isfinite(a_in.rowsTop)) return 0;
        return std::max(0, static_cast<int>(std::floor((a_in.bottom - a_in.rowsTop + kRoomTolerance) / a_in.rowH)));
    }

    /// The rows to draw: every candidate up to maxChoices when they and nothing else fit; else as many rows as leave one line for
    /// "N more" (at least one row; the "more" line only when it fits too)
    inline Plan PlanRows(const PlanIn& a_in)
    {
        Plan plan;
        plan.end = a_in.rowsTop;
        if (a_in.candidates <= 0) return plan;
        const int most = std::clamp(a_in.maxChoices, kMinMaxChoices, kMaxMaxChoices);
        const int lines = LinesFit(a_in);
        if (a_in.candidates <= most && a_in.candidates <= lines) {
            plan.rows = a_in.candidates;
        } else {
            plan.rows = std::max(1, std::min(most, lines - 1));
            plan.moreLine = lines >= plan.rows + 1;
        }
        plan.more = a_in.candidates - plan.rows;
        plan.end = a_in.rowsTop + a_in.rowH * static_cast<double>(plan.rows + (plan.moreLine ? 1 : 0));
        return plan;
    }

    /// The lowest y (card coordinates, from the block box's top) a line may reach: the card's full height less its bottom room,
    /// and - when the host sent the column's room (Context::columnRoom: from the content clip's top, with the column not scrolled)
    /// - the column's visible bottom less that room. a_contentTop: the content clip's top in card coordinates (the cards' pad)
    inline double RowsBottom(double a_fullH, double a_cardBottom, const std::optional<double>& a_columnRoom, double a_contentTop)
    {
        const double card = a_fullH - a_cardBottom;
        if (!a_columnRoom || !std::isfinite(*a_columnRoom)) return card;
        return std::min(card, a_contentTop + *a_columnRoom - a_cardBottom);
    }

    /// The text sizes the rows' block is laid out with (reading sizes: the host's Context metrics, or the fallback at registration)
    struct Sizes
    {
        double title{};    // the school title
        double heading{};  // "No learning target · Choose the next spell"
        double row{};      // a spell row and the "more" line
    };

    /// The card's height for a_rows spell rows and the "more" line: the card's top room, the title, the heading, the lines and the
    /// bottom room
    inline double NeededHeight(const LayoutMath::TextMetrics& a_metrics, const Sizes& a_sizes, int a_lines, double a_cardTop, double a_cardBottom)
    {
        return a_cardTop + a_metrics.FieldH(a_sizes.title) + a_metrics.FieldH(a_sizes.heading) + a_metrics.FieldH(a_sizes.row) * std::max(0, a_lines) +
               a_cardBottom;
    }

    /// The progress block's registered height (its most): the ini rect's height, or more when maxChoices rows and the "more" line
    /// need more at a_metrics / a_sizes (the fallback metrics at kDataLoaded - larger list rows in game then fit fewer rows)
    inline double RegisteredHeight(double a_iniH, int a_maxChoices, const LayoutMath::TextMetrics& a_metrics, const Sizes& a_sizes, double a_cardTop,
        double a_cardBottom)
    {
        const int most = std::clamp(a_maxChoices, kMinMaxChoices, kMaxMaxChoices);
        return std::max(a_iniH, std::ceil(NeededHeight(a_metrics, a_sizes, most + 1, a_cardTop, a_cardBottom)));
    }

    /// A row's place in the content clip (Context::view*, the release's mapped point) from its card-clip place: the card clip
    /// sits at (a_cardX, a_cardY) in the content clip (HomAddon::CardClip)
    struct Span
    {
        double top{}, bottom{};
    };

    inline Span ToContent(const Span& a_card, double a_cardY)
    {
        return { a_card.top + a_cardY, a_card.bottom + a_cardY };
    }

    /// The row count changed with the room: the card is drawn again (the rows that fit are drawn, no fewer, no more)
    inline bool RedrawForRoom(const std::optional<double>& a_drawnWith, const std::optional<double>& a_now, double a_tolerance = kRoomTolerance)
    {
        if (a_drawnWith.has_value() != a_now.has_value()) return true;
        return a_drawnWith && std::abs(*a_drawnWith - *a_now) > a_tolerance;
    }
}
