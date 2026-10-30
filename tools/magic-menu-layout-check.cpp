// magic-menu-layout-check - offline check of the magic menu prototype's block layout
// (plugins/SL_MagicMenu/src/Layout.cpp, the same code the widget arranges its panels with):
// the default layout against the ini rects, the column border and its limits, hidden and fill
// blocks per tab (plan D6), the drop slot and the move with hidden / absent ids (plan D3),
// scroll clamping, the column areas and the preview box cut to its column (Task 3), the layout JSON (version, repair of a bad file),
// and the layout mode's pure parts (Task 4): moves keep every block once, cancel restores the entry State, the file safety plan;
// the gamepad / keyboard focus model (Task 5): focus, pick, move, drop, cancel, border, the insertion line and keep-in-view scroll;
// the add-on blocks (Task 6, BlockRegistry.cpp): the registration checks, the id prefix, the order, the window, and the layout
// side (an add-on joins the end of the right column, an absent one keeps its place, a bad height does not move the others);
// the retired ids on disk (HoM add-on Task 2): ToFinal on read, ToLegacy on write, upgrade -> downgrade -> upgrade keeps the place;
// the core alone (Task 5): tree / progress / nodecard are retired ids with no height of their own - the geometry cases give them
// the heights the Heart of Magic block registers (its ini rects), as an add-on taking them over does.
//
//   magic-menu-layout-check     one PASS/FAIL line per check, a total line
//
// Not covered here (widget code, checked by reading and in game only): PlaceStripAndGear / StripContent / MoveStrip (resolving
// SkyUI's bottomBar.buttonPanel, reading its children and the content cache, setting _x/_y; the rule itself,
// BlockRules::PlacePanel, is checked here with made-up mappings), Visibility / AddonsHiddenOn / WatchEmpty, the click delivery
// of the gear and the reset button, and MapPoint itself (the strip's mapping between its parent and the stage goes through it).
//
// No game types. Exit 0 when every check passes, 1 otherwise.

#include "BlockRegistry.h"
#include "BlockRules.h"
#include "BlockRulesV3.h"
#include "Layout.h"
#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Layout::BlockId;
    using Layout::Slot;
    using Layout::State;
    using Settings::Rect;

    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr double kTolerance = 1e-9;

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    bool Near(double a_lhs, double a_rhs) { return std::abs(a_lhs - a_rhs) <= kTolerance; }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    std::string Show(const Rect& a_rect) { return Text("%.1f,%.1f,%.1f,%.1f", a_rect.left, a_rect.top, a_rect.right, a_rect.bottom); }

    std::string Show(const std::vector<BlockId>& a_ids)
    {
        std::string text = "[";
        for (const auto& id : a_ids) {
            text += (text.size() > 1 ? "," : "") + id;
        }
        return text + "]";
    }

    /// The block's rect is exactly l,t,r,b
    void ExpectRect(const std::map<BlockId, Rect>& a_rects, const BlockId& a_id, double a_l, double a_t, double a_r, double a_b,
        const std::string& a_case)
    {
        const auto it = a_rects.find(a_id);
        const bool ok = it != a_rects.end() && Near(it->second.left, a_l) && Near(it->second.top, a_t) && Near(it->second.right, a_r) &&
                        Near(it->second.bottom, a_b);
        Expect(ok, Text("%s: %s at %.0f,%.0f,%.0f,%.0f (got %s)", a_case.c_str(), a_id.c_str(), a_l, a_t, a_r, a_b,
                       it == a_rects.end() ? "none" : Show(it->second).c_str()));
    }

    const Settings::Values kValues{};
    const auto kFrames = Layout::DefaultFrames(kValues);
    // HoM add-on plan Task 5: the Heart of Magic block's ini rects (plugins/SL_MagicMenu_HoM/src/HomSettings.h, layout E2) - the
    // retired ids' heights when that block takes them over (legacyId). The core's own heights are Layout::DefaultHeights
    const Rect kTreeRect{ 978, 67, 1238, 297 }, kProgressRect{ 648, 306, 969, 408 }, kNodeCardRect{ 978, 306, 1238, 623 };
    const std::map<BlockId, double> kCoreHeights = Layout::DefaultHeights(kValues);
    std::map<BlockId, double> WithRetired(std::map<BlockId, double> a_heights)
    {
        a_heights[Layout::kTree] = kTreeRect.Height();
        a_heights[Layout::kProgress] = kProgressRect.Height();
        a_heights[Layout::kNodeCard] = kNodeCardRect.Height();
        return a_heights;
    }
    const auto kHeights = WithRetired(kCoreHeights);
    // the core's built-in blocks (Settings.cpp BuiltinBlocks) and the same with the three retired ids known (an add-on that took
    // them over, or an SL_MagicMenu before Task 5 that drew them itself)
    const std::set<BlockId> kBuiltins{ Layout::kPreview, Layout::kSpellCard };
    const std::set<BlockId> kKnown{ Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kSpellCard, Layout::kNodeCard };

    void DefaultLayout()
    {
        std::printf("default layout\n");
        const State state = Layout::Default(kValues);
        Expect(Near(state.border, 969), Text("border 969 (got %.1f)", state.border));
        const auto rects = Layout::Arrange(state, kHeights, {}, {}, kFrames);
        Expect(rects.size() == 5, Text("five rects (got %zu)", rects.size()));
        ExpectRect(rects, Layout::kPreview, 648, 67, 969, 297, "default");
        ExpectRect(rects, Layout::kProgress, 648, 306, 969, 408, "default");
        ExpectRect(rects, Layout::kSpellCard, 648, 417, 969, 623, "default");
        ExpectRect(rects, Layout::kTree, 978, 67, 1238, 297, "default");
        ExpectRect(rects, Layout::kNodeCard, 978, 306, 1238, 623, "default");
        // the ini's own rects, the other way round: Default + ini heights is exactly today's layout E2
        const std::map<BlockId, Rect> ini{ { Layout::kPreview, kValues.preview }, { Layout::kTree, kTreeRect },
            { Layout::kProgress, kProgressRect }, { Layout::kSpellCard, kValues.itemCard }, { Layout::kNodeCard, kNodeCardRect } };
        bool same = true;
        for (const auto& [id, rect] : ini) {
            const auto it = rects.find(id);
            same = same && it != rects.end() && Near(it->second.left, rect.left) && Near(it->second.top, rect.top) &&
                   Near(it->second.right, rect.right) && Near(it->second.bottom, rect.bottom);
        }
        Expect(same, "default arrange with the retired ids' heights = the E2 rects (core preview / item card, the HoM block's three)");
        const auto content = Layout::ContentHeight(state, rects, kFrames);
        Expect(Near(content[0], 556) && Near(content[1], 556), Text("content 556 / 556, no scroll (got %.1f / %.1f)", content[0], content[1]));
        Expect(Near(Layout::ClampScroll(40, content[0], kFrames[0].Height()), 0), "default column fits: scroll 40 -> 0");
    }

    void Border()
    {
        std::printf("column border\n");
        State state = Layout::Default(kValues);
        state.border = 900;
        const auto rects = Layout::Arrange(state, kHeights, {}, {}, kFrames);
        ExpectRect(rects, Layout::kPreview, 648, 67, 900, 297, "border 900");
        ExpectRect(rects, Layout::kSpellCard, 648, 417, 900, 623, "border 900");
        ExpectRect(rects, Layout::kTree, 909, 67, 1238, 297, "border 900");
        ExpectRect(rects, Layout::kNodeCard, 909, 306, 1238, 623, "border 900");

        const double low = Layout::ClampBorder(500, kFrames[0], kFrames[1]);
        const double high = Layout::ClampBorder(1200, kFrames[0], kFrames[1]);
        Expect(Near(low, 648 + 240), Text("ClampBorder(500) -> 888 (got %.1f)", low));
        Expect(Near(high, 1238 - 9 - 240), Text("ClampBorder(1200) -> 989 (got %.1f)", high));
        Expect(Near(Layout::ClampBorder(969, kFrames[0], kFrames[1]), 969), "ClampBorder(969) unchanged");
        Expect(Near(Layout::ClampBorder(std::numeric_limits<double>::quiet_NaN(), kFrames[0], kFrames[1]), 888), "ClampBorder(NaN) -> 888");

        state.border = 2000;  // a saved border out of range: Arrange clamps it, no column narrower than 240
        const auto clamped = Layout::Arrange(state, kHeights, {}, {}, kFrames);
        ExpectRect(clamped, Layout::kPreview, 648, 67, 989, 297, "border 2000 arranged");
        ExpectRect(clamped, Layout::kTree, 998, 67, 1238, 297, "border 2000 arranged");

        // the widest gap Settings allows (110): two 240 columns still fit exactly
        auto wide = kFrames;
        wide[0].gap = wide[1].gap = 110;
        const double only = Layout::ClampBorder(969, wide[0], wide[1]);
        Expect(Near(only, 888), Text("gap 110: the border has one place, 888 (got %.1f)", only));
        auto tooWide = kFrames;
        tooWide[0].gap = tooWide[1].gap = 200;
        const double mid = Layout::ClampBorder(969, tooWide[0], tooWide[1]);
        Expect(Near(mid, (888 + 798) / 2.0), Text("gap 200 (impossible): midpoint 843 (got %.1f)", mid));
    }

    void HiddenAndFill()
    {
        std::printf("hidden and fill blocks\n");
        const State state = Layout::Default(kValues);
        const auto rects = Layout::Arrange(state, kHeights, { Layout::kTree, Layout::kProgress }, {}, kFrames);
        Expect(!rects.contains(Layout::kTree) && !rects.contains(Layout::kProgress), "hidden tree, progress: no rects");
        ExpectRect(rects, Layout::kNodeCard, 978, 67, 1238, 384, "hidden tree, progress");
        ExpectRect(rects, Layout::kSpellCard, 648, 306, 969, 512, "hidden tree, progress");

        // Powers / Shouts: tree, progress, node card hidden; the spell card fills the rest: 556 - 230 - 9 = 317
        const std::set<BlockId> fill{ Layout::kSpellCard };
        const auto powers = Layout::Arrange(state, kHeights, { Layout::kTree, Layout::kProgress, Layout::kNodeCard }, fill, kFrames);
        ExpectRect(powers, Layout::kSpellCard, 648, 306, 969, 623, "Powers (fill)");
        Expect(powers.contains(Layout::kSpellCard) && Near(powers.at(Layout::kSpellCard).Height(), 317), "Powers: spell card 317 tall");
        Expect(powers.size() == 2, Text("Powers: preview and spell card only (got %zu)", powers.size()));

        // build 16: Powers and Active Effects hide the same blocks (the widget's TabHidden returns this set for both)
        // HoM add-on plan Task 5: no built-in block is hidden per tab (NoTreeTabHidden is gone); the HoM block's tabs mask hides
        // its three blocks on Powers/Shouts and Active Effects - the same set, now the add-on's
        const std::set<BlockId> noTree{ Layout::kTree, Layout::kProgress, Layout::kNodeCard };
        const auto effects = Layout::Arrange(state, kHeights, noTree, fill, kFrames);
        ExpectRect(effects, Layout::kPreview, 648, 67, 969, 297, "Active Effects (build 16): the preview");
        ExpectRect(effects, Layout::kSpellCard, 648, 306, 969, 623, "Active Effects (build 16): the card under it (fill)");

        // a column whose blocks are all hidden but the card: the whole column, 556 (no tab does this since build 16)
        const auto cardOnly = Layout::Arrange(
            state, kHeights, { Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kNodeCard }, fill, kFrames);
        ExpectRect(cardOnly, Layout::kSpellCard, 648, 67, 969, 623, "card alone in its column (fill)");

        // default tab with fill on: the column is already full, the card keeps its own 206
        const auto normal = Layout::Arrange(state, kHeights, {}, fill, kFrames);
        ExpectRect(normal, Layout::kSpellCard, 648, 417, 969, 623, "fill, column full");

        // overfull column: a fill block never shrinks below its height, and the column then scrolls
        auto tall = kHeights;
        tall[Layout::kPreview] = 400;
        const auto over = Layout::Arrange(state, tall, {}, fill, kFrames);
        ExpectRect(over, Layout::kSpellCard, 648, 587, 969, 793, "fill, overfull column keeps 206");
        const auto content = Layout::ContentHeight(state, over, kFrames);
        Expect(Near(content[0], 726), Text("overfull content 726 (got %.1f)", content[0]));
        Expect(Near(Layout::ClampScroll(500, content[0], kFrames[0].Height()), 170), "overfull: scroll 500 -> 170");

        // an absent add-on (no height) between blocks takes no room and no gap; its place stays in the state
        State withAddon = state;
        withAddon.columns[0].insert(withAddon.columns[0].begin() + 1, "addon.x");
        const auto absent = Layout::Arrange(withAddon, kHeights, {}, {}, kFrames);
        Expect(!absent.contains("addon.x"), "absent add-on: no rect");
        ExpectRect(absent, Layout::kProgress, 648, 306, 969, 408, "absent add-on between");
        // a present add-on (has a height) stacks like any block
        auto withHeight = kHeights;
        withHeight["addon.x"] = 50;
        const auto present = Layout::Arrange(withAddon, withHeight, {}, {}, kFrames);
        ExpectRect(present, "addon.x", 648, 306, 969, 356, "present add-on");
        ExpectRect(present, Layout::kSpellCard, 648, 476, 969, 682, "present add-on pushes down");
    }

    /// Build 16 (build 15 game run: "our card shown at 648,306..969,157"): the card's box in its column for a saved layout
    /// {preview, spellcard} / {tree, progress, nodecard} on the tabs without a tree, and CardBoxProblem on bad boxes
    void CardBoxInColumn()
    {
        std::printf("card box in its column (build 16)\n");
        State custom = Layout::Default(kValues);
        custom.columns = { std::vector<BlockId>{ Layout::kPreview, Layout::kSpellCard },
            std::vector<BlockId>{ Layout::kTree, Layout::kProgress, Layout::kNodeCard } };
        const std::set<BlockId> fill{ Layout::kSpellCard };
        const Rect column = Layout::ColumnArea(custom, 0, kFrames);
        for (const bool own : { true, false }) {
            const auto rects = Layout::Arrange(custom, kHeights, { Layout::kTree, Layout::kProgress, Layout::kNodeCard }, own ? fill : std::set<BlockId>{}, kFrames);
            const auto content = Layout::ContentHeight(custom, rects, kFrames);
            const char* who = own ? "our card (fill)" : "SkyUI's slot";
            ExpectRect(rects, Layout::kSpellCard, 648, 306, 969, own ? 623 : 512, Text("custom layout, Powers hidden set, %s", who));
            const auto it = rects.find(Layout::kSpellCard);
            const bool real = it != rects.end() && it->second.top < it->second.bottom && it->second.left < it->second.right;
            Expect(real, Text("custom layout, %s: top < bottom, left < right", who));
            const auto problem = it != rects.end() ? Layout::CardBoxProblem(it->second, column, content[0]) : std::string("no rect");
            Expect(problem.empty(), Text("custom layout, %s: inside column 0 %s (%s)", who, Show(column).c_str(), problem.empty() ? "ok" : problem.c_str()));
        }
        // the same layout on a spell tab (nothing hidden) and with the card drawn cut to its content (67 + 90 on its own tab)
        const auto spell = Layout::Arrange(custom, kHeights, {}, fill, kFrames);
        Expect(Layout::CardBoxProblem(spell.at(Layout::kSpellCard), column, Layout::ContentHeight(custom, spell, kFrames)[0]).empty(),
            "custom layout, spell tab: card inside its column");
        Expect(Layout::CardBoxProblem({ 648, 306, 969, 396 }, column, 556).empty(), "card cut to its content (306..396): ok");

        // CardBoxProblem refuses the bad boxes
        Expect(!Layout::CardBoxProblem({ 648, 306, 969, 157 }, column, 556).empty(), "build 15's 648,306..969,157 (inverted): FAIL");
        Expect(!Layout::CardBoxProblem({ 648, 306, 969, 306 }, column, 556).empty(), "empty box (top = bottom): FAIL");
        Expect(!Layout::CardBoxProblem({ 969, 306, 648, 623 }, column, 556).empty(), "right left of left: FAIL");
        Expect(!Layout::CardBoxProblem({ 978, 306, 1238, 623 }, column, 556).empty(), "in the other column's x span: FAIL");
        Expect(!Layout::CardBoxProblem({ 648, 40, 969, 300 }, column, 556).empty(), "above the column's top: FAIL");
        Expect(!Layout::CardBoxProblem({ 648, 417, 969, 700 }, column, 556).empty(), "below the column's end (no scroll): FAIL");
        Expect(Layout::CardBoxProblem({ 648, 587, 969, 793 }, column, 726).empty(), "below the viewport in a scrolling column (content 726): ok");
        Expect(!Layout::CardBoxProblem({ 648, 306, 969, std::numeric_limits<double>::quiet_NaN() }, column, 556).empty(), "NaN edge: FAIL");
    }

    void Scroll()
    {
        std::printf("scroll clamp\n");
        Expect(Near(Layout::ClampScroll(-50, 400, 300), 0), "ClampScroll(-50, 400, 300) -> 0");
        Expect(Near(Layout::ClampScroll(500, 400, 300), 100), "ClampScroll(500, 400, 300) -> 100");
        Expect(Near(Layout::ClampScroll(60, 400, 300), 60), "ClampScroll(60, 400, 300) -> 60");
        Expect(Near(Layout::ClampScroll(50, 300, 300), 0), "content = visible -> 0");
        Expect(Near(Layout::ClampScroll(50, 200, 300), 0), "content < visible -> 0");
        Expect(Near(Layout::ClampScroll(std::numeric_limits<double>::quiet_NaN(), 400, 300), 0), "NaN offset -> 0");
    }

    /// The column viewports (blocks plan Task 3: the masks and the wheel's hit areas) and the preview box cut to its column
    void ColumnsAndVisibleBox()
    {
        std::printf("column areas and the visible box\n");
        const State state = Layout::Default(kValues);
        const Rect left = Layout::ColumnArea(state, 0, kFrames), right = Layout::ColumnArea(state, 1, kFrames);
        Expect(Near(left.left, 648) && Near(left.top, 67) && Near(left.right, 969) && Near(left.bottom, 623),
            "column 0 area 648,67,969,623 (got " + Show(left) + ")");
        Expect(Near(right.left, 978) && Near(right.top, 67) && Near(right.right, 1238) && Near(right.bottom, 623),
            "column 1 area 978,67,1238,623 (got " + Show(right) + ")");
        const auto rects = Layout::Arrange(state, kHeights, {}, {}, kFrames);
        bool inside = true;
        for (int column = 0; column < Layout::kColumns; ++column) {
            const Rect area = Layout::ColumnArea(state, column, kFrames);
            for (const auto& id : state.columns[column]) {
                const auto& r = rects.at(id);
                inside = inside && Near(r.left, area.left) && Near(r.right, area.right) && r.top >= area.top && r.bottom <= area.bottom + kTolerance;
            }
        }
        Expect(inside, "default layout: every block spans its column's area across and lies inside it down");
        State wide = state;
        wide.border = 100;  // clamped to 648 + 240 as Arrange does
        Expect(Near(Layout::ColumnArea(wide, 0, kFrames).right, 888) && Near(Layout::ColumnArea(wide, 1, kFrames).left, 897),
            "border 100 -> column 0 ends at 888, column 1 starts at 897");
        const Rect none = Layout::ColumnArea(state, 2, kFrames);
        Expect(!(none.Width() > 0) && !(none.Height() > 0), "column 2 -> empty area");

        const Rect viewport{ 648, 67, 969, 623 };
        const auto whole = Layout::VisibleBox({ 656, 97, 961, 289 }, viewport);
        Expect(whole && Near(whole->left, 656) && Near(whole->top, 97) && Near(whole->right, 961) && Near(whole->bottom, 289),
            "box inside the viewport -> itself");
        const auto cut = Layout::VisibleBox({ 656, 37, 961, 229 }, viewport);  // scrolled up by 60: its top 30 units are out
        Expect(cut && Near(cut->top, 67) && Near(cut->bottom, 229) && Near(cut->left, 656) && Near(cut->right, 961),
            "box scrolled 30 above the viewport -> 656,67,961,229 (got " + (cut ? Show(*cut) : std::string("none")) + ")");
        const auto bottom = Layout::VisibleBox({ 600, 600, 700, 700 }, viewport);
        Expect(bottom && Near(bottom->left, 648) && Near(bottom->top, 600) && Near(bottom->right, 700) && Near(bottom->bottom, 623),
            "box over the bottom-left corner -> 648,600,700,623");
        Expect(!Layout::VisibleBox({ 656, -200, 961, -8 }, viewport), "box fully above the viewport -> nullopt");
        Expect(!Layout::VisibleBox({ 656, 623, 961, 815 }, viewport), "box touching the viewport's bottom edge only -> nullopt");
        Expect(!Layout::VisibleBox({ 969, 97, 1100, 289 }, viewport), "box touching the viewport's right edge only -> nullopt");
        Expect(!Layout::VisibleBox({ 656, 97, 656, 289 }, viewport), "zero-width box -> nullopt");
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        Expect(!Layout::VisibleBox({ kNaN, 97, 961, 289 }, viewport) && !Layout::VisibleBox({ 656, 97, 961, 289 }, { 648, kNaN, 969, 623 }),
            "a NaN edge in the box or the viewport -> nullopt");
    }

    void SnapAndMove()
    {
        std::printf("snap and move\n");
        const State state = Layout::Default(kValues);
        const auto rects = Layout::Arrange(state, kHeights, {}, {}, kFrames);

        // preview dropped at x 1100 between the tree (centre 182) and the node card (centre 464.5)
        const Slot right = Layout::SnapSlot(state, Layout::kPreview, 1100, 300, rects, kFrames);
        Expect(right == Slot{ 1, Layout::kNodeCard }, Text("preview at 1100,300 -> column 1 before nodecard (got %d, %s)", right.column,
                                                             right.before ? right.before->c_str() : "end"));
        const State moved = Layout::Move(state, Layout::kPreview, right);
        Expect(moved.columns[0] == std::vector<BlockId>{ Layout::kProgress, Layout::kSpellCard } &&
                   moved.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kPreview, Layout::kNodeCard },
            "moved: " + Show(moved.columns[0]) + " / " + Show(moved.columns[1]));

        // onto its own place: same layout
        const Slot own = Layout::SnapSlot(state, Layout::kPreview, rects.at(Layout::kPreview).CenterX(), rects.at(Layout::kPreview).CenterY(),
            rects, kFrames);
        Expect(Layout::Move(state, Layout::kPreview, own) == state, "preview onto its own place: unchanged");
        const Slot ownLast = Layout::SnapSlot(state, Layout::kSpellCard, 800, 600, rects, kFrames);
        Expect(ownLast == Slot{ 0, std::nullopt } && Layout::Move(state, Layout::kSpellCard, ownLast) == state,
            "spellcard onto its own place (end of column): unchanged");

        // the column split is at border + gap / 2 = 973.5
        Expect(Layout::SnapSlot(state, Layout::kTree, 973, 100, rects, kFrames).column == 0, "x 973 -> column 0");
        Expect(Layout::SnapSlot(state, Layout::kTree, 974, 100, rects, kFrames).column == 1, "x 974 -> column 1");
        // below every block: the end of the column
        Expect(Layout::SnapSlot(state, Layout::kPreview, 1100, 700, rects, kFrames) == Slot{ 1, std::nullopt }, "below the last block -> end");

        // same column: spellcard to the top
        const Slot top = Layout::SnapSlot(state, Layout::kSpellCard, 800, 70, rects, kFrames);
        Expect(top == Slot{ 0, Layout::kPreview }, "spellcard at the top -> before preview");
        const State up = Layout::Move(state, Layout::kSpellCard, top);
        Expect(up.columns[0] == std::vector<BlockId>{ Layout::kSpellCard, Layout::kPreview, Layout::kProgress }, "same column: " + Show(up.columns[0]));
        // same column: preview down past progress
        const State down = Layout::Move(state, Layout::kPreview, Layout::SnapSlot(state, Layout::kPreview, 800, 400, rects, kFrames));
        Expect(down.columns[0] == std::vector<BlockId>{ Layout::kProgress, Layout::kPreview, Layout::kSpellCard }, "same column down: " + Show(down.columns[0]));
    }

    void SnapAroundHiddenAndAbsent()
    {
        std::printf("snap and move with hidden / absent ids (plan D3)\n");
        State state = Layout::Default(kValues);
        state.columns[0] = { Layout::kPreview, Layout::kProgress, "addon.gone", Layout::kSpellCard };
        const std::set<BlockId> hidden{ Layout::kProgress };
        const auto rects = Layout::Arrange(state, kHeights, hidden, {}, kFrames);
        ExpectRect(rects, Layout::kSpellCard, 648, 306, 969, 512, "hidden progress, absent add-on");

        // node card dropped just above the spell card's centre: before spellcard, i.e. after the hidden and absent ids
        const Slot slot = Layout::SnapSlot(state, Layout::kNodeCard, 800, 350, rects, kFrames);
        Expect(slot == Slot{ 0, Layout::kSpellCard }, "nodecard at 800,350 -> before spellcard (hidden progress skipped)");
        const State moved = Layout::Move(state, Layout::kNodeCard, slot);
        Expect(moved.columns[0] == std::vector<BlockId>{ Layout::kPreview, Layout::kProgress, "addon.gone", Layout::kNodeCard, Layout::kSpellCard },
            "inserted right before spellcard: " + Show(moved.columns[0]));
        Expect(moved.columns[1] == std::vector<BlockId>{ Layout::kTree }, "taken out of column 1: " + Show(moved.columns[1]));

        // hidden / absent ids at the column end: a drop at the end goes after them
        State tail = Layout::Default(kValues);
        tail.columns[0] = { Layout::kPreview, Layout::kSpellCard, Layout::kProgress, "addon.gone" };
        const auto tailRects = Layout::Arrange(tail, kHeights, hidden, {}, kFrames);
        const Slot end = Layout::SnapSlot(tail, Layout::kTree, 800, 600, tailRects, kFrames);
        Expect(end == Slot{ 0, std::nullopt }, "tree below the last shown block -> end");
        const State atEnd = Layout::Move(tail, Layout::kTree, end);
        Expect(atEnd.columns[0] == std::vector<BlockId>{ Layout::kPreview, Layout::kSpellCard, Layout::kProgress, "addon.gone", Layout::kTree },
            "appended after the hidden and absent ids: " + Show(atEnd.columns[0]));

        // refusals: before = itself, stale before, unknown block, bad column
        const State base = Layout::Default(kValues);
        Expect(Layout::Move(base, Layout::kPreview, Slot{ 1, Layout::kPreview }) == base, "before = the block itself: unchanged");
        const State stale = Layout::Move(base, Layout::kPreview, Slot{ 1, Layout::kProgress });  // progress is in column 0
        Expect(stale.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kNodeCard, Layout::kPreview }, "before not in that column: appended " +
                                                                                                                  Show(stale.columns[1]));
        Expect(Layout::Move(base, "addon.none", Slot{ 0, std::nullopt }) == base, "block not in the layout: unchanged");
        Expect(Layout::Move(base, Layout::kPreview, Slot{ 2, std::nullopt }) == base, "column 2: unchanged");
        Expect(Layout::Move(base, Layout::kPreview, Slot{ -1, std::nullopt }) == base, "column -1: unchanged");
    }

    /// FromJson refuses the text with a reason and does not call it a newer version
    void ExpectRejected(std::string_view a_text, const std::string& a_case)
    {
        std::string why;
        bool newer = true;
        const bool rejected = !Layout::FromJson(a_text, &why, &newer);
        Expect(rejected && !why.empty() && !newer, a_case + " -> nullopt, reason: " + why);
    }

    void Json()
    {
        std::printf("layout JSON\n");
        const State fallback = Layout::Default(kValues);

        State custom = fallback;
        custom.columns[0] = { Layout::kTree, "addon.x" };
        custom.columns[1] = { Layout::kPreview, Layout::kNodeCard, Layout::kProgress, Layout::kSpellCard };
        custom.border = 912.5;
        const std::string text = Layout::ToJson(custom);
        std::string why;
        const auto back = Layout::FromJson(text, &why);
        Expect(back && *back == custom, "round trip");
        Expect(text.find("\"version\": 1") != std::string::npos, "ToJson writes \"version\": 1");

        // a hand-edited file: no version (= 1), blocks missing, border out of range
        const auto partial = Layout::FromJson(R"({"columns":[["tree"],["preview"]],"border":2000})", &why);
        Expect(partial.has_value(), "no version, partial columns: accepted");
        if (partial) {
            const State repaired = Layout::Repair(*partial, kKnown, fallback);
            Expect(repaired.columns[0] == std::vector<BlockId>{ Layout::kTree, Layout::kProgress, Layout::kSpellCard } &&
                       repaired.columns[1] == std::vector<BlockId>{ Layout::kPreview, Layout::kNodeCard },
                "missing blocks appended to their default columns: " + Show(repaired.columns[0]) + " / " + Show(repaired.columns[1]));
            const double border = Layout::ClampBorder(repaired.border, kFrames[0], kFrames[1]);
            Expect(Near(border, 989), Text("border 2000 clamped to 989 (got %.1f)", border));
        }

        ExpectRejected("not json", "\"not json\"");

        bool newer = false;
        why.clear();
        const auto future = Layout::FromJson(R"({"version":2,"columns":[[],[]],"border":969})", &why, &newer);
        const bool futureOk = !future && newer && why.find("unsupported version") != std::string::npos;
        Expect(futureOk, "version 2 -> nullopt, newer, reason: " + why);
        ExpectRejected(R"({"version":0,"columns":[[],[]]})", "version 0");
        ExpectRejected(R"({"version":"1","columns":[[],[]]})", "version \"1\"");
        ExpectRejected(R"({"columns":[["tree"]]})", "one column");
        ExpectRejected(R"({"columns":[["tree", 5],[]]})", "non-string id");
        ExpectRejected(R"({"columns":[[],[]],"border":"wide"})", "string border");
        ExpectRejected("[1,2]", "not an object");

        // no border: Repair puts the fallback's in
        const auto noBorder = Layout::FromJson(R"({"version":1,"columns":[["preview","progress","spellcard"],["tree","nodecard"]]})", &why);
        Expect(noBorder && std::isnan(noBorder->border) && Layout::Repair(*noBorder, kKnown, fallback) == fallback, "no border -> the fallback's");

        // unknown id kept in place, duplicate dropped (first place wins), empty id dropped
        const auto odd = Layout::FromJson(
            R"({"version":1,"columns":[["preview","addon.x","progress","spellcard",""],["tree","preview","nodecard"]],"border":969})", &why);
        Expect(odd.has_value(), "odd file parses");
        if (odd) {
            const State repaired = Layout::Repair(*odd, kKnown, fallback);
            Expect(repaired.columns[0] == std::vector<BlockId>{ Layout::kPreview, "addon.x", Layout::kProgress, Layout::kSpellCard },
                "unknown addon.x kept in place, empty id dropped: " + Show(repaired.columns[0]));
            Expect(repaired.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kNodeCard }, "duplicate preview dropped: " + Show(repaired.columns[1]));
        }

        // a known block the fallback does not place (a new add-on): column 0's end
        std::set<BlockId> known = kKnown;
        known.insert("addon.new");
        const State added = Layout::Repair(fallback, known, fallback);
        Expect(added.columns[0].back() == "addon.new" && added.columns[1] == fallback.columns[1], "known block not in the fallback -> end of column 0");
        Expect(Layout::Repair(fallback, kKnown, fallback) == fallback, "Repair of the default: unchanged");
    }

    /// Each id of a_ids exactly once over both columns
    bool EachOnce(const State& a_state, const std::set<BlockId>& a_ids)
    {
        for (const auto& id : a_ids) {
            int count = 0;
            for (const auto& column : a_state.columns) {
                count += static_cast<int>(std::ranges::count(column, id));
            }
            if (count != 1) {
                return false;
            }
        }
        return true;
    }

    void MoveKeepsEveryBlock()
    {
        std::printf("move + repair keep every block exactly once (Task 4)\n");
        const State fallback = Layout::Default(kValues);
        State withAbsent = fallback;
        withAbsent.columns[0] = { Layout::kPreview, "addon.gone", Layout::kProgress, Layout::kSpellCard };
        std::set<BlockId> all = kKnown;
        all.insert("addon.gone");
        const std::vector<std::optional<BlockId>> anchors{ std::nullopt, Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kSpellCard,
            Layout::kNodeCard, BlockId{ "addon.gone" }, BlockId{ "addon.none" } };
        int cases = 0, bad = 0;
        for (const bool absent : { false, true }) {
            const State& start = absent ? withAbsent : fallback;
            for (const auto& block : all) {
                for (int column = -1; column <= Layout::kColumns; ++column) {
                    for (const auto& before : anchors) {
                        const State moved = Layout::Move(start, block, Slot{ column, before });
                        ++cases;
                        const bool once = EachOnce(moved, absent ? all : kKnown);
                        const bool stable = Layout::Repair(moved, kKnown, fallback) == moved;
                        if (!once || !stable) {
                            ++bad;
                        }
                    }
                }
            }
        }
        Expect(bad == 0, Text("every single move (%d cases: each block, columns -1..2, each anchor incl. absent and unknown ids): each id once, "
                              "Repair leaves it as it is (%d bad)",
                             cases, bad));

        // a long chain of drags (a fixed LCG, seed 20261026): the same holds after every step
        std::uint32_t seed = 20261026u;
        const auto next = [&](std::uint32_t a_range) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<std::uint32_t>((seed >> 8) % a_range);
        };
        const std::vector<BlockId> ids(all.begin(), all.end());
        State chain = withAbsent;
        int chainBad = 0;
        constexpr int kSteps = 500;
        for (int step = 0; step < kSteps; ++step) {
            const auto& block = ids[next(static_cast<std::uint32_t>(ids.size()))];
            const auto& before = anchors[next(static_cast<std::uint32_t>(anchors.size()))];
            chain = Layout::Move(chain, block, Slot{ static_cast<int>(next(Layout::kColumns)), before });
            if (!EachOnce(chain, all) || !(Layout::Repair(chain, kKnown, fallback) == chain)) {
                ++chainBad;
            }
        }
        Expect(chainBad == 0, Text("%d chained moves (LCG seed 20261026): each id once after every step (%d bad)", kSteps, chainBad));

        // a file that lost blocks and repeats others: Repair puts each back to exactly once
        State broken = fallback;
        broken.columns[0] = { Layout::kPreview, Layout::kPreview, "addon.gone" };
        broken.columns[1] = { Layout::kTree, Layout::kSpellCard, Layout::kTree };
        const State repaired = Layout::Repair(broken, kKnown, fallback);
        Expect(EachOnce(repaired, all), "repair of a file with lost and repeated blocks: each id once " + Show(repaired.columns[0]) + " / " +
                                           Show(repaired.columns[1]));
    }

    void CancelRestoresEntry()
    {
        std::printf("cancel restores the entry State (Task 4, L4's offline twin)\n");
        // the widget's mode keeps the State it was entered with and edits a copy (drops, the border, reset); cancel puts the
        // entry back. The entry here is not the default: an absent add-on, a moved border
        State entry = Layout::Default(kValues);
        entry.columns[0] = { Layout::kTree, "addon.gone", Layout::kSpellCard };
        entry.columns[1] = { Layout::kPreview, Layout::kProgress, Layout::kNodeCard };
        entry.border = 900.25;
        const std::string entryText = Layout::ToJson(entry);
        State working = entry;
        working = Layout::Move(working, Layout::kPreview, Slot{ 0, Layout::kTree });
        working = Layout::Move(working, Layout::kNodeCard, Slot{ 0, std::nullopt });
        working.border = Layout::ClampBorder(1100, kFrames[0], kFrames[1]);
        working = Layout::Default(kValues);  // reset
        working = Layout::Move(working, Layout::kTree, Slot{ 0, Layout::kPreview });
        Expect(!(working == entry), "the edits changed the working State");
        working = entry;  // cancel
        Expect(working == entry && Layout::ToJson(working) == entryText, "after cancel: equal to the entry State, the same JSON byte for byte");
        const auto reread = Layout::FromJson(Layout::ToJson(working), nullptr);
        Expect(reread && *reread == entry, "the entry State's JSON reads back as itself (L3's offline twin)");
    }

    std::string Show(const std::optional<BlockId>& a_id) { return a_id ? *a_id : std::string("none"); }

    std::string Show(const std::optional<Slot>& a_slot)
    {
        return a_slot ? Text("{%d, %s}", a_slot->column, a_slot->before ? a_slot->before->c_str() : "end") : std::string("none");
    }

    void SlotLineAndScrollToShow()
    {
        std::printf("insertion line and keep-in-view scroll (Task 5)\n");
        const State state = Layout::Default(kValues);
        const auto rects = Layout::Arrange(state, kHeights, {}, {}, kFrames);
        Expect(Near(Layout::SlotLineY(state, Slot{ 0, Layout::kProgress }, Layout::kPreview, rects, kFrames), 301.5),
            "line before progress: its top 306 - gap / 2 = 301.5");
        Expect(Near(Layout::SlotLineY(state, Slot{ 0, std::nullopt }, Layout::kPreview, rects, kFrames), 627.5),
            "line at column 0's end: the spell card's bottom 623 + 4.5");
        Expect(Near(Layout::SlotLineY(state, Slot{ 1, std::nullopt }, Layout::kNodeCard, rects, kFrames), 301.5),
            "line at column 1's end with the node card picked: the tree's bottom 297 + 4.5");
        State empty = state;
        empty.columns[1].clear();
        Expect(Near(Layout::SlotLineY(empty, Slot{ 1, std::nullopt }, Layout::kPreview, rects, kFrames), 67.0), "empty column: its top 67");
        Expect(Near(Layout::SlotLineY(state, Slot{ 5, std::nullopt }, Layout::kPreview, rects, kFrames), 67.0), "column out of range: frame top");

        // a column 556 visible from 67, 900 of content
        constexpr double kTop = 67.0, kVisible = 556.0, kContent = 900.0;
        Expect(Near(Layout::ScrollToShow(0, 100, 300, kTop, kVisible, kContent), 0), "in view: unchanged");
        Expect(Near(Layout::ScrollToShow(0, 700, 900, kTop, kVisible, kContent), 900 - 67 - 556), "below: its bottom at the viewport's bottom");
        Expect(Near(Layout::ScrollToShow(300, 100, 200, kTop, kVisible, kContent), 33), "above: its top at the viewport's top");
        Expect(Near(Layout::ScrollToShow(0, 200, 800, kTop, kVisible, kContent), 133), "taller than the viewport: its top shown");
        Expect(Near(Layout::ScrollToShow(0, 900, 1200, kTop, kVisible, kContent), 344), "past the content: clamped to content - visible");
        Expect(Near(Layout::ScrollToShow(0, 700, 900, kTop, kVisible, 500), 0), "content fits: always 0");
        Expect(Near(Layout::ScrollToShow(std::numeric_limits<double>::quiet_NaN(), 100, 200, kTop, kVisible, kContent), 0),
            "a non-finite scroll counts as 0 (the span is in view then)");
    }

    /// One key on this layout with all blocks arranged (no tab hides any)
    Layout::NavResult Key(const State& a_state, const Layout::Focus& a_focus, Layout::Nav a_nav, const std::set<BlockId>& a_visible, bool a_on = true)
    {
        const auto rects = Layout::Arrange(a_state, kHeights, {}, {}, kFrames);
        return Layout::Navigate(a_state, a_focus, a_nav, a_on, a_visible, rects, kFrames);
    }

    void PadFocus()
    {
        std::printf("gamepad / keyboard focus model (Task 5)\n");
        using Layout::Focus;
        using Layout::Nav;
        using Layout::NavEffect;
        const State state = Layout::Default(kValues);
        const std::set<BlockId>& all = kKnown;
        const Focus none{};
        const auto at = [](const BlockId& a_id) { return Focus{ a_id, std::nullopt }; };

        // failure: not in the mode
        for (const Nav nav : { Nav::kUp, Nav::kAccept, Nav::kCancel, Nav::kBorderRight }) {
            const auto r = Key(state, at(Layout::kPreview), nav, all, false);
            Expect(r.effect == NavEffect::kIgnored && r.focus == at(Layout::kPreview) && r.state == state,
                Text("mode off, key %d: ignored, nothing changes", static_cast<int>(nav)));
        }

        // normal: the first key gives the focus, then up / down / left / right
        auto r = Key(state, none, Nav::kDown, all);
        Expect(r.effect == NavEffect::kFocused && r.focus.focus == Layout::kPreview, "no focus, Down: the first block (preview), got " + Show(r.focus.focus));
        r = Key(state, none, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kFocused && !r.focus.Picked(), "no focus, A: focus only, no pick");
        r = Key(state, at(Layout::kPreview), Nav::kDown, all);
        Expect(r.focus.focus == Layout::kProgress, "preview Down -> progress, got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kProgress), Nav::kDown, all);
        Expect(r.focus.focus == Layout::kSpellCard, "progress Down -> spellcard, got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kSpellCard), Nav::kDown, all);
        Expect(r.effect == NavEffect::kNone && r.focus == at(Layout::kSpellCard), "boundary: spellcard Down at the column's end stays (no wrap)");
        r = Key(state, at(Layout::kPreview), Nav::kUp, all);
        Expect(r.effect == NavEffect::kNone && r.focus == at(Layout::kPreview), "boundary: preview Up at the column's top stays");
        r = Key(state, at(Layout::kPreview), Nav::kRight, all);
        Expect(r.focus.focus == Layout::kTree, "preview Right -> tree (same centre 182), got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kProgress), Nav::kRight, all);
        Expect(r.focus.focus == Layout::kNodeCard, "progress (357) Right -> nodecard (464.5, nearer than tree 182), got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kNodeCard), Nav::kLeft, all);
        Expect(r.focus.focus == Layout::kSpellCard, "nodecard (464.5) Left -> spellcard (520), got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kTree), Nav::kRight, all);
        Expect(r.effect == NavEffect::kNone, "boundary: Right in the right column stays");
        r = Key(state, at(Layout::kPreview), Nav::kLeft, all);
        Expect(r.effect == NavEffect::kNone, "boundary: Left in the left column stays");

        // hidden blocks (not visible: progress with no school, our card not shown yet)
        std::set<BlockId> noProgress = all;
        noProgress.erase(Layout::kProgress);
        r = Key(state, at(Layout::kPreview), Nav::kDown, noProgress);
        Expect(r.focus.focus == Layout::kSpellCard, "hidden progress skipped: preview Down -> spellcard, got " + Show(r.focus.focus));
        Expect(Layout::Normalize(state, at(Layout::kProgress), noProgress) == at(Layout::kSpellCard), "a focused block gone: the next visible below it");
        const std::set<BlockId> topOnly{ Layout::kPreview, Layout::kTree, Layout::kNodeCard };
        Expect(Layout::Normalize(state, at(Layout::kSpellCard), topOnly) == at(Layout::kPreview), "nothing below: the nearest above it");
        const std::set<BlockId> rightOnly{ Layout::kTree, Layout::kNodeCard };
        Expect(Layout::Normalize(state, at(Layout::kProgress), rightOnly) == none, "nothing visible in its column: no focus");
        r = Key(state, none, Nav::kDown, rightOnly);
        Expect(r.focus.focus == Layout::kTree, "column 0 all hidden: the first focus is column 1's top, got " + Show(r.focus.focus));
        r = Key(state, at(Layout::kTree), Nav::kLeft, rightOnly);
        Expect(r.effect == NavEffect::kNone, "Left into a column with nothing visible: stays");
        r = Key(state, none, Nav::kDown, {});
        Expect(r.effect == NavEffect::kNone && r.focus == none, "nothing visible: no focus");

        // failure: unknown block, an absent add-on id in the layout
        Expect(Layout::Normalize(state, at("addon.none"), all) == none, "unknown block focused: cleared");
        r = Key(state, at("addon.none"), Nav::kDown, all);
        Expect(r.effect == NavEffect::kFocused && r.focus.focus == Layout::kPreview, "unknown block, Down: the first block, got " + Show(r.focus.focus));
        State absent = state;
        absent.columns[0] = { Layout::kPreview, "addon.gone", Layout::kProgress, Layout::kSpellCard };
        Expect(Layout::Normalize(absent, at("addon.gone"), all) == at(Layout::kProgress), "an absent add-on's id focused: the next visible block");
        r = Key(absent, at(Layout::kPreview), Nav::kDown, all);
        Expect(r.focus.focus == Layout::kProgress, "absent id skipped: preview Down -> progress");

        // pick, move in the column, drop
        r = Key(state, at(Layout::kPreview), Nav::kAccept, all);
        Expect(r.effect == NavEffect::kPicked && r.focus.Picked() && r.focus.target == Slot{ 0, Layout::kProgress },
            "A picks preview: target its own place {0, progress}, got " + Show(r.focus.target));
        const Focus picked = r.focus;
        r = Key(state, picked, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kDroppedInPlace && r.state == state && r.focus == at(Layout::kPreview), "A again: dropped at its place, same layout");
        r = Key(absent, Focus{ Layout::kPreview, Layout::SlotOf(absent, Layout::kPreview, all) }, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kDroppedInPlace && r.state == absent, "own place past an absent id: no Move, the saved array as it was");
        r = Key(state, picked, Nav::kUp, all);
        Expect(r.effect == NavEffect::kNone && r.focus == picked, "boundary: picked at the top, Up: no move");
        r = Key(state, picked, Nav::kDown, all);
        Expect(r.focus.target == Slot{ 0, Layout::kSpellCard }, "picked preview Down -> before spellcard, got " + Show(r.focus.target));
        r = Key(state, r.focus, Nav::kDown, all);
        Expect(r.focus.target == Slot{ 0, std::nullopt }, "Down again -> the column's end, got " + Show(r.focus.target));
        const Focus atEnd = r.focus;
        r = Key(state, atEnd, Nav::kDown, all);
        Expect(r.effect == NavEffect::kNone, "boundary: target at the end, Down: no move");
        r = Key(state, atEnd, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kDropped && r.state.columns[0] == std::vector<BlockId>{ Layout::kProgress, Layout::kSpellCard, Layout::kPreview } &&
                   r.focus == at(Layout::kPreview),
            "A drops it at the end: " + Show(r.state.columns[0]) + ", the focus stays on it");

        // pick, across the columns
        r = Key(state, picked, Nav::kRight, all);
        Expect(r.focus.target == Slot{ 1, Layout::kNodeCard }, "picked preview Right: line 301.5 -> before nodecard (centre 464.5), got " + Show(r.focus.target));
        const Focus acrossPick = r.focus;
        const auto across = Key(state, acrossPick, Nav::kAccept, all);
        Expect(across.effect == NavEffect::kDropped && across.state.columns[0] == std::vector<BlockId>{ Layout::kProgress, Layout::kSpellCard } &&
                   across.state.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kPreview, Layout::kNodeCard },
            "dropped in column 1: " + Show(across.state.columns[0]) + " / " + Show(across.state.columns[1]));
        r = Key(state, acrossPick, Nav::kRight, all);
        Expect(r.effect == NavEffect::kNone, "boundary: target in the right column, Right: no move");
        r = Key(state, acrossPick, Nav::kLeft, all);
        Expect(r.focus.target == Slot{ 0, Layout::kProgress }, "Right then Left: back before progress, got " + Show(r.focus.target));

        // cancel
        r = Key(state, picked, Nav::kCancel, all);
        Expect(r.effect == NavEffect::kPickCancelled && r.state == state && r.focus == at(Layout::kPreview), "B with a pick: the pick ends, nothing moved");
        r = Key(state, acrossPick, Nav::kCancel, all);
        Expect(r.effect == NavEffect::kPickCancelled && r.state == state, "B with a pick aimed at the other column: nothing moved");
        r = Key(state, at(Layout::kPreview), Nav::kCancel, all);
        Expect(r.effect == NavEffect::kLeave && r.state == state, "B with no pick: leave the mode (cancel)");
        r = Key(state, none, Nav::kCancel, all);
        Expect(r.effect == NavEffect::kLeave, "B with no focus: leave the mode");

        // empty column and single-block column
        State left = state;
        left.columns[0] = { Layout::kPreview, Layout::kProgress, Layout::kSpellCard, Layout::kTree, Layout::kNodeCard };
        left.columns[1].clear();
        r = Key(left, at(Layout::kProgress), Nav::kRight, all);
        Expect(r.effect == NavEffect::kNone, "empty column: Right has nothing to focus");
        r = Key(left, Key(left, at(Layout::kProgress), Nav::kAccept, all).focus, Nav::kRight, all);
        Expect(r.focus.target == Slot{ 1, std::nullopt }, "picked, Right into the empty column: its end, got " + Show(r.focus.target));
        r = Key(left, r.focus, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kDropped && r.state.columns[1] == std::vector<BlockId>{ Layout::kProgress }, "dropped into the empty column");
        State single = state;
        single.columns[0] = { Layout::kPreview, Layout::kProgress, Layout::kSpellCard, Layout::kNodeCard };
        single.columns[1] = { Layout::kTree };
        r = Key(single, at(Layout::kTree), Nav::kDown, all);
        Expect(r.effect == NavEffect::kNone, "single-block column: Down stays");
        const Focus soloPick = Key(single, at(Layout::kTree), Nav::kAccept, all).focus;
        Expect(soloPick.target == Slot{ 1, std::nullopt }, "single block picked: target its own place (the end)");
        Expect(Key(single, soloPick, Nav::kUp, all).effect == NavEffect::kNone && Key(single, soloPick, Nav::kDown, all).effect == NavEffect::kNone,
            "single block picked: Up / Down do not move the target");
        r = Key(single, soloPick, Nav::kLeft, all);
        Expect(r.focus.target == Slot{ 0, Layout::kPreview }, "single block (tree, line at the empty column's top 67) Left: before preview, got " + Show(r.focus.target));
        r = Key(single, r.focus, Nav::kAccept, all);
        Expect(r.effect == NavEffect::kDropped && r.state.columns[1].empty() && EachOnce(r.state, kKnown), "the single block moved out: its column empty, every block once");

        // targets made valid
        Expect(Layout::Normalize(state, Focus{ Layout::kPreview, Slot{ 0, Layout::kProgress } }, noProgress) == Focus{ Layout::kPreview, Slot{ 0, std::nullopt } },
            "target before a hidden block: the column's end");
        Expect(Layout::Normalize(state, Focus{ Layout::kPreview, Slot{ 3, std::nullopt } }, all) == at(Layout::kPreview), "target column out of range: the pick ends");
        Expect(Layout::Normalize(state, Focus{ Layout::kProgress, Slot{ 0, std::nullopt } }, noProgress) == at(Layout::kSpellCard),
            "the picked block hidden: the pick ends, the focus moves");

        // the border
        r = Key(state, picked, Nav::kBorderRight, all);
        Expect(r.effect == NavEffect::kBorder && Near(r.state.border, 979) && r.focus == picked, "RB: border 969 -> 979, the pick kept");
        r = Key(state, none, Nav::kBorderLeft, all);
        Expect(r.effect == NavEffect::kBorder && Near(r.state.border, 959), "LB: border 969 -> 959");
        State wide = state;
        wide.border = 989;  // the highest: 1238 - 9 - 240
        Expect(Key(wide, none, Nav::kBorderRight, all).effect == NavEffect::kNone, "boundary: border at its highest, RB: no change");
        wide.border = 985;
        r = Key(wide, none, Nav::kBorderRight, all);
        Expect(Near(r.state.border, 989), Text("border 985, RB: clamped to 989 (got %.1f)", r.state.border));
        wide.border = 2000;
        Expect(Key(wide, none, Nav::kBorderRight, all).effect == NavEffect::kNone, "a saved border past the limit, RB: clamped, no change");
        wide.border = 888;  // the lowest: 648 + 240
        Expect(Key(wide, none, Nav::kBorderLeft, all).effect == NavEffect::kNone, "boundary: border at its lowest, LB: no change");

        // a long chain of keys (fixed LCG, seed 20261027) with the visible set changing: every block once after each key, the
        // focus always visible or none, a target always in range
        std::uint32_t seed = 20261027u;
        const auto next = [&](std::uint32_t a_range) {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<std::uint32_t>((seed >> 8) % a_range);
        };
        const std::vector<std::set<BlockId>> visibles{ all, noProgress, topOnly, rightOnly, {} };
        State chain = absent;
        std::set<BlockId> absentAll = all;
        absentAll.insert("addon.gone");
        Focus focus{};
        int bad = 0, drops = 0, borders = 0;
        constexpr int kSteps = 2000;
        constexpr std::uint32_t kKeys = 8, kChangeOneIn = 4;
        for (int step = 0; step < kSteps; ++step) {
            const auto& visible = visibles[next(kChangeOneIn) == 0 ? next(static_cast<std::uint32_t>(visibles.size())) : 0];
            const auto result = Key(chain, focus, static_cast<Nav>(next(kKeys)), visible);
            drops += result.effect == NavEffect::kDropped ? 1 : 0;
            borders += result.effect == NavEffect::kBorder ? 1 : 0;
            chain = result.state;
            focus = result.focus;
            const bool focusOk = !focus.focus || visible.contains(*focus.focus);
            const bool targetOk = !focus.target || (focus.focus && focus.target->column >= 0 && focus.target->column < Layout::kColumns);
            if (!EachOnce(chain, absentAll) || !focusOk || !targetOk) {
                ++bad;
            }
        }
        Expect(bad == 0 && drops > 0 && borders > 0, Text("%d random keys (%d drops, %d border moves): every block once, the focus visible, targets in range "
                                                          "(%d bad)",
                                                         kSteps, drops, borders, bad));
    }

    void FileSafety()
    {
        std::printf("layout file safety (plan D10, Task 4)\n");
        using Layout::FileState;
        using Layout::SavePlan;
        const auto plan = [](FileState a_state, bool a_kept, bool a_exists) { return Layout::PlanSave(a_state, a_kept, a_exists); };
        Expect(plan(FileState::kRead, false, true) == SavePlan{ false, false, true }, "normal file: write .tmp, replace keeping .bak");
        Expect(plan(FileState::kMissing, false, false) == SavePlan{ false, false, false }, "no file yet: write .tmp, move it in (no .bak)");
        Expect(plan(FileState::kRead, false, false) == SavePlan{ false, false, false }, "file gone since it was read: move only");
        Expect(plan(FileState::kMissing, false, true) == SavePlan{ false, false, true }, "a file appeared since load: replace keeping .bak");
        Expect(plan(FileState::kBroken, false, true) == SavePlan{ false, true, true }, "failed read: copy to .bad first, then replace");
        Expect(plan(FileState::kBroken, true, true) == SavePlan{ false, false, true }, "failed read, .bad kept already (a retry): no second copy");
        Expect(plan(FileState::kBroken, false, false) == SavePlan{ false, false, false }, "failed read, file gone: nothing to keep, move only");
        Expect(plan(FileState::kNewer, false, true).refuse && plan(FileState::kNewer, true, false).refuse, "newer build's file: never written");
        Expect(Layout::AfterSave(FileState::kBroken, false) == FileState::kBroken &&
                   plan(Layout::AfterSave(FileState::kBroken, false), false, true) == plan(FileState::kBroken, false, true),
            "write failure: the state is kept, the retry plans the same");
        Expect(Layout::AfterSave(FileState::kNewer, false) == FileState::kNewer, "refused: still the newer build's file");
        Expect(Layout::AfterSave(FileState::kBroken, true) == FileState::kRead && Layout::AfterSave(FileState::kMissing, true) == FileState::kRead,
            "written: a read layout from then on (no .bad copy at the next save)");

        // final review: the .tmp could not be put in place
        using Layout::FailedPut;
        Expect(Layout::AfterFailedPut(true, true, true) == FailedPut::kFileThere && Layout::AfterFailedPut(true, true, false) == FailedPut::kFileThere,
            "a failed put with the file still there (ReplaceFileW 1176 / the move failed before it): nothing to restore");
        Expect(Layout::AfterFailedPut(true, false, true) == FailedPut::kRestoreBak,
            "the replace moved the old file to .bak and the move failed (1177 + MoveFileExW): .bak moved back");
        Expect(Layout::AfterFailedPut(true, false, false) == FailedPut::kNoFile, "no file and no .bak: nothing to restore");
        Expect(Layout::AfterFailedPut(false, false, true) == FailedPut::kNoFile,
            "a first save (no replace) with an old .bak lying there: the .bak is not moved in");
        Expect(!Layout::KeepTmp(FailedPut::kRestoreBak, true), "the .bak moved back: the .tmp is removed");
        Expect(Layout::KeepTmp(FailedPut::kRestoreBak, false), "the .bak could not be moved back: the .tmp (the edited layout) is kept");
        Expect(!Layout::KeepTmp(FailedPut::kFileThere, false) && !Layout::KeepTmp(FailedPut::kNoFile, false), "otherwise the .tmp is removed");
        Expect(Layout::ReadBackup(FileState::kMissing, true), "load: no file but a .bak - the .bak is read");
        Expect(!Layout::ReadBackup(FileState::kMissing, false), "load: no file and no .bak - the default");
        Expect(!Layout::ReadBackup(FileState::kRead, true) && !Layout::ReadBackup(FileState::kBroken, true) && !Layout::ReadBackup(FileState::kNewer, true),
            "load: a file there (read, broken or newer) - the .bak is never read instead");
        // the next save after a load from .bak: the file is missing, so it is created (move only), the .bak left alone
        Expect(plan(FileState::kMissing, false, false) == SavePlan{ false, false, false }, "after a .bak load: the next save moves the .tmp in");
    }

    // final review: the layout mode's on/off as the input sees it - the newest request wins
    void ModeOrdering()
    {
        std::printf("layout mode requests in order (final review)\n");
        {
            // a quick K off/on: Save then Enter queued before the frame runs them
            Layout::ModeLedger ledger;
            const auto enter1 = ledger.Request(true);
            ledger.Settle(enter1, true);  // the frame entered
            const auto save = ledger.Request(false);
            const auto enter2 = ledger.Request(true);
            Expect(ledger.on, "K off/on: the flag is on at once");
            ledger.Settle(save, false);  // the Save's leave: older than the Enter
            Expect(ledger.on, "K off/on: the Save's leave does not turn the flag off under the newer Enter");
            ledger.Settle(enter2, true);
            Expect(ledger.on, "K off/on: the Enter settles it on (the widget is in the mode, the hook and the wraps keep input)");
        }
        {
            // one K: off
            Layout::ModeLedger ledger;
            ledger.Settle(ledger.Request(true), true);
            const auto save = ledger.Request(false);
            Expect(!ledger.on, "one K: the flag is off at once");
            ledger.Settle(save, false);
            Expect(!ledger.on, "one K: the leave settles it off");
        }
        {
            // a save that failed keeps the mode on; an entry that was refused turns it off
            Layout::ModeLedger ledger;
            ledger.Settle(ledger.Request(true), true);
            const auto save = ledger.Request(false);
            ledger.Settle(save, true);
            Expect(ledger.on, "failed save: the frame's result (still on) is the flag");
            Layout::ModeLedger refused;
            refused.Settle(refused.Request(true), false);
            Expect(!refused.on, "refused entry: the frame's result (off) is the flag");
        }
        {
            // K on/off/on: three requests, only the last settles
            Layout::ModeLedger ledger;
            const auto a = ledger.Request(true);
            const auto b = ledger.Request(false);
            const auto c = ledger.Request(true);
            ledger.Settle(a, true);
            ledger.Settle(b, false);
            Expect(ledger.on, "K on/off/on: the older results change nothing");
            ledger.Settle(c, false);  // e.g. the chrome was missing
            Expect(!ledger.on && c > b && b > a, "K on/off/on: the newest result is the flag; the numbers grow");
        }

        // fix wave 2: a frame that runs its queue as LayoutFrame does - each request's result settled, a frame-side change
        // (B/Esc's leave, the lock button, a tab change) numbered newest, and the frame's state synced when nothing is left queued
        enum class Req { kEnter, kSave, kNavLeave };
        struct Queued
        {
            Req kind;
            std::uint64_t seq;
        };
        const auto queue = [](Layout::ModeLedger& a_ledger, std::vector<Queued>& a_queue, Req a_kind) {
            const auto seq = a_kind == Req::kNavLeave ? 0 : a_ledger.Request(a_kind == Req::kEnter);
            a_queue.push_back({ a_kind, seq });
        };
        // returns whether the widget is in the mode after the frame
        const auto frame = [](Layout::ModeLedger& a_ledger, std::vector<Queued>& a_queue, bool a_active, bool a_tabChange) {
            for (const auto& request : a_queue) {
                if (request.kind == Req::kNavLeave) {
                    if (a_active) {
                        const auto seq = a_ledger.Request(false);  // BeginModeChange
                        a_active = false;
                        a_ledger.Settle(seq, a_active);
                    }
                    continue;
                }
                a_active = request.kind == Req::kEnter;  // Enter succeeds, Save leaves
                a_ledger.Settle(request.seq, a_active);
            }
            a_queue.clear();
            if (a_tabChange && a_active) {
                const auto seq = a_ledger.Request(false);
                a_active = false;
                a_ledger.Settle(seq, a_active);
                a_ledger.Sync(false, a_active);
            }
            a_ledger.Sync(!a_queue.empty(), a_active);
            return a_active;
        };
        {
            // Esc, K, K before the next frame, in the mode: [Nav(Esc) -> leave, Save, Enter]
            Layout::ModeLedger ledger;
            std::vector<Queued> q;
            ledger.Settle(ledger.Request(true), true);
            queue(ledger, q, Req::kNavLeave);
            queue(ledger, q, Req::kSave);  // the hook still sees the mode on: K saves
            queue(ledger, q, Req::kEnter);
            const bool active = frame(ledger, q, true, false);
            Expect(active && ledger.on, Text("[Nav(Esc), Save, Enter]: the widget in the mode and the flag on (active %d, flag %d)", active, ledger.on));
        }
        {
            // a frame-side leave (the lock button) with an Enter already queued after it
            Layout::ModeLedger ledger;
            std::vector<Queued> q;
            ledger.Settle(ledger.Request(true), true);
            queue(ledger, q, Req::kSave);
            queue(ledger, q, Req::kEnter);
            // the lock button's leave runs before the queue in a frame of its own (BeginModeChange numbers it newest)
            const auto lockSeq = ledger.Request(false);
            ledger.Settle(lockSeq, false);
            const bool active = frame(ledger, q, false, false);
            Expect(active && ledger.on, Text("frame-side leave, then a queued Enter: flag follows the widget (active %d, flag %d)", active, ledger.on));
        }
        {
            // K, K and a tab change in the same frame: the tab change ends the mode the Enter started
            Layout::ModeLedger ledger;
            std::vector<Queued> q;
            ledger.Settle(ledger.Request(true), true);
            queue(ledger, q, Req::kSave);
            queue(ledger, q, Req::kEnter);
            const bool active = frame(ledger, q, true, true);
            Expect(!active && !ledger.on, Text("K, K + tab change: out of the mode, flag off (active %d, flag %d)", active, ledger.on));
        }
        {
            // a request queued while the frame runs is left alone by Sync; it settles when it runs
            Layout::ModeLedger ledger;
            const auto enter = ledger.Request(true);
            ledger.Sync(true, false);
            Expect(ledger.on, "Sync with a request still queued changes nothing");
            ledger.Settle(enter, true);
            Expect(ledger.on, "... and that request settles it");
            const auto before = ledger.latest;
            ledger.Sync(false, false);
            Expect(!ledger.on && ledger.latest > before, "Sync with nothing queued: the frame's state, the number moved on");
            ledger.Settle(before, true);
            Expect(!ledger.on, "a result of a request from before the Sync cannot flip it back");
        }
    }
}

namespace
{
    void DrawNothing(RE::GFxMovieView*, RE::GFxValue&, float, float, void*) {}

    // API v2 review: these registry cases send the frozen v1 struct (48 bytes), so they keep testing the v1 path; the v2 cases are
    // tools/magic-menu-block-api-check's
    BlockApi::RegistrationV1 Reg(const char* a_id, float a_height = 100.0f)
    {
        return { .id = a_id, .title = "T", .height = a_height, .draw = DrawNothing };
    }

    BlockRegistry::Outcome Send(BlockRegistry::Registry& a_registry, const BlockApi::RegistrationV1& a_reg, std::string_view a_sender = "AddonA")
    {
        return a_registry.Register(a_sender, &a_reg, sizeof(a_reg));
    }

    void ExpectRefused(const BlockRegistry::Outcome& a_outcome, std::string_view a_reasonPart, const std::string& a_case)
    {
        Expect(!a_outcome.accepted && a_outcome.reason.find(a_reasonPart) != std::string::npos,
            Text("%s: refused, reason has \"%s\" (got %s \"%s\")", a_case.c_str(), std::string(a_reasonPart).c_str(),
                a_outcome.accepted ? "accepted" : "refused", a_outcome.reason.c_str()));
    }

    void AddonRegistry()
    {
        std::printf("add-on block registry (Task 6)\n");
        const std::set<std::string> builtins(kKnown.begin(), kKnown.end());
        BlockRegistry::Registry registry(builtins);
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInf = std::numeric_limits<float>::infinity();

        const auto first = Send(registry, Reg("stats"));
        Expect(first.accepted && first.id == "AddonA.stats" && first.reason.empty(),
            Text("no '.': prefixed with the sender -> AddonA.stats (got %s)", first.id.c_str()));
        const auto dotted = Send(registry, Reg("Other.panel"), "AddonB");
        Expect(dotted.accepted && dotted.id == "Other.panel", Text("with a '.': kept as sent -> Other.panel (got %s)", dotted.id.c_str()));
        Expect(BlockRegistry::QualifiedId("", "x") == "addon.x", "no sender name: addon.x");
        ExpectRefused(Send(registry, Reg("stats")), "registered already", "the same id again from the same plugin");
        ExpectRefused(Send(registry, Reg("AddonA.stats"), "AddonC"), "registered already", "the same final id from another plugin");
        ExpectRefused(Send(registry, Reg("")), "empty", "empty id");
        ExpectRefused(Send(registry, Reg(nullptr)), "empty", "null id");
        const std::string longId(BlockApi::kMaxIdLength + 1, 'x');
        ExpectRefused(Send(registry, Reg(longId.c_str())), "longer", "an id over kMaxIdLength");
        ExpectRefused(Send(registry, Reg("x.nan", kNaN)), "height", "NaN height");
        ExpectRefused(Send(registry, Reg("x.inf", kInf)), "height", "infinite height");
        ExpectRefused(Send(registry, Reg("x.neg", -5.0f)), "height", "negative height");
        ExpectRefused(Send(registry, Reg("x.zero", 0.0f)), "height", "zero height");
        ExpectRefused(Send(registry, Reg("x.tall", BlockApi::kMaxHeight + 1.0f)), "height", "a height over kMaxHeight");
        auto noDraw = Reg("x.nodraw");
        noDraw.draw = nullptr;
        ExpectRefused(Send(registry, noDraw), "draw", "null draw callback");
        auto newer = Reg("x.newer");
        newer.apiVersion = BlockApi::kVersion + 1;  // API v3: version 4 is the first one this host does not read
        ExpectRefused(Send(registry, newer), Text("apiVersion %u", BlockApi::kVersion + 1), "a newer apiVersion");
        auto older = Reg("x.old");
        older.apiVersion = 0;
        ExpectRefused(Send(registry, older), "apiVersion 0", "apiVersion 0");
        auto small = Reg("x.small");
        small.structSize = 8;
        ExpectRefused(Send(registry, small), "structSize", "structSize smaller than v1");
        const auto shortMessage = Reg("x.short");
        ExpectRefused(registry.Register("AddonA", &shortMessage, BlockApi::kRegistrationV1Size - 1), "smaller", "message shorter than v1");
        ExpectRefused(registry.Register("AddonA", nullptr, 0), "no Registration", "no data");
        auto longer = Reg("x.longer");  // a v1 add-on whose struct grew at the end: its v1 part is read
        longer.structSize = BlockApi::kRegistrationV1Size + 16;
        Expect(Send(registry, longer).accepted, "structSize larger than v1 (same version): accepted");
        // a built-in id comes only with a '.' (no '.' is prefixed): "preview" from AddonA is AddonA.preview, not the built-in block
        Expect(Send(registry, Reg("preview")).id == "AddonA.preview", "\"preview\" from an add-on is AddonA.preview, not the built-in block");
        BlockRegistry::Registry reserved({ "Mine.block" });
        ExpectRefused(Send(reserved, Reg("Mine.block")), "built-in", "a reserved id");

        std::string id = "Temp.copy", title = "COPIED";
        auto reg = Reg(id.c_str());
        reg.title = title.c_str();
        const auto copied = Send(registry, reg);
        id.assign("overwritten");
        title.assign("overwritten");
        const auto* kept = registry.Find("Temp.copy");
        Expect(copied.accepted && kept && kept->title == "COPIED", "id and title copied (the sender's strings changed after the call)");

        std::vector<std::string> order;
        for (const auto& block : registry.Blocks()) order.push_back(block.id);
        const std::vector<std::string> want{ "AddonA.stats", "Other.panel", "x.longer", "AddonA.preview", "Temp.copy" };
        Expect(order == want, Text("registration order kept: %s", Show(order).c_str()));
        Expect(registry.Blocks()[1].sender == "AddonB" && Near(registry.Blocks()[0].height, 100.0), "sender and height kept");

        const bool firstClose = registry.Close();
        const bool secondClose = registry.Close();
        Expect(firstClose && !secondClose && registry.Closed(), "Close: true once, then closed");
        ExpectRefused(Send(registry, Reg("late")), "closed", "a registration after the first menu open");
        Expect(registry.Blocks().size() == want.size(), "nothing added after the close");
    }

    void AddonLayout()
    {
        std::printf("add-on blocks in the layout (Task 6)\n");
        const State def = Layout::Default(kValues);
        const std::vector<BlockId> addons{ "A.one", "B.two" };
        const State withAddons = Layout::AddBlocks(def, addons);
        Expect(withAddons.columns[0] == def.columns[0] &&
                   withAddons.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kNodeCard, "A.one", "B.two" },
            Text("AddBlocks: at the end of the right column in order (got %s)", Show(withAddons.columns[1]).c_str()));
        Expect(Layout::AddBlocks(withAddons, addons) == withAddons, "AddBlocks twice: unchanged (each once)");
        Expect(Layout::AddBlocks(def, addons, 5) == def, "AddBlocks to a column out of range: unchanged");

        // final review (RF3 test path): [Debug] bDebugFillerBlock's block joins the left column after the spell card, the others the
        // right column; a saved layout without it gets it there too (Repair follows the default)
        const State withFiller = Layout::AddAddons(def, { "A.one", Layout::kDebugFillerId, "B.two" });
        Expect(withFiller.columns[0] == std::vector<BlockId>{ Layout::kPreview, Layout::kProgress, Layout::kSpellCard, Layout::kDebugFillerId } &&
                   withFiller.columns[1] == std::vector<BlockId>{ Layout::kTree, Layout::kNodeCard, "A.one", "B.two" },
            Text("AddAddons: the filler after the spell card (%s), the rest in the right column (%s)", Show(withFiller.columns[0]).c_str(),
                Show(withFiller.columns[1]).c_str()));
        Expect(Layout::AddAddons(def, addons) == withAddons, "AddAddons without the filler: as AddBlocks to the right column");
        std::set<BlockId> knownFiller = kKnown;
        knownFiller.insert(Layout::kDebugFillerId);
        const State repairedFiller = Layout::Repair(def, knownFiller, Layout::AddAddons(def, { Layout::kDebugFillerId }));
        Expect(repairedFiller.columns[0].back() == Layout::kDebugFillerId && std::ranges::count(repairedFiller.columns[1], Layout::kDebugFillerId) == 0,
            "Repair: a saved layout without the filler gets it at the left column's end");
        auto fillerHeights = kHeights;
        fillerHeights[Layout::kDebugFillerId] = 300;
        const State fillerOnly = Layout::AddAddons(def, { Layout::kDebugFillerId });
        const auto fillerRects = Layout::Arrange(fillerOnly, fillerHeights, {}, {}, kFrames);
        const auto fillerContent = Layout::ContentHeight(fillerOnly, fillerRects, kFrames);
        Expect(fillerContent[0] > kFrames[0].Height() && fillerRects.contains(Layout::kDebugFillerId),
            Text("the filler makes the preview's column overflow (content %.1f > visible %.1f): it scrolls", fillerContent[0], kFrames[0].Height()));

        // the saved file has B.two at the top of the left column: it keeps that place, A.one (no place) goes to the right column's end
        State saved = def;
        saved.columns[0].insert(saved.columns[0].begin(), "B.two");
        std::set<BlockId> known = kKnown;
        known.insert(addons.begin(), addons.end());
        const State repaired = Layout::Repair(saved, known, withAddons);
        Expect(repaired.columns[0].front() == "B.two" && repaired.columns[1].back() == "A.one" && std::ranges::count(repaired.columns[1], "B.two") == 0,
            Text("Repair: a saved add-on keeps its place, a new one at the right column's end (%s / %s)", Show(repaired.columns[0]).c_str(),
                Show(repaired.columns[1]).c_str()));

        // an add-on not installed any more: its id stays in the layout (Repair keeps unknown ids), arranged as absent
        State gone = def;
        gone.columns[1].insert(gone.columns[1].begin() + 1, "Gone.block");
        const State kept = Layout::Repair(gone, kKnown, def);
        Expect(kept.columns[1] == std::vector<BlockId>{ Layout::kTree, "Gone.block", Layout::kNodeCard }, "Repair keeps an absent add-on's place");
        const auto absent = Layout::Arrange(kept, kHeights, {}, {}, kFrames);
        ExpectRect(absent, Layout::kNodeCard, 978, 306, 1238, 623, "absent add-on between tree and node card");
        Expect(!absent.contains("Gone.block"), "the absent add-on gets no rect");

        // an add-on at the right column's end: below the node card, its column overflows and scrolls
        auto heights = kHeights;
        heights["A.one"] = 300;
        const State one = Layout::AddBlocks(def, { "A.one" });
        const auto withOne = Layout::Arrange(one, heights, {}, {}, kFrames);
        ExpectRect(withOne, "A.one", 978, 632, 1238, 932, "an add-on 300 high below the node card");
        const auto content = Layout::ContentHeight(one, withOne, kFrames);
        Expect(Near(content[1], 865) && Near(Layout::ClampScroll(1000, content[1], kFrames[1].Height()), 309),
            Text("its column overflows: content 865, scroll clamps at 309 (got %.1f)", content[1]));

        // the Task 1 minor: a bad height must not move the other blocks (the add-on above the tree, so a spread would move both)
        State badState = def;
        badState.columns[1].insert(badState.columns[1].begin(), "Bad.block");
        for (const double bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -50.0 }) {
            auto badHeights = kHeights;
            badHeights["Bad.block"] = bad;
            const auto rects = Layout::Arrange(badState, badHeights, {}, { Layout::kNodeCard }, kFrames);
            Expect(!rects.contains("Bad.block"), Text("height %.0f: the add-on is absent", bad));
            ExpectRect(rects, Layout::kTree, 978, 67, 1238, 297, Text("height %.0f: tree unmoved", bad));
            ExpectRect(rects, Layout::kNodeCard, 978, 306, 1238, 623, Text("height %.0f: node card (fill) unmoved", bad));
        }
    }

    // HoM add-on plan Task 2 (design G-e): the retired ids tree / progress / nodecard stay on disk; ToFinal renames them on every
    // read, ToLegacy back on every write. kHomTree is the add-on's final id (the alias a v3 block's legacyId makes)
    constexpr const char* kHomTree = "SL_MagicMenu_HoM.tree";
    const std::map<BlockId, BlockId> kTreeAlias{ { Layout::kTree, kHomTree } };

    int Count(const State& a_state, const BlockId& a_id)
    {
        int count = 0;
        for (const auto& column : a_state.columns) count += static_cast<int>(std::ranges::count(column, a_id));
        return count;
    }

    /// What Settings::AddBlocks builds from a layout as read: the same function (Layout::WithAddons, HoM add-on Task 5 - no copy),
    /// with the core's built-ins
    State Upgrade(const State& a_read, const std::map<BlockId, BlockId>& a_aliases, const std::vector<BlockId>& a_addons)
    {
        return Layout::WithAddons(a_read, kBuiltins, a_aliases, a_addons, Layout::Default(kValues)).layout;
    }

    void LegacyIds()
    {
        std::printf("legacy ids in the saved layout (HoM add-on Task 2)\n");
        const State legacy{ { std::vector<BlockId>{ Layout::kPreview, Layout::kTree }, std::vector<BlockId>{ Layout::kNodeCard } }, 900 };
        const State finalState = Layout::ToFinal(legacy, kTreeAlias);
        Expect(finalState.columns[0] == std::vector<BlockId>{ Layout::kPreview, kHomTree } && finalState.columns[1] == legacy.columns[1] &&
                   Near(finalState.border, 900),
            Text("ToFinal: tree read as %s in its place in the left column (got %s / %s)", kHomTree, Show(finalState.columns[0]).c_str(),
                Show(finalState.columns[1]).c_str()));
        Expect(Layout::ToLegacy(finalState, kTreeAlias) == legacy, "ToLegacy: written back as tree again");
        const auto json = Layout::ToJson(Layout::ToLegacy(finalState, kTreeAlias));
        const auto back = Layout::FromJson(json, nullptr);
        Expect(json.find(kHomTree) == std::string::npos && back && *back == legacy && Layout::ToFinal(*back, kTreeAlias) == finalState,
            "the JSON on disk holds tree, not the final id; read again it is the final id in the same place");

        // a file with both ids: the retired one's place wins, the other is dropped (either order, either column)
        const State both{ { std::vector<BlockId>{ kHomTree, Layout::kPreview }, std::vector<BlockId>{ Layout::kTree, Layout::kNodeCard } }, 900 };
        const State bothRead = Layout::ToFinal(both, kTreeAlias);
        Expect(bothRead.columns[0] == std::vector<BlockId>{ Layout::kPreview } && bothRead.columns[1] == std::vector<BlockId>{ kHomTree, Layout::kNodeCard },
            Text("both ids: the retired one's place (right column top) wins (got %s / %s)", Show(bothRead.columns[0]).c_str(),
                Show(bothRead.columns[1]).c_str()));
        const State bothSame{ { std::vector<BlockId>{ Layout::kTree, Layout::kPreview, kHomTree }, {} }, 900 };
        Expect(Layout::ToFinal(bothSame, kTreeAlias).columns[0] == std::vector<BlockId>{ kHomTree, Layout::kPreview },
            "both ids in one column, the final one below: the retired one's place wins");

        // ToLegacy never writes a duplicate: a stray retired id beside its final one is dropped (the final one's place is the
        // player's), a final id with no alias is written as is
        const State stray{ { std::vector<BlockId>{ Layout::kTree, Layout::kPreview }, std::vector<BlockId>{ kHomTree, "A.one" } }, 900 };
        const State strayOut = Layout::ToLegacy(stray, kTreeAlias);
        Expect(strayOut.columns[0] == std::vector<BlockId>{ Layout::kPreview } && strayOut.columns[1] == std::vector<BlockId>{ Layout::kTree, "A.one" } &&
                   Count(strayOut, Layout::kTree) == 1,
            Text("ToLegacy with both ids: tree once, at the final id's place (got %s / %s)", Show(strayOut.columns[0]).c_str(),
                Show(strayOut.columns[1]).c_str()));
        Expect(Layout::ToLegacy(finalState, {}) == finalState && Layout::ToFinal(finalState, {}) == finalState && Layout::ToFinal(legacy, {}) == legacy,
            "no alias: ToFinal / ToLegacy change nothing");

        // upgrade -> save -> read by a build with no alias (downgrade) -> save -> alias again: tree keeps the place the player gave it
        const std::vector<BlockId> addons{ kHomTree };
        const State moved = Layout::Move(Layout::Default(kValues), Layout::kTree, Slot{ 0, Layout::kProgress });  // tree above progress
        const State up1 = Upgrade(moved, kTreeAlias, addons);
        const State expectUp{ { std::vector<BlockId>{ Layout::kPreview, kHomTree, Layout::kProgress, Layout::kSpellCard },
                                  std::vector<BlockId>{ Layout::kNodeCard } },
            moved.border };
        Expect(up1 == expectUp, Text("upgrade: the add-on in tree's place (got %s / %s)", Show(up1.columns[0]).c_str(), Show(up1.columns[1]).c_str()));
        const auto saved = Layout::FromJson(Layout::ToJson(Layout::ToLegacy(up1, kTreeAlias)), nullptr);
        const State down = saved ? Layout::Repair(Layout::ToFinal(*saved, {}), kKnown, Layout::Default(kValues)) : State{};
        Expect(down == moved,
            Text("downgrade: the older build reads tree where it was (got %s / %s)", Show(down.columns[0]).c_str(), Show(down.columns[1]).c_str()));
        const auto savedDown = Layout::FromJson(Layout::ToJson(Layout::ToLegacy(down, {})), nullptr);
        const State up2 = savedDown ? Upgrade(*savedDown, kTreeAlias, addons) : State{};
        Expect(up2 == up1 && Count(up2, Layout::kTree) == 0 && Count(up2, kHomTree) == 1, "upgrade again: the same place, the add-on once");

        // the default layout with the alias: the add-on takes tree's place, AddAddons does not append it a second time
        const State defUp = Upgrade(Layout::Default(kValues), kTreeAlias, addons);
        Expect(defUp.columns[1] == std::vector<BlockId>{ kHomTree, Layout::kNodeCard } && Count(defUp, kHomTree) == 1,
            Text("default + alias: right column {%s, nodecard} (got %s)", kHomTree, Show(defUp.columns[1]).c_str()));

    }

    // HoM add-on plan Task 5 (Step 2): the core alone - the default layout with no add-on, the core's own heights and built-ins, as
    // Settings::AddBlocks and Widget's BlockHeights make them. The retired ids keep their places (unknown ids), Arrange skips them
    // (no height): preview, then the spell card with no gap; the right column is empty
    void CoreOnly()
    {
        std::printf("the core alone (HoM add-on Task 5): retired ids skipped, no gap\n");
        Expect(kCoreHeights.size() == 2 && kCoreHeights.contains(Layout::kPreview) && kCoreHeights.contains(Layout::kSpellCard),
            Text("DefaultHeights: preview and spellcard only (%zu heights)", kCoreHeights.size()));
        for (const auto id : BlockRules::kRetiredIds) {
            Expect(!kCoreHeights.contains(BlockId(id)), Text("DefaultHeights has no %s (a retired id)", BlockId(id).c_str()));
        }
        const State def = Layout::Default(kValues);
        const auto with = Layout::WithAddons(def, kBuiltins, {}, {}, def);
        Expect(with.layout == def && with.fallback == def && with.renamed == def,
            Text("no add-on: WithAddons keeps the default as it is, tree / progress / nodecard in place (got %s / %s)", Show(with.layout.columns[0]).c_str(),
                Show(with.layout.columns[1]).c_str()));
        // a file of an older build (only the retired ids and the core's two): read the same way
        const auto file = Layout::FromJson(R"({"version":1,"columns":[["preview","progress","spellcard"],["tree","nodecard"]],"border":969})", nullptr);
        Expect(file && Layout::WithAddons(*file, kBuiltins, {}, {}, def).layout == def, "an older build's default file: read as the default, nothing added");
        // a file that lost the spell card: Repair appends the built-in to its default column; it never appends a retired id
        const State noCard{ { std::vector<BlockId>{ Layout::kPreview }, std::vector<BlockId>{} }, 969 };
        const auto repaired = Layout::WithAddons(noCard, kBuiltins, {}, {}, def).layout;
        Expect(repaired.columns[0] == std::vector<BlockId>{ Layout::kPreview, Layout::kSpellCard } && repaired.columns[1].empty(),
            Text("a file without spellcard: it is appended, no retired id comes back (got %s / %s)", Show(repaired.columns[0]).c_str(),
                Show(repaired.columns[1]).c_str()));
        for (const bool own : { true, false }) {
            const std::set<BlockId> fill = own ? std::set<BlockId>{ Layout::kSpellCard } : std::set<BlockId>{};
            const auto rects = Layout::Arrange(with.layout, kCoreHeights, {}, fill, kFrames);
            const char* card = own ? "core only, our card" : "core only, SkyUI's card";
            Expect(rects.size() == 2 && !rects.contains(Layout::kTree) && !rects.contains(Layout::kProgress) && !rects.contains(Layout::kNodeCard),
                Text("%s: Arrange skips the retired ids (%zu rects)", card, rects.size()));
            ExpectRect(rects, Layout::kPreview, 648, 67, 969, 297, card);
            // one gap (9) below the preview: our card fills the column down to the item card's ini bottom, SkyUI's keeps its 206
            ExpectRect(rects, Layout::kSpellCard, 648, 306, 969, own ? 623 : 512, card);
            const auto content = Layout::ContentHeight(with.layout, rects, kFrames);
            Expect(Near(content[1], 0) && Near(content[0], own ? 556 : 445),
                Text("%s: right column empty (content %.1f), left %.1f", card, content[1], content[0]));
        }
        // the empty look (BlockRules::EmptyLookOf): the preview keeps its centred line, a retired id has none of its own
        for (const auto id : BlockRules::kRetiredIds) {
            Expect(BlockRules::EmptyLookOf(id, true, false) == BlockRules::EmptyLook::kAsUsual,
                Text("EmptyLookOf(%s, nothing shown): as usual (the add-on's own look; the tree's line left the core)", BlockId(id).c_str()));
        }
    }
}

namespace
{
    /// Block API v2 kMessageHeight: an add-on block at the top of the right column shrinks, grows back and shrinks again
    /// (the 'SLMH' sequence of a spell change in the SR block's line layout): every time the blocks below it move by the
    /// difference, the column stays one gap apart with no overlap, its content height (the scroll extent) follows, and the
    /// left column does not move
    void AddonHeightChange()
    {
        std::printf("add-on block height change (kMessageHeight)\n");
        const BlockId addon = "SpellResearch.archetypes";
        State state = Layout::Default(kValues);
        state.columns[1].insert(state.columns[1].begin(), addon);  // blocks below it, so they move
        constexpr double kRegistered = 134, kShrunk = 112;  // the SR block's list and reason-only heights
        const auto gapsOk = [&](const std::map<BlockId, Rect>& a_rects, int a_column) {
            double bottom = -1;
            for (const auto& id : state.columns[a_column]) {
                const auto it = a_rects.find(id);
                if (it == a_rects.end()) continue;
                if (bottom >= 0 && !Near(it->second.top - bottom, kFrames[a_column].gap)) return false;
                bottom = it->second.bottom;
            }
            return true;
        };
        const auto arrange = [&](double a_height) {
            auto heights = kHeights;
            heights[addon] = a_height;
            return Layout::Arrange(state, heights, {}, {}, kFrames);
        };
        const std::vector<double> sequence{ kRegistered, kShrunk, kRegistered, kShrunk };
        std::vector<std::map<BlockId, Rect>> steps;
        for (const double height : sequence) steps.push_back(arrange(height));
        const auto& below = state.columns[1];
        for (std::size_t i = 1; i < steps.size(); ++i) {
            const double change = sequence[i] - sequence[i - 1];
            const auto& was = steps[i - 1];
            const auto& now = steps[i];
            Expect(Near(now.at(addon).top, was.at(addon).top) && Near(now.at(addon).Height(), sequence[i]),
                Text("step %zu: the add-on keeps its top and is %.0f high", i, sequence[i]));
            bool moved = true;
            for (std::size_t k = 1; k < below.size(); ++k) {
                const auto& id = below[k];
                moved = moved && Near(now.at(id).top - was.at(id).top, change) && Near(now.at(id).Height(), was.at(id).Height());
            }
            Expect(moved, Text("step %zu (%+.0f): every block below moves by %+.0f, its size unchanged", i, change, change));
            bool left = true;
            for (const auto& id : state.columns[0]) {
                left = left && was.contains(id) == now.contains(id) &&
                       (!now.contains(id) || (Near(now.at(id).top, was.at(id).top) && Near(now.at(id).bottom, was.at(id).bottom) &&
                                                 Near(now.at(id).left, was.at(id).left) && Near(now.at(id).right, was.at(id).right)));
            }
            Expect(left, Text("step %zu: every block of the left column where it was", i));
            Expect(gapsOk(now, 1) && gapsOk(now, 0), Text("step %zu: both columns one gap apart, none overlapping", i));
            const double contentWas = Layout::ContentHeight(state, was, kFrames)[1];
            const double contentNow = Layout::ContentHeight(state, now, kFrames)[1];
            Expect(Near(contentNow - contentWas, change), Text("step %zu: the right column's content %+.0f (%.0f -> %.0f)", i, change, contentWas, contentNow));
        }
        // the scroll extent: a column scrolled to its end before the shrink is clamped to the new end
        const double visible = kFrames[1].Height();
        const double tall = Layout::ContentHeight(state, steps[0], kFrames)[1] + 200;  // as if the column overflowed
        Expect(Near(Layout::ClampScroll(tall - visible, tall - (kRegistered - kShrunk), visible), std::max(0.0, tall - (kRegistered - kShrunk) - visible)),
            "an overflowing column scrolled to its end: the scroll clamps to the shrunk content's end");
    }
}

namespace
{
    // user 2026-10-30: blocks fit their content (BlockRules::ApplyFits with Layout::Arrange), the layout row, the gear
    void FitLayout()
    {
        std::printf("blocks fit their content (BlockRules::ApplyFits, Arrange)\n");
        const State def = Layout::Default(kValues);
        const std::set<BlockId> fillCard{ Layout::kSpellCard };
        const auto same = [](const Settings::Rect& a_a, const Settings::Rect& a_b) {
            return Near(a_a.left, a_b.left) && Near(a_a.top, a_b.top) && Near(a_a.right, a_b.right) && Near(a_a.bottom, a_b.bottom);
        };
        const auto sameAll = [&](const std::map<BlockId, Settings::Rect>& a_a, const std::map<BlockId, Settings::Rect>& a_b) {
            if (a_a.size() != a_b.size()) return false;
            for (const auto& [id, rect] : a_a) {
                if (!a_b.contains(id) || !same(rect, a_b.at(id))) return false;
            }
            return true;
        };
        const auto arrange = [&](const std::map<BlockId, double>& a_fits, bool a_mode) {
            auto heights = kHeights;
            auto fill = fillCard;
            BlockRules::ApplyFits(heights, fill, a_fits, a_mode);
            return Layout::Arrange(def, heights, {}, fill, kFrames);
        };
        const auto full = arrange({}, false);
        const auto progress = full.at(Layout::kProgress), card = full.at(Layout::kSpellCard);
        const double shrink = 40.0;
        const auto fitted = arrange({ { Layout::kProgress, progress.Height() - shrink } }, false);
        Expect(Near(fitted.at(Layout::kProgress).Height(), progress.Height() - shrink) && Near(fitted.at(Layout::kSpellCard).top, card.top - shrink),
            Text("the progress card %.0f shorter: the spell card under it moves up %.0f (top %.1f -> %.1f)", shrink, shrink, card.top,
                fitted.at(Layout::kSpellCard).top));
        Expect(Near(fitted.at(Layout::kSpellCard).bottom, card.bottom), "the spell card (a fill block, not fitted) still reaches its column's bottom");
        Expect(same(fitted.at(Layout::kTree), full.at(Layout::kTree)) && same(fitted.at(Layout::kNodeCard), full.at(Layout::kNodeCard)),
            "the other column is untouched");
        const double cardFit = 120.0;
        const auto both = arrange({ { Layout::kProgress, progress.Height() - shrink }, { Layout::kSpellCard, cardFit } }, false);
        Expect(Near(both.at(Layout::kSpellCard).Height(), cardFit) && Near(both.at(Layout::kSpellCard).top, card.top - shrink),
            Text("the spell card fitted to %.0f leaves the fill: %.0f high under the shorter progress card", cardFit, both.at(Layout::kSpellCard).Height()));
        const auto mode = arrange({ { Layout::kProgress, progress.Height() - shrink }, { Layout::kSpellCard, cardFit } }, true);
        Expect(sameAll(mode, full), "in the layout mode every block is at its full height (the fits kept for the mode's end)");
        const auto nodeCard = full.at(Layout::kNodeCard);
        const auto nodeFit = arrange({ { Layout::kNodeCard, 60.0 } }, false);
        Expect(Near(nodeFit.at(Layout::kNodeCard).Height(), 60.0) && same(nodeFit.at(Layout::kTree), full.at(Layout::kTree)) &&
                   same(nodeFit.at(Layout::kPreview), full.at(Layout::kPreview)),
            Text("the node card fitted to its hint line (60 of %.0f): the tree above and the left column unmoved", nodeCard.Height()));
        // the scroll follows: a column that overflowed and shrinks back clamps its scroll (Layout::ClampScroll)
        auto tall = kHeights;
        tall[Layout::kNodeCard] += 200.0;
        const auto overflow = Layout::Arrange(def, tall, {}, fillCard, kFrames);
        const double contentTall = Layout::ContentHeight(def, overflow, kFrames)[1], visible = kFrames[1].Height();
        std::map<BlockId, double> fitTall{ { Layout::kNodeCard, nodeCard.Height() } };
        auto fill = fillCard;
        BlockRules::ApplyFits(tall, fill, fitTall, false);
        const auto back = Layout::Arrange(def, tall, {}, fill, kFrames);
        const double contentBack = Layout::ContentHeight(def, back, kFrames)[1];
        Expect(contentTall > visible && Layout::ClampScroll(contentTall - visible, contentBack, visible) <= std::max(0.0, contentBack - visible) + kTolerance,
            Text("a column scrolled to its end (content %.0f) whose card fits back (content %.0f): the scroll clamps to %.0f", contentTall, contentBack,
                Layout::ClampScroll(contentTall - visible, contentBack, visible)));
        Expect(BlockRules::FittedHeight(80.0, 100.0) == 80.0 + BlockRules::kFitSlack && BlockRules::FittedHeight(150.0, 100.0) == 100.0 &&
                   BlockRules::FittedHeight(std::numeric_limits<double>::quiet_NaN(), 100.0) == 100.0,
            "FittedHeight: needed + slack, never over the full height, a bad needed height keeps the full one");

        std::printf("the layout row and the gear\n");
        const Settings::Rect visible1610{ 0, -40, 1280, 760 };
        // the reset button right of the gear in the edit mode (user 2026-10-30): eased unfold / fold, rect, clicks
        {
            using BlockRules::AnimWidth;
            const double open = BlockRules::kResetOpenMs, close = BlockRules::kResetCloseMs;
            Expect(Near(AnimWidth(0, 90, 1000, 1000, open), 0) && Near(AnimWidth(0, 90, 1000, 1000 + open, open), 90) &&
                       Near(AnimWidth(0, 90, 1000, 5000, open), 90),
                "unfold: 0 at the start, full at the end and after it");
            const double mid = AnimWidth(0, 90, 0, open / 2, open);
            Expect(mid > 45 && mid < 90, Text("ease-out: more than half way at half the time (%.1f of 90)", mid));
            Expect(Near(AnimWidth(90, 0, 0, 0, close), 90) && Near(AnimWidth(90, 0, 0, close, close), 0) && AnimWidth(90, 0, 0, close / 2, close) < 45,
                "fold: full at the start, 0 at the end, eased");
            const double resumed = AnimWidth(40, 90, 0, open / 2, open);
            Expect(Near(AnimWidth(40, 90, 0, 0, open), 40) && resumed > 40 && resumed < 90 && Near(AnimWidth(40, 90, 0, open, open), 90),
                Text("a fold turned into an unfold half way: starts at 40 (where it was), %.1f half way, full at the end", resumed));
            const Settings::Rect gear{ 648, 655, 666, 673 };
            const auto button = BlockRules::ResetButtonRect(gear, 90, 18, 4, visible1610);
            Expect(Near(button.left, 670) && Near(button.CenterY(), gear.CenterY()) && Near(button.Width(), 90),
                Text("the reset button 4 right of the gear on its row (%s)", Show(button).c_str()));
            const auto edge = BlockRules::ResetButtonRect(Settings::Rect{ 1240, 740, 1258, 758 }, 90, 18, 4, visible1610);
            Expect(edge.right <= 1280 + kTolerance && edge.bottom <= 760 + kTolerance, Text("at the stage's corner: kept inside (%s)", Show(edge).c_str()));
            Expect(BlockRules::ResetClickable(90, 90, true) && !BlockRules::ResetClickable(60, 90, true) && !BlockRules::ResetClickable(90, 90, false) &&
                       !BlockRules::ResetClickable(0, 0, true),
                "clickable only fully out and in the edit mode");
        }
        // the key-hint strip and the gear under it on one vertical line (user 2026-10-30): columns' bottom 623, left column at 648
        const auto row = BlockRules::HintRowRects(648, 623, 400, 24, 18, 4, 4, visible1610);
        Expect(Near(row.strip.left, 648) && Near(row.strip.top, 627) && Near(row.strip.Width(), 400) && Near(row.strip.Height(), 24),
            Text("strip: from the left column's left edge, 4 under the columns (%s)", Show(row.strip).c_str()));
        Expect(Near(row.gear.left, row.strip.left) && Near(row.gear.top, 655) && Near(row.gear.Width(), 18),
            Text("gear: one row under the strip (4 below it), its left edge on the strip's start (%s)", Show(row.gear).c_str()));
        const auto low = BlockRules::HintRowRects(648, 750, 400, 24, 18, 4, 4, visible1610);
        Expect(Near(low.gear.bottom, 760) && Near(low.gear.top - low.strip.bottom, 4) && Near(low.gear.left, low.strip.left),
            Text("columns down to the stage's bottom: strip and gear move up together, still apart by 4 (%s / %s)", Show(low.strip).c_str(), Show(low.gear).c_str()));
        const auto wide = BlockRules::HintRowRects(900, 600, 600, 24, 18, 4, 4, visible1610);
        Expect(Near(wide.strip.right, 1280) && Near(wide.gear.left, wide.strip.left),
            Text("a strip wider than the room right of the column: moved left to stay inside, the gear with it (%s)", Show(wide.strip).c_str()));
        const auto none = BlockRules::HintRowRects(648, 623, 0, 0, 18, 4, 4, visible1610);
        Expect(Near(none.gear.top, 627) && Near(none.gear.left, 648), "no strip: the gear alone, 4 under the columns at the column's edge");
        // the key-hint strip: SkyUI's bottomBar.buttonPanel moved as one clip (BlockRules::PlacePanel, VisibleUnion)
        {
            using BlockRules::Point;
            constexpr double kTol = 0.5;
            const auto unscaled = [](const Point& a_point) { return std::optional<Point>{ a_point }; };  // an unscaled parent
            const auto at = [](const std::optional<BlockRules::PanelPlace>& a_place) {
                return a_place ? Text("%.2f,%.2f", a_place->want.x, a_place->want.y) : std::string("no place");
            };
            const Settings::Rect content{ 120, 700, 520, 724 };  // the panel at 100,700; its visible buttons start 20 right of its origin
            const Point target{ 648, 647 };
            // first frame: nothing known in place - placed by SkyUI, moved by the target's step (648 - 120, 647 - 700)
            const auto first = BlockRules::PlacePanel(Point{ 100, 700 }, Point{ 100, 700 }, std::nullopt, content, target, unscaled, kTol);
            Expect(first && first->rePlaced && first->set && Near(first->want.x, 628) && Near(first->want.y, 647),
                "strip, first frame: SkyUI's place, set to 628,647 (" + at(first) + ")");
            // no change: the panel still where it was set (within a twip) - not re-placed, nothing set
            const Settings::Rect moved{ 648, 647, 1048, 671 };
            const auto still = BlockRules::PlacePanel(Point{ 628.02, 647 }, Point{ 628.02, 647 }, Point{ 628, 647 }, moved, target, unscaled, kTol);
            Expect(still && !still->rePlaced && !still->set, "strip, no change: not re-placed, no set");
            // SkyUI places it again the only way it does (positionElements writes _x, BottomBar.as:51): x back to 100, y as the host left it
            const Settings::Rect xOnly{ 120, 647, 520, 671 };
            const auto again = BlockRules::PlacePanel(Point{ 100, 647 }, Point{ 100, 647 }, Point{ 628, 647 }, xOnly, target, unscaled, kTol);
            Expect(again && again->rePlaced && again->set && Near(again->want.x, 628) && Near(again->want.y, 647),
                "strip, SkyUI set _x back to 100 (y kept): re-placed, set to 628,647 (" + at(again) + ")");
            // SkyUI placing it exactly onto the value in place: not seen as a re-place, and nothing to set (already where it belongs)
            const auto onto = BlockRules::PlacePanel(Point{ 628, 647 }, Point{ 628, 647 }, Point{ 628, 647 }, moved, target, unscaled, kTol);
            Expect(onto && !onto->rePlaced && !onto->set, "strip, re-placed exactly onto the place in use: harmless, no set");
            // a button inside it hides (its visible content now starts 60 right of the origin): no re-place, moved by the new step
            const Settings::Rect narrower{ 688, 647, 1048, 671 };
            const auto hidden = BlockRules::PlacePanel(Point{ 628, 647 }, Point{ 628, 647 }, Point{ 628, 647 }, narrower, target, unscaled, kTol);
            Expect(hidden && !hidden->rePlaced && hidden->set && Near(hidden->want.x, 588) && Near(hidden->want.y, 647),
                "strip, a button hid inside it: not re-placed, set to 588 so its visible start is 648 (" + at(hidden) + ")");
            // a scaled parent (Inventory Reforged: the menu at 0.85, offset 200,26): stage = 200 + 0.85 x, 26 + 0.85 y
            constexpr double kScale = 0.85, kOffX = 200, kOffY = 26;
            const auto toStage = [&](const Point& a_p) { return Point{ kOffX + kScale * a_p.x, kOffY + kScale * a_p.y }; };
            const auto toParent = [&](const Point& a_p) { return std::optional<Point>{ Point{ (a_p.x - kOffX) / kScale, (a_p.y - kOffY) / kScale } }; };
            // the panel at 100,700 in its parent: on the stage 285,621; its visible buttons from local x 20 (17 on the stage), 255 x 20.4
            const Settings::Rect scaled{ 302, 621, 557, 641.4 };
            const auto inScaled = BlockRules::PlacePanel(Point{ 100, 700 }, toStage(Point{ 100, 700 }), std::nullopt, scaled, target, toParent, kTol);
            const auto landed = inScaled ? toStage(inScaled->want) : Point{};
            Expect(inScaled && inScaled->set && Near(landed.x + 17, 648) && Near(landed.y, 647) && Near(inScaled->want.x, (631 - kOffX) / kScale),
                "strip in a 0.85 parent: origin " + at(inScaled) + " in the parent, its visible start on the stage at 648,647");
            // SkyUI re-places it under the 0.85 parent (_x back to 100, y kept at the host's 730.59): moved back onto the same origin
            if (inScaled) {
                const Point xBack{ 100, inScaled->want.y };
                const auto xBackStage = toStage(xBack);
                const Settings::Rect xBackContent{ xBackStage.x + 17, xBackStage.y, xBackStage.x + 17 + 255, xBackStage.y + 20.4 };
                const auto rescaled = BlockRules::PlacePanel(xBack, xBackStage, inScaled->want, xBackContent, target, toParent, kTol);
                Expect(rescaled && rescaled->rePlaced && rescaled->set && Near(rescaled->want.x, inScaled->want.x) && Near(rescaled->want.y, inScaled->want.y),
                    "strip in a 0.85 parent, SkyUI set _x back: re-placed, set to the same origin again (" + at(rescaled) + ")");
            }
            const auto failed = BlockRules::PlacePanel(Point{ 100, 700 }, Point{ 100, 700 }, std::nullopt, content, target,
                [](const Point&) { return std::optional<Point>{}; }, kTol);
            Expect(!failed, "strip: the mapping into the parent fails - no place (nothing set)");
            // the visible content: a hidden (wider) button left out; none shown - none (the widget then does not move the panel)
            const auto united = BlockRules::VisibleUnion({ { Settings::Rect{ 0, 0, 100, 24 }, true }, { Settings::Rect{ -200, -10, 900, 40 }, false },
                { Settings::Rect{ 110, 0, 220, 24 }, true } });
            Expect(united && Near(united->left, 0) && Near(united->right, 220) && Near(united->top, 0) && Near(united->bottom, 24),
                Text("VisibleUnion: the hidden child left out (%s)", united ? Show(*united).c_str() : "none"));
            Expect(!BlockRules::VisibleUnion({ { Settings::Rect{ 0, 0, 100, 24 }, false } }) && !BlockRules::VisibleUnion({}),
                "VisibleUnion: nothing shown (hideButtons / clearButtons) - none");
            // GPT on 4846c6a5: a shown button whose bounds did not read - no part-union, and not to be cached; nothing shown is
            const std::vector<BlockRules::ChildBox> unread{ { Settings::Rect{ 0, 0, 100, 24 }, true, true }, { Settings::Rect{}, true, false } };
            const std::vector<BlockRules::ChildBox> noneShown{ { Settings::Rect{ 0, 0, 100, 24 }, false, true }, { Settings::Rect{}, false, false } };
            Expect(!BlockRules::VisibleUnion(unread) && !BlockRules::AllShownRead(unread), "a shown button's bounds failed to read: no union, not cacheable");
            Expect(!BlockRules::VisibleUnion(noneShown) && BlockRules::AllShownRead(noneShown) && BlockRules::AllShownRead({}),
                "nothing shown (a hidden child unread too): no union, cacheable");
        }
        // the board (user 2026-10-30): the columns' areas 648..1238 x 67..623, 16 larger; empty looks; the strip from its bottom
        {
            const Settings::Rect columns{ 648, 67, 1238, 623 };
            const auto board = BlockRules::BoardRect(columns, BlockRules::kBoardPad, visible1610);
            Expect(Near(board.left, 632) && Near(board.top, 51) && Near(board.right, 1254) && Near(board.bottom, 639), Text("board: the columns + 16 (%s)", Show(board).c_str()));
            const auto cut = BlockRules::BoardRect(Settings::Rect{ 0, -40, 1275, 755 }, BlockRules::kBoardPad, visible1610);
            Expect(Near(cut.left, 0) && Near(cut.top, -40) && Near(cut.right, 1280) && Near(cut.bottom, 760), Text("board cut to the visible stage (%s)", Show(cut).c_str()));
            using BlockRules::EmptyLook;
            Expect(BlockRules::EmptyLookOf(Layout::kPreview, true, false) == EmptyLook::kLineCentred &&
                       BlockRules::EmptyLookOf(Layout::kSpellCard, true, false) == EmptyLook::kLineOnly &&
                       BlockRules::EmptyLookOf(Layout::kTree, true, false) == EmptyLook::kAsUsual,
                "nothing chosen: the preview keeps its size with a line, our card one line, the retired tree id as usual (HoM add-on Task 5)");
            Expect(BlockRules::EmptyLookOf("SpellResearch.archetypes", true, true, true) == EmptyLook::kAsUsual,
                "in the layout mode an add-on drawn per selection shows (review R2: it can be moved)");
            Expect(BlockRules::EmptyLookOf("SpellResearch.archetypes", true, true) == EmptyLook::kClosed &&
                       BlockRules::EmptyLookOf("Old.v1block", true, false) == EmptyLook::kAsUsual &&
                       BlockRules::EmptyLookOf(Layout::kTree, false, false) == EmptyLook::kAsUsual &&
                       BlockRules::EmptyLookOf("SpellResearch.archetypes", false, true) == EmptyLook::kAsUsual,
                "an add-on drawn per selection closes up with nothing shown; a v1 block as usual; anything chosen: as usual");
            const auto hang = BlockRules::HintRowRects(648, board.bottom, 400, 24, 18, 8, 4, visible1610);
            Expect(Near(hang.strip.top, 647) && Near(hang.strip.left, 648) && Near(hang.gear.top, 675) && Near(hang.gear.left, 648),
                Text("strip 8 under the board's bottom (%s), the gear one row below at its start (%s)", Show(hang.strip).c_str(), Show(hang.gear).c_str()));
        }
        // the gear's shape (user's choice "B"): 6 teeth with flat tops, every point within the radius, both outlines closed
        {
            const double cx = 9.0, cy = 9.0, r = 9.0, eps = 1e-9;
            const auto shape = BlockRules::GearOutline(cx, cy, r);
            const auto radius = [&](const BlockRules::Point& a_p) { return std::hypot(a_p.x - cx, a_p.y - cy); };
            int tips = 0;
            bool within = true;
            for (const auto& p : shape.rim) {
                within = within && radius(p) <= r + eps;
                tips += std::abs(radius(p) - r) <= eps ? 1 : 0;
            }
            const std::size_t perTooth = 4 + (BlockRules::kGearValleySteps - 1);
            Expect(shape.rim.size() == BlockRules::kGearTeeth * perTooth + 1 && tips == 2 * BlockRules::kGearTeeth,
                Text("gear rim: %d teeth, %zu points (%zu each + the closing one), %d tooth-top corners on the radius", BlockRules::kGearTeeth,
                    shape.rim.size(), perTooth, tips));
            Expect(within, "every rim point within the outer radius");
            const auto closed = [](const std::vector<BlockRules::Point>& a_line) {
                return a_line.size() > 2 && a_line.front().x == a_line.back().x && a_line.front().y == a_line.back().y;
            };
            Expect(closed(shape.rim) && closed(shape.hole), "both outlines end on their first point (no start tick)");
            bool round = shape.hole.size() == static_cast<std::size_t>(BlockRules::kGearHoleSegments) + 1;
            for (const auto& p : shape.hole) round = round && std::abs(radius(p) - r * BlockRules::kGearHoleShare) <= 1e-9;
            Expect(round, Text("the hole: %d segments at %.2f of the radius", BlockRules::kGearHoleSegments, BlockRules::kGearHoleShare));
            bool rootOk = true;
            for (const auto& p : shape.rim) rootOk = rootOk && (std::abs(radius(p) - r) <= eps || std::abs(radius(p) - r * BlockRules::kGearRootShare) <= 1e-9);
            Expect(rootOk, "every other rim point on the root circle (flat valleys, flat tops)");
        }
        // the tree tooltip's top (review round 2): panel coordinates, title 22, tooltip 60 high, full-height limit 180
        Expect(Near(BlockRules::TooltipY(50, 60, 22, 180, 0, 0, 300), 50), "tooltip inside everything: kept where placed");
        Expect(Near(BlockRules::TooltipY(200, 60, 22, 180, 120, 0, 300), 180), "below the tree's full height: clamped to it");
        Expect(Near(BlockRules::TooltipY(90, 60, 22, 180, 30, 0, 120), 30), "past the column's visible bottom (120): above its node (30)");
        Expect(Near(BlockRules::TooltipY(90, 60, 22, 180, 10, 0, 120), 60), "above the node would hit the title: shifted up to end at the visible bottom");
        Expect(Near(BlockRules::TooltipY(40, 60, 22, 180, -30, 35, 80), 35), "a visible part shorter than the tooltip: starts at its top");
        // less jumping (user 2026-10-30): a grow now, a shrink after it held kShrinkDelayMs (BlockRules::DecideFit)
        {
            using BlockRules::FitStep;
            std::optional<BlockRules::HeldShrink> held;
            const double delay = BlockRules::kShrinkDelayMs;
            Expect(BlockRules::DecideFit(100, 140, 0, held) == FitStep::kApply && !held, "a grow applies at once");
            Expect(BlockRules::DecideFit(140, 100, 1000, held) == FitStep::kHold && held && Near(held->sinceMs, 1000), "a shrink is held, its time starts");
            Expect(BlockRules::DecideFit(140, 100, 1000 + delay - 1, held) == FitStep::kHold, "still held 1 ms before the delay");
            Expect(BlockRules::DecideFit(140, 100, 1000 + delay, held) == FitStep::kApply && !held, "applied once the same height held for the delay");
            BlockRules::DecideFit(140, 100, 2000, held);
            Expect(BlockRules::DecideFit(140, 90, 2000 + delay, held) == FitStep::kHold && held && Near(held->sinceMs, 2000 + delay),
                "another shrink height restarts the wait (a new row's content)");
            Expect(BlockRules::DecideFit(140, 160, 2000 + delay + 10, held) == FitStep::kApply && !held, "a grow while a shrink waits cancels it");
            BlockRules::DecideFit(140, 100, 3000, held);
            Expect(BlockRules::DecideFit(140, 140, 3010, held) == FitStep::kUnchanged && !held, "back to the present height: the wait is dropped");
            Expect(BlockRules::DecideFit(140, 100, 4000, held, 0.0) == FitStep::kApply, "a delay of 0 shrinks at once");
            // GPT review of 4b83efd3: a shrink waiting, then a new height asked for in the layout mode, then the mode's end
            BlockRules::FitState state{ 140.0, std::nullopt };
            Expect(!BlockRules::AskHeight(state, 100, 0, false) && state.held && Near(*state.current, 140), "AskHeight: a shrink waits (still 140)");
            Expect(BlockRules::AskHeight(state, 120, 50, true) && Near(*state.current, 120) && !state.held,
                "a new height in the layout mode applies and drops the waiting shrink");
            Expect(!BlockRules::DueShrink(state, 1000, false) && Near(*state.current, 120), "after the mode's end nothing old comes back: the newest height (120) wins");
            BlockRules::FitState waiting{ 140.0, std::nullopt };
            BlockRules::AskHeight(waiting, 100, 0, false);
            Expect(!BlockRules::DueShrink(waiting, 100, false) && BlockRules::DueShrink(waiting, delay, false) == 100.0 && !BlockRules::DueShrink(waiting, delay, true),
                "DueShrink: not before the delay, the height once due, nothing in the layout mode");
            BlockRules::FitState fresh;
            Expect(BlockRules::AskHeight(fresh, 80, 0, false) && Near(*fresh.current, 80), "the menu's first fit applies at once");
            Expect(BlockRules::AskHeight(waiting, 160, 10, false) && !waiting.held, "a grow applies and drops the waiting shrink");
        }
        Expect(Near(BlockRules::GearHintX(Settings::Rect{ 434, 680, 452, 698 }, 80, 4, visible1610), 22), "the gear's hint right of it when it fits");
        Expect(Near(BlockRules::GearHintX(Settings::Rect{ 1250, 680, 1268, 698 }, 80, 4, visible1610), -84), "at the stage's right edge: left of it");
        Expect(Near(BlockRules::GearHintX(Settings::Rect{ 648, 675, 666, 693 }, 80, 4, visible1610, true), -84),
            "the edit mode: the hint on the gear's left, clear of the reset button (review I10)");
        Expect(Near(BlockRules::GearHintX(Settings::Rect{ 10, 675, 28, 693 }, 80, 4, visible1610, true), 22), "the edit mode with no room on the left: right of it");
        // corner bounds (controller finding 2026-10-30): the logged scrollbar 432,79..452,667 through Inventory Reforged's window
        // (scale 0.85, moved 200, 26) lands at 567.2,93.2..584.2,593 - where the screenshot shows it
        {
            const auto through = [](double a_x, double a_y) { return BlockRules::Point{ 0.85 * a_x + 200.0, 0.85 * a_y + 26.0 }; };
            const auto box = BlockRules::CornerBounds({ through(432, 79), through(452, 79), through(452, 667), through(432, 667) });
            Expect(box && Near(box->left, 567.2) && Near(box->top, 93.15) && Near(box->right, 584.2) && Near(box->bottom, 592.95),
                Text("corners through scale 0.85 and (200, 26): %s", box ? Show(*box).c_str() : "none"));
            const double c = std::cos(std::numbers::pi / 2), s = std::sin(std::numbers::pi / 2);
            const auto turn = [&](double a_x, double a_y) { return BlockRules::Point{ c * a_x - s * a_y, s * a_x + c * a_y }; };
            const auto turned = BlockRules::CornerBounds({ turn(0, 0), turn(10, 0), turn(10, 4), turn(0, 4) });
            Expect(turned && Near(turned->Width(), 4) && Near(turned->Height(), 10), "a quarter turn: the box round the turned corners (4 x 10)");
            const double nan = std::numeric_limits<double>::quiet_NaN();
            Expect(!BlockRules::CornerBounds({ BlockRules::Point{ nan, 0 }, BlockRules::Point{ 1, 0 }, BlockRules::Point{ 1, 1 }, BlockRules::Point{ 0, 1 } }) &&
                       !BlockRules::CornerBounds({ BlockRules::Point{ 5, 5 }, BlockRules::Point{ 5, 5 }, BlockRules::Point{ 5, 5 }, BlockRules::Point{ 5, 5 } }),
                "a corner that is not a number, or an empty box: none");
        }
    }
}

int main()
{
    FitLayout();
    DefaultLayout();
    Border();
    HiddenAndFill();
    CardBoxInColumn();
    Scroll();
    ColumnsAndVisibleBox();
    SnapAndMove();
    SnapAroundHiddenAndAbsent();
    Json();
    MoveKeepsEveryBlock();
    CancelRestoresEntry();
    FileSafety();
    ModeOrdering();
    SlotLineAndScrollToShow();
    PadFocus();
    AddonRegistry();
    AddonLayout();
    LegacyIds();
    CoreOnly();
    AddonHeightChange();
    std::printf("layout-check: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? kExitOk : kExitFail;
}
