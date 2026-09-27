/**
 * LayerFlowTest - node tests for how a frame gets its tree layer (run-tests.js),
 * with stand-in canvases (no drawing):
 * - LayerScroll: a shift swaps the canvases, moves the layer's pan by whole
 *   device pixels, clears the spare before copying, queues the uncovered strips;
 *   _drawSome draws the pieces on screen first whatever the time, the others in
 *   the time left, and asks for the next frame while some wait.
 * - LayerBuild.step for real (stand-in pieces): urgent, the pieces on screen
 *   first, one a frame when there is no time, the swap once they and the names
 *   are done, the rest handed to LayerScroll with the next frame asked for, and
 *   _lastMs left as it was by a build cut short; what finished builds do to
 *   _lastMs (a middling one leaves it, however many come; the first after a
 *   design change is not counted).
 * - CanvasRenderer._drawTree's order: a drag scrolls, a change is built over
 *   frames when that is wanted and drawn at once when not, a stale build is
 *   dropped (its change marked again, the restart count started over), a glide
 *   builds for its end and starts again on a change, too many restarts draw at
 *   once, a build seen past the old picture's margin goes on urgently, and
 *   pieces left by an urgent swap are drawn on a stretched frame at the layer's
 *   own zoom, but wait beside a new build or with the tree changed.
 *
 * Depends on: LayerScroll, LayerBuild, CanvasRenderer (canvasRendererV2.js,
 * canvasRendererFrame.js)
 */

var LayerFlowTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** A 2d context that draws nothing and remembers the calls it got. */
    _ctx: function(log) {
        var c = { calls: log || [] };
        ['setTransform', 'clearRect', 'save', 'restore', 'scale', 'translate', 'rotate', 'beginPath', 'rect', 'clip',
         'getImageData'].forEach(function(n) {
            c[n] = function() { c.calls.push(n + '(' + Array.prototype.slice.call(arguments).join(',') + ')'); };
        });
        c.drawImage = function(img, x, y) { c.calls.push('drawImage(' + (img && img.id) + ',' + x + ',' + y + ')'); };
        var op = 'source-over';
        Object.defineProperty(c, 'globalCompositeOperation', {
            get: function() { return op; },
            set: function(v) { op = v; c.calls.push('composite=' + v); }
        });
        return c;
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var S = g.LayerScroll, B = g.LayerBuild, CR = g.CanvasRenderer;
        if (!S || !B || !CR || typeof CR._drawTree !== 'function') {
            this.check(false, 'LayerScroll, LayerBuild and CanvasRenderer loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var saved = { ensure: S._ensureSpare, strip: S._drawStrip, step: S.step, spare: S._spare, sctx: S._spareCtx,
                      pending: S._pending, target: S.TARGET_FRAME_MS, ts: g.TreeStyle, bstart: B.start, babort: B.abort,
                      bstep: B.step, perf: g.performance };
        try {
            this._scrollTests(g, S);
            this._stepTests(g, S, B);
            this._drawTreeTests(g, S, B, CR);
        } finally {
            S._ensureSpare = saved.ensure; S._drawStrip = saved.strip; S.step = saved.step; S._spare = saved.spare;
            S._spareCtx = saved.sctx; S._pending = saved.pending; S.TARGET_FRAME_MS = saved.target;
            g.TreeStyle = saved.ts; B.start = saved.bstart; B.abort = saved.babort; B.step = saved.bstep;
            B._build = null; B._lastMs = 0; B._restarts = 0;
        }
        return { passed: this.passed, failed: this.failed };
    },

    _scrollTests: function(g, S) {
        var self = this, log = [];
        var spare = { id: 'spare', width: 1000, height: 800 }, spareCtx = this._ctx(log);
        S._spare = spare; S._spareCtx = spareCtx;
        S._ensureSpare = function() { return S._spare; };
        var r = { panX: 70.3, panY: 0, _layerPanX: 0, _layerPanY: 0, zoom: 1, rotation: 0,
                  _treeLayer: { id: 'layer', width: 1000, height: 800 }, _treeLayerCtx: this._ctx(),
                  _layerLabels: [], canvas: { width: 700, height: 500 } };
        S._pending = [];
        var ok = S._shift(r, 1.5, 128);
        // 70.3 css px at 1.5 = 105.45 device px: 105 whole ones, 70 css px
        this.check(ok && r._treeLayer === spare && S._spare.id === 'layer', 'a shift swaps the layer and the spare');
        this.check(Math.abs(r._layerPanX - 70) < 1e-9 && r._layerPanY === 0, "the layer's pan moves by whole device pixels");
        var clearAt = log.indexOf('clearRect(0,0,1000,800)'), drawAt = log.indexOf('drawImage(layer,105,0)');
        this.check(clearAt >= 0 && drawAt > clearAt && log.join().indexOf('copy') < 0,
            'the spare is cleared, then the picture drawn moved (no copy compositing)');
        var area = 0;
        S._pending.forEach(function(p) { area += p[2] * p[3]; });
        this.check(S._pending.length > 0 && area === 105 * 800, 'the uncovered strip waits to be drawn, all of it');

        // _drawSome: on screen first whatever the time, the rest in the time left
        var drawn = [];
        S._drawStrip = function(rr, gg, piece) { drawn.push(piece.join()); };
        g.TreeStyle = { beginLabels: function() {}, drawLabel: function() {}, tokens: {}, renderChapters: function() {} };
        r._layerPanX = 0; r.panX = 0;
        var onScreen = [300, 300, 100, 100], off1 = [0, 0, 50, 50], off2 = [950, 750, 50, 50];
        S._pending = [off1, onScreen, off2];
        S.TARGET_FRAME_MS = -1000;                 // no time left in this frame
        r.__needsRender = false; r._frameStartAt = 0;
        S._drawSome(r, 1, 128, { cx: 350, cy: 250, cos: 1, sin: 0 });
        this.check(drawn.length === 1 && drawn[0] === onScreen.join(), 'no time left: only the piece on screen is drawn');
        this.check(S._pending.length === 2 && r.__needsRender === true, 'the others wait and the next frame is asked for');
        this.check(r.panX === 0, "the live pan is put back after drawing at the layer's pan");
        drawn = [];
        S._pending = [off1, off2];
        S._drawSome(r, 1, 128, { cx: 350, cy: 250, cos: 1, sin: 0 });
        this.check(drawn.length === 1, 'none on screen and no time: one piece all the same (progress every frame)');
        drawn = [];
        S.TARGET_FRAME_MS = 1e9;
        r.__needsRender = false;
        S._pending = [off1, off2];
        S._drawSome(r, 1, 128, { cx: 350, cy: 250, cos: 1, sin: 0 });
        this.check(drawn.length === 2 && S._pending.length === 0 && r.__needsRender === false, 'time enough: all drawn, no frame asked for');
    },

    /** LayerBuild.step itself (not stubbed): an urgent build and its early swap. */
    _stepTests: function(g, S, B) {
        var drawn = [];
        var spare = { id: 'spare', width: 1056, height: 856 }, spareCtx = this._ctx();
        S._spare = spare; S._spareCtx = spareCtx;
        S._ensureSpare = function() { return S._spare; };
        S._pending = [];
        S._drawStrip = function(rr, gg, piece, dpr, margin, view, whole) {
            drawn.push({ piece: piece, ctx: gg, whole: whole, zoom: rr.zoom });
        };
        g.TreeStyle = { renderChapters: function() {} };
        var r = { panX: 0, panY: 0, zoom: 1, rotation: 0, _layerPanX: 0, _layerPanY: 0, _layerZoom: 1, _layerRotation: 0,
                  canvas: { width: 300, height: 200 }, _treeLayer: { id: 'layer', width: 1056, height: 856 },
                  _treeLayerCtx: this._ctx(), renderLabels: function() {}, __needsRender: false, _frameStartAt: -1e9 };
        var view = { cx: 150, cy: 100, rotRad: 0, cos: 1, sin: 0 };
        B._build = null; B._restarts = 0; B._lastMs = 50;
        B.start(r, 1, 128, view);
        var tiles = B._build.tiles.length;
        r.panX = 200;                                // held past the margin: urgent
        var vr = B._screenRect(r, B._build);
        var shownCount = B._build.tiles.filter(function(t) { return S.overlaps(t, vr); }).length;
        var steps = [], guard = 0, swapped = false;
        while (!swapped && guard++ < 50) { r.__needsRender = false; swapped = B.step(r, -1e9, true); steps.push(swapped); }
        var firstOnScreen = drawn.slice(0, shownCount).every(function(d) { return S.overlaps(d.piece, vr); });
        this.check(shownCount > 0 && shownCount < tiles && firstOnScreen &&
            drawn.every(function(d) { return d.ctx === spareCtx && d.whole; }),
            'urgent build: the pieces on screen first, onto the spare, as whole-layer pieces');
        this.check(steps.length === shownCount + 1 && drawn.length === shownCount,
            'no time left: one piece a frame, the swap as soon as the on-screen ones and the names are done');
        this.check(swapped && r._treeLayer === spare && S._spare.id === 'layer' && r._layerPanX === 0,
            'the early swap: the built picture is the layer, for the view it was built for');
        this.check(S._pending.length === tiles - shownCount, "the pieces left over go to LayerScroll's queue");
        this.check(r.__needsRender === true && r._animationOnlyRender === false,
            'after the early swap the next frame is asked for, unthrottled');
        this.check(B._lastMs === 50 && !B.active() && B._restarts === 0, 'a build cut short leaves _lastMs as it was');
        S._pending = [];

        // What finished builds do to _lastMs
        var built = function(spent) { return { tiles: [], spent: spent, pieces: 1, minPiece: 0, panX: 0, panY: 0, zoom: 1, rotation: 0 }; };
        var rr = { _treeLayer: { id: 'a' }, _treeLayerCtx: this._ctx() };
        B._lastMs = 30;
        for (var i = 0; i < 50; i++) B._swapIn(rr, built(6));
        this.check(B._lastMs === 30,
            'builds that stay middling leave a slow figure: no repaint at once (a long frame) every so many builds');
        B._swapIn(rr, built(40));
        this.check(B._lastMs === 40, 'a slower build raises it');
        // The repaint after a design or language change makes its sprites: not counted
        B._lastMs = 3; B.noteRestyle(); B.noteSync(26);
        this.check(B._lastMs === 3 && !B._unmeasured, 'after a design change the first repaint at once is not counted');
        B.noteSync(5);
        this.check(B._lastMs === 5, '...the one after it is');
        B._lastMs = 30; B.noteRestyle(); B._swapIn(rr, built(60));
        this.check(B._lastMs === 30 && !B._unmeasured, 'nor is a first build after it');
        var early = built(60); early.tiles = [[0, 0, 10, 10]];
        B.noteRestyle(); B._swapIn(rr, early); S._pending = [];
        this.check(B._lastMs === 30 && !B._unmeasured, '...nor one swapped in early, which still made the caches');
        // The session's first repaint follows the design applied at start-up: kept as a first figure
        B._lastMs = 0; B.noteRestyle(); B.noteSync(40);
        this.check(B._lastMs === 40 && B._provisional && !B._unmeasured,
            "the session's first repaint after the start-up design: its cost kept, so a slow tree's first click is spread");
        B._swapIn(rr, built(6));
        this.check(B._lastMs === 6 && !B._provisional, '...and the next whole repaint replaces it, a middling build included');
        B._lastMs = 0;
    },

    _drawTreeTests: function(g, S, B, CR) {
        var self = this, calls = [];
        var spare = { id: 'spare', width: 1056, height: 856 };
        S._spare = spare; S._spareCtx = this._ctx();
        S._ensureSpare = function() { return S._spare; };
        S._drawStrip = function() { calls.push('piece'); };
        S._pending = [];
        g.TreeStyle = { renderChapters: function() {} };
        var scrollResult = false;
        S.step = function() { calls.push('scroll'); return scrollResult; };
        var bstart = B.start, babort = B.abort, bstep = B.step, lastUrgent = null;
        B.start = function(r, dpr, margin, view, target) { calls.push(target ? 'glideBuild' : 'build'); return bstart.apply(B, arguments); };
        B.abort = function(r) { calls.push('abort'); return babort.apply(B, arguments); };
        B.step = function(r, fs, urgent) { calls.push('step'); lastUrgent = !!urgent; return false; };   // never done here

        var r = Object.create(CR);
        r.canvas = { width: 800, height: 600 }; r.ctx = this._ctx(); r._width = 800; r._height = 600;
        r.zoom = 1; r.rotation = 0; r.panX = 0; r.panY = 0;
        r._layerZoom = 1; r._layerRotation = 0; r._layerPanX = 0; r._layerPanY = 0;
        r._treeLayer = { id: 'layer', width: 1056, height: 856 }; r._treeLayerCtx = this._ctx(); r._treeLayerStale = false;
        r._ensureTreeLayer = function() { return this._treeLayer; };
        r._renderTreeInto = function() { calls.push('sync'); };
        r._drawMoving = function() {};
        r.renderLabels = function() {};
        r.isAnimating = false; r._wheelAt = -1e9; r._glideTarget = null; r._fxThisFrame = false;
        r.__needsRender = false; r._treeDirty = false; r._frameStartAt = 0; r._layerLabels = [];
        var view = { cx: 400, cy: 300, rotRad: 0, cos: 1, sin: 0, viewLeft: -900, viewRight: 900, viewTop: -800, viewBottom: 800 };
        var frame = function() { calls = []; r._drawTree(r.ctx, 1, view, false); return calls.join(' '); };

        B._build = null; B._restarts = 0;
        r.panX = 100; scrollResult = true;
        this.check(frame() === 'scroll', 'a drag with nothing changed: the layer scrolls');
        r.panX = 0; r._layerPanX = 0; scrollResult = false;

        B._lastMs = 0; r._treeDirty = true;
        this.check(frame() === 'sync' && r._treeDirty === false, 'a change, repaints quick: drawn at once');

        B._lastMs = 50; r._treeDirty = true;
        var f = frame();
        this.check(f === 'build step' && B.active() && r._treeDirty === false, 'a change, repaints slow: built over frames');
        this.check(frame() === 'step', 'the next frame goes on with it');

        r.zoom = 1.2; r._layerZoom = 1;              // the view it was for is gone (not in motion)
        B._restarts = 2;
        f = frame();
        this.check(f.indexOf('abort') === 0 && f.indexOf('build') > 0 && B.active() && B._build.zoom === 1.2,
            'a stale build is dropped, its change built again for the new view');
        this.check(B._restarts === 0, 'a build dropped for its view is not a restart: the count starts over');
        r._layerZoom = 1.2;

        B._build = null; B._restarts = 0;
        r.isAnimating = true; r._treeDirty = true;
        r._glideTarget = { zoom: 1.5, rotation: 0, panX: 40, panY: -20 };
        f = frame();
        this.check(f.indexOf('glideBuild') === 0 && B.forGlide(), 'a change during a glide: built for where the glide ends');
        r._treeDirty = true;
        f = frame();
        this.check(f.indexOf('glideBuild') === 0 && B._restarts === 1, 'another change during the glide: built again (a restart)');
        r.isAnimating = false; r._glideTarget = null;

        B._build = null; B._restarts = B.MAX_RESTARTS; r._treeDirty = true; r.zoom = 1.2; r._layerZoom = 1.2;
        this.check(!B.wanted(r, r._treeLayer), 'too many restarts: not spread any more');
        bstart.call(B, r, 1, 128, view);          // a build in hand
        r._treeDirty = true;
        f = frame();
        this.check(f === 'abort sync' && !B.active() && B._restarts === 0, 'too many restarts: the build goes, drawn at once, count reset');

        // What a finished build says about repainting at once (_lastMs)
        var built = function(spent, pieces, minPiece) {
            return { tiles: [], spent: spent, pieces: pieces, minPiece: minPiece, panX: 0, panY: 0, zoom: r.zoom, rotation: 0 };
        };
        B._lastMs = 30; B._swapIn(r, built(6, 1, 1));
        this.check(B._lastMs > B.SYNC_MAX_MS, 'a middling build leaves a slow tree spread');
        B._swapIn(r, built(40, 20, 2));            // 40 - 19 * 2 = 2 ms: clearly quick
        this.check(B._lastMs === 2, "a clearly quick build (pieces' overhead counted once) lets the next repaint try at once");
        B._lastMs = 10; B._swapIn(r, built(60, 2, 1));
        this.check(B._lastMs === 59, 'a slow build raises it');
        r._treeLayer = { id: 'layer', width: 1056, height: 856 }; r._layerZoom = r.zoom;

        B._lastMs = 50; B._restarts = 0; r._treeDirty = true;
        frame();                                   // a build starts
        r.panX = 200;                              // held still past the old picture's margin
        f = frame();
        this.check(f === 'step' && lastUrgent === true, 'past the margin while building: the build goes on urgently');

        // Pieces an urgent swap left over: drawn on frames that do not scroll
        S._pending = [[0, 0, 50, 50]];
        f = frame();
        this.check(f === 'step' && S._pending.length === 1, 'not while an urgent build is about to replace the layer');
        r.panX = 0;
        var zoomAt = null;
        S._drawStrip = function(rr, gg) { calls.push(gg === r._treeLayerCtx ? 'piece' : 'piece-elsewhere'); zoomAt = rr.zoom; };
        f = frame();
        this.check(f === 'step' && S._pending.length === 1,
            'beside a new build: they wait (its pieces have the frame, its swap replaces the layer)');
        B._build = null;
        r._treeDirty = true; r._wheelAt = performance.now(); r.zoom = 0.9;   // a click, then a wheel zoom
        f = frame();
        this.check(f === '' && S._pending.length === 1 && r._treeDirty === true,
            'the tree changed: they wait on a stretched frame (they would show it in the old picture)');
        S._pending = [[0, 0, 50, 50]];
        r._treeDirty = false; r._wheelAt = performance.now(); r.zoom = 0.9;   // a wheel zoom straight after the swap
        f = frame();
        this.check(f === 'piece' && S._pending.length === 0 && zoomAt === r._layerZoom && r.zoom === 0.9,
            "a stretched frame draws them, at the layer's zoom (the live one put back)");
        S._pending = [[0, 0, 50, 50], [60, 0, 50, 50]];                    // both on screen, no time left
        var target = S.TARGET_FRAME_MS;
        S.TARGET_FRAME_MS = -1000;
        f = frame();
        S.TARGET_FRAME_MS = target;
        this.check(f === 'piece' && S._pending.length === 1 && r.__needsRender === true,
            'a stretched frame keeps to its time, on-screen pieces too: one, the next frame asked for');
        r._wheelAt = -1e9; r.zoom = r._layerZoom;
        S._pending = [];
        r.panX = 0;
    }
};

if (typeof window !== 'undefined') window.LayerFlowTest = LayerFlowTest;
