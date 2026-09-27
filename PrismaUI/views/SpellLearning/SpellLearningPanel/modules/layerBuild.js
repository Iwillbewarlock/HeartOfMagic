/**
 * LayerBuild - a whole repaint of the tree layer spread over several frames.
 *
 * A click on a spell (its path is highlighted, the rest dimmed) and a wheel zoom
 * or glide coming to rest repaint the whole tree layer: every spell, line and
 * name, 25-60 ms on a CPU canvas in the desktop bench, 12-19 ms logged in game -
 * one long frame each time. Instead the new picture is drawn onto the spare
 * canvas (LayerScroll's) in pieces, nearest the middle first, as many a frame
 * as fit in what is left of the frame (TARGET_FRAME_MS minus the frame so far,
 * MIN_PIECE_MS at least); the names and chapter titles go on last, whole. Until
 * then the old layer stays on screen, mapped onto the view as during a glide
 * (moved, stretched and turned as the camera went), and when the picture is
 * done the two canvases swap.
 * The highlight of a click shows a few frames later; nothing stalls.
 *
 * A repaint that is quick anyway (the last whole one took SYNC_MAX_MS or less,
 * e.g. with undiscovered spells hidden) is still done at once. So is the first
 * one, one with no old picture to show (stale layer, resize), and one in edit
 * mode. A build is dropped when the zoom, rotation, pixel ratio or layer size
 * changes under it, and started again when the tree changes again.
 *
 * Depends on: CanvasRenderer (the layer, _renderTreeInto, renderLabels),
 * LayerScroll (the spare canvas, pieces, the piece drawing), TreeStyle,
 * HoverOverlay (optional)
 */

var LayerBuild = {

    ENABLED: true,
    SYNC_MAX_MS: 8,           // a whole repaint this quick is still done in one frame
    TARGET_FRAME_MS: 8,       // pieces fill a frame up to this, counted from the frame's start
    MIN_PIECE_MS: 1,          // ...but a frame always gets one piece in
    TILE_PX: 384,             // device px: the layer is drawn in pieces this big (each costs ~1 ms of its own)

    _build: null,             // { pan, zoom, rotation, dpr, w, h, view, tiles, spent }
    _lastMs: 0,               // what the last whole repaint cost (sync or all pieces), ms

    active: function() {
        return !!this._build;
    },

    /** Should a whole repaint now be spread over frames? */
    wanted: function(r, layer) {
        return this.ENABLED && this._lastMs > this.SYNC_MAX_MS && !!layer && layer.width > 0 &&
            typeof LayerScroll !== 'undefined' && !(typeof EditMode !== 'undefined' && EditMode.isActive);
    },

    /** A whole repaint was just done at once; it took `ms`. */
    noteSync: function(ms) {
        this._lastMs = ms;
        this._build = null;
    },

    abort: function() {
        this._build = null;
    },

    /** Is the running build still for this view (zoom, rotation, pixel ratio, layer size)? */
    valid: function(r, dpr, layer) {
        var b = this._build;
        return !!b && b.zoom === r.zoom && b.rotation === r.rotation && b.dpr === dpr &&
            !!layer && b.w === layer.width && b.h === layer.height;
    },

    /** Start a build of the whole layer for the view as it is now. */
    start: function(r, dpr, margin, view) {
        var layer = r._treeLayer;
        var spare = LayerScroll._ensureSpare(layer);
        if (!spare) { this._build = null; return false; }
        var g = LayerScroll._spareCtx;
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.globalAlpha = 1;
        g.globalCompositeOperation = 'source-over';
        g.clearRect(0, 0, spare.width, spare.height);
        this._build = {
            panX: r.panX, panY: r.panY, zoom: r.zoom, rotation: r.rotation, dpr: dpr, margin: margin,
            w: layer.width, h: layer.height,
            view: { cx: view.cx, cy: view.cy, rotRad: view.rotRad, cos: view.cos, sin: view.sin },
            tiles: LayerScroll.pieces([[0, 0, layer.width, layer.height]], this.TILE_PX, layer.width / 2, layer.height / 2),
            spent: 0,
            pieceMs: 0             // what a piece has cost so far, on average
        };
        return true;
    },

    /**
     * Draw what fits in this frame. all: everything left, now. Returns true when
     * the picture is done and swapped in (the renderer's layer changed).
     * @param {Object} r - CanvasRenderer
     * @param {number} frameStart - performance.now() at the start of the frame
     * @param {boolean} [all]
     */
    step: function(r, frameStart, all) {
        var b = this._build;
        if (!b) return false;
        var self = this, g = LayerScroll._spareCtx;
        var now = function() { return (typeof performance !== 'undefined') ? performance.now() : Date.now(); };
        var t0 = now();
        var budget = Math.max(this.MIN_PIECE_MS, this.TARGET_FRAME_MS - (t0 - frameStart));
        var livePanX = r.panX, livePanY = r.panY;
        r.panX = b.panX; r.panY = b.panY;                   // drawn at the build's pan
        var done = false;
        var draw = function() {
            do {
                if (b.tiles.length) {
                    var p0 = now();
                    LayerScroll._drawStrip(r, g, b.tiles.shift(), b.dpr, b.margin, b.view, true);
                    var ms = now() - p0;
                    b.pieceMs = b.pieceMs ? b.pieceMs * 0.7 + ms * 0.3 : ms;
                } else {
                    self._finish(r, g, b);
                    done = true;
                }
                // Another piece only if one more of the usual cost still fits
            } while (!done && (all || now() - t0 + b.pieceMs < budget));
        };
        try {
            if (typeof HoverOverlay !== 'undefined') HoverOverlay.withoutHover(r, draw);
            else draw();
        } finally {
            r.panX = livePanX; r.panY = livePanY;
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

    _swapIn: function(r, b) {
        var old = r._treeLayer, oldCtx = r._treeLayerCtx;
        r._treeLayer = LayerScroll._spare;
        r._treeLayerCtx = LayerScroll._spareCtx;
        LayerScroll._spare = old;
        LayerScroll._spareCtx = oldCtx;
        LayerScroll.reset();
        r._layerPanX = b.panX;
        r._layerPanY = b.panY;
        r._layerZoom = b.zoom;
        r._layerRotation = b.rotation;
        r._treeLayerStale = false;
        r._treeLayerDraws = (r._treeLayerDraws || 0) + 1;
        r._layerBuilds = (r._layerBuilds || 0) + 1;
        this._lastMs = b.spent;
        this._build = null;
    }
};

window.LayerBuild = LayerBuild;
