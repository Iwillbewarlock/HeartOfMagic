#pragma once

// =============================================================================
// TREE DATA - the saved Heart of Magic spell tree, read for the magic menu's
// tree window. It reads spell_tree.json directly (a planned HoM-side contract, I1
// in design section 4, GetTreeWindow, would replace this). Pure data: no GFx and no game
// calls (FormID -> form lookup happens in the caller), so the offline check tool
// can use it too.
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TreeData
{
    enum class EdgeKind : std::uint8_t
    {
        kStructural,  // the tree line: a node's "prerequisites" / "children"
        kLock         // a PreReq Master lock: "locks"[].nodeId (a hard prerequisite drawn as a chain)
    };

    struct Node
    {
        std::string id;          // the saved "formId" text ("0x00012FCD"; may carry "_dup_<n>")
        std::uint32_t formId{};  // parsed hex of id without any "_dup_" suffix (0 if it does not parse)
        std::string persistentId;  // "Plugin.esp|0x000ABC" (may be empty)
        std::string school;
        std::string name;
        std::string skillLevel;  // "Novice" .. "Master" as saved (may be empty)
        std::string themeColor;  // the node's own colour as saved (HoM's node.themeColor, canvasRendererSpell.js:71); "" = none
        int tier{};
        double x{}, y{};  // world (canvas, y down)
        bool hasXY{};
        bool isRoot{};
        // The prerequisites as HoM evaluates them (TreePrereqEntries.h rules, see NormalisePrereqs in TreeData.cpp):
        // legacy "prerequisites" promoted, none for a root, no repeats, an id in at most one list, softNeeded 0..soft
        std::vector<std::uint32_t> hard;  // all of these mastered (FormIDs)
        std::vector<std::uint32_t> soft;  // softNeeded of these mastered (FormIDs)
        int softNeeded{};
    };

    struct Edge
    {
        int from{};  // node index (the prerequisite / parent)
        int to{};    // node index (the later spell)
        EdgeKind kind{};
    };

    struct School
    {
        std::string name;
        std::optional<double> spokeDeg;  // "spokeAngle" (degrees)
        int rootIndex = -1;
        std::vector<int> nodes;  // indexes into Tree::nodes
    };

    struct Tree
    {
        std::vector<Node> nodes;
        std::vector<Edge> edges;
        std::vector<School> schools;
        bool noRotate{};
        std::string layoutMode;
        std::unordered_map<std::uint32_t, std::vector<int>> byFormId;  // a FormID can appear more than once
        std::unordered_map<std::string, int> byId;                      // saved id text -> index
        std::vector<std::vector<int>> adjacent;                          // per node: edge indexes touching it

        // counts for the log
        int nodesWithoutXY{};
        int structuralEdges{};
        int lockEdges{};
        int unparsedIds{};
    };

    /// Parses the file's text (UTF-8, an optional BOM is skipped). nullopt + a reason on failure.
    std::optional<Tree> Parse(std::string_view a_json, std::string* a_error = nullptr);

    /// Reads and parses a file by path.
    std::optional<Tree> Load(const std::string& a_path, std::string* a_error = nullptr);

    struct View
    {
        double centerX{}, centerY{};  // world point shown at the window centre
        double scale = 1.0;           // stage units per world unit (no rotation: spec v11 T10 / R12)
        double halfW{}, halfH{};      // the window's half size in stage units (inside the border)
        double margin{};              // a node is inside when |sx| <= halfW - margin and |sy| <= halfH - margin (its mark's reach)
        int maxSteps = 2;             // link steps (either kind and direction) that count as linked to the centre
        int maxNodes = 40;
        std::vector<int> keep;        // kept first under the cap when inside the window (the selected node, the learning target)
    };

    struct PlacedNode
    {
        int index{};
        double sx{}, sy{};  // stage offset from the window centre (y down)
        int steps = -1;     // link steps from the centre, -1 when more than maxSteps (not linked)
    };

    struct PlacedEdge
    {
        int edge{};          // index into Tree::edges
        double ax{}, ay{};   // stage offsets of both ends (both drawn)
        double bx{}, by{};
    };

    struct Window
    {
        std::vector<PlacedNode> nodes;    // centre first, then the kept ones, then nearest first
        std::vector<PlacedEdge> edges;    // edges between two drawn nodes
        int candidatesBeforeCap{};        // the school's nodes inside the window, before maxNodes
        std::vector<PlacedNode> outside;  // the centre's direct neighbours of its school outside the window (offsets not clipped)
        int neighboursCapped{};           // the centre's direct neighbours inside the window but over maxNodes
        int neighboursNoXY{};             // ... without x/y
        int neighboursOtherSchool{};      // ... of another school
    };

    /// The window around a_center (node index), a viewport on the saved layout (spec v10 T3): every node of the
    /// centre's school that lands inside the window at the saved x/y, scaled and not turned (spec v11 T10 / R12: HoM's
    /// wheel rotation is 0 by default and focusRotate=false leaves it so), linked or not; capped at maxNodes by
    /// distance, the centre first and a_view.keep next. a_view.centerX/Y are ignored and taken from the centre node.
    Window Query(const Tree& a_tree, int a_center, View a_view);

    /// The shortest path a_from -> a_to over structural edges (either direction) between nodes of a_from's school,
    /// as node indexes from a_from to a_to; a tie goes to the lower node index. Empty when there is none.
    std::vector<int> StructuralPath(const Tree& a_tree, int a_from, int a_to);

    /// The centre and every node one STRUCTURAL edge away from it, either direction (spec v11 T11 / R11: the full-opacity
    /// set; lock edges do not count, unlike Query's link steps), each once, the centre first then in index order. Empty
    /// for an index out of range.
    std::vector<int> StructuralNeighbours(const Tree& a_tree, int a_center);

    /// The first node index for a FormID, preferring a_school's node and a non-dup node; -1 if none.
    int Find(const Tree& a_tree, std::uint32_t a_formId, std::string_view a_school = {});
}
