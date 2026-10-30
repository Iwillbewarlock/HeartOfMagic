// =============================================================================
// PROGRESS CARD - the active school's learning target, its XP bar and the next
// step (the core's Widget.cpp W:7119-7316 at f83dc6f3, NextStepLine / DrawProgress
// / RedrawProgress). Ported unchanged but for: the card clip in the content clip
// the host lends (CardClip: the same places in the card's box), TextFit with the
// metrics of the host's Context, the fit sent as 'SLMH' (FitCard) and - HoM
// add-on design "Lost or changed on screen" - no row-2 baseline alignment with
// the node card (W:7170-7186: two blocks now), so the plan's offset is 0.
// =============================================================================

#include "Common.h"

#include "Cards.h"

#include "AddonState.h"
#include "Hom.h"
#include "HomSettings.h"

#include <algorithm>
#include <format>
#include <optional>

namespace HomAddon
{
    namespace
    {
        using HomStrings::Fill;
        using HomStrings::SchoolText;
        using HomStrings::Str;
        using HomStrings::Text;

        // Progress card (laid out top-down by line heights of the sizes in use)
        // The XP numbers sit right of the bar in a row as high as their field, the bar centred in it (review round 2 #2: the
        // field centred on the then 9-high bar ran into the target line above and the hint below); they keep kXpSize, not the
        // reading size, so the card's last line keeps room at large list rows. The bar is a thin line (UI polish 2026-10-29):
        // kBarBack behind (a neutral grey: kPanelEdge would read as the card's rule lines at 0%), kTitleColor for the share learned
        constexpr std::uint32_t kBarBack = 0x4A4A4A;
        constexpr double kSchoolSize = 11.0, kTargetSize = 19.0, kBarH = 2.0, kXpW = 120.0, kXpSize = 12.0, kHintSize = 13.0;
        constexpr int kMessageLines = 2;

        // The progress card's lines, laid out top-down in its FULL height (user 2026-10-30: the rooms never depend on its fitted
        // height); the height they need, nullopt when nothing was drawn (no school: the card keeps its height and is hidden)
        std::optional<double> DrawProgress(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, double a_boxW, const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const auto& session = TheSession();
            if (session.school.empty()) return std::nullopt;
            auto content = CardClip(a_content);
            if (!content.IsDisplayObject()) return std::nullopt;
            const auto metrics = Metrics();
            auto& log = menu.textLog;
            const double fullH = RegisteredHeight(BlockKind::kProgress);
            const double w = a_boxW - 2 * kPad;

            const double titleSize = Size(kSchoolSize), hintSize = Size(kHintSize), bottom = fullH - kCardBottom;
            auto title = TextFit::FitLine(metrics, log, a_movie, content, "school", 0, kCardTop, w, titleSize,
                Fill(Str::kProgressTitle, { { "{school}", SchoolText(session.school) } }), kTitleColor);
            double y = kCardTop + FieldH(titleSize);
            const auto dropTitle = [&](double a_need, const char* a_what) {
                title.Invoke("removeTextField");
                title = RE::GFxValue{};
                if (log.cut.insert("progress|title").second) {
                    logger::info("{}: Q4 progress card drops its school title - {} needs {:.1f} of the card's {:.1f}", kLogPrefix, a_what, a_need,
                        bottom - kCardTop);
                }
            };
            // Nothing is drawn past the card (review round 2 #1: with a tall font and large list rows even size 8 ended at 106 of
            // 102): when what has to follow does not fit under the school title, the title goes; an optional last line that does
            // not fit at kMinFitSize is left out. Both logged once per menu.
            const auto makeRoom = [&](double a_need, const char* a_what) {
                if (y + a_need <= bottom || !title.IsDisplayObject()) return;
                dropTitle(a_need, a_what);
                y = kCardTop;
            };
            // no learning target to show: one line (up to two rows) that says what to do, in place of the target and its bar,
            // smaller (one row when two do not fit) when the card has no room for it at the reading size (review round 1 #4)
            const auto message = [&](const Line& a_line) {
                makeRoom(FieldH(TextFit::kMinFitSize), "the message");
                int lines = kMessageLines;
                double size = TextFit::SizeFor(metrics, log, hintSize, lines, bottom - y, nullptr);
                if (FieldH(size, lines) > bottom - y) {
                    lines = 1;
                    size = TextFit::SizeFor(metrics, log, hintSize, lines, bottom - y, "progress message");
                }
                const auto field = TextFit::FitLines(metrics, log, a_movie, content, "message", 0, y, w, size, lines, a_line.text, kWhite);
                Q2State(a_line.id, a_line.text, &field);
                // the lines the text took (one short message: one line), not the field's two
                const int used = field.IsDisplayObject() ? std::clamp(TextFit::TextLines(metrics, field, size), 1, lines) : lines;
                return y + FieldH(size, used) + kCardBottom;
            };
            if (!Spells::Get()) return message({ Str::kProgressNoTree, Text(Str::kProgressNoTree) });
            // the blocks are registered only once HoM's API is there, so this is a guard (review round 1 #3)
            if (!Hom::Ready()) return message({ Str::kProgressNoHom, Text(Str::kProgressNoHom) });
            if (!Spells::SchoolNamed(session.school)) {
                return message({ Str::kProgressNoSchoolTree, Fill(Str::kProgressNoSchoolTree, { { "{school}", SchoolText(session.school) } }) });
            }
            // "loading" only while no snapshot has had the school for less than kNoDataFrames frames (a refresh for it is asked
            // for every kPollFrames frames); after that the card says the data did not arrive (WatchSnapshotCover)
            const auto it = a_snapshot.schools.find(session.school);
            if (it == a_snapshot.schools.end()) {
                return message(menu.noData ? Line{ Str::kProgressNoData, Text(Str::kProgressNoData) } : Line{ Str::kProgressNotReady, Text(Str::kProgressNotReady) });
            }
            const auto& info = SchoolInfoOf(a_snapshot);
            const auto& state = it->second;
            if (!state.target) return message(NextStepLine(info, false));

            // the learning target: name and share, the bar with its XP, and the next step under it
            const int targetNode = Spells::FindNode(state.target, session.school);
            const auto* form = RE::TESForm::LookupByID(state.target);
            const std::string name = targetNode >= 0         ? Spells::NodeName(targetNode) :
                                     form && form->GetName() ? std::string(form->GetName()) :
                                                               std::format("{:08X}", state.target);
            const auto pct = std::format("{:.0f}", state.percent);
            // where each line goes and what gives way (LayoutMath::PlanProgress: the hint shrinks, then the hint goes, then the
            // title; the same function tools/magic-menu-hom-block-check runs); no row-2 offset (HoM add-on design)
            const LayoutMath::ProgressIn planIn{ fullH, kCardTop, kCardBottom, titleSize, kTargetSize, kXpSize, kBarH, hintSize, TextFit::kMinFitSize,
                TextFit::kFitStep, 0.0 };
            const auto plan = LayoutMath::PlanProgress(TextFit::LayoutMetrics(metrics), planIn);
            if (!plan.title && title.IsDisplayObject()) dropTitle(plan.targetH + plan.barRowH, "the learning target and its bar");
            const double barRowH = plan.barRowH, xpH = FieldH(kXpSize);
            y = plan.targetY;
            auto target = Draw::Text(a_movie, content, "target", 0, y, w, plan.targetH, "", kWhite, kTargetSize);
            const auto compose = [&](const std::string& a_name) { return Fill(Str::kProgressTarget, { { "{spell}", a_name }, { "{pct}", pct } }); };
            if (target.IsDisplayObject()) TextFit::FitText(metrics, log, a_movie, target, w, kTargetSize, 1, name, compose);
            Q2State(Str::kProgressTarget, compose(name), &target);
            y = plan.barRowY;

            const double barW = std::max(0.0, w - kXpW - kPad), barY = y + (barRowH - kBarH) / 2;
            Draw::NoLine(content);
            Draw::Rect(content, 0, barY, barW, kBarH, kBarBack, Draw::kAlphaFull);
            if (state.percent > 0) {
                Draw::Rect(content, 0, barY, barW * std::clamp(state.percent / 100.0, 0.0, 1.0), kBarH, kTitleColor, Draw::kAlphaFull);
            }
            const auto xp = Fill(Str::kXp, { { "{cur}", std::format("{:.0f}", state.required * state.percent / 100.0f) },
                                               { "{need}", std::format("{:.0f}", state.required) } });
            TextFit::FitLine(metrics, log, a_movie, content, "xp", barW + kPad, y + (barRowH - xpH) / 2, kXpW, kXpSize, xp, kWhite, "right");
            y = plan.hintY;
            // the list's spell has no tree node: said here instead of the next step (the tree window says it too)
            const Line next = menu.notInTree ? Line{ Str::kProgressNotInTree, Text(Str::kProgressNotInTree) } : NextStepLine(info, true);
            if (!plan.hint) {
                if (log.cut.insert("progress|hint").second) {
                    logger::info("{}: Q4 progress card leaves out its last line '{}' - {:.1f} left, it needs {:.1f} even at size {:.0f}", kLogPrefix,
                        next.text, bottom - y, FieldH(TextFit::kMinFitSize), TextFit::kMinFitSize);
                }
                const RE::GFxValue none;
                Q2State(next.id, next.text, &none);  // logged as "(no field)": the state was reached, its line not shown
                return LayoutMath::ProgressNeeded(TextFit::LayoutMetrics(metrics), planIn, plan);
            }
            if (plan.hintSize < hintSize && log.cut.insert(std::format("progress|hint {:.1f}", plan.hintSize)).second) {
                logger::info("{}: Q4 progress card hint at size {:.1f} (reading size {:.1f}) - {:.1f} left under the bar", kLogPrefix, plan.hintSize,
                    hintSize, bottom - y);
            }
            const auto hint = TextFit::FitLine(metrics, log, a_movie, content, "hint", 0, y, w, plan.hintSize, next.text, kLabelColor);
            Q2State(next.id, next.text, &hint);
            return LayoutMath::ProgressNeeded(TextFit::LayoutMetrics(metrics), planIn, plan);
        }
    }

    // What to do next in the active school (Q2), from its numbers: the line in place of the learning target when there is none,
    // and the line under the target when there is one. Precedence: candidates (counted exactly as the next-learnable key walks
    // them), learnable spells without a tree position, spells at full XP not learned yet, spells learned early and not
    // mastered, locked spells (prerequisites missing), spells of unknown state, every spell mastered.
    Line NextStepLine(const SchoolInfo& a_info, bool a_hasTarget)
    {
        const auto& t = a_info.tally;
        const auto count = [](int a_n) { return std::to_string(a_n); };
        if (t.candidates > 0) {
            const Str id = a_hasTarget ? Str::kProgressCandidates : Str::kProgressNoneCandidates;
            return { id, Fill(id, { { "{n}", count(t.candidates) }, { "{key}", HomStrings::KeyLabel(HomSettings::Get().keyNext) } }) };
        }
        if (t.available > 0) return { Str::kProgressCandidatesNoXY, Fill(Str::kProgressCandidatesNoXY, { { "{n}", count(t.available) } }) };
        if (t.ready > 0) return { Str::kProgressReady, Fill(Str::kProgressReady, { { "{n}", count(t.ready) } }) };
        if (t.learning > 0) return { Str::kProgressLearningEarly, Fill(Str::kProgressLearningEarly, { { "{n}", count(t.learning) } }) };
        if (t.locked > 0) return { Str::kProgressNoneNothing, Text(Str::kProgressNoneNothing) };
        if (t.unknown > 0) return { Str::kProgressUnknown, Fill(Str::kProgressUnknown, { { "{n}", count(t.unknown) } }) };
        if (a_hasTarget) return { Str::kProgressNoOther, Text(Str::kProgressNoOther) };
        if (t.mastered > 0) return { Str::kProgressAllMastered, Text(Str::kProgressAllMastered) };
        return { Str::kProgressNoSchoolTree, Fill(Str::kProgressNoSchoolTree, { { "{school}", SchoolText(a_info.school) } }) };
    }

    // user 2026-10-30: the card fits its lines and bar ('SLMH': the blocks under it move up on the next frame)
    void DrawProgressBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float, std::uint32_t a_shown, void*)
    {
        EnsureMenu();
        if (!a_movie) return;
        SettleShownForDraw(a_shown);  // final review I1: the school of a new form before the card is drawn
        auto& menu = Menu();
        menu.drawnWith[Index(BlockKind::kProgress)] = menu.metrics;
        SyncProgressShown();  // Task 6 review 4: with no school the box is hidden from its first draw, not a frame later
        const auto snapshot = Spells::Current();
        if (const auto needed = DrawProgress(a_movie, a_clip, a_width + 2 * kCardPad, *snapshot)) FitCard(BlockKind::kProgress, *needed);
    }
}
