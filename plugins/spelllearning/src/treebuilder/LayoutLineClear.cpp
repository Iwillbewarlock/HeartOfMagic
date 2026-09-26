#include "treebuilder/LayoutDeclutterInternal.h"
#include "treebuilder/LayoutMath.h"

#include <algorithm>

// =============================================================================
// LINE SEARCH (modules/layoutLineClear.js)
// =============================================================================
//
// Move spells so the tree's straight lines pass them by. For each spell that
// is in the way, or whose own lines run through others or meet too narrowly,
// try DIRECTIONS x RADII spots round where the layout put it and round where
// it is now, and move it to the cheapest (LayoutLineClearCost.cpp). Spells in
// list order, PASSES times or until a pass moves none; a pass after the first
// looks only at spells whose surroundings changed. Roots never move; a spell
// kept in a sector only tries spots inside it.

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // SETUP AND PASSES
    // =========================================================================

    LineClear::LineClear(std::vector<Item>& list, std::vector<Edge>& edges, const Heart& heart, double clear, double minDist) :
        m_list(list),
        m_edges(edges),
        m_heart(heart),
        m_clear2(clear * clear),
        m_minDist(minDist)
    {
        m_incident.resize(list.size());
        for (auto& item : m_list) {
            item.ox = item.x;
            item.oy = item.y;
            item.v = 0;
        }
        for (int e = 0; e < static_cast<int>(m_edges.size()); e++) {
            m_edges[e].v0 = m_edges[e].v1 = -1;
            m_incident[m_edges[e].a].push_back(e);
            m_incident[m_edges[e].b].push_back(e);
        }
        for (int i = 0; i < static_cast<int>(m_list.size()); i++) GridAdd(i);

        // The spots to try, as offsets, ring by ring
        for (int ri = 0; ri < kRings; ri++) {
            for (int di = 0; di < kDirections; di++) {
                const double ang = static_cast<double>(di) / kDirections * 2 * kPi;
                m_offsets.push_back(LayoutMath::Cos(ang) * kRadii[ri]);
                m_offsets.push_back(LayoutMath::Sin(ang) * kRadii[ri]);
            }
        }
        m_dirty.assign(m_list.size(), 1);
    }

    void LineClear::Run()
    {
        const int count = static_cast<int>(m_list.size());
        std::vector<char> next;
        while (m_pass < kPasses) {
            BuildEdgeGrid();
            next.assign(m_list.size(), 0);
            int moved = 0;
            for (int at = 0; at < count; at++) {
                if (!m_list[at].fixed && m_dirty[at] && SearchOne(at, next)) moved++;
            }
            if (!moved) break;
            m_dirty.swap(next);
            m_pass++;
        }
    }

    int LineClear::Other(int edge, int it) const
    {
        const Edge& e = m_edges[edge];
        return e.a == it ? e.b : e.a;
    }

    // =========================================================================
    // ONE SPELL
    // =========================================================================

    // Try the spots round one spell and move it to the cheapest; true if it moved
    bool LineClear::SearchOne(int it, std::vector<char>& next)
    {
        m_hasFan = false;
        Item& item = m_list[it];
        double best = Cost(it, item.x, item.y, kInfinity);
        if (best < kGoodEnough) return false;
        item.reach = ReachScale(it);
        // Gathered once for the widest ring, then cut down per ring
        GatherFan(it);
        RingSpells(it);
        const auto& own = m_incident[it];
        m_angleFan.resize(own.size());
        for (std::size_t a = 0; a < own.size(); a++) NeighbourLines(it, static_cast<int>(a), m_angleFan[a]);
        m_hasFan = true;

        const int perRing = 2 * kDirections;
        double bx = item.x, by = item.y;
        // Round where the layout put it, and round where it is now (once moved)
        const int rounds = (item.x != item.ox || item.y != item.oy) ? 2 : 1;
        const double sc = item.reach;
        const std::size_t spots = m_offsets.size();
        for (std::size_t o = 0; o < spots && best >= kGoodEnough; o += 2) {
            m_ring = static_cast<int>(o) / perRing;
            for (int k = 0; k < rounds; k++) {
                const double x = (k ? item.x : item.ox) + m_offsets[o] * sc;
                const double y = (k ? item.y : item.oy) + m_offsets[o + 1] * sc;
                if (item.hasSector && !InSector(item.sector, x, y)) continue;
                const double c = Cost(it, x, y, best);
                if (c < best - kBetterBy) {
                    best = c;
                    bx = x;
                    by = y;
                }
            }
        }
        m_hasFan = false;
        if (bx == item.x && by == item.y) return false;
        MarkAround(it, next);
        GridMove(it, bx, by);
        MarkAround(it, next);
        m_movedIds.insert(it);
        return true;
    }

    // How much further than RADII `it` searches: more for long lines
    double LineClear::ReachScale(int it) const
    {
        const Item& item = m_list[it];
        double longest = 0;
        for (const int e : m_incident[it]) {
            const Item& o = m_list[Other(e, it)];
            const double dx = o.x - item.x, dy = o.y - item.y;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len > longest) longest = len;
        }
        return std::max(1.0, std::min(kMaxReachScale, longest / kReachLine));
    }

    // Mark for the next pass what a move of `it` (at where it is now) changes
    void LineClear::MarkAround(int it, std::vector<char>& marks)
    {
        const Item& item = m_list[it];
        marks[it] = 1;
        for (const int line : m_incident[it]) {
            const int oi = Other(line, it);
            const Item& o = m_list[oi];
            marks[oi] = 1;
            // Angles at the neighbour involve its other neighbours
            for (const int theirs : m_incident[oi]) marks[m_edges[theirs].a] = marks[m_edges[theirs].b] = 1;
            // Spells along the line
            NearLine(item.x, item.y, o.x, o.y, m_clear2, m_scratchNear);
            for (const int n : m_scratchNear) marks[n] = 1;
            // Lines running close to it (their ends' line gap cost changes)
            EdgesNear(line, item.x, item.y, o.x, o.y, 0, m_scratchEdges);
            for (const int e : m_scratchEdges) marks[m_edges[e].a] = marks[m_edges[e].b] = 1;
        }
        // Spells close by (lines past them, overlap)
        const double reach = std::max(m_minDist, std::sqrt(m_clear2));
        Near(item.x, item.y, item.x, item.y, reach, m_scratchNear);
        for (const int n : m_scratchNear) marks[n] = 1;
    }

    // =========================================================================
    // COUNT (for the log)
    // =========================================================================

    // Lines passing within `clear` of a spell they do not end at (pairs)
    int LineClear::CountLinesThrough()
    {
        // Where the spells are now (the declutter rounds moved them since)
        m_nodeGrid.clear();
        for (int i = 0; i < static_cast<int>(m_list.size()); i++) GridAdd(i);
        int count = 0;
        for (const Edge& e : m_edges) {
            const Item& p = m_list[e.a];
            const Item& q = m_list[e.b];
            NearLine(p.x, p.y, q.x, q.y, m_clear2, m_scratchNear);
            for (const int ni : m_scratchNear) {
                const Item& n = m_list[ni];
                if (ni != e.a && ni != e.b && SegDist2(n.x, n.y, p.x, p.y, q.x, q.y) < m_clear2) count++;
            }
        }
        return count;
    }

    // Squared distance from (px, py) to the line a-b, away from its ends; infinity near them
    double LineClear::SegDist2(double px, double py, double ax, double ay, double bx, double by) const
    {
        const double vx = bx - ax, vy = by - ay;
        const double l2 = vx * vx + vy * vy;
        if (l2 < kTinyLength2) return kInfinity;
        const double t = ((px - ax) * vx + (py - ay) * vy) / l2;
        if (t < kEndMargin || t > 1 - kEndMargin) return kInfinity;
        const double cx = ax + vx * t - px, cy = ay + vy * t - py;
        return cx * cx + cy * cy;
    }
}
