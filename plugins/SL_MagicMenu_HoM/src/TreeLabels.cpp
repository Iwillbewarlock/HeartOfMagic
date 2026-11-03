// =============================================================================
// TREE LABELS - the tree window's node labels (HoM add-on plan Task 7): the core's
// Widget.cpp at f83dc6f3, W:6599-6688 (LabelText, PlaceLabel, NodeLabelText,
// PlaceLabelAt, PlaceName) and W:6746-6783 (PlaceNodeLabel, LabelRank), unchanged
// but for the add-on's settings and the selected node handed to LabelRank.
// =============================================================================

#include "Common.h"

#include "TreeDraw.h"

#include <algorithm>
#include <optional>

namespace HomAddon
{
    namespace
    {
        // A node's label (spec v11 T15): HoM's text and colour, beside the node (by default facing the centre: left of a node
        // right of the centre; the centre's to its right). The text wraps to kLabelLines lines at the reading size and ends in
        // "..." when it needs more (Q4).
        struct LabelText
        {
            std::string name;  // the name cut to labelMaxChars, or "???"
            bool left{};       // the label left of the node
            double gap{};      // from the node's centre to the label
            std::uint32_t color{};
            double alpha = 1.0;  // the colour's own alpha x the dim factor: the field's _alpha, its only alpha (R10)
        };

        // A node's label in its clip (a_clip sits at the node): the text wraps to kLabelLines lines at the reading size and ends
        // in "..." when it needs more (Q4). Kept inside the window: up and down by its padding, and across no wider than the room
        // to the window edge on its side (v6 design review #2: a far-side label of a node near the edge ran past the panel), left
        // out when that room is under kMinLabelW. Its box goes into a_drawn. True when drawn.
        bool PlaceLabel(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const TreeData::PlacedNode& a_placed, const LabelText& a_text,
            const HomSettings::Area& a_frame, std::vector<LabelBox>& a_drawn)
        {
            const double size = Size(kLabelSize), boxH = FieldH(size, kLabelLines);
            const double labelW = LayoutMath::LabelWidth(a_placed.sx, a_text.gap, a_frame.halfW + kPad, a_text.left, kLabelW);
            if (labelW < kMinLabelW) return false;
            auto field = Draw::Text(a_movie, a_clip, "label", a_text.left ? -a_text.gap - labelW : a_text.gap, -boxH / 2, labelW, boxH, "",
                a_text.color, size, a_text.left ? "right" : "left");
            if (!field.IsDisplayObject()) return false;
            field.SetMember("wordWrap", true);
            field.SetMember("_alpha", a_text.alpha * Draw::kAlphaFull);
            FitWhole(a_movie, field, labelW, size, kLabelLines, a_text.name);
            const double width = std::min(TextW(field), labelW), height = std::min(TextH(field), boxH);
            // centred on the node, inside the window's padding
            const double limit = a_frame.halfH + kPad;
            const double top = std::clamp(a_placed.sy - height / 2, -limit, std::max(-limit, limit - height)) - a_placed.sy;
            field.SetMember("_y", top);
            const double from = a_text.left ? a_placed.sx - a_text.gap - width : a_placed.sx + a_text.gap;
            a_drawn.push_back({ from, a_placed.sy + top, from + width, a_placed.sy + top + height });
            return true;
        }

        // HoM's label of a drawn node (spec v11 T15; _labelTextOf / _labelCandidates, canvasRendererLabels.js:90-153): none for
        // a locked spell; "???" in labelHidden for an available one under revealName % (not cheatMode); the name cut to
        // labelMaxChars in labelUnlocked / the learning colour / labelAvailable. Its alpha: the colour's own x the dim factor
        // (HoM draws a label at _contextFactor). The percent lines of build 10 are gone (the tooltip has the percent).
        std::optional<LabelText> NodeLabelText(const TreeNode& a_node, bool a_left, const HomStyle::Style& a_style)
        {
            using HomStyle::Display;
            if (a_node.display == Display::kLocked) return std::nullopt;  // even under cheatMode (review v11 #9: Labels rule)
            const auto reveal = HomStyle::Reveal(a_node.display, a_node.look.percent, a_style.revealName, a_style.cheatMode);
            if (reveal == HomStyle::Name::kNone) return std::nullopt;
            const auto& t = a_style.tree;
            const bool hidden = reveal == HomStyle::Name::kHidden;
            const auto colour = a_node.display == Display::kUnlocked ? t.labelUnlocked :
                                a_node.display == Display::kLearning ? a_style.learning :
                                hidden                               ? t.labelHidden :
                                                                       t.labelAvailable;
            const double dim = a_node.full ? 1.0 : a_style.DimFactor();  // review v11 #2: the focus is dimmed as its class
            return LabelText{ hidden ? std::string(HomStyle::kHiddenName) : HomStyle::ClipLabel(Spells::NodeName(a_node.placed.index), t.labelMaxChars),
                a_left, a_node.reach + kLabelGap, colour.rgb, std::clamp(colour.alpha * dim, 0.0, 1.0) };
        }

        // The same label drawn into a_box (window coordinates; a place LayoutMath::KeptLabelPlaces chose), aligned to the side it
        // sits on (right-aligned on the left of the node, centred above or below). The box is the size the label measured at
        // its first place, so the text fits it again (one unit of slack).
        void PlaceLabelAt(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, const TreeData::PlacedNode& a_placed, const LabelText& a_text,
            const LabelBox& a_box, LayoutMath::Place a_place, std::vector<LabelBox>& a_drawn)
        {
            constexpr double kSlack = 1.0;
            const double size = Size(kLabelSize), boxH = FieldH(size, kLabelLines), w = a_box.right - a_box.left + kSlack;
            const bool left = a_place == LayoutMath::Place::kLeft || a_place == LayoutMath::Place::kLeftFar;
            const bool centred = a_place == LayoutMath::Place::kBelow || a_place == LayoutMath::Place::kAbove;
            const double x = (left ? a_box.right - w : centred ? (a_box.left + a_box.right - w) / 2 : a_box.left) - a_placed.sx;
            auto field = Draw::Text(a_movie, a_clip, "label", x, a_box.top - a_placed.sy, w, boxH, "", a_text.color, size,
                left ? "right" : centred ? "center" : "left");
            if (!field.IsDisplayObject()) return;
            field.SetMember("wordWrap", true);
            field.SetMember("_alpha", a_text.alpha * Draw::kAlphaFull);
            FitWhole(a_movie, field, w, size, kLabelLines, a_text.name);
            a_drawn.push_back(a_box);
        }
    }

    const char* PlaceName(LayoutMath::Place a_place)
    {
        switch (a_place) {
        case LayoutMath::Place::kLeft: return "left";
        case LayoutMath::Place::kBelow: return "below";
        case LayoutMath::Place::kAbove: return "above";
        case LayoutMath::Place::kRightFar: return "right, further out";
        case LayoutMath::Place::kLeftFar: return "left, further out";
        default: return "right";
        }
    }

    // a_farSteps: how often both sides are tried further out (only for the centre's, the selected node's and the target's: marks
    // are close, so an ordinary node's label further out would sit by its neighbour and read as that one's)
    KeptPlace PlaceNodeLabel(RE::GFxMovieView* a_movie, TreeNode& a_node, bool a_defaultLeft, int a_farSteps, const HomSettings::Area& a_frame,
        const HomStyle::Style& a_style, std::vector<LabelBox>& a_labels)
    {
        auto text = NodeLabelText(a_node, a_defaultLeft, a_style);
        if (!text) return { .none = true };
        if (!PlaceLabel(a_movie, a_node.clip, a_node.placed, *text, a_frame, a_labels)) {
            text->left = !text->left;
            if (!PlaceLabel(a_movie, a_node.clip, a_node.placed, *text, a_frame, a_labels)) return {};
        }
        const LabelBox drawn = a_labels.back();
        a_labels.pop_back();
        auto places = LayoutMath::KeptLabelPlaces(a_node.placed.sx, a_node.placed.sy, drawn.right - drawn.left, drawn.bottom - drawn.top,
            text->gap, text->left, a_frame.halfW + kPad, a_frame.halfH + kPad, a_node.reach, a_farSteps);
        places.front().box = drawn;  // exactly where it was drawn
        const auto choice = LayoutMath::ChoosePlace(places, a_labels);
        const auto& chosen = places[choice.index];
        if (choice.clear && choice.index == 0) {
            a_labels.push_back(drawn);
        } else {
            RE::GFxValue field;
            if (a_node.clip.GetMember("label", &field) && field.IsDisplayObject()) field.Invoke("removeTextField");
            if (choice.clear) PlaceLabelAt(a_movie, a_node.clip, a_node.placed, *text, chosen.box, chosen.place, a_labels);
        }
        return { chosen.place, choice.firstOverlap, choice.overlap, choice.clear };
    }

    // The label order (spec v10 T5): the centre, the selected node, the learning target, then the centre's direct neighbours,
    // then every other node; nearest first within each (the window's order; review v10: clickable before faint broke that)
    int LabelRank(const TreeNode& a_node, int a_target, int a_selected)
    {
        const int index = a_node.placed.index;
        if (index == TheSession().centre) return 0;
        if (index == a_selected) return 1;
        if (index == a_target) return 2;
        return a_node.placed.steps == 1 ? 3 : 4;
    }
}
