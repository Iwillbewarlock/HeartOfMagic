// magic-menu-hom-block-check, the progress card's next-spell rows (2026-11-03, progress-next-pick plan): ChoiceRules.h - the
// rows' order (the last target's direct children first, then Spells::Learnable's tier / name / FormID order), the rows that fit
// (at most iMaxChoices, an "N more" line under them, at least one row in a small room), the room from the card's full height and
// the column's room (Context::columnRoom) at four screen shapes, the registered height, the content-clip place of a row; and
// HomSettings.h's [Progress] iMaxChoices. See magic-menu-hom-block-check.cpp for the tool's usage.

#include "magic-menu-hom-block-check.h"

#include "BlockRulesV3.h"
#include "ChoiceRules.h"
#include "HomSettings.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace HomBlockCheck
{
    namespace
    {
        // the fallback metrics at kDataLoaded (HomAddon::kLineFactor 1.25, TextFit::kGutter 4) and the reading size 14 every text
        // takes before the list's row size is measured (TextFit::kListRowFallback; the card's 11 / 13 are below it)
        constexpr LayoutMath::TextMetrics kFallback{ 1.25, 1.25, 4.0 };
        constexpr ChoiceRules::Sizes kFallbackSizes{ 14.0, 14.0, 14.0 };
        constexpr double kCardTop = 4.0, kCardBottom = 2.0, kCardPad = 4.0;  // HomAddon::kCardTop / kCardBottom / kCardPad
        constexpr double kRowH = 14.0 * 1.25 + 4.0;                          // 21.5: one row at the fallback

        // A made-up school: tree nodes 0..7, structural links 0 -> 1, 0 -> 2, 2 -> 5, 6 -> 1 (6 is 0's "_dup_" twin), a lock 0 -> 3
        // and 4 -> 0 (4 is 0's prerequisite)
        TreeData::Tree MadeUpTree()
        {
            TreeData::Tree tree;
            tree.nodes.resize(8);
            tree.adjacent.resize(8);
            const auto link = [&](int a_from, int a_to, TreeData::EdgeKind a_kind) {
                tree.edges.push_back({ a_from, a_to, a_kind });
                const int index = static_cast<int>(tree.edges.size()) - 1;
                tree.adjacent[static_cast<std::size_t>(a_from)].push_back(index);
                tree.adjacent[static_cast<std::size_t>(a_to)].push_back(index);
            };
            using TreeData::EdgeKind;
            link(0, 1, EdgeKind::kStructural);
            link(0, 2, EdgeKind::kStructural);
            link(2, 5, EdgeKind::kStructural);
            link(6, 1, EdgeKind::kStructural);
            link(0, 3, EdgeKind::kLock);
            link(4, 0, EdgeKind::kStructural);
            return tree;
        }

        std::string Show(const std::vector<int>& a_nodes)
        {
            std::string text;
            for (const int node : a_nodes) text += (text.empty() ? "" : ",") + std::to_string(node);
            return text;
        }

        void Order()
        {
            std::printf("choices: the rows' order (the last target's children first, then tier / name / FormID)\n");
            const auto tree = MadeUpTree();
            const std::set<int> children = ChoiceRules::ChildrenOf(tree, { 0 });
            Expect(children == std::set<int>{ 1, 2 }, "children of 0: 1 and 2 (structural links from it; not 3 - a lock -, not 4 - its prerequisite)");
            Expect(ChoiceRules::ChildrenOf(tree, { 0, 6 }) == std::set<int>{ 1, 2 }, "0 with its dup twin 6: 1 once, 2");
            Expect(ChoiceRules::ChildrenOf(tree, { 5 }).empty(), "a leaf (5): no children");
            // the candidates as Spells::Learnable orders them (tier, name, FormID): 7, 3, 5, 2, 1
            const std::vector<int> learnable{ 7, 3, 5, 2, 1 };
            Expect(ChoiceRules::Order(learnable, children) == std::vector<int>{ 2, 1, 7, 3, 5 },
                Text("last target 0: its children 2, 1 first (in their tier order), then 7, 3, 5 - got %s", Show(ChoiceRules::Order(learnable, children)).c_str()));
            Expect(ChoiceRules::Order(learnable, {}) == learnable, "no last target this session: tier / name / FormID as they are");
            Expect(ChoiceRules::Order(learnable, ChoiceRules::ChildrenOf(tree, { 4 })) == learnable,
                "the last target's only child (0) is not a candidate: the order as it is");
            Expect(ChoiceRules::ChildrenOf(tree, { -1, 99 }).empty() && ChoiceRules::Order({}, children).empty(),
                "a parent index out of range (-1, 99): no children, no crash; no candidates: no rows");
            TreeData::Tree broken = tree;
            broken.adjacent[0].push_back(42);  // an edge index past the edges
            Expect(ChoiceRules::ChildrenOf(broken, { 0 }) == std::set<int>{ 1, 2 }, "an edge index past the edges: skipped");
        }

        ChoiceRules::Plan Rows(int a_candidates, int a_max, double a_lines)
        {
            return ChoiceRules::PlanRows({ a_candidates, a_max, 47.0, kRowH, 47.0 + a_lines * kRowH });
        }

        void RowPlans()
        {
            std::printf("choices: the rows that fit (iMaxChoices, the \"more\" line, one row at least)\n");
            const auto three = Rows(3, 4, 10);
            Expect(three.rows == 3 && three.more == 0 && !three.moreLine && Near(three.end, 47.0 + 3 * kRowH, 1e-9), "3 candidates, max 4, room for 10 lines: 3 rows");
            const auto seven = Rows(7, 4, 10);
            Expect(seven.rows == 4 && seven.more == 3 && seven.moreLine && Near(seven.end, 47.0 + 5 * kRowH, 1e-9), "7 candidates, max 4: 4 rows and \"3 more\"");
            const auto exact = Rows(5, 4, 5);
            Expect(exact.rows == 4 && exact.more == 1 && exact.moreLine, "5 candidates, room for exactly 5 lines: 4 rows and \"1 more\" (the room's end counts)");
            const auto four = Rows(4, 4, 4);
            Expect(four.rows == 4 && !four.moreLine && four.more == 0, "4 candidates in 4 lines: 4 rows, no \"more\" line");
            const auto short3 = Rows(5, 4, 3.5);
            Expect(short3.rows == 2 && short3.more == 3 && short3.moreLine, "5 candidates, room for 3 lines: 2 rows and \"3 more\"");
            const auto one = Rows(6, 4, 1);
            Expect(one.rows == 1 && one.more == 5 && !one.moreLine, "room for one line: 1 row, no \"more\" line (no room for it)");
            const auto none = Rows(6, 4, -2);
            Expect(none.rows == 1 && !none.moreLine && Near(none.end, 47.0 + kRowH, 1e-9), "no room at all: still 1 row (at least one choice)");
            const auto zero = Rows(0, 4, 10);
            Expect(zero.rows == 0 && zero.more == 0 && !zero.moreLine && zero.end == 47.0, "no candidates: no rows (the card's message instead)");
            Expect(Rows(5, 0, 10).rows == 1 && Rows(20, 99, 30).rows == ChoiceRules::kMaxMaxChoices, "iMaxChoices out of range: 0 -> 1 row, 99 -> 8 rows");
            const auto nanRoom = ChoiceRules::PlanRows({ 3, 4, 47.0, kRowH, std::nan("") });
            const auto zeroRow = ChoiceRules::PlanRows({ 3, 4, 47.0, 0.0, 200.0 });
            Expect(nanRoom.rows == 1 && zeroRow.rows == 1 && !nanRoom.moreLine, "a room that is not a number or a row height of 0: 1 row");
        }

        void Rooms()
        {
            std::printf("choices: the room (the card's full height, the column's visible room)\n");
            using ChoiceRules::RowsBottom;
            Expect(RowsBottom(157.0, kCardBottom, std::nullopt, kCardPad) == 155.0, "no column room (an older host): the card's full height less 2");
            Expect(RowsBottom(157.0, kCardBottom, 313.0, kCardPad) == 155.0, "a column room of 313: the card's own height decides (155)");
            Expect(RowsBottom(157.0, kCardBottom, 60.0, kCardPad) == 62.0, "a column room of 60: 4 + 60 - 2 = 62 (the column's visible bottom)");
            Expect(RowsBottom(157.0, kCardBottom, std::nan(""), kCardPad) == 155.0, "a room that is not a number: the card's height");
            Expect(ChoiceRules::RedrawForRoom(std::nullopt, 313.0) && ChoiceRules::RedrawForRoom(313.0, 200.0) && !ChoiceRules::RedrawForRoom(313.0, 313.005) &&
                       !ChoiceRules::RedrawForRoom(std::nullopt, std::nullopt),
                "drawn again when the room arrives or changes; not for the same room");
            const auto span = ChoiceRules::ToContent({ 47.0, 68.5 }, -kCardPad);
            Expect(span.top == 43.0 && span.bottom == 64.5, "a row 47..68.5 in the card clip is 43..64.5 in the content clip (the card clip at y -4)");
        }

        void Heights()
        {
            std::printf("choices: the registered and the needed height\n");
            const double needed = ChoiceRules::NeededHeight(kFallback, kFallbackSizes, 5, kCardTop, kCardBottom);
            Expect(Near(needed, 4.0 + 2 * kRowH + 5 * kRowH + 2.0, 1e-9), Text("4 rows + the \"more\" line at the fallback: 4 + title + heading + 5 x 21.5 + 2 = %.1f", needed));
            Expect(ChoiceRules::RegisteredHeight(102.0, 4, kFallback, kFallbackSizes, kCardTop, kCardBottom) == 157.0,
                "registered: the ini's 102 raised to 157 (156.5 up) for 4 rows and the \"more\" line");
            Expect(ChoiceRules::RegisteredHeight(102.0, 1, kFallback, kFallbackSizes, kCardTop, kCardBottom) == 102.0,
                "iMaxChoices 1 (92 needed): the ini's 102 stays");
            Expect(ChoiceRules::RegisteredHeight(300.0, 4, kFallback, kFallbackSizes, kCardTop, kCardBottom) == 300.0, "a taller ini rect (300): kept");
            Expect(ChoiceRules::RegisteredHeight(102.0, 99, kFallback, kFallbackSizes, kCardTop, kCardBottom) ==
                       ChoiceRules::RegisteredHeight(102.0, ChoiceRules::kMaxMaxChoices, kFallback, kFallbackSizes, kCardTop, kCardBottom),
                "iMaxChoices 99: as 8");
            // the game measures larger list rows (row size 20, line 1.2): the registered 157 fits fewer rows - the plan says how many
            const LayoutMath::TextMetrics game{ 1.2, 1.1, 4.0 };
            const double rowH = game.FieldH(20.0), rowsTop = kCardTop + 2 * rowH;
            const auto plan = ChoiceRules::PlanRows({ 7, 4, rowsTop, rowH, 157.0 - kCardBottom });
            Expect(plan.rows == 2 && plan.moreLine && plan.end <= 155.0 + ChoiceRules::kRoomTolerance,
                Text("list rows of 20 in game: 2 rows + \"5 more\" in the registered 157 (ends at %.1f)", plan.end));
        }

        // The default layout (E2) at four screen shapes: the progress card's box at 306 (its content clip at 310), the columns'
        // area 67..623 - the column's visible room (BlockRules::ColumnRoom, the host's Context::columnRoom) and the rows it fits
        void ScreenShapes()
        {
            std::printf("choices: screen shapes 16:9, 16:10, 21:9, 4:3 (the visible frame's bottom on the 1280 x 720 stage)\n");
            struct Shape
            {
                const char* name;
                double visibleBottom;
            };
            constexpr double kRegistered = 157.0, kContentTop = 310.0;
            const double rowsTop = kCardTop + 2 * kRowH;
            for (const Shape shape : { Shape{ "16:9", 720.0 }, Shape{ "16:10", 760.0 }, Shape{ "21:9", 720.0 }, Shape{ "4:3", 840.0 } }) {
                const double room = BlockRules::ColumnRoom(kContentTop, 0.0, 623.0, shape.visibleBottom);
                const double bottom = ChoiceRules::RowsBottom(kRegistered, kCardBottom, room, kCardPad);
                const auto plan = ChoiceRules::PlanRows({ 7, 4, rowsTop, kRowH, bottom });
                Expect(plan.rows == 4 && plan.moreLine && 306.0 + plan.end + kCardBottom <= std::min(623.0, shape.visibleBottom),
                    Text("%s: room %.0f - 4 rows + \"3 more\", the card's bottom %.1f inside the column (623)", shape.name, room, 306.0 + plan.end + kCardBottom));
            }
            // a player's layout with the card low in its column (content top 560): 63 of room - one row and no "more" line under it
            const double lowRoom = BlockRules::ColumnRoom(560.0, 0.0, 623.0, 720.0);
            const auto low = ChoiceRules::PlanRows({ 7, 4, rowsTop, kRowH, ChoiceRules::RowsBottom(kRegistered, kCardBottom, lowRoom, kCardPad) });
            Expect(low.rows == 1 && !low.moreLine, Text("the card low in its column (room %.0f): 1 row", lowRoom));
            // scrolled: the same rows (the room does not follow the scroll)
            const double scrolled = BlockRules::ColumnRoom(kContentTop - 120.0, 120.0, 623.0, 720.0);
            Expect(scrolled == 313.0, "the column scrolled 120: the same room 313, the same rows");
        }

        void MaxChoicesKey()
        {
            std::printf("choices: [Progress] iMaxChoices (HomSettings.h)\n");
            const auto read = [](const char* a_value) {
                return HomSettings::Read([&](std::string_view a_section, std::string_view a_key) -> std::optional<std::string> {
                    if (a_section == "Progress" && a_key == "iMaxChoices") return HomSettings::CleanValue(a_value);
                    return std::nullopt;
                });
            };
            Expect(HomSettings::Values{}.maxChoices == 4, "default 4");
            const auto six = read("6 ; six rows"), one = read("1"), eight = read("8");
            Expect(six.values.maxChoices == 6 && six.warnings.empty() && one.values.maxChoices == 1 && eight.values.maxChoices == 8,
                "6 (a comment after it), 1 and 8 read");
            for (const char* bad : { "0", "9", "four", "2.5" }) {
                const auto r = read(bad);
                Expect(r.values.maxChoices == 4 && r.warnings.size() == 1, Text("'%s': kept at 4, one warning", bad));
            }
        }
    }

    void Choices()
    {
        Order();
        RowPlans();
        Rooms();
        Heights();
        ScreenShapes();
        MaxChoicesKey();
    }
}
