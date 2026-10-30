// magic-menu-hom-block-check - offline check of the Heart of Magic add-on block's pure parts (HoM add-on plan Tasks 6/7):
// plugins/SL_MagicMenu_HoM/src as the plugin runs it - HomSettings.h's reading of the block's keys from SL_MagicMenu.ini (the
// shipped dist ini and made-up ones: defaults, accepted ranges, comments, rects with no area), the blocks' registered heights,
// Selection.h's pick rules (bug G1: a U key pick counts as chosen; G acts only when chosen; a list change drops the pick
// unless it was the add-on's own served show request), Strings.h's fill and key labels, the cards' height requests
// (HomLayoutMath.h CardHeight / HeightSender) and - moved here from magic-menu-preview-check in Task 6, unchanged - the tree
// window's label, badge and link-box arithmetic, the progress card's plan and the node card's rows.
// Not covered here (the in-game self-test only, bSelfTest=1): GetPrivateProfileString itself, HoM's API, anything drawn.
//
//   magic-menu-hom-block-check              one PASS/FAIL line per check, a total line
//   magic-menu-hom-block-check --selftest   the same checks, then a "selftest: all PASS / N FAIL" summary line
//
// No game types. Exit 0 when every check passes, 1 when one fails, 2 on a bad argument.

#include "BlockRules.h"
#include "HomLayoutMath.h"
#include "HomSettings.h"
#include "LayoutMath.h"
#include "Selection.h"
#include "Settings.h"
#include "Strings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef HOM_BLOCK_DIST_INI
#    error "HOM_BLOCK_DIST_INI (the shipped SL_MagicMenu.ini) must be defined by CMake"
#endif

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    bool Near(double a_lhs, double a_rhs, double a_tolerance)
    {
        return std::abs(a_lhs - a_rhs) <= a_tolerance;
    }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    // ---- an ini as GetPrivateProfileString reads it (the plugin's reader; written apart from HomSettings.h on purpose) ----

    std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string Trim(std::string_view a_text)
    {
        const auto first = a_text.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) return {};
        const auto last = a_text.find_last_not_of(" \t\r");
        return std::string(a_text.substr(first, last - first + 1));
    }

    // Sections and keys case-insensitive, the first of a repeated key wins, ';' lines are comments, the value trimmed at both
    // ends (GetPrivateProfileString's own rules); then CleanValue, as HomSettings.cpp's Raw() does
    struct Ini
    {
        std::map<std::string, std::string> values;  // "section|key" lowered -> raw value

        explicit Ini(const std::string& a_text)
        {
            std::istringstream in(a_text);
            std::string line, section;
            while (std::getline(in, line)) {
                const auto text = Trim(line);
                if (text.empty() || text.front() == ';') continue;
                if (text.front() == '[') {
                    const auto close = text.find(']');
                    section = Lower(close == std::string::npos ? text.substr(1) : text.substr(1, close - 1));
                    continue;
                }
                const auto eq = text.find('=');
                if (eq == std::string::npos) continue;
                values.try_emplace(section + "|" + Lower(Trim(text.substr(0, eq))), Trim(text.substr(eq + 1)));
            }
        }

        HomSettings::RawFn Raw() const
        {
            return [this](std::string_view a_section, std::string_view a_key) -> std::optional<std::string> {
                const auto it = values.find(Lower(a_section) + "|" + Lower(a_key));
                if (it == values.end()) return std::nullopt;
                return HomSettings::CleanValue(it->second);
            };
        }
    };

    HomSettings::ReadResult ReadText(const std::string& a_text)
    {
        const Ini ini(a_text);
        return HomSettings::Read(ini.Raw());
    }

    bool SameRect(const HomSettings::Rect& a_lhs, const HomSettings::Rect& a_rhs)
    {
        return a_lhs.left == a_rhs.left && a_lhs.top == a_rhs.top && a_lhs.right == a_rhs.right && a_lhs.bottom == a_rhs.bottom;
    }

    bool AllDefault(const HomSettings::Values& a_v)
    {
        const HomSettings::Values d;
        return a_v.keyNext == d.keyNext && a_v.keySetTarget == d.keySetTarget && SameRect(a_v.tree, d.tree) && SameRect(a_v.progress, d.progress) &&
               SameRect(a_v.nodeCard, d.nodeCard) && a_v.treeScale == d.treeScale && a_v.treeMarkScale == d.treeMarkScale && a_v.maxSteps == d.maxSteps &&
               a_v.maxNodes == d.maxNodes && a_v.debugExtraHeight == d.debugExtraHeight && a_v.selfTest == d.selfTest;
    }

    // ---- the cases ----

    void SettingsDefaults()
    {
        std::printf("settings: defaults (Settings.h at f83dc6f3)\n");
        const HomSettings::Values d;
        Expect(d.keyNext == 22 && d.keySetTarget == 34, Text("keys U %u and G %u (iNext 22, iSetTarget 34)", d.keyNext, d.keySetTarget));
        Expect(d.treeScale == 0.75 && d.treeMarkScale == 1.0 && d.maxSteps == 1 && d.maxNodes == 60 && d.debugExtraHeight == 0.0 && d.selfTest,
            "tree scale 0.75, mark scale 1.0, steps 1, nodes 60, no extra height, self-test on (the host's default)");
        Expect(SameRect(d.tree, { 978, 67, 1238, 297 }) && SameRect(d.progress, { 648, 306, 969, 408 }) && SameRect(d.nodeCard, { 978, 306, 1238, 623 }),
            "rects: tree 978,67..1238,297, progress 648,306..969,408, node card 978,306..1238,623 (layout E2)");
        const auto empty = ReadText("");
        Expect(AllDefault(empty.values) && empty.warnings.empty(), "an empty ini: every default, no warning");
        const auto heights = HomSettings::BlockHeights(d);
        Expect(heights.tree == 230.0 && heights.progress == 102.0 && heights.nodeCard == 317.0, "registered heights 230 / 102 / 317 (each rect's bottom - top)");
    }

    void SettingsDistIni()
    {
        std::printf("settings: the shipped SL_MagicMenu.ini\n");
        std::ifstream file(HOM_BLOCK_DIST_INI, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        Expect(file.is_open() && !text.str().empty(), Text("read %s", HOM_BLOCK_DIST_INI));
        const auto read = ReadText(text.str());
        Expect(read.warnings.empty(), Text("no warning (%zu)", read.warnings.size()));
        for (const auto& warning : read.warnings) std::printf("    %s\n", warning.c_str());
        Expect(AllDefault(read.values), "every block key it sets is the block's default (keys 22 / 34, the E2 rects, scales, caps, self-test 1)");
    }

    void SettingsValues()
    {
        std::printf("settings: values read, accepted ranges, comments\n");
        {
            const auto read = ReadText(
                "[General]\nbSelfTest=0\n[Keys]\niNext=19 ; R\niSetTarget = 35\n[Layout]\nfTreeLeft=900\nfTreeTop=60\nfTreeRight=1200\nfTreeBottom=280\n"
                "fTreeScale=0.5\nfTreeMarkScale=2\niMaxSteps=3\niMaxNodes=120\nfProgressBottom=420 # taller\n[Debug]\nfDebugExtraHeight=300\n");
            const auto& v = read.values;
            Expect(read.warnings.empty() && !v.selfTest && v.keyNext == 19 && v.keySetTarget == 35, "bSelfTest 0, iNext 19 (comment cut), iSetTarget 35");
            Expect(SameRect(v.tree, { 900, 60, 1200, 280 }) && v.progress.bottom == 420 && v.progress.top == 306,
                "fTree* all four read; fProgressBottom alone changes only the bottom");
            Expect(v.treeScale == 0.5 && v.treeMarkScale == 2.0 && v.maxSteps == 3 && v.maxNodes == 120 && v.debugExtraHeight == 300.0,
                "scale 0.5, mark scale 2, steps 3, nodes 120, extra height 300");
            const auto heights = HomSettings::BlockHeights(v);
            Expect(heights.tree == 220.0 && heights.progress == 114.0 && heights.nodeCard == 617.0, "heights follow: 220 / 114 / 317 + 300 = 617");
        }
        {
            // the range edges: the ends are in, past them the default stays (one warning each)
            const auto edges = ReadText("[Layout]\nfTreeScale=0.01\nfTreeMarkScale=3\niMaxSteps=6\niMaxNodes=200\n[Keys]\niNext=255\niSetTarget=1\n"
                                        "[Debug]\nfDebugExtraHeight=2000\n");
            const auto& e = edges.values;
            Expect(edges.warnings.empty() && e.treeScale == 0.01 && e.treeMarkScale == 3.0 && e.maxSteps == 6 && e.maxNodes == 200 && e.keyNext == 255 &&
                       e.keySetTarget == 1 && e.debugExtraHeight == 2000.0,
                "range ends accepted: scale 0.01, mark 3, steps 6, nodes 200, keys 255 / 1, extra 2000");
            const auto past = ReadText("[Layout]\nfTreeScale=10.5\nfTreeMarkScale=0.4\niMaxSteps=7\niMaxNodes=0\n[Keys]\niNext=256\niSetTarget=0\n"
                                       "[Debug]\nfDebugExtraHeight=2001\n");
            Expect(AllDefault(past.values) && past.warnings.size() == 7,
                Text("past the ends (10.5, 0.4, 7, 0, 256, 0, 2001): every default kept, %zu warnings (want 7)", past.warnings.size()));
            const auto low = ReadText("[Layout]\nfTreeScale=0.005\niMaxSteps=0\niMaxNodes=201\nfTreeMarkScale=3.5\n");
            Expect(AllDefault(low.values) && low.warnings.size() == 4, "0.005 / 0 / 201 / 3.5: defaults 0.75 / 1 / 60 / 1.0 kept");
            const auto scale = std::ranges::find_if(past.warnings, [](const std::string& a_w) { return a_w.starts_with("[Layout] fTreeScale"); });
            Expect(scale != past.warnings.end() && *scale == "[Layout] fTreeScale = '10.5' is not a number in 0.01..10 - keeping 0.75",
                Text("the warning names the key, the range and the kept value: \"%s\"", scale != past.warnings.end() ? scale->c_str() : "(none)"));
        }
        {
            const auto bad = ReadText("[Layout]\nfTreeScale=nan\niMaxNodes=6x\nfTreeMarkScale=inf\n[General]\nbSelfTest=2\n[Keys]\niNext=-3\n");
            Expect(AllDefault(bad.values) && bad.warnings.size() == 5, Text("nan, '6x', inf, bSelfTest 2, iNext -3: not read (%zu warnings)", bad.warnings.size()));
            const auto blank = ReadText("[Layout]\nfTreeScale=   ; nothing\niMaxNodes=\n");
            Expect(AllDefault(blank.values) && blank.warnings.empty(), "a value that is only a comment, or empty: the default, no warning");
            const auto flat = ReadText("[Layout]\nfNodeCardTop=623\nfProgressRight=600\n");
            Expect(AllDefault(flat.values) && flat.warnings.size() == 2, "a node card rect 0 high, a progress rect right of nothing: both keep the default rect");
            const auto cased = ReadText("[layout]\nFTREESCALE=0.6\n[KEYS]\ninext=23\n");
            Expect(cased.values.treeScale == 0.6 && cased.values.keyNext == 23, "section and key names in any case (GetPrivateProfileString's rule)");
        }
        Expect(HomSettings::CleanValue("  12 ;x") == std::optional<std::string>("  12") && !HomSettings::CleanValue(" ;only").has_value() &&
                   HomSettings::CleanValue("0.5#c") == std::optional<std::string>("0.5"),
            "CleanValue: trailing comment and blanks cut (as Settings.cpp Raw), nothing left = no value");
    }

    void SelectionRules()
    {
        std::printf("selection: the pick rules (bug G1)\n");
        using Selection::Chosen;
        Expect(!Chosen(0, -1), "nothing shown, no key pick: not chosen (the node card's hint)");
        Expect(Chosen(0, 7), "nothing shown, the U key picked node 7: chosen (G1 - the node card shows the pick)");
        Expect(Chosen(0x00012FCD, -1), "the card shows a spell, no pick: chosen");
        Expect(Chosen(0x00012FCD, 3), "both: chosen");
        Expect(Chosen(0, 0), "a key pick of node 0 counts (an index, not a flag)");
        Expect(!Selection::SetTargetActs(0, -1) && Selection::SetTargetActs(0, 7) && Selection::SetTargetActs(0x00012FCD, -1),
            "G / the button act only when chosen: not with nothing, yes on a key pick, yes on a shown spell");
        Expect(Selection::DropPick(3, 4), "selection seq 3 seen, 4 now: the pick is dropped");
        Expect(!Selection::DropPick(4, 4), "the same seq: kept");
        using Source = BlockApi::SelectionSource;
        Expect(!Selection::DropPick(3, 4, Source::kAddon, 9, 9), "the change was our own served request 9: kept");
        Expect(Selection::DropPick(3, 4, Source::kAddon, 8, 9), "a show request served, but not our last one (8, we sent 9): dropped");
        Expect(Selection::DropPick(3, 4, Source::kList, 9, 9), "the player's list (source kList): dropped, whatever servedRequest says");
        Expect(Selection::DropPick(3, 4, Source::kAddon, 0, 0), "we never sent a request (0): a served 0 is not ours");
        Expect(!Selection::DropPick(4, 4, Source::kList, 0, 0), "no change: kept");
    }

    void StringsTable()
    {
        std::printf("strings: the HoM rows\n");
        using namespace HomStrings;
        Expect(KeyLabel(22) == "[U]" && KeyLabel(34) == "[G]" && KeyLabel(19) == "[R]" && KeyLabel(2) == "[#2]", "key labels [U] [G] [R], an unnamed key [#2]");
        Expect(Fill(Str::kProgressCandidates, { { "{n}", "3" }, { "{key}", KeyLabel(22) } }) == "배울 수 있는 주문 3개 · [U] 다음 주문",
            "progress_candidates filled: 3, [U]");
        Expect(Fill(Str::kXp, { { "{cur}", "40" }, { "{need}", "100" } }) == "40 / 100 경험치", "xp: 40 / 100");
        Expect(SchoolText("Destruction") == "파괴마법" && SchoolText("Mysticism") == "Mysticism", "school names: Destruction 파괴마법, an unknown key as it is");
        Expect(std::string_view(Key(Str::kNodeSub)) == "node_sub" && std::string_view(Key(Str::kProgressNoData)) == "progress_no_data",
            "keys as the core logged them (node_sub, progress_no_data)");
    }

    void CardHeights()
    {
        std::printf("cards: height requests ('SLMH')\n");
        Expect(LayoutMath::kFitSlack == BlockRules::kFitSlack && LayoutMath::HeightSender::kTolerance == BlockRules::kHeightTolerance,
            "the slack 0.5 and tolerance 0.01 are the host's own values");
        Expect(LayoutMath::CardHeight(80.0, 102.0) == 80.5 && LayoutMath::CardHeight(200.0, 102.0) == 102.0 && LayoutMath::CardHeight(-1.0, 102.0) == 102.0 &&
                   LayoutMath::CardHeight(std::nan(""), 317.0) == 317.0,
            "needed 80 -> 80.5; more than registered -> 102 (the host refuses a taller one); <= 0 or NaN -> registered");
        LayoutMath::HeightSender sender;
        const bool first = sender.ShouldSend(80.5), same = sender.ShouldSend(80.505), other = sender.ShouldSend(90.0);
        sender.Reset();
        const bool afterReset = sender.ShouldSend(90.0);
        Expect(first && !same && other && afterReset, "sent once per change (80.5, then 80.505 not, 90 again); a new menu sends again");
    }

    // ---- moved from magic-menu-preview-check (HoM add-on plan Task 6), unchanged ----

    // Build 6 (HomLayoutMath.h): the tree window's labels, its faint locked neighbours and exits, and the progress card's plan
    void TreeLayout()
    {
        std::printf("tree window layout (labels at the edge, badge sectors, label places)\n");
        const HomSettings::Values defaults;
        const auto area = HomSettings::TreeContent(defaults.tree);
        const double edge = area.halfW + Settings::kPanelPad, gap = 10.0 + 1.0, most = 104.0, least = 20.0;
        Expect(Near(area.halfW, 122.0, 1e-9) && Near(area.halfH, 87.0, 1e-9), Text("E2 tree window half size %.1f x %.1f (want 122 x 87)", area.halfW, area.halfH));
        const double atEdge = LayoutMath::LabelWidth(120.0, gap, edge, false, most);
        Expect(atEdge < least, Text("a far-side label of a node at sx 120 has %.1f units to the edge: left out (< %.0f)", atEdge, least));
        Expect(Near(LayoutMath::LabelWidth(52.6, gap, edge, false, most), 66.4, 1e-9), "a far-side label at sx 52.6 shrinks to the edge: 66.4");
        bool towardCentre = true;
        for (double sx = -area.halfW; sx <= area.halfW; sx += 1.0) {
            // the selected node's brackets at fTreeMarkScale: the widest gap a drawn node's label uses (spec v10 T2)
            const double reach = 15.0 * defaults.treeMarkScale + 1.0;
            towardCentre = towardCentre && LayoutMath::LabelWidth(sx, reach, edge, sx > 0, most) >= most;
        }
        Expect(towardCentre, "a drawn node's label (facing the centre) keeps its full 104 everywhere in the window");

        // the off-window badges' sectors (spec v10 T7): from the window centre, y down; each node of Window::outside once
        {
            const std::vector<TreeData::PlacedNode> outside{ { 1, 300.0, 0.0 }, { 2, 0.0, -200.0 }, { 3, 0.0, 150.0 }, { 4, -250.0, 0.0 },
                { 5, 200.0, 190.0 }, { 6, 400.0, 30.0 } };
            const auto badges = LayoutMath::Badges(outside);
            Expect(badges.total == 6 && badges.sectors[0] == 2 && badges.sectors[6] == 1 && badges.sectors[2] == 1 && badges.sectors[4] == 1 &&
                       badges.sectors[1] == 1,
                Text("badges: right 2 (one 4 degrees down), up 1, down 1, left 1, down-right 1 (total %d)", badges.total));
            const auto none = LayoutMath::Badges({});
            Expect(none.total == 0 && std::ranges::all_of(none.sectors, [](int a_n) { return a_n == 0; }), "nothing outside: no badge");
        }

        // the centre's label place (round 3, build 7 T7.2): each place's box against what is reserved; a one-line label at size 14
        // (FieldH 21.5) 88 wide, beside the centre at gap 11 (round 3's numbers)
        using LayoutMath::Place;
        const LayoutMath::Box right{ 11.0, -10.75, 99.0, 10.75 }, left{ -99.0, -10.75, -11.0, 10.75 };
        const std::vector<LayoutMath::PlacedBox> sides{ { Place::kRight, right }, { Place::kLeft, left } };
        const auto nodeBox = [](double a_x, double a_y, double a_r) { return LayoutMath::Box{ a_x - a_r, a_y - a_r, a_x + a_r, a_y + a_r }; };
        {
            const auto clear = LayoutMath::ChoosePlace(sides, { nodeBox(0, 0, 10) });
            Expect(clear.clear && clear.index == 0 && clear.overlap == 0.0 && clear.firstOverlap == 0.0, "both sides clear: the right (as before)");
        }
        {
            // GPT round 3: a drawn neighbour at (-52.5, 0) and an off-window exit up-right (sector 7: arrow at (80, -80), text
            // 36 wide at about (52, -69)); the counting rule turned the label left onto the neighbour
            const std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, 10), nodeBox(-52.5, 0, 10), nodeBox(80, -80, 10),
                { 34.0, -79.6, 70.0, -58.1 } };
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(side.clear && side.index == 0 && side.overlap == 0.0, Text("a neighbour left, a badge up-right: right (overlap %.1f)", side.overlap));
        }
        {
            // round 1 #4: the E2 scene, a faint locked neighbour up-right at (52.6, -45.7) with its link from the centre
            std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, 10), nodeBox(52.6, -45.7, 10) };
            const auto link = LayoutMath::LinkBoxes(0, 0, 52.6, -45.7, 10, 4, 1);
            reserved.insert(reserved.end(), link.begin(), link.end());
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(side.clear && side.index == 1 && side.firstOverlap > 0.0 && side.overlap == 0.0,
                Text("a faint locked neighbour up-right: its link crosses the right label, so left (overlap right %.1f, left %.1f)", side.firstOverlap,
                    side.overlap));
        }
        {
            // spec v10 T5: no overlap is accepted any more (build 9 took the smaller one, left 50 against right 200)
            const std::vector<LayoutMath::Box> reserved{ { 20.0, -5.0, 40.0, 5.0 }, { -30.0, -5.0, -25.0, 5.0 } };
            const auto side = LayoutMath::ChoosePlace(sides, reserved);
            Expect(!side.clear && Near(side.firstOverlap, 200.0, 1e-9) && Near(side.overlap, 50.0, 1e-9),
                "both sides covered: no place, the label is dropped (the log names the least covered: left 50 against right 200)");
            Expect(!LayoutMath::ChoosePlace({}, reserved).clear, "no place offered at all: not clear");
        }

        // build 7 T7.2: labels start past the state mark's ring, and a kept label leaves two close neighbours' marks clear
        {
            constexpr double kReach = LayoutMath::MarkReach(7.0, 3.0, 2.5), kLabelGap = 1.0;
            Expect(Near(kReach, 11.25, 1e-12), Text("the mark reaches %.2f (diamond 7, ring gap 3, half the 2.5 arc)", kReach));
            const auto mark = nodeBox(0, 0, kReach);
            const LayoutMath::Box oldLeft{ -11.0 - 30.0, -10.75, -11.0, 10.75 }, newLeft{ -(kReach + kLabelGap) - 30.0, -10.75, -(kReach + kLabelGap), 10.75 };
            Expect(LayoutMath::OverlapArea(oldLeft, mark) > 0.0 && LayoutMath::OverlapArea(newLeft, mark) == 0.0,
                Text("a left label at build 6's gap 11 reaches into the ring (%.2f); at %.2f it is clear", LayoutMath::OverlapArea(oldLeft, mark),
                    kReach + kLabelGap));
        }
        {
            // the build-6 game run (log "Heal ... centre label right (overlap right 297.8, left 365.5)", frames f_024-f_040): the centre
            // with neighbours 15.5 right, 19 left and one up-left; a 30 x 21.5 label at gap 12.25
            constexpr double kReach = 11.25, kGap = 12.25, kEdgeX = 130.0, kEdgeY = 95.0;
            const std::vector<LayoutMath::Box> reserved{ nodeBox(0, 0, kReach), nodeBox(15.5, 0, kReach), nodeBox(-19.0, 0, kReach),
                nodeBox(-9.0, -10.0, kReach), nodeBox(6.7, -48.0, kReach) };
            const auto places = LayoutMath::KeptLabelPlaces(0, 0, 30.0, 21.5, kGap, false, kEdgeX, kEdgeY, 10.0, 2);
            const auto choice = LayoutMath::ChoosePlace(places, reserved);
            Expect(places.size() == 8 && places[0].place == Place::kRight && places[1].place == Place::kLeft && places[2].place == Place::kBelow,
                Text("places tried: right, left, below, above, then both sides further out twice (%zu)", places.size()));
            Expect(choice.clear && choice.firstOverlap > 0.0 && choice.overlap == 0.0 && places[choice.index].place == Place::kBelow,
                Text("both sides cover a diamond (right %.1f): the label goes below, clear", choice.firstOverlap));
        }
        {
            // a node near the right edge: a place past the window is not offered; the default side stays offered
            const auto places = LayoutMath::KeptLabelPlaces(100.0, 0, 60.0, 21.5, 12.25, false, 130.0, 95.0, 10.0, 2);
            const bool noRightFar = std::ranges::none_of(places, [](const LayoutMath::PlacedBox& a_p) { return a_p.place == Place::kRightFar; });
            Expect(places.front().place == Place::kRight && noRightFar && places[1].place == Place::kLeft,
                "near the right edge: no place further right; the drawn (default) side is still the first");
        }
        {
            const auto link = LayoutMath::LinkBoxes(0, 0, 10, 0, 2, 2, 0.5);
            Expect(link.size() == 4 && Near(link.front().left, 1.5, 1e-9) && Near(link.back().right, 8.5, 1e-9),
                Text("a 10-long link, 2 off each end, every 2: 4 boxes from x 1.5 to 8.5 (got %zu)", link.size()));
            Expect(LayoutMath::LinkBoxes(0, 0, 3, 0, 2, 2, 0.5).empty(), "a link shorter than both ends' room: no boxes");
        }
    }

    void ProgressLayout()
    {
        std::printf("progress card plan (102 high: what gives way)\n");
        constexpr double kAscent = 0.90;  // the build-3 frame's ascent share
        const auto plan = [](double a_first, double a_title, double a_hint, double a_offset) {
            const LayoutMath::TextMetrics metrics{ a_first, a_first, 4.0 };
            return std::pair{ metrics, LayoutMath::PlanProgress(metrics, { 102.0, 4.0, 2.0, a_title, 19.0, 12.0, 2.0, a_hint, 8.0, 0.5, a_offset }) };
        };
        const auto last = [](const LayoutMath::TextMetrics& a_metrics, const LayoutMath::ProgressPlan& a_plan) {
            return a_plan.hint ? a_plan.hintY + a_metrics.FieldH(a_plan.hintSize) : a_plan.barRowY + a_plan.barRowH;
        };
        {
            const auto [m, p] = plan(1.25, 14.0, 14.0, kAscent * (19.0 - 14.0));
            Expect(!p.offsetDropped && Near(p.offset, 4.5, 1e-9) && p.title && p.hint && Near(p.hintSize, 14.0, 1e-9) && Near(last(m, p), 98.25, 1e-9),
                Text("rows 14, line 1.25: offset 4.50 kept, hint at 14, last line ends at %.2f of 100", last(m, p)));
            const double titleBase = p.titleY + 2.0 + kAscent * 14.0, nameBase = 4.0 + 2.0 + kAscent * 19.0;
            Expect(Near(titleBase, nameBase, 1e-9), Text("and the title's baseline %.2f meets the name's %.2f (both 306 + this = 329.1)", titleBase, nameBase));
        }
        {
            const auto [m, p] = plan(1.25, 16.0, 16.0, kAscent * (19.0 - 16.0));
            Expect(!p.offsetDropped && p.hint && p.hintSize < 16.0 && last(m, p) <= p.bottom,
                Text("rows 16: the hint shrinks to %.1f first, the offset %.2f stays, ends at %.2f", p.hintSize, p.offset, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.25, 14.0, 16.0, 4.5);
            Expect(!p.offsetDropped && p.hint && p.hintSize < 16.0 && last(m, p) <= p.bottom,
                Text("review table case (offset 4.5, hint 16): the hint shrinks to %.1f, the offset stays, ends at %.2f", p.hintSize, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.5, 14.0, 14.0, 4.5);
            const double withOffset = 4.0 + 4.5 + m.FieldH(14.0) + p.targetH + p.barRowH + m.FieldH(8.0);
            Expect(p.offsetDropped && p.title && p.hint && Near(withOffset, 104.0, 1e-9) && Near(last(m, p), 99.5, 1e-9),
                Text("line 1.5: with the offset a size-8 hint would end at %.2f, so the offset goes and the hint stays (size %.1f, ends %.2f)",
                    withOffset, p.hintSize, last(m, p)));
        }
        {
            const auto [m, p] = plan(1.6, 14.0, 14.0, 4.5);
            Expect(p.offsetDropped && p.offset == 0.0 && p.title && !p.hint && last(m, p) <= p.bottom,
                Text("a tall font (line 1.6): not even a size-8 hint fits, so the offset goes, then the hint; ends at %.2f", last(m, p)));
        }
        {
            const auto [m, p] = plan(2.5, 14.0, 14.0, 4.5);
            Expect(p.offsetDropped && !p.title && Near(p.targetY, 4.0, 1e-9) && !p.hint && last(m, p) <= p.bottom,
                Text("line 2.5: offset, hint and title give way; the target and bar end at %.2f", last(m, p)));
        }
    }

    // user 2026-10-30: blocks fit their content - each fit read back at the fitted height lays out the same lines (no loop); the
    // HoM cards' half of preview-check's FitMath (its LinesFit cases stay there)
    void FitMath()
    {
        std::printf("blocks fit their content (HomLayoutMath)\n");
        constexpr double kSlack = BlockRules::kFitSlack;  // the slack FitBlock adds (review R9: the host's own value)
        const double rowH = 20.0, pitch = 17.0;
        for (const int count : { 1, 2, 3, 7 }) {
            const auto rows = static_cast<std::size_t>(count);
            const double h = LayoutMath::RowsHeight(rows, rowH, pitch);
            Expect(LayoutMath::RowsFit(h + kSlack, rowH, pitch) == rows && LayoutMath::RowsFit(h - 1.0, rowH, pitch) == rows - 1,
                Text("%zu rows take %.1f: that room (+ slack) fits %zu, 1 less fits one fewer", rows, h, rows));
        }
        Expect(LayoutMath::RowsHeight(0, rowH, pitch) == 0.0 && LayoutMath::RowsFit(rowH - 1.0, rowH, pitch) == 0, "no rows: 0 high; under one row: none fit");
        const LayoutMath::TextMetrics metrics{ 1.2, 1.1, 4.0 };
        // the progress card: the plan at its needed height is the plan at its full height (hint, sizes, places)
        for (const double hint : { 13.0, 16.0 }) {
            const LayoutMath::ProgressIn full{ 102.0, 4.0, 2.0, 11.0, 19.0, 12.0, 2.0, hint, 8.0, 0.5, 0.0 };
            const auto plan = LayoutMath::PlanProgress(metrics, full);
            const double needed = LayoutMath::ProgressNeeded(metrics, full, plan);
            auto fitted = full;
            fitted.cardH = std::min(needed + kSlack, full.cardH);
            const auto again = LayoutMath::PlanProgress(metrics, fitted);
            Expect(needed < full.cardH && again.hint == plan.hint && again.hintSize == plan.hintSize && again.hintY == plan.hintY && again.title == plan.title &&
                       LayoutMath::ProgressNeeded(metrics, fitted, again) == needed,
                Text("progress card, hint size %.0f: needs %.1f of 102, the plan at that height is the same (hint %.1f at %.1f)", hint, needed, plan.hintSize, plan.hintY));
        }
        Expect(LayoutMath::NodeCardNeeded(80.0, 37.0, 4.0, 74.0) == 195.0, "node card: rows top 80 + rows 37 + gap 4 + bottom part 74 = 195");
    }
}

int main(int a_argc, char** a_argv)
{
    bool selftest = false;
    for (int i = 1; i < a_argc; ++i) {
        const std::string_view arg = a_argv[i];
        if (arg == "--selftest") {
            selftest = true;
        } else {
            std::fprintf(stderr, "usage: magic-menu-hom-block-check [--selftest]\n");
            return kExitUsage;
        }
    }
    SettingsDefaults();
    SettingsDistIni();
    SettingsValues();
    SelectionRules();
    StringsTable();
    CardHeights();
    TreeLayout();
    ProgressLayout();
    FitMath();
    std::printf("hom-block-check: %d PASS, %d FAIL\n", g_pass, g_fail);
    if (selftest) std::printf(g_fail == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_fail);
    return g_fail == 0 ? kExitOk : kExitFail;
}
