/**
 * LayoutDeclutterTest - node tests for LayoutDeclutter (run-tests.js).
 *
 * A small made-up tree with known faults: two spells on top of each other, a
 * spell on the heart, spells on lines they do not end at, two lines leaving a
 * spell almost on top of each other, two lines running side by side, two
 * long lines running side by side a long way (a bundle). Checks that the
 * faults are gone, the tree is spaced out by SPREAD with the root keeping its
 * direction, spells stay in their school's sector, and the result is the same
 * every run.
 *
 * Depends on: LayoutDeclutter
 */

var LayoutDeclutterTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _tree: function() {
        // One school to the right (sector -45..45 degrees), a root, two spells
        // far from everything, two spells on one spot, one spell on the heart
        return {
            globe: { x: 0, y: 0, radius: 45 },
            schools: {
                Test: {
                    startAngle: -45, endAngle: 45,
                    nodes: [
                        { formId: 'root', x: 120, y: 0, isRoot: true, children: ['far', 'a'] },
                        { formId: 'far', x: 420, y: 0, children: [] },
                        { formId: 'stranger', x: 270, y: 3, children: [] },
                        { formId: 'a', x: 200, y: 120, children: ['b'] },
                        { formId: 'b', x: 201, y: 121, children: [] },
                        { formId: 'onHeart', x: 30, y: 10, children: [] },
                        { formId: 'p', x: 250, y: -100, children: ['q'] },
                        { formId: 'q', x: 400, y: -100, children: [] },
                        { formId: 'onLine', x: 325, y: -98, children: [] },
                        { formId: 'fan', x: 300, y: 150, children: ['f1', 'f2'] },
                        { formId: 'f1', x: 420, y: 160, children: [] },
                        { formId: 'f2', x: 420, y: 176, children: [] },
                        { formId: 'g1', x: 330, y: 180, children: ['g2'] },
                        { formId: 'g2', x: 560, y: 180, children: [] },
                        { formId: 'h1', x: 380, y: 190, children: ['h2'] },
                        { formId: 'h2', x: 520, y: 185, children: [] },
                        { formId: 'k1', x: 300, y: -200, children: ['k2'] },
                        { formId: 'k2', x: 760, y: -300, children: [] },
                        { formId: 'm1', x: 330, y: -180, children: ['m2'] },
                        { formId: 'm2', x: 790, y: -282, children: [] }
                    ]
                }
            }
        };
    },

    _byId: function(tree) {
        var out = {};
        tree.schools.Test.nodes.forEach(function(n) { out[n.formId] = n; });
        return out;
    },

    run: function() {
        console.log('');
        console.log('LayoutDeclutter');
        var L = LayoutDeclutter;
        var log = console.log;
        var tree = this._tree();
        console.log = function() {};
        var result = L.apply(tree);
        console.log = log;
        var n = this._byId(tree);
        var dist = function(p, q) { return Math.sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y)); };

        var clearOf = function(m, p, q) {
            return LayoutLineClear._segDist2(m.x, m.y, p.x, p.y, q.x, q.y) >= (L.LINE_CLEAR - 0.5) * (L.LINE_CLEAR - 0.5);
        };
        this.check(Math.abs(n.root.x - 120 * L.SPREAD) < 0.01 && n.root.y === 0,
            'the root is only spaced out (same direction)');
        this.check(dist(n.a, n.b) >= 2 * L.NODE_RADIUS + L.GAP - 0.5, 'two spells on one spot are pushed apart');
        this.check(clearOf(n.stranger, n.root, n.far), 'a spell on a long line it does not end at is moved off it');
        this.check(clearOf(n.onLine, n.p, n.q), 'a spell on a short line it does not end at is moved off it');
        this.check(result.linesLeft === 0, 'no spell is left on a line (' + result.linesLeft + ')');
        var spread = function(o, p, q) {
            var d = Math.abs(Math.atan2(p.y - o.y, p.x - o.x) - Math.atan2(q.y - o.y, q.x - o.x));
            return (d > Math.PI ? 2 * Math.PI - d : d) * 180 / Math.PI;
        };
        var lc = LayoutLineClear;
        var gapOk = lc._cross(n.g1.x, n.g1.y, n.g2.x, n.g2.y, n.h1.x, n.h1.y, n.h2.x, n.h2.y) ||
            Math.min(lc._pointSeg2(n.h1.x, n.h1.y, n.g1.x, n.g1.y, n.g2.x, n.g2.y), lc._pointSeg2(n.h2.x, n.h2.y, n.g1.x, n.g1.y, n.g2.x, n.g2.y),
                     lc._pointSeg2(n.g1.x, n.g1.y, n.h1.x, n.h1.y, n.h2.x, n.h2.y), lc._pointSeg2(n.g2.x, n.g2.y, n.h1.x, n.h1.y, n.h2.x, n.h2.y)) >=
            (lc.LINE_GAP - 0.5) * (lc.LINE_GAP - 0.5);
        this.check(gapOk, 'two lines running 5 units apart are moved at least LINE_GAP apart');
        var side = Math.min(lc._pointSeg2(n.m1.x, n.m1.y, n.k1.x, n.k1.y, n.k2.x, n.k2.y),
                            lc._pointSeg2(n.m2.x, n.m2.y, n.k1.x, n.k1.y, n.k2.x, n.k2.y));
        this.check(lc._cross(n.k1.x, n.k1.y, n.k2.x, n.k2.y, n.m1.x, n.m1.y, n.m2.x, n.m2.y) ||
            Math.sqrt(side) >= 2 * lc.LINE_GAP, 'two long lines side by side 20 apart are opened up to twice LINE_GAP (' +
            Math.round(Math.sqrt(side)) + ')');
        this.check(spread(n.fan, n.f1, n.f2) >= 20, 'two lines leaving a spell 7 degrees apart are opened up (' +
            Math.round(spread(n.fan, n.f1, n.f2)) + ' degrees)');
        this.check(dist(n.onHeart, { x: 0, y: 0 }) >= 45 + L.HEART_CLEARANCE - 0.5, 'a spell on the heart is moved off it');
        var inSector = tree.schools.Test.nodes.every(function(node) {
            return Math.abs(Math.atan2(node.y, node.x)) <= Math.PI / 4 + 1e-6;
        });
        this.check(inSector, 'every spell stays inside its school sector');
        this.check(result.overlapsLeft === 0, 'no overlaps are left (' + result.overlapsLeft + ')');

        var again = this._tree();
        console.log = function() {};
        L.apply(again);
        console.log = log;
        this.check(JSON.stringify(again) === JSON.stringify(tree), 'the same tree comes out the same');

        // A piece at a time (applyAsync's way, a yield after almost every spell): the same tree
        var pieces = this._tree(), steps = 1;
        console.log = function() {};
        var job = L.begin(pieces);
        while (!L.step(job, 0.001)) steps++;
        console.log = log;
        this.check(steps > 1 && JSON.stringify(pieces) === JSON.stringify(tree),
            'a piece at a time gives the same tree (' + steps + ' pieces)');

        var empty = { schools: {} };
        this.check(L.apply(empty).moved === 0, 'an empty tree is left alone');
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.LayoutDeclutterTest = LayoutDeclutterTest;
if (typeof global !== 'undefined') global.LayoutDeclutterTest = LayoutDeclutterTest;
