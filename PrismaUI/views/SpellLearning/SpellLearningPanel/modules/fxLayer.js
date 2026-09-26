/**
 * FxLayer - small canvases over the tree for what moves every frame.
 *
 * The game's browser paints the page on the CPU and hands the whole picture to
 * the game each time something on it changes, so what an animation frame costs
 * is decided by how much of the page it changes, not by our drawing. With the
 * heart, the globe, the selection sigil, the learning glow and the travelling
 * particles drawn into the tree canvas, every frame of them changed the whole
 * canvas - most of the panel - ten times a second, and clicks waited behind it
 * (about 110 ms a loop turn for 2 ms of drawing, SpellLearning.log).
 *
 * Each of those now gets a spot: a canvas no bigger than what it draws, laid
 * over the tree canvas at the same place. The tree canvas is left alone on an
 * animation frame (CanvasRenderer.render keeps what it shows), so a frame
 * changes a few small boxes instead.
 *
 * A spot is drawn in the tree canvas's own coordinates (CSS px from its top
 * left): draw() offsets the spot's context, so the drawing code is the same
 * whichever canvas it lands on. Spots are reused by key; one not drawn in a
 * frame is hidden at end().
 *
 * Usage (every frame):
 *   FxLayer.begin(mainCanvas);
 *   FxLayer.draw('hub', x, y, w, h, dpr, function(ctx) { ... });
 *   FxLayer.end();
 *
 * opts (optional, last argument of draw): version - a spot drawn with the same
 * version at the same place is kept as it is, not drawn again (the hover
 * preview, which only changes with the hover or the view); under - the spot
 * lies under all the others (right on the tree canvas).
 *
 * Depends on: nothing
 */

var FxLayer = {

    SIZE_STEP: 32,         // spot sizes are rounded up to this (CSS px), so a moving box rarely reallocates
    MAX_SPOTS: 16,         // spots kept; a new key takes over one not drawn this frame

    _spots: {},            // key -> { canvas, ctx, w, h, x, y, shown }
    _count: 0,
    _used: null,
    _main: null,
    _failed: false,
    _lastMade: null,

    /** Can spots be used over this canvas (it is in the page)? */
    ready: function(main) {
        return !this._failed && !!(main && main.parentNode);
    },

    begin: function(main) {
        this._main = main;
        this._used = {};
        this.lastDrawn = false;
    },

    /** Did the last draw() run its drawFn (false: the box was off the canvas)? */
    lastDrawn: false,

    /**
     * Draw one spot. x, y, w, h: the box in the main canvas's CSS px; the part
     * outside the main canvas is not drawn.
     * @param {function(CanvasRenderingContext2D)} drawFn - draws in main-canvas CSS px
     * @returns {boolean} false when the spot could not be drawn (the caller draws on the main canvas)
     */
    draw: function(key, x, y, w, h, dpr, drawFn, opts) {
        var under = !!(opts && opts.under);
        var version = opts ? opts.version : undefined;
        var main = this._main;
        this.lastDrawn = false;
        if (!main || this._failed) return false;
        var mw = main.width / dpr, mh = main.height / dpr;
        var x0 = Math.max(0, Math.floor(x)), y0 = Math.max(0, Math.floor(y));
        var x1 = Math.min(mw, Math.ceil(x + w)), y1 = Math.min(mh, Math.ceil(y + h));
        if (x1 <= x0 || y1 <= y0) return true;          // off screen: nothing to draw, not a failure

        var spot = this._spots[key];
        if (!spot) {
            if (this._count >= this.MAX_SPOTS && !under) {
                // Keys come and go (a spot per selected or learning spell):
                // take over one that is not drawn this frame
                spot = this._reclaim();
                if (!spot) return false;
                spot.version = undefined;
            } else {
                spot = this._make(main, under);
                if (!spot) return false;
                this._count++;
            }
            this._spots[key] = spot;
        }
        this._used[key] = true;

        var step = this.SIZE_STEP;
        var sw = Math.ceil((x1 - x0) / step) * step, sh = Math.ceil((y1 - y0) / step) * step;
        if (spot.w !== sw || spot.h !== sh || spot.dpr !== dpr) {
            spot.canvas.width = Math.round(sw * dpr);
            spot.canvas.height = Math.round(sh * dpr);
            spot.canvas.style.width = sw + 'px';
            spot.canvas.style.height = sh + 'px';
            spot.w = sw; spot.h = sh; spot.dpr = dpr;
        }
        var left = main.offsetLeft + x0, top = main.offsetTop + y0;
        if (spot.x !== left) { spot.canvas.style.left = left + 'px'; spot.x = left; }
        if (spot.y !== top) { spot.canvas.style.top = top + 'px'; spot.y = top; }
        if (!spot.shown) { spot.canvas.style.display = 'block'; spot.shown = true; spot.version = undefined; }
        // Same picture at the same place: leave it
        if (version !== undefined && spot.version === version && spot.drawnAt === left + ',' + top + ',' + sw + ',' + sh + ',' + dpr) {
            this.lastDrawn = true;
            return true;
        }
        spot.version = version;
        spot.drawnAt = left + ',' + top + ',' + sw + ',' + sh + ',' + dpr;

        var ctx = spot.ctx;
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.globalAlpha = 1;
        ctx.globalCompositeOperation = 'source-over';
        ctx.clearRect(0, 0, spot.canvas.width, spot.canvas.height);
        ctx.setTransform(dpr, 0, 0, dpr, -x0 * dpr, -y0 * dpr);
        this.lastDrawn = true;
        try {
            drawFn(ctx);
        } finally {
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.globalAlpha = 1;
        }
        return true;
    },

    /** Hide every spot not drawn since begin(). */
    end: function() {
        for (var key in this._spots) {
            if (!this._spots.hasOwnProperty(key)) continue;
            if (!this._used || !this._used[key]) this._hide(this._spots[key]);
        }
    },

    /** Hide all spots (spots switched off, panel hidden, tree tab left). */
    hideAll: function() {
        for (var key in this._spots) {
            if (this._spots.hasOwnProperty(key)) this._hide(this._spots[key]);
        }
    },

    /** A spot not drawn this frame, taken from its key; null when every spot is in use. */
    _reclaim: function() {
        for (var key in this._spots) {
            if (!this._spots.hasOwnProperty(key) || (this._used && this._used[key])) continue;
            var spot = this._spots[key];
            if (spot.under) continue;          // stays where it lies, for its own key
            delete this._spots[key];
            return spot;
        }
        return null;
    },

    /**
     * Put the spots right after the main canvas again, in the order they were
     * made: the canvas was taken out and put back (CanvasRenderer.show), which
     * put it after them - and over them.
     */
    reattach: function(main) {
        if (!main || !main.parentNode) return;
        var after = main, key, pass;
        // The ones that lie under the others first
        for (pass = 0; pass < 2; pass++) {
            for (key in this._spots) {
                if (!this._spots.hasOwnProperty(key) || !!this._spots[key].under !== (pass === 0)) continue;
                var c = this._spots[key].canvas;
                main.parentNode.insertBefore(c, after.nextSibling);
                after = c;
            }
        }
        this._lastMade = after === main ? null : after;
    },

    _hide: function(spot) {
        if (spot.shown) {
            spot.canvas.style.display = 'none';
            spot.shown = false;
        }
    },

    _make: function(main, under) {
        try {
            var c = document.createElement('canvas');
            c.className = 'tree-fx-spot';
            // Over the tree canvas and under everything placed after it
            // (zoom bar, tabs, tooltip); never takes the mouse
            c.style.cssText = 'position: absolute; left: 0; top: 0; display: none; pointer-events: none; z-index: 1;';
            var ctx = c.getContext('2d');
            if (!ctx) throw new Error('no 2d context');
            // After the spots made before it: the first frame makes them in drawing
            // order. One that lies under the others goes right after the canvas
            if (under) {
                main.parentNode.insertBefore(c, main.nextSibling);
            } else {
                var after = this._lastMade && this._lastMade.parentNode === main.parentNode ? this._lastMade : main;
                main.parentNode.insertBefore(c, after.nextSibling);
                this._lastMade = c;
            }
            return { canvas: c, ctx: ctx, w: 0, h: 0, dpr: 0, x: null, y: null, shown: false, under: !!under };
        } catch (e) {
            this._failed = true;
            console.warn('[FxLayer] spots unavailable, drawing on the tree canvas: ' + (e && e.message ? e.message : e));
            return null;
        }
    }
};

window.FxLayer = FxLayer;
