/**
 * LayoutLineGrid - the spatial grids LayoutLineClear searches with: spells by
 * cell, and lines by every cell they pass near; the fans, what one spell's
 * search looks at, gathered from them once per spell, and of those the lines
 * that can add to its line gap cost (_gapFan); and the lines the search keeps
 * (_shortLines) with its geometry helpers. Split off LayoutLineClear (file
 * size); its methods are added to LayoutLineClear and read its constants,
 * _clear2 and _work. Load right after layoutLineClear.js.
 *
 * Depends on: LayoutLineClear
 */

(function() {
    var grids = {

        // =========================================================================
        // LINES AND GEOMETRY
        // =========================================================================

        /**
         * The lines no longer than MAX_LINE, then of those the ones at no spell
         * with more than MAX_SPELL_LINES of them, in order (see both; the C++
         * LineClear constructor). Where the spells are when the search starts.
         */
        _shortLines: function(edges) {
            var out = [], kept = [], lines = [], max2 = this.MAX_LINE * this.MAX_LINE, i;
            for (i = 0; i < edges.length; i++) {
                var dx = edges[i][1].x - edges[i][0].x, dy = edges[i][1].y - edges[i][0].y;
                if (!(dx * dx + dy * dy <= max2)) continue;
                kept.push(edges[i]);
                lines[edges[i][0].index] = (lines[edges[i][0].index] || 0) + 1;
                lines[edges[i][1].index] = (lines[edges[i][1].index] || 0) + 1;
            }
            for (i = 0; i < kept.length; i++) {
                if (lines[kept[i][0].index] <= this.MAX_SPELL_LINES && lines[kept[i][1].index] <= this.MAX_SPELL_LINES) out.push(kept[i]);
            }
            return out;
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

        /** Squared distance from (px, py) to the line a-b, away from its ends; Infinity near them. */
        _segDist2: function(px, py, ax, ay, bx, by) {
            var vx = bx - ax, vy = by - ay;
            var l2 = vx * vx + vy * vy;
            if (l2 < 0.0001) return Infinity;
            var t = ((px - ax) * vx + (py - ay) * vy) / l2;
            if (t < this.END_MARGIN || t > 1 - this.END_MARGIN) return Infinity;
            var cx = ax + vx * t - px, cy = ay + vy * t - py;
            return cx * cx + cy * cy;
        },

        // =========================================================================
        // GRIDS
        // =========================================================================

        /**
         * A cell's key: a number, not a string (this runs a few million times
         * per tree), and a small whole one, which every engine keeps as an
         * integer (the old (cx + 32768) * 65536 went past 2^31). Cells past
         * KEY_SPAN (hundreds of thousands of tree units out, never in a real
         * tree) get a string instead, so no two cells ever share a key.
         */
        KEY_SPAN: 8192,
        MAX_CELL: 1073741824,  // cells clamped to +-2^30, as LayoutDeclutter.MAX_CELL and the C++ kMaxCell

        /** Math.floor(v / cell) within +-MAX_CELL (NaN: -MAX_CELL), as the C++ FloorCell. */
        _floorCell: function(v, cell) {
            var c = Math.floor(v / cell), m = this.MAX_CELL;
            return c > m ? m : (c >= -m ? c : -m);
        },

        _cellKey: function(cx, cy) {
            var span = this.KEY_SPAN;
            if (cx >= -span && cx < span && cy >= -span && cy < span) return (cx + span) * 2 * span + (cy + span);
            return cx + ',' + cy;
        },

        _key: function(x, y) {
            return this._cellKey(this._floorCell(x, this.CELL), this._floorCell(y, this.CELL));
        },

        _gridAdd: function(it) {
            var key = this._key(it.x, it.y);
            (this._nodeGrid[key] = this._nodeGrid[key] || []).push(it);
            it._cell = key;
        },

        _gridMove: function(it, x, y) {
            var bucket = this._nodeGrid[it._cell];
            if (bucket) {
                var at = bucket.indexOf(it);
                if (at >= 0) bucket.splice(at, 1);
            }
            it.x = x;
            it.y = y;
            it._v = (it._v || 0) + 1;
            this._gridAdd(it);
        },

        /** Spells in the cells touching the box (x0, y0)-(x1, y1) grown by pad. */
        _near: function(x0, y0, x1, y1, pad) {
            var out = [];
            var cx0 = this._floorCell(x0 - pad, this.CELL), cx1 = this._floorCell(x1 + pad, this.CELL);
            var cy0 = this._floorCell(y0 - pad, this.CELL), cy1 = this._floorCell(y1 + pad, this.CELL);
            for (var cx = cx0; cx <= cx1; cx++) {
                for (var cy = cy0; cy <= cy1; cy++) {
                    var bucket = this._nodeGrid[this._cellKey(cx, cy)];
                    if (bucket) for (var i = 0; i < bucket.length; i++) out.push(bucket[i]);
                }
            }
            return out;
        },

        /**
         * Every cell within `clear` of the line a-b, once each: the cells of its
         * box whose centre is near enough to the line, so a long diagonal line
         * does not take in a whole square of the tree. Returns the keys in a
         * shared array, the first _keyCount of it (valid until the next call):
         * no callback per cell, which the game's browser (no JIT) pays dearly
         * for.
         */
        _cellsAlong: function(ax, ay, bx, by) {
            var keys = this._keyBuf || (this._keyBuf = []), n = 0;
            var cell = this.CELL, pad = Math.sqrt(this._clear2);
            var reach = pad + cell * 0.7072;          // clear plus half a cell's diagonal
            var reach2 = reach * reach;
            var cx0 = this._floorCell(Math.min(ax, bx) - pad, cell), cx1 = this._floorCell(Math.max(ax, bx) + pad, cell);
            var cy0 = this._floorCell(Math.min(ay, by) - pad, cell), cy1 = this._floorCell(Math.max(ay, by) + pad, cell);
            var vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
            // _cellKey written out, with its range checked once for the whole box
            var span = this.KEY_SPAN, wide = 2 * span;
            var inRange = cx0 >= -span && cx1 < span && cy0 >= -span && cy1 < span;
            // Each column is tested only over the rows the line can reach there:
            // the part of the line within `reach` of the column across, as high
            // and as low as that part goes, plus `reach` (and a unit to spare
            // for rounding). The same cells as testing the whole box, in the
            // same order, for a fraction of the tests on a long slanted line.
            var band = reach + 1;
            for (var cx = cx0; cx <= cx1; cx++) {
                var mx = (cx + 0.5) * cell;
                var t0 = 0, t1 = 1;
                if (vx !== 0) {
                    t0 = (mx - band - ax) / vx; t1 = (mx + band - ax) / vx;
                    if (t0 > t1) { var sw = t0; t0 = t1; t1 = sw; }
                    if (t0 < 0) t0 = 0;
                    if (t1 > 1) t1 = 1;
                    if (t0 > t1) continue;
                } else if (Math.abs(mx - ax) > band) continue;
                var y0 = ay + vy * t0, y1 = ay + vy * t1;
                // In KEY_SPAN these stay in it, where _floorCell changes nothing
                var from = inRange ? Math.floor((Math.min(y0, y1) - band) / cell) : this._floorCell(Math.min(y0, y1) - band, cell);
                var to = inRange ? Math.floor((Math.max(y0, y1) + band) / cell) : this._floorCell(Math.max(y0, y1) + band, cell);
                if (from < cy0) from = cy0;
                if (to > cy1) to = cy1;
                for (var cy = from; cy <= to; cy++) {
                    var my = (cy + 0.5) * cell;
                    var t = l2 > 0 ? ((mx - ax) * vx + (my - ay) * vy) / l2 : 0;
                    if (t < 0) t = 0; else if (t > 1) t = 1;
                    var dx = ax + vx * t - mx, dy = ay + vy * t - my;
                    if (dx * dx + dy * dy <= reach2) keys[n++] = inRange ? (cx + span) * wide + (cy + span) : this._cellKey(cx, cy);
                }
            }
            this._keyCount = n;
            this._work += this.CELL_WORK * n;
            return keys;
        },

        /** Spells in the cells along the line a-b (see _cellsAlong). */
        _nearLine: function(ax, ay, bx, by) {
            var out = [], grid = this._nodeGrid;
            var keys = this._cellsAlong(ax, ay, bx, by), count = this._keyCount;
            for (var k = 0; k < count; k++) {
                var bucket = grid[keys[k]];
                if (bucket) for (var i = 0; i < bucket.length; i++) out.push(bucket[i]);
            }
            return out;
        },

        /** Each line in every cell along it (see _cellsAlong); rebuilt every pass. */
        _buildEdgeGrid: function(edges) {
            var grid = this._edgeGrid = {};
            for (var e = 0; e < edges.length; e++) {
                var edge = edges[e];
                var keys = this._cellsAlong(edge[0].x, edge[0].y, edge[1].x, edge[1].y), count = this._keyCount;
                for (var k = 0; k < count; k++) (grid[keys[k]] = grid[keys[k]] || []).push(edge);
            }
        },

        // =========================================================================
        // FANS: what one spell's search looks at, gathered once per spell
        // =========================================================================

        /**
         * For each of `it`'s lines, the spells near any line it could have from
         * a spot it will try (returned), and the other lines near it
         * (this._edgeFan): gathered once per spell instead of once per spot.
         * One walk over the cells serves both when the lines' walk is the
         * wider (the usual case): it finds every spell the narrower walk would,
         * and a few more, which _ringSpells drops.
         */
        _gatherFan: function(it) {
            var own = this._incident[it.index];
            var dx = it.x - it.ox, dy = it.y - it.oy;
            var reach = this.RADII[this.RADII.length - 1] * (it._reach || 1) + Math.sqrt(dx * dx + dy * dy);
            var clear2 = this._clear2, clear = Math.sqrt(clear2), fan = [], lines = [];
            var pad = clear + reach;
            // Lines within LINE_GAP of any line it could have (_edgesNear pads by clear, more than enough)
            var extra = reach + this.LINE_GAP * this.BUNDLE_MAX - clear;
            var shared = clear + extra >= pad;
            var grid = this._nodeGrid;
            for (var i = 0; i < own.length; i++) {
                var o = own[i][0] === it ? own[i][1] : own[i][0];
                lines.push(this._boxed(this._edgesNear(own[i], it.x, it.y, o.x, o.y, extra)));
                if (!shared) {
                    this._clear2 = pad * pad;          // _cellsAlong reads its pad from here
                    fan.push(this._nearLine(it.x, it.y, o.x, o.y));
                    this._clear2 = clear2;
                    continue;
                }
                // The spells in the cells _edgesNear just walked (still in _keyBuf)
                var keys = this._keyBuf, count = this._keyCount, near = [];
                for (var k = 0; k < count; k++) {
                    var bucket = grid[keys[k]];
                    if (bucket) for (var b = 0; b < bucket.length; b++) near.push(bucket[b]);
                }
                fan.push(near);
            }
            this._edgeFan = lines;
            return fan;
        },

        /**
         * Store each line's box, direction and vector on it (read by
         * _lineGapCost); kept until one of its ends moves (_v, counted up by
         * _gridMove).
         */
        _boxed: function(lines) {
            for (var i = 0; i < lines.length; i++) {
                var e = lines[i], p = e[0], q = e[1];
                if (e._v0 === p._v && e._v1 === q._v) continue;
                e._v0 = p._v; e._v1 = q._v;
                e._x0 = Math.min(p.x, q.x); e._x1 = Math.max(p.x, q.x);
                e._y0 = Math.min(p.y, q.y); e._y1 = Math.max(p.y, q.y);
                e._ex = q.x - p.x; e._ey = q.y - p.y;
                e._l2 = e._ex * e._ex + e._ey * e._ey;
                e._ang = Math.atan2(q.y - p.y, q.x - p.x);
            }
            return lines;
        },

        /** Lines in the cells along a-b, grown by `extra`, once each (not `self`). */
        _edgesNear: function(self, ax, ay, bx, by, extra) {
            var out = [], grid = this._edgeGrid, stamp = ++this._stamp;
            var clear2 = this._clear2;
            var pad = Math.sqrt(clear2) + extra;
            this._clear2 = pad * pad;          // _cellsAlong reads its pad from here
            var keys = this._cellsAlong(ax, ay, bx, by), count = this._keyCount;
            this._clear2 = clear2;
            for (var k = 0; k < count; k++) {
                var bucket = grid[keys[k]];
                if (!bucket) continue;
                this._work += bucket.length;
                for (var i = 0; i < bucket.length; i++) {
                    var e = bucket[i];
                    if (e === self || e._stamp === stamp) continue;
                    e._stamp = stamp;
                    out.push(e);
                }
            }
            return out;
        },
        /**
         * How far from where `it` is now a spot of ring r (RADII[r]) can be,
         * in either round of _searchOne (round where it is now, round where
         * the layout put it), with a hair to spare for rounding.
         */
        _ringReach: function(it, r) {
            var dx = it.x - it.ox, dy = it.y - it.oy;
            return this.RADII[r] * (it._reach || 1) + Math.sqrt(dx * dx + dy * dy) + 0.001;
        },

        /**
         * Sort each line's spell fan by the first ring whose spots can bring
         * the line within `clear` of the spell, dropping `it`, the line's far
         * end and spells no spot can reach. _cost then looks at the first
         * _fanEnd[i][ring] of fan[i] only: the inner rings see a small part of
         * the fan. Order within the fan does not matter there (each spell adds
         * 1, the limit is checked after the whole line).
         *
         * Moving `it` by ρ moves the point a share t along its line by only
         * (1 - t)ρ: the lines from a ring's spots stay within clear + ρ of the
         * line from `it`, and nearer the far end within less. A spell counts
         * only at t between END_MARGIN and 1 - END_MARGIN, at least as far
         * along as its own place along the line less clear + ρ. Both tests
         * only rule out what cannot count, with a hair to spare for rounding.
         */
        _ringSpells: function(it, fan) {
            var own = this._incident[it.index], rings = this.RADII.length, clear = Math.sqrt(this._clear2);
            var lo = this.END_MARGIN, hi = 1 - this.END_MARGIN;
            var reach = [], lim2 = [], r, k;
            for (r = 0; r < rings; r++) {
                reach.push(this._ringReach(it, r));
                lim2.push((clear + reach[r]) * (clear + reach[r]));
            }
            var ends = [], ringOf = [];
            for (var i = 0; i < own.length; i++) {
                var o = own[i][0] === it ? own[i][1] : own[i][0];
                var list = fan[i], counts = [];
                for (r = 0; r <= rings; r++) counts.push(0);
                var vx = o.x - it.x, vy = o.y - it.y, l2 = vx * vx + vy * vy, len = Math.sqrt(l2);
                for (k = 0; k < list.length; k++) {
                    var m = list[k];
                    r = rings;                                  // no ring: dropped
                    if (m !== it && m !== o) {
                        var wx = m.x - it.x, wy = m.y - it.y;
                        var along = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
                        var t = along < 0 ? 0 : (along > 1 ? 1 : along);
                        var dx = vx * t - wx, dy = vy * t - wy, d2 = dx * dx + dy * dy;
                        var side = len > 1 ? Math.abs(wx * vy - wy * vx) / len : -1;
                        for (r = 0; r < rings; r++) {
                            if (d2 > lim2[r]) continue;
                            if (side < 0) break;
                            var from = Math.max(lo, along - (clear + reach[r]) / len);
                            if (from <= hi && side <= clear + (1 - from) * reach[r] + 0.001) break;
                        }
                    }
                    ringOf[k] = r;
                    counts[r]++;
                }
                // Where each ring's spells start, then place them (list order within a ring)
                var at = [], end = [], sum = 0;
                for (r = 0; r < rings; r++) { at.push(sum); sum += counts[r]; end.push(sum); }
                var sorted = [];
                for (k = 0; k < list.length; k++) if (ringOf[k] < rings) sorted[at[ringOf[k]]++] = list[k];
                fan[i] = sorted;
                ends.push(end);
            }
            this._fanEnd = ends;
            return fan;
        },

        /**
         * Of each of `it`'s line fans (_edgeFan), the lines that can add to
         * _lineGapCost from some spot of this search, in the same order
         * (_gapCut), and a frame for each line of `it` (_gapFrame: along it,
         * across it, its length; framed from MIN_FRAME) with each kept line's
         * box in it and 1 if it is steep, else 0 (_gapBox, five numbers a
         * line), for the tests per spot there. Set
         * up here, worked out per line by _gapFanLine once GAP_FAN_AFTER spots
         * got as far as that line's gap part (the cut costs a walk over the
         * whole fan, and most lines of most searches see a few spots at most;
         * before it, the whole fan, which gives the same sums).
         *
         * Every spot is within R of where `it` is (_ringReach), so every line
         * `it` can have lies within R of the line it has, the base, and turns
         * from it by asin(R / length) at most. A line adds only if it crosses a
         * spot's line (narrower than MIN_CROSS) or comes within LINE_GAP x
         * BUNDLE_MAX of it. Dropped: lines sharing an end (never counted), lines
         * further than R + LINE_GAP x BUNDLE_MAX from the base, and lines every
         * spot's line crosses at MIN_CROSS or more (`it` further than R from
         * the line, both its ends clear of every line from the far end o
         * through a spot, the angle to the base wider than MIN_CROSS plus the
         * turn). Marked steep: lines at BUNDLE_ANGLE plus the turn or more from
         * the base - one adds only by coming within LINE_GAP without crossing.
         * Each test has FAN_MARGIN (FAN_ANGLE_MARGIN) to spare, far above
         * rounding in a tree within MAX_COORD, and a NaN keeps the line. A line
         * dropped or passed over adds nothing, so no sum changes (see
         * _lineGapCost): the costs, and the spells' moves, stay the same.
         */
        _gapFan: function(it) {
            var n = this._incident[it.index].length, cut = [], boxes = [], frames = [], uses = [];
            for (var i = 0; i < n; i++) { cut.push(null); boxes.push(null); frames.push(0, 0, 0); uses.push(0); }
            this._gapCut = cut;
            this._gapBox = boxes;
            this._gapFrame = frames;
            this._gapUses = uses;
        },

        /** _gapFan's cut of `it`'s line i (returned; see there). */
        _gapFanLine: function(it, i) {
            var own = this._incident[it.index], o = own[i][0] === it ? own[i][1] : own[i][0];
            var m = this.FAN_MARGIN, R = this._ringReach(it, this.RADII.length - 1) + m;
            var wide = R + this.LINE_GAP * this.BUNDLE_MAX + m, wide2 = wide * wide;
            var steep = this.MIN_CROSS + this.FAN_ANGLE_MARGIN, bundle = this.BUNDLE_ANGLE + this.FAN_ANGLE_MARGIN;
            var ix = it.x, iy = it.y;
            var vx = o.x - ix, vy = o.y - iy, len = Math.sqrt(vx * vx + vy * vy);
            var framed = len >= this.MIN_FRAME, ux = framed ? vx / len : 0, uy = framed ? vy / len : 0;
            var turn = R < len ? Math.asin(R / len) : Infinity, base = Math.atan2(vy, vx);
            // A line whose box is further than `wide` from the base's box is further from the base
            var bx0 = Math.min(ix, o.x) - wide, bx1 = Math.max(ix, o.x) + wide;
            var by0 = Math.min(iy, o.y) - wide, by1 = Math.max(iy, o.y) + wide;
            var list = this._edgeFan[i], out = [], box = [];
            for (var k = 0; k < list.length; k++) {
                var e = list[k], p = e[0], q = e[1];
                if (p === it || q === it || p === o || q === o) continue;
                if (e._x1 < bx0 || e._x0 > bx1 || e._y1 < by0 || e._y0 > by1) continue;
                // Its ends in the base's frame; its box there is no nearer the base than it is
                var pu = (p.x - ix) * ux + (p.y - iy) * uy, pw = (p.y - iy) * ux - (p.x - ix) * uy;
                var qu = (q.x - ix) * ux + (q.y - iy) * uy, qw = (q.y - iy) * ux - (q.x - ix) * uy;
                var u0 = Math.min(pu, qu), u1 = Math.max(pu, qu), w0 = Math.min(pw, qw), w1 = Math.max(pw, qw);
                if (framed) {
                    var gu = u0 > len ? u0 - len : (u1 < 0 ? -u1 : 0), gw = w0 > 0 ? w0 : (w1 < 0 ? -w1 : 0);
                    if (gu * gu + gw * gw >= wide2) continue;
                }
                // p and q against the base, `it` and o against e (as _lineGapCost's _cross)
                var sp = vx * (p.y - iy) - vy * (p.x - ix), sq = vx * (q.y - iy) - vy * (q.x - ix);
                var si = e._ex * (iy - p.y) - e._ey * (ix - p.x), so = e._ex * (o.y - p.y) - e._ey * (o.x - p.x);
                var crossing = sp * sq < 0 && si * so < 0;
                var an = Math.abs(base - e._ang) % Math.PI;
                if (an > Math.PI / 2) an = Math.PI - an;
                if (crossing && turn < Infinity) {
                    var el = Math.sqrt(e._l2);
                    var lp = Math.sqrt((p.x - o.x) * (p.x - o.x) + (p.y - o.y) * (p.y - o.y));
                    var lq = Math.sqrt((q.x - o.x) * (q.x - o.x) + (q.y - o.y) * (q.y - o.y));
                    if (Math.abs(si) > (R + m) * el && Math.abs(so) > m * el &&
                        Math.abs(sp) > (R + m) * lp + m * len && Math.abs(sq) > (R + m) * lq + m * len &&
                        an >= steep + turn) continue;
                }
                out.push(e);
                box.push(u0, u1, w0, w1, an >= bundle + turn ? 1 : 0);
            }
            this._gapCut[i] = out;
            this._gapBox[i] = box;
            this._gapFrame[3 * i] = ux; this._gapFrame[3 * i + 1] = uy; this._gapFrame[3 * i + 2] = len;
            return out;
        }
    };

    var target = (typeof LayoutLineClear !== 'undefined') ? LayoutLineClear : window.LayoutLineClear;
    for (var name in grids) {
        if (grids.hasOwnProperty(name)) target[name] = grids[name];
    }
})();
