// magic-menu-tree-check - offline check of the magic menu prototype's tree data
// (plugins/SL_MagicMenu/src/TreeData.cpp): parses a saved spell tree as the plugin
// does and runs the tree window query with every node as the centre.
//
//   magic-menu-tree-check [tree.json]   counts, window sizes and mark spacing for that tree
//                                       (default: the TAKEALOOK overwrite spell_tree.json)
//   magic-menu-tree-check --selftest    made-up trees: normal, boundary (BOM, empty school), malformed,
//                                       the viewport window (spec v10 T3, T7), the target path (T8), the unturned
//                                       non-square window (spec v11 T10 / R12), the full-opacity set (T11 / R11),
//                                       the list selection rule (spec v12 S1 / G1: SelectionRule.h)
//
// Exit 0 on success, 1 when a self-test fails or a drawn mark of the file's tree reaches past half the distance to
// its closest neighbour at the plugin's defaults (spec v11 R5), 2 when the file cannot be read or parsed.

#include "HomStyle.h"
#include "LayoutMath.h"
#include "SelectionRule.h"
#include "Settings.h"
#include "TreeData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitBadInput = 2;

    constexpr const char* kDefaultTree = "D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json";

    /// The largest reach a drawn mark has at the plugin's defaults (Widget.cpp: HomStyle::MaxReach at fTreeScale x
    /// fTreeMarkScale, line widths x fTreeScale (review v11 #5), with the learnable ring as Skald draws it - the larger case)
    double DefaultReach()
    {
        const Settings::Values defaults;
        return HomStyle::MaxReach(defaults.treeScale * defaults.treeMarkScale, true, defaults.treeScale);
    }

    /// The window the plugin draws with its default settings (Settings.h: the tree rect, fTreeScale, steps, cap;
    /// the content area inside the title and padding, as Widget.cpp computes it; the mark's reach as margin; no turn)
    TreeData::View CheckView()
    {
        const Settings::Values defaults;
        const auto area = Settings::TreeContent(defaults.tree);
        TreeData::View view;
        view.scale = defaults.treeScale;
        view.halfW = area.halfW;
        view.halfH = area.halfH;
        view.margin = DefaultReach();
        view.maxSteps = defaults.maxSteps;
        view.maxNodes = defaults.maxNodes;
        return view;
    }

    /// The closest two drawn nodes of a window (stage units; infinity with fewer than two). O(n^2).
    double Closest(const TreeData::Window& a_window)
    {
        double closest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < a_window.nodes.size(); ++i) {
            for (std::size_t j = i + 1; j < a_window.nodes.size(); ++j) {
                const auto& a = a_window.nodes[i];
                const auto& b = a_window.nodes[j];
                closest = std::min(closest, std::hypot(a.sx - b.sx, a.sy - b.sy));
            }
        }
        return closest;
    }

    const TreeData::School* SchoolOf(const TreeData::Tree& a_tree, const TreeData::Node& a_node)
    {
        const auto it = std::ranges::find_if(a_tree.schools, [&](const TreeData::School& a_school) {
            return a_school.name == a_node.school;
        });
        return it == a_tree.schools.end() ? nullptr : &*it;
    }

    void PrintCounts(const TreeData::Tree& a_tree)
    {
        std::printf("tree: %zu nodes, %zu schools, layoutMode \"%s\", noRotate %s\n", a_tree.nodes.size(),
            a_tree.schools.size(), a_tree.layoutMode.c_str(), a_tree.noRotate ? "true" : "false");
        for (const auto& school : a_tree.schools) {
            const auto rootText = school.rootIndex >= 0 ? a_tree.nodes[school.rootIndex].id : std::string("(none)");
            char spoke[32] = "(none)";
            if (school.spokeDeg) {
                std::snprintf(spoke, sizeof(spoke), "%.1f", *school.spokeDeg);
            }
            std::printf("  %-12s %4zu nodes  spoke %7s deg (not used: the window is not turned)  root %s\n", school.name.c_str(),
                school.nodes.size(), spoke, rootText.c_str());
        }
        const auto zeroFormIds = std::ranges::count_if(a_tree.nodes, [](const TreeData::Node& a_node) {
            return a_node.formId == 0;
        });
        std::printf("nodes without x/y: %d\n", a_tree.nodesWithoutXY);
        std::printf("edges: %d structural, %d lock (%zu total)\n", a_tree.structuralEdges, a_tree.lockEdges,
            a_tree.edges.size());
        std::printf("unparsed ids (links to no node, nodes with no id text): %d\n", a_tree.unparsedIds);
        std::printf("nodes whose id is not hex (formId 0): %lld\n", static_cast<long long>(zeroFormIds));
    }

    /// The window at the plugin's defaults with every node as the centre. False when a drawn mark reaches past half the
    /// distance to the closest other drawn mark somewhere (spec v11 R5: the largest reach against half the closest pair).
    bool PrintWindowStats(const TreeData::Tree& a_tree)
    {
        int centres = 0;
        int skipped = 0;
        int maxCandidates = 0;
        int maxCandidatesAt = -1;
        long long sumCandidates = 0;
        long long sumShown = 0;
        long long sumUnlinked = 0;
        long long sumOutside = 0;
        int overCap = 0;
        int onlyItself = 0;
        int crowded = 0;
        double closest = std::numeric_limits<double>::infinity();
        int closestAt = -1;
        const double width = 2 * DefaultReach();
        for (int index = 0; index < static_cast<int>(a_tree.nodes.size()); ++index) {
            const auto& node = a_tree.nodes[index];
            const auto* school = SchoolOf(a_tree, node);
            if (!node.hasXY || !school) {
                ++skipped;
                continue;
            }
            const auto view = CheckView();
            const auto window = TreeData::Query(a_tree, index, view);
            ++centres;
            sumCandidates += window.candidatesBeforeCap;
            sumShown += static_cast<long long>(window.nodes.size());
            sumUnlinked += std::ranges::count_if(window.nodes, [](const TreeData::PlacedNode& a_node) { return a_node.steps < 0; });
            sumOutside += static_cast<long long>(window.outside.size());
            if (window.candidatesBeforeCap > maxCandidates) {
                maxCandidates = window.candidatesBeforeCap;
                maxCandidatesAt = index;
            }
            if (window.candidatesBeforeCap > view.maxNodes) {
                ++overCap;
            }
            if (window.nodes.size() == 1) {
                ++onlyItself;
            }
            const auto near = Closest(window);
            crowded += near < width ? 1 : 0;
            if (near < closest) {
                closest = near;
                closestAt = index;
            }
        }
        const auto shown = CheckView();
        std::printf("window query (plugin defaults: scale %.2f stage units per tree unit, half %.1f x %.1f, margin %.2f, %d steps linked, "
                    "cap %d), every node as centre:\n",
            shown.scale, shown.halfW, shown.halfH, shown.margin, shown.maxSteps, shown.maxNodes);
        std::printf("  centres run: %d (skipped, no x/y or no school: %d)\n", centres, skipped);
        if (centres == 0) {
            return true;
        }
        std::printf("  candidatesBeforeCap: max %d (centre %s \"%s\"), avg %.2f\n", maxCandidates,
            a_tree.nodes[maxCandidatesAt].id.c_str(), a_tree.nodes[maxCandidatesAt].name.c_str(),
            static_cast<double>(sumCandidates) / centres);
        std::printf("  nodes shown after cap: avg %.2f (not linked within the steps: avg %.2f)\n", static_cast<double>(sumShown) / centres,
            static_cast<double>(sumUnlinked) / centres);
        std::printf("  centre's neighbours outside the window (badges): avg %.2f\n", static_cast<double>(sumOutside) / centres);
        std::printf("  centres over the cap of %d: %d\n", shown.maxNodes, overCap);
        std::printf("  centres showing only themselves: %d\n", onlyItself);
        const bool spaced = crowded == 0;
        std::printf("  %s  closest two drawn marks %.2f apart (centre %s \"%s\"), half %.2f; the largest drawn reach %.3f "
                    "(HoM sizes x fTreeScale %.2f x fTreeMarkScale %.2f): %d centre(s) with marks closer than twice that\n",
            spaced ? "PASS" : "FAIL", closest, closestAt >= 0 ? a_tree.nodes[closestAt].id.c_str() : "-",
            closestAt >= 0 ? a_tree.nodes[closestAt].name.c_str() : "-", closest / 2, width / 2, Settings::Values{}.treeScale,
            Settings::Values{}.treeMarkScale, crowded);
        return spaced;
    }

    // ---- self-tests ----

    int g_failures = 0;

    void Expect(bool a_ok, const char* a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what);
        if (!a_ok) {
            ++g_failures;
        }
    }

    bool Near(double a_lhs, double a_rhs) { return std::abs(a_lhs - a_rhs) < 1e-9; }

    const TreeData::PlacedNode* Placed(const TreeData::Window& a_window, int a_index)
    {
        const auto it = std::ranges::find_if(a_window.nodes, [&](const TreeData::PlacedNode& a_node) {
            return a_node.index == a_index;
        });
        return it == a_window.nodes.end() ? nullptr : &*it;
    }

    // One school growing along +x (its spokeAngle is no longer used: the window shows x/y as saved, spec v11 T10).
    // R -> A -> B by prerequisites; R -> D only by R's children; L locks B; "0xDEAD" is in no node;
    // "0x00000002_dup_1" is a copy of A in another school.
    constexpr std::string_view kNormalTree = R"({
        "noRotate": false, "layoutMode": "sun",
        "schools": {
            "Alpha": { "root": "0x00000001", "spokeAngle": 0, "nodes": [
                {"formId": "0x00000001", "name": "R", "x": 0, "y": 0, "children": ["0x00000002", "0x00000005"]},
                {"formId": "0x00000002", "name": "A", "x": 10, "y": 0, "prerequisites": ["0x00000001", "0xDEAD"],
                 "hardPrereqs": ["0x00000001"], "softPrereqs": ["0x00000001", "junk"], "softNeeded": 1, "tier": 1},
                {"formId": "0x00000003", "name": "B", "x": 20, "y": 0, "prerequisites": ["0x00000002"],
                 "locks": [{"nodeId": "0x00000004", "score": 0, "revealed": false}]},
                {"formId": "0x00000004", "name": "L", "x": 20, "y": 10},
                {"formId": "0x00000005", "name": "D", "x": 0, "y": 300}
            ]},
            "Beta": { "root": "0x00000002_dup_1", "nodes": [
                {"formId": "0x00000002_dup_1", "name": "A copy", "x": 500, "y": 500}
            ]}
        }
    })";

    void TestNormal()
    {
        std::printf("normal: a small tree\n");
        std::string error;
        const auto tree = TreeData::Parse(kNormalTree, &error);
        Expect(tree.has_value(), "parses");
        if (!tree) {
            std::printf("    error: %s\n", error.c_str());
            return;
        }
        Expect(tree->nodes.size() == 6 && tree->schools.size() == 2, "6 nodes in 2 schools");
        Expect(tree->structuralEdges == 3, "3 structural edges (R->A from both lists counted once, A->B, R->D from children)");
        Expect(tree->lockEdges == 1, "1 lock edge (L -> B)");
        Expect(tree->unparsedIds == 1, "1 unparsed id (0xDEAD)");
        Expect(tree->nodesWithoutXY == 0, "every node has x/y");

        const auto& alpha = tree->schools[0];
        const auto& beta = tree->schools[1];
        Expect(alpha.name == "Alpha" && alpha.rootIndex == 0, "Alpha's root is R");
        Expect(beta.rootIndex == 5, "Beta's root is the dup copy, by its exact id");
        Expect(tree->nodes[5].formId == 0x2, "the dup id parses to 0x2");
        Expect(tree->nodes[1].hard.size() == 1 && tree->nodes[1].soft.empty() && tree->nodes[1].softNeeded == 0,
            "hard/soft lists drop a non-hex entry; R in both lists stays in hard only, softNeeded clamps to 0");
        Expect(TreeData::Find(*tree, 0x2) == 1, "Find prefers the non-dup node");
        Expect(TreeData::Find(*tree, 0x2, "Beta") == 5, "Find prefers the node in the school asked for");
        Expect(TreeData::Find(*tree, 0x99) == -1, "Find of an unknown FormID is -1");

        TreeData::View view;
        view.scale = 2.0;
        view.halfW = 100.0;
        view.halfH = 100.0;
        view.maxSteps = 2;
        view.maxNodes = 40;
        const auto window = TreeData::Query(*tree, 0, view);
        // From R, unturned at scale 2: A (20,0), B (40,0), L (40,20) inside; D (0,600) outside; the Beta copy is another school
        Expect(window.candidatesBeforeCap == 4 && window.nodes.size() == 4, "R's window: R, A, B and L (D outside, the Beta copy not its school)");
        Expect(!window.nodes.empty() && window.nodes[0].index == 0 && window.nodes[0].steps == 0, "the centre comes first, 0 steps");
        const auto* a = Placed(window, 1);
        const auto* b = Placed(window, 2);
        const auto* l = Placed(window, 3);
        Expect(a && Near(a->sx, 20.0) && Near(a->sy, 0.0), "A lands straight right of R at 10 * scale 2 (+x stays +x: no turn)");
        Expect(a && b && window.nodes[1].index == 1 && window.nodes[2].index == 2 && b->sx > a->sx, "nearest first: A, then B further right");
        Expect(l && Near(l->sx, 40.0) && Near(l->sy, 20.0), "L at (20, 10) lands at (40, 20): +y stays down");
        Expect(l && l->steps == -1 && a && a->steps == 1 && b && b->steps == 2,
            "an unlinked node inside the window is drawn: L (3 steps, over maxSteps 2) has steps -1; A 1, B 2");
        Expect(!Placed(window, 4) && !Placed(window, 5), "a node outside the window (D) and one of another school are not drawn");
        // R-A, A-B and the lock L-B, all between drawn nodes; R-D is out (D not drawn)
        Expect(window.edges.size() == 3, "3 edges between drawn nodes: R-A, A-B, L-B");
        Expect(std::ranges::none_of(window.edges, [&](const TreeData::PlacedEdge& a_edge) { return tree->edges[a_edge.edge].to == 4; }),
            "no edge to D (not drawn)");
        Expect(window.outside.size() == 1 && window.outside[0].index == 4 && Near(window.outside[0].sx, 0.0) && Near(window.outside[0].sy, 600.0),
            "R's neighbours outside the window: D only, its offset unclipped (y 300 -> 600 down)");
        const auto badges = LayoutMath::Badges(window.outside);
        Expect(badges.total == 1 && badges.sectors[2] == 1, "its badge: 1 in sector 2 (down)");

        view.maxNodes = 2;
        const auto capped = TreeData::Query(*tree, 0, view);
        Expect(capped.candidatesBeforeCap == 4 && capped.nodes.size() == 2 && capped.nodes[1].index == 1,
            "cap 2 keeps R and the nearest (A)");
        view.keep = { 2 };
        const auto kept = TreeData::Query(*tree, 0, view);
        Expect(kept.nodes.size() == 2 && kept.nodes[1].index == 2, "the target survives the cap: keep B, cap 2 -> R, B");
        Expect(kept.neighboursCapped == 1 && kept.outside.size() == 1,
            "A (inside, over the cap) counts as capped, not outside: no badge for it");
        // review v10: a cap under the centre plus the kept ones is raised to them (selected B, target L)
        view.keep = { 2, 3 };
        for (const int cap : { 1, 2, 3 }) {
            view.maxNodes = cap;
            const auto small = TreeData::Query(*tree, 0, view);
            char what[128];
            std::snprintf(what, sizeof(what), "cap %d with keep B, L: R, B and L all drawn, A capped (%zu drawn)", cap, small.nodes.size());
            Expect(small.nodes.size() == 3 && small.nodes[0].index == 0 && Placed(small, 2) && Placed(small, 3) && !Placed(small, 1), what);
        }
        view.maxNodes = 4;
        const auto roomy = TreeData::Query(*tree, 0, view);
        Expect(roomy.nodes.size() == 4 && roomy.nodes[1].index == 2 && roomy.nodes[2].index == 3 && roomy.nodes[3].index == 1,
            "cap 4 with keep B, L: R, B, L, then A");
        view.keep.clear();
        view.maxNodes = 40;
        view.margin = 70.0;
        const auto margined = TreeData::Query(*tree, 0, view);
        Expect(margined.nodes.size() == 2 && Placed(margined, 1) && !Placed(margined, 2),
            "margin 70: only |sx|, |sy| <= 30 is inside - A (20 right) drawn, B (40 right) not");
        Expect(TreeData::Query(*tree, 99, view).nodes.empty(), "an out-of-range centre gives an empty window");
    }

    // The badges' contract (spec v10 T7): C's direct neighbours X, Q, Q2 inside; Y (also a lock link: two edges), Y2 and Z
    // outside; W without x/y; V in another school. U inside, not linked. Cap 3.
    constexpr std::string_view kBadgeTree = R"({
        "schools": {
            "S": { "root": "0x00000001", "nodes": [
                {"formId": "0x00000001", "name": "C", "x": 0, "y": 0,
                 "children": ["0x00000002", "0x00000003", "0x00000004", "0x00000005", "0x00000006", "0x00000007", "0x00000008", "0x00000009"]},
                {"formId": "0x00000002", "name": "X", "x": 10, "y": 0},
                {"formId": "0x00000003", "name": "Y", "x": 500, "y": 0, "locks": [{"nodeId": "0x00000001"}]},
                {"formId": "0x00000004", "name": "Y2", "x": 400, "y": 30},
                {"formId": "0x00000005", "name": "Z", "x": 0, "y": -300},
                {"formId": "0x00000006", "name": "W"},
                {"formId": "0x00000007", "name": "Q", "x": 50, "y": 0},
                {"formId": "0x00000008", "name": "Q2", "x": 60, "y": 0},
                {"formId": "0x0000000A", "name": "U", "x": -20, "y": 0}
            ]},
            "T": { "nodes": [
                {"formId": "0x00000009", "name": "V", "x": 5, "y": 5}
            ]}
        }
    })";

    void TestViewport()
    {
        std::printf("viewport: badges, cap, other schools (spec v10 T3, T7)\n");
        std::string error;
        const auto tree = TreeData::Parse(kBadgeTree, &error);
        Expect(tree.has_value() && tree->nodes.size() == 10, "parses, 10 nodes");
        if (!tree || tree->nodes.size() != 10) {
            std::printf("    error: %s\n", error.c_str());
            return;
        }
        TreeData::View view;
        view.scale = 1.0;
        view.halfW = 100.0;
        view.halfH = 100.0;
        view.maxSteps = 1;
        view.maxNodes = 3;
        const auto window = TreeData::Query(*tree, 0, view);
        Expect(window.candidatesBeforeCap == 5 && window.nodes.size() == 3 && window.nodes[1].index == 1 && window.nodes[2].index == 8,
            "inside: C, X, U, Q, Q2 (not V: another school); cap 3 keeps C, X and the unlinked U (nearest)");
        const auto* u = Placed(window, 8);
        Expect(u && u->steps == -1, "U is drawn although not linked to C");
        Expect(window.outside.size() == 3, "outside: Y, Y2, Z - Y once although it has two edges to C");
        const auto badges = LayoutMath::Badges(window.outside);
        Expect(badges.total == 3 && badges.sectors[0] == 2 && badges.sectors[6] == 1, "badges: 2 right (Y, Y2), 1 up (Z)");
        Expect(window.neighboursCapped == 2 && window.neighboursNoXY == 1 && window.neighboursOtherSchool == 1,
            "apart, not in the badges: Q and Q2 capped, W without x/y, V another school");
        view.keep = { 7 };
        const auto kept = TreeData::Query(*tree, 0, view);
        Expect(kept.nodes.size() == 3 && kept.nodes[1].index == 7 && kept.nodes[2].index == 1, "keep Q2 (the target): C, Q2, then the nearest X");

        // The spacing at the plugin's defaults (spec v11 R5): the largest drawn reach - a selected learnable spell's ring,
        // (9 + 1.5 + 4) x fTreeScale 0.75 x fTreeMarkScale 1 + its half line 0.6 x fTreeScale 0.75 (review v11 #5) - stays under half the closest pair of
        // spell_tree.json (37.9 tree units -> 28.4, half 14.21); a pair 30 apart (half 11.25) would not
        const Settings::Values defaults;
        const double reach = DefaultReach();
        Expect(Near(defaults.treeMarkScale, 1.0) && Near(reach, 14.5 * 0.75 + 0.6 * 0.75), "the largest reach at the defaults: 11.325 (fTreeMarkScale 1)");
        Expect(reach <= 37.9 * defaults.treeScale / 2 && reach > 30.0 * defaults.treeScale / 2,
            "37.9 tree units apart: 11.325 <= 14.21 PASS; 30 apart (11.25) would FAIL");
    }

    // The learning target's path (spec v10 T8): C -> {A, B} -> T by structural links, A and B tie; L reaches T by a lock only;
    // O in school Q (schools are read in name order: P first, so C is index 0)
    constexpr std::string_view kPathTree = R"({
        "schools": {
            "P": { "root": "0x00000001", "nodes": [
                {"formId": "0x00000001", "name": "C", "x": 0, "y": 0, "children": ["0x00000003", "0x00000002"]},
                {"formId": "0x00000002", "name": "A", "x": 10, "y": 0, "children": ["0x00000004"]},
                {"formId": "0x00000003", "name": "B", "x": 0, "y": 10, "children": ["0x00000004"]},
                {"formId": "0x00000004", "name": "T", "x": 10, "y": 10},
                {"formId": "0x00000005", "name": "L", "x": 20, "y": 20, "locks": [{"nodeId": "0x00000004"}]}
            ]},
            "Q": { "nodes": [ {"formId": "0x00000006", "name": "O", "x": 0, "y": 0} ]}
        }
    })";

    void TestPath()
    {
        std::printf("target path: shortest structural path, ties by node index (spec v10 T8)\n");
        const auto tree = TreeData::Parse(kPathTree);
        Expect(tree.has_value() && tree->nodes.size() == 6, "parses, 6 nodes");
        if (!tree || tree->nodes.size() != 6) {
            return;
        }
        Expect(TreeData::StructuralPath(*tree, 0, 3) == std::vector<int>{ 0, 1, 3 }, "C -> T: through A (index 1), not B (index 2)");
        Expect(TreeData::StructuralPath(*tree, 3, 0) == std::vector<int>{ 3, 1, 0 }, "T -> C: links go either way");
        Expect(TreeData::StructuralPath(*tree, 0, 0) == std::vector<int>{ 0 }, "C -> C: the node alone");
        Expect(TreeData::StructuralPath(*tree, 0, 4).empty(), "C -> L: only a lock link reaches L, no structural path");
        Expect(TreeData::StructuralPath(*tree, 0, 5).empty() && TreeData::StructuralPath(*tree, 0, 99).empty(),
            "another school's node or an index out of range: no path");
    }

    // A non-square window, unturned (spec v11 T10 / R12): C at (100, 50) and its children, raw +x right and +y down. The window
    // is 100 x 30 half size at scale 1: P (+60, 0) inside; Q (0, +40) below the window; S (0, -35) above it; T (+150, 0) past
    // the right edge; E (+50, +50) inside across but not down; U (-90, +25), not linked, inside
    constexpr std::string_view kWideTree = R"({
        "schools": {
            "W": { "root": "0x00000001", "nodes": [
                {"formId": "0x00000001", "name": "C", "x": 100, "y": 50,
                 "children": ["0x00000002", "0x00000003", "0x00000004", "0x00000005", "0x00000006"]},
                {"formId": "0x00000002", "name": "P", "x": 160, "y": 50},
                {"formId": "0x00000003", "name": "Q", "x": 100, "y": 90},
                {"formId": "0x00000004", "name": "S", "x": 100, "y": 15},
                {"formId": "0x00000005", "name": "T", "x": 250, "y": 50},
                {"formId": "0x00000006", "name": "E", "x": 150, "y": 100},
                {"formId": "0x00000007", "name": "U", "x": 10, "y": 75}
            ]}
        }
    })";

    void TestWideWindow()
    {
        std::printf("non-square window, unturned (spec v11 T10 / R12)\n");
        const auto tree = TreeData::Parse(kWideTree);
        Expect(tree.has_value() && tree->nodes.size() == 7, "parses, 7 nodes");
        if (!tree || tree->nodes.size() != 7) {
            return;
        }
        TreeData::View view;
        view.scale = 1.0;
        view.halfW = 100.0;
        view.halfH = 30.0;
        view.maxNodes = 40;
        const auto window = TreeData::Query(*tree, 0, view);
        const auto* p = Placed(window, 1);
        const auto* u = Placed(window, 6);
        Expect(p && Near(p->sx, 60.0) && Near(p->sy, 0.0), "P (+60 x) lands at (60, 0): +x is right");
        Expect(u && Near(u->sx, -90.0) && Near(u->sy, 25.0), "U (-90 x, +25 y) lands at (-90, 25): inside the wide window, +y is down");
        Expect(window.nodes.size() == 3 && !Placed(window, 2) && !Placed(window, 3) && !Placed(window, 4) && !Placed(window, 5),
            "inside: C, P, U; out: Q (40 > 30 down), S (35 > 30 up), T (150 > 100 right), E (50 > 30 down although 50 < 100 across)");
        Expect(window.edges.size() == 1 && Near(window.edges[0].ax, 0.0) && Near(window.edges[0].ay, 0.0) && Near(window.edges[0].bx, 60.0) &&
                   Near(window.edges[0].by, 0.0),
            "one edge drawn, C-P, its ends at the raw offsets (0, 0) and (60, 0)");
        const auto badges = LayoutMath::Badges(window.outside);
        Expect(window.outside.size() == 4 && badges.total == 4 && badges.sectors[2] == 1 && badges.sectors[6] == 1 && badges.sectors[0] == 1 &&
                   badges.sectors[1] == 1,
            "badges by raw direction: Q down (2), S up (6), T right (0), E down-right (1)");
        view.halfW = 30.0;
        view.halfH = 100.0;
        const auto tall = TreeData::Query(*tree, 0, view);
        Expect(tall.nodes.size() == 3 && Placed(tall, 2) && Placed(tall, 3) && !Placed(tall, 1) && !Placed(tall, 6) && !Placed(tall, 5),
            "the same tree in a tall window (30 x 100): Q and S inside, P, U and E (50 > 30 across) out");
    }

    // The full-opacity set (spec v11 T11 / R11): C's structural links either way are full - A its child, B its parent (C lists
    // B as a prerequisite: the edge runs B -> C); L reaches C by a lock only (dim, though Query's link steps count it); A2 is
    // two links away (dim)
    constexpr std::string_view kOpacityTree = R"({
        "schools": {
            "O": { "root": "0x00000002", "nodes": [
                {"formId": "0x00000001", "name": "C", "x": 0, "y": 0, "prerequisites": ["0x00000002"], "children": ["0x00000003"],
                 "locks": [{"nodeId": "0x00000004"}]},
                {"formId": "0x00000002", "name": "B", "x": -10, "y": 0},
                {"formId": "0x00000003", "name": "A", "x": 10, "y": 0, "children": ["0x00000005"]},
                {"formId": "0x00000004", "name": "L", "x": 0, "y": 10},
                {"formId": "0x00000005", "name": "A2", "x": 20, "y": 0}
            ]}
        }
    })";

    void TestOpacitySet()
    {
        std::printf("full-opacity set: the centre and its structural neighbours (spec v11 T11 / R11)\n");
        const auto tree = TreeData::Parse(kOpacityTree);
        Expect(tree.has_value() && tree->nodes.size() == 5 && tree->lockEdges == 1, "parses, 5 nodes, 1 lock edge");
        if (!tree || tree->nodes.size() != 5) {
            return;
        }
        const auto full = TreeData::StructuralNeighbours(*tree, 0);
        Expect(full == std::vector<int>{ 0, 1, 2 }, "C's set: C, B (its parent: a reverse structural link is full), A (its child)");
        Expect(std::ranges::find(full, 3) == full.end(), "L, linked by a lock only, is not in it (dim)");
        Expect(std::ranges::find(full, 4) == full.end(), "A2, two structural links away, is not in it (dim)");
        TreeData::View view;
        view.scale = 1.0;
        view.halfW = 100.0;
        view.halfH = 100.0;
        view.maxSteps = 1;
        const auto window = TreeData::Query(*tree, 0, view);
        const auto* l = Placed(window, 3);
        Expect(l && l->steps == 1, "while Query's link steps (clickability) do count the lock: L 1 step - the two rules stay apart");
        Expect(TreeData::StructuralNeighbours(*tree, 4) == std::vector<int>{ 4, 2 }, "A2's set: A2 and A");
        Expect(TreeData::StructuralNeighbours(*tree, 99).empty() && TreeData::StructuralNeighbours(*tree, -1).empty(), "an index out of range: empty");
    }

    void TestBoundary()
    {
        std::printf("boundary: BOM, a school with no nodes, a node without x/y\n");
        const std::string text = "\xEF\xBB\xBF"
                                 R"({"layoutMode":"flat","schools":{"Empty":{"spokeAngle":30,"nodes":[]},
                                     "Lone":{"spokeAngle":30,"nodes":[{"formId":"0x10","x":"5","y":null,"isRoot":true}]}}})";
        std::string error;
        const auto tree = TreeData::Parse(text, &error);
        Expect(tree.has_value(), "parses with a BOM");
        if (!tree) {
            std::printf("    error: %s\n", error.c_str());
            return;
        }
        Expect(tree->schools.size() == 2 && tree->schools[0].nodes.empty() && tree->schools[0].rootIndex == -1,
            "the empty school is kept, with no root");
        Expect(tree->nodes.size() == 1 && tree->nodesWithoutXY == 1, "a string x and a null y count as no x/y");
        Expect(tree->schools[1].rootIndex == 0, "the root falls back to isRoot");
        Expect(tree->edges.empty() && tree->unparsedIds == 0, "no edges, nothing unparsed");
        Expect(TreeData::Query(*tree, 0, TreeData::View{}).nodes.empty(), "a centre without x/y gives an empty window");
        const auto empty = TreeData::Parse(R"({"schools":{}})");
        Expect(empty && empty->nodes.empty() && empty->schools.empty(), "no schools at all: an empty tree");
    }

    // HoM's prerequisite rules (spelllearning/include/TreePrereqEntries.h): legacy promotion, roots, repeats
    constexpr std::string_view kPrereqTree = R"({
        "schools": {
            "P": { "root": "0x00000001", "nodes": [
                {"formId": "0x00000001", "name": "R", "x": 0, "y": 0, "hardPrereqs": ["0x00000009"]},
                {"formId": "0x00000009", "name": "Q", "x": 5, "y": 0, "isRoot": true, "prerequisites": ["0x00000001"]},
                {"formId": "0x00000002", "name": "A", "x": 10, "y": 0, "prerequisites": ["0x00000001", "0x00000003", "0x00000004"]},
                {"formId": "0x00000003", "name": "B", "x": 20, "y": 0, "prerequisites": ["0x00000001"]},
                {"formId": "0x00000004", "name": "C", "x": 30, "y": 0, "hardPrereqs": ["0x00000001", "0x00000001", "0x00000004"],
                 "softPrereqs": ["0x00000001", "0x00000002", "0x00000002", "0x00000003"], "softNeeded": 5,
                 "locks": [{"nodeId": "0x00000001"}]},
                {"formId": "0x00000005", "name": "D", "x": 40, "y": 0, "prerequisites": ["0x00000001"], "softPrereqs": ["0x00000002"]}
            ]}
        }
    })";

    void TestPrereqs()
    {
        std::printf("prerequisites: normalised as HoM evaluates them\n");
        std::string error;
        const auto tree = TreeData::Parse(kPrereqTree, &error);
        Expect(tree.has_value() && tree->nodes.size() == 6, "parses, 6 nodes");
        if (!tree || tree->nodes.size() != 6) {
            std::printf("    error: %s\n", error.c_str());
            return;
        }
        const auto& n = tree->nodes;
        using Ids = std::vector<std::uint32_t>;
        Expect(n[0].hard.empty() && n[0].soft.empty() && n[0].softNeeded == 0, "the school's root (by its id) has no requirements");
        Expect(n[1].hard.empty() && n[1].soft.empty(), "an isRoot node has no requirements, its prerequisites dropped");
        Expect(n[2].hard == Ids{ 0x1 } && n[2].soft == (Ids{ 0x3, 0x4 }) && n[2].softNeeded == 1,
            "legacy: 3 prerequisites -> the first hard, the rest soft, 1 needed");
        Expect(n[3].hard == Ids{ 0x1 } && n[3].soft.empty() && n[3].softNeeded == 0, "legacy: 1 prerequisite -> hard");
        Expect(n[4].hard == Ids{ 0x1 } && n[4].soft == (Ids{ 0x2, 0x3 }) && n[4].softNeeded == 2,
            "repeats and its own id dropped, R kept in hard only (a lock: softNeeded 4 - 1), softNeeded clamped to 2");
        Expect(n[5].hard.empty() && n[5].soft == Ids{ 0x2 } && n[5].softNeeded == 0,
            "a soft list present: no legacy promotion, softNeeded 0 stays");
    }

    void TestMalformed()
    {
        std::printf("failure: malformed input\n");
        std::string error;
        Expect(!TreeData::Parse(R"({"schools": {"A": {"nodes": [)", &error) && !error.empty(),
            "cut-off JSON: nullopt with a reason");
        std::printf("    reason: %s\n", error.c_str());
        error.clear();
        Expect(!TreeData::Parse("[1, 2]", &error) && !error.empty(), "an array at the top: nullopt with a reason");
        error.clear();
        Expect(!TreeData::Parse(R"({"version":"1.0"})", &error) && !error.empty(), "no schools: nullopt with a reason");
        error.clear();
        Expect(!TreeData::Load("Z:/no/such/spell_tree.json", &error) && !error.empty(), "a missing file: nullopt with a reason");
    }

    // spec v12 S1 / G1: SkyUI's list events -> a selection, a hover or nothing (SelectionRule.h)
    void TestSelectionRule()
    {
        using namespace SelectionRule;
        std::printf("selection rule (spec v12 G1)\n");
        // made-up rows: row i holds FormID 0x100 + i unless a test moves one; the committed entry is row 3 (0x103)
        constexpr std::uint32_t kBase = 0x100;
        const auto id = [](int a_index) { return a_index < 0 ? 0u : kBase + static_cast<std::uint32_t>(a_index); };
        const auto change = [&](int a_index, std::optional<int> a_flag, int a_committed = 3) {
            Input in;
            in.index = a_index;
            in.keyboardOrMouse = a_flag;
            in.sameEntry = SameEntry(id(a_index), a_index, id(a_committed), a_committed);
            return Decide(in);
        };
        const auto press = [&](Event a_event, int a_index, std::optional<int> a_flag, int a_committed, bool a_elsewhere = false) {
            Input in;
            in.event = a_event;
            in.index = a_index;
            in.keyboardOrMouse = a_flag;
            in.sameEntry = SameEntry(id(a_index), a_index, id(a_committed), a_committed);
            in.cardElsewhere = a_elsewhere;
            return Decide(in);
        };
        // normal
        auto r = change(5, kSelectKeyboard);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kKey, "a keyboard/gamepad move (SELECT_KEYBOARD) is a selection by key");
        r = change(5, kSelectMouse);
        Expect(r.verdict == Verdict::kHover, "a mouse roll-over (SELECT_MOUSE, not ours) is a hover");
        r = press(Event::kPress, 5, kSelectMouse, 3);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kClick, "a left click on another row is a selection by click");
        r = press(Event::kPressAux, 5, kSelectMouse, 3);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kClick, "a right click (itemPressAux) is a selection by click");
        // boundary
        r = change(-1, kSelectMouse);
        Expect(r.verdict == Verdict::kKeep, "-1 from PageUp/PageDown (SELECT_MOUSE) keeps the selection, not a hover");
        r = change(-1, kSelectKeyboard);
        Expect(r.verdict == Verdict::kKeep, "-1 with any flag keeps the selection");
        Input ours;
        ours.index = 7;
        ours.keyboardOrMouse = kSelectMouse;  // the selectedIndex setter is SELECT_MOUSE (BasicList.as:106-108)
        ours.ours = Mark{ Source::kTree, 7 };
        r = Decide(ours);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kTree, "our own marked set (SELECT_MOUSE) is a selection by tree, not a hover");
        ours.index = 9;  // the setter scrolled; UpdateList re-selected the row under the cursor (ScrollingList.as:190-193)
        Expect(Decide(ours).verdict == Verdict::kHover, "a nested mouse change of another row during our set is judged on its own (a hover)");
        ours.index = -1;
        ours.ours = Mark{ Source::kTree, -1 };
        Expect(Decide(ours).verdict == Verdict::kKeep, "our own marked set to -1 keeps the selection");
        // re-layout: after a sort another entry sits at the committed index 3, the committed one (0x103) moved to 8
        Input sorted;
        sorted.event = Event::kPress;
        sorted.index = 3;
        sorted.keyboardOrMouse = kSelectMouse;
        sorted.sameEntry = SameEntry(0x999, 3, id(3), 3);
        r = Decide(sorted);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kClick, "after a sort, a click on another FormID at the committed index is a selection");
        sorted.index = 8;
        sorted.sameEntry = SameEntry(id(3), 8, id(3), 3);
        Expect(Decide(sorted).verdict == Verdict::kKeep, "after a sort, a click on the committed FormID at its new index changes nothing");
        r = change(3, kSelectMouse);
        Expect(r.verdict == Verdict::kKeep, "a mouse change onto the committed entry (InvalidateData's restore after an equip) is no hover");
        Expect(SameEntry(0, 4, 0, 4) && !SameEntry(0, 4, 0, 5), "entries without a FormID (active effects) compare by index");
        Expect(!SameEntry(id(3), 3, id(3), -1) && !SameEntry(0, -1, 0, -1), "nothing committed or no row is never the same entry");
        r = press(Event::kPress, 3, kSelectMouse, 3);
        Expect(r.verdict == Verdict::kKeep, "a click on the committed row again changes nothing");
        r = press(Event::kPress, 3, kSelectMouse, 3, true);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kClick, "a click on the committed row while a tree card shows is a selection");
        // click after hover on the same row: the hover is swallowed, the press of that row selects it
        const auto hover = change(5, kSelectMouse);
        r = press(Event::kPress, 5, kSelectMouse, 3);
        Expect(hover.verdict == Verdict::kHover && r.verdict == Verdict::kSelect && r.source == Source::kClick,
            "hover row 5 then click row 5: a hover, then a selection by click");
        r = press(Event::kPress, 5, std::nullopt, 3);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kClick, "a press without keyboardOrMouse is a click (not upgraded to a key)");
        r = press(Event::kPress, 5, kSelectKeyboard, 3);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kKey, "Enter on a highlighted row (itemPress, SELECT_KEYBOARD) is a selection by key");
        r = change(3, kSelectKeyboard, 3);
        Expect(r.verdict == Verdict::kSelect, "a keyboard move back onto the committed row is passed on (the game's card was not moved by hovers)");
        r = change(0, kSelectKeyboard, -1);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kKey, "the first key move with nothing committed (-1) selects row 0");
        // failure
        r = change(5, std::nullopt);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kOther,
            "a selectionChange without keyboardOrMouse (another mod's) is passed on as build 11 did, not swallowed");
        r = change(5, 7);
        Expect(r.verdict == Verdict::kSelect && r.source == Source::kOther, "an unknown keyboardOrMouse value (7) is passed on, not swallowed");
        r = press(Event::kPress, -1, kSelectMouse, 3);
        Expect(r.verdict == Verdict::kKeep, "a press of -1 (BasicList.as:298 never sends it) changes nothing");
        Expect(std::string_view(Name(Source::kClick)) == "click" && std::string_view(Name(Source::kKey)) == "key" &&
                   std::string_view(Name(Source::kTree)) == "tree" && std::string_view(Name(Source::kOpen)) == "open",
            "the S4 log names: click, key, tree, open");

        // review v12 #S2: Commit's change test
        Expect(!CommitChanges(true, true, false, false), "the same row and form again, nothing else shown: no change");
        Expect(CommitChanges(true, true, false, true), "back from a tree card to the same row (same FormID, same index): a change (selSeq bumps)");
        Expect(CommitChanges(true, false, false, false) && CommitChanges(false, true, false, false), "another row or another form: a change");
        Expect(CommitChanges(true, true, true, false), "forced: a change");

        // review v12 #S1: ItemMenu.onItemHighlightChange
        const auto highlight = [](int a_index, bool a_ours, bool a_committed, bool a_same, bool a_elsewhere = false) {
            HighlightInput in;
            in.index = a_index;
            in.ours = a_ours;
            in.committed = a_committed;
            in.sameEntry = a_same;
            in.cardElsewhere = a_elsewhere;
            return DecideHighlight(in);
        };
        Expect(highlight(5, false, true, true) == Highlight::kPass, "the committed entry's highlight (InvalidateListData after an equip of it): passed");
        Expect(highlight(7, false, true, false) == Highlight::kSwallow, "another row's highlight (InvalidateListData with the mouse on row 7): swallowed");
        Expect(highlight(7, true, true, false) == Highlight::kPass, "our own (inside the saved original / our dispatch): passed");
        Expect(highlight(-1, false, true, false) == Highlight::kSwallow, "-1 from a category change with a selection: swallowed (S3 puts it back)");
        Expect(highlight(-1, false, false, false) == Highlight::kPass, "-1 with nothing selected (menu open): passed, vanilla's no-selection path");
        Expect(highlight(-1, true, true, false) == Highlight::kPass, "our own -1: passed");
        Expect(highlight(5, false, true, true, true) == Highlight::kSwallow, "the committed row's highlight while a tree card is the selection: swallowed");
        Expect(highlight(-1, false, true, false, true) == Highlight::kSwallow, "-1 while only a tree card is up: swallowed (the tree card stays)");
    }

    int RunSelfTest()
    {
        TestSelectionRule();
        TestNormal();
        TestBoundary();
        TestPrereqs();
        TestViewport();
        TestPath();
        TestWideWindow();
        TestOpacitySet();
        TestMalformed();
        std::printf(g_failures == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_failures);
        return g_failures == 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    const std::string_view arg = a_argc > 1 ? a_argv[1] : "";
    if (arg == "--selftest") {
        return RunSelfTest();
    }
    const std::string path = arg.empty() ? kDefaultTree : std::string(arg);
    std::string error;
    const auto tree = TreeData::Load(path, &error);
    if (!tree) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return kExitBadInput;
    }
    std::printf("file: %s\n", path.c_str());
    PrintCounts(*tree);
    const bool spaced = PrintWindowStats(*tree);
    return spaced ? kExitOk : kExitFail;
}
