#include "treebuilder/LayoutDeclutterInternal.h"
#include "treebuilder/LayoutMath.h"

#include <algorithm>

// =============================================================================
// LINE SEARCH: THE COST OF A SPOT (modules/layoutLineClear.js, COST)
// =============================================================================
//
//   cost = lines passing within `clear` of the spot
//        + other spells within `clear` of the spell's own lines from the spot
//        + ANGLE_COST for lines meeting narrower than MIN_ANGLE
//        + LINE_GAP_COST for lines running too close or crossing too flat
//        + OVERLAP_COST for spells closer than minDist
//        + HEART_COST on the heart
//        + MOVE_COST x distance from where the layout put it
//
// Every sum is taken in the JavaScript's order, term by term: the search keeps
// a spot only when it beats the best by 0.01, and a cost a bit off picks
// another spot.

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // COST
    // =========================================================================

    // The cost of `it` at (x, y); stops counting once it reaches `limit`
    double LineClear::Cost(int it, double x, double y, double limit)
    {
        const Item& item = m_list[it];
        double c = 0;
        // Cheap parts first
        const double mx = x - item.ox, my = y - item.oy;
        c += kMoveCost * std::sqrt(mx * mx + my * my);
        const double hx = x - m_heart.x, hy = y - m_heart.y;
        if (hx * hx + hy * hy < m_heart.r * m_heart.r) c += kHeartCost;

        // Other spells too close
        const double min = m_minDist, cell = kCell;
        const double min2 = min * min * kOverlapHair;
        const std::int64_t cx0 = FloorCell(x - min, cell), cx1 = FloorCell(x + min, cell);
        const std::int64_t cy0 = FloorCell(y - min, cell), cy1 = FloorCell(y + min, cell);
        for (std::int64_t cx = cx0; cx <= cx1; cx++) {
            for (std::int64_t cy = cy0; cy <= cy1; cy++) {
                auto close = m_nodeGrid.find(MakeCellKey(cx, cy));
                if (close == m_nodeGrid.end()) continue;
                for (const int si : close->second) {
                    if (si == it) continue;
                    const Item& s = m_list[si];
                    const double dx = s.x - x, dy = s.y - y, d2 = dx * dx + dy * dy;
                    if (d2 >= min2) continue;
                    const double d = std::sqrt(d2);
                    if (d < min) c += kOverlapCost * (1 - d / min) + kOverlapBase;
                }
            }
        }
        if (c >= limit) return c;

        // Lines meeting at too narrow an angle, here and at its neighbours
        c += AngleCost(it, x, y);
        if (c >= limit) return c;

        // Lines passing the spot
        const double clear2 = m_clear2, lo = kEndMargin, hi = 1 - kEndMargin;
        double vx, vy, l2, t, px, py;
        auto lines = m_edgeGrid.find(MakeCellKey(FloorCell(x, cell), FloorCell(y, cell)));
        if (lines != m_edgeGrid.end()) {
            for (const int ei : lines->second) {
                const Edge& e = m_edges[ei];
                if (e.a == it || e.b == it) continue;
                const Item& p = m_list[e.a];
                const Item& q = m_list[e.b];
                vx = q.x - p.x;
                vy = q.y - p.y;
                l2 = vx * vx + vy * vy;
                if (l2 < kTinyLength2) continue;
                t = ((x - p.x) * vx + (y - p.y) * vy) / l2;
                if (t < lo || t > hi) continue;
                px = p.x + vx * t - x;
                py = p.y + vy * t - y;
                if (px * px + py * py < clear2) c += 1;
            }
            if (c >= limit) return c;
        }

        // Its own lines from the spot, past other spells
        const auto& own = m_incident[it];
        for (std::size_t i = 0; i < own.size(); i++) {
            const int oi = Other(own[i], it);
            const Item& o = m_list[oi];
            const std::vector<int>* nearby;
            std::size_t count;
            if (m_hasFan) {
                nearby = &m_fan[i];
                count = static_cast<std::size_t>(m_fanEnd[i][static_cast<std::size_t>(m_ring)]);  // this ring's share
            } else {
                NearLine(x, y, o.x, o.y, m_clear2, m_scratchNear);
                nearby = &m_scratchNear;
                count = m_scratchNear.size();
            }
            vx = o.x - x;
            vy = o.y - y;
            l2 = vx * vx + vy * vy;
            if (l2 < kTinyLength2) continue;
            for (std::size_t k = 0; k < count; k++) {
                const int mi = (*nearby)[k];
                if (mi == it || mi == oi) continue;
                const Item& m = m_list[mi];
                t = ((m.x - x) * vx + (m.y - y) * vy) / l2;
                if (t < lo || t > hi) continue;
                px = x + vx * t - m.x;
                py = y + vy * t - m.y;
                if (px * px + py * py < clear2) c += 1;
            }
            if (c >= limit) return c;
        }
        // Its own lines against other lines running too close (the dearest)
        return c + LineGapCost(it, x, y, limit - c);
    }

    // LINE_GAP_COST for each other line one of `it`'s lines (from (x, y)) runs
    // closer than LINE_GAP to without crossing it, or crosses at less than
    // MIN_CROSS. Lines sharing a spell are the angle cost's business.
    double LineClear::LineGapCost(int it, double x, double y, double limit)
    {
        const auto& own = m_incident[it];
        const double gap = kLineGap, gap2 = gap * gap, box = gap * kBundleMax;
        const double bundle2 = gap2 * kBundleMax * kBundleMax;
        double c = 0;
        double t, dx, dy, d2, d;
        for (std::size_t i = 0; i < own.size(); i++) {
            const int oi = Other(own[i], it);
            const Item& o = m_list[oi];
            const std::vector<int>* others;
            if (m_hasFan) {
                others = &m_edgeFan[i];
            } else {
                EdgesNear(own[i], x, y, o.x, o.y, box, m_scratchEdges);
                Boxed(m_scratchEdges);
                others = &m_scratchEdges;
            }
            const double minX = std::min(x, o.x) - box, maxX = std::max(x, o.x) + box;
            const double minY = std::min(y, o.y) - box, maxY = std::max(y, o.y) + box;
            double ang = -1;
            // This line, (x, y) to o
            const double vx = o.x - x, vy = o.y - y, l2 = vx * vx + vy * vy;
            for (const int ei : *others) {
                const Edge& e = m_edges[ei];
                if (e.x1 < minX || e.x0 > maxX || e.y1 < minY || e.y0 > maxY) continue;
                if (e.a == it || e.b == it || e.a == oi || e.b == oi) continue;
                const Item& p = m_list[e.a];
                const Item& q = m_list[e.b];
                const double ex = e.ex, ey = e.ey;
                bool crossing = false;
                if ((vx * (p.y - y) - vy * (p.x - x)) * (vx * (q.y - y) - vy * (q.x - x)) < 0) {
                    crossing = (ex * (y - p.y) - ey * (x - p.x)) * (ex * (o.y - p.y) - ey * (o.x - p.x)) < 0;
                }
                if (crossing) {
                    if (ang < 0) ang = LayoutMath::Atan2(o.y - y, o.x - x);
                    double an = std::fmod(std::fabs(ang - e.ang), kPi);
                    if (an > kPi / 2) an = kPi - an;
                    if (an < kMinCross) c += kLineGapCost * (1 - an / kMinCross);
                } else {
                    // The nearest of: p and q to this line, its ends to that one
                    t = l2 > 0 ? ((p.x - x) * vx + (p.y - y) * vy) / l2 : 0;
                    if (t < 0) t = 0;
                    else if (t > 1) t = 1;
                    dx = x + vx * t - p.x;
                    dy = y + vy * t - p.y;
                    d2 = dx * dx + dy * dy;
                    t = l2 > 0 ? ((q.x - x) * vx + (q.y - y) * vy) / l2 : 0;
                    if (t < 0) t = 0;
                    else if (t > 1) t = 1;
                    dx = x + vx * t - q.x;
                    dy = y + vy * t - q.y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    const double el2 = e.l2;
                    t = el2 > 0 ? ((x - p.x) * ex + (y - p.y) * ey) / el2 : 0;
                    if (t < 0) t = 0;
                    else if (t > 1) t = 1;
                    dx = p.x + ex * t - x;
                    dy = p.y + ey * t - y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    t = el2 > 0 ? ((o.x - p.x) * ex + (o.y - p.y) * ey) / el2 : 0;
                    if (t < 0) t = 0;
                    else if (t > 1) t = 1;
                    dx = p.x + ex * t - o.x;
                    dy = p.y + ey * t - o.y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    if (d2 < bundle2) {
                        const double need = BundleGap(x, y, o.x, o.y, e, gap);
                        if (d2 < need * need) c += kLineGapCost * (1 - std::sqrt(d2) / need) * (need / gap);
                    }
                }
                if (c >= limit) return c;
            }
        }
        return c;
    }

    // The room two lines need: LINE_GAP, plus LINE_GAP for every BUNDLE_LEN
    // they run side by side closer to parallel than BUNDLE_ANGLE
    double LineClear::BundleGap(double ax, double ay, double bx, double by, const Edge& e, double gap) const
    {
        const double vx = bx - ax, vy = by - ay;
        const double len = std::sqrt(vx * vx + vy * vy);
        if (len < kTinyDistance) return gap;
        double an = std::fmod(std::fabs(LayoutMath::Atan2(vy, vx) - e.ang), kPi);
        if (an > kPi / 2) an = kPi - an;
        if (an >= kBundleAngle) return gap;
        const double ux = vx / len, uy = vy / len;
        const Item& p = m_list[e.a];
        const Item& q = m_list[e.b];
        const double t0 = (p.x - ax) * ux + (p.y - ay) * uy, t1 = (q.x - ax) * ux + (q.y - ay) * uy;
        const double side = std::min(len, std::max(t0, t1)) - std::max(0.0, std::min(t0, t1));
        if (side <= 0) return gap;
        return gap * std::min(kBundleMax, 1 + side / kBundleLen);
    }

    // =========================================================================
    // ANGLES
    // =========================================================================

    // ANGLE_COST for each pair of lines meeting narrower than MIN_ANGLE, with
    // `it` at (x, y): at the spell, and at each spell it is joined to
    double LineClear::AngleCost(int it, double x, double y)
    {
        const auto& own = m_incident[it];
        if (own.empty()) return 0;
        double c = 0, d;
        const std::size_t n = own.size();
        const double pi = kPi, minAngle = kMinAngle;
        if (m_dirs.size() < n) {
            m_dirs.resize(n);
            m_lens.resize(n);
        }
        for (std::size_t i = 0; i < n; i++) {
            const Item& o = m_list[Other(own[i], it)];
            const double ox = o.x - x, oy = o.y - y;
            const double len = std::sqrt(ox * ox + oy * oy);
            m_dirs[i] = LayoutMath::Atan2(oy, ox);
            m_lens[i] = len;
            // At the neighbour: the line to here against its other lines
            const double back = LayoutMath::Atan2(-oy, -ox);
            const std::vector<double>* theirs;
            if (m_hasFan) {
                theirs = &m_angleFan[i];
            } else {
                NeighbourLines(it, static_cast<int>(i), m_theirs);
                theirs = &m_theirs;
            }
            for (std::size_t k = 0; k < theirs->size(); k += 2) {
                d = std::fabs(back - (*theirs)[k]);
                if (d > pi) d = 2 * pi - d;
                if (d < minAngle) c += Narrow(back, (*theirs)[k], std::min(len, (*theirs)[k + 1]));
            }
        }
        // At the spell itself
        for (std::size_t i = 0; i < n; i++) {
            for (std::size_t j = i + 1; j < n; j++) {
                d = std::fabs(m_dirs[i] - m_dirs[j]);
                if (d > pi) d = 2 * pi - d;
                if (d < minAngle) c += Narrow(m_dirs[i], m_dirs[j], std::min(m_lens[i], m_lens[j]));
            }
        }
        return c;
    }

    // The other lines at the far end of `it`'s line i, as [direction, length, ...]
    void LineClear::NeighbourLines(int it, int i, std::vector<double>& out) const
    {
        out.clear();
        const int line = m_incident[it][static_cast<std::size_t>(i)];
        const int oi = Other(line, it);
        const Item& o = m_list[oi];
        for (const int theirs : m_incident[oi]) {
            if (theirs == line) continue;
            const Item& q = m_list[Other(theirs, oi)];
            const double qx = q.x - o.x, qy = q.y - o.y;
            out.push_back(LayoutMath::Atan2(qy, qx));
            out.push_back(std::sqrt(qx * qx + qy * qy));
        }
    }

    // The cost of two directions (radians) closer than MIN_ANGLE; `len` the shorter line
    double LineClear::Narrow(double a, double b, double len) const
    {
        double d = std::fabs(a - b);
        if (d > kPi) d = 2 * kPi - d;
        if (d >= kMinAngle) return 0;
        return kAngleCost * (1 - d / kMinAngle) * (1 + len / kLongLine);
    }
}
