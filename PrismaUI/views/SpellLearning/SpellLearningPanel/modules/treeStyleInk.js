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
            var p = shape.pts, n = p.length, i;
            var px = function(q) { return x + q[0] * c - q[1] * s; };
            var py = function(q) { return y + q[0] * s + q[1] * c; };
            if (!shape.round) {
                path.moveTo(px(p[0]), py(p[0]));
                for (i = 1; i < n; i++) path.lineTo(px(p[i]), py(p[i]));
                path.closePath();
                return;
            }
            // A smooth loop: from the middle of each side to the next, bent through the corner
            var last = p[n - 1];
            path.moveTo((px(last) + px(p[0])) / 2, (py(last) + py(p[0])) / 2);
            for (i = 0; i < n; i++) {
                var q = p[i], nx = p[(i + 1) % n];
                path.quadraticCurveTo(px(q), py(q), (px(q) + px(nx)) / 2, (py(q) + py(nx)) / 2);
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
         * The stroke style for stippled locked lines: a dot pattern in `color`
         * (lockedEdgeStipple = dots this many world units apart), or null when off,
         * not at full detail or when the engine makes no patterns (then solid).
         * The dots belong to the tree, so they stay put while it is panned.
         */
        inkStipple: function(ctx, color) {
            var n = Math.round(this.tokens.lockedEdgeStipple);
            if (!(n > 0) || !this._inkFull) return null;
            n = Math.max(2, Math.min(6, n));
            var key = color + '|' + n;
            if (this._stipple && this._stipple.key === key && this._stipple.ctx === ctx) return this._stipple.pattern;
            var pattern = null;
            try {
                var tile = document.createElement('canvas');
                tile.width = n;
                tile.height = n;
                var g = tile.getContext('2d');
                g.fillStyle = color;
                g.fillRect(0, 0, 1, 1);                 // a dot, softened on two sides
                g.globalAlpha = 0.5;
                g.fillRect(1, 0, 1, 1);
                g.fillRect(0, 1, 1, 1);
                pattern = ctx.createPattern(tile, 'repeat');
            } catch (e) {
                pattern = null;
            }
            this._stipple = { key: key, ctx: ctx, pattern: pattern };
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
