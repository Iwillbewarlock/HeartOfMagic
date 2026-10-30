#include "Common.h"

#include "TreeMarks.h"

#include "AddonState.h"
#include "Draw.h"

#include <algorithm>
#include <mutex>
#include <set>

namespace HomAddon
{
    namespace
    {
        // HoM's config and design presets (in game through MO2's VFS; relative to the game folder like Spells.cpp's kTreePath)
        constexpr const char* kHomConfigPath = "Data/SKSE/Plugins/SpellLearning/config.json";
        constexpr const char* kHomPresetDir = "Data/SKSE/Plugins/SpellLearning/presets/design";

        std::mutex g_styleLock;
        std::shared_ptr<const HomStyle::Style> g_style = std::make_shared<const HomStyle::Style>();
    }

    std::shared_ptr<const HomStyle::Style> CurrentStyle()
    {
        const std::scoped_lock lock(g_styleLock);
        return g_style;
    }

    // The line (T19) is logged when it differs from the last one; each note (a default taken and why) once per game session
    void ReadHomStyle()
    {
        static std::string s_lastSummary;
        static std::set<std::string> s_notesLogged;
        auto style = std::make_shared<const HomStyle::Style>(HomStyle::Read(kHomConfigPath, kHomPresetDir));
        if (auto summary = style->Summary(); summary != s_lastSummary) {
            logger::info("HomStyle: {} ({}, {})", summary, kHomConfigPath, kHomPresetDir);
            s_lastSummary = std::move(summary);
        }
        for (const auto& note : style->notes) {
            if (s_notesLogged.insert(note).second) logger::info("HomStyle: {} (logged once)", note);
        }
        const std::scoped_lock lock(g_styleLock);
        g_style = std::move(style);
    }

    HomStyle::Behaviour BehaviourOf(Spells::State a_state)
    {
        using State = Spells::State;
        switch (a_state) {
        case State::kLocked: return HomStyle::Behaviour::kLocked;
        case State::kAvailable: return HomStyle::Behaviour::kAvailable;
        case State::kReady: return HomStyle::Behaviour::kReady;
        case State::kTarget: return HomStyle::Behaviour::kTarget;
        case State::kLearning: return HomStyle::Behaviour::kLearning;
        case State::kMastered: return HomStyle::Behaviour::kMastered;
        default: return HomStyle::Behaviour::kUnknown;
        }
    }

    HomStyle::Display DisplayOfNode(int a_index, const Spells::Snapshot& a_snapshot)
    {
        const auto& node = Data().tree.nodes[a_index];
        const bool isTarget = a_index == Spells::FindNode(a_snapshot.Target(node.school), node.school);
        return HomStyle::DisplayOf(BehaviourOf(Spells::LookOf(a_snapshot, a_index).state), isTarget);
    }

    bool NameShown(int a_index, const Spells::Snapshot& a_snapshot)
    {
        const auto style = CurrentStyle();
        return HomStyle::Reveal(DisplayOfNode(a_index, a_snapshot), Spells::LookOf(a_snapshot, a_index).percent, style->revealName,
                   style->cheatMode) == HomStyle::Name::kShown;
    }

    std::string ShownName(int a_index, const Spells::Snapshot& a_snapshot)
    {
        return NameShown(a_index, a_snapshot) ? Spells::NodeName(a_index) : std::string(HomStyle::kHiddenName);
    }

    double Percent(const HomStyle::Color& a_color, double a_alpha)
    {
        return std::clamp(a_color.alpha * a_alpha, 0.0, 1.0) * Draw::kAlphaFull;
    }

    MarkDrawn DrawHomMark(RE::GFxValue& a_clip, const HomStyle::Style& a_style, const MarkPlace& a_place, const MarkLook& a_look)
    {
        using HomStyle::Display;
        const auto& t = a_style.tree;
        const auto school = a_style.SchoolColor(std::string(a_place.school), a_place.themeColor);
        // review v11 #7: the mystery outline and "?" take the inked school colour, never the node's themeColor
        // (renderMysteryNode, canvasRendererSpell.js:19)
        const auto mysteryColour = HomStyle::Dim(a_style.SchoolColor(std::string(a_place.school)), HomStyle::kMysteryDim);
        const auto size = HomStyle::SizeOf(a_look.display, a_look.mystery, a_place.level, a_place.tier, a_look.focus, t.availableRing);
        HomStyle::Color fill, stroke;
        double alpha = 1.0;
        bool dashed = false;
        switch (a_look.display) {
        case Display::kUnlocked:
            fill = t.unlockedFill.value_or(school);
            stroke = a_look.onPath ? a_style.hubRing : t.unlockedRim.value_or(school);
            break;
        case Display::kLearning:
            fill = a_style.learning;  // the learning colour, the hub ring round it (R4)
            stroke = a_style.hubRing;
            break;
        case Display::kAvailable:
            fill = t.nodeFill;
            stroke = school;
            alpha = t.availableAlpha;
            break;
        default:
            dashed = true;  // "state is encoded in the outline too: locked = dashed" (:265-266)
            if (a_look.mystery) {
                fill = t.mysteryFill;
                stroke = mysteryColour;
                alpha = HomStyle::kMysteryAlpha;
            } else {
                fill = t.nodeFill;
                stroke = t.lockedStroke.value_or(school);
                alpha = HomStyle::kLockedAlpha;
            }
            break;
        }
        alpha *= a_look.dim;
        if (a_look.focus) stroke = t.focusStroke;  // its alpha stays its class's (review v11 #2)
        const double s = a_place.scale, r = size.size * s, ls = a_place.lineScale;
        auto body = HomStyle::ShapePoints(a_place.school, a_place.worldX, a_place.worldY, a_place.x, a_place.y, r);
        if (size.ring) {
            // the learnable spell's own thin ring, a circle, see-through (no backdrop under it: NodeBatch "bare")
            Draw::LineStyle(a_clip, HomStyle::kRingWidth * ls, school.rgb, Percent(school, HomStyle::kRingAlpha * a_look.dim));
            Draw::Polygon(a_clip, HomStyle::ShapePoints({}, 0, 0, a_place.x, a_place.y, (size.size + HomStyle::kRingGap) * s));
        }
        Draw::NoLine(a_clip);
        // HoM's opaque _underlay under a see-through spell is left out: on our 70 % panel it would show as a black patch
        // darker than the panel (HoM's page is opaque, so there it matches); the links under a dimmed spell show through a little
        Draw::BeginFill(a_clip, fill.rgb, Percent(fill, alpha));
        Draw::Polygon(a_clip, body);
        Draw::EndFill(a_clip);
        // the outline after the fill, a dashed one in one phase round the whole shape (R10)
        Draw::LineStyle(a_clip, size.stroke * ls, stroke.rgb, Percent(stroke, alpha));
        if (dashed) {
            Draw::Segments(a_clip, HomStyle::DashOutline(body, HomStyle::kDashShare * r, HomStyle::kDashGapShare * r));
        } else {
            Draw::Polygon(a_clip, body);
        }
        const auto inner = [&](double a_share, const HomStyle::Color& a_colour) {
            Draw::NoLine(a_clip);
            Draw::BeginFill(a_clip, a_colour.rgb, Percent(a_colour, alpha));
            Draw::Polygon(a_clip, HomStyle::ShapePoints(a_place.school, a_place.worldX, a_place.worldY, a_place.x, a_place.y, r * a_share));
            Draw::EndFill(a_clip);
        };
        if (a_look.display == Display::kUnlocked) inner(HomStyle::kCoreShare, t.unlockedCore.value_or(HomStyle::InnerAccent(school)));
        if (a_look.display == Display::kLearning) inner(HomStyle::kLearningDotShare, { HomStyle::kLearningDot, 1.0 });
        const bool question = a_look.display == Display::kLocked && a_look.mystery;
        return { std::move(body), HomStyle::Reach(size, a_place.school, s, ls), question, mysteryColour, HomStyle::kMysteryMarkAlpha * a_look.dim };
    }

    void DrawQuestion(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, double a_cx, double a_cy, double a_scale, const MarkDrawn& a_drawn,
        const TextFit::Metrics& a_metrics)
    {
        if (!a_drawn.question) return;
        const double size = HomStyle::kMarkFontSize * a_scale, h = TextFit::FieldH(a_metrics, size);
        auto mark = Draw::Text(a_movie, a_clip, "mark", a_cx - h / 2, a_cy - h / 2, h, h, "?", a_drawn.questionColor.rgb, size, "center");
        if (mark.IsDisplayObject()) mark.SetMember("_alpha", Percent(a_drawn.questionColor, a_drawn.questionAlpha));
    }
}
