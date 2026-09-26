/**
 * LayoutDeclutter - no spell hidden under another spell, a line or the heart.
 *
 * Every tree mode (classic, tree, graph, oracle, thematic) places spells on
 * its own and bakes x/y into the tree it saves. None of them checked what the
 * tree then looks like: spells on neighbouring grid dots closer together than
 * a known spell is drawn (radius 12, 16 with its XP ring), short lines running
 * through spells they do not end at, and spells under the heart.
 *
 * applyAsync(output, onDone) runs just before the tree is saved (each mode's
 * applyTree saves in onDone), a piece at a time so the panel keeps drawing;
 * apply(output) does the same at once:
 *   1. the whole tree is spaced out by SPREAD, to make room between lines;
 *   2. spells are moved off the tree's lines - every parent-to-child line keeps
 *      LINE_CLEAR from the centre of every spell it does not end at, as far as
 *      a search round each spell can manage (LayoutLineClear). The lines stay
 *      straight: the spells move, not the lines;
 *   3. spells are pushed apart, a little at a time, until no two are closer
 *      than 2 x NODE_RADIUS + GAP and none sits on the heart (the globe,
 *      HEART_CLEARANCE round it), or ITERATIONS rounds have passed.
 * School roots do not move (the school's spoke is baked from them; SPREAD
 * keeps their direction), and a spell inside its school's sector is kept
 * inside it (not for flat or unturned layouts). Deterministic: the same tree
 * always comes out the same.
 *
 * What cannot be cleared - long lines across a dense area - is left touching.
 * No line shows through a spell anyway: the tree draws spells over lines,
 * underlaid with the background before their see-through fill
 * (CanvasRenderer._backdrop).
 *
 * Depends on: nothing (works on the saved tree format:
 *   output.schools[name] = { nodes: [{ formId, x, y, isRoot, children }],
 *   startAngle, endAngle }, output.globe, output.layoutMode, output.noRotate)
 */

var LayoutDeclutter = {

    NODE_RADIUS: 16,       // the biggest a spell is drawn: 12, plus the XP ring's 4
    GAP: 6,                // between two spells
    TOUCH_RADIUS: 12,      // a spell as drawn without its ring: two closer than twice this touch (log)
    SPREAD: 1.35,          // the whole tree is spaced out this much first, to make room between lines
    LINE_CLEAR: 20,        // a line keeps this far from other spells' centres (a locked spell's 7 + about
                           // 10 px at the default zoom); 0 = lines not looked at (LayoutLineClear)
    HEART_CLEARANCE: 50,   // beyond the globe's radius (runes, glow)
    ITERATIONS: 60,
    MAX_STEP: 10,          // world units a spell moves per round at most
    DONE_BELOW: 0.25,      // a round that moves nothing more than this ends it
    GOLDEN_ANGLE: 2.39996, // spreads spells that sit exactly on each other
    SLICE_MS: 60,          // applyAsync works this long, then lets the panel draw a frame

    /**
     * Move the spells of a tree about to be saved, all at once.
     * @param {Object} output - the tree (schools with baked x/y); changed in place
     * @returns {{moved: number, rounds: number, overlapsLeft: number, linesLeft: number}}
     */
    apply: function(output) {
        var job = this.begin(output);
        this.step(job, Infinity);
        return job.result;
    },

    /**
     * The same, a SLICE_MS piece at a time between frames, then onDone(result).
     * The game's browser has no JIT: a big tree takes it seconds, and all at
     * once the panel froze for them. Progress goes to the tree builder's status
     * line. A second call before the first is done drops the first (its onDone
     * is never called).
     */
    applyAsync: function(output, onDone) {
        var self = this, job = this.begin(output), shown = -1;
        this._asyncJob = job;
        function tick() {
            if (self._asyncJob !== job) return;
            var done = self.step(job, self.SLICE_MS);
            var pct = Math.floor(job.progress * 100);
            // Not while the panel is closed: each write repaints the whole view
            if (!done && pct !== shown && window._panelVisible !== false &&
                typeof TreeGrowth !== 'undefined' && TreeGrowth.setStatusText) {
                shown = pct;
                TreeGrowth.setStatusText('Arranging spells... ' + pct + '%', '#f59e0b');
            }
            if (!done) { setTimeout(tick, 0); return; }
            self._asyncJob = null;
            if (onDone) onDone(job.result);
        }
        setTimeout(tick, 0);
        return job;
    },

    /** Set up the work for step(): the tree spread out, the line search started. */
    begin: function(output) {
        var job = { progress: 0, result: null };
        if (!output || !output.schools) { job.result = { moved: 0, rounds: 0, overlapsLeft: 0 }; return job; }
        job.t0 = (typeof performance !== 'undefined') ? performance.now() : 0;
        // Sectors are wedges round the centre: a flat (side by side) or unturned
        // layout does not keep its schools in them
        var useSectors = !(output.layoutMode === 'flat' || output.noRotate === true);
        var items = this._collect(output, useSectors);
        if (items.list.length < 2) { job.result = { moved: 0, rounds: 0, overlapsLeft: 0 }; return job; }
        var globe = output.globe || {};
        job.items = items;
        job.heart = {
            x: globe.x || 0,
            y: globe.y || 0,
            r: (globe.radius || 45) + this.HEART_CLEARANCE
        };

        // Room first: every spell (roots too) out from the centre by SPREAD
        if (this.SPREAD !== 1) {
            for (var i = 0; i < items.list.length; i++) {
                items.list[i].x *= this.SPREAD;
                items.list[i].y *= this.SPREAD;
            }
        }
        // Then off the lines (a search, LayoutLineClear), then apart and off the heart
        if (this.LINE_CLEAR > 0 && typeof LayoutLineClear !== 'undefined') {
            var self = this;
            job.lines = LayoutLineClear.start(items.list, this._edges(items), job.heart, {
                clear: this.LINE_CLEAR,
                minDist: 2 * this.NODE_RADIUS + this.GAP,
                inSector: function(sector, x, y) { return self._inSector(sector, x, y); }
            });
        }
        return job;
    },

    /** Work on `job` for about budgetMs (Infinity: to the end); true once done (job.result). */
    step: function(job, budgetMs) {
        if (job.result) return true;
        var clock = (typeof performance !== 'undefined') ? performance : Date;
        var until = budgetMs === Infinity ? Infinity : clock.now() + budgetMs;
        if (job.lines && !job.lines.result) {
            var linesDone = LayoutLineClear.step(job.lines, budgetMs);
            job.progress = job.lines.progress * 0.97;
            if (!linesDone) return false;
        }
        // Apart and off the heart: each round is quick, but all of them in
        // one slice ran well past it without a JIT, so the slice may end
        // between rounds (job.rounds, job.apart carry on in the next)
        var list = job.items.list;
        job.rounds = job.rounds || 0;
        while (!job.apart && job.rounds < this.ITERATIONS) {
            if (this._round(list, job.heart) < this.DONE_BELOW) { job.apart = true; break; }
            job.rounds++;
            if (until !== Infinity && clock.now() >= until) {
                job.progress = 0.97 + 0.03 * job.rounds / this.ITERATIONS;
                return false;
            }
        }
        var rounds = job.rounds;

        var moved = 0;
        list.forEach(function(it) {
            var n = it.node;
            var nx = Math.round(it.x * 100) / 100, ny = Math.round(it.y * 100) / 100;
            if (Math.abs(nx - n.x) > 0.5 || Math.abs(ny - n.y) > 0.5) moved++;
            n.x = nx;
            n.y = ny;
        });
        var lines = job.lines ? job.lines.result : null;
        var left = this._countOverlaps(list);
        var linesLeft = lines ? LayoutLineClear.countLinesThrough(list, this._edges(job.items)) : -1;
        console.log('[LayoutDeclutter] spread x' + this.SPREAD + ', ' + moved + ' of ' + list.length +
            ' spells moved' + (lines ? ' (' + lines.moved + ' off lines in ' + lines.passes + ' passes)' : '') +
            ', ' + left + ' pairs still touching' + (lines ? ', ' + linesLeft + ' spells still on a line' : '') +
            (job.t0 ? ', ' + Math.round(performance.now() - job.t0) + ' ms' : ''));
        job.progress = 1;
        job.result = { moved: moved, rounds: rounds, overlapsLeft: left, linesLeft: linesLeft };
        return true;
    },

    // =========================================================================
    // SETUP
    // =========================================================================

    /** Every positioned spell, in a fixed order, with its school's sector. */
    _collect: function(output, useSectors) {
        var list = [], byId = {};
        var names = Object.keys(output.schools).sort();
        for (var s = 0; s < names.length; s++) {
            var school = output.schools[names[s]];
            var nodes = (school && school.nodes) || [];
            var sector = useSectors === false ? null : this._sector(school);
            for (var i = 0; i < nodes.length; i++) {
                var n = nodes[i];
                if (typeof n.x !== 'number' || typeof n.y !== 'number' || isNaN(n.x) || isNaN(n.y)) continue;
                var it = { node: n, x: n.x, y: n.y, fixed: !!n.isRoot, sector: null, index: list.length };
                // Kept in its sector only if it started there
                if (sector && this._inSector(sector, n.x, n.y)) it.sector = sector;
                list.push(it);
                if (n.formId) byId[n.formId] = it;
            }
        }
        return { list: list, byId: byId };
    },

    /** The school's sector as a centre angle and half width (radians), or null. */
    _sector: function(school) {
        if (!school || typeof school.startAngle !== 'number' || typeof school.endAngle !== 'number') return null;
        var a0 = school.startAngle * Math.PI / 180, a1 = school.endAngle * Math.PI / 180;
        var half = (a1 - a0) / 2;
        if (!(half > 0) || half >= Math.PI) return null;
        return { mid: a0 + half, half: half };
    },

    _inSector: function(sector, x, y) {
        return Math.abs(this._angleDiff(Math.atan2(y, x), sector.mid)) <= sector.half;
    },

    _angleDiff: function(a, b) {
        var d = a - b;
        while (d > Math.PI) d -= 2 * Math.PI;
        while (d < -Math.PI) d += 2 * Math.PI;
        return d;
    },


    // =========================================================================
    // ONE ROUND
    // =========================================================================

    /** Push everything that is too close apart once; returns the largest move. */
    _round: function(list, heart) {
        var i;
        for (i = 0; i < list.length; i++) { list[i].dx = 0; list[i].dy = 0; }
        var minDist = 2 * this.NODE_RADIUS + this.GAP;
        var grid = this._grid(list, minDist);

        // Spell against spell
        for (i = 0; i < list.length; i++) {
            var a = list[i];
            var near = this._near(grid, minDist, a.x - minDist, a.y - minDist, a.x + minDist, a.y + minDist);
            for (var k = 0; k < near.length; k++) {
                var b = near[k];
                if (b.index <= a.index) continue;
                var dx = b.x - a.x, dy = b.y - a.y;
                var d = Math.sqrt(dx * dx + dy * dy);
                if (d >= minDist) continue;
                if (d < 0.001) {
                    var ang = b.index * this.GOLDEN_ANGLE;
                    dx = Math.cos(ang); dy = Math.sin(ang); d = 0.001;
                }
                this._pushPair(a, b, dx / d, dy / d, minDist - d);
            }
        }


        // Off the heart, then move: each spell by its pushes, at most MAX_STEP
        var biggest = 0;
        for (i = 0; i < list.length; i++) {
            var it = list[i];
            if (it.fixed) continue;
            var hx = it.x - heart.x, hy = it.y - heart.y;
            var hd = Math.sqrt(hx * hx + hy * hy);
            if (hd < heart.r) {
                if (hd < 0.001) { hx = Math.cos(it.index * this.GOLDEN_ANGLE); hy = Math.sin(it.index * this.GOLDEN_ANGLE); hd = 1; }
                it.dx += hx / hd * (heart.r - hd);
                it.dy += hy / hd * (heart.r - hd);
            }
            var len = Math.sqrt(it.dx * it.dx + it.dy * it.dy);
            if (len < 0.001) continue;
            var step = Math.min(len, this.MAX_STEP);
            var nx = it.x + it.dx / len * step, ny = it.y + it.dy / len * step;
            if (it.sector) {
                this._clampToSector(it.sector, nx, ny);
                nx = this._clampX; ny = this._clampY;
            }
            var moved = Math.sqrt((nx - it.x) * (nx - it.x) + (ny - it.y) * (ny - it.y));
            if (moved > biggest) biggest = moved;
            it.x = nx;
            it.y = ny;
        }
        return biggest;
    },

    /**
     * The lines the tree always draws, parent to child. Lock lines and
     * cross-school bridges are only drawn for the selected or hovered spell,
     * so they are not kept clear of.
     */
    _edges: function(items) {
        var seen = {}, out = [], n = items.list.length;
        items.list.forEach(function(it) {
            var ids = it.node.children;
            if (!ids || !ids.length) return;
            for (var i = 0; i < ids.length; i++) {
                var other = items.byId[ids[i]];
                if (!other || other === it) continue;
                // The pair as one number (a string per line was slow without a JIT)
                var key = it.index < other.index ? it.index * n + other.index : other.index * n + it.index;
                if (seen[key]) continue;
                seen[key] = true;
                out.push([it, other]);
            }
        });
        return out;
    },


    /** Split a push between two spells; a fixed one does not move, the other takes all of it. */
    _pushPair: function(a, b, ux, uy, overlap) {
        var share = (a.fixed || b.fixed) ? overlap : overlap / 2;
        if (!a.fixed) { a.dx -= ux * share; a.dy -= uy * share; }
        if (!b.fixed) { b.dx += ux * share; b.dy += uy * share; }
    },


    /**
     * Keep a point within its sector's angles (radius kept); the result goes
     * to _clampX, _clampY (no array per spell per round).
     */
    _clampToSector: function(sector, x, y) {
        this._clampX = x;
        this._clampY = y;
        var r = Math.sqrt(x * x + y * y);
        if (r < 0.001) return;
        var diff = this._angleDiff(Math.atan2(y, x), sector.mid);
        // A spell's own width inside the edge, not just its centre
        var margin = Math.min(sector.half * 0.5, this.NODE_RADIUS / r);
        var limit = sector.half - margin;
        if (Math.abs(diff) <= limit) return;
        var a = sector.mid + (diff > 0 ? limit : -limit);
        this._clampX = Math.cos(a) * r;
        this._clampY = Math.sin(a) * r;
    },

    // =========================================================================
    // SPATIAL GRID
    // =========================================================================

    /**
     * A cell's key: a small whole number, not a string built for every spell
     * every round (as LayoutLineClear._cellKey). Cells past KEY_SPAN, never
     * in a real tree, get a string so no two share a key.
     */
    KEY_SPAN: 8192,
    _cellKey: function(cx, cy) {
        var span = this.KEY_SPAN;
        if (cx >= -span && cx < span && cy >= -span && cy < span) return (cx + span) * 2 * span + (cy + span);
        return cx + ',' + cy;
    },

    _grid: function(list, cell) {
        var g = {};
        for (var i = 0; i < list.length; i++) {
            var key = this._cellKey(Math.floor(list[i].x / cell), Math.floor(list[i].y / cell));
            (g[key] = g[key] || []).push(list[i]);
        }
        return g;
    },

    /** Spells whose cell touches the box (x0, y0)-(x1, y1). */
    _near: function(grid, cell, x0, y0, x1, y1) {
        var out = [];
        var cx0 = Math.floor(x0 / cell), cx1 = Math.floor(x1 / cell);
        var cy0 = Math.floor(y0 / cell), cy1 = Math.floor(y1 / cell);
        for (var cx = cx0; cx <= cx1; cx++) {
            for (var cy = cy0; cy <= cy1; cy++) {
                var bucket = grid[this._cellKey(cx, cy)];
                if (bucket) for (var i = 0; i < bucket.length; i++) out.push(bucket[i]);
            }
        }
        return out;
    },

    /** Pairs of spells whose drawn shapes still touch (2 x TOUCH_RADIUS apart or less; for the log). */
    _countOverlaps: function(list) {
        var minDist = 2 * this.TOUCH_RADIUS;
        var grid = this._grid(list, minDist), count = 0;
        for (var i = 0; i < list.length; i++) {
            var a = list[i];
            var near = this._near(grid, minDist, a.x - minDist, a.y - minDist, a.x + minDist, a.y + minDist);
            for (var k = 0; k < near.length; k++) {
                var b = near[k];
                if (b.index <= a.index) continue;
                var dx = b.x - a.x, dy = b.y - a.y;
                if (dx * dx + dy * dy < minDist * minDist) count++;
            }
        }
        return count;
    }
};

window.LayoutDeclutter = LayoutDeclutter;
