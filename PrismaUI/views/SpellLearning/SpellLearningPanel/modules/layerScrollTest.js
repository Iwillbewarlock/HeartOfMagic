/**
 * LayerScrollTest - node tests for LayerScroll's arithmetic (run-tests.js):
 * the strips a shift uncovers (each axis, both, none), that the pieces cover
 * them exactly and come nearest first, and that kept names move with the
 * picture and are dropped once wholly outside the layer. And LayerBuild's
 * states: when it is used, a frame's piece, the next frame asked for, the swap.
 *
 * Depends on: LayerScroll, LayerBuild
 */

var LayerScrollTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** Sum of the areas, and whether any two overlap. */
    _area: function(rects) {
        var a = 0, overlap = false;
        for (var i = 0; i < rects.length; i++) {
            a += rects[i][2] * rects[i][3];
            for (var j = i + 1; j < rects.length; j++) {
                var p = rects[i], q = rects[j];
                if (p[0] < q[0] + q[2] && q[0] < p[0] + p[2] && p[1] < q[1] + q[3] && q[1] < p[1] + p[3]) overlap = true;
            }
        }
        return { area: a, overlap: overlap };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var S = g.LayerScroll || (typeof window !== 'undefined' && window.LayerScroll);
        if (!S) { this.check(false, 'LayerScroll loaded'); return { passed: this.passed, failed: this.failed }; }
        var W = 100, H = 80;

        var r = S.exposedRects(W, H, 10, 0);
        this.check(r.length === 1 && r[0].join() === '0,0,10,80', 'moved right: the left strip');
        r = S.exposedRects(W, H, -10, 0);
        this.check(r.length === 1 && r[0].join() === '90,0,10,80', 'moved left: the right strip');
        r = S.exposedRects(W, H, 0, 6);
        this.check(r.length === 1 && r[0].join() === '0,0,100,6', 'moved down: the top strip');
        r = S.exposedRects(W, H, 0, -6);
        this.check(r.length === 1 && r[0].join() === '0,74,100,6', 'moved up: the bottom strip');
        this.check(S.exposedRects(W, H, 0, 0).length === 0, 'not moved: nothing');

        r = S.exposedRects(W, H, -10, 6);
        var a = this._area(r);
        // uncovered: everything but the (90 x 74) the old picture still covers
        this.check(r.length === 2 && !a.overlap && a.area === W * H - 90 * 74, 'diagonal: two strips, no overlap, exact area');

        var pieces = S.pieces(S.exposedRects(1652, 1264, -120, 90), 480, 826, 632);
        var pa = this._area(pieces);
        this.check(!pa.overlap && pa.area === 1652 * 1264 - 1532 * 1174, 'pieces cover the strips exactly');
        var ok = true;
        for (var i = 0; i < pieces.length; i++) if (pieces[i][2] > 480 || pieces[i][3] > 480) ok = false;
        this.check(ok, 'no piece longer than the limit');
        var d = function(p) { var ex = p[0] + p[2] / 2 - 826, ey = p[1] + p[3] / 2 - 632; return ex * ex + ey * ey; };
        var sorted = true;
        for (i = 1; i < pieces.length; i++) if (d(pieces[i]) < d(pieces[i - 1])) sorted = false;
        this.check(sorted, 'pieces nearest the middle first');

        var moved = S.moveRects([[0, 0, 10, 80], [90, 0, 10, 80]], -15, 0, W, H);
        this.check(moved.length === 1 && moved[0].join() === '75,0,10,80', 'waiting strips move with the picture; one pushed off is dropped');
        moved = S.moveRects([[0, 0, 10, 80]], -5, 0, W, H);
        this.check(moved.length === 1 && moved[0].join() === '0,0,5,80', 'a strip half pushed off is cut to the layer');
        this.check(S.overlaps([0, 0, 10, 10], [9, 9, 5, 5]) && !S.overlaps([0, 0, 10, 10], [10, 0, 5, 5]), 'overlap test (touching is not overlapping)');

        var labels = [
            { x: 10, y: 10, l: 0, r: 20, t: 8, b: 20 },
            { x: 95, y: 10, l: 85, r: 105, t: 8, b: 20 }
        ];
        S.shiftLabels(labels, -30, 5, { l: 0, r: 100, t: 0, b: 80 });
        this.check(labels.length === 1 && labels[0].x === 65 && labels[0].l === 55 && labels[0].t === 13,
            'kept names move with the picture; one wholly outside is dropped');

        this._buildTests(g, S);
        return { passed: this.passed, failed: this.failed };
    },

    /** LayerBuild's states with a stand-in renderer and canvases (no drawing). */
    _buildTests: function(g, S) {
        var B = g.LayerBuild || (typeof window !== 'undefined' && window.LayerBuild);
        if (!B) { this.check(false, 'LayerBuild loaded'); return; }
        var saved = { ensure: S._ensureSpare, strip: S._drawStrip, spare: S._spare, ctx: S._spareCtx, ts: g.TreeStyle };
        var noop = function() {};
        var fakeCtx = { setTransform: noop, clearRect: noop, save: noop, restore: noop, scale: noop, translate: noop };
        var pieces = 0;
        S._ensureSpare = function() { S._spare = { width: 1000, height: 800, id: 'spare' }; S._spareCtx = fakeCtx; return S._spare; };
        // A piece takes 2 ms, as a real one would take some
        S._drawStrip = function() { pieces++; var until = Date.now() + 2; while (Date.now() < until) { /* busy */ } };
        g.TreeStyle = { renderChapters: noop };
        var r = { panX: 5, panY: 6, zoom: 1.2, rotation: 0, _treeLayer: { width: 1000, height: 800, id: 'old' },
                  _treeLayerCtx: fakeCtx, renderLabels: noop };
        var view = { cx: 400, cy: 300, rotRad: 0, cos: 1, sin: 0 };
        try {
            B._lastMs = 5;
            this.check(!B.wanted(r, r._treeLayer), 'a quick whole repaint stays in one frame');
            B._lastMs = 30;
            this.check(B.wanted(r, r._treeLayer), 'a slow one is spread over frames');

            this.check(B.start(r, 1, 128, view) && B.active(), 'a build starts');
            var total = B._build.tiles.length;
            this.check(total > 1, 'the layer is cut into pieces');
            // A frame with no time left gets one piece and asks for the next frame
            r.__needsRender = false;
            var done = B.step(r, -1000, false);
            this.check(!done && pieces === 1 && r.__needsRender === true, 'no time left: one piece, next frame asked for');
            this.check(B.valid(r, 1, r._treeLayer), 'still valid for the same view');
            r.zoom = 1.3;
            this.check(!B.valid(r, 1, r._treeLayer), 'a new zoom makes it stale');
            r.zoom = 1.2;
            done = B.step(r, 0, true);
            this.check(done && !B.active(), 'all at once finishes it');
            this.check(r._treeLayer.id === 'spare' && S._spare.id === 'old', 'the new picture swaps in, the old one becomes the spare');
            this.check(r._layerZoom === 1.2 && r._layerPanX === 5 && r._layerPanY === 6, 'the layer takes the view it was built for');

            // Dropped halfway: the change it carried is marked again
            r._treeDirty = false;
            B.start(r, 1, 128, view);
            B.abort(r);
            this.check(r._treeDirty === true && !B.active(), 'an aborted build marks the tree for a repaint again');

            // Built for a glide's end: valid while the camera is on the way, and after it arrives
            var end = { zoom: 2, rotation: 30, panX: 40, panY: -20 };
            r._glideTarget = end;
            B.start(r, 1, 128, view, end);
            this.check(B.forGlide() && B.valid(r, 1, r._treeLayer), 'a glide build is valid while the camera glides');
            r._glideTarget = null; r.zoom = 2; r.rotation = 30;
            this.check(B.valid(r, 1, r._treeLayer), 'and once it has arrived');
            r.zoom = 1.5;
            this.check(!B.valid(r, 1, r._treeLayer), 'but not if the camera stopped elsewhere');
        } finally {
            S._ensureSpare = saved.ensure; S._drawStrip = saved.strip; S._spare = saved.spare; S._spareCtx = saved.ctx;
            g.TreeStyle = saved.ts;
            B.abort(); B._lastMs = 0;
        }
    }
};

if (typeof window !== 'undefined') window.LayerScrollTest = LayerScrollTest;
