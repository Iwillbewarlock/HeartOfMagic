#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================================================================
// Internal types shared by LayoutDeclutter*.cpp and LayoutLine*.cpp.
// NOT part of the public API (see LayoutDeclutter.h).
//
// Everything mirrors the JavaScript (modules/layoutDeclutter.js,
// layoutLineClear.js, layoutLineGrid.js) field for field, so the two can be
// read side by side; the JS names are given where they differ.
// =============================================================================

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // CONSTANTS (the JavaScript's, see there for what each is for)
    // =========================================================================

    inline constexpr double kPi = 3.141592653589793;  // Math.PI

    // LayoutDeclutter
    inline constexpr double kNodeRadius = 16;
    inline constexpr double kGap = 6;
    inline constexpr double kTouchRadius = 12;
    inline constexpr double kSpread = 1.35;
    inline constexpr double kLineClear = 20;
    inline constexpr double kHeartClearance = 50;
    inline constexpr double kDefaultGlobeRadius = 45;
    inline constexpr int kIterations = 60;
    inline constexpr double kMaxStep = 10;
    inline constexpr double kDoneBelow = 0.25;
    inline constexpr double kGoldenAngle = 2.39996;

    // LayoutLineClear
    inline constexpr double kRadii[] = { 10, 20, 32, 46, 64 };
    inline constexpr int kRings = 5;
    inline constexpr int kDirections = 12;
    inline constexpr int kPasses = 6;
    inline constexpr double kMoveCost = 0.01;
    inline constexpr double kOverlapCost = 6;
    inline constexpr double kOverlapBase = 2;
    inline constexpr double kHeartCost = 5;
    inline constexpr double kMinAngle = 30 * kPi / 180;
    inline constexpr double kAngleCost = 3;
    inline constexpr double kLongLine = 150;
    inline constexpr double kReachLine = 200;
    inline constexpr double kMaxReachScale = 3;
    inline constexpr double kLineGap = 11;
    inline constexpr double kMinCross = 15 * kPi / 180;
    inline constexpr double kLineGapCost = 1;
    inline constexpr double kBundleAngle = 20 * kPi / 180;
    inline constexpr double kBundleLen = 250;
    inline constexpr double kBundleMax = 3;
    inline constexpr double kGoodEnough = 0.5;
    inline constexpr double kCell = 50;
    inline constexpr double kEndMargin = 0.05;
    inline constexpr double kBetterBy = 0.01;          // a spot must beat the best by this
    inline constexpr double kOverlapHair = 1.000001;   // min squared, a hair over
    inline constexpr double kHalfCellDiagonal = 0.7072;
    inline constexpr double kRoundingHair = 0.001;
    inline constexpr double kTinyLength2 = 0.0001;     // a line shorter than this (squared) is no line
    inline constexpr double kTinyDistance = 0.001;

    inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

    // =========================================================================
    // TYPES
    // =========================================================================

    struct Sector
    {
        double mid = 0;
        double half = 0;
    };

    struct Heart
    {
        double x = 0;
        double y = 0;
        double r = 0;
    };

    // One positioned spell (the JS item)
    struct Item
    {
        std::string formId;
        std::vector<std::string> children;
        double nodeX = 0;  // n.x, n.y: where the layout put it, before SPREAD
        double nodeY = 0;
        double x = 0;
        double y = 0;
        bool fixed = false;
        bool hasSector = false;
        Sector sector;
        int index = 0;
        // Declutter round
        double dx = 0;
        double dy = 0;
        // Line search
        double ox = 0;
        double oy = 0;
        int v = 0;          // _v: moves so far (Edge's cache key)
        std::int64_t cell = 0;
        double reach = 0;   // _reach; 0 = not set yet
    };

    // One line, parent to child (the JS [itemA, itemB] pair and its cache)
    struct Edge
    {
        int a = 0;
        int b = 0;
        int v0 = -1;
        int v1 = -1;
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        double ex = 0, ey = 0, l2 = 0, ang = 0;
        std::uint64_t stamp = 0;
    };

    using CellKey = std::int64_t;
    using Grid = std::unordered_map<CellKey, std::vector<int>>;

    // A cell's key; any two cells get different keys (as the JS _cellKey)
    inline CellKey MakeCellKey(std::int64_t cx, std::int64_t cy)
    {
        return static_cast<CellKey>((static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cy) & 0xFFFFFFFFull));
    }

    inline std::int64_t FloorCell(double v, double cell)
    {
        return static_cast<std::int64_t>(std::floor(v / cell));
    }

    double AngleDiff(double a, double b);
    bool InSector(const Sector& sector, double x, double y);

    // =========================================================================
    // LINE SEARCH (layoutLineClear.js + layoutLineGrid.js)
    // =========================================================================

    class LineClear
    {
    public:
        LineClear(std::vector<Item>& list, std::vector<Edge>& edges, const Heart& heart, double clear, double minDist);

        void Run();
        int Moved() const { return static_cast<int>(m_movedIds.size()); }
        int Passes() const { return m_pass; }
        int CountLinesThrough();

    private:
        // LayoutLineClear.cpp
        bool SearchOne(int it, std::vector<char>& next);
        double ReachScale(int it) const;
        void MarkAround(int it, std::vector<char>& marks);
        double SegDist2(double px, double py, double ax, double ay, double bx, double by) const;
        int Other(int edge, int it) const;

        // LayoutLineClearCost.cpp
        double Cost(int it, double x, double y, double limit);
        double LineGapCost(int it, double x, double y, double limit);
        double BundleGap(double ax, double ay, double bx, double by, const Edge& e, double gap) const;
        double AngleCost(int it, double x, double y);
        void NeighbourLines(int it, int i, std::vector<double>& out) const;
        double Narrow(double a, double b, double len) const;

        // LayoutLineGrid.cpp
        void GridAdd(int it);
        void GridMove(int it, double x, double y);
        void Near(double x0, double y0, double x1, double y1, double pad, std::vector<int>& out) const;
        void CellsAlong(double ax, double ay, double bx, double by, double clear2);
        void NearLine(double ax, double ay, double bx, double by, double clear2, std::vector<int>& out);
        void BuildEdgeGrid();
        void GatherFan(int it);
        void Boxed(const std::vector<int>& lines);
        void EdgesNear(int self, double ax, double ay, double bx, double by, double extra, std::vector<int>& out);
        double RingReach(int it, int r) const;
        void RingSpells(int it);

        std::vector<Item>& m_list;
        std::vector<Edge>& m_edges;
        Heart m_heart;
        double m_clear2 = 0;
        double m_minDist = 0;

        std::vector<std::vector<int>> m_incident;  // item -> its edges
        Grid m_nodeGrid;
        Grid m_edgeGrid;
        std::uint64_t m_stamp = 0;
        std::vector<double> m_offsets;
        std::vector<CellKey> m_keyBuf;
        int m_keyCount = 0;

        // Fans: what one spell's search looks at (valid while m_hasFan)
        bool m_hasFan = false;
        int m_ring = 0;
        std::vector<std::vector<int>> m_fan;
        std::vector<std::vector<int>> m_fanEnd;
        std::vector<std::vector<int>> m_edgeFan;
        std::vector<std::vector<double>> m_angleFan;

        // Scratch (no allocation per spot tried)
        std::vector<double> m_dirs;
        std::vector<double> m_lens;
        std::vector<double> m_theirs;
        std::vector<int> m_scratchNear;
        std::vector<int> m_scratchEdges;
        std::vector<int> m_ringOf;
        std::vector<int> m_sorted;

        // Passes
        std::vector<char> m_dirty;
        std::unordered_set<int> m_movedIds;
        int m_pass = 0;
    };
}
