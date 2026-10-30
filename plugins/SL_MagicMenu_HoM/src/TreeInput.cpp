// =============================================================================
// TREE INPUT - the tree window's mouse (HoM add-on plan Task 7, kFlagOwnMouse): the
// core's node handlers at f83dc6f3 (NodeClick, ShowFocus, NodeRollOver,
// NodeRollOut W:2798-2866) on the node clips the tree draws, RecheckHover and
// DropStaleHover (W:6853-6886), and OnNodeClicked's own part (W:7790-7801) with
// 'SLMS' in place of the list / tree card / 3D path (now the host's). The
// handlers only record; the tree's frame acts on the click: L2 mask clicks judged
// against the frame's Context::view* (outside: counted, not acted on; logged at
// the menu's close). Events are ignored while the last Context seen was in the
// layout mode (its covers lie above the panels).
// =============================================================================

#include "Common.h"

#include "TreeDraw.h"

#include "TreeRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <mutex>
#include <utility>

namespace HomAddon
{
    namespace
    {
        // the node clicks of this menu (SelfTest L2 mask clicks at its close); the close comes from the menu sink's thread
        std::mutex g_maskLock;
        TreeRules::MaskCount g_mask;
        std::uint32_t g_lastRequestId = 0;  // the add-on's 'SLMS' numbers (per process; 0 is "none")

        std::optional<std::pair<double, double>> RootMouse(RE::GFxMovie* a_movie)
        {
            RE::GFxValue x, y;
            if (!a_movie || !a_movie->GetVariable(&x, "_root._xmouse") || !x.IsNumber() || !a_movie->GetVariable(&y, "_root._ymouse") || !y.IsNumber()) {
                return std::nullopt;
            }
            return std::pair{ x.GetNumber(), y.GetNumber() };
        }

        // Clicks only record (the frame acts, so no clip is removed inside its own handler); the place is judged in the frame
        class NodeClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr || Menu().layoutMode) return;  // the layout mode owns the mouse (its covers take clicks first)
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index) return;
                Menu().click = MenuState::NodeClick{ static_cast<int>(*index), RootMouse(a_params.movie) };
            }
        };

        // The hover tooltip's node (spec v10 T6): recorded, drawn by the frame; the node's own outline is toggled here (R6). A roll-out
        // of another node (the events of two neighbours may come in either order) leaves the hovered one alone
        class NodeRollOver final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr || Menu().layoutMode) return;  // no tooltips in the layout mode
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index) return;
                Menu().hovered = static_cast<int>(*index);
                ShowFocus(*a_params.thisPtr, true);
            }
        };

        class NodeRollOut final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr) return;
                const auto index = Number(*a_params.thisPtr, kIndexMember);
                if (!index) return;
                ShowFocus(*a_params.thisPtr, false);
                if (Menu().hovered == static_cast<int>(*index)) Menu().hovered = -1;
            }
        };

        // Made once per process on the UI thread, never released (SKSE plugins are not unloaded; a handler left on a dead movie's clip
        // goes with it)
        struct Handlers
        {
            RE::GPtr<NodeClick> click = RE::make_gptr<NodeClick>();
            RE::GPtr<NodeRollOver> rollOver = RE::make_gptr<NodeRollOver>();
            RE::GPtr<NodeRollOut> rollOut = RE::make_gptr<NodeRollOut>();
        };

        const Handlers& TheHandlers()
        {
            static const Handlers s_handlers;
            return s_handlers;
        }

        // The tree's node clip a_index in the lent content clip (the panel's tree content), or not a display object
        RE::GFxValue NodeClip(RE::GFxValue& a_content, int a_index)
        {
            auto panel = Child(a_content, kPanelName);
            auto tree = panel.IsDisplayObject() ? Child(panel, kTreeContent) : RE::GFxValue{};
            const auto name = std::format("n{}", a_index);
            return tree.IsDisplayObject() ? Child(tree, name.c_str()) : RE::GFxValue{};
        }

        void OnNodeClicked(int a_index, const Spells::Snapshot& a_snapshot)
        {
            if (!Spells::Get() || a_index < 0 || a_index >= static_cast<int>(Data().tree.nodes.size())) return;
            auto& menu = Menu();
            menu.selected = a_index;
            menu.selectedByKey = false;
            menu.notInTree = false;  // the tree shows what was clicked: no longer dimmed (review v7 round 1)
            menu.dirty = true;
            // the tree follows what is clicked (user, game run 2): centre on it, learned or not; the list change our request causes
            // is ours (Selection::OwnRequest) and centres on this same node, so nothing moves twice
            if (Centrable(a_index)) SetCentre(a_index);
            if (menu.tabSchool.empty()) SetSchool(Data().tree.nodes[a_index].school);
            const auto id = Data().gameIds[a_index];
            if (!id) {
                logger::info("HoM block: tree click {} - no game form for it: the list and the card stay", Spells::NodeName(a_index));
                return;
            }
            // what the core's click did next (select it in the list, else the tree card and the 3D; a hidden name gets neither) is the
            // host's now: one 'SLMS', its result in the next Context (source kAddon, servedRequest)
            const bool nameShown = NameShown(a_index, a_snapshot);
            if (++g_lastRequestId == 0) ++g_lastRequestId;
            menu.lastRequest = g_lastRequestId;
            RequestShowSpell(id, menu.lastRequest, nameShown);
            logger::info("HoM block: tree click {:08X} {}: show request {} sent (name {})", id, Spells::NodeName(a_index), menu.lastRequest,
                nameShown ? "shown" : "hidden by HoM");
        }
    }

    void ShowFocus(RE::GFxValue& a_clip, bool a_shown)
    {
        RE::GFxValue focus;
        if (a_clip.GetMember(kFocusChild, &focus) && focus.IsDisplayObject()) focus.SetMember("_visible", a_shown);
    }

    void BindNodeHandlers(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, bool a_clickable)
    {
        const auto& handlers = TheHandlers();
        if (a_clickable) {
            SetHandler(a_movie, a_clip, "onRelease", handlers.click.get());
        } else {
            a_clip.SetMember("useHandCursor", false);
        }
        SetHandler(a_movie, a_clip, "onRollOver", handlers.rollOver.get());
        SetHandler(a_movie, a_clip, "onRollOut", handlers.rollOut.get());
    }

    // After a redraw the tooltip's node must still be drawn and where it was (a click recentres the tree; the node under the mouse
    // is then another one until the mouse moves): else nothing is hovered (spec v10 T6)
    void RecheckHover(const std::optional<std::pair<double, double>>& a_before)
    {
        constexpr double kSamePlace = 0.5;
        auto& menu = Menu();
        const auto it = std::ranges::find_if(menu.drawn, [&](const TreeData::PlacedNode& a_placed) { return a_placed.index == menu.hovered; });
        if (!a_before || it == menu.drawn.end() || std::abs(it->sx - a_before->first) > kSamePlace ||
            std::abs(it->sy - a_before->second) > kSamePlace) {
            menu.hovered = -1;
        }
        menu.tooltipDue = true;
    }

    // review v11 #10: a redraw can put another mark (or none) under a still mouse without a roll-out, so each frame while a node is
    // hovered its hit area is tested at the mouse (one hitTest, only while hovering); off it, the hover, its outline and (through
    // RedrawTooltip) the tooltip go. The layout mode drops the hover at once (f83dc6f3 did so at the mode's start, W:6099)
    void DropStaleHover(RE::GFxMovieView* a_movie, RE::GFxValue& a_content)
    {
        auto& menu = Menu();
        if (menu.hovered < 0) return;
        auto clip = NodeClip(a_content, menu.hovered);
        if (menu.layoutMode) {
            if (clip.IsDisplayObject()) ShowFocus(clip, false);
            menu.hovered = -1;
            menu.tooltipDue = true;
            return;
        }
        auto hit = clip.IsDisplayObject() ? Child(clip, "hit") : RE::GFxValue{};
        const auto at = RootMouse(a_movie);
        if (!hit.IsDisplayObject() || !at) return;
        RE::GFxValue result;
        std::array<RE::GFxValue, 3> args{ RE::GFxValue(at->first), RE::GFxValue(at->second), RE::GFxValue(true) };  // shapeFlag: the polygon
        if (!hit.Invoke("hitTest", &result, args.data(), args.size()) || !result.IsBool() || result.GetBool()) return;
        ShowFocus(clip, false);
        menu.hovered = -1;
    }

    void TakeNodeClick(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot)
    {
        auto& menu = Menu();
        const auto click = std::exchange(menu.click, std::nullopt);
        if (!click || a_context.layoutMode) return;  // a click recorded just before the layout mode came on goes ('SLMS' would be dropped)
        // the release's place in the content clip's space, judged against the visible part (blocks plan Task 3: setMask's click
        // masking is unverified in Skyrim's Scaleform). A place that could not be read or mapped is acted on, as at f83dc6f3
        TreeRules::Point at;
        RE::GFxValue root;
        if (click->rootAt && a_movie->GetVariable(&root, "_root")) at = MapPoint(a_movie, root, a_content, click->rootAt->first, click->rootAt->second);
        const TreeRules::View view{ a_context.viewLeft, a_context.viewTop, a_context.viewRight, a_context.viewBottom };
        bool act = false;
        {
            const std::scoped_lock lock(g_maskLock);
            act = TreeRules::CountClick(g_mask, at, view);
        }
        if (!act) {
            logger::info("HoM block: node click outside the tree column's visible part ignored ({:.1f},{:.1f}; visible {:.0f},{:.0f}..{:.0f},{:.0f})",
                at->first, at->second, view.left, view.top, view.right, view.bottom);
            return;
        }
        OnNodeClicked(click->index, a_snapshot);
    }

    void OnMenuClose()
    {
        TreeRules::MaskCount count;
        {
            const std::scoped_lock lock(g_maskLock);
            count = std::exchange(g_mask, {});
        }
        if (count.clicks == 0) return;
        SelfTest(std::format("L2 mask clicks {} - {} node click(s) this menu, {} outside the tree column's visible area (ignored; want 0: setMask "
                             "keeps the masked-off part from taking clicks)",
            count.outside == 0 ? "PASS" : "FAIL", count.clicks, count.outside));
    }
}
