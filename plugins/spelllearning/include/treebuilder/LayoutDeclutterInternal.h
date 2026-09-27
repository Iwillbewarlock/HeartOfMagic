#pragma once

#include "treebuilder/LayoutDeclutter.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
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
    // A line longer than this (after SPREAD) is left out of the search and the
    // count (LineClear's constructor): every walk along a line is as long as
    // the line, and a real tree's longest is under 2,000 (the JS MAX_LINE)
    inline constexpr double kMaxLine = 20000;
    // Lines at a spell with more lines than this are left out too: its angle
    // cost pairs every two of them at every spot, and each of its neighbours'
    // every line of it (a builder gives a spell a few children; the JS MAX_SPELL_LINES)
    inline constexpr int kMaxSpellLines = 64;
    inline constexpr double kFanMargin = 0.01;       // GapFan's tests: tree units to spare (the JS FAN_MARGIN)
    inline constexpr double kFanAngleMargin = 1e-6;  // ...and radians (FAN_ANGLE_MARGIN)
    inline constexpr double kMinFrame = 1;           // a line shorter than this gets no frame in GapFan
    // A line's fan is cut once this many spots got to its gap part. Speed only (the
    // sums are the same either way), tuned per engine: the JS GAP_FAN_AFTER is 6
    inline constexpr int kGapFanAfter = 2;
    // The line search stops (the spells not yet searched stay where they are)
    // once its work passes this (the JS MAX_WORK; LineClear::m_work says what
    // counts): a safety cap, 2.3 times the heaviest test tree's 0.86e9 (the
    // game's 1,428-spell tree: 0.04e9); 2.3-5.7 ns a unit native (measured)
    inline constexpr std::uint64_t kMaxWork = 2000000000;
    inline constexpr std::uint64_t kCellWork = 11;  // a cell walked counts as this many lines or spells looked at (measured)
    inline constexpr double kBetterBy = 0.01;          // a spot must beat the best by this
    inline constexpr double kOverlapHair = 1.000001;   // min squared, a hair over
    inline constexpr double kHalfCellDiagonal = 0.7072;
    inline constexpr double kRoundingHair = 0.001;
    inline constexpr double kTinyLength2 = 0.0001;     // a line shorter than this (squared) is no line
    inline constexpr double kTinyDistance = 0.001;

    inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

    // BuildEdgeGrid keeps its cells in one array over the lines' box of cells
    // up to this many cells (a real tree: some tens of thousands); past it, by
    // column (each column's cells sorted by row) up to this many columns, and
    // by hash past that (never from a tree within kMaxCoord)
    inline constexpr std::int64_t kMaxDenseCells = std::int64_t{ 1 } << 21;
    inline constexpr std::int64_t kMaxGridColumns = std::int64_t{ 1 } << 20;

    // Input bounds (the JS MAX_COORD, MAX_ANGLE, MAX_CELL). A spell further out
    // than kMaxCoord, or not finite, is not taken (left where it is): a real
    // tree stays within a few thousand units. What the line search walks is
    // bounded by kMaxLine, not by this. A sector whose angle is past kMaxAngle
    // (degrees) is no sector: AngleDiff turns an angle back a turn at a time.
    inline constexpr double kMaxCoord = 1e6;
    inline constexpr double kMaxAngle = 3600;
    // Grid cells are clamped to +-kMaxCell (never reached from kMaxCoord): no
    // out-of-range double is cast, and a cell fits 32 bits for MakeCellKey
    inline constexpr double kMaxCell = 1073741824;  // 2^30
    static_assert(kMaxCell < 2147483648.0, "a cell must fit a signed 32-bit half of a CellKey");

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
    };

    // One line as LineGapCost reads it, copied out of Edge and its spells so
    // the loop over a fan reads one array front to back (the JS record: 17
    // numbers in this order, steep as 1 or 0): its box, its box in GapFan's
    // frame (framed lines only), its ends, its vector, length squared and
    // direction, and whether every spot's line meets it at BUNDLE_ANGLE or
    // more (GapFan)
    struct GapRec
    {
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        double u0 = 0, u1 = 0, w0 = 0, w1 = 0;
        double px = 0, py = 0, qx = 0, qy = 0;
        double ex = 0, ey = 0, l2 = 0, ang = 0;
        bool steep = false;
    };

    using CellKey = std::int64_t;
    using Grid = std::unordered_map<CellKey, std::vector<int>>;

    // A cell's key: cx in the high 32 bits, cy in the low 32. Every cell comes
    // from FloorCell, within +-kMaxCell < 2^31, so both halves fit and no two
    // cells share a key (the JS _cellKey uses a string past its KEY_SPAN instead)
    inline CellKey MakeCellKey(std::int64_t cx, std::int64_t cy)
    {
        return static_cast<CellKey>((static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cy) & 0xFFFFFFFFull));
    }

    // floor(v / cell), clamped to +-kMaxCell before the cast (NaN: -kMaxCell), as the JS _floorCell
    inline std::int64_t FloorCell(double v, double cell)
    {
        const double f = std::floor(v / cell);
        if (!(f >= -kMaxCell)) return -static_cast<std::int64_t>(kMaxCell);
        if (f > kMaxCell) return static_cast<std::int64_t>(kMaxCell);
        return static_cast<std::int64_t>(f);
    }

    // Run's and LineClear's cancel check (a null flag: never cancelled)
    inline void ThrowIfCancelled(const std::atomic<bool>* cancel)
    {
        if (cancel && cancel->load(std::memory_order_relaxed)) throw Cancelled();
    }

    double AngleDiff(double a, double b);
    bool InSector(const Sector& sector, double x, double y);

    // =========================================================================
    // LINE SEARCH (layoutLineClear.js + layoutLineGrid.js)
    // =========================================================================

    class LineClear
    {
    public:
        // cancel (may be null): checked once per spell; set, Run throws Cancelled
        LineClear(std::vector<Item>& list, std::vector<Edge>& edges, const Heart& heart, double clear, double minDist,
            const std::atomic<bool>* cancel = nullptr);

        void Run();
        int Moved() const { return static_cast<int>(m_movedIds.size()); }
        int Passes() const { return m_pass; }
        std::uint64_t Work() const { return m_work; }
        bool Capped() const { return m_capped; }
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
        double BundleGap(double ax, double ay, double len, double ux, double uy, double dir, const GapRec& e, double gap) const;
        GapRec MakeGapRec(const Edge& e) const;
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
        void BuildEdgeColumns(std::int64_t x0, std::int64_t columns);
        std::span<const int> EdgeCell(CellKey key) const;
        void GatherFan(int it);
        void Boxed(const std::vector<int>& lines);
        void EdgesNear(int self, double ax, double ay, double bx, double by, double extra, std::vector<int>& out);
        double RingReach(int it, int r) const;
        void RingSpells(int it);
        void GapFan(int it);
        void GapFanLine(int it, std::size_t i);
        void NearRecs(int it, int oi, const std::vector<int>& lines, double minX, double maxX, double minY, double maxY);

        std::vector<Item>& m_list;
        std::vector<Edge>& m_edges;
        Heart m_heart;
        const std::atomic<bool>* m_cancel = nullptr;
        double m_clear2 = 0;
        double m_minDist = 0;

        std::vector<std::vector<int>> m_incident;  // item -> its edges
        std::vector<double> m_px, m_py;            // the items' x, y, side by side for the costs (GridMove keeps them)
        Grid m_nodeGrid;
        // Lines by cell (BuildEdgeGrid): each cell's lines, in line order, end to
        // end in m_gridLines. Dense: from m_gridStart[cell], the cells of the box
        // at m_gridX0, m_gridY0, m_gridW x m_gridH. Columns: column cx - m_gridX0
        // has cells m_colStart[c] up to m_colStart[c + 1], rows m_cellY (sorted),
        // lines from m_gridStart[cell]. Hash: m_edgeGrid
        enum class GridMode { Dense, Columns, Hash };
        Grid m_edgeGrid;
        GridMode m_gridMode = GridMode::Dense;
        std::vector<int> m_colStart;
        std::vector<std::int32_t> m_cellY;
        std::int64_t m_gridX0 = 0, m_gridY0 = 0, m_gridW = 0, m_gridH = 0;
        std::vector<int> m_gridStart;
        std::vector<int> m_gridLines;
        std::vector<CellKey> m_gridKeys;  // BuildEdgeGrid's every (cell, line), in line order
        std::vector<int> m_gridOf;
        std::vector<std::uint64_t> m_edgeStamp;  // EdgesNear's once-each marks, by line
        std::uint64_t m_stamp = 0;
        // Work done (the JS _work): kCellWork per cell walked (CellsAlong), the
        // lines looked at in them (EdgesNear), per spot tried the lines and spells
        // its fans hold (SearchOne) and the spells and lines in its own cells
        // (Cost) - whole numbers, the same in both, so kMaxWork stops both at the
        // same spell
        std::uint64_t m_work = 0;
        bool m_capped = false;
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
        // GapFan: each line's frame (along x, along y, length; m_framed[i] 0 =
        // none) and the lines of m_edgeFan[i] it keeps, as GapRecs, once
        // made (m_gapBuilt; m_gapUses: spots that got to that line's gap part)
        std::vector<double> m_gapFrame;
        std::vector<char> m_framed;
        std::vector<char> m_gapBuilt;
        std::vector<int> m_gapUses;
        std::vector<std::vector<GapRec>> m_gapRec;
        std::vector<GapRec> m_scratchRecs;

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
