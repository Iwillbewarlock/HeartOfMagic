/**
 * LayoutLineClear - move spells so the tree's straight lines pass them by.
 *
 * Part of LayoutDeclutter (run before its spell-against-spell pass). A line
 * from a parent to a child should keep `clear` tree units from the centre of
 * every spell it does not end at, and the lines stay straight - so the spells
 * move, not the lines. Pushing spells off lines with forces did not work on a
 * dense tree (pushes from many lines cancel out, and spells jammed together):
 * this searches instead. For each spell that is in the way, or whose own
 * lines run through others or meet too narrowly, it tries DIRECTIONS x RADII
 * spots round where the layout put it and round where it is now, and moves it
 * to the cheapest:
 *
 *   cost = lines passing within `clear` of the spot
 *        + other spells within `clear` of the spell's own lines from the spot
 *        + ANGLE_COST for two lines meeting narrower than MIN_ANGLE, at the
 *          spell and at each spell it is joined to, times 1 + the shorter
 *          line's length / LONG_LINE (long lines at a narrow angle run
 *          together a long way: the bundles out of a spell with many children)
 *        + LINE_GAP_COST for each other line its own lines run closer than
 *          LINE_GAP to (more room for lines side by side a long way: a
 *          bundle), or cross at less than MIN_CROSS
 *        + OVERLAP_COST for every spell closer than minDist (more the closer)
 *        + HEART_COST on the heart
 *        + MOVE_COST x distance from where the layout put it
 *
 * Spells are taken in list order, PASSES times or until a pass moves none; a
 * pass after the first looks only at spells whose surroundings changed.
 * Roots never move; a spell kept in a sector only tries spots inside it.
 * Deterministic.
 *
 * The plugin runs the same search in C++ (LayoutLineClear*.cpp, LayoutLineGrid.cpp):
 * a change here goes there too (see LayoutDeclutter).
 *
 * Depends on: layoutLineGrid.js (its grids and fans, loaded right after this file);
 *   called by LayoutDeclutter with its items
 */

var LayoutLineClear = {

    RADII: [10, 20, 32, 46, 64],
    DIRECTIONS: 12,
    PASSES: 6,
    MOVE_COST: 0.01,       // per tree unit away from the layout's spot
    OVERLAP_COST: 6,       // spells closer than minDist: up to this, more the closer...
    OVERLAP_BASE: 2,       // ...plus this
    HEART_COST: 5,
    MIN_ANGLE: 30 * Math.PI / 180,  // two lines meeting at a spell should be at least this far apart...
    ANGLE_COST: 3,                  // ...or cost up to this (more the narrower)
    LONG_LINE: 150,                 // a narrow angle between lines this long costs double (longer: more)
    REACH_LINE: 200,                // a spell whose longest line is longer than this searches further...
    MAX_REACH_SCALE: 3,             // ...up to this many times RADII (far out, a small move turns a line little)
    LINE_GAP: 11,                   // two lines not sharing a spell keep this far apart (8 px at the default zoom)...
    MIN_CROSS: 15 * Math.PI / 180,  // ...and where they cross, cross at least this steeply...
    LINE_GAP_COST: 1,               // ...or cost up to this
    BUNDLE_ANGLE: 20 * Math.PI / 180, // two lines closer to parallel than this, running side by side...
    BUNDLE_LEN: 250,                // ...need LINE_GAP more room for every this much they run together...
    BUNDLE_MAX: 3,                  // ...up to this many times LINE_GAP
    GOOD_ENOUGH: 0.5,      // a spell costing less than this is left where it is
    CELL: 50,              // spatial grid cell, tree units
    END_MARGIN: 0.05,      // near a line's own ends (share of its length) other spells do not count

    /**
     * All at once (tests, and anything that cannot wait).
     * @param {Array} list - LayoutDeclutter items ({x, y, fixed, sector, index})
     * @param {Array} edges - [itemA, itemB] pairs (the lines)
     * @param {{x, y, r}} heart
     * @param {{clear: number, minDist: number, inSector: function(sector, x, y)}} opts
     * @returns {{moved: number, passes: number}}
     */
    run: function(list, edges, heart, opts) {
        var job = this.start(list, edges, heart, opts);
        this.step(job, Infinity);
        return job.result;
    },

    /**
     * Set up a search to be worked through with step() - a piece at a time, so
     * the panel keeps drawing (the game's browser takes seconds for a big
     * tree). One job at a time: starting another ends the one before.
     * Arguments as run().
     */
    start: function(list, edges, heart, opts) {
        var i;
        this._clear2 = opts.clear * opts.clear;
        this._opts = opts;
        this._heart = heart;
        this._incident = [];
        for (i = 0; i < list.length; i++) {
            list[i].ox = list[i].x;
            list[i].oy = list[i].y;
            list[i]._v = 0;
            this._incident.push([]);
        }
        for (i = 0; i < edges.length; i++) {
            edges[i]._v0 = edges[i]._v1 = -1;
            this._incident[edges[i][0].index].push(edges[i]);
            this._incident[edges[i][1].index].push(edges[i]);
        }
        this._nodeGrid = {};
        for (i = 0; i < list.length; i++) this._gridAdd(list[i]);
        this._stamp = 0;

        // The spots to try, as offsets, worked out once
        var offsets = [];
        for (var ri = 0; ri < this.RADII.length; ri++) {
            for (var di = 0; di < this.DIRECTIONS; di++) {
                var ang = di / this.DIRECTIONS * 2 * Math.PI;
                offsets.push(Math.cos(ang) * this.RADII[ri], Math.sin(ang) * this.RADII[ri]);
            }
        }
        // A pass looks again only at spells whose surroundings changed in the
        // one before (dirty): moved ones, those joined to them, those near them
        // or near their lines. The first pass looks at all of them.
        var dirty = [];
        for (i = 0; i < list.length; i++) dirty.push(true);
        this._job = {
            list: list, edges: edges, offsets: offsets, dirty: dirty, next: null,
            movedIds: {}, pass: 0, i: 0, moved: 0, progress: 0, result: null
        };
        return this._job;
    },

    /**
     * Work on `job` for about `budgetMs` (Infinity: to the end). Returns true
     * once it is done (job.result set); job.progress runs 0..1.
     */
    step: function(job, budgetMs) {
        if (job.result) return true;
        if (this._job !== job) {                   // another search took over
            job.result = { moved: 0, passes: job.pass, cancelled: true };
            return true;
        }
        var clock = (typeof performance !== 'undefined') ? performance : Date;
        var until = budgetMs === Infinity ? Infinity : clock.now() + budgetMs;
        var list = job.list;
        while (job.pass < this.PASSES) {
            if (job.i === 0) {
                this._buildEdgeGrid(job.edges);
                job.next = [];
                for (var n = 0; n < list.length; n++) job.next.push(false);
                job.moved = 0;
            }
            while (job.i < list.length) {
                var at = job.i++;
                if (!list[at].fixed && job.dirty[at] && this._searchOne(list[at], job)) job.moved++;
                if (until !== Infinity && clock.now() >= until) {
                    job.progress = (job.pass + job.i / list.length) / this.PASSES;
                    return false;
                }
            }
            job.i = 0;
            if (!job.moved) break;
            job.dirty = job.next;
            job.pass++;
        }
        job.progress = 1;
        job.result = { moved: Object.keys(job.movedIds).length, passes: job.pass };
        this._job = null;
        return true;
    },

    /** Try the spots round one spell and move it to the cheapest; true if it moved. */
    _searchOne: function(it, job) {
        this._fan = this._edgeFan = this._angleFan = null;
        var best = this._cost(it, it.x, it.y);
        if (best < this.GOOD_ENOUGH) return false;
        it._reach = this._reachScale(it);
        // Gathered once for the widest ring, then cut down per ring (see _ringSpells)
        this._fan = this._ringSpells(it, this._gatherFan(it));   // sets _edgeFan too
        var perRing = 2 * this.DIRECTIONS;
        this._angleFan = [];
        for (var a = 0; a < this._incident[it.index].length; a++) this._angleFan.push(this._neighbourLines(it, a));
        var bx = it.x, by = it.y, offsets = job.offsets, opts = this._opts;
        // Round where the layout put it, and round where it is now (once
        // it has moved away from there)
        var rounds = (it.x !== it.ox || it.y !== it.oy) ? 2 : 1;
        // Nearest spots first (offsets go out ring by ring): the first
        // spot with nothing wrong at all ends the search
        var sc = it._reach;
        for (var o = 0; o < offsets.length && best >= this.GOOD_ENOUGH; o += 2) {
            this._ring = Math.floor(o / perRing);
            for (var k = 0; k < rounds; k++) {
                var x = (k ? it.x : it.ox) + offsets[o] * sc, y = (k ? it.y : it.oy) + offsets[o + 1] * sc;
                if (it.sector && !opts.inSector(it.sector, x, y)) continue;
                var c = this._cost(it, x, y, best);
                if (c < best - 0.01) { best = c; bx = x; by = y; }
            }
        }
        this._fan = this._edgeFan = this._angleFan = null;
        if (bx === it.x && by === it.y) return false;
        this._markAround(it, job.next);
        this._gridMove(it, bx, by);
        this._markAround(it, job.next);
        job.movedIds[it.index] = true;
        return true;
    },

    /** How much further than RADII `it` searches: more for long lines (see REACH_LINE). */
    _reachScale: function(it) {
        var own = this._incident[it.index], longest = 0;
        for (var i = 0; i < own.length; i++) {
            var o = own[i][0] === it ? own[i][1] : own[i][0];
            var dx = o.x - it.x, dy = o.y - it.y;
            var len = Math.sqrt(dx * dx + dy * dy);
            if (len > longest) longest = len;
        }
        return Math.max(1, Math.min(this.MAX_REACH_SCALE, longest / this.REACH_LINE));
    },

    /** Mark for the next pass what a move of `it` (at where it is now) changes. */
    _markAround: function(it, marks) {
        var i, k, near;
        marks[it.index] = true;
        var own = this._incident[it.index];
        for (i = 0; i < own.length; i++) {
            var o = own[i][0] === it ? own[i][1] : own[i][0];
            marks[o.index] = true;
            // Angles at the neighbour involve its other neighbours
            var theirs = this._incident[o.index];
            for (k = 0; k < theirs.length; k++) marks[theirs[k][0].index] = marks[theirs[k][1].index] = true;
            // Spells along the line
            near = this._nearLine(it.x, it.y, o.x, o.y);
            for (k = 0; k < near.length; k++) marks[near[k].index] = true;
            // Lines running close to it (their ends' line gap cost changes)
            var lines = this._edgesNear(own[i], it.x, it.y, o.x, o.y, 0);
            for (k = 0; k < lines.length; k++) marks[lines[k][0].index] = marks[lines[k][1].index] = true;
        }
        // Spells close by (lines past them, overlap)
        var reach = Math.max(this._opts.minDist, Math.sqrt(this._clear2));
        near = this._near(it.x, it.y, it.x, it.y, reach);
        for (k = 0; k < near.length; k++) marks[near[k].index] = true;
    },

    /** Lines passing within `clear` of a spell they do not end at (pairs), for the log. */
    countLinesThrough: function(list, edges) {
        // Where the spells are now (LayoutDeclutter moves them after run)
        this._nodeGrid = {};
        for (var i = 0; i < list.length; i++) this._gridAdd(list[i]);
        var count = 0;
        for (var e = 0; e < edges.length; e++) {
            var p = edges[e][0], q = edges[e][1];
            var near = this._nearLine(p.x, p.y, q.x, q.y);
            for (var k = 0; k < near.length; k++) {
                var n = near[k];
                if (n !== p && n !== q && this._segDist2(n.x, n.y, p.x, p.y, q.x, q.y) < this._clear2) count++;
            }
        }
        return count;
    },

    // =========================================================================
    // COST
    // =========================================================================

    /** The cost of `it` at (x, y); stops counting once it reaches `limit` (optional). */
    _cost: function(it, x, y, limit) {
        if (limit === undefined) limit = Infinity;
        var c = 0, i, k;
        // Cheap parts first: once past `limit` the spot is out, and the line
        // parts (the dear ones) are not worked out at all
        var mx = x - it.ox, my = y - it.oy;
        c += this.MOVE_COST * Math.sqrt(mx * mx + my * my);
        var hx = x - this._heart.x, hy = y - this._heart.y;
        if (hx * hx + hy * hy < this._heart.r * this._heart.r) c += this.HEART_COST;
        // Other spells too close (the cells of _near, walked in place: this runs
        // for every spot tried, and the game's browser has no JIT)
        var min = this._opts.minDist, cell = this.CELL, grid = this._nodeGrid;
        // Squared first, the root only for the few that are close: a hair over
        // min squared, so rounding never skips one the root would count
        var min2 = min * min * 1.000001;
        var cx0 = Math.floor((x - min) / cell), cx1 = Math.floor((x + min) / cell);
        var cy0 = Math.floor((y - min) / cell), cy1 = Math.floor((y + min) / cell);
        var span = this.KEY_SPAN, wide = 2 * span;
        var inRange = cx0 >= -span && cx1 < span && cy0 >= -span && cy1 < span;
        for (var cx = cx0; cx <= cx1; cx++) {
            for (var cy = cy0; cy <= cy1; cy++) {
                var close = grid[inRange ? (cx + span) * wide + (cy + span) : this._cellKey(cx, cy)];
                if (!close) continue;
                for (k = 0; k < close.length; k++) {
                    var s = close[k];
                    if (s === it) continue;
                    var dx = s.x - x, dy = s.y - y, d2 = dx * dx + dy * dy;
                    if (d2 >= min2) continue;
                    var d = Math.sqrt(d2);
                    if (d < min) c += this.OVERLAP_COST * (1 - d / min) + this.OVERLAP_BASE;
                }
            }
        }
        if (c >= limit) return c;
        // Lines meeting at too narrow an angle, here and at its neighbours
        c += this._angleCost(it, x, y);
        if (c >= limit) return c;
        // Lines passing the spot (_segDist2 written out, as below)
        var clear2 = this._clear2, lo = this.END_MARGIN, hi = 1 - this.END_MARGIN;
        var vx, vy, l2, t, px, py;
        var lx = Math.floor(x / cell), ly = Math.floor(y / cell);          // _key written out
        var lines = this._edgeGrid[inRange ? (lx + span) * wide + (ly + span) : this._cellKey(lx, ly)];
        if (lines) {
            for (i = 0; i < lines.length; i++) {
                var e = lines[i], p = e[0], q = e[1];
                if (p === it || q === it) continue;
                vx = q.x - p.x; vy = q.y - p.y; l2 = vx * vx + vy * vy;
                if (l2 < 0.0001) continue;
                t = ((x - p.x) * vx + (y - p.y) * vy) / l2;
                if (t < lo || t > hi) continue;
                px = p.x + vx * t - x; py = p.y + vy * t - y;
                if (px * px + py * py < clear2) c += 1;
            }
            if (c >= limit) return c;
        }
        // Its own lines from the spot, past other spells
        var own = this._incident[it.index];
        for (i = 0; i < own.length; i++) {
            var o = own[i][0] === it ? own[i][1] : own[i][0];
            var near = this._fan ? this._fan[i] : this._nearLine(x, y, o.x, o.y);
            var count = this._fan ? this._fanEnd[i][this._ring] : near.length;   // this ring's share (_ringSpells)
            vx = o.x - x; vy = o.y - y; l2 = vx * vx + vy * vy;
            if (l2 < 0.0001) continue;
            for (k = 0; k < count; k++) {
                var m = near[k];
                if (m === it || m === o) continue;
                t = ((m.x - x) * vx + (m.y - y) * vy) / l2;
                if (t < lo || t > hi) continue;
                px = x + vx * t - m.x; py = y + vy * t - m.y;
                if (px * px + py * py < clear2) c += 1;
            }
            if (c >= limit) return c;
        }
        // Its own lines against other lines running too close (the dearest)
        return c + this._lineGapCost(it, x, y, limit - c);
    },

    /**
     * LINE_GAP_COST for each other line one of `it`'s lines (from (x, y)) runs
     * closer than LINE_GAP to without crossing it, or crosses at less than
     * MIN_CROSS. Lines sharing a spell are the angle cost's business.
     * _cross and _pointSeg2 are written out (the same sums, so the same
     * results): this is the innermost loop of the search, and without a JIT
     * the calls cost more than the sums.
     */
    _lineGapCost: function(it, x, y, limit) {
        var own = this._incident[it.index];
        var gap = this.LINE_GAP, gap2 = gap * gap, c = 0, box = gap * this.BUNDLE_MAX;
        var bundle2 = gap2 * this.BUNDLE_MAX * this.BUNDLE_MAX;
        var t, dx, dy, d2, d;
        for (var i = 0; i < own.length; i++) {
            var o = own[i][0] === it ? own[i][1] : own[i][0];
            var others = this._edgeFan ? this._edgeFan[i] : this._boxed(this._edgesNear(own[i], x, y, o.x, o.y, box));
            var minX = Math.min(x, o.x) - box, maxX = Math.max(x, o.x) + box;
            var minY = Math.min(y, o.y) - box, maxY = Math.max(y, o.y) + box;
            var ang = -1;
            // This line, (x, y) to o
            var vx = o.x - x, vy = o.y - y, l2 = vx * vx + vy * vy;
            for (var k = 0; k < others.length; k++) {
                var e = others[k];
                if (e._x1 < minX || e._x0 > maxX || e._y1 < minY || e._y0 > maxY) continue;
                var p = e[0], q = e[1];
                if (p === it || q === it || p === o || q === o) continue;
                var ex = e._ex, ey = e._ey;
                // _cross(x, y, o.x, o.y, p.x, p.y, q.x, q.y)
                var crossing = false;
                if ((vx * (p.y - y) - vy * (p.x - x)) * (vx * (q.y - y) - vy * (q.x - x)) < 0) {
                    crossing = (ex * (y - p.y) - ey * (x - p.x)) * (ex * (o.y - p.y) - ey * (o.x - p.x)) < 0;
                }
                if (crossing) {
                    if (ang < 0) ang = Math.atan2(o.y - y, o.x - x);
                    var an = Math.abs(ang - e._ang) % Math.PI;
                    if (an > Math.PI / 2) an = Math.PI - an;
                    if (an < this.MIN_CROSS) c += this.LINE_GAP_COST * (1 - an / this.MIN_CROSS);
                } else {
                    // The nearest of: p and q to this line, its ends to that one (_pointSeg2 each)
                    t = l2 > 0 ? ((p.x - x) * vx + (p.y - y) * vy) / l2 : 0;
                    if (t < 0) t = 0; else if (t > 1) t = 1;
                    dx = x + vx * t - p.x; dy = y + vy * t - p.y;
                    d2 = dx * dx + dy * dy;
                    t = l2 > 0 ? ((q.x - x) * vx + (q.y - y) * vy) / l2 : 0;
                    if (t < 0) t = 0; else if (t > 1) t = 1;
                    dx = x + vx * t - q.x; dy = y + vy * t - q.y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    var el2 = e._l2;
                    t = el2 > 0 ? ((x - p.x) * ex + (y - p.y) * ey) / el2 : 0;
                    if (t < 0) t = 0; else if (t > 1) t = 1;
                    dx = p.x + ex * t - x; dy = p.y + ey * t - y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    t = el2 > 0 ? ((o.x - p.x) * ex + (o.y - p.y) * ey) / el2 : 0;
                    if (t < 0) t = 0; else if (t > 1) t = 1;
                    dx = p.x + ex * t - o.x; dy = p.y + ey * t - o.y;
                    d = dx * dx + dy * dy;
                    if (d < d2) d2 = d;
                    if (d2 < bundle2) {
                        var need = this._bundleGap(x, y, o.x, o.y, e, gap);
                        if (d2 < need * need) c += this.LINE_GAP_COST * (1 - Math.sqrt(d2) / need) * (need / gap);
                    }
                }
                if (c >= limit) return c;
            }
        }
        return c;
    },

    /**
     * The room two lines need: LINE_GAP, plus LINE_GAP for every BUNDLE_LEN
     * they run side by side when they are closer to parallel than
     * BUNDLE_ANGLE (at most BUNDLE_MAX x LINE_GAP).
     */
    _bundleGap: function(ax, ay, bx, by, e, gap) {
        var vx = bx - ax, vy = by - ay;
        var len = Math.sqrt(vx * vx + vy * vy);
        if (len < 0.001) return gap;
        var an = Math.abs(Math.atan2(vy, vx) - e._ang) % Math.PI;
        if (an > Math.PI / 2) an = Math.PI - an;
        if (an >= this.BUNDLE_ANGLE) return gap;
        // How far along this line the other one runs beside it
        var ux = vx / len, uy = vy / len;
        var t0 = (e[0].x - ax) * ux + (e[0].y - ay) * uy, t1 = (e[1].x - ax) * ux + (e[1].y - ay) * uy;
        var side = Math.min(len, Math.max(t0, t1)) - Math.max(0, Math.min(t0, t1));
        if (side <= 0) return gap;
        return gap * Math.min(this.BUNDLE_MAX, 1 + side / this.BUNDLE_LEN);
    },

    /** Do a-b and c-d cross (strictly)? */
    _cross: function(ax, ay, bx, by, cx, cy, dx, dy) {
        var o1 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        var o2 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
        var o3 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx);
        var o4 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
        return o1 * o2 < 0 && o3 * o4 < 0;
    },

    /** Squared distance from a point to the whole segment a-b (ends included). */
    _pointSeg2: function(px, py, ax, ay, bx, by) {
        var vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
        var t = l2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / l2 : 0;
        if (t < 0) t = 0; else if (t > 1) t = 1;
        var dx = ax + vx * t - px, dy = ay + vy * t - py;
        return dx * dx + dy * dy;
    },

    /**
     * ANGLE_COST for each pair of lines meeting narrower than MIN_ANGLE, with
     * `it` at (x, y). _narrow's test is written out and _narrow called only
     * for a pair it counts: without a JIT a call costs far more than the
     * angle itself (atan2 is built in), and most pairs are wide.
     */
    _angleCost: function(it, x, y) {
        var own = this._incident[it.index];
        if (!own.length) return 0;
        var c = 0, i, j, k, d, n = own.length;
        var pi = Math.PI, minAngle = this.MIN_ANGLE;
        // Reused; only the first n are read (setting length is not free either)
        var dirs = this._dirs || (this._dirs = []), lens = this._lens || (this._lens = []);
        var fan = this._angleFan;
        for (i = 0; i < n; i++) {
            var o = own[i][0] === it ? own[i][1] : own[i][0];
            var ox = o.x - x, oy = o.y - y;
            var len = Math.sqrt(ox * ox + oy * oy);
            dirs[i] = Math.atan2(oy, ox);
            lens[i] = len;
            // At the neighbour: the line to here against its other lines
            var back = Math.atan2(-oy, -ox);
            var theirs = fan ? fan[i] : this._neighbourLines(it, i);
            for (k = 0; k < theirs.length; k += 2) {
                d = Math.abs(back - theirs[k]);
                if (d > pi) d = 2 * pi - d;
                if (d < minAngle) c += this._narrow(back, theirs[k], Math.min(len, theirs[k + 1]));
            }
        }
        // At the spell itself
        for (i = 0; i < n; i++) {
            for (j = i + 1; j < n; j++) {
                d = Math.abs(dirs[i] - dirs[j]);
                if (d > pi) d = 2 * pi - d;
                if (d < minAngle) c += this._narrow(dirs[i], dirs[j], Math.min(lens[i], lens[j]));
            }
        }
        return c;
    },

    /**
     * The other lines at the far end of `it`'s line i, as [direction, length,
     * ...]: they do not change while `it` alone tries spots (see _angleFan).
     */
    _neighbourLines: function(it, i) {
        var own = this._incident[it.index], out = [];
        var o = own[i][0] === it ? own[i][1] : own[i][0];
        var theirs = this._incident[o.index];
        for (var k = 0; k < theirs.length; k++) {
            if (theirs[k] === own[i]) continue;
            var q = theirs[k][0] === o ? theirs[k][1] : theirs[k][0];
            var qx = q.x - o.x, qy = q.y - o.y;
            out.push(Math.atan2(qy, qx), Math.sqrt(qx * qx + qy * qy));
        }
        return out;
    },

    /** The cost of two directions (radians) being closer than MIN_ANGLE; `len` the shorter line. */
    _narrow: function(a, b, len) {
        var d = Math.abs(a - b);
        if (d > Math.PI) d = 2 * Math.PI - d;
        if (d >= this.MIN_ANGLE) return 0;
        return this.ANGLE_COST * (1 - d / this.MIN_ANGLE) * (1 + len / this.LONG_LINE);
    },

    /** Squared distance from (px, py) to the line a-b, away from its ends; Infinity near them. */
    _segDist2: function(px, py, ax, ay, bx, by) {
        var vx = bx - ax, vy = by - ay;
        var l2 = vx * vx + vy * vy;
        if (l2 < 0.0001) return Infinity;
        var t = ((px - ax) * vx + (py - ay) * vy) / l2;
        if (t < this.END_MARGIN || t > 1 - this.END_MARGIN) return Infinity;
        var cx = ax + vx * t - px, cy = ay + vy * t - py;
        return cx * cx + cy * cy;
    }
};

if (typeof window !== 'undefined') window.LayoutLineClear = LayoutLineClear;
