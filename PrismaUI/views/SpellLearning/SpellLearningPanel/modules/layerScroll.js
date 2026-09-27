/**
 * LayerScroll - a drag past the tree layer's margin shifts the layer instead of
 * repainting it.
 *
 * The tree layer (CanvasRenderer._drawTree) is drawn TREE_LAYER_MARGIN px wider
 * than the view all round, so a drag slides it. Past the margin it used to be
 * repainted whole: every spell, line and name, 25-60 ms on a CPU canvas at
 * 1600x1000 - a hitch every ~150 px of a drag. Now the picture is copied onto a
 * spare canvas moved by the drag (a whole number of device pixels, so nothing
 * blurs), and only the strips the move uncovered are drawn: the spells and lines
 * inside them (culled to the strip), clipped to it. 3-6 ms a strip.
 *
 * It starts when half the margin is used up (EARLY_SHARE), so the uncovered
 * strips are still off screen, in the margin. They are drawn nearest the view
 * first, as many a frame as fit in FRAME_BUDGET_MS (at least one); the rest
 * wait for the next frames. Should the
 * view reach a piece still waiting, everything waiting is drawn at once.
 *
 * Names are screen-aligned and placed so they do not overlap, so a name cut by a
 * strip's edge would come out as half a name. The names on the layer are kept
 * (CanvasRenderer._layerLabels, in layer CSS px): on a shift they move with the
 * picture, the ones reaching into a strip are drawn again clipped to it, and new
 * names are placed only where the strips uncovered them, against the kept ones.
 *
 * Not used (a whole repaint instead) when the tree changed, the zoom or rotation
 * changed, the layer is stale, edit mode draws into it, or the move is at least
 * the layer's size.
 *
 * Depends on: CanvasRenderer (the layer, _renderTreeInto, _labelCandidates),
 * TreeStyle (labels), HoverOverlay (optional)
 */

var LayerScroll = {

    ENABLED: true,
    EARLY_SHARE: 0.5,         // shift once the drag has used this share of the margin
    CULL_PAD_PX: 8,           // css px added round a strip's world box, past the largest spell reach
    CHUNK_PX: 2048,           // device px: a strip is drawn in pieces no longer than this (each piece
                              // costs ~1.7 ms of its own - names, chapter titles - so pieces stay big)
    FRAME_BUDGET_MS: 6,       // pieces drawn in one frame, until this much time went on them

    _spare: null,
    _spareCtx: null,
    _failed: false,
    _pending: [],             // strips uncovered but not drawn yet, [x, y, w, h] layer device px

    /**
     * The layer rects (device px) a move of (sx, sy) device px uncovers in a
     * w x h layer: one per axis that moved. The vertical strip spans the full
     * height; the horizontal one leaves out the columns the vertical one has.
     * @returns {Array<Array<number>>} [x, y, w, h] each
     */
    exposedRects: function(w, h, sx, sy) {
        var out = [];
        var colX = 0, colW = w;
        if (sx > 0) { out.push([0, 0, sx, h]); colX = sx; colW = w - sx; }
        else if (sx < 0) { out.push([w + sx, 0, -sx, h]); colW = w + sx; }
        if (colW > 0) {
            if (sy > 0) out.push([colX, 0, colW, sy]);
            else if (sy < 0) out.push([colX, h + sy, colW, -sy]);
        }
        return out;
    },

    /**
     * Cut rects [x, y, w, h] into pieces no longer than `max` along either side,
     * nearest (cx, cy) first.
     */
    pieces: function(rects, max, cx, cy) {
        var out = [];
        for (var i = 0; i < rects.length; i++) {
            var r = rects[i];
            var nx = Math.max(1, Math.ceil(r[2] / max)), ny = Math.max(1, Math.ceil(r[3] / max));
            for (var a = 0; a < nx; a++) {
                var x0 = r[0] + Math.round(a * r[2] / nx), x1 = r[0] + Math.round((a + 1) * r[2] / nx);
                for (var b = 0; b < ny; b++) {
                    var y0 = r[1] + Math.round(b * r[3] / ny), y1 = r[1] + Math.round((b + 1) * r[3] / ny);
                    out.push([x0, y0, x1 - x0, y1 - y0]);
                }
            }
        }
        var d = function(p) { var ex = p[0] + p[2] / 2 - cx, ey = p[1] + p[3] / 2 - cy; return ex * ex + ey * ey; };
        out.sort(function(p, q) { return d(p) - d(q); });
        return out;
    },

    /** Do two rects {l, r, t, b} overlap? */
    _meets: function(a, b) {
        return a.l < b.r && a.r > b.l && a.t < b.b && a.b > b.t;
    },

    /**
     * Move the kept names by (ox, oy) css px and drop the ones now wholly
     * outside `bounds` {l, r, t, b}. Returns the kept list (same array).
     */
    shiftLabels: function(list, ox, oy, bounds) {
        var kept = 0;
        for (var i = 0; i < list.length; i++) {
            var lab = list[i];
            lab.x += ox; lab.y += oy;
            lab.l += ox; lab.r += ox; lab.t += oy; lab.b += oy;
            if (this._meets(lab, bounds)) list[kept++] = lab;
        }
        list.length = kept;
        return list;
    },

    /** A spare canvas the size of the layer, or null. */
    _ensureSpare: function(layer) {
        if (this._failed) return null;
        try {
            if (!this._spare) {
                this._spare = document.createElement('canvas');
                this._spareCtx = this._spare.getContext('2d');
                if (!this._spareCtx) throw new Error('no 2d context');
            }
            if (this._spare.width !== layer.width || this._spare.height !== layer.height) {
                this._spare.width = layer.width;
                this._spare.height = layer.height;
            }
            return this._spare;
        } catch (e) {
            this._failed = true;
            return null;
        }
    },

    /** Forget strips waiting to be drawn (the layer was repainted whole). */
    reset: function() {
        this._pending = [];
    },

    /**
     * The layer's part of a frame, when this module can do it: a waiting strip,
     * or a shift once the drag has used EARLY_SHARE of the margin. Returns true
     * when it did something (the layer and its pan may have changed), false when
     * the caller should go on as before (paste as it is, or repaint whole).
     * @param {Object} r - CanvasRenderer
     * @param {number} dpr
     * @param {number} margin - css px
     * @param {Object} view - the view render() made (cx, cy, rotRad, cos, sin)
     */
    step: function(r, dpr, margin, view) {
        if (!this.ENABLED || !r._layerLabels) return false;
        var dx = r.panX - r._layerPanX, dy = r.panY - r._layerPanY;
        if (this._pending.length) {
            // The view reached the bare edge: everything waiting, now, and shift on
            var all = Math.abs(dx) > margin || Math.abs(dy) > margin;
            this._drawSome(r, dpr, margin, view, all);
            if (!all) return true;
            if (this._shift(r, dpr, margin, view)) return true;
            return !(Math.abs(dx) > margin || Math.abs(dy) > margin);
        }
        var early = margin * this.EARLY_SHARE;
        if (Math.abs(dx) <= early && Math.abs(dy) <= early) return false;
        return this._shift(r, dpr, margin, view);
    },

    /** Move the picture by the drag and queue the uncovered strips; draws the first. */
    _shift: function(r, dpr, margin, view) {
        var layer = r._treeLayer;
        var sx = Math.round((r.panX - r._layerPanX) * dpr);
        var sy = Math.round((r.panY - r._layerPanY) * dpr);
        if (Math.abs(sx) >= layer.width || Math.abs(sy) >= layer.height) return false;
        var spare = this._ensureSpare(layer);
        if (!spare) return false;

        // The picture, moved ('copy' clears the rest in the same pass); then
        // the two canvases trade places
        var g = this._spareCtx;
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.globalAlpha = 1;
        g.globalCompositeOperation = 'copy';
        g.drawImage(layer, sx, sy);
        g.globalCompositeOperation = 'source-over';
        this._spare = layer;
        this._spareCtx = r._treeLayerCtx;
        r._treeLayer = spare;
        r._treeLayerCtx = g;

        // The layer's pan moves by the whole pixels; the rest stays for the paste
        r._layerPanX += sx / dpr;
        r._layerPanY += sy / dpr;
        this.shiftLabels(r._layerLabels, sx / dpr, sy / dpr,
            { l: -margin, r: spare.width / dpr - margin, t: -margin, b: spare.height / dpr - margin });
        this._pending = this.pieces(this.exposedRects(spare.width, spare.height, sx, sy), this.CHUNK_PX,
            spare.width / 2, spare.height / 2);
        r._layerScrolls = (r._layerScrolls || 0) + 1;
        this._drawSome(r, dpr, margin, view, false);
        return true;
    },

    /** Draw waiting pieces (at the layer's pan): all, or as many as fit in the frame budget (one at least). */
    _drawSome: function(r, dpr, margin, view, all) {
        var self = this, g = r._treeLayerCtx;
        var now = function() { return (typeof performance !== 'undefined') ? performance.now() : Date.now(); };
        var draw = function() {
            var start = now();
            self._found = undefined;               // the names that could go in, worked out once a frame
            do {
                self._drawStrip(r, g, self._pending.shift(), dpr, margin, view);
            } while (self._pending.length && (all || now() - start < self.FRAME_BUDGET_MS));
        };
        var livePanX = r.panX, livePanY = r.panY;
        r.panX = r._layerPanX; r.panY = r._layerPanY;      // _renderTreeInto draws at the pan in r
        try {
            if (typeof HoverOverlay !== 'undefined') HoverOverlay.withoutHover(r, draw);
            else draw();
        } finally {
            r.panX = livePanX; r.panY = livePanY;
        }
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
    },

    /** The world box (x0, y0, x1, y1) a layer rect (device px) shows, padded. */
    _worldBox: function(r, rect, dpr, margin, view, panX, panY) {
        var x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        var z = r.zoom, cos = view.cos, sin = view.sin;
        var xs = [rect[0], rect[0] + rect[2]], ys = [rect[1], rect[1] + rect[3]];
        for (var i = 0; i < 2; i++) {
            for (var j = 0; j < 2; j++) {
                // layer device px -> screen css px -> world (undo pan, zoom, rotation)
                var u = (xs[i] / dpr - margin - view.cx - panX) / z;
                var v = (ys[j] / dpr - margin - view.cy - panY) / z;
                var wx = u * cos + v * sin, wy = -u * sin + v * cos;
                if (wx < x0) x0 = wx; if (wx > x1) x1 = wx;
                if (wy < y0) y0 = wy; if (wy > y1) y1 = wy;
            }
        }
        // The largest thing a spell draws past its centre: its halo
        var pad = 2.6 * r._minSize(12) + this.CULL_PAD_PX / z;
        return { l: x0 - pad, r: x1 + pad, t: y0 - pad, b: y1 + pad };
    },

    _drawStrip: function(r, g, rect, dpr, margin, view) {
        var box = this._worldBox(r, rect, dpr, margin, view, r.panX, r.panY);
        g.save();
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.clearRect(rect[0], rect[1], rect[2], rect[3]);
        g.beginPath();
        g.rect(rect[0], rect[1], rect[2], rect[3]);
        g.clip();
        g.scale(dpr, dpr);
        g.translate(margin, margin);
        r._renderTreeInto(g, {
            cx: view.cx, cy: view.cy, rotRad: view.rotRad, cos: view.cos, sin: view.sin,
            viewLeft: box.l, viewRight: box.r, viewTop: box.t, viewBottom: box.b,
            labelMargin: margin, noLabels: true
        });
        g.restore();
        this._labels(r, g, rect, dpr, margin, view);
    },

    /** The names in a strip: kept ones reaching into it drawn again, new ones placed in it. */
    _labels: function(r, g, rect, dpr, margin, view) {
        var kept = r._layerLabels;
        var strips = [{ l: rect[0] / dpr - margin, r: (rect[0] + rect[2]) / dpr - margin,
                        t: rect[1] / dpr - margin, b: (rect[1] + rect[3]) / dpr - margin }];
        var s;
        if (this._found === undefined) this._found = r._labelCandidates(view.cx, view.cy, view.cos, view.sin, margin);
        var found = this._found;
        if (!found) return;
        g.save();
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.translate(margin, margin);
        TreeStyle.beginLabels(g, found.fontSize);
        g.textAlign = 'center';
        g.textBaseline = 'top';

        // Kept names reaching into a strip: drawn again, clipped to it
        var i, lab;
        for (s = 0; s < strips.length; s++) {
            var st = strips[s], clipped = false;
            for (i = 0; i < kept.length; i++) {
                lab = kept[i];
                if (!this._meets(lab, st)) continue;
                if (!clipped) {
                    g.save();
                    g.beginPath();
                    g.rect(st.l, st.t, st.r - st.l, st.b - st.t);
                    g.clip();
                    clipped = true;
                }
                g.globalAlpha = lab.alpha;
                TreeStyle.drawLabel(g, lab.text, lab.x, lab.y, lab.color);
            }
            if (clipped) g.restore();
        }

        // New names: only where a strip uncovered them, never over a kept one
        var have = {};
        for (i = 0; i < kept.length; i++) have[kept[i].node.id] = true;
        var cands = found.candidates;
        for (var c = 0; c < cands.length && kept.length < found.maxLabels; c++) {
            var cand = cands[c];
            if (have[cand.node.id]) continue;
            var rect = r._labelRect(g, cand, found.fontSize);
            var inStrip = false;
            for (s = 0; s < strips.length && !inStrip; s++) inStrip = this._meets(rect, strips[s]);
            if (!inStrip) continue;
            var collides = false;
            if (cand.priority < 5) {
                for (i = 0; i < kept.length; i++) {
                    if (this._meets(rect, kept[i])) { collides = true; break; }
                }
            }
            if (collides) continue;
            lab = r._keepLabel(cand, rect);
            kept.push(lab);
            have[cand.node.id] = true;
            g.globalAlpha = lab.alpha;
            TreeStyle.drawLabel(g, lab.text, lab.x, lab.y, lab.color);
        }
        g.restore();
        g.globalAlpha = 1;
    }
};

window.LayerScroll = LayerScroll;
