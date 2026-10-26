// magic-menu-layout-check - offline check of the magic menu prototype's block layout
// (plugins/SL_MagicMenu/src/Layout.cpp, the same code the widget arranges its panels with):
// the default layout against the ini rects, the column border and its limits, hidden and fill
// blocks per tab (plan D6), the drop slot and the move with hidden / absent ids (plan D3),
// scroll clamping and the layout JSON (version, repair of a bad file).
//
//   magic-menu-layout-check     one PASS/FAIL line per check, a total line
//
// No game types. Exit 0 when every check passes, 1 otherwise.

#include "Layout.h"
#include "Settings.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
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
    const auto kHeights = Layout::DefaultHeights(kValues);
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
        const std::map<BlockId, Rect> ini{ { Layout::kPreview, kValues.preview }, { Layout::kTree, kValues.tree },
            { Layout::kProgress, kValues.progress }, { Layout::kSpellCard, kValues.itemCard }, { Layout::kNodeCard, kValues.nodeCard } };
        bool same = true;
        for (const auto& [id, rect] : ini) {
            const auto it = rects.find(id);
            same = same && it != rects.end() && Near(it->second.left, rect.left) && Near(it->second.top, rect.top) &&
                   Near(it->second.right, rect.right) && Near(it->second.bottom, rect.bottom);
        }
        Expect(same, "default arrange = the five Settings::Values rects");
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

        // Active Effects: only the card: the whole column, 556
        const auto effects = Layout::Arrange(
            state, kHeights, { Layout::kPreview, Layout::kTree, Layout::kProgress, Layout::kNodeCard }, fill, kFrames);
        ExpectRect(effects, Layout::kSpellCard, 648, 67, 969, 623, "Active Effects (fill)");

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
}

int main()
{
    DefaultLayout();
    Border();
    HiddenAndFill();
    Scroll();
    SnapAndMove();
    SnapAroundHiddenAndAbsent();
    Json();
    std::printf("layout-check: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? kExitOk : kExitFail;
}
