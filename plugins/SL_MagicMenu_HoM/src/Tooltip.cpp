// =============================================================================
// TOOLTIP - the tree's hover tooltip (spec v10 T6; the core's RedrawTooltip,
// W:7347-7412 at f83dc6f3): the hovered node's name, school and level, and state
// (the node card's words) in a box beside its mark, inside the tree box. Drawn
// into the overlay the host lends the tree's frame (a clip of the tree's column
// above every panel in it, its origin on the content clip's: the core's tooltip
// parent), so it is never under a neighbouring panel and is not dimmed with a
// tree that is not the selection's (T7.1). The place is worked out in the tree
// box's coordinates as before and moved by the content clip's offset in it (read
// at the tree's draw: MenuState::treeInset); the
// column's visible part comes from Context::view* (the core's ColumnViewport).
// Drawn again when the hovered node changes, the tree was drawn again, or the
// host removed the overlay (a new Context::overlayGeneration: TooltipValid). None
// in the layout mode (no overlay then).
// =============================================================================

#include "Common.h"

#include "TreeDraw.h"

#include "BlockRules.h"
#include "Cards.h"
#include "Draw.h"
#include "TreeRules.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace HomAddon
{
    void RedrawTooltip(RE::GFxMovieView* a_movie, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot)
    {
        using HomStrings::Fill;
        using HomStrings::SchoolText;
        using HomStrings::Str;
        auto& menu = Menu();
        const int hovered = a_context.layoutMode || !a_overlay ? -1 : menu.hovered;  // no tooltip in the layout mode
        if (hovered == menu.tooltipFor && !menu.tooltipDue && TreeRules::TooltipValid(menu.tooltipGeneration, a_context.overlayGeneration)) return;
        menu.tooltipDue = false;
        menu.tooltipFor = -1;
        menu.tooltipGeneration = a_context.overlayGeneration;
        if (!a_overlay) return;  // the host removed the overlay (and the tooltip in it)
        Draw::Remove(*a_overlay, kTooltipName);
        const auto it = std::ranges::find_if(menu.drawn, [&](const TreeData::PlacedNode& a_placed) { return a_placed.index == hovered; });
        if (hovered < 0 || !Spells::Get() || it == menu.drawn.end()) return;
        // the column's visible part, in the tree box's coordinates (the content clip sits at pad, title strip + pad in it); none of it
        // shows: no tooltip
        const double pad = menu.treeInset.pad, titleH = menu.treeInset.titleH, offX = pad, offY = titleH + pad;
        if (!(a_context.viewBottom > a_context.viewTop) || !(a_context.viewRight > a_context.viewLeft)) {
            menu.tooltipFor = hovered;  // done with nothing shown (fix round 1 M5: not removed again every frame; a scroll makes a new overlay)
            return;
        }
        const auto [boxW, boxH] = menu.treeBox;
        const auto [originX, originY] = menu.treeOrigin;  // where RedrawTree put the drawing's 0,0 (the body's centre)
        const auto& node = Data().tree.nodes[hovered];
        const auto look = Spells::LookOf(a_snapshot, hovered);
        const auto school = SchoolText(node.school);
        const auto level = LevelName(node.skillLevel);
        // the name by HoM's reveal rule (spec v11 T16): "???" for a locked spell and for one HoM shows as "???"; the school, level
        // and state lines stay
        const auto style = CurrentStyle();
        const std::array<std::pair<std::string, std::uint32_t>, 3> lines{ {
            { ShownName(hovered, a_snapshot), kWhite },
            { level.empty() ? school : Fill(Str::kNodeSub, { { "{school}", school }, { "{level}", level } }), kTitleColor },
            { StateLine(look.state, look.percent, false).text, kLabelColor },
        } };
        const double size = Size(kLabelSize), lineH = FieldH(size);
        const double w = std::min(kTooltipW, boxW - 2 * pad), h = static_cast<double>(lines.size()) * lineH + 2 * kTooltipPad;
        // right of the mark, else left of it, centred on its height; with room on neither side (review v10: the centre node of a
        // 260-wide tree, whose clamped tooltip covered it) above it, else below it, centred on it. Inside the box (below the title,
        // within the padding)
        const double nx = originX + it->sx, ny = originY + it->sy,
                     off = HomStyle::MaxReach(MarkScale(), style->tree.availableRing, HomSettings::Get().treeScale) + kTooltipGap;
        double x = nx + off, y = ny - h / 2;
        if (x + w > boxW - pad) x = nx - off - w;
        if (x < pad) {
            x = nx - w / 2;
            y = ny - off - h >= titleH ? ny - off - h : ny + off;
        }
        x = std::clamp(x, pad, std::max(pad, boxW - pad - w));
        // inside the tree window (its registered height: it does not fit its content) and inside the column's visible part (review
        // round 2: the column mask would cut it), above its node when it would run past the visible bottom
        y = BlockRules::TooltipY(y, h, titleH, boxH - pad - h, ny - off - h, a_context.viewTop + offY, a_context.viewBottom + offY);
        // GPT on 8897d3db (Draw::NextDepth can fail): the tooltip counts as drawn for this hover only once its clip and every line came;
        // a part that did not come removes the rest, and tooltipFor stays -1, so the next frame tries again
        auto tip = Draw::Clip(*a_overlay, kTooltipName);
        if (!tip.IsDisplayObject()) return;
        tip.SetMember("_x", x - offX);
        tip.SetMember("_y", y - offY);
        Draw::Box(tip, w, h, kPanelFill, kTooltipAlpha, kPanelEdge, kEdgeWidth);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto name = std::format("t{}", i);
            const auto field = FitLine(a_movie, tip, name.c_str(), kTooltipPad, kTooltipPad + static_cast<double>(i) * lineH, w - 2 * kTooltipPad, size,
                lines[i].first, lines[i].second);
            if (!field.IsDisplayObject()) {
                Draw::Remove(*a_overlay, kTooltipName);
                return;
            }
        }
        menu.tooltipFor = hovered;
        Q2State("tree_tooltip", std::format("{} - {}", lines[0].first, lines[2].first));
    }
}
