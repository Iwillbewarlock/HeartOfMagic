#pragma once

#include <cstdint>
#include <optional>
#include <utility>

// =============================================================================
// TREE RULES - the tree window's pure rules (HoM add-on plan Task 7), no game
// types: tools/magic-menu-hom-block-check runs them, the tree block calls them.
// =============================================================================

namespace TreeRules
{
    /// The tooltip drawn into the overlay of generation a_drawn is still up only while the host's overlay is that one
    /// (Context::overlayGeneration a_now: the host bumps it each time it removed the overlay - a hide, move, scroll, redraw or
    /// the layout mode)
    constexpr bool TooltipValid(std::uint32_t a_drawn, std::uint32_t a_now)
    {
        return a_drawn == a_now;
    }

    /// What the tree window shows (RedrawTree's paths at f83dc6f3, in their order)
    enum class Look
    {
        kEmpty,         // nothing chosen: one dim line in the middle (user 2026-10-30, the board)
        kNoTree,        // no spell_tree.json read
        kNoCentre,      // no centre for the school
        kSchoolHidden,  // T17: HoM's schoolVisibility hides the centre's school
        kTree,          // the tree drawn
    };

    constexpr Look LookOf(bool a_chosen, bool a_hasTree, bool a_hasCentre, bool a_schoolShown)
    {
        if (!a_chosen) return Look::kEmpty;
        if (!a_hasTree) return Look::kNoTree;
        if (!a_hasCentre) return Look::kNoCentre;
        if (!a_schoolShown) return Look::kSchoolHidden;
        return Look::kTree;
    }

    /// B1 (a verified bug at f83dc6f3: the legend stayed up over the message states): HoM's four looks belong to a drawn tree
    /// only - not to nothing chosen, no tree, no centre or a school HoM hides
    constexpr bool LegendShown(Look a_look)
    {
        return a_look == Look::kTree;
    }

    /// The content clip's part inside its column's visible area (Context::viewLeft .. viewBottom; all 0 when none of it shows)
    struct View
    {
        double left{}, top{}, right{}, bottom{};
    };

    using Point = std::optional<std::pair<double, double>>;

    /// L2 mask clicks (blocks plan Task 3: whether setMask keeps clicks off a masked-off part is unverified in Skyrim's
    /// Scaleform): a node click is acted on only inside the visible part, edges included. A click whose place could not be read
    /// is acted on (f83dc6f3 did so too); with no visible part every placed click is outside
    constexpr bool ClickInView(const Point& a_at, const View& a_view)
    {
        if (!a_at) return true;
        if (!(a_view.right > a_view.left) || !(a_view.bottom > a_view.top)) return false;
        const auto [x, y] = *a_at;
        return x >= a_view.left && x <= a_view.right && y >= a_view.top && y <= a_view.bottom;
    }

    /// The node clicks of one menu, logged at its close (SelfTest L2 mask clicks: want 0 outside)
    struct MaskCount
    {
        int clicks{};
        int outside{};
    };

    /// One recorded node click counted; true when it is acted on (inside the visible part), false when it is only counted
    constexpr bool CountClick(MaskCount& a_count, const Point& a_at, const View& a_view)
    {
        ++a_count.clicks;
        if (ClickInView(a_at, a_view)) return true;
        ++a_count.outside;
        return false;
    }
}
