/**
 * LayerScrollTest - node tests for LayerScroll's arithmetic (run-tests.js):
 * the strips a shift uncovers (each axis, both, none), that the pieces cover
 * them exactly and come nearest first, and that kept names move with the
 * picture and are dropped once wholly outside the layer.
 *
 * Depends on: LayerScroll
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

        var labels = [
            { x: 10, y: 10, l: 0, r: 20, t: 8, b: 20 },
            { x: 95, y: 10, l: 85, r: 105, t: 8, b: 20 }
        ];
        S.shiftLabels(labels, -30, 5, { l: 0, r: 100, t: 0, b: 80 });
        this.check(labels.length === 1 && labels[0].x === 65 && labels[0].l === 55 && labels[0].t === 13,
            'kept names move with the picture; one wholly outside is dropped');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.LayerScrollTest = LayerScrollTest;
