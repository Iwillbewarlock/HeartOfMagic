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
 * A repaint that is quick anyway (SYNC_MAX_MS or less) is still done at once.
 * What the last build cost counts its pieces' own overhead once, not once a
 * piece (the cheapest piece stands for it), so a tree that repaints quickly is
 * not spread for ever. So is the first repaint, one with no old picture to
 * show (stale layer, resize) and one in edit mode; and a tree that keeps
 * changing faster than a build ends (MAX_RESTARTS builds started again in a
 * row) is repainted at once.
 *
 * The view held still past the old picture's margin (a jump without a glide, a
 * drag during the build) would show its bare edge: the build goes on urgently -
 * a bigger share of the frame (URGENT_TARGET_FRAME_MS), the pieces the screen
 * shows first - and swaps in as soon as those and the names are done; the pieces
 * left over go to LayerScroll's queue, drawn in the next frames' time left like
 * a drag's strips. (It used to finish everything in that frame: one long frame.)
 * The bare edge shows for the frames that takes, as during a glide.
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

    _build: null,             // { panX, panY, zoom, rotation, dpr, margin, w, h, view, tiles, spent, glide,
                              //   pieces, minPiece, urgent }
    _lastMs: 0,               // what the last whole repaint cost (sync, or a build less its pieces' overhead), ms
    _pieceMs: 3,              // a piece's running cost, ms
    _finishMs: 4,             // the names and chapter titles' running cost, ms
    _restarts: 0,             // builds started again before one was done, in a row

    active: function() {
        return !!this._build;
    },

    /** Is the running build for a glide's end? */
    forGlide: function() {
        return !!(this._build && this._build.glide);
    },

    /** Should a whole repaint now be spread over frames? */
    wanted: function(r, layer) {
        return this.ENABLED && this._lastMs > this.SYNC_MAX_MS && !!layer && layer.width > 0 &&
            !this.overRestarts() &&
            typeof LayerScroll !== 'undefined' && !(typeof EditMode !== 'undefined' && EditMode.isActive);
    },

    /** Started again MAX_RESTARTS times in a row: the tree changes faster than a build ends. */
    overRestarts: function() {
        return this._restarts >= this.MAX_RESTARTS;
    },

    /** A whole repaint was just done at once; it took `ms`. */
    noteSync: function(ms) {
        this._lastMs = ms;
        this._build = null;
        this._restarts = 0;
    },

    /** Drop the build; the change it carried is marked again so it is not lost. */
    abort: function(r) {
        if (this._build && r) r._treeDirty = true;
        this._build = null;
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
            spent: 0, pieces: 0, minPiece: Infinity, urgent: false
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
            b.urgent = true;
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
                } else {
                    // The names and titles: in this frame only if they fit (or nothing was drawn yet)
                    if (drew && left() < self._finishMs) break;
                    var f0 = now();
                    self._finish(r, g, b);
                    self._finishMs = self._finishMs * 0.7 + (now() - f0) * 0.3;
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
        if (!b.pieces || !isFinite(b.minPiece)) return b.spent;
        return Math.max(0, b.spent - (b.pieces - 1) * b.minPiece);
    },

    _swapIn: function(r, b) {
        var old = r._treeLayer, oldCtx = r._treeLayerCtx;
        r._treeLayer = LayerScroll._spare;
        r._treeLayerCtx = LayerScroll._spareCtx;
        LayerScroll._spare = old;
        LayerScroll._spareCtx = oldCtx;
        LayerScroll.reset();
        // An urgent build swapped in before its last pieces: LayerScroll draws them
        // (clipped, the kept names drawn again into them) like a drag's strips
        if (b.tiles.length) LayerScroll._pending = b.tiles;
        r._layerPanX = b.panX;
        r._layerPanY = b.panY;
        r._layerZoom = b.zoom;
        r._layerRotation = b.rotation;
        r._treeLayerStale = false;
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
        r._layerBuilds = (r._layerBuilds || 0) + 1;
        // Decides sync or spread next time; a build cut short leaves its pieces' cost out
        if (!b.tiles.length) this._lastMs = this._estimate(b);
        this._build = null;
        this._restarts = 0;
    }
};

window.LayerBuild = LayerBuild;
