// =============================================================================
// PROGRESS CHOICES - the progress card's next-spell rows (plan 2026-11-03,
// progress-next-pick; rules in ChoiceRules.h): with no learning target in the
// active school the card lists the spells the next-learnable key walks, the last
// target's tree children first, as rows under a heading; a row's release (its own
// AS2 handler, kFlagOwnMouse - press and release on the same row) is recorded and
// acted on in the progress block's frame: the same pick as the U key and the same
// set-target request as the node card's button and the G key (Frame.cpp). The
// rows fit the card's registered height and the column's visible room
// (Context::columnRoom, measured each frame; the session's last one at a menu's
// first draw). A row under the mouse shows a faint band (its own handlers).
// =============================================================================

#include "Common.h"

#include "Cards.h"

#include "AddonState.h"
#include "ChoiceRules.h"
#include "HomSettings.h"
#include "TreeRules.h"

#include <algorithm>
#include <format>
#include <utility>

namespace HomAddon
{
    namespace
    {
        using HomStrings::Fill;
        using HomStrings::Str;
        using HomStrings::Text;

        constexpr const char* kNodeMember = "choiceNode";  // a row clip's tree node (the release handler reads it)
        constexpr const char* kRowMember = "choiceRow";    // a row clip's index (the hover)
        constexpr const char* kHoverChild = "hover";
        constexpr double kHoverAlpha = 18.0;  // the band under the mouse: kTitleColor at this alpha (the text stays readable)
        constexpr double kHitAlpha = 0.0;     // the row's hit fill: invisible, still hit by the mouse (AS2 tests shapes, not alpha)

        static_assert(ChoiceRules::kDefaultMaxChoices == HomSettings::Values{}.maxChoices && ChoiceRules::kMinMaxChoices == HomSettings::kMinChoices &&
                          ChoiceRules::kMaxMaxChoices == HomSettings::kMaxChoices,
            "iMaxChoices: one default and range");

        void ShowHover(RE::GFxValue& a_row, bool a_shown)
        {
            RE::GFxValue hover;
            if (a_row.GetMember(kHoverChild, &hover) && hover.IsDisplayObject()) hover.SetMember("_visible", a_shown);
        }

        // The rows' handlers only record (the frame acts: no clip is removed inside its own handler); events are ignored while the
        // last Context was in the layout mode (its covers lie above the panels)
        class RowRelease final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr || Menu().layoutMode) return;
                const auto node = Number(*a_params.thisPtr, kNodeMember);
                if (!node) return;
                Menu().choiceClick = MenuState::ChoiceClick{ static_cast<int>(*node), RootMouse(a_params.movie) };
            }
        };

        class RowRollOver final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr || Menu().layoutMode) return;
                ShowHover(*a_params.thisPtr, true);
                if (const auto row = Number(*a_params.thisPtr, kRowMember)) Menu().choiceHover = static_cast<int>(*row);
            }
        };

        class RowRollOut final : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a_params) override
            {
                EnsureMenu();
                if (!a_params.thisPtr) return;
                ShowHover(*a_params.thisPtr, false);
                const auto row = Number(*a_params.thisPtr, kRowMember);
                if (row && Menu().choiceHover == static_cast<int>(*row)) Menu().choiceHover = -1;
            }
        };

        // Made once per process on the UI thread, never released (SKSE plugins are not unloaded)
        struct Handlers
        {
            RE::GPtr<RowRelease> release = RE::make_gptr<RowRelease>();
            RE::GPtr<RowRollOver> rollOver = RE::make_gptr<RowRollOver>();
            RE::GPtr<RowRollOut> rollOut = RE::make_gptr<RowRollOut>();
        };

        const Handlers& TheHandlers()
        {
            static const Handlers s_handlers;
            return s_handlers;
        }

        // The rows' order: the school's last target's tree children first (ChoiceRules::Order)
        std::vector<int> OrderedChoices(const SchoolInfo& a_info, int& a_children)
        {
            const auto& session = TheSession();
            std::vector<int> parents;
            if (const auto last = session.lastTarget.find(a_info.school); last != session.lastTarget.end()) {
                if (const auto nodes = Data().byGameId.find(last->second); nodes != Data().byGameId.end()) {
                    std::ranges::copy_if(nodes->second, std::back_inserter(parents), [&](int a_node) { return Data().tree.nodes[a_node].school == a_info.school; });
                }
            }
            const auto children = ChoiceRules::ChildrenOf(Data().tree, parents);
            a_children = static_cast<int>(std::ranges::count_if(a_info.candidates, [&](int a_node) { return children.contains(a_node); }));
            return ChoiceRules::Order(a_info.candidates, children);
        }

        // One row: a clip at a_y holding the band, the hit fill and the text "<name> · <level>" (the name alone, cut, when the level
        // does not fit beside it)
        void DrawRow(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, int a_row, int a_node, double a_y, double a_w, double a_size)
        {
            const auto name = std::format("choice{}", a_row);
            auto row = Draw::Clip(a_card, name.c_str());
            if (!row.IsDisplayObject()) return;
            const double h = FieldH(a_size);
            row.SetMember("_y", a_y);
            row.SetMember(kNodeMember, static_cast<double>(a_node));
            row.SetMember(kRowMember, static_cast<double>(a_row));
            auto hover = Draw::Clip(row, kHoverChild);
            if (hover.IsDisplayObject()) {
                Draw::NoLine(hover);
                Draw::Rect(hover, 0, 0, a_w, h, kTitleColor, kHoverAlpha);
                hover.SetMember("_visible", false);
            }
            Draw::NoLine(row);
            Draw::Rect(row, 0, 0, a_w, h, kPanelFill, kHitAlpha);
            const std::string spell = Spells::NodeName(a_node);
            const auto& level = Data().tree.nodes[a_node].skillLevel;
            auto& log = Menu().textLog;
            auto field = Draw::Text(a_movie, row, "label", 0, 0, a_w, h, "", kWhite, a_size);
            if (field.IsDisplayObject()) {
                const auto full = level.empty() ? spell : Fill(Str::kProgressChoice, { { "{spell}", spell }, { "{level}", LevelName(level) } });
                TextFit::SetText(a_movie, field, full);
                if (!TextFit::TextFits(Metrics(), field, a_w, a_size, 1)) TextFit::FitWhole(Metrics(), log, a_movie, field, a_w, a_size, 1, spell);
            }
            const auto& handlers = TheHandlers();
            SetHandler(a_movie, row, "onRelease", handlers.release.get());
            SetHandler(a_movie, row, "onRollOver", handlers.rollOver.get());
            SetHandler(a_movie, row, "onRollOut", handlers.rollOut.get());
        }
    }

    double ProgressRegisteredHeight(double a_iniH, int a_maxChoices)
    {
        static_assert(TextFit::kGutter == 4.0 && kLineFactor == 1.25, "the fallback metrics ChoiceRules' checks use");
        const TextFit::Metrics fallback{ 0.0, kLineFactor, kLineFactor };  // before the first menu: the list row size not measured
        const double row = TextFit::Size(fallback, kProgressHintSize);
        const ChoiceRules::Sizes sizes{ TextFit::Size(fallback, kProgressSchoolSize), row, row };
        return ChoiceRules::RegisteredHeight(a_iniH, a_maxChoices, TextFit::LayoutMetrics(fallback), sizes, kCardTop, kCardBottom);
    }

    std::optional<double> DrawChoices(RE::GFxMovieView* a_movie, RE::GFxValue& a_card, const ChoicesArea& a_area, const SchoolInfo& a_info)
    {
        auto& menu = Menu();
        auto& log = menu.textLog;
        const auto metrics = Metrics();
        const double size = Size(kProgressHintSize), rowH = FieldH(size);
        const double bottom = ChoiceRules::RowsBottom(a_area.fullH, kCardBottom, menu.progressRoom, kCardPad);
        // the school title goes when the heading and one row do not fit under it (the message's rule, ProgressCard.cpp makeRoom)
        double y = a_area.y;
        if (a_area.titleShown && y + rowH + rowH > bottom + ChoiceRules::kRoomTolerance) y = a_area.dropTitle(rowH + rowH);
        const auto heading = TextFit::FitLine(metrics, log, a_movie, a_card, "heading", 0, y, a_area.w, size, Text(Str::kProgressPickHeading), kLabelColor);
        Q2State(Str::kProgressPickHeading, Text(Str::kProgressPickHeading), &heading);
        const double rowsTop = y + rowH;
        int children = 0;
        const auto ordered = OrderedChoices(a_info, children);
        const int maxChoices = HomSettings::Get().maxChoices;
        const auto plan = ChoiceRules::PlanRows({ static_cast<int>(ordered.size()), maxChoices, rowsTop, rowH, bottom });
        for (int i = 0; i < plan.rows; ++i) {
            DrawRow(a_movie, a_card, i, ordered[static_cast<std::size_t>(i)], rowsTop + rowH * i, a_area.w, size);
        }
        if (plan.moreLine) {
            const auto more = Fill(Str::kProgressChoicesMore,
                { { "{n}", std::to_string(plan.more) }, { "{key}", HomStrings::KeyLabel(HomSettings::Get().keyNext) } });
            const auto field = TextFit::FitLine(metrics, log, a_movie, a_card, "more", 0, rowsTop + rowH * plan.rows, a_area.w, size, more, kLabelColor);
            Q2State(Str::kProgressChoicesMore, more, &field);
        }
        menu.choicesShown = true;
        menu.choicesDrawnRoom = menu.progressRoom;
        menu.choiceHover = -1;  // new row clips: none is under the mouse until it moves
        const auto line = std::format("{}|{}|{}|{}", a_info.school, plan.rows, plan.more, children);
        if (menu.choicesLogged.insert(line).second) {
            const auto last = TheSession().lastTarget.find(a_info.school);
            logger::info("HoM block: {} has no learning target - {} row(s) of {} candidate(s), {} more; last target {} ({} of its children "
                         "listed first); room {:.1f} under the card ({})",
                a_info.school, plan.rows, ordered.size(), plan.more,
                last != TheSession().lastTarget.end() ? std::format("{:08X}", last->second) : std::string("unknown this session"), children,
                bottom, menu.progressRoom ? std::format("column room {:.1f}", *menu.progressRoom) : std::string("the card's own height"));
            const bool inside = plan.end <= bottom + ChoiceRules::kRoomTolerance;
            SelfTest(std::format("C1 choices {} - {} row(s) + {} line(s) end at {:.1f}, the room ends at {:.1f}{}", inside ? "PASS" : "WARN", plan.rows,
                plan.moreLine ? 1 : 0, plan.end, bottom, inside ? "" : " (one row is always shown: the column has less room than one row)"));
        }
        return plan.end + kCardBottom;
    }

    int ProgressChoicesFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const BlockApi::Context& a_context)
    {
        auto& menu = Menu();
        // the column's room: the rows are drawn again when it changed (a block above refitted, another layout); never from the scroll
        if (a_context.structSize >= BlockApi::kContextColumnRoomNeeded) {
            menu.progressRoom = static_cast<double>(a_context.columnRoom);
            TheSession().progressRoom = menu.progressRoom;
            if (menu.choicesShown && ChoiceRules::RedrawForRoom(menu.choicesDrawnRoom, menu.progressRoom)) {
                menu.choicesDrawnRoom = menu.progressRoom;
                RequestRedraw(BlockKind::kProgress);
            }
        }
        if (a_context.layoutMode && menu.choiceHover >= 0) {
            const auto name = std::format("choice{}", menu.choiceHover);
            auto card = Child(a_content, "card");
            auto row = card.IsDisplayObject() ? Child(card, name.c_str()) : RE::GFxValue{};
            if (row.IsDisplayObject()) ShowHover(row, false);
            menu.choiceHover = -1;
        }
        const auto click = std::exchange(menu.choiceClick, std::nullopt);
        if (!click || a_context.layoutMode || !a_movie) return -1;
        // the release's place judged against the column's visible part, as the tree's node clicks (setMask's click masking is
        // unverified in Skyrim's Scaleform); a place that could not be read is acted on
        TreeRules::Point at;
        RE::GFxValue root;
        if (click->rootAt && a_movie->GetVariable(&root, "_root")) at = MapPoint(a_movie, root, a_content, click->rootAt->first, click->rootAt->second);
        const TreeRules::View view{ a_context.viewLeft, a_context.viewTop, a_context.viewRight, a_context.viewBottom };
        if (!TreeRules::ClickInView(at, view)) {
            logger::info("HoM block: progress card row click outside the column's visible part ignored ({:.1f},{:.1f}; visible {:.0f},{:.0f}..{:.0f},{:.0f})",
                at->first, at->second, view.left, view.top, view.right, view.bottom);
            return -1;
        }
        return click->node;
    }
}
