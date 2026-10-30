#pragma once

#include "AddonState.h"
#include "HomLayoutMath.h"
#include "HomSettings.h"
#include "TreeData.h"
#include "TreeMarks.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// TREE DRAW - what the tree window's files share (HoM add-on plan Task 7): the
// core's tree constants (W:173-193 at f83dc6f3), its drawn-node structs, and the
// panel clip. The host lends a content clip below the title strip and inside the
// padding; the tree draws into a child "panel" clip at the box's top-left (like
// the cards' CardClip), so every part keeps the core's place and code:
// TreeContent of the box, the legend at its bottom, the not-in-tree note in the
// title strip. TreeDraw.cpp: RedrawTree and its links, marks and badges;
// TreeLabels.cpp: the labels; TreeInput.cpp: the node handlers, hover and click;
// Tooltip.cpp: the hover tooltip in the host's overlay; TreeMarks.cpp: the legend.
// =============================================================================

namespace HomAddon
{
    // Tree window. Its marks, links and label colours are Heart of Magic's own (spec v11 T12-T15: HomStyle, read at menu open);
    // sizes are HoM world units x fTreeScale x fTreeMarkScale, line widths HoM world units x fTreeScale (review v11 #5)
    inline constexpr double kLabelW = 104.0, kLabelSize = 10.0, kLabelGap = 1.0;
    inline constexpr int kLabelLines = 2;  // a name wraps to this many lines, then ends in "..."
    inline constexpr double kLinkBoxStep = 4.0;  // a link's boxes for the labels to keep off (LayoutMath::LinkBoxes)
    inline constexpr double kNoteSize = 10.0, kLegendSize = 10.0, kLegendIconR = 5.0, kLegendTextGap = 4.0;
    inline constexpr double kTreeRowSize = 13.0;  // the core's kRowSize: the tree's message lines
    // tree badges (spec v10 T7): arrow half size, its distance from the edge, the "N개 더" text's width
    inline constexpr double kExitArrow = 5.0, kExitInset = 7.0, kExitTextW = 56.0;
    // the fill colour of a node's invisible hit area (alpha 0); HoM's opaque _backdrop under dimmed spells is not drawn (DrawHomMark)
    inline constexpr std::uint32_t kBackdrop = 0x000000;
    inline constexpr double kMinLabelW = 20.0;  // a label with less room than this to the window edge is left out
    inline constexpr int kKeptFarSteps = 2;      // a kept label tries its sides this many mark-reach steps further out
    inline constexpr int kKeptRank = 2;          // LabelRank of the kept labels and below: the centre, the selected node, the target
    inline constexpr double kDimTreeAlpha = 35.0;  // the previous tree under a selection that is not in the tree (T7.1)
    // The hover tooltip (spec v10 T6): its width, inner padding, gap from the node's mark and fill strength
    inline constexpr double kTooltipW = 150.0, kTooltipPad = 4.0, kTooltipGap = 4.0, kTooltipAlpha = 90.0;
    inline constexpr double kRectTolerance = 0.01;  // stage units: rounding in sums is not an overflow
    inline constexpr const char* kIndexMember = "mmxIndex";  // a node clip's tree index, read by its handlers
    inline constexpr const char* kFocusChild = "focus";      // a node clip's hover outline (spec v11 R6)
    inline constexpr const char* kTooltipName = "mmxTreeTooltip";  // the tooltip's clip in the overlay
    // The tree box's title strip and padding (the host's Settings::kPanelTitleH / kPanelPad; the tree registers with pad kPad)
    inline constexpr double kTitleH = Settings::kPanelTitleH;
    inline constexpr const char* kPanelName = "panel";     // the lent content clip's child at the box's top-left
    inline constexpr const char* kTreeContent = "content";  // the panel's child at the drawing's centre (the core's content clip)

    using LabelBox = LayoutMath::Box;

    // A drawn node (spec v10 T4, v11 T11-T13): clickable when linked to the centre (within iMaxSteps, either kind of link) or
    // learned / learnable - build 10's rule, unchanged (T11); full when it is the centre or one STRUCTURAL link from it, else
    // dimmed (R11, built apart from the clickability steps); drawn in HoM's look of its display state (R1)
    struct TreeNode
    {
        TreeData::PlacedNode placed;
        Spells::Look look;
        HomStyle::Display display{};
        bool clickable{};
        bool full{};
        bool focus{};    // the clicked (selected) node: HoM's focus look, FOCUS_GROW larger (R6)
        bool onPath{};   // on the learning target's path
        double reach{};  // how far its mark reaches, stage units (HomStyle::Reach)
        RE::GFxValue clip;
    };

    // Where a label went (spec v10 T5; build 6 review round 3, build 7 T7.2): see PlaceNodeLabel
    struct KeptPlace
    {
        LayoutMath::Place place = LayoutMath::Place::kRight;
        double firstOverlap{}, overlap{};
        bool drawn{};
        bool none{};
    };

    // ---- TreeDraw.cpp ----

    /// HoM's sizes in the tree window (spec v11 T12): HoM world units x fTreeScale x fTreeMarkScale
    double MarkScale();

    /// The lent content clip's child at the box's top-left (the host's padding and title strip): the core's tree panel
    RE::GFxValue PanelClip(RE::GFxValue& a_content);

    // ---- TreeLabels.cpp ----

    /// A node's label at the first clear place (spec v10 T5): its default side, the other, below, above, both further out
    KeptPlace PlaceNodeLabel(RE::GFxMovieView* a_movie, TreeNode& a_node, bool a_defaultLeft, int a_farSteps, const HomSettings::Area& a_frame,
        const HomStyle::Style& a_style, std::vector<LabelBox>& a_labels);

    /// The label order (spec v10 T5): the centre, the selected node, the learning target, the centre's neighbours, the rest
    int LabelRank(const TreeNode& a_node, int a_target, int a_selected);

    const char* PlaceName(LayoutMath::Place a_place);

    // ---- TreeInput.cpp ----

    /// A node clip's hover outline shown or hidden (R6)
    void ShowFocus(RE::GFxValue& a_clip, bool a_shown);

    /// The node clip's own handlers (made once per process, never released): onRelease when clickable, onRollOver / onRollOut
    void BindNodeHandlers(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, bool a_clickable);

    /// After a redraw the tooltip's node must still be drawn and where it was, else nothing is hovered (spec v10 T6)
    void RecheckHover(const std::optional<std::pair<double, double>>& a_before);

    /// review v11 #10: the hovered node's hit area tested at the mouse each frame (in the lent content clip); off it the hover
    /// goes. In the layout mode the hover goes at once
    void DropStaleHover(RE::GFxMovieView* a_movie, RE::GFxValue& a_content);

    /// The node click recorded since the last frame: L2 mask clicks (outside Context::view* counted, not acted on), then the
    /// click's own part and 'SLMS'. Dropped in the layout mode
    void TakeNodeClick(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot);

    // ---- Tooltip.cpp ----

    /// The hovered node's tooltip in the host's overlay (none in the layout mode or without an overlay)
    void RedrawTooltip(RE::GFxMovieView* a_movie, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot);

    // ---- TreeMarks.cpp ----

    /// The tree window's legend strip (spec v11 T18) at the bottom of the panel a_w x a_h
    void DrawLegend(RE::GFxMovieView* a_movie, RE::GFxValue& a_panel, double a_w, double a_h, const std::string& a_school);

    // ---- the core's text helpers, with the add-on's metrics and log ----

    inline double TextW(const RE::GFxValue& a_field) { return TextFit::TextW(a_field); }
    inline double TextH(const RE::GFxValue& a_field) { return TextFit::TextH(a_field); }

    inline bool FitWhole(RE::GFxMovieView* a_movie, RE::GFxValue& a_field, double a_w, double a_size, int a_lines, const std::string& a_text)
    {
        return TextFit::FitWhole(Metrics(), Menu().textLog, a_movie, a_field, a_w, a_size, a_lines, a_text);
    }

    inline RE::GFxValue FitLine(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name, double a_x, double a_y, double a_w,
        double a_size, const std::string& a_text, std::uint32_t a_rgb, const char* a_align = "left")
    {
        return TextFit::FitLine(Metrics(), Menu().textLog, a_movie, a_parent, a_name, a_x, a_y, a_w, a_size, a_text, a_rgb, a_align);
    }

    inline RE::GFxValue FitLines(RE::GFxMovieView* a_movie, RE::GFxValue& a_parent, const char* a_name, double a_x, double a_y, double a_w,
        double a_size, int a_lines, const std::string& a_text, std::uint32_t a_rgb, const char* a_align = "left")
    {
        return TextFit::FitLines(Metrics(), Menu().textLog, a_movie, a_parent, a_name, a_x, a_y, a_w, a_size, a_lines, a_text, a_rgb, a_align);
    }
}
