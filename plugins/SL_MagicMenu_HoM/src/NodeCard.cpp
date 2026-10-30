// =============================================================================
// NODE CARD - the clicked or key-picked node (else the school's learning target):
// name, school and level, its state in HoM's look, its requirements, its XP and
// the set-target button (the core's Widget.cpp W:7322-7337 StateLine and
// W:7414-7603 at f83dc6f3: Mark, Row, RequirementRows, DrawRows, DrawButton,
// RedrawNodeCard; 2026-10-24 user layout, mockup v8). Ported unchanged but for:
// the card clip in the content clip the host lends (CardClip), TextFit with the
// host's metrics, the fit sent as 'SLMH' with the bottom part hung from the height
// the block is drawn at, "chosen" with the U key's pick (bug G1, Selection.h) and
// the button's own AS2 handler (kFlagOwnMouse: it only records, the frame acts).
// SelfTest L2 node button bottom (W:8081-8116) reads the button back in the block.
// =============================================================================

#include "Common.h"

#include "Cards.h"

#include "AddonState.h"
#include "HomSettings.h"
#include "TreeMarks.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace HomAddon
{
    namespace
    {
        using HomStrings::Fill;
        using HomStrings::SchoolText;
        using HomStrings::Str;
        using HomStrings::Text;
        using State = Spells::State;

        // Node card (the mockup's layout: name, school and level, state with its mark, requirements, XP, button)
        // The bottom block hangs from the button: its bottom at the card bottom minus kPad (spec v6 L2), the XP line and the
        // bottom rule above it as in build 5 (build 5: 38 / 70 / 76)
        constexpr double kNameSize = 19.0, kSubSize = 12.0, kStateSize = 14.0, kStateIconR = 6.0, kStateTextX = 22.0, kRuleGap = 4.0,
                         kRowSize = 13.0, kMarkShare = 0.8, kMarkGap = 4.0, kButtonH = 28.0, kButtonSize = 14.0,
                         kButtonFromBottom = kPad + kButtonH, kXpFromBottom = kButtonFromBottom + 32.0,
                         kBottomRuleFromBottom = kButtonFromBottom + 38.0;
        constexpr int kNameLines = 2, kHintLines = 2;
        constexpr std::uint32_t kOk = 0x6CC070, kNotYet = 0xE05050;
        constexpr double kBaselineTolerance = 0.5;  // L2 node button bottom: read back this close
        constexpr int kStableFrames = 2;            // frames the node card's content must stay the same before L2 reads it back
        constexpr int kHeightWaitFrames = 60;       // frames L2 waits for the box to take its asked height (the host's held shrink: 150 ms)
        // No check/cross characters: the game font has neither U+2713 nor U+2717 (build 3 frame f_008: a missing-glyph box at the
        // start of each requirement line); Draw::Check / Draw::Cross draw them as lines. U+00B7 "·" is in the font.

        enum class Mark
        {
            kNone,   // a heading: from the left edge
            kCheck,  // a drawn check (kOk), then the text
            kCross,  // a drawn cross (kNotYet), then the text
            kSpace   // no mark, the text lined up with the marked rows
        };

        struct Row
        {
            std::string name;           // the whole text, or with a format the {name} it fills (cut first when the row is too long)
            std::optional<Str> format;
            std::uint32_t color;        // the text's; a check or cross takes its own state colour (UI polish 2026-10-29)
            Mark mark;
        };

        // One text field per line (two fields placed by an estimated width overlapped on a Korean name, game run 2026-10-25)
        std::vector<Row> RequirementRows(const TreeData::Node& a_node, const Spells::Snapshot& a_snapshot)
        {
            std::vector<Row> rows;
            // a state the snapshot does not have yet is "checking", never a cross (game run 2: a cross from missing data)
            const auto add = [&](std::uint32_t a_saved) {
                const auto index = Spells::FindSaved(a_saved, a_node.school).first;
                // each prerequisite named by its own state (review v11 #1; spellCardNeeds.js:85)
                std::string name = index >= 0 ? ShownName(index, a_snapshot) : std::format("{:08X}", a_saved);
                switch (Spells::PrereqOf(a_snapshot, a_saved, a_node.school)) {
                case Spells::Prereq::kMastered: rows.push_back({ std::move(name), Str::kReqMastered, kLabelColor, Mark::kCheck }); break;
                case Spells::Prereq::kNotMastered: rows.push_back({ std::move(name), Str::kReqNotMastered, kLabelColor, Mark::kCross }); break;
                case Spells::Prereq::kUnknown: rows.push_back({ std::move(name), Str::kReqChecking, kDimText, Mark::kSpace }); break;
                case Spells::Prereq::kNotInGame: rows.push_back({ std::move(name), Str::kReqNotInGame, kDimText, Mark::kSpace }); break;
                }
            };
            if (!a_node.hard.empty()) {
                rows.push_back({ Text(Str::kReqAll), std::nullopt, kTitleColor, Mark::kNone });
                for (const auto saved : a_node.hard) add(saved);
            }
            if (!a_node.soft.empty() && a_node.softNeeded > 0) {
                rows.push_back({ Fill(Str::kReqSome, { { "{n}", std::to_string(a_node.softNeeded) } }), std::nullopt, kTitleColor, Mark::kNone });
                for (const auto saved : a_node.soft) add(saved);
            }
            if (rows.empty()) rows.push_back({ Text(Str::kReqNone), std::nullopt, kTitleColor, Mark::kNone });
            return rows;
        }

        // The rows from a_top down to a_bottom at the reading size; the last row that fits says how many more there are. Returns
        // the height the drawn rows take (LayoutMath::RowsHeight)
        double DrawRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, std::vector<Row> a_rows, double a_w, double a_top, double a_bottom)
        {
            const auto metrics = Metrics();
            auto& log = Menu().textLog;
            const double size = Size(kRowSize), h = FieldH(size), pitch = LineH(size) + TextFit::kGutter / 2;
            const auto fit = LayoutMath::RowsFit(a_bottom - a_top, h, pitch);
            if (fit == 0) {
                if (!a_rows.empty() && log.cut.insert("rows|no room").second) {
                    logger::info("{}: Q4 node card has no room for its {} requirement line(s) ({:.0f} high, one needs {:.0f})", kLogPrefix, a_rows.size(),
                        a_bottom - a_top, h);
                }
                return 0.0;
            }
            if (a_rows.size() > fit) {
                // a heading left as the last shown line, with none of its spells under it, goes too (review round 2 #3); "N more"
                // counts the spells left out, not their heading lines (review round 1 #5)
                auto keep = fit - 1;
                while (keep > 0 && !a_rows[keep - 1].format) --keep;
                const auto hidden = std::count_if(a_rows.begin() + static_cast<std::ptrdiff_t>(keep), a_rows.end(),
                    [](const Row& a_row) { return a_row.format.has_value(); });
                a_rows.resize(keep);
                if (hidden > 0) {
                    a_rows.push_back({ Fill(Str::kReqMore, { { "{n}", std::to_string(hidden) } }), std::nullopt, kDimText, Mark::kNone });
                }
            }
            const double markSize = size * kMarkShare, textX = markSize + kMarkGap;
            for (std::size_t i = 0; i < a_rows.size() && i < fit; ++i) {
                const auto& row = a_rows[i];
                const double y = a_top + pitch * static_cast<double>(i);
                const double x = row.mark == Mark::kNone ? 0.0 : textX;
                const double markY = y + h / 2 - markSize / 2;
                if (row.mark == Mark::kCheck) Draw::Check(a_content, 0, markY, markSize, kOk);
                if (row.mark == Mark::kCross) Draw::Cross(a_content, 0, markY, markSize, kNotYet);
                const auto name = std::format("row{}", i);
                auto field = Draw::Text(a_movie, a_content, name.c_str(), x, y, a_w - x, h, "", row.color, size);
                if (!field.IsDisplayObject()) continue;
                if (row.format) {
                    TextFit::FitText(metrics, log, a_movie, field, a_w - x, size, 1, row.name,
                        [&](const std::string& a_name) { return Fill(*row.format, { { "{name}", a_name } }); });
                } else {
                    TextFit::FitWhole(metrics, log, a_movie, field, a_w - x, size, 1, row.name);
                }
            }
            return LayoutMath::RowsHeight(std::min(a_rows.size(), fit), h, pitch);
        }

        // The set-target button's own handler (kFlagOwnMouse): it only records the press - the frame acts on it (no clip is
        // changed inside its own handler). Made once per process, never released (SKSE plugins are not unloaded)
        class ButtonClick final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params&) override
            {
                if (Menu().layoutMode) return;  // the layout mode owns the mouse (its covers lie above the panels)
                PressButton();
            }
        };

        RE::GFxFunctionHandler* ButtonHandler()
        {
            static RE::GPtr<ButtonClick> s_handler = RE::make_gptr<ButtonClick>();
            return s_handler.get();
        }

        // a_canSet false: the card shows a node nobody selected (L3's default view), which the button cannot act on
        void DrawButton(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, double a_w, double a_y, const Spells::Look& a_look, bool a_canSet)
        {
            auto button = Draw::Clip(a_content, "button");
            if (!button.IsDisplayObject()) return;
            button.SetMember("_y", a_y);
            const bool enabled = a_canSet && a_look.state == State::kAvailable;
            // one look for both states, the panels' own (UI polish 2026-10-29): the label's brightness tells them apart
            Draw::LineStyle(button, kEdgeWidth, kPanelEdge);
            Draw::Rect(button, 0, 0, a_w, kButtonH, kPanelFill, Draw::kAlphaFull);
            const std::string label = enabled ? Fill(Str::kNodeSetTarget, { { "{key}", HomStrings::KeyLabel(HomSettings::Get().keySetTarget) } }) :
                                                StateLine(a_look.state, a_look.percent, true).text;
            const double size = Size(kButtonSize);
            TextFit::FitLine(Metrics(), Menu().textLog, a_movie, button, "label", 0, (kButtonH - FieldH(size)) / 2, a_w, size, label,
                enabled ? kWhite : kDimText, "center");
            if (enabled) SetHandler(a_movie, button, "onRelease", ButtonHandler());
            auto& menu = Menu();
            menu.buttonDrawn = true;
            menu.buttonBottom = a_y + kButtonH;
        }

        // a_boxW / a_boxH: the card's box as the host drew it now; the rooms come from its registered (full) height
        void RedrawNodeCard(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, double a_boxW, double a_boxH, RE::FormID a_shown,
            const Spells::Snapshot& a_snapshot)
        {
            auto content = CardClip(a_clip);
            if (!content.IsDisplayObject()) return;
            auto& menu = Menu();
            const auto& session = TheSession();
            const auto metrics = Metrics();
            auto& log = menu.textLog;
            // user 2026-10-30: the rooms from the card's full height, the bottom part at the height it is drawn at (its fit)
            const double w = a_boxW - 2 * kPad, fullH = RegisteredHeight(BlockKind::kNodeCard);
            // What the card shows: the clicked (or key-picked) node; with none, the school's learning target as if it had been
            // clicked (spec v6 L3), only on this card - selected stays -1, so the tree neither recentres nor draws a selection for
            // it and no click path (list selection, P2 card, P3 preview) runs
            // user 2026-10-30 (the board): nothing chosen - the hint line only; bug G1: a U key pick counts as chosen
            // Task 6 review 3: a key pick made under another form is dropped here already (the host draws the card for a new form
            // before the tree's frame drops the pick)
            const bool chosen = Chosen(a_shown);
            int shown = Spells::Get() && chosen ? SelectedFor(a_shown) : -1;
            const bool byDefault = shown < 0;
            if (Spells::Get() && byDefault && chosen && !session.school.empty()) {
                const RE::FormID target = a_snapshot.Target(session.school);
                shown = Spells::FindNode(target, session.school);
                if (target && shown < 0) {
                    // a target outside the tree: the hint stays (the progress card names it)
                    const auto* form = RE::TESForm::LookupByID(target);
                    Q2State("node_default_target_not_in_tree", std::format("{:08X} {} - the hint is shown", target,
                        form && form->GetName() ? form->GetName() : "(no form)"));
                }
            }
            if (shown < 0) {
                // a short hint at the top (Q2), not a big centred line
                const auto text = Fill(Str::kNodeHint, { { "{key}", HomStrings::KeyLabel(HomSettings::Get().keyNext) } });
                const double size = Size(kRowSize);
                const auto hint = TextFit::FitLines(metrics, log, a_movie, content, "hint", 0, kCardTop, w, size, kHintLines, text, kDimText);
                Q2State(Str::kNodeHint, text, &hint);
                const int lines = hint.IsDisplayObject() ? std::clamp(TextFit::TextLines(metrics, hint, size), 1, kHintLines) : kHintLines;
                FitCard(BlockKind::kNodeCard, kCardTop + FieldH(size, lines) + kPad);  // only the hint line
                return;
            }
            const auto& node = Data().tree.nodes[shown];
            const auto look = Spells::LookOf(a_snapshot, shown);
            double y = kCardTop;
            // the name wraps to two lines, then ends in "..." (Q4); what follows moves down by the lines it took
            // "???" while HoM keeps the name hidden (review v11 #1; spellCardView.js:213-218)
            auto name = TextFit::FitLines(metrics, log, a_movie, content, "name", 0, y, w, kNameSize, kNameLines, ShownName(shown, a_snapshot), kWhite);
            y += name.IsDisplayObject() ? std::clamp(TextFit::TextH(name), FieldH(kNameSize), FieldH(kNameSize, kNameLines)) : FieldH(kNameSize);
            const double subSize = Size(kSubSize);
            const auto school = SchoolText(node.school);
            const auto level = LevelName(node.skillLevel);
            TextFit::FitLine(metrics, log, a_movie, content, "sub", 0, y, w, subSize,
                level.empty() ? school : Fill(Str::kNodeSub, { { "{school}", school }, { "{level}", level } }), kTitleColor);
            y += FieldH(subSize);
            const double stateSize = Size(kStateSize), stateH = FieldH(stateSize);
            {
                // the state in HoM's look (spec v11 T13) at the known size kStateIconR, the node's school shape and colour
                constexpr double kIconScale = kStateIconR / HomStyle::kKnownSize;
                const auto style = CurrentStyle();
                const bool isTarget = shown == Spells::FindNode(a_snapshot.Target(node.school), node.school);
                const double reach = HomStyle::MaxReach(kIconScale, style->tree.availableRing);
                DrawHomMark(content, *style, { node.school, node.themeColor, node.skillLevel, node.tier, node.x, node.y, reach, y + stateH / 2, kIconScale },
                    { HomStyle::DisplayOf(BehaviourOf(look.state), isTarget), style->Mystery(), false, false, 1.0 });
            }
            const auto state = StateLine(look.state, look.percent, false);
            const auto stateField = TextFit::FitLine(metrics, log, a_movie, content, "state", kStateTextX, y, w - kStateTextX, stateSize, state.text, kWhite);
            Q2State(state.id, state.text, &stateField);
            if (byDefault) Q2State("node_default_target", std::format("{} - {}", Spells::NodeName(shown), state.text));
            if (!byDefault && KeyPickFor(a_shown) >= 0) Q2State("node_next_candidate", std::format("{} - {}", Spells::NodeName(shown), state.text));
            y += stateH + kRuleGap;
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, y);
            Draw::LineTo(content, w, y);

            const double rowsTop = y + kRuleGap;
            const double rowsH = DrawRows(a_movie, content, RequirementRows(node, a_snapshot), w, rowsTop, fullH - kBottomRuleFromBottom - kRuleGap);
            // the card ends under its rows: the bottom rule, the XP line and the button hang from the bottom of the box it is drawn
            // in (after a 'SLMH' the host draws it again at the asked height, which asks for the same one)
            FitCard(BlockKind::kNodeCard, LayoutMath::NodeCardNeeded(rowsTop, rowsH, kRuleGap, kBottomRuleFromBottom));
            const double h = a_boxH;
            const double bottomRule = h - kBottomRuleFromBottom;
            Draw::LineStyle(content, kEdgeWidth, kPanelEdge);
            Draw::MoveTo(content, 0, bottomRule);
            Draw::LineTo(content, w, bottomRule);
            const auto* spell = a_snapshot.Spell(Data().gameIds[shown]);
            const float required = spell ? spell->required : 0.0f;
            TextFit::FitLine(metrics, log, a_movie, content, "xp", 0, h - kXpFromBottom, w, stateSize,
                Fill(Str::kXp, { { "{cur}", std::format("{:.0f}", required * look.percent / 100.0f) }, { "{need}", std::format("{:.0f}", required) } }),
                kLabelColor, "right");
            DrawButton(a_movie, content, w, h - kButtonFromBottom, look, !byDefault);
        }
    }

    // The node card's state words; on the button a target says only that it is the target
    Line StateLine(State a_state, float a_percent, bool a_button)
    {
        const auto pct = std::format("{:.0f}", a_percent);
        switch (a_state) {
        case State::kTarget:
            return a_button ? Line{ Str::kNodeIsTarget, Text(Str::kNodeIsTarget) } :
                              Line{ Str::kNodeTargetPct, Fill(Str::kNodeTargetPct, { { "{pct}", pct } }) };
        case State::kLearning: return { Str::kNodeLearningPct, Fill(Str::kNodeLearningPct, { { "{pct}", pct } }) };
        case State::kAvailable: return { Str::kNodeCanLearn, Text(Str::kNodeCanLearn) };
        case State::kReady: return { Str::kNodeReady, Text(Str::kNodeReady) };
        case State::kMastered: return { Str::kNodeMastered, Text(Str::kNodeMastered) };
        case State::kLocked: return { Str::kNodeLocked, Text(Str::kNodeLocked) };
        default: return { Str::kNodeUnknown, Text(Str::kNodeUnknown) };
        }
    }

    void DrawNodeCardBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void*)
    {
        EnsureMenu();
        if (!a_movie) return;
        auto& menu = Menu();
        menu.drawnWith[Index(BlockKind::kNodeCard)] = menu.metrics;
        menu.buttonDrawn = false;
        menu.nodeContentH = a_height;
        const auto snapshot = Spells::Current();
        RedrawNodeCard(a_movie, a_clip, a_width + 2 * kCardPad, a_height + 2 * kCardPad, a_shown, *snapshot);
    }

    // The node card's button ends at the card's bottom minus the padding (spec v6 L2), read back once per menu as drawn: the
    // button rect's bottom (its _y + kButtonH, not the clip's bounds: its centred label can hang below a 28-high rect at large
    // reading sizes, build 6 review round 2 #2) and the card box's bottom minus kPad, both mapped to _root. At f83dc6f3 the
    // reference was the item card slot's bottom (the two built-in blocks ended at one bottom); the add-on cannot see the
    // host's spell card, so this part only checks the card clip's offset and the bottom anchoring in GFx (both sides come from
    // the drawn height). Task 6 review 5: the box's height is also compared with the height the add-on asked for ('SLMH',
    // else the registered one) - a value the draw did not make. Read once the node card's content clip has stayed the same for
    // kStableFrames frames (a fit's redraw makes a new one) and its box has its asked height (a held shrink waits up to
    // kHeightWaitFrames)
    void NodeCardFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue*, const BlockApi::Context& a_context, void*)
    {
        EnsureMenu();
        auto& menu = Menu();
        if (menu.buttonChecked || !HomSettings::Get().selfTest || !a_movie || a_context.layoutMode) return;
        if (a_context.contentGeneration != menu.nodeContentGeneration) {
            menu.nodeContentGeneration = a_context.contentGeneration;
            menu.nodeStableFrames = 0;
            return;
        }
        if (++menu.nodeStableFrames < kStableFrames || !menu.buttonDrawn) return;
        const double asked = menu.heights[Index(BlockKind::kNodeCard)].Sent().value_or(RegisteredHeight(BlockKind::kNodeCard));
        const double box = menu.nodeContentH + 2 * kCardPad;
        const bool heightOk = std::abs(box - asked) <= kBaselineTolerance;
        if (!heightOk && ++menu.nodeHeightWait < kHeightWaitFrames) return;  // the host holds a shrink for a while
        RE::GFxValue root;
        auto card = Child(a_content, "card");
        auto button = card.IsDisplayObject() ? Child(card, "button") : RE::GFxValue{};
        if (!button.IsDisplayObject() || !a_movie->GetVariable(&root, "_root")) return;
        menu.buttonChecked = true;
        const auto top = Number(button, "_y");
        const auto edge = top ? MapPoint(a_movie, card, root, 0.0, *top + kButtonH) : std::nullopt;
        // the box's bottom is the content clip's bottom plus the pad below it
        const auto want = MapPoint(a_movie, a_content, root, 0.0, menu.nodeContentH + kCardPad - kPad);
        if (!edge || !want) {
            SelfTest(std::format("L2 node button bottom FAIL - the {} could not be mapped to _root", !edge ? "button rect's bottom" : "card box's bottom"));
            return;
        }
        const bool bottomOk = std::abs(edge->second - want->second) <= kBaselineTolerance;
        SelfTest(std::format("L2 node button bottom {} - card clip offset: rect bottom {:.2f} in _root, want {:.2f} (the drawn box's bottom - "
                             "padding {:.0f}) {}; box height {:.1f}, asked {:.1f} (registered {:.1f}) {}; tolerance {:.1f}",
            bottomOk && heightOk ? "PASS" : "FAIL", edge->second, want->second, kPad, bottomOk ? "ok" : "off", box, asked,
            RegisteredHeight(BlockKind::kNodeCard), heightOk ? "ok" : std::format("off after {} frames", menu.nodeHeightWait), kBaselineTolerance));
    }
}
