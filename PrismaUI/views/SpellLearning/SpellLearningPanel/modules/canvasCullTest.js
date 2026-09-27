/**
 * CanvasCullTest - node tests for CanvasRenderer's culling index (run-tests.js):
 * the spells and lines a box's grid cells give, once each and in drawing order,
 * are exactly the ones the full loop's box tests pass (spells off any finite
 * place and very long lines included, bowed lines allowed for); the index is
 * made again when the lists change, and not used in edit mode.
 *
 * Depends on: CanvasRenderer (canvasRendererV2.js, canvasRendererData.js,
 * canvasRendererEdges.js)
 */

var CanvasCullTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** A seeded random number in 0..1 (the same tree every run). */
    _rng: function(seed) {
        var s = seed >>> 0;
        return function() { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
    },

    /** A stand-in tree: spells scattered round the middle, lines between them. */
    _tree: function(r, rand) {
        var nodes = [], edges = [], map = new Map(), i;
        for (i = 0; i < 600; i++) {
            var a = rand() * Math.PI * 2, d = rand() * 2500;
            var n = { id: 'n' + i, formId: '0x' + i, x: Math.cos(a) * d, y: Math.sin(a) * d, isRoot: i % 97 === 0 };
            nodes.push(n);
        }
        nodes[7].x = NaN;                       // off any finite place
        nodes[8].y = Infinity;
        for (i = 0; i < nodes.length; i++) { map.set(nodes[i].id, nodes[i]); map.set(nodes[i].formId, nodes[i]); }
        for (i = 1; i < nodes.length; i++) {
            var from = Math.floor(rand() * i);
            edges.push({ from: nodes[from].id, to: nodes[i].formId });
        }
        edges.push({ from: 'n1', to: 'missing' });                          // an end that is not there
        nodes.push({ id: 'far1', x: -20000, y: -20000 }, { id: 'far2', x: 20000, y: 20000 });
        map.set('far1', nodes[nodes.length - 2]); map.set('far2', nodes[nodes.length - 1]);
        edges.push({ from: 'far1', to: 'far2' });                           // longer than any grid keeps
        r.nodes = nodes; r.edges = edges; r._nodeMap = map; r._discoveryVisibleIds = null;
        r._cullDirty = true;
    },

    /** The full loop's answer: indices of spells whose centre is in the box. */
    _nodesBrute: function(r, l, rt, t, b) {
        var out = [];
        for (var i = 0; i < r.nodes.length; i++) {
            var n = r.nodes[i];
            if (n.x < l || n.x > rt || n.y < t || n.y > b) continue;
            out.push(i);
        }
        return out;
    },

    /** The old renderEdges test (shouldDrawEdge), edge by edge. */
    _edgesBrute: function(r, l, rt, t, b, bend) {
        var out = [];
        for (var i = 0; i < r.edges.length; i++) {
            var e = r.edges[i], f = r._nodeMap.get(e.from), to = r._nodeMap.get(e.to);
            if (!f || !to) continue;
            var minX = Math.min(f.x, to.x), maxX = Math.max(f.x, to.x), minY = Math.min(f.y, to.y), maxY = Math.max(f.y, to.y);
            var ext = bend ? bend * (maxX - minX + maxY - minY) : 0;
            if (maxX + ext < l || minX - ext > rt || maxY + ext < t || minY - ext > b) continue;
            out.push(i);
        }
        return out;
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var r = g.CanvasRenderer;
        if (!r || typeof r._cullIndex !== 'function') { this.check(false, 'CanvasRenderer loaded'); return { passed: this.passed, failed: this.failed }; }
        var saved = { nodes: r.nodes, edges: r.edges, map: r._nodeMap, disc: r._discoveryVisibleIds, edit: g.EditMode };
        var rand = this._rng(12345);
        try {
            g.EditMode = { isActive: false };
            this._tree(r, rand);
            var ix = r._cullIndex();
            this.check(ix.nodeCells && ix.edgeCells, 'the index has its grids');
            this.check(ix.keys[3] === r.edges[3].from + '->' + r.edges[3].to && ix.from[3] === r._nodeMap.get(r.edges[3].from),
                'each line keeps its two spells and its key');
            this.check(ix.roots.length === 7 && ix.roots[1] === 97, 'the roots, in node order');

            var nodeOk = true, edgeOk = true, gridUsed = 0, big = 0;
            for (var k = 0; k < 300; k++) {
                var w = 50 + rand() * 1500, h = 50 + rand() * 1500;
                var l = (rand() - 0.5) * 6000, t = (rand() - 0.5) * 6000;
                if (k % 50 === 0) { w = 12000; h = 12000; l = -6000; t = -6000; }   // bigger than the grid is worth
                var n = r._nodesInBox(l, l + w, t, t + h), got = [];
                if (n < 0) { big++; for (var a = 0; a < r.nodes.length; a++) got.push(a); } else { gridUsed++; for (a = 0; a < n; a++) got.push(r._cull.nodePick[a]); }
                // the caller's own box test, then compare with the full loop (same set, same order)
                var kept = got.filter(function(i) { var q = r.nodes[i]; return !(q.x < l || q.x > l + w || q.y < t || q.y > t + h); });
                if (kept.join() !== this._nodesBrute(r, l, l + w, t, t + h).join()) nodeOk = false;
                var bend = (k % 3 === 0) ? 0 : 0.08 + (k % 3) * 0.02;
                var nv = r._visibleEdges(r._cullIndex(), l, l + w, t, t + h, bend), vis = [];
                for (a = 0; a < nv; a++) vis.push(r._cull.vis[a]);
                if (vis.join() !== this._edgesBrute(r, l, l + w, t, t + h, bend).join()) edgeOk = false;
            }
            this.check(nodeOk, 'spells from the grid = spells the full loop draws, in order (300 boxes)');
            this.check(edgeOk, 'lines from the grid = lines the full loop draws, in order, bowed or not');
            this.check(gridUsed > 200 && big >= 6, 'small boxes use the grid, a huge one looks at everything');

            // The lists change: made again
            r.edges.push({ from: 'n2', to: 'n3' });
            ix = r._cullIndex();
            this.check(ix.edgeCount === r.edges.length && ix.keys[ix.edgeCount - 1] === 'n2->n3', 'a line added: the index is made again');
            r._cullDirty = true;
            this.check(r._cullIndex() !== ix, 'marked dirty (buildSpatialIndex): made again');

            // Edit mode: no grids, fresh each time, rebuilt after
            g.EditMode.isActive = true;
            this.check(r._nodesInBox(0, 10, 0, 10) === -1, 'edit mode: every spell looked at');
            var e1 = r._cullIndex();
            this.check(!e1.edgeCells && e1.keys.length === r.edges.length && r._cullIndex() !== e1, 'edit mode: no grids, made for each use');
            g.EditMode.isActive = false;
            this.check(r._cullIndex().edgeCells !== null, 'after edit mode: the grids again');
        } finally {
            r.nodes = saved.nodes; r.edges = saved.edges; r._nodeMap = saved.map; r._discoveryVisibleIds = saved.disc;
            r._cull = null; r._cullDirty = true;
            g.EditMode = saved.edit;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.CanvasCullTest = CanvasCullTest;
