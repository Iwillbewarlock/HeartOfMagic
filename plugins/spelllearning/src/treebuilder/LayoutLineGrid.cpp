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

    // Each line in every cell along it; rebuilt at the start of every pass
    void LineClear::BuildEdgeGrid()
    {
        m_edgeGrid.clear();
        for (int e = 0; e < static_cast<int>(m_edges.size()); e++) {
            const Item& p = m_list[m_edges[e].a];
            const Item& q = m_list[m_edges[e].b];
            CellsAlong(p.x, p.y, q.x, q.y, m_clear2);
            for (int k = 0; k < m_keyCount; k++) m_edgeGrid[m_keyBuf[static_cast<std::size_t>(k)]].push_back(e);
        }
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
            auto bucket = m_edgeGrid.find(m_keyBuf[static_cast<std::size_t>(k)]);
            if (bucket == m_edgeGrid.end()) continue;
            for (const int e : bucket->second) {
                if (e == self || m_edges[e].stamp == stamp) continue;
                m_edges[e].stamp = stamp;
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
