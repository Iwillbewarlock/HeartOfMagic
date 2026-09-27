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
 * strips are still off screen, in the margin. Pieces the screen shows (or is
 * about to, LOOKAHEAD_PX) are drawn in the same frame whatever the time; the
 * others fill what is left of the frame (TARGET_FRAME_MS) and ask for the next
 * frames. Another shift moves the waiting pieces with the picture.
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
    TARGET_FRAME_MS: 8,       // pieces the screen does not show yet fill a frame up to this, from its start
    LOOKAHEAD_PX: 48,         // css px round the screen that count as shown (the drag's next frames)
    DESCENT_SHARE: 0.35,      // of the font size, how far letters and outline reach below a name's box
    KEPT_MAX_SHARE: 2,        // names kept on the layer at most, as a multiple of the on-screen cap

    _spare: null,
    _spareCtx: null,
    _failed: false,
    _pending: [],             // strips uncovered but not drawn yet, [x, y, w, h] layer device px
    _pieceMs: 3,              // what a piece has cost lately, ms (a running average)
    _drewThisFrame: false,
    _found: undefined,        // the name candidates of this frame (_labels)

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

    /**
     * Move waiting rects [x, y, w, h] by (sx, sy) with the picture and cut them
     * to the w x h layer; the ones pushed wholly off it are dropped.
     */
    moveRects: function(rects, sx, sy, w, h) {
        var out = [];
        for (var i = 0; i < rects.length; i++) {
            var x0 = Math.max(0, rects[i][0] + sx), y0 = Math.max(0, rects[i][1] + sy);
            var x1 = Math.min(w, rects[i][0] + rects[i][2] + sx), y1 = Math.min(h, rects[i][1] + rects[i][3] + sy);
            if (x1 > x0 && y1 > y0) out.push([x0, y0, x1 - x0, y1 - y0]);
        }
        return out;
    },

    /** Does rect p [x, y, w, h] overlap rect v [x, y, w, h]? */
    overlaps: function(p, v) {
        return p[0] < v[0] + v[2] && p[0] + p[2] > v[0] && p[1] < v[1] + v[3] && p[1] + p[3] > v[1];
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
     * What of the layer the screen shows this frame, in layer device px, grown by
     * LOOKAHEAD_PX so a strip about to come into view counts as shown.
     */
    _viewRect: function(r, dpr, margin) {
        var pad = Math.round(this.LOOKAHEAD_PX * dpr);
        var offX = Math.round((margin - (r.panX - r._layerPanX)) * dpr);
        var offY = Math.round((margin - (r.panY - r._layerPanY)) * dpr);
        return [offX - pad, offY - pad, r.canvas.width + 2 * pad, r.canvas.height + 2 * pad];
    },

    /**
     * The layer's part of a frame, when this module can do it: a shift once the
     * drag has used EARLY_SHARE of the margin, and waiting strips. Returns true
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
        var early = margin * this.EARLY_SHARE;
        var did = false;
        if (Math.abs(dx) > early || Math.abs(dy) > early) {
            if (!this._shift(r, dpr, margin)) {
                this._pending = [];
                return false;                              // too far: the caller repaints whole
            }
            did = true;
        }
        if (this._pending.length) {
            this._drawSome(r, dpr, margin, view);
            did = true;
        }
        return did;
    },

    /** Move the picture by the drag; the uncovered strips join the waiting ones. */
    _shift: function(r, dpr, margin) {
        var layer = r._treeLayer;
        var sx = Math.round((r.panX - r._layerPanX) * dpr);
        var sy = Math.round((r.panY - r._layerPanY) * dpr);
        if (Math.abs(sx) >= layer.width || Math.abs(sy) >= layer.height) return false;
        var spare = this._ensureSpare(layer);
        if (!spare) return false;

        // The picture, moved, onto the cleared spare; then the two canvases trade
        // places. (Not 'copy' compositing: an engine that composites drawImage
        // under 'copy' as source-over would let the spare's old picture show
        // through the see-through parts. The clear costs about the same.)
        var g = this._spareCtx;
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.globalAlpha = 1;
        g.globalCompositeOperation = 'source-over';
        g.clearRect(0, 0, spare.width, spare.height);
        g.drawImage(layer, sx, sy);
        var uncovered = this.exposedRects(spare.width, spare.height, sx, sy);
        this._spare = layer;
        this._spareCtx = r._treeLayerCtx;
        r._treeLayer = spare;
        r._treeLayerCtx = g;

        // The layer's pan moves by the whole pixels; the rest stays for the paste
        r._layerPanX += sx / dpr;
        r._layerPanY += sy / dpr;
        this.shiftLabels(r._layerLabels, sx / dpr, sy / dpr,
            { l: -margin, r: spare.width / dpr - margin, t: -margin, b: spare.height / dpr - margin });
        this._pending = this.moveRects(this._pending, sx, sy, spare.width, spare.height)
            .concat(this.pieces(uncovered, this.CHUNK_PX, spare.width / 2, spare.height / 2));
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
        r._layerScrolls = (r._layerScrolls || 0) + 1;
        return true;
    },

    /**
     * Draw waiting pieces (at the layer's pan): every one the screen shows or is
     * about to, whatever the time; then others while the frame has time left
     * (TARGET_FRAME_MS from its start, the next piece at its usual cost, one at
     * least if none was drawn). Pieces left over ask for the next frame.
     */
    _drawSome: function(r, dpr, margin, view) {
        var self = this, g = r._treeLayerCtx;
        var now = function() { return (typeof performance !== 'undefined') ? performance.now() : Date.now(); };
        var vr = this._viewRect(r, dpr, margin);
        // The screen in layer css px, for counting the names shown (_labels)
        var viewCss = { l: vr[0] / dpr - margin, r: (vr[0] + vr[2]) / dpr - margin,
                        t: vr[1] / dpr - margin, b: (vr[1] + vr[3]) / dpr - margin };
        var must = [], rest = [];
        for (var i = 0; i < this._pending.length; i++) {
            (this.overlaps(this._pending[i], vr) ? must : rest).push(this._pending[i]);
        }
        var frameStart = r._frameStartAt || now();
        var draw = function() {
            self._found = undefined;               // the names that could go in, worked out once a frame
            while (must.length) self._drawTimed(r, g, must.shift(), dpr, margin, view, viewCss);
            while (rest.length) {
                // One piece at least when nothing was drawn yet, or a frame that is
                // always late would ask for frames forever without progress
                var left = self.TARGET_FRAME_MS - (now() - frameStart);
                if (self._drewThisFrame && left < self._pieceMs) break;
                self._drawTimed(r, g, rest.shift(), dpr, margin, view, viewCss);
            }
        };
        this._drewThisFrame = false;
        var livePanX = r.panX, livePanY = r.panY;
        r.panX = r._layerPanX; r.panY = r._layerPanY;      // _renderTreeInto draws at the pan in r
        try {
            if (typeof HoverOverlay !== 'undefined') HoverOverlay.withoutHover(r, draw);
            else draw();
        } finally {
            r.panX = livePanX; r.panY = livePanY;
        }
        this._pending = rest;
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
        if (rest.length) {
            // The next frame, unthrottled - not the dirty-marking setter: the tree did not change
            r.__needsRender = true;
            r._animationOnlyRender = false;
        }
    },

    _drawTimed: function(r, g, piece, dpr, margin, view, viewCss) {
        var t0 = (typeof performance !== 'undefined') ? performance.now() : Date.now();
        this._drawStrip(r, g, piece, dpr, margin, view, false, viewCss);
        var ms = ((typeof performance !== 'undefined') ? performance.now() : Date.now()) - t0;
        this._pieceMs = this._pieceMs * 0.7 + ms * 0.3;
        this._drewThisFrame = true;
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
        // The largest thing a spell draws past its centre: the halo of a
        // selected spell (a little bigger than the others)
        var pad = r.HALO_SCALE * (r._minSize(r.KNOWN_SIZE) + r.FOCUS_GROW) + this.CULL_PAD_PX / z;
        return { l: x0 - pad, r: x1 + pad, t: y0 - pad, b: y1 + pad };
    },

    /**
     * Draw the tree into one layer rect (device px) of g, clipped to it, the
     * spells and lines culled to its world box. whole: a piece of a whole-layer
     * build (LayerBuild) - no names or chapter titles, drawn once at the end.
     */
    _drawStrip: function(r, g, rect, dpr, margin, view, whole, viewCss) {
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
            labelMargin: margin, noLabels: true, noChapters: true
        });
        g.restore();
        if (whole) return;
        // Names, then chapter titles over them, as a whole repaint draws them
        // (the titles used to go in with the tree, under the names: a seam at the
        // strip's edge where a name crossed a title)
        this._labels(r, g, rect, dpr, margin, view, viewCss);
        this._chapters(r, g, rect, dpr, margin, view);
    },

    /** The chapter titles in a strip, clipped to it (after its names). */
    _chapters: function(r, g, rect, dpr, margin, view) {
        g.save();
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.beginPath();
        g.rect(rect[0], rect[1], rect[2], rect[3]);
        g.clip();
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.translate(margin, margin);
        TreeStyle.renderChapters(g, r, view.cx, view.cy, view.cos, view.sin, margin);
        g.restore();
        g.globalAlpha = 1;
    },

    /**
     * The names in a strip: kept ones reaching into it drawn again (clipped to
     * it, their outline and descenders counted), new ones placed in it against
     * the kept ones. The cap counts the names on screen, not the ones left in
     * the margin behind the drag.
     */
    _labels: function(r, g, rect, dpr, margin, view, viewCss) {
        var kept = r._layerLabels;
        var st = { l: rect[0] / dpr - margin, r: (rect[0] + rect[2]) / dpr - margin,
                   t: rect[1] / dpr - margin, b: (rect[1] + rect[3]) / dpr - margin };
        if (this._found === undefined) this._found = r._labelCandidates(view.cx, view.cy, view.cos, view.sin, margin);
        var found = this._found;
        if (!found) return;
        g.save();
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.translate(margin, margin);
        TreeStyle.beginLabels(g, found.fontSize);
        r._labelFontFrom(g);
        g.textAlign = 'center';
        g.textBaseline = 'top';

        var i, lab;
        var reach = (TreeStyle.tokens.labelHaloWidth || 0) + found.fontSize * this.DESCENT_SHARE;
        var clipped = false;
        for (i = 0; i < kept.length; i++) {
            lab = kept[i];
            if (!(lab.l - reach < st.r && lab.r + reach > st.l && lab.t - reach < st.b && lab.b + reach > st.t)) continue;
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

        var shown = 0, have = {};
        for (i = 0; i < kept.length; i++) {
            have[kept[i].node.id] = true;
            if (!viewCss || this._meets(kept[i], viewCss)) shown++;
        }
        var cands = found.candidates;
        var keptMax = found.maxLabels * this.KEPT_MAX_SHARE;
        var pad = r.LABEL_PAD, fontSize = found.fontSize;
        for (var c = 0; c < cands.length && shown < found.maxLabels && kept.length < keptMax; c++) {
            var cand = cands[c];
            if (have[cand.node.id]) continue;
            // Above or below the strip: its box cannot meet it (the box's top and
            // bottom as _labelRect makes them), and its width need not be looked up
            if (!(cand.y - pad < st.b && cand.y + fontSize + pad > st.t)) continue;
            var box = r._labelRect(g, cand, fontSize);
            if (!this._meets(box, st)) continue;
            var collides = false;
            if (cand.priority < 5) {
                for (i = 0; i < kept.length; i++) {
                    if (this._meets(box, kept[i])) { collides = true; break; }
                }
            }
            if (collides) continue;
            lab = r._keepLabel(cand, box);
            kept.push(lab);
            have[cand.node.id] = true;
            if (!viewCss || this._meets(lab, viewCss)) shown++;
            g.globalAlpha = lab.alpha;
            TreeStyle.drawLabel(g, lab.text, lab.x, lab.y, lab.color);
        }
        g.restore();
        g.globalAlpha = 1;
    }
};

window.LayerScroll = LayerScroll;
