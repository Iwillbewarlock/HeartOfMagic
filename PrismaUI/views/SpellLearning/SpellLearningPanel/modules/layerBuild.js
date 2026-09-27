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
 * A repaint that is quick anyway (the last whole one took SYNC_MAX_MS or less,
 * e.g. with undiscovered spells hidden) is still done at once. So is the first
 * one, one with no old picture to show (stale layer, resize) and one in edit
 * mode; and when the view, held still, is past the old picture's margin (a jump
 * without a glide, a drag during the build) the rest is finished in that frame. A build is dropped when the view it was for is gone (zoom,
 * rotation, glide target, pixel ratio, layer size), and the tree is marked for
 * a repaint again so the change it carried is not lost.
 *
 * Depends on: CanvasRenderer (the layer, _renderTreeInto, renderLabels),
 * LayerScroll (the spare canvas, pieces, the piece drawing), TreeStyle,
 * HoverOverlay (optional)
 */

var LayerBuild = {

    ENABLED: true,
    SYNC_MAX_MS: 8,           // a whole repaint this quick is still done in one frame
    TARGET_FRAME_MS: 6,       // pieces fill a frame up to this, from its start (the paste, the hub and
                              // the moving parts come after the tree, so it leaves them room)
    TILE_PX: 384,             // device px: the layer is drawn in pieces this big (each costs ~1 ms of its own)

    _build: null,             // { panX, panY, zoom, rotation, dpr, margin, w, h, view, tiles, spent, glide }
    _lastMs: 0,               // what the last whole repaint cost (sync or all pieces), ms
    _pieceMs: 3,              // a piece's running cost, ms
    _finishMs: 4,             // the names and chapter titles' running cost, ms

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
            typeof LayerScroll !== 'undefined' && !(typeof EditMode !== 'undefined' && EditMode.isActive);
    },

    /** A whole repaint was just done at once; it took `ms`. */
    noteSync: function(ms) {
        this._lastMs = ms;
        this._build = null;
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
     * rotation, panX, panY) or else the view as it is now.
     */
    start: function(r, dpr, margin, view, target) {
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
            spent: 0
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
        var drew = false, done = false;
        var left = function() { return self.TARGET_FRAME_MS - (now() - frameStart); };
        var draw = function() {
            while (!done) {
                if (b.tiles.length) {
                    if (!all && drew && left() < self._pieceMs) break;
                    var p0 = now();
                    LayerScroll._drawStrip(r, g, b.tiles.shift(), b.dpr, b.margin, b.view, true);
                    self._pieceMs = self._pieceMs * 0.7 + (now() - p0) * 0.3;
                } else {
                    // The names and titles: in this frame only if they fit (or nothing was drawn yet)
                    if (!all && drew && left() < self._finishMs) break;
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
        // The pieces' own costs (culling loops, dividers) make this a little more
        // than one repaint at once would take: it only decides sync or spread
        this._lastMs = b.spent;
        this._build = null;
    }
};

window.LayerBuild = LayerBuild;
