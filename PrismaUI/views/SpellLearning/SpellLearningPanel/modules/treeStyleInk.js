/**
 * TreeStyleInk - how a design draws the spells' shapes and the lines between
 * them: a hand-drawn wobble, dotted or broken lines, an engraved double line and
 * an inset line on known spells. Added to TreeStyle like treeStyleBook.js, and
 * every effect here is off unless a design preset turns it on (the Ink tokens in
 * TreeStyle.DEFAULTS).
 *
 * The tree layer is painted on the CPU in game and repainted on every pan step,
 * so none of this adds paint calls per spell:
 * - the wobble is geometry only: each school's shape is jittered into
 *   HAND_VARIANTS shapes once per design, and a spell picks one by its position,
 *   so the batch (NodeBatch) and the spell-by-spell drawing (renderNode) agree;
 *   straight lines get a slight bow in the same way;
 * - a line style is a dash or a dot pattern on strokes that are made anyway, or
 *   one more stroke per edge colour (the engraved cut, one per school). Stippled
 *   lines are a pattern, not a dash: dashing the ~1,400 locked lines into dots
 *   cost 25-40 ms a repaint on a CPU canvas, the pattern nothing measurable;
 * - all of it only at the full level of detail, and the cut and the inset line
 *   only where they are wide enough on screen to be seen.
 *
 * Depends on: TreeStyle, NodeBatch (SHAPES)
 */

(function() {
    var HAND_VARIANTS = 4;
    var ROUND_POINTS = 9;          // a hand-drawn circle: a smooth loop through this many points
    var BOW_SHARE = 0.5;           // a line's bow, as a share of handDrawn times its length
    var CUT_MIN_SCREEN_PX = 2.2;   // the engraved cut: the line must be this wide on screen
    var STIPPLE_MIN_ZOOM = 1;      // below this the dots are ~2 px apart and read as a grey line anyway;
                                   // the pattern costs ~6 ms a repaint on a CPU canvas, so it waits
    var INNER_MIN_SCREEN_PX = 5;   // the inset line: its shape must be this big on screen
    var INNER_WIDTH_PX = 0.8;      // the inset line's width on screen

    /** A small repeatable random series (the same shapes on every machine). */
    function rng(seed) {
        return function() {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            return seed / 0x7fffffff;
        };
    }

    /** A number in [0, 1) that stays the same for the same point. */
    function hash01(x, y) {
        var h = Math.sin(x * 12.9898 + y * 78.233) * 43758.5453;
        return h - Math.floor(h);
    }

    var Ink = {
        _inkFull: true,            // this repaint is at the full level of detail
        _inkZoom: 1,
        _hand: null,               // school -> [shape x HAND_VARIANTS], for _handAmount
        _handAmount: 0,
        _dashCache: {},
        _stipple: null,            // { key, pattern } for lockedEdgeStipple

        /** Before each repaint of the tree: the level of detail and zoom it is drawn at. */
        beginInk: function(full, zoom) {
            this._inkFull = full !== false;
            this._inkZoom = zoom > 0 ? zoom : 1;
        },

        /** The wobble for this repaint, as a share of a shape's size (0 = off). */
        handAmount: function() {
            var a = this.tokens && this.tokens.handDrawn;
            return (a > 0 && this._inkFull) ? a : 0;
        },

        /**
         * The hand-drawn shapes for this repaint, or null when the design has none:
         * school -> HAND_VARIANTS shapes, each { pts: [[x, y]...], round }. Key ''
         * is the circle (Restoration and schools without a shape of their own).
         */
        handShapes: function() {
            var a = this.handAmount();
            if (!a) return null;
            if (this._hand && this._handAmount === a) return this._hand;
            var r = rng(7331);
            var jitter = function() { return (r() * 2 - 1) * a; };
            var out = {};
            var schools = [''];
            for (var s in NodeBatch.SHAPES) if (NodeBatch.SHAPES.hasOwnProperty(s)) schools.push(s);
            for (var i = 0; i < schools.length; i++) {
                var name = schools[i], list = [];
                for (var v = 0; v < HAND_VARIANTS; v++) {
                    var pts = [], k;
                    if (!name) {
                        var turn = r() * Math.PI * 2;
                        for (k = 0; k < ROUND_POINTS; k++) {
                            var ang = turn + k * Math.PI * 2 / ROUND_POINTS, rad = 1 + jitter();
                            pts.push([Math.cos(ang) * rad, Math.sin(ang) * rad]);
                        }
                    } else {
                        var src = NodeBatch.SHAPES[name];
                        for (k = 0; k < src.length; k++) pts.push([src[k][0] + jitter(), src[k][1] + jitter()]);
                    }
                    list.push({ pts: pts, round: !name });
                }
                out[name] = list;
            }
            this._hand = out;
            this._handAmount = a;
            return out;
        },

        /** Which of the variants a spell at (x, y) draws. */
        handVariant: function(x, y) {
            return Math.floor(hash01(x, y) * HAND_VARIANTS) % HAND_VARIANTS;
        },

        /**
         * Add a hand-drawn shape to path, centred on (x, y): its points turned and
         * scaled by (c, s) = (cos, sin) of the turn times the size.
         */
        traceShape: function(path, shape, x, y, c, s) {
            // Plain arithmetic, no helper closures: this runs for every spell of a
            // repaint, and the game's engine has no JIT
            var p = shape.pts, n = p.length, i, q, qx, qy;
            if (!shape.round) {
                q = p[0];
                path.moveTo(x + q[0] * c - q[1] * s, y + q[0] * s + q[1] * c);
                for (i = 1; i < n; i++) {
                    q = p[i];
                    path.lineTo(x + q[0] * c - q[1] * s, y + q[0] * s + q[1] * c);
                }
                path.closePath();
                return;
            }
            // A smooth loop: from the middle of each side to the next, bent through the corner
            q = p[n - 1];
            var prevX = x + q[0] * c - q[1] * s, prevY = y + q[0] * s + q[1] * c;
            q = p[0];
            var firstX = x + q[0] * c - q[1] * s, firstY = y + q[0] * s + q[1] * c;
            path.moveTo((prevX + firstX) / 2, (prevY + firstY) / 2);
            qx = firstX; qy = firstY;
            for (i = 0; i < n; i++) {
                var nq = p[(i + 1) % n];
                var nx = x + nq[0] * c - nq[1] * s, ny = y + nq[0] * s + nq[1] * c;
                path.quadraticCurveTo(qx, qy, (qx + nx) / 2, (qy + ny) / 2);
                qx = nx; qy = ny;
            }
            path.closePath();
        },

        /** How far a straight line from (x1, y1) to (x2, y2) bows, as a share of its length (0 = straight). */
        edgeBow: function(x1, y1, x2, y2) {
            var a = this.handAmount();
            if (!a) return 0;
            return (hash01(x1 + y2, y1 - x2) * 2 - 1) * a * BOW_SHARE;
        },

        /** A dash token ("on off on off...", screen px) for this repaint, in world units; null = solid. */
        inkDash: function(spec) {
            if (!spec || !this._inkFull) return null;
            var base = this._dashCache[spec];
            if (base === undefined) {
                base = String(spec).split(/[\s,]+/).map(parseFloat).filter(function(v) { return v >= 0; });
                if (!base.length) base = null;
                this._dashCache[spec] = base;
            }
            if (!base) return null;
            var z = this._inkZoom;
            return base.map(function(v) { return v / z; });
        },

        /**
         * The stroke style for stippled locked lines: a dot pattern in `color`,
         * or null when off, not at full detail or when the engine makes no
         * patterns (then solid). The dots belong to the tree, so they stay put
         * while it is panned. lockedEdgeStipple picks the tile: up to 5 a 5 x 5
         * tile, above that 7 x 7. A line is one tile pixel wide, so the dots sit
         * like queens on a board - one in every row, column and diagonal
         * (column k, row 2k mod n) - and a line in any direction meets one every
         * n pixels; a tile with an empty row had lines lying in it vanish.
         */
        inkStipple: function(ctx, color) {
            var want = Math.round(this.tokens.lockedEdgeStipple);
            if (!(want > 0) || !this._inkFull || this._inkZoom < STIPPLE_MIN_ZOOM) return null;
            var n = want <= 5 ? 5 : 7;
            var key = color + '|' + n;
            // The layer and LayerScroll's spare take turns: a pattern kept for each
            var cache = this._stipple && this._stipple.key === key ? this._stipple : (this._stipple = { key: key, ctxs: [], patterns: [] });
            for (var i = 0; i < cache.ctxs.length; i++) if (cache.ctxs[i] === ctx) return cache.patterns[i];
            var pattern = null;
            try {
                var tile = document.createElement('canvas');
                tile.width = n;
                tile.height = n;
                var g = tile.getContext('2d');
                g.fillStyle = color;
                for (var k = 0; k < n; k++) {
                    var row = (2 * k) % n;
                    g.globalAlpha = 1;
                    g.fillRect(k, row, 1, 1);              // the dot, softened to the right and below
                    g.globalAlpha = 0.45;
                    g.fillRect((k + 1) % n, row, 1, 1);
                    g.fillRect(k, (row + 1) % n, 1, 1);
                }
                pattern = ctx.createPattern(tile, 'repeat');
            } catch (e) {
                pattern = null;
            }
            if (cache.ctxs.length > 2) { cache.ctxs.shift(); cache.patterns.shift(); }
            cache.ctxs.push(ctx);
            cache.patterns.push(pattern);
            return pattern;
        },

        /** The engraved cut down unlocked lines of `width` (world units): its width, or 0 when off or too thin. */
        edgeCutWidth: function(width) {
            var cut = this.tokens.edgeCut;
            if (!(cut > 0) || !this._inkFull || width * this._inkZoom < CUT_MIN_SCREEN_PX) return 0;
            return width * cut;
        },

        /**
         * The inset line inside a known spell of `size`: { size, color, width } in
         * world units, or null when the design has none or it is too small to see.
         * color '' = the page behind the tree (the caller's backdrop).
         */
        innerLineAt: function(size) {
            var k = this.tokens.innerLine;
            if (!(k > 0) || !this._inkFull || size * k * this._inkZoom < INNER_MIN_SCREEN_PX) return null;
            return { size: size * k, color: this.tokens.innerLineColor, width: INNER_WIDTH_PX / this._inkZoom };
        },

        /** The inset line for a spell drawn on its own (ctx scaled to `size`, path a unit shape). */
        strokeInnerLine: function(ctx, path, size, backdrop) {
            var inner = this.innerLineAt(size);
            if (!inner) return;
            var k = inner.size / size;
            ctx.save();
            ctx.scale(k, k);
            ctx.strokeStyle = inner.color || backdrop;
            ctx.lineWidth = inner.width / inner.size;
            ctx.stroke(path);
            ctx.restore();
        }
    };

    for (var k in Ink) {
        if (Ink.hasOwnProperty(k)) TreeStyle[k] = Ink[k];
    }
})();
