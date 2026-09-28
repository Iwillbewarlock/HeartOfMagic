/**
 * LayerScrollTest - node tests for LayerScroll's arithmetic (run-tests.js):
 * the strips a shift uncovers (each axis, both, none), that the pieces cover
 * them exactly and come nearest first, and that kept names move with the
 * picture and are dropped once wholly outside the layer, what the screen shows
 * of a layer pasted stretched or turned (_stretchedViewRect). And LayerBuild's
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

        // What the screen shows of a layer pasted stretched (drawPendingAside):
        // an 800 x 600 view, margin 128, the layer's middle at (528, 428)
        var look = S.LOOKAHEAD_PX;
        S.LOOKAHEAD_PX = 0;
        try {
            var sv = { cx: 400, cy: 300 };
            var sr = { canvas: { width: 800, height: 600 }, zoom: 0.5, _layerZoom: 1, rotation: 0, _layerRotation: 0,
                       panX: 0, panY: 0, _layerPanX: 0, _layerPanY: 0 };
            this.check(S._stretchedViewRect(sr, 1, 128, sv).join() === '-272,-172,1600,1200',
                'zoomed out to half: twice the screen, round the layer middle');
            sr.zoom = 2;
            this.check(S._stretchedViewRect(sr, 1, 128, sv).join() === '328,278,400,300',
                'zoomed in twice: half the screen, round the layer middle');
            sr.zoom = 1; sr.panX = 30;
            this.check(S._stretchedViewRect(sr, 1, 128, sv).join() === S._viewRect(sr, 1, 128).join(),
                'not stretched: the same as a plain paste');
            sr.panX = 0; sr.rotation = 90;
            var turned = S._stretchedViewRect(sr, 1, 128, sv);
            this.check(Math.abs(turned[2] - 600) <= 1 && Math.abs(turned[3] - 800) <= 1 &&
                       Math.abs(turned[0] + turned[2] / 2 - 528) <= 1 && Math.abs(turned[1] + turned[3] / 2 - 428) <= 1,
                'turned a quarter: the screen on its side, round the layer middle');
            sr.panX = 30;                          // turned, the view moved on: the sign of the turn shows
            turned = S._stretchedViewRect(sr, 1, 128, sv);
            this.check(Math.abs(turned[0] + turned[2] / 2 - 528) <= 1 && Math.abs(turned[1] + turned[3] / 2 - 458) <= 1,
                'turned a quarter with the pan moved on: the screen centre mapped back the right way round');
            // Pixel ratio 2, the view moved on since the layer was drawn, zoomed out
            var hd = { canvas: { width: 1600, height: 1200 }, zoom: 0.5, _layerZoom: 1, rotation: 0, _layerRotation: 0,
                       panX: 40, panY: 0, _layerPanX: 10, _layerPanY: 0 };
            this.check(S._stretchedViewRect(hd, 2, 128, sv).join() === '-684,-344,3200,2400',
                'pixel ratio 2 and a pan moved on: device px, the pan unscaled by the zoom');
        } finally {
            S.LOOKAHEAD_PX = look;
        }

        this._titleTests(g, S);
        this._buildTests(g, S);
        return { passed: this.passed, failed: this.failed };
    },

    /**
     * A strip's new names drawn past it, and the spots where one meets a chapter
     * title outside the strip drawn again (tree, kept names, titles over them).
     */
    _titleTests: function(g, S) {
        var noop = function() {};
        var ctx = { save: noop, restore: noop, setTransform: noop, translate: noop, beginPath: noop, rect: noop, clip: noop };
        var saved = { ts: g.TreeStyle, strip: S._drawStrip, found: S._found, titles: S._titles };
        var drawn = [];
        g.TreeStyle = {
            tokens: { labelHaloWidth: 0 },
            beginLabels: noop, drawLabel: function(gg, text) { drawn.push(text); },
            chapterBoxes: function() { return [{ l: 100, r: 140, t: 0, b: 12 }]; }
        };
        try {
            // One strip, css x 0-50 (margin 0, pixel ratio 1); a name placed in it runs to x 60
            var cand = function(id, x) { return { node: { id: id }, x: x, y: 2, priority: 5 }; };
            var r = {
                _layerLabels: [], LABEL_PAD: 0,
                _labelCandidates: function() { return { fontSize: 10, maxLabels: 10, candidates: [cand('a', 30), cand('b', 20)] }; },
                _labelFontFrom: noop,
                _labelRect: function(gg, c) { return c.node.id === 'a' ? { l: 20, r: 60, t: 2, b: 12 } : { l: 5, r: 15, t: 20, b: 30 }; },
                _keepLabel: function(c, box) { return { node: c.node, text: c.node.id, x: c.x, y: c.y, l: box.l, r: box.r, t: box.t, b: box.b, alpha: 1 }; }
            };
            S._found = undefined;
            var spilled = S._labels(r, ctx, [0, 0, 50, 50], 1, 0, { cx: 0, cy: 0, cos: 1, sin: 0 });
            this.check(drawn.join() === 'a,b' && spilled.length === 1 && spilled[0].text === 'a',
                "new names drawn; the one reaching past the strip is returned");
            drawn = [];
            var again = S._labels(r, ctx, [0, 0, 50, 50], 1, 0, { cx: 0, cy: 0, cos: 1, sin: 0 }, null, true);
            this.check(again.length === 0 && drawn.join() === 'a,b' && r._layerLabels.length === 2,
                'keptOnly: the kept names drawn again, none placed, nothing returned');

            var spots = [];
            S._drawStrip = function(rr, gg, rect, dpr, margin, view, whole, viewCss, keptOnly) {
                spots.push({ rect: rect.join(), whole: whole, keptOnly: keptOnly });
            };
            S._found = { fontSize: 10 };
            var view = { cx: 0, cy: 0, cos: 1, sin: 0 };
            // reach = 10 x DESCENT_SHARE = 3.5 past the name's box
            S._titles = undefined;
            S._underTitles(r, ctx, [0, 0, 100, 50], 1, 0, view, null, [{ l: 60, r: 110, t: 2, b: 12 }]);
            this.check(spots.length === 1 && spots[0].rect === '100,0,14,12' && spots[0].whole === false && spots[0].keptOnly === true,
                'a name over a title past the strip: that spot drawn again, kept names only');
            spots = [];
            S._underTitles(r, ctx, [0, 0, 100, 50], 1, 0, view, null, [{ l: 60, r: 110, t: 2, b: 12 }, { l: 80, r: 125, t: 6, b: 16 }]);
            this.check(spots.length === 1 && spots[0].rect === '100,0,29,12',
                'two names over one title: one spot round both (drawn once)');
            spots = [];
            S._underTitles(r, ctx, [0, 0, 200, 50], 1, 0, view, null, [{ l: 60, r: 110, t: 2, b: 12 }]);
            this.check(spots.length === 0, 'the title inside the strip: already drawn over the name, nothing more');
            S._underTitles(r, ctx, [0, 0, 50, 50], 1, 0, view, null, [{ l: 20, r: 60, t: 30, b: 40 }]);
            this.check(spots.length === 0, 'a spilled name away from every title: nothing more');
            g.TreeStyle.chapterBoxes = function() { return []; };
            S._titles = undefined;
            S._underTitles(r, ctx, [0, 0, 100, 50], 1, 0, view, null, [{ l: 60, r: 110, t: 2, b: 12 }]);
            this.check(spots.length === 0, 'no chapter titles (the design has none): nothing more');
        } finally {
            g.TreeStyle = saved.ts; S._drawStrip = saved.strip; S._found = saved.found; S._titles = saved.titles;
        }
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
            // A frame with time enough (it started "later") finishes it
            done = B.step(r, ((typeof performance !== 'undefined') ? performance.now() : Date.now()) + 1e9, false);
            this.check(done && !B.active(), 'a frame with time enough finishes it');
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
