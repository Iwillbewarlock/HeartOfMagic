#pragma once

#include "HomStyle.h"
#include "Spells.h"
#include "TextFit.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// TREE MARKS - Heart of Magic's spell marks and its tree style, shared by the
// tree window, its legend and the node card's state icon (ported from the core's
// Widget.cpp at f83dc6f3, W:2879-3033 and ReadHomStyle W:8358-8372, unchanged but
// for the text metrics DrawQuestion takes; HoM add-on plan Task 6 - the node card
// needs them before Task 7 adds the tree and its legend here).
// =============================================================================

namespace HomAddon
{
    /// Heart of Magic's tree look and settings (spec v11 T), read again at each menu open (ReadHomStyle, from the menu sink): the
    /// player may change HoM's settings or design between menus. Handed over under a lock as a shared pointer, the style itself
    /// never changed once published
    std::shared_ptr<const HomStyle::Style> CurrentStyle();
    void ReadHomStyle();

    HomStyle::Behaviour BehaviourOf(Spells::State a_state);

    /// HoM's display state of a tree node (R1): its behaviour state, the learning target of its school
    HomStyle::Display DisplayOfNode(int a_index, const Spells::Snapshot& a_snapshot);

    /// Whether HoM names this spell to the player (review v11 #1, T16): Reveal on its display state, each node by its own;
    /// else "???" wherever the name would show (tooltip, node card title, requirement rows) and no item card or 3D on a click
    bool NameShown(int a_index, const Spells::Snapshot& a_snapshot);
    std::string ShownName(int a_index, const Spells::Snapshot& a_snapshot);

    /// A HoM colour's own alpha times a_alpha, as the drawing API's percent: the one place the dim factor and the state's
    /// alpha go in (R10: never also into the clip's _alpha)
    double Percent(const HomStyle::Color& a_color, double a_alpha);

    struct MarkLook
    {
        HomStyle::Display display{};
        bool mystery{};    // a locked spell drawn as undiscovered (HomStyle::Style::Mystery)
        bool focus{};      // the selected spell: FOCUS_GROW larger, focusStroke; its alpha its full/dim class's (review v11 #2)
        bool onPath{};     // a learned spell on the learning target's path: the hub ring for its outline (:103)
        double dim = 1.0;  // the off-path factor (T11; _contextFactor)
    };

    struct MarkPlace
    {
        std::string_view school, themeColor, level;
        int tier{};
        double worldX{}, worldY{};  // the node's saved place: HoM turns its shape toward the tree origin (NodeBatch.turnAt)
        double x{}, y{};            // the mark's centre in its clip
        double scale{};             // stage units per HoM world unit
        double lineScale = 1.0;     // line widths: HoM world units x this (the tree's fTreeScale; 1 in the legend and card)
    };

    /// What a mark drew, for its node clip: the body's outline (the hover outline follows it), how far it reaches (hit area,
    /// labels), and the mystery "?" with its colour
    struct MarkDrawn
    {
        std::vector<HomStyle::Point> body;
        double reach{};
        bool question{};
        HomStyle::Color questionColor;
        double questionAlpha{};
    };

    /// Heart of Magic's spell marks (spec v11 T12, T13): renderNode's looks (canvasRendererSpell.js:69-288) and the mystery look
    /// (renderMysteryNode, :18-67). Not ported: the XP progress ring (:168-199; the percent stays in the tooltip), the
    /// hard-prerequisite shell and ring (:207-255, R5), halos and the hand-drawn ink.
    MarkDrawn DrawHomMark(RE::GFxValue& a_clip, const HomStyle::Style& a_style, const MarkPlace& a_place, const MarkLook& a_look);

    /// The undiscovered spell's "?" of a mark DrawHomMark drew at (a_cx, a_cy) in a_clip (NodeBatch.MARK_FONT 10 x a_scale,
    /// upright), its colour's alpha as the field's _alpha; nothing when the mark has none. The tree and the legend (review v11 #8)
    void DrawQuestion(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, double a_cx, double a_cy, double a_scale, const MarkDrawn& a_drawn,
        const TextFit::Metrics& a_metrics);
}
