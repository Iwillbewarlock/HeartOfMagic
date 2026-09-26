#include "treebuilder/LayoutDeclutter.h"
#include "treebuilder/LayoutDeclutterInternal.h"
#include "treebuilder/LayoutMath.h"

#include <algorithm>
#include <chrono>

// =============================================================================
// LAYOUT DECLUTTER (modules/layoutDeclutter.js)
// =============================================================================
//
//   1. the whole tree is spaced out by SPREAD, to make room between lines;
//   2. spells are moved off the tree's lines (LineClear, a search);
//   3. spells are pushed apart, a little at a time, until no two are closer
//      than 2 x NODE_RADIUS + GAP and none sits on the heart, or ITERATIONS
//      rounds have passed.
// School roots do not move, and a spell inside its school's sector is kept
// inside it (not for flat or unturned layouts). Deterministic.

namespace LayoutDeclutter::Internal
{
    // =========================================================================
    // SECTORS
    // =========================================================================

    double AngleDiff(double a, double b)
    {
        double d = a - b;
        while (d > kPi) d -= 2 * kPi;
        while (d < -kPi) d += 2 * kPi;
        return d;
    }

    bool InSector(const Sector& sector, double x, double y)
    {
        return std::fabs(AngleDiff(LayoutMath::Atan2(y, x), sector.mid)) <= sector.half;
    }
}

namespace
{
    using namespace LayoutDeclutter::Internal;
    using json = nlohmann::json;

    // =========================================================================
    // READING THE REQUEST
    // =========================================================================

    // JavaScript truthiness of a JSON value (!!value)
    bool Truthy(const json& v)
    {
        if (v.is_null()) return false;
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_number()) {
            const double d = v.get<double>();
            return d != 0 && !std::isnan(d);
        }
        if (v.is_string()) return !v.get_ref<const std::string&>().empty();
        return true;  // objects and arrays
    }

    // The property name JavaScript would look a value up by (byId[value])
    std::string JsKey(const json& v)
    {
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
        if (v.is_number_unsigned()) return std::to_string(v.get<std::uint64_t>());
        if (v.is_null()) return "null";
        if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
        return v.dump();
    }

    // _sector: the school's centre angle and half width, or none
    bool ReadSector(const json& school, Sector& out)
    {
        if (!school.is_object()) return false;
        auto start = school.find("startAngle");
        auto end = school.find("endAngle");
        if (start == school.end() || end == school.end() || !start->is_number() || !end->is_number()) return false;
        const double a0 = start->get<double>() * kPi / 180, a1 = end->get<double>() * kPi / 180;
        const double half = (a1 - a0) / 2;
        if (!(half > 0) || half >= kPi) return false;
        out.mid = a0 + half;
        out.half = half;
        return true;
    }

    struct Collected
    {
        std::vector<Item> list;
        std::unordered_map<std::string, int> byId;
    };

    void CollectSchool(const json& school, bool useSectors, Collected& out)
    {
        if (!school.is_object()) return;
        auto nodes = school.find("nodes");
        if (nodes == school.end() || !nodes->is_array()) return;
        Sector sector;
        const bool hasSector = useSectors && ReadSector(school, sector);
        for (const auto& n : *nodes) {
            if (!n.is_object()) continue;
            auto x = n.find("x");
            auto y = n.find("y");
            if (x == n.end() || y == n.end() || !x->is_number() || !y->is_number()) continue;
            Item it;
            it.nodeX = it.x = x->get<double>();
            it.nodeY = it.y = y->get<double>();
            if (std::isnan(it.x) || std::isnan(it.y)) continue;
            auto root = n.find("isRoot");
            it.fixed = root != n.end() && Truthy(*root);
            it.index = static_cast<int>(out.list.size());
            // Kept in its sector only if it started there
            if (hasSector && InSector(sector, it.x, it.y)) {
                it.hasSector = true;
                it.sector = sector;
            }
            auto formId = n.find("formId");
            const bool hasId = formId != n.end() && Truthy(*formId);
            if (hasId) it.formId = JsKey(*formId);
            auto children = n.find("children");
            if (children != n.end() && children->is_array()) {
                for (const auto& c : *children) it.children.push_back(JsKey(c));
            }
            if (hasId) out.byId[it.formId] = it.index;
            out.list.push_back(std::move(it));
        }
    }

    // _collect: schools by name (or in the order sent), nodes in array order
    Collected Collect(const json& schools, bool useSectors)
    {
        Collected out;
        if (schools.is_array()) {
            for (const auto& school : schools) CollectSchool(school, useSectors, out);
        } else if (schools.is_object()) {
            for (const auto& [name, school] : schools.items()) CollectSchool(school, useSectors, out);  // std::map: by name
        }
        return out;
    }

    // _edges: the lines the tree always draws, parent to child, once each
    std::vector<Edge> MakeEdges(const Collected& items)
    {
        std::vector<Edge> out;
        std::unordered_set<std::int64_t> seen;
        const std::int64_t n = static_cast<std::int64_t>(items.list.size());
        for (const auto& it : items.list) {
            for (const auto& id : it.children) {
                auto found = items.byId.find(id);
                if (found == items.byId.end() || found->second == it.index) continue;
                const int other = found->second;
                const std::int64_t key = it.index < other ? it.index * n + other : other * n + it.index;
                if (!seen.insert(key).second) continue;
                Edge e;
                e.a = it.index;
                e.b = other;
                out.push_back(e);
            }
        }
        return out;
    }

    // =========================================================================
    // ONE ROUND (apart and off the heart)
    // =========================================================================

    Grid MakeGrid(const std::vector<Item>& list, double cell)
    {
        Grid g;
        for (const auto& it : list) g[MakeCellKey(FloorCell(it.x, cell), FloorCell(it.y, cell))].push_back(it.index);
        return g;
    }

    void NearIn(const Grid& grid, double cell, double x0, double y0, double x1, double y1, std::vector<int>& out)
    {
        out.clear();
        const std::int64_t cx0 = FloorCell(x0, cell), cx1 = FloorCell(x1, cell);
        const std::int64_t cy0 = FloorCell(y0, cell), cy1 = FloorCell(y1, cell);
        for (std::int64_t cx = cx0; cx <= cx1; cx++) {
            for (std::int64_t cy = cy0; cy <= cy1; cy++) {
                auto bucket = grid.find(MakeCellKey(cx, cy));
                if (bucket != grid.end()) out.insert(out.end(), bucket->second.begin(), bucket->second.end());
            }
        }
    }

    void PushPair(Item& a, Item& b, double ux, double uy, double overlap)
    {
        const double share = (a.fixed || b.fixed) ? overlap : overlap / 2;
        if (!a.fixed) {
            a.dx -= ux * share;
            a.dy -= uy * share;
        }
        if (!b.fixed) {
            b.dx += ux * share;
            b.dy += uy * share;
        }
    }

    // Keep a point within its sector's angles (radius kept), a spell's width inside the edge
    void ClampToSector(const Sector& sector, double& x, double& y)
    {
        const double r = std::sqrt(x * x + y * y);
        if (r < kTinyDistance) return;
        const double diff = AngleDiff(LayoutMath::Atan2(y, x), sector.mid);
        const double margin = std::min(sector.half * 0.5, kNodeRadius / r);
        const double limit = sector.half - margin;
        if (std::fabs(diff) <= limit) return;
        const double a = sector.mid + (diff > 0 ? limit : -limit);
        x = LayoutMath::Cos(a) * r;
        y = LayoutMath::Sin(a) * r;
    }

    // Push everything that is too close apart once; returns the largest move
    double Round(std::vector<Item>& list, const Heart& heart, std::vector<int>& nearby)
    {
        for (auto& it : list) it.dx = it.dy = 0;
        const double minDist = 2 * kNodeRadius + kGap;
        const Grid grid = MakeGrid(list, minDist);

        // Spell against spell
        for (auto& a : list) {
            NearIn(grid, minDist, a.x - minDist, a.y - minDist, a.x + minDist, a.y + minDist, nearby);
            for (const int bi : nearby) {
                Item& b = list[bi];
                if (b.index <= a.index) continue;
                double dx = b.x - a.x, dy = b.y - a.y;
                double d = std::sqrt(dx * dx + dy * dy);
                if (d >= minDist) continue;
                if (d < kTinyDistance) {
                    const double ang = b.index * kGoldenAngle;
                    dx = LayoutMath::Cos(ang);
                    dy = LayoutMath::Sin(ang);
                    d = kTinyDistance;
                }
                PushPair(a, b, dx / d, dy / d, minDist - d);
            }
        }

        // Off the heart, then move: each spell by its pushes, at most MAX_STEP
        double biggest = 0;
        for (auto& it : list) {
            if (it.fixed) continue;
            double hx = it.x - heart.x, hy = it.y - heart.y;
            double hd = std::sqrt(hx * hx + hy * hy);
            if (hd < heart.r) {
                if (hd < kTinyDistance) {
                    hx = LayoutMath::Cos(it.index * kGoldenAngle);
                    hy = LayoutMath::Sin(it.index * kGoldenAngle);
                    hd = 1;
                }
                it.dx += hx / hd * (heart.r - hd);
                it.dy += hy / hd * (heart.r - hd);
            }
            const double len = std::sqrt(it.dx * it.dx + it.dy * it.dy);
            if (len < kTinyDistance) continue;
            const double step = std::min(len, kMaxStep);
            double nx = it.x + it.dx / len * step, ny = it.y + it.dy / len * step;
            if (it.hasSector) ClampToSector(it.sector, nx, ny);
            const double moved = std::sqrt((nx - it.x) * (nx - it.x) + (ny - it.y) * (ny - it.y));
            if (moved > biggest) biggest = moved;
            it.x = nx;
            it.y = ny;
        }
        return biggest;
    }

    // Pairs of spells whose drawn shapes still touch (for the log)
    int CountOverlaps(const std::vector<Item>& list)
    {
        const double minDist = 2 * kTouchRadius;
        const Grid grid = MakeGrid(list, minDist);
        std::vector<int> nearby;
        int count = 0;
        for (const auto& a : list) {
            NearIn(grid, minDist, a.x - minDist, a.y - minDist, a.x + minDist, a.y + minDist, nearby);
            for (const int bi : nearby) {
                const Item& b = list[bi];
                if (b.index <= a.index) continue;
                const double dx = b.x - a.x, dy = b.y - a.y;
                if (dx * dx + dy * dy < minDist * minDist) count++;
            }
        }
        return count;
    }

    // JavaScript's Math.round: halves up (toward +infinity), -0 kept
    double JsRound(double v)
    {
        if (!std::isfinite(v)) return v;
        const double f = std::floor(v);
        const double r = (v - f >= 0.5) ? f + 1 : f;
        if (r == 0 && std::signbit(v)) return -0.0;
        return r;
    }

    constexpr double kRoundTo = 100;   // positions are saved to 0.01
    constexpr double kMovedOver = 0.5; // a spell moved further than this counts as moved
}

namespace LayoutDeclutter
{
    // =========================================================================
    // RUN
    // =========================================================================

    json Run(const json& request)
    {
        const auto t0 = std::chrono::steady_clock::now();
        json reply = json::object();
        reply["id"] = request.contains("id") ? request["id"] : json();
        reply["positions"] = json::array();

        const json empty = json::object();
        const json& schools = request.contains("schools") ? request["schools"] : empty;
        const std::string layoutMode = request.contains("layoutMode") && request["layoutMode"].is_string() ?
            request["layoutMode"].get<std::string>() : std::string();
        const bool noRotate = request.contains("noRotate") && request["noRotate"].is_boolean() && request["noRotate"].get<bool>();
        // Sectors are wedges round the centre: a flat or unturned layout does not keep its schools in them
        const bool useSectors = !(layoutMode == "flat" || noRotate);
        Collected items = Collect(schools, useSectors);
        auto& list = items.list;
        if (list.size() < 2) {
            reply["moved"] = 0;
            reply["rounds"] = 0;
            reply["overlapsLeft"] = 0;
            reply["skipped"] = true;
            return reply;
        }

        // The globe: `x || 0`, `radius || 45`
        Heart heart;
        const json& globe = request.contains("globe") && request["globe"].is_object() ? request["globe"] : empty;
        auto number = [&globe](const char* key, double fallback) {
            if (!globe.contains(key) || !Truthy(globe[key]) || !globe[key].is_number()) return fallback;
            return globe[key].get<double>();
        };
        heart.x = number("x", 0);
        heart.y = number("y", 0);
        heart.r = number("radius", kDefaultGlobeRadius) + kHeartClearance;

        // Room first: every spell (roots too) out from the centre by SPREAD
        for (auto& it : list) {
            it.x *= kSpread;
            it.y *= kSpread;
        }
        // Then off the lines, then apart and off the heart
        std::vector<Edge> edges = MakeEdges(items);
        LineClear lines(list, edges, heart, kLineClear, 2 * kNodeRadius + kGap);
        lines.Run();

        int rounds = 0;
        std::vector<int> nearby;
        while (rounds < kIterations) {
            if (Round(list, heart, nearby) < kDoneBelow) break;
            rounds++;
        }

        int moved = 0;
        auto& positions = reply["positions"];
        for (const auto& it : list) {
            const double nx = JsRound(it.x * kRoundTo) / kRoundTo, ny = JsRound(it.y * kRoundTo) / kRoundTo;
            if (std::fabs(nx - it.nodeX) > kMovedOver || std::fabs(ny - it.nodeY) > kMovedOver) moved++;
            positions.push_back(json::array({ it.formId, nx, ny }));
        }
        const int left = CountOverlaps(list);
        const int linesLeft = lines.CountLinesThrough();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        logger::info("[LayoutDeclutter] spread x{}, {} of {} spells moved ({} off lines in {} passes), "
                     "{} pairs still touching, {} spells still on a line, {} ms (native)",
            kSpread, moved, list.size(), lines.Moved(), lines.Passes(), left, linesLeft, static_cast<long long>(ms + 0.5));

        reply["moved"] = moved;
        reply["rounds"] = rounds;
        reply["overlapsLeft"] = left;
        reply["linesLeft"] = linesLeft;
        reply["linesMoved"] = lines.Moved();
        reply["passes"] = lines.Passes();
        reply["ms"] = ms;
        return reply;
    }
}
