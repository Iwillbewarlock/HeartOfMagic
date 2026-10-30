#include "Common.h"

#include "TreeMarks.h"

#include "AddonState.h"
#include "Draw.h"
#include "HomSettings.h"
#include "TreeDraw.h"

#include <algorithm>
#include <array>
#include <format>
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

    // The tree window's legend strip (spec v11 T18; W:3042-3118 at f83dc6f3, drawn at every tree draw now - the content clip is new
    // each time, so the last school's legend is never left over): HoM's four looks - unlocked, learning, available, locked (the mystery
    // look when HoM's discovery mode shows locked spells so) - in a_school's shape and colour, the known size kLegendIconR.
    // One line in the smaller kLegendSize (spec v10 T9, below the reading size: a key, not text to read), laid out by the
    // texts' measured widths: the first item where the window title's letters start (kPad plus the text field's own
    // kGutter / 2), the spare room only between items
    // (UI polish 2026-10-29; it used to put half a gap before the first and after the last).
    void DrawLegend(RE::GFxMovieView* a_movie, RE::GFxValue& a_panel, double a_w, double a_h, const std::string& a_school)
    {
        struct Item
        {
            HomStyle::Display display;
            HomStrings::Str text;
        };
        constexpr std::array<Item, 4> kItems{ { { HomStyle::Display::kUnlocked, HomStrings::Str::kLegendMastered },
            { HomStyle::Display::kLearning, HomStrings::Str::kLegendLearning }, { HomStyle::Display::kAvailable, HomStrings::Str::kLegendCanLearn },
            { HomStyle::Display::kLocked, HomStrings::Str::kTreeLocked } } };
        constexpr double kStep = 1.0, kIconScale = kLegendIconR / HomStyle::kKnownSize;
        constexpr double kTurnX = 1.0;  // a world place straight right of the origin: the school's shape at its base turn
        const auto style = CurrentStyle();
        const double y = a_h - HomSettings::kTreeLegendH / 2 - kPad / 2;
        const double reach = HomStyle::MaxReach(kIconScale, style->tree.availableRing);
        const double room = a_w - 2 * kPad, icon = 2 * reach;
        for (double size = kLegendSize;; size -= kStep) {
            Draw::Remove(a_panel, "legend");
            auto legend = Draw::Clip(a_panel, "legend");
            if (!legend.IsDisplayObject()) return;
            const double h = FieldH(size);
            std::array<RE::GFxValue, kItems.size()> fields;
            double used = 0;
            for (std::size_t i = 0; i < kItems.size(); ++i) {
                const auto name = std::format("t{}", i);
                fields[i] = Draw::Text(a_movie, legend, name.c_str(), 0, y - h / 2, room, h, HomStrings::Text(kItems[i].text), kDimText, size);
                used += icon + kLegendTextGap + TextW(fields[i]);
            }
            if (used > room && size - kStep >= TextFit::kMinFitSize) continue;  // shrinks to kMinFitSize before cutting
            if (used > room && Menu().textLog.cut.insert("legend|cut").second) {  // final review 2: once a menu (Rearrange draws it again)
                logger::info("HoM block: the tree legend needs {:.0f} of {:.0f} stage units at size {:.0f} - what runs past the border is cut "
                             "(logged once this menu)",
                    used, room, size);
            }
            static_assert(kItems.size() > 1, "the legend's spare room is shared between its items");
            const double gap = std::max(0.0, (room - TextFit::kGutter / 2 - used) / static_cast<double>(kItems.size() - 1));
            const double right = kPad + room;
            double x = kPad + TextFit::kGutter / 2;
            for (std::size_t i = 0; i < kItems.size(); ++i) {
                if (x + icon > right) {  // not even the mark fits: neither is drawn (review round 1 #6)
                    if (fields[i].IsDisplayObject()) fields[i].Invoke("removeTextField");
                    continue;
                }
                const auto drawn = DrawHomMark(legend, *style, { a_school, {}, {}, 0, kTurnX, 0.0, x + reach, y, kIconScale },
                    { kItems[i].display, style->Mystery(), false, false, 1.0 });
                if (drawn.question) {
                    // the locked icon's "?" as on the tree's mystery marks (review v11 #8), in a clip of its own
                    const auto clipName = std::format("q{}", i);
                    auto mark = Draw::Clip(legend, clipName.c_str());
                    if (mark.IsDisplayObject()) DrawQuestion(a_movie, mark, x + reach, y, kIconScale, drawn, Metrics());
                }
                x += icon + kLegendTextGap;
                if (!fields[i].IsDisplayObject()) continue;
                // the last text now ends at the border exactly: rounding in the sums is not an overflow
                if (x + TextW(fields[i]) > right + kRectTolerance) {
                    // the text ends in "..." at the border, or goes when not even that fits
                    fields[i].Invoke("removeTextField");
                    if (right - x >= size) {
                        const auto name = std::format("t{}", i);
                        fields[i] = FitLine(a_movie, legend, name.c_str(), x, y - h / 2, right - x, size, HomStrings::Text(kItems[i].text), kDimText);
                    }
                    x = right;
                    continue;
                }
                fields[i].SetMember("_x", x);
                x += TextW(fields[i]) + gap;
            }
            return;
        }
    }
}
