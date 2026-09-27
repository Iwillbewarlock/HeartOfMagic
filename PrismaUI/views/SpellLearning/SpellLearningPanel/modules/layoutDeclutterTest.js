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
        this._hostile(L);
        this._longLine(L);
        this._native(L);
        return { passed: this.passed, failed: this.failed };
    },

    /** Spells at Infinity, 1e308 or 1e7, a sector at 1e15 degrees, an infinite globe: done at once, left alone. */
    _hostile: function(L) {
        var log = console.log;
        var tree = {
            globe: { x: 0, y: 0, radius: Infinity },
            schools: {
                Far: {
                    startAngle: 1e15, endAngle: 1e15 + 1,
                    nodes: [
                        { formId: 'r', x: 100, y: 100, isRoot: true, children: ['a', 'inf', 'huge', 'line'] },
                        { formId: 'a', x: 101, y: 101, children: [] },
                        { formId: 'inf', x: Infinity, y: 5, children: [] },
                        { formId: 'huge', x: 1e308, y: 1e308, children: [] },
                        { formId: 'line', x: 1e7, y: 100, children: [] },
                        { formId: 'nan', x: NaN, y: -Infinity, children: [] }
                    ]
                }
            }
        };
        var t0 = Date.now();
        console.log = function() {};
        var result = L.apply(tree);
        console.log = log;
        var n = {};
        tree.schools.Far.nodes.forEach(function(node) { n[node.formId] = node; });
        this.check(Date.now() - t0 < 2000, 'a tree with spells at Infinity and 1e308 is done at once (' + (Date.now() - t0) + ' ms)');
        this.check(n.inf.x === Infinity && n.huge.x === 1e308 && n.line.x === 1e7 && isNaN(n.nan.x),
            'spells not finite or past MAX_COORD are left where they are');
        this.check(isFinite(n.a.x) && isFinite(n.a.y) && result.moved === 2,
            'the others are still arranged (an infinite globe counts as the default)');
    },

    /** A line longer than MAX_LINE is left out: a spell on it stays (only spread out), and it is not counted. */
    _longLine: function(L) {
        var log = console.log, lc = LayoutLineClear;
        var far = lc.MAX_LINE / L.SPREAD + 1000;
        var tree = {
            globe: { x: 0, y: 0, radius: 45 },
            schools: {
                Long: {
                    nodes: [
                        { formId: 'r', x: 100, y: 0, isRoot: true, children: ['far'] },
                        { formId: 'far', x: far, y: 0, children: [] },
                        { formId: 'onIt', x: far / 2, y: 3, children: [] }
                    ]
                }
            }
        };
        console.log = function() {};
        var result = L.apply(tree);
        console.log = log;
        var on = tree.schools.Long.nodes[2];
        this.check(Math.abs(on.x - far / 2 * L.SPREAD) < 0.01 && Math.abs(on.y - 3 * L.SPREAD) < 0.01 && result.linesLeft === 0,
            'a spell on a line longer than MAX_LINE is left on it, and not counted (' + result.linesLeft + ')');
    },

    /**
     * The plugin path: only the reply with this request's id is taken; a timeout pauses the plugin, a
     * reply resumes it (the plugin's { cancelled } answer to DeclutterCancel too); every request the
     * panel stops waiting for is cancelled (timeout, unreadable reply, a newer request arranged here).
     */
    _native: function(L) {
        var sent = [], oldCpp = window.callCpp, warn = console.warn, log = console.log;
        var cancels = [];
        window.callCpp = function(name, arg) {
            if (name === 'DeclutterTree') sent.push(JSON.parse(arg));
            if (name === 'DeclutterCancel') cancels.push(arg);
        };
        console.warn = function() {};
        L._nativeRetryAt = 0;
        var done = 0, token = L.applyAsync(this._tree(), function() { done++; });
        clearTimeout(token.timer);
        L._onNativeResult(JSON.stringify({ id: null, error: 'a request that could not be read' }));
        L._onNativeResult(JSON.stringify({ id: 'declutter-older', error: 'an older request' }));
        this.check(sent.length === 1 && !token.answered && done === 0,
            'replies with id null or another request\'s id are not taken as this one\'s');
        L._onNativeTimeout(token);
        this.check(cancels.length === 1 && cancels[0] === token.id, 'a timeout tells the plugin to stop that request (DeclutterCancel)');
        L._asyncJob = null;                    // the JavaScript pass it started stops at its first slice
        var t2 = L.applyAsync(this._tree(), function() {});
        L._asyncJob = null;
        this.check(sent.length === 1 && !t2.native, 'after a timeout the plugin is not asked for NATIVE_RETRY_MS');
        L._onNativeResult(JSON.stringify({ id: token.id, positions: [] }));   // late, from the plugin
        var t3 = L.applyAsync(this._tree(), function() { done++; });
        this.check(sent.length === 2 && t3.native, 'a late reply from the plugin has it asked again');

        // Timed out again; the plugin answers the DeclutterCancel with { id, cancelled: true }
        L._onNativeTimeout(t3);
        var job3 = t3.job, before = JSON.stringify(t3.output), paused = L._nativeRetryAt > Date.now();
        L._onNativeResult(JSON.stringify({ id: t3.id, cancelled: true }));
        this.check(paused && L._nativeRetryAt === 0 && t3.job === job3 && L._asyncJob === t3 &&
            JSON.stringify(t3.output) === before && done === 0 && cancels.length === 2,
            'a { cancelled } reply ends the pause and applies nothing (no second fallback)');
        L._asyncJob = null;
        var t4 = L.applyAsync(this._tree(), function() {});
        this.check(sent.length === 3 && t4.native, 'after a { cancelled } reply the plugin is asked again');
        L._onNativeResult('{"id": "declutter-');                            // cut off
        this.check(t4.answered && !!t4.job && L._asyncJob === t4,
            'an unreadable reply has the waiting request arranged here at once');
        this.check(cancels.length === 3 && cancels[2] === t4.id, 'an unreadable reply tells the plugin to stop that request');

        // A { cancelled } reply to a request still waiting: arranged here once, nothing applied
        L._asyncJob = null;
        var t5 = L.applyAsync(this._tree(), function() {});
        before = JSON.stringify(t5.output);
        L._onNativeResult(JSON.stringify({ id: t5.id, cancelled: true }));
        var job5 = t5.job;
        L._onNativeResult(JSON.stringify({ id: t5.id, cancelled: true }));
        this.check(t5.answered && !!job5 && t5.job === job5 && JSON.stringify(t5.output) === before,
            'a { cancelled } reply to a waiting request has it arranged here once, nothing applied');

        // A newer request arranged here (too few spells) while the plugin still works on the last one
        L._asyncJob = null;
        var t6 = L.applyAsync(this._tree(), function() {});
        var t7 = L.applyAsync({ schools: {} }, function() {});
        this.check(sent.length === 5 && !t7.native && t6.answered && cancels.length === 4 && cancels[3] === t6.id,
            'a newer request arranged here tells the plugin to stop the last one');
        L._asyncJob = null;
        L._nativeRetryAt = 0;
        window.callCpp = oldCpp;
        console.warn = warn;
        console.log = log;
    }
};

if (typeof window !== 'undefined') window.LayoutDeclutterTest = LayoutDeclutterTest;
if (typeof global !== 'undefined') global.LayoutDeclutterTest = LayoutDeclutterTest;
