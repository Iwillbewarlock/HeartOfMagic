#include "treebuilder/LayoutDeclutterInternal.h"
#include "treebuilder/LayoutMath.h"

#include <algorithm>

// =============================================================================
// LINE SEARCH: GRIDS AND FANS (modules/layoutLineGrid.js)
// =============================================================================
//
// Spells by cell, lines by every cell they pass near, and the fans - what one
// spell's search looks at, gathered once per spell. Every walk goes over the
// cells and buckets in the JavaScript's order: the costs are sums, and a sum
// taken in another order can differ in its last bit.

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // GRIDS
    // =========================================================================

    void LineClear::GridAdd(int it)
    {
        Item& item = m_list[it];
        const CellKey key = MakeCellKey(FloorCell(item.x, kCell), FloorCell(item.y, kCell));
        m_nodeGrid[key].push_back(it);
        item.cell = key;
    }

    void LineClear::GridMove(int it, double x, double y)
    {
        Item& item = m_list[it];
        auto bucket = m_nodeGrid.find(item.cell);
        if (bucket != m_nodeGrid.end()) {
            auto at = std::find(bucket->second.begin(), bucket->second.end(), it);
            if (at != bucket->second.end()) bucket->second.erase(at);
        }
        item.x = x;
        item.y = y;
        m_px[static_cast<std::size_t>(it)] = x;
        m_py[static_cast<std::size_t>(it)] = y;
        item.v++;
        GridAdd(it);
    }

    void LineClear::Near(double x0, double y0, double x1, double y1, double pad, std::vector<int>& out) const
    {
        out.clear();
        const std::int64_t cx0 = FloorCell(x0 - pad, kCell), cx1 = FloorCell(x1 + pad, kCell);
        const std::int64_t cy0 = FloorCell(y0 - pad, kCell), cy1 = FloorCell(y1 + pad, kCell);
        for (std::int64_t cx = cx0; cx <= cx1; cx++) {
            for (std::int64_t cy = cy0; cy <= cy1; cy++) {
                auto bucket = m_nodeGrid.find(MakeCellKey(cx, cy));
                if (bucket != m_nodeGrid.end()) out.insert(out.end(), bucket->second.begin(), bucket->second.end());
            }
        }
    }

    // Every cell within sqrt(clear2) of the line a-b, once each, into
    // m_keyBuf (the first m_keyCount). As _cellsAlong: each column tested only
    // over the rows the line can reach there.
    void LineClear::CellsAlong(double ax, double ay, double bx, double by, double clear2)
    {
        int n = 0;
        const double cell = kCell;
        const double pad = std::sqrt(clear2);
        const double reach = pad + cell * kHalfCellDiagonal;
        const double reach2 = reach * reach;
        const std::int64_t cx0 = FloorCell(std::min(ax, bx) - pad, cell), cx1 = FloorCell(std::max(ax, bx) + pad, cell);
        const std::int64_t cy0 = FloorCell(std::min(ay, by) - pad, cell), cy1 = FloorCell(std::max(ay, by) + pad, cell);
        const double vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
        const double band = reach + 1;
        for (std::int64_t cx = cx0; cx <= cx1; cx++) {
            const double mx = (static_cast<double>(cx) + 0.5) * cell;
            double t0 = 0, t1 = 1;
            if (vx != 0) {
                t0 = (mx - band - ax) / vx;
                t1 = (mx + band - ax) / vx;
                if (t0 > t1) std::swap(t0, t1);
                if (t0 < 0) t0 = 0;
                if (t1 > 1) t1 = 1;
                if (t0 > t1) continue;
            } else if (std::fabs(mx - ax) > band) {
                continue;
            }
            const double y0 = ay + vy * t0, y1 = ay + vy * t1;
            std::int64_t from = FloorCell(std::min(y0, y1) - band, cell), to = FloorCell(std::max(y0, y1) + band, cell);
            if (from < cy0) from = cy0;
            if (to > cy1) to = cy1;
            for (std::int64_t cy = from; cy <= to; cy++) {
                const double my = (static_cast<double>(cy) + 0.5) * cell;
                double t = l2 > 0 ? ((mx - ax) * vx + (my - ay) * vy) / l2 : 0;
                if (t < 0) t = 0;
                else if (t > 1) t = 1;
                const double dx = ax + vx * t - mx, dy = ay + vy * t - my;
                if (dx * dx + dy * dy <= reach2) {
                    if (n >= static_cast<int>(m_keyBuf.size())) m_keyBuf.resize(static_cast<std::size_t>(n) * 2 + 16);
                    m_keyBuf[static_cast<std::size_t>(n++)] = MakeCellKey(cx, cy);
                }
            }
        }
        m_keyCount = n;
        m_work += kCellWork * static_cast<std::uint64_t>(n);
    }

    void LineClear::NearLine(double ax, double ay, double bx, double by, double clear2, std::vector<int>& out)
    {
        out.clear();
        CellsAlong(ax, ay, bx, by, clear2);
        for (int k = 0; k < m_keyCount; k++) {
            auto bucket = m_nodeGrid.find(m_keyBuf[static_cast<std::size_t>(k)]);
            if (bucket != m_nodeGrid.end()) out.insert(out.end(), bucket->second.begin(), bucket->second.end());
        }
    }

    // Each line in every cell along it; rebuilt at the start of every pass.
    // The cells' lists in line order (as the JS pushes them), in one array
    // over the box of cells the lines take when it is small enough
    void LineClear::BuildEdgeGrid()
    {
        m_edgeGrid.clear();
        m_gridKeys.clear();
        m_gridOf.clear();
        std::int64_t x0 = 0, x1 = -1, y0 = 0, y1 = -1;
        for (int e = 0; e < static_cast<int>(m_edges.size()); e++) {
            const Item& p = m_list[m_edges[e].a];
            const Item& q = m_list[m_edges[e].b];
            CellsAlong(p.x, p.y, q.x, q.y, m_clear2);
            for (int k = 0; k < m_keyCount; k++) {
                const CellKey key = m_keyBuf[static_cast<std::size_t>(k)];
                const std::int64_t cx = key >> 32, cy = static_cast<std::int32_t>(key & 0xFFFFFFFF);
                if (x1 < x0) {
                    x0 = x1 = cx;
                    y0 = y1 = cy;
                }
                x0 = std::min(x0, cx);
                x1 = std::max(x1, cx);
                y0 = std::min(y0, cy);
                y1 = std::max(y1, cy);
                m_gridKeys.push_back(key);
                m_gridOf.push_back(e);
            }
        }
        const std::int64_t w = x1 - x0 + 1, h = y1 - y0 + 1;
        const bool dense = x1 < x0 || (w <= kMaxDenseCells && h <= kMaxDenseCells && w * h <= kMaxDenseCells);
        m_gridMode = dense ? GridMode::Dense : (w <= kMaxGridColumns ? GridMode::Columns : GridMode::Hash);
        if (m_gridMode == GridMode::Hash) {
            for (std::size_t k = 0; k < m_gridKeys.size(); k++) m_edgeGrid[m_gridKeys[k]].push_back(m_gridOf[k]);
            return;
        }
        if (m_gridMode == GridMode::Columns) {
            BuildEdgeColumns(x0, w);
            return;
        }
        m_gridX0 = x0;
        m_gridY0 = y0;
        m_gridW = x1 < x0 ? 0 : w;
        m_gridH = x1 < x0 ? 0 : h;
        const auto cells = static_cast<std::size_t>(m_gridW * m_gridH);
        m_gridStart.assign(cells + 1, 0);
        for (const CellKey key : m_gridKeys) {
            const std::int64_t cx = key >> 32, cy = static_cast<std::int32_t>(key & 0xFFFFFFFF);
            m_gridStart[static_cast<std::size_t>((cx - x0) * m_gridH + (cy - y0)) + 1]++;
        }
        for (std::size_t c = 0; c < cells; c++) m_gridStart[c + 1] += m_gridStart[c];
        m_gridLines.resize(m_gridKeys.size());
        std::vector<int> next(m_gridStart.begin(), m_gridStart.end() - 1);  // each cell's next free place
        for (std::size_t k = 0; k < m_gridKeys.size(); k++) {
            const CellKey key = m_gridKeys[k];
            const std::int64_t cx = key >> 32, cy = static_cast<std::int32_t>(key & 0xFFFFFFFF);
            m_gridLines[static_cast<std::size_t>(next[static_cast<std::size_t>((cx - x0) * m_gridH + (cy - y0))]++)] = m_gridOf[k];
        }
    }

    // BuildEdgeGrid by column, for a box of cells too big for one array: the
    // (cell, line) pairs by column, then each column's by row, each cell's
    // lines staying in line order
    void LineClear::BuildEdgeColumns(std::int64_t x0, std::int64_t columns)
    {
        const std::size_t count = m_gridKeys.size();
        m_gridX0 = x0;
        m_gridW = columns;
        std::vector<int> byColumn(count), start(static_cast<std::size_t>(columns) + 1, 0);
        for (const CellKey key : m_gridKeys) start[static_cast<std::size_t>((key >> 32) - x0) + 1]++;
        for (std::size_t c = 0; c < static_cast<std::size_t>(columns); c++) start[c + 1] += start[c];
        std::vector<int> next(start.begin(), start.end() - 1);
        for (std::size_t k = 0; k < count; k++) byColumn[static_cast<std::size_t>(next[static_cast<std::size_t>((m_gridKeys[k] >> 32) - x0)]++)] = static_cast<int>(k);
        auto row = [this](int k) { return static_cast<std::int32_t>(m_gridKeys[static_cast<std::size_t>(k)] & 0xFFFFFFFF); };
        m_colStart.assign(static_cast<std::size_t>(columns) + 1, 0);
        m_cellY.clear();
        m_gridStart.clear();
        m_gridLines.resize(count);
        std::size_t placed = 0;
        for (std::size_t c = 0; c < static_cast<std::size_t>(columns); c++) {
            m_colStart[c] = static_cast<int>(m_cellY.size());
            const auto from = byColumn.begin() + start[c], to = byColumn.begin() + start[c + 1];
            std::stable_sort(from, to, [&row](int a, int b) { return row(a) < row(b); });
            for (auto k = from; k != to; ++k) {
                if (k == from || row(*k) != row(*(k - 1))) {
                    m_cellY.push_back(row(*k));
                    m_gridStart.push_back(static_cast<int>(placed));
                }
                m_gridLines[placed++] = m_gridOf[static_cast<std::size_t>(*k)];
            }
        }
        m_colStart[static_cast<std::size_t>(columns)] = static_cast<int>(m_cellY.size());
        m_gridStart.push_back(static_cast<int>(placed));
    }

    // The lines in one cell, in line order (empty outside the lines' cells)
    std::span<const int> LineClear::EdgeCell(CellKey key) const
    {
        if (m_gridMode == GridMode::Hash) {
            auto bucket = m_edgeGrid.find(key);
            if (bucket == m_edgeGrid.end()) return {};
            return { bucket->second.data(), bucket->second.size() };
        }
        if (m_gridMode == GridMode::Columns) {
            const std::int64_t c = (key >> 32) - m_gridX0;
            if (c < 0 || c >= m_gridW) return {};
            const std::int32_t cy = static_cast<std::int32_t>(key & 0xFFFFFFFF);
            const auto first = m_cellY.begin() + m_colStart[static_cast<std::size_t>(c)];
            const auto last = m_cellY.begin() + m_colStart[static_cast<std::size_t>(c) + 1];
            const auto at = std::lower_bound(first, last, cy);
            if (at == last || *at != cy) return {};
            const auto cell = static_cast<std::size_t>(at - m_cellY.begin());
            return { m_gridLines.data() + m_gridStart[cell], static_cast<std::size_t>(m_gridStart[cell + 1] - m_gridStart[cell]) };
        }
        const std::int64_t cx = (key >> 32) - m_gridX0, cy = static_cast<std::int32_t>(key & 0xFFFFFFFF) - m_gridY0;
        if (cx < 0 || cx >= m_gridW || cy < 0 || cy >= m_gridH) return {};
        const auto cell = static_cast<std::size_t>(cx * m_gridH + cy);
        const int from = m_gridStart[cell], to = m_gridStart[cell + 1];
        return { m_gridLines.data() + from, static_cast<std::size_t>(to - from) };
    }

    // =========================================================================
    // FANS
    // =========================================================================

    // For each of `it`'s lines: the spells near any line it could have from a
    // spot it will try (m_fan) and the other lines near it (m_edgeFan)
    void LineClear::GatherFan(int it)
    {
        const Item& item = m_list[it];
        const auto& own = m_incident[it];
        const double dx = item.x - item.ox, dy = item.y - item.oy;
        const double reach = kRadii[kRings - 1] * (item.reach != 0 ? item.reach : 1) + std::sqrt(dx * dx + dy * dy);
        const double clear2 = m_clear2, clear = std::sqrt(clear2);
        const double pad = clear + reach;
        const double extra = reach + kLineGap * kBundleMax - clear;
        const bool shared = clear + extra >= pad;
        m_fan.resize(own.size());
        m_edgeFan.resize(own.size());
        for (std::size_t i = 0; i < own.size(); i++) {
            const Item& o = m_list[Other(own[i], it)];
            EdgesNear(own[i], item.x, item.y, o.x, o.y, extra, m_edgeFan[i]);
            Boxed(m_edgeFan[i]);
            auto& nearby = m_fan[i];
            if (!shared) {
                NearLine(item.x, item.y, o.x, o.y, pad * pad, nearby);
                continue;
            }
            // The spells in the cells EdgesNear just walked (still in m_keyBuf)
            nearby.clear();
            for (int k = 0; k < m_keyCount; k++) {
                auto bucket = m_nodeGrid.find(m_keyBuf[static_cast<std::size_t>(k)]);
                if (bucket != m_nodeGrid.end()) nearby.insert(nearby.end(), bucket->second.begin(), bucket->second.end());
            }
        }
    }

    // Each line's box, vector and direction, kept until one of its ends moves
    void LineClear::Boxed(const std::vector<int>& lines)
    {
        for (const int index : lines) {
            Edge& e = m_edges[index];
            const Item& p = m_list[e.a];
            const Item& q = m_list[e.b];
            if (e.v0 == p.v && e.v1 == q.v) continue;
            e.v0 = p.v;
            e.v1 = q.v;
            e.x0 = std::min(p.x, q.x);
            e.x1 = std::max(p.x, q.x);
            e.y0 = std::min(p.y, q.y);
            e.y1 = std::max(p.y, q.y);
            e.ex = q.x - p.x;
            e.ey = q.y - p.y;
            e.l2 = e.ex * e.ex + e.ey * e.ey;
            e.ang = LayoutMath::Atan2(q.y - p.y, q.x - p.x);
        }
    }

    // Lines in the cells along a-b, grown by `extra`, once each (not `self`)
    void LineClear::EdgesNear(int self, double ax, double ay, double bx, double by, double extra, std::vector<int>& out)
    {
        out.clear();
        const std::uint64_t stamp = ++m_stamp;
        const double pad = std::sqrt(m_clear2) + extra;
        CellsAlong(ax, ay, bx, by, pad * pad);
        for (int k = 0; k < m_keyCount; k++) {
            const std::span<const int> bucket = EdgeCell(m_keyBuf[static_cast<std::size_t>(k)]);
            m_work += bucket.size();
            for (const int e : bucket) {
                if (e == self || m_edgeStamp[static_cast<std::size_t>(e)] == stamp) continue;
                m_edgeStamp[static_cast<std::size_t>(e)] = stamp;
                out.push_back(e);
            }
        }
    }

    // How far from where `it` is now a spot of ring r can be, with a hair to spare
    double LineClear::RingReach(int it, int r) const
    {
        const Item& item = m_list[it];
        const double dx = item.x - item.ox, dy = item.y - item.oy;
        return kRadii[r] * (item.reach != 0 ? item.reach : 1) + std::sqrt(dx * dx + dy * dy) + kRoundingHair;
    }

    // Sort each line's fan by the first ring whose spots can bring the line
    // within `clear` of the spell, dropping what no spot can reach
    // (m_fanEnd[i][ring]: how much of m_fan[i] that ring looks at)
    void LineClear::RingSpells(int it)
    {
        const Item& item = m_list[it];
        const auto& own = m_incident[it];
        const double clear = std::sqrt(m_clear2);
        const double lo = kEndMargin, hi = 1 - kEndMargin;
        double reach[kRings];
        double lim2[kRings];
        for (int r = 0; r < kRings; r++) {
            reach[r] = RingReach(it, r);
            lim2[r] = (clear + reach[r]) * (clear + reach[r]);
        }
        m_fanEnd.resize(own.size());
        for (std::size_t i = 0; i < own.size(); i++) {
            const int oi = Other(own[i], it);
            const Item& o = m_list[oi];
            auto& list = m_fan[i];
            int counts[kRings + 1] = {};
            const double vx = o.x - item.x, vy = o.y - item.y, l2 = vx * vx + vy * vy, len = std::sqrt(l2);
            m_ringOf.resize(list.size());
            for (std::size_t k = 0; k < list.size(); k++) {
                const int mi = list[k];
                int r = kRings;  // no ring: dropped
                if (mi != it && mi != oi) {
                    const Item& m = m_list[mi];
                    const double wx = m.x - item.x, wy = m.y - item.y;
                    const double along = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
                    const double t = along < 0 ? 0 : (along > 1 ? 1 : along);
                    const double dx = vx * t - wx, dy = vy * t - wy, d2 = dx * dx + dy * dy;
                    const double side = len > 1 ? std::fabs(wx * vy - wy * vx) / len : -1;
                    for (r = 0; r < kRings; r++) {
                        if (d2 > lim2[r]) continue;
                        if (side < 0) break;
                        const double from = std::max(lo, along - (clear + reach[r]) / len);
                        if (from <= hi && side <= clear + (1 - from) * reach[r] + kRoundingHair) break;
                    }
                }
                m_ringOf[k] = r;
                counts[r]++;
            }
            auto& end = m_fanEnd[i];
            end.assign(kRings, 0);
            int at[kRings];
            int sum = 0;
            for (int r = 0; r < kRings; r++) {
                at[r] = sum;
                sum += counts[r];
                end[r] = sum;
            }
            m_sorted.assign(static_cast<std::size_t>(sum), 0);
            for (std::size_t k = 0; k < list.size(); k++) {
                if (m_ringOf[k] < kRings) m_sorted[static_cast<std::size_t>(at[m_ringOf[k]]++)] = list[k];
            }
            list.swap(m_sorted);
        }
    }
}

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // LINE GAP FAN
    // =========================================================================

    // A line as LineGapCost reads it (its frame box 0, not steep: GapFanLine's business)
    GapRec LineClear::MakeGapRec(const Edge& e) const
    {
        const Item& p = m_list[e.a];
        const Item& q = m_list[e.b];
        GapRec r;
        r.x0 = e.x0;
        r.x1 = e.x1;
        r.y0 = e.y0;
        r.y1 = e.y1;
        r.px = p.x;
        r.py = p.y;
        r.qx = q.x;
        r.qy = q.y;
        r.ex = e.ex;
        r.ey = e.ey;
        r.l2 = e.l2;
        r.ang = e.ang;
        return r;
    }

    // Of `lines`, those LineGapCost looks at for the line it-oi from a spot with
    // this box (grown by LINE_GAP x BUNDLE_MAX), as records into m_scratchRecs:
    // lines sharing a spell and lines whose box misses it left out (it passes over both)
    void LineClear::NearRecs(int it, int oi, const std::vector<int>& lines, double minX, double maxX, double minY, double maxY)
    {
        m_scratchRecs.clear();
        for (const int ei : lines) {
            const Edge& e = m_edges[ei];
            if (e.x1 < minX || e.x0 > maxX || e.y1 < minY || e.y0 > maxY) continue;
            if (e.a == it || e.b == it || e.a == oi || e.b == oi) continue;
            m_scratchRecs.push_back(MakeGapRec(e));
        }
    }

    // Set up the cut of each of `it`'s line fans (the JS _gapFan, which says
    // why each test only drops lines that add nothing): made per line by
    // GapFanLine once kGapFanAfter spots got to that line's gap part
    void LineClear::GapFan(int it)
    {
        const std::size_t lines = m_incident[it].size();
        m_gapFrame.assign(lines * 3, 0);
        m_framed.assign(lines, 0);
        m_gapBuilt.assign(lines, 0);
        m_gapUses.assign(lines, 0);
        m_gapRec.resize(lines);
    }

    // Of m_edgeFan[i], the lines that can add to LineGapCost from some spot of
    // this search, in the same order, as GapRecs (m_gapRec[i]), and the frame
    // of `it`'s line i with each kept line's box in it (the JS _gapFanLine)
    void LineClear::GapFanLine(int it, std::size_t i)
    {
        const Item& item = m_list[it];
        const int oi = Other(m_incident[it][i], it);
        const Item& o = m_list[oi];
        const double m = kFanMargin, R = RingReach(it, kRings - 1) + m;
        const double wide = R + kLineGap * kBundleMax + m, wide2 = wide * wide;
        const double steep = kMinCross + kFanAngleMargin, bundle = kBundleAngle + kFanAngleMargin;
        const double ix = item.x, iy = item.y;
        const double vx = o.x - ix, vy = o.y - iy, len = std::sqrt(vx * vx + vy * vy);
        const bool framed = len >= kMinFrame;
        const double ux = framed ? vx / len : 0, uy = framed ? vy / len : 0;
        const double turn = R < len ? std::asin(R / len) : kInfinity, base = LayoutMath::Atan2(vy, vx);
        // A line whose box is further than `wide` from the base's box is further from the base
        const double bx0 = std::min(ix, o.x) - wide, bx1 = std::max(ix, o.x) + wide;
        const double by0 = std::min(iy, o.y) - wide, by1 = std::max(iy, o.y) + wide;
        auto& recs = m_gapRec[i];
        recs.clear();
        for (const int ei : m_edgeFan[i]) {
            const Edge& e = m_edges[ei];
            if (e.a == it || e.b == it || e.a == oi || e.b == oi) continue;
            if (e.x1 < bx0 || e.x0 > bx1 || e.y1 < by0 || e.y0 > by1) continue;
            const Item& p = m_list[e.a];
            const Item& q = m_list[e.b];
            // Its ends in the base's frame; its box there is no nearer the base than it is
            const double pu = (p.x - ix) * ux + (p.y - iy) * uy, pw = (p.y - iy) * ux - (p.x - ix) * uy;
            const double qu = (q.x - ix) * ux + (q.y - iy) * uy, qw = (q.y - iy) * ux - (q.x - ix) * uy;
            const double u0 = std::min(pu, qu), u1 = std::max(pu, qu), w0 = std::min(pw, qw), w1 = std::max(pw, qw);
            if (framed) {
                const double gu = u0 > len ? u0 - len : (u1 < 0 ? -u1 : 0), gw = w0 > 0 ? w0 : (w1 < 0 ? -w1 : 0);
                if (gu * gu + gw * gw >= wide2) continue;
            }
            // p and q against the base, `it` and o against e (as LineGapCost's crossing test)
            const double sp = vx * (p.y - iy) - vy * (p.x - ix), sq = vx * (q.y - iy) - vy * (q.x - ix);
            const double si = e.ex * (iy - p.y) - e.ey * (ix - p.x), so = e.ex * (o.y - p.y) - e.ey * (o.x - p.x);
            const bool crossing = sp * sq < 0 && si * so < 0;
            double an = std::fmod(std::fabs(base - e.ang), kPi);
            if (an > kPi / 2) an = kPi - an;
            if (crossing && turn < kInfinity) {
                const double el = std::sqrt(e.l2);
                const double lp = std::sqrt((p.x - o.x) * (p.x - o.x) + (p.y - o.y) * (p.y - o.y));
                const double lq = std::sqrt((q.x - o.x) * (q.x - o.x) + (q.y - o.y) * (q.y - o.y));
                if (std::fabs(si) > (R + m) * el && std::fabs(so) > m * el &&
                    std::fabs(sp) > (R + m) * lp + m * len && std::fabs(sq) > (R + m) * lq + m * len &&
                    an >= steep + turn) continue;
            }
            GapRec& r = recs.emplace_back(MakeGapRec(e));
            r.u0 = u0;
            r.u1 = u1;
            r.w0 = w0;
            r.w1 = w1;
            r.steep = an >= bundle + turn;
        }
        m_gapBuilt[i] = 1;
        m_framed[i] = framed ? 1 : 0;
        m_gapFrame[3 * i] = ux;
        m_gapFrame[3 * i + 1] = uy;
        m_gapFrame[3 * i + 2] = len;
    }
}
