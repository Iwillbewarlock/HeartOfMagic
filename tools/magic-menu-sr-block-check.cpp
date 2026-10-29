// magic-menu-sr-block-check - offline check of the Spell Research add-on block's pure parts (Spell Research add-on design
// 2026-10-27, "Changes / 2"): plugins/SL_MagicMenu_SpellResearch/src/SrBlockRules.h as the plugin runs it - which archetypes
// show and in what order, the reason when none do, the list and line layouts and their wrapping to a width, an unknown
// archetype's frame-only icon, the icon data's bounds, the link click rule (press and release in the same link), the reply
// code -> notification, the open request's requestId matching and 5 s drop - and SrBlockText.h: ResolveLayout's and
// ResolveLanguage's reading of the ini text (a BOM too), the game language mapping, the archetype short / name fallback,
// and the shipped lang/en.json against the keys the block uses.
//
//   magic-menu-sr-block-check                     one PASS/FAIL line per check, a total line
//   magic-menu-sr-block-check --selftest          the same checks, a "selftest: ..." line
//   magic-menu-sr-block-check --lang <en.json>    another language file instead of the shipped en.json
//
// No game types. Exit 0 when every check passes, 1 when one fails, 2 on a bad argument.

#include "SrBlockRules.h"
#include "SrBlockText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef SR_BLOCK_EN_JSON
#    error "SR_BLOCK_EN_JSON (the shipped lang/en.json) must be defined by CMake"
#endif

namespace
{
    using namespace SrBlock;

    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;
    constexpr double kCharW = 6.0;     // the made-up text width of one character
    constexpr double kEpsilon = 1e-9;  // layout sums compared with this slack
    // archetypes used below: SR's index order (SR Prisma ARCHETYPES)
    constexpr int kFire = 12, kFrost = 15, kShields = 26, kShadow = 25, kLight = 19, kDaedra = 9, kSouls = 28, kActor = 41,
                  kArea = 42, kLocation = 43, kSelf = 44, kSensing = 54, kSummoning = 57, kCastFireForget = 1, kDestruction = 38;

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    Knowledge NoKnowledge()
    {
        Knowledge k{};
        return k;
    }

    SpellEntry Entry(int a_tier, std::initializer_list<int> a_archetypes)
    {
        SpellEntry entry;
        entry.tier = a_tier;
        for (const int a : a_archetypes) entry.archetypes.set(static_cast<std::size_t>(a));
        return entry;
    }

    std::vector<int> Archetypes(const std::vector<Item>& a_items)
    {
        std::vector<int> out;
        for (const auto& item : a_items) out.push_back(item.archetype);
        return out;
    }

    // ---------------------------------------------------------------------
    void Grouping()
    {
        std::printf("grouping and order\n");
        Expect(GroupOf(0) == Group::kNone && GroupOf(1) == Group::kNone, "0-1 (casting) are no shown group");
        Expect(GroupOf(2) == Group::kElement && GroupOf(35) == Group::kElement, "2 and 35 are elements");
        Expect(GroupOf(36) == Group::kNone && GroupOf(40) == Group::kNone, "36-40 (schools) are no shown group");
        Expect(GroupOf(41) == Group::kTargeting && GroupOf(44) == Group::kTargeting, "41 and 44 are targeting");
        Expect(GroupOf(45) == Group::kTechnique && GroupOf(59) == Group::kTechnique, "45 and 59 are techniques");
        Expect(GroupOf(-1) == Group::kNone && GroupOf(60) == Group::kNone, "out of range: none");

        auto k = NoKnowledge();
        k[kDaedra][3] = 1.0f;
        k[kLocation][3] = 0.5f;
        k[kSouls][3] = 2.0f;
        k[kSummoning][2] = 9.0f;  // known at another tier only
        const auto conjure = Entry(3, { kSummoning, kLocation, kSouls, kDaedra, kCastFireForget, 37 });
        const auto items = Items(conjure, k);
        Expect(Archetypes(items) == std::vector<int>{ kDaedra, kSouls, kLocation, kSummoning },
            "Conjure Dremora Lord: Daedra, Souls, Location, Summoning in SR's order; casting and school left out");
        Expect(items.size() == 4 && items[0].known && items[1].known && items[2].known && !items[3].known,
            "known = a global > 0 at the spell's tier (Summoning known only at another tier: '?')");
        const auto noTier = Items(Entry(-1, { kFire, kArea }), k);
        Expect(noTier.size() == 2 && !noTier[0].known && !noTier[1].known, "a spell in no tier list: every archetype '?'");
        auto all = NoKnowledge();
        for (auto& row : all) row.fill(1.0f);
        Expect(Items(Entry(4, { 0, 1, 36, 37, 38, 39, 40 }), all).empty(), "only casting and school archetypes: nothing shown");
    }

    void Decisions()
    {
        std::printf("what the block shows\n");
        auto k = NoKnowledge();
        k[kFire][2] = 1.0f;
        k[kArea][2] = 1.0f;
        const auto fireball = Entry(2, { kFire, kActor, kArea, kDestruction });
        View view{ .shown = 0x1C789, .haveSnapshot = true, .ready = true, .listsEmpty = false, .isSpell = true, .entry = &fireball, .knowledge = &k };

        auto expect = [](const View& a_view, Show a_show, std::string_view a_reason, const char* a_what) {
            const auto d = Decide(a_view);
            Expect(d.show == a_show && a_reason == d.reasonKey, Text("%s: show %d reason '%s'", a_what, static_cast<int>(d.show), d.reasonKey));
        };
        auto none = view;
        none.shown = 0;
        expect(none, Show::kNothing, "", "nothing on the card");
        auto noSnapshot = view;
        noSnapshot.haveSnapshot = false;
        expect(noSnapshot, Show::kReason, "reason.notReady", "snapshot not built yet");
        auto importing = view;
        importing.ready = false;
        expect(importing, Show::kReason, "reason.notReady", "SR importing or not set up");
        auto power = view;
        power.isSpell = false;
        expect(power, Show::kReason, "reason.notSpell", "a power on the card");
        auto empty = view;
        empty.listsEmpty = true;
        expect(empty, Show::kReason, "reason.listsEmpty", "SR's lists empty (Import never run)");
        auto unknown = view;
        unknown.entry = nullptr;
        expect(unknown, Show::kReason, "reason.unknownSpell", "SR does not file the spell");
        const auto schoolOnly = Entry(1, { kDestruction, kCastFireForget });
        auto noArchetypes = view;
        noArchetypes.entry = &schoolOnly;
        expect(noArchetypes, Show::kReason, "reason.noArchetypes", "only school and casting filed");
        expect(view, Show::kItems, "reason.unknownArchetype", "Fireball, Actor not known: items and the '?' reason");
        k[kActor][2] = 1.0f;
        expect(view, Show::kItems, "", "all known: items, no reason line");
        const auto noTier = Entry(-1, { kFire });
        auto tierless = view;
        tierless.entry = &noTier;
        expect(tierless, Show::kItems, "reason.noTier", "in no tier list: items as '?', the tier reason");
    }

    // ---------------------------------------------------------------------
    Metrics MadeUpMetrics(double a_width)
    {
        Metrics m;
        m.width = a_width;
        m.separatorW = 3.0;
        m.ellipsisW = 3 * kCharW;
        return m;
    }

    std::vector<Measured> Measure(const std::vector<Item>& a_items, const std::vector<int>& a_nameChars)
    {
        std::vector<Measured> out;
        for (std::size_t i = 0; i < a_items.size(); ++i) {
            out.push_back({ a_items[i], a_items[i].known ? a_nameChars[i] * kCharW : kCharW });
        }
        return out;
    }

    const std::array<double, kShownGroups.size()> kLabels{ 7 * kCharW, 9 * kCharW, 9 * kCharW };  // Element, Targeting, Technique

    // Every placement must hold these, whatever the input
    void Invariants(const Placement& a_p, const Metrics& a_m, std::size_t a_items, const char* a_what)
    {
        bool inside = true, linesOk = a_p.lines <= a_m.maxLines;
        int icons = 0, texts = 0, ellipses = 0;
        std::set<int> placed;
        for (const auto& part : a_p.parts) {
            if (part.line < 0 || part.line >= a_m.maxLines) linesOk = false;
            if (part.x < -kEpsilon || part.x + part.w > a_m.width + kEpsilon) inside = false;
            if (part.kind == Part::Kind::kIcon) {
                ++icons;
                placed.insert(part.index);
            }
            if (part.kind == Part::Kind::kText) ++texts;
            if (part.kind == Part::Kind::kEllipsis) ++ellipses;
        }
        Expect(inside, Text("%s: every part inside 0..%.0f", a_what, a_m.width));
        Expect(linesOk, Text("%s: %d line(s), no more than %d", a_what, a_p.lines, a_m.maxLines));
        Expect(a_p.shown + a_p.hidden == static_cast<int>(a_items) && icons == a_p.shown && texts == a_p.shown &&
                   placed.size() == static_cast<std::size_t>(a_p.shown),
            Text("%s: shown %d + hidden %d = %zu, one icon and one text each", a_what, a_p.shown, a_p.hidden, a_items));
        Expect((a_p.hidden > 0) == (ellipses == 1) && ellipses <= 1, Text("%s: an ellipsis exactly when some are left out", a_what));
    }

    // Two parts of one line overlap (each part's [x, x + w); the icon box counts as its width)
    bool Overlaps(const Placement& a_p)
    {
        for (int line = 0; line < a_p.lines; ++line) {
            std::vector<const Part*> parts;
            for (const auto& part : a_p.parts) {
                if (part.line == line) parts.push_back(&part);
            }
            std::ranges::sort(parts, {}, &Part::x);
            for (std::size_t i = 1; i < parts.size(); ++i) {
                if (parts[i]->x < parts[i - 1]->x + parts[i - 1]->w - kEpsilon) return true;
            }
        }
        return false;
    }

    // Every separator sits between one archetype's text and the next archetype's icon on its line; in list, of one group
    bool SeparatorsBetweenItems(const Placement& a_p, LayoutKind a_kind)
    {
        const auto& parts = a_p.parts;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].kind != Part::Kind::kSeparator) continue;
            if (i == 0 || i + 1 >= parts.size()) return false;
            const auto& before = parts[i - 1];
            const auto& after = parts[i + 1];
            if (before.kind != Part::Kind::kText || after.kind != Part::Kind::kIcon || before.line != parts[i].line || after.line != parts[i].line) return false;
            if (before.x + before.w > parts[i].x + kEpsilon || parts[i].x + parts[i].w > after.x + kEpsilon) return false;
            if (a_kind == LayoutKind::kList && GroupOf(before.index) != GroupOf(after.index)) return false;
        }
        return true;
    }

    const Part* Find(const Placement& a_p, Part::Kind a_kind, int a_index)
    {
        for (const auto& part : a_p.parts) {
            if (part.kind == a_kind && part.index == a_index) return &part;
        }
        return nullptr;
    }

    void Layouts()
    {
        std::printf("list and line layouts\n");
        constexpr double kColumn = 244.0;  // the right column's 260 less the padding
        const auto m = MadeUpMetrics(kColumn);

        // Fireball: Fire / ? . Area
        const std::vector<Item> fireball{ { kFire, true }, { kActor, false }, { kArea, true } };
        const auto fb = Measure(fireball, { 4, 0, 4 });
        const auto list = Place(LayoutKind::kList, m, fb, kLabels);
        Invariants(list, m, fb.size(), "list Fireball");
        const auto* fire = Find(list, Part::Kind::kIcon, kFire);
        const auto* actor = Find(list, Part::Kind::kIcon, kActor);
        const auto* area = Find(list, Part::Kind::kIcon, kArea);
        Expect(list.lines == 2 && fire && actor && area && fire->line == 0 && actor->line == 1 && area->line == 1,
            "list Fireball: Element on line 0, Targeting (? and Area) on line 1");
        Expect(std::abs(list.labelColumn - (9 * kCharW + m.labelGap)) < kEpsilon && fire && std::abs(fire->x - list.labelColumn) < kEpsilon,
            "list: the archetypes start after the widest used label plus the gap");
        int labels = 0, separators = 0;
        for (const auto& part : list.parts) {
            labels += part.kind == Part::Kind::kLabel;
            separators += part.kind == Part::Kind::kSeparator;
        }
        Expect(labels == 2 && separators == 1, "list Fireball: two labels (no Technique row), one separator (between ? and Area)");
        Expect(actor && area && area->x > actor->x + m.iconSize, "list: Area after '?' on its line");

        const auto line = Place(LayoutKind::kLine, m, fb, kLabels);
        Invariants(line, m, fb.size(), "line Fireball");
        int lineSeparators = 0;
        for (const auto& part : line.parts) lineSeparators += part.kind == Part::Kind::kSeparator;
        Expect(line.lines == 1 && lineSeparators == 2 && line.labelColumn == 0.0, "line Fireball: one line, three archetypes, two separators, no labels");

        // Ice Storm + Ward (the mock's long case): Frost Shields ? ? / Self Area / Sensing
        const std::vector<Item> many{ { kFrost, true }, { kLight, false }, { kShadow, false }, { kShields, true }, { kSelf, true }, { kArea, true },
            { kSensing, true } };
        const auto mm = Measure(many, { 5, 0, 0, 7, 4, 4, 7 });
        const auto manyLine = Place(LayoutKind::kLine, m, mm, kLabels);
        Invariants(manyLine, m, mm.size(), "line, seven archetypes at 244");
        Expect(manyLine.lines == 2 && manyLine.hidden == 0, Text("line, seven archetypes: wraps to 2 lines, none left out (%d lines)", manyLine.lines));
        bool noLeadingSeparator = true;
        for (std::size_t i = 0; i < manyLine.parts.size(); ++i) {
            if (manyLine.parts[i].kind == Part::Kind::kSeparator && (i + 1 >= manyLine.parts.size() || manyLine.parts[i + 1].line != manyLine.parts[i].line)) {
                noLeadingSeparator = false;
            }
            if (manyLine.parts[i].kind == Part::Kind::kSeparator && i > 0 && manyLine.parts[i - 1].line != manyLine.parts[i].line) noLeadingSeparator = false;
        }
        Expect(noLeadingSeparator, "line: no separator starts or ends a line");

        // A narrow column: wraps to three lines and leaves the rest out with an ellipsis
        const auto narrow = MadeUpMetrics(100.0);
        const auto narrowLine = Place(LayoutKind::kLine, narrow, mm, kLabels);
        Invariants(narrowLine, narrow, mm.size(), "line, seven archetypes at 100");
        Expect(narrowLine.lines == kArchetypeLines && narrowLine.hidden > 0, Text("line at 100: 3 lines, %d left out", narrowLine.hidden));
        const auto narrowList = Place(LayoutKind::kList, narrow, mm, kLabels);
        Invariants(narrowList, narrow, mm.size(), "list, seven archetypes at 100");
        Expect(narrowList.lines == kArchetypeLines && narrowList.hidden > 0, Text("list at 100: 3 lines, %d left out", narrowList.hidden));

        // A name longer than the whole line: cut to the room
        const std::vector<Item> longName{ { kFire, true } };
        const auto ln = Measure(longName, { 80 });
        const auto cut = Place(LayoutKind::kLine, m, ln, kLabels);
        Invariants(cut, m, ln.size(), "line, one 480-wide name");
        const auto* text = Find(cut, Part::Kind::kText, kFire);
        Expect(text && text->w < 80 * kCharW && std::abs(text->x + text->w - kColumn) < kEpsilon, "a name longer than the line is cut to the room left");

        // One line only: the second group has no line, an ellipsis ends line 0
        auto oneLine = m;
        oneLine.maxLines = 1;
        const auto single = Place(LayoutKind::kList, oneLine, fb, kLabels);
        Invariants(single, oneLine, fb.size(), "list with one line");
        Expect(single.shown == 1 && single.hidden == 2, "list with one line: Element shown, Targeting left out");

        // Many made-up inputs (fixed seed, LCG): the invariants hold for both layouts at several widths
        std::uint32_t seed = 20261029u;
        auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        int runs = 0, broken = 0, overlapping = 0, badSeparators = 0;
        for (int run = 0; run < 400; ++run) {
            std::vector<Item> items;
            for (int a = 0; a < static_cast<int>(kArchetypeCount); ++a) {
                if (GroupOf(a) != Group::kNone && next() % 9 == 0) items.push_back({ a, next() % 3 != 0 });
            }
            std::vector<int> chars;
            for (std::size_t i = 0; i < items.size(); ++i) chars.push_back(2 + static_cast<int>(next() % 18));
            const auto measured = Measure(items, chars);
            for (const double width : { 120.0, 180.0, 244.0, 400.0 }) {
                for (const auto kind : { LayoutKind::kList, LayoutKind::kLine }) {
                    const auto metrics = MadeUpMetrics(width);
                    const auto p = Place(kind, metrics, measured, kLabels);
                    ++runs;
                    bool ok = p.lines <= metrics.maxLines && p.shown + p.hidden == static_cast<int>(items.size());
                    for (const auto& part : p.parts) ok = ok && part.x >= -kEpsilon && part.x + part.w <= width + kEpsilon && part.line < metrics.maxLines;
                    if (!ok) ++broken;
                    if (Overlaps(p)) ++overlapping;
                    if (!SeparatorsBetweenItems(p, kind)) ++badSeparators;
                }
            }
        }
        Expect(broken == 0, Text("%d made-up placements (seed 20261029): inside the width and the lines, every archetype counted (%d broken)", runs, broken));
        Expect(overlapping == 0, Text("%d made-up placements: no two parts of a line overlap (%d overlapping)", runs, overlapping));
        Expect(badSeparators == 0,
            Text("%d made-up placements: every separator between an archetype's text and the next one's icon, in list one group's (%d not)", runs, badSeparators));
    }

    void Height()
    {
        std::printf("fixed height\n");
        Expect(kContentH == kRowH * kArchetypeLines + kLinksGap + kLinkRowH + kReasonH, Text("content %.0f = 3 lines + gap + links + reason", kContentH));
        Expect(kBlockHeight == kContentH + kHostTitleH + 2 * kHostPad, Text("block %.0f = content + the host's title strip 22 + padding 2 x 8", kBlockHeight));
        Expect(kReasonTop + kReasonH <= kContentH && kLinksTop >= kRowH * kArchetypeLines, "links under the archetypes, reason line under the links");
    }

    // ---------------------------------------------------------------------
    void Icons()
    {
        std::printf("icons\n");
        using namespace ArchetypeIcons;
        bool inGrid = true, rangesOk = true, presence = true, framesOk = true, unknownFrame = true;
        for (const auto& p : kPoints) {
            if (!(p.x >= 0.0f && p.x <= kIconGrid && p.y >= 0.0f && p.y <= kIconGrid)) inGrid = false;
        }
        for (const auto& sub : kSubpaths) {
            if (sub.count < 2 || static_cast<std::size_t>(sub.first) + sub.count > kPoints.size()) rangesOk = false;
            if (sub.fill && !sub.closed) rangesOk = false;
        }
        std::array<std::vector<std::pair<std::uint16_t, std::uint16_t>>, 4> groupFrame{};
        std::array<bool, 4> groupSeen{};
        for (int a = 0; a < static_cast<int>(kIconCount); ++a) {
            const auto& icon = kIcons[static_cast<std::size_t>(a)];
            const bool shownGroup = GroupOf(a) != Group::kNone;
            if (shownGroup != (icon.count > 0)) presence = false;
            if (static_cast<std::size_t>(icon.first) + icon.count > kSubpaths.size() || icon.frame > icon.count) rangesOk = false;
            if (!shownGroup) {
                if (icon.frame != 0 || IconSubpaths(a, false).count != 0 || IconSubpaths(a, true).count != 0) presence = false;
                continue;
            }
            if (icon.frame == 0 || icon.frame >= icon.count) framesOk = false;  // a frame and a glyph after it
            const auto unknown = IconSubpaths(a, false), known = IconSubpaths(a, true);
            if (unknown.first != icon.first || unknown.count != icon.frame || known.count != icon.count) unknownFrame = false;
            std::vector<std::pair<std::uint16_t, std::uint16_t>> frame;
            for (std::size_t k = 0; k < icon.frame; ++k) {
                const auto& sub = kSubpaths[icon.first + k];
                frame.emplace_back(sub.first, sub.count);
            }
            const auto g = static_cast<std::size_t>(GroupOf(a));
            if (!groupSeen[g]) {
                groupSeen[g] = true;
                groupFrame[g] = frame;
            } else if (groupFrame[g] != frame) {
                framesOk = false;
            }
        }
        Expect(inGrid, Text("every one of the %zu points inside 0..24", kPoints.size()));
        Expect(rangesOk, "every subpath inside kPoints with 2+ points (fills closed), every icon inside kSubpaths, frame <= count");
        Expect(presence, "icons for 2-35 and 41-59, none for casting (0-1) and schools (36-40)");
        Expect(framesOk, "each shown icon has a frame then a glyph, and every icon of a group the same frame subpaths");
        Expect(unknownFrame, "an unknown archetype draws its frame subpaths only, a known one all");
        Expect(kIcons[kFire].frame == 1 && kIcons[kArea].frame == 5 && kIcons[kSensing].frame == 1,
            "frames: element ring 1 subpath, targeting ring with ticks 5, technique square 1");
        Expect(IconSubpaths(-1, true).count == 0 && IconSubpaths(60, true).count == 0, "out-of-range archetype: nothing drawn");
    }

    // ---------------------------------------------------------------------
    void Clicks()
    {
        std::printf("links and clicks\n");
        const auto rects = PlaceLinks({ 42.0, 44.0, 60.0 }, kLinksTop, 244.0);
        Expect(rects.size() == 3 && rects[0].left == 0.0 && rects[1].left == 42.0 + kLinkGap && rects[2].left == rects[1].right + kLinkGap,
            "three links left to right with the gap");
        Expect(rects[0].top == kLinksTop && rects[0].bottom == kLinksTop + kLinkRowH, "a link's rect is its row");
        const double y = kLinksTop + 5;
        Expect(ClickedLink(rects, 10, y, 30, y) == std::optional<std::size_t>(0), "press and release in Journal: Journal");
        Expect(ClickedLink(rects, 60, y, 70, y) == std::optional<std::size_t>(1), "press and release in Alembic: Alembic");
        Expect(!ClickedLink(rects, 10, y, 60, y).has_value(), "pressed in Journal, released in Alembic: nothing");
        Expect(!ClickedLink(rects, 10, y, 10, kLinksTop - 10).has_value(), "released above the row: nothing");
        Expect(!ClickedLink(rects, 45, y, 45, y).has_value(), "in the gap between two links: nothing");
        Expect(!ClickedLink(rects, 10, 5, 10, y).has_value(), "pressed on the archetypes: nothing");
        const auto cut = PlaceLinks({ 100.0, 100.0, 100.0 }, kLinksTop, 150.0);
        Expect(cut.size() == 2 && cut[1].right == 150.0, "links past a narrow width: cut to it, the one that has no room left out");
        Expect(kLinks[0].what == 2 && kLinks[1].what == 3 && kLinks[2].what == 1, "the row: Journal (2), Alembic (3), Experience (1) - SRPrismaAPI::What");
    }

    void Replies()
    {
        std::printf("reply codes\n");
        Expect(NotificationKey(kResultBusy) == std::string_view("notify.busy"), "Busy -> notify.busy");
        Expect(NotificationKey(kResultRefused) == std::string_view("notify.refused"), "Refused -> notify.refused");
        Expect(NotificationKey(kResultBadMessage) == std::string_view("notify.badMessage"), "BadMessage -> notify.badMessage");
        Expect(NotificationKey(kResultAccepted) == nullptr && NotificationKey(kResultOpened) == nullptr, "Accepted and Opened: no notification");
        Expect(NotificationKey(99) == nullptr, "an unknown code: no notification");
    }

    void Requests()
    {
        std::printf("open requests\n");
        PendingRequest pending;
        constexpr std::int64_t t0 = 1'000'000;
        Expect(!pending.Begin(0, t0), "requestId 0 is refused");
        Expect(pending.Begin(7, t0) && pending.Id() == 7, "request 7 starts");
        Expect(!pending.Begin(8, t0 + 100), "one in flight: request 8 waits");
        Expect(!pending.Matches(6, t0 + 200), "a reply for 6 (not pending): ignored");
        Expect(pending.Matches(7, t0 + 200), "a reply for 7 within 5 s: matches");
        Expect(!pending.Matches(7, t0 + PendingRequest::kTimeoutMs), "the same reply at 5 s: dropped");
        using Verdict = PendingRequest::Verdict;
        Expect(pending.Accept(7, t0 + 300) == Verdict::kAccepted && pending.Id() == 0, "accepted in time: cleared");
        Expect(pending.Accept(7, t0 + 400) == Verdict::kNotPending, "a second reply for 7: not pending (a duplicate)");
        Expect(pending.Accept(0, t0 + 400) == Verdict::kNotPending, "a reply with requestId 0: not pending");
        Expect(pending.Begin(9, t0 + 500), "request 9 starts after 7 was answered");
        Expect(pending.Accept(8, t0 + 600) == Verdict::kNotPending && pending.Id() == 9, "a reply for another id while 9 waits: not pending, 9 kept");
        Expect(pending.Accept(9, t0 + 500 + PendingRequest::kTimeoutMs + 1) == Verdict::kTooLate && pending.Id() == 0,
            "a reply after 5 s: too late, and the request cleared");
        Expect(pending.Begin(10, t0 + 600), "request 10 starts");
        Expect(pending.Begin(11, t0 + 600 + PendingRequest::kTimeoutMs), "no reply for 5 s: request 10 dropped, 11 starts");
        pending.Clear(10);
        Expect(pending.Id() == 11, "clearing the dropped 10 leaves 11");
        pending.Clear(11);
        Expect(pending.Id() == 0 && !pending.Busy(t0 + 600 + PendingRequest::kTimeoutMs), "not delivered: cleared, nothing in flight");
    }

    void IniLayout()
    {
        std::printf("ResolveLayout's ini reading\n");
        auto expect = [](std::string_view a_text, LayoutKind a_kind, bool a_found, bool a_known, const char* a_what) {
            const auto read = ParseLayout(a_text);
            Expect(read.kind == a_kind && read.found == a_found && read.known == a_known,
                Text("%s: %s (found %d, known %d, value '%s')", a_what, read.kind == LayoutKind::kList ? "list" : "line", read.found, read.known,
                    read.value.c_str()));
        };
        expect("", LayoutKind::kList, false, false, "no file text");
        expect("[General]\nToggleKey = Y\n", LayoutKind::kList, false, false, "no [MagicMenuBlock]");
        expect("[MagicMenuBlock]\nsLayout=line\n", LayoutKind::kLine, true, true, "sLayout=line");
        expect("[MagicMenuBlock]\r\nsLayout = list\r\n", LayoutKind::kList, true, true, "sLayout = list, CRLF");
        expect("[magicmenublock]\n  SLAYOUT =  LINE   ; the C layout\n", LayoutKind::kLine, true, true, "case-insensitive, spaces, comment");
        expect("[MagicMenuBlock]\nsLayout=line # hash comment\n", LayoutKind::kLine, true, true, "hash comment");
        expect("[MagicMenuBlock]\nsLayout=grid\n", LayoutKind::kList, true, false, "unknown value: list");
        expect("[MagicMenuBlock]\nsLayout=\n", LayoutKind::kList, true, false, "empty value: list");
        expect("[General]\nsLayout=line\n[MagicMenuBlock]\n", LayoutKind::kList, false, false, "the key in another section: not read");
        expect("[MagicMenuBlock]\n; sLayout=line\n", LayoutKind::kList, false, false, "a commented-out line: not read");
        expect("[MagicMenuBlock]\nsLayout=line\nsLayout=list\n", LayoutKind::kLine, true, true, "the first occurrence counts");
        expect("[MagicMenuBlock]\nsLayoutX=line\n", LayoutKind::kList, false, false, "another key that starts the same: not read");
        expect("[ MagicMenuBlock ]\nsLayout=line", LayoutKind::kLine, true, true, "spaces in the section brackets, no final newline");
        expect("\xEF\xBB\xBF[MagicMenuBlock]\nsLayout=line\n", LayoutKind::kLine, true, true, "a UTF-8 BOM before the first section");
        Expect(StripBom("\xEF\xBB\xBF" "abc") == "abc" && StripBom("abc") == "abc" && StripBom("\xEF\xBB") == "\xEF\xBB", "StripBom: only a whole BOM at the start");
    }

    void Language()
    {
        std::printf("ResolveLanguage's reading\n");
        Expect(GameLanguageCode("KOREAN") == "ko" && GameLanguageCode("korean") == "ko", "the game's KOREAN -> ko (any case)");
        Expect(GameLanguageCode("ENGLISH") == "en" && GameLanguageCode("FRENCH") == "en" && GameLanguageCode("") == "en", "ENGLISH, FRENCH, nothing -> en");
        auto expect = [](std::string_view a_text, std::string_view a_game, std::string_view a_code, bool a_fromSetting, const char* a_what) {
            const auto read = ParseLanguage(a_text, a_game);
            Expect(read.code == a_code && read.fromSetting == a_fromSetting,
                Text("%s: '%s' (from the setting %d, value '%s')", a_what, read.code.c_str(), read.fromSetting, read.value.c_str()));
        };
        expect("", "KOREAN", "ko", false, "no ini text, a Korean game");
        expect("[Display]\nsLanguage = auto\n", "KOREAN", "ko", false, "auto, a Korean game");
        expect("[Display]\nsLanguage = auto\n", "ENGLISH", "en", false, "auto, an English game");
        expect("[Display]\nsLanguage = ko ; Korean\n", "ENGLISH", "ko", true, "ko in an English game");
        expect("[display]\nSLANGUAGE=EN\n", "KOREAN", "en", true, "EN in a Korean game (case-insensitive)");
        expect("[Display]\nsLanguage = fr\n", "KOREAN", "ko", false, "an unknown value: the game's");
        expect("[General]\nsLanguage = ko\n", "ENGLISH", "en", false, "the key in another section: the game's");
        expect("\xEF\xBB\xBF[Display]\nsLanguage=ko\n", "ENGLISH", "ko", true, "a UTF-8 BOM before [Display]");
    }

    // ---------------------------------------------------------------------
    void Strings(const std::string& a_langPath)
    {
        std::printf("strings\n");
        const StringTable english{ { "title", "Research Archetypes" }, { "link.journal", "Journal" } };
        const StringTable korean{ { "title", "Korean title" } };
        const auto fromLanguage = LookUp(&korean, english, "title");
        const auto fromEnglish = LookUp(&korean, english, "link.journal");
        const auto fromKey = LookUp(&korean, english, "link.missing");
        Expect(fromLanguage.text == "Korean title" && fromLanguage.source == StringSource::kLanguage, "a key the language has: its text");
        Expect(fromEnglish.text == "Journal" && fromEnglish.source == StringSource::kEnglish, "a key the language lacks: English");
        Expect(fromKey.text == "link.missing" && fromKey.source == StringSource::kKey, "a key English lacks too: the key itself");
        Expect(LookUp(nullptr, english, "title").source == StringSource::kEnglish, "no language file: English");
        const StringTable en{ { "archetype.42.name", "Area Targeting" }, { "archetype.42.short", "Area" }, { "archetype.12.name", "Fire" } };
        const StringTable ko{ { "archetype.42.name", "KO area name" }, { "archetype.12.short", "KO fire" } };
        Expect(ArchetypeName(&ko, en, 12).text == "KO fire", "archetype name: the language's short first");
        Expect(ArchetypeName(&ko, en, 42).text == "KO area name", "archetype name: the language's name when it has no short");
        Expect(ArchetypeName(nullptr, en, 42).text == "Area" && ArchetypeName(nullptr, en, 12).text == "Fire",
            "archetype name: English short, else English name");
        const auto noName = ArchetypeName(&ko, en, 9);
        Expect(noName.text == "archetype.9.short" && noName.source == StringSource::kKey, "archetype name: neither has it - the short key");

        std::ifstream file(a_langPath, std::ios::binary);
        if (!file) {
            Expect(false, Text("%s opens", a_langPath.c_str()));
            return;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(buffer.str());
        } catch (const nlohmann::json::exception& e) {
            Expect(false, Text("%s parses: %s", a_langPath.c_str(), e.what()));
            return;
        }
        Expect(json.is_object(), Text("%s is a JSON object", a_langPath.c_str()));
        if (!json.is_object()) return;
        int missing = 0;
        std::string firstMissing;
        for (const auto& key : RequiredKeys()) {
            const auto it = json.find(key);
            if (it == json.end() || !it->is_string() || it->get<std::string>().empty()) {
                if (missing++ == 0) firstMissing = key;
            }
        }
        Expect(missing == 0, Text("every key the block uses is a non-empty string (%d missing%s%s)", missing, missing ? ", first " : "", firstMissing.c_str()));
        Expect(json.value("archetype.12.short", "") == "Fire" && json.value("archetype.42.short", "") == "Area" &&
                   json.value("archetype.42.name", "") == "Area Targeting" && json.value("archetype.57.short", "") == "Summoning",
            "names by SR's index: 12 Fire, 42 Area (name Area Targeting), 57 Summoning");
        try {
            const auto withBom = nlohmann::json::parse(std::string("\xEF\xBB\xBF") + buffer.str());
            Expect(withBom == json, "the language file with a UTF-8 BOM parses the same");
        } catch (const nlohmann::json::exception& e) {
            Expect(false, Text("the language file with a UTF-8 BOM parses: %s", e.what()));
        }
    }

    int Run(const std::string& a_langPath)
    {
        Grouping();
        Decisions();
        Layouts();
        Height();
        Icons();
        Clicks();
        Replies();
        Requests();
        IniLayout();
        Language();
        Strings(a_langPath);
        std::printf("sr-block-check: %d PASS, %d FAIL\n", g_pass, g_fail);
        return g_fail == 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    std::string langPath = SR_BLOCK_EN_JSON;
    bool selftest = false;
    for (int i = 1; i < a_argc; ++i) {
        const std::string_view arg = a_argv[i];
        if (arg == "--selftest") {
            selftest = true;
        } else if (arg == "--lang" && i + 1 < a_argc) {
            langPath = a_argv[++i];
        } else {
            std::fprintf(stderr, "usage: magic-menu-sr-block-check [--selftest] [--lang <en.json>]\n");
            return kExitUsage;
        }
    }
    const int code = Run(langPath);
    if (selftest) std::printf(g_fail == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_fail);
    return code;
}
