/**
 * LayerBuild - a whole repaint of the tree layer spread over several frames.
 *
 * A click on a spell (its path is highlighted, the rest dimmed) and a wheel zoom
 * or glide coming to rest repaint the whole tree layer: every spell, line and
 * name, 25-60 ms on a CPU canvas in the desktop bench, 12-19 ms logged in game -
 * one long frame each time. Instead the new picture is drawn onto the spare
 * canvas (LayerScroll's) in pieces, nearest the middle first, as many a frame
 * as fit in what is left of the frame (TARGET_FRAME_MS minus the frame so far,
 * one piece at least); the names and chapter titles go on last, whole, in a
 * frame of their own if they do not fit. Until then the old layer stays on
 * screen, mapped onto the view as during a glide (moved, stretched and turned),
 * and when the picture is done the two canvases swap.
 *
 * A camera glide (TreeCamera) says where it ends (CanvasRenderer._glideTarget):
 * the picture is built for that view while the camera glides, so after a click
 * the highlighted tree is ready when the camera arrives, however far it went.
 *
 * A repaint that is quick anyway (SYNC_MAX_MS or less, as the last one done at
 * once measured) is still done at once. A build tells too, counting its pieces'
 * own overhead once, not once a piece (the cheapest piece stands for it). A
 * clearly quick one (QUICK_SHARE of SYNC_MAX_MS) switches back to repaints at
 * once; a slower one raises the figure; a middling one leaves it as it is:
 * culled pieces can cost less than one whole repaint, so a slow tree's builds can
 * look quick, and trying it at once again would be one long frame (a figure
 * lowered a little a build did that every ~11 builds). A tree that was slow once
 * (a hitch) and is middling since stays spread - its repaints show the old
 * picture a frame or two longer, with no long frame. The repaint right after a
 * design, effects, font or language change is not counted (noteRestyle): it
 * makes the sprites, patterns and text widths and would pass for a slow tree -
 * unless nothing was measured yet: then it is the figure, as a slow tree's first
 * click must be spread (a quick tree may stay spread, no long frame).
 * After such a change the names' font is new too (_textCold): the first names in
 * a new face cost 5-15 ms more (widths, shaping, glyph outlines), so the repaint
 * is spread even for a quick tree, and before the names its build measures their
 * widths and draws each letter once on a small canvas, in the frames' time left
 * (_warmText); an uncounted build that was slow puts the figure just past
 * SYNC_MAX_MS, as noteSync does.
 * Done at once whatever the figure: the first repaint, one with
 * no old picture to show (stale layer, resize) and one in edit mode; and a tree
 * that keeps changing faster than a build ends (MAX_RESTARTS builds started
 * again in a row, the count starting over when a build is dropped for another
 * reason, done or drawn at once) is repainted at once - one long frame, where
 * letting builds finish with a change pending would show the tree a change late
 * for as long as the changes keep coming.
 *
 * The view held still past the old picture's margin (a jump without a glide, a
 * drag during the build) would show its bare edge: the build goes on urgently -
 * a bigger share of the frame (URGENT_TARGET_FRAME_MS), the pieces the screen
 * shows first - and swaps in as soon as those and the names are done; the pieces
 * left over go to LayerScroll's queue, drawn in the next frames' time left like
 * a drag's strips (the next frame is asked for at once; a frame that stretches
 * the layer draws them too, LayerScroll.drawPendingAside, unless a new build is
 * under way or the tree changed).
 * (It used to finish everything in that frame: one long frame.) The bare edge
 * shows for the frames that takes, as during a glide.
 *
 * A build is dropped when the view it was for is gone (zoom, rotation, glide
 * target, pixel ratio, layer size), and the tree is marked for a repaint again
 * so the change it carried is not lost.
 *
 * Depends on: CanvasRenderer (the layer, _renderTreeInto, renderLabels),
 * LayerScroll (the spare canvas, pieces, the piece drawing, its queue),
 * TreeStyle, HoverOverlay (optional)
 */

var LayerBuild = {

    ENABLED: true,
    SYNC_MAX_MS: 8,           // a whole repaint this quick is still done in one frame
    TARGET_FRAME_MS: 6,       // pieces fill a frame up to this, from its start (the paste, the hub and
                              // the moving parts come after the tree, so it leaves them room)
    URGENT_TARGET_FRAME_MS: 11, // ...and up to this while the old picture shows its bare edge
    TILE_PX: 384,             // device px: the layer is drawn in pieces this big (each costs ~1 ms of its own)
    MAX_RESTARTS: 4,          // builds started again (the tree changed meanwhile) in a row: then at once
    QUICK_SHARE: 0.5,         // a build this share of SYNC_MAX_MS or less lets the next repaint try at once

    _build: null,             // { panX, panY, zoom, rotation, dpr, margin, w, h, view, tiles, spent, glide,
                              //   pieces, minPiece }
    _lastMs: 0,               // what the last whole repaint cost (sync, or a build less its pieces' overhead), ms
    _pieceMs: 3,              // a piece's running cost, ms
    _finishMs: 4,             // the names and chapter titles' running cost, ms
    _restarts: 0,             // builds started again before one was done, in a row
    _unmeasured: false,       // the next whole repaint follows a design or language change (noteRestyle)
    _textCold: false,         // ...and the names' font is new since names were last drawn: their widths
                              // and glyphs are made first (_warmText), spread, before the finish
    _warmCanvas: null,        // a small canvas the glyphs are drawn on to make them (_warmText)
    WARM_PAD_PX: 4,           // css px round a letter drawn there; the canvas is two letters tall and wide

    active: function() {
        return !!this._build;
    },

    /** Is the running build for a glide's end? */
    forGlide: function() {
        return !!(this._build && this._build.glide);
    },

    /** Should a whole repaint now be spread over frames? */
    wanted: function(r, layer) {
        // With the names' font new (a design switch) even a quick tree's repaint is
        // spread: making the widths and glyphs of a new face costs 5-15 ms by itself
        return this.ENABLED && (this._lastMs > this.SYNC_MAX_MS || this._textCold) && !!layer && layer.width > 0 &&
            !this.overRestarts() &&
            typeof LayerScroll !== 'undefined' && !(typeof EditMode !== 'undefined' && EditMode.isActive);
    },

    /** Started again MAX_RESTARTS times in a row: the tree changes faster than a build ends. */
    overRestarts: function() {
        return this._restarts >= this.MAX_RESTARTS;
    },

    /**
     * A design or language was just applied: the next whole repaint makes its
     * sprites, patterns and text widths the first time (18-26 ms in the bench
     * where the tree takes 3-9), so what it costs says nothing about the tree -
     * taken as _lastMs it would keep a quick tree spread for good (a middling
     * build leaves the figure). It is not counted; the one after it is. Called
     * where the caches go: TreeStyle.set (a design, Design Effects), a late
     * stylesheet or font (TreeStyle.fontsChanged), a language (switchLocale).
     * With nothing measured yet (the session's first repaint: the design is
     * applied at start-up) its cost is the figure all the same, so a slow tree's
     * first click is spread; later builds change it by the usual rule (a middling
     * one leaves it: replacing it outright gave a slow tree whose culled builds
     * look quick one long frame, on the click after). A quick tree measured high
     * this way stays spread unless a build is clearly quick - its repaints show
     * the old picture a frame or two longer, no long frame.
     */
    noteRestyle: function() {
        this._unmeasured = true;
        this._textCold = true;
    },

    /** A whole repaint was just done at once; it took `ms`. */
    noteSync: function(ms) {
        if (!this._unmeasured || !(this._lastMs > 0)) {
            this._lastMs = ms;
        } else if (ms > this.SYNC_MAX_MS) {
            // Not counted, but slow: the design may be heavier than the last one
            // (a light one to one with page, sigils and glow). The figure only goes
            // just past SYNC_MAX_MS, so the next repaint is spread and its build
            // says (a slower one raises it, a clearly quick one brings it down);
            // left as it was, the next click was a repaint at once - a long frame
            this._lastMs = Math.max(this._lastMs, this.SYNC_MAX_MS + 1);
        }
        this._unmeasured = false;
        this._textCold = false;                 // the names were drawn with it
        this._build = null;
        this._restarts = 0;
    },

    /**
     * Drop the build; the change it carried is marked again so it is not lost.
     * The restart count starts again too (the next build is not a restart of this
     * one), except when the count itself is why it goes (keepRestarts): the
     * repaint at once that follows resets it (noteSync).
     */
    abort: function(r, keepRestarts) {
        if (this._build && r) r._treeDirty = true;
        this._build = null;
        if (!keepRestarts) this._restarts = 0;
    },

    /** Is the running build still for the view it would be shown in (and not in edit mode)? */
    valid: function(r, dpr, layer) {
        var b = this._build;
        if (!b || b.dpr !== dpr || !layer || b.w !== layer.width || b.h !== layer.height) return false;
        if (typeof EditMode !== 'undefined' && EditMode.isActive) return false;
        var g = r._glideTarget;
        if (b.glide && g && g.zoom === b.zoom && g.rotation === b.rotation && g.panX === b.panX && g.panY === b.panY) return true;
        return b.zoom === r.zoom && b.rotation === r.rotation;
    },

    /**
     * Start a build of the whole layer: for `target` (a glide's end: zoom,
     * rotation, panX, panY) or else the view as it is now. Replacing a build
     * that was not done counts towards MAX_RESTARTS.
     */
    start: function(r, dpr, margin, view, target) {
        if (this._build) this._restarts++;
        var layer = r._treeLayer;
        var spare = LayerScroll._ensureSpare(layer);
        if (!spare) { this._build = null; return false; }
        var g = LayerScroll._spareCtx;
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.globalAlpha = 1;
        g.globalCompositeOperation = 'source-over';
        g.clearRect(0, 0, spare.width, spare.height);
        var t = target || { zoom: r.zoom, rotation: r.rotation, panX: r.panX, panY: r.panY };
        var rad = t.rotation * Math.PI / 180;
        this._build = {
            panX: t.panX, panY: t.panY, zoom: t.zoom, rotation: t.rotation, glide: !!target,
            dpr: dpr, margin: margin, w: layer.width, h: layer.height,
            view: { cx: view.cx, cy: view.cy, rotRad: rad, cos: Math.cos(rad), sin: Math.sin(rad) },
            tiles: LayerScroll.pieces([[0, 0, layer.width, layer.height]], this.TILE_PX, layer.width / 2, layer.height / 2),
            spent: 0, pieces: 0, minPiece: Infinity
        };
        return true;
    },

    /**
     * What of the build's layer (device px) the screen shows now, grown by
     * LayerScroll.LOOKAHEAD_PX: the pieces to draw first when urgent.
     */
    _screenRect: function(r, b) {
        var pad = Math.round(LayerScroll.LOOKAHEAD_PX * b.dpr);
        var offX = Math.round((b.margin - (r.panX - b.panX)) * b.dpr);
        var offY = Math.round((b.margin - (r.panY - b.panY)) * b.dpr);
        var w = r.canvas ? r.canvas.width : b.w, h = r.canvas ? r.canvas.height : b.h;
        return [offX - pad, offY - pad, w + 2 * pad, h + 2 * pad];
    },

    /**
     * Draw what fits in this frame. urgent: the old picture shows its bare edge -
     * a bigger share of the frame, the pieces on screen first, and the swap as
     * soon as those and the names are done (the rest to LayerScroll's queue).
     * Returns true when the picture is swapped in (the renderer's layer changed).
     * @param {Object} r - CanvasRenderer
     * @param {number} frameStart - performance.now() at the start of the frame
     * @param {boolean} [urgent]
     */
    step: function(r, frameStart, urgent) {
        var b = this._build;
        if (!b) return false;
        var self = this, g = LayerScroll._spareCtx;
        var now = function() { return (typeof performance !== 'undefined') ? performance.now() : Date.now(); };
        var t0 = now();
        var drew = false, done = false, onScreen = -1;
        if (urgent) {
            // The pieces the screen shows first (kept nearest the middle first among them)
            var vr = this._screenRect(r, b), shown = [], rest = [];
            for (var i = 0; i < b.tiles.length; i++) (LayerScroll.overlaps(b.tiles[i], vr) ? shown : rest).push(b.tiles[i]);
            b.tiles = shown.concat(rest);
            onScreen = shown.length;
        }
        var target = urgent ? this.URGENT_TARGET_FRAME_MS : this.TARGET_FRAME_MS;
        var left = function() { return target - (now() - frameStart); };
        var draw = function() {
            while (!done) {
                if (b.tiles.length && onScreen !== 0) {
                    if (drew && left() < self._pieceMs) break;
                    var p0 = now();
                    LayerScroll._drawStrip(r, g, b.tiles.shift(), b.dpr, b.margin, b.view, true);
                    var ms = now() - p0;
                    self._pieceMs = self._pieceMs * 0.7 + ms * 0.3;
                    b.pieces++;
                    if (ms < b.minPiece) b.minPiece = ms;
                    if (onScreen > 0) onScreen--;
                } else if (self._textCold && !b.warmed) {
                    // A new font: its widths and glyphs made in the time left, then the names
                    // (their time is the font's, not the tree's: left out of _estimate)
                    if (drew && left() <= 0) break;
                    var w0 = now(), warmed = self._warmText(r, b, left);
                    b.warmMs = (b.warmMs || 0) + now() - w0;
                    if (!warmed) break;
                } else {
                    // The names and titles: in this frame only if they fit (or nothing was drawn yet)
                    if (drew && left() < self._finishMs) break;
                    var f0 = now();
                    self._finish(r, g, b);
                    self._finishMs = self._finishMs * 0.7 + (now() - f0) * 0.3;
                    self._textCold = false;
                    done = true;
                }
                drew = true;
            }
        };
        // Drawn as the view the build is for: pan, zoom, turn and level of detail
        var live = { panX: r.panX, panY: r.panY, zoom: r.zoom, rotation: r.rotation, lod: r._lodTier };
        r.panX = b.panX; r.panY = b.panY; r.zoom = b.zoom; r.rotation = b.rotation;
        if (typeof r._computeLODTier === 'function') r._lodTier = r._computeLODTier();
        try {
            if (typeof HoverOverlay !== 'undefined') HoverOverlay.withoutHover(r, draw);
            else draw();
        } finally {
            r.panX = live.panX; r.panY = live.panY; r.zoom = live.zoom; r.rotation = live.rotation; r._lodTier = live.lod;
        }
        b.spent += now() - t0;
        if (!done) {
            // The next frame, unthrottled (animation frames come only ~12 a second
            // and not at all while a button is held) - not the dirty-marking setter
            r.__needsRender = true;
            r._animationOnlyRender = false;
            return false;
        }
        this._swapIn(r, b);
        return true;
    },

    /**
     * Before the names of a new font (a design, a late web font, a language):
     * their widths measured into the renderer's cache (_labelWidth) and each
     * letter drawn once, outlined and filled, at the names' and the chapter
     * titles' size and scale on a small canvas of its own - the first names in
     * a new face cost 5-15 ms more than the next (widths, shaping, glyph outlines
     * for the halo; 13 ms on Chalkboard in the bench), all in the finish's frame
     * before. As many as fit in the time left, one at least; returns true when
     * all are done (b.warmed). The glyph part holds where the engine keeps its
     * glyphs across canvases (Chrome does); the widths are ours.
     */
    _warmText: function(r, b, left) {
        var w = b.warm;
        if (!w) {
            var v = b.view, found = r._labelCandidates ? r._labelCandidates(v.cx, v.cy, v.cos, v.sin, b.margin) : null;
            var texts = [], chars = '', titles = '', seen = {}, i, k, s;
            if (found) for (i = 0; i < found.candidates.length; i++) texts.push(found.candidates[i].text);
            var addChars = function(text, into) {
                for (k = 0; k < text.length; k++) {
                    var c = text.charAt(k), key = into + c;
                    if (!seen[key]) { seen[key] = true; if (into === 'n') chars += c; else titles += c; }
                }
            };
            for (i = 0; i < texts.length; i++) if (texts[i]) addChars(texts[i], 'n');
            if (TreeStyle.tokens && TreeStyle.tokens.chapterTitles && r.schools) {
                for (s in r.schools) if (r.schools.hasOwnProperty(s)) addChars('— ' + TreeStyle._schoolName(s), 't');
            }
            w = b.warm = { texts: texts, chars: chars, titles: titles, fontSize: found ? found.fontSize : 0, i: 0, j: 0, k: 0 };
            if (!this._warmCanvas && typeof document !== 'undefined' && document.createElement) {
                try { this._warmCanvas = document.createElement('canvas'); } catch (e) { this._warmCanvas = null; }
            }
            // Big enough for the largest letter at the build's scale (a letter off the
            // canvas may not be drawn at all, and so not made)
            var tk = TreeStyle.tokens || {};
            var side = Math.ceil((2 * Math.max(w.fontSize, tk.chapterTitles ? tk.chapterSize || 0 : 0) + 2 * this.WARM_PAD_PX) * b.dpr);
            if (this._warmCanvas && (this._warmCanvas.width !== side || this._warmCanvas.height !== side)) {
                this._warmCanvas.width = side;
                this._warmCanvas.height = side;
            }
        }
        var ctx = this._warmCanvas && this._warmCanvas.getContext ? this._warmCanvas.getContext('2d') : null;
        if (!ctx) { b.warmed = true; return true; }
        var t = TreeStyle.tokens || {}, phase = -1, pad = this.WARM_PAD_PX;
        // The canvas is set up afresh each call (a late font may have emptied the width cache meanwhile)
        var setUp = function(to) {
            if (phase === to) return;
            phase = to;
            ctx.setTransform(b.dpr, 0, 0, b.dpr, 0, 0);
            ctx.textAlign = 'left';
            ctx.textBaseline = 'top';
            TreeStyle.beginLabels(ctx, w.fontSize);
            if (to === 0) r._labelFontFrom(ctx);
            if (to === 2) {
                ctx.font = t.chapterSize + 'px ' + TreeStyle.labelFamily();
                ctx.lineWidth = t.labelHaloWidth + 1;
            }
        };
        var letter = function(c) {
            if (t.labelHalo) ctx.strokeText(c, pad, pad);
            ctx.fillText(c, pad, pad);
        };
        do {
            if (w.i < w.texts.length) {
                // The widths, with the font set as renderLabels sets it, so the cache is kept
                setUp(0);
                r._labelWidth(ctx, w.texts[w.i++]);
            } else if (w.j < w.chars.length) {
                setUp(1);
                letter(w.chars.charAt(w.j++));
            } else if (w.k < w.titles.length) {
                setUp(2);
                letter(w.titles.charAt(w.k++));
            } else {
                break;
            }
        } while (left() > 0);
        if (w.i >= w.texts.length && w.j >= w.chars.length && w.k >= w.titles.length) b.warmed = true;
        return !!b.warmed;
    },

    /** The names and chapter titles, whole, over the finished pieces. */
    _finish: function(r, g, b) {
        var v = b.view;
        g.save();
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.scale(b.dpr, b.dpr);
        g.translate(b.margin, b.margin);
        r.renderLabels(g, v.cx, v.cy, v.cos, v.sin, b.margin);
        TreeStyle.renderChapters(g, r, v.cx, v.cy, v.cos, v.sin, b.margin);
        g.restore();
        g.globalAlpha = 1;
    },

    /**
     * What a whole repaint at once would have cost: the build's time less its
     * pieces' own overhead but one (the cheapest piece stands for it - a piece
     * with nothing in it, an empty corner of the margin, costs about that).
     */
    _estimate: function(b) {
        var spent = b.spent - (b.warmMs || 0);
        if (!b.pieces || !isFinite(b.minPiece)) return Math.max(0, spent);
        return Math.max(0, spent - (b.pieces - 1) * b.minPiece);
    },

    _swapIn: function(r, b) {
        var old = r._treeLayer, oldCtx = r._treeLayerCtx;
        r._treeLayer = LayerScroll._spare;
        r._treeLayerCtx = LayerScroll._spareCtx;
        LayerScroll._spare = old;
        LayerScroll._spareCtx = oldCtx;
        LayerScroll.reset();
        // An urgent build swapped in before its last pieces: LayerScroll draws them
        // (clipped, the kept names drawn again into them) like a drag's strips, from
        // the next frame on - asked for now, unthrottled (animation frames come only
        // ~12 a second, none at all when idle), as step() does for a build's next piece
        if (b.tiles.length) {
            LayerScroll._pending = b.tiles;
            r.__needsRender = true;
            r._animationOnlyRender = false;
        }
        r._layerPanX = b.panX;
        r._layerPanY = b.panY;
        r._layerZoom = b.zoom;
        r._layerRotation = b.rotation;
        r._treeLayerStale = false;
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
        r._layerBuilds = (r._layerBuilds || 0) + 1;
        // Decides sync or spread next time (a build cut short says nothing). A
        // clearly quick build sets it; a slow one raises it; a middling one leaves
        // it: a build's pieces are culled to their own box and may cost less than
        // one repaint of the whole view, so taken as it is (or lowered a little a
        // build) it would switch a slow tree back to a long frame
        // A build after a design change (noteRestyle) is not counted, swapped in
        // whole or early (its pieces on screen and the names made the caches)
        this._textCold = false;                 // its names were drawn (the finish comes before any swap)
        if (this._unmeasured) {
            this._unmeasured = false;
            // Not counted, but slow: as noteSync, the figure goes just past SYNC_MAX_MS
            // (the new design may be heavier; the repaint after a design switch is
            // spread since its names' font is new, so this is where a quick tree hears it)
            if (!b.tiles.length && this._lastMs > 0 && this._estimate(b) > this.SYNC_MAX_MS) {
                this._lastMs = Math.max(this._lastMs, this.SYNC_MAX_MS + 1);
            }
        } else if (!b.tiles.length) {
            var est = this._estimate(b);
            if (est < this.SYNC_MAX_MS * this.QUICK_SHARE || est > this._lastMs) this._lastMs = est;
        }
        this._build = null;
        this._restarts = 0;
    }
};

window.LayerBuild = LayerBuild;
