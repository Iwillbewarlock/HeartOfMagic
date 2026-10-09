/**
 * LabelCullTest - node tests for the spell names (run-tests.js): the name
 * candidates _labelCandidates takes from the culling index are exactly the ones
 * the full loop over every spell gives (same spells, same order, same places)
 * for many pans, zooms, turns and margins; and below LABEL_FOCUS_ZOOM at most
 * MAX_LABELS_FOCUS names are placed, the most important first, MAX_LABELS above;
 * the selected spell's name, in the layer's margin, is placed (renderLabels:
 * first; LayerScroll._labels: past a cap already full).
 *
 * Depends on: CanvasRenderer (canvasRendererV2.js, canvasRendererData.js,
 * canvasRendererLabels.js), LayerScroll, settings; sets its own state stand-in
 */

var LabelCullTest = {

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

    /** A stand-in tree of spells in every state, some nameless, a few off any finite place. */
    _tree: function(r, rand) {
        var states = ['unlocked', 'learning', 'available', 'locked'], nodes = [];
        for (var i = 0; i < 1400; i++) {
            var a = rand() * Math.PI * 2, d = rand() * 2500;
            nodes.push({ id: 'n' + i, formId: '0x' + i, name: i % 31 === 0 ? '' : 'Spell ' + i,
                         school: i % 5 === 0 ? 'Hidden' : 'Destruction',
                         state: states[Math.floor(rand() * 4)], x: Math.cos(a) * d, y: Math.sin(a) * d });
        }
        nodes[7].x = NaN;
        nodes[8].y = Infinity;
        r.nodes = nodes; r.edges = []; r._nodeMap = new Map(); r._discoveryVisibleIds = null;
        r._cullDirty = true;
    },

    /** The candidates as one string: spell, place, in view, priority - in order. */
    _sig: function(found) {
        if (!found) return 'null';
        return found.candidates.map(function(c) {
            return c.node.id + '@' + c.x + ',' + c.y + (c.inView ? 'v' : 'm') + c.priority;
        }).join(' ') + ' max' + found.maxLabels;
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var r = g.CanvasRenderer;
        if (!r || typeof r._labelCandidates !== 'function' || typeof r._nodesInBox !== 'function') {
            this.check(false, 'CanvasRenderer with names and the culling index loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var keys = ['nodes', 'edges', '_nodeMap', '_discoveryVisibleIds', 'zoom', 'panX', 'panY', '_width', '_height',
                    'selectedNode', 'hoveredNode', '_hoverPathNodes', 'labelScale', '_learningColor', '_contextFactor', '_nodesInBox'];
        var saved = {}, k;
        for (k = 0; k < keys.length; k++) saved[keys[k]] = r.hasOwnProperty(keys[k]) ? { v: r[keys[k]] } : null;
        var savedG = { ts: g.TreeStyle, edit: g.EditMode, cheat: settings.cheatMode, vis: settings.schoolVisibility,
                       names: settings.showNodeNames, state: g.state };
        var rand = this._rng(4242);
        try {
            g.EditMode = { isActive: false };
            g.TreeStyle = {
                tokens: { labelUnlocked: '#fff', labelHidden: '#888', labelAvailable: '#ccc', labelMaxChars: 24 },
                clipLabel: function(t) { return t; },
                beginLabels: function() {}, drawLabel: function() {}
            };
            settings.schoolVisibility = { Hidden: false };
            settings.showNodeNames = true;
            g.state = { spellProgress: { '0x3': { xp: 50, required: 100 } } };
            r._learningColor = function() { return '#0ff'; };
            r._contextFactor = function() { return 1; };
            r.labelScale = 1; r._hoverPathNodes = null;
            this._tree(r, rand);
            r.selectedNode = r.nodes[11]; r.hoveredNode = r.nodes[12];

            // The index against the full loop (_nodesInBox answering "every spell")
            var real = r._nodesInBox, grid = 0, same = true, firstBad = '';
            var zooms = [0.5, 0.6, 0.79, 0.8, 1.0, 1.3, 2.2];
            for (var c = 0; c < 140; c++) {
                r.zoom = zooms[c % zooms.length];
                r.panX = (rand() - 0.5) * 3000; r.panY = (rand() - 0.5) * 3000;
                r._width = 600 + Math.floor(rand() * 1100); r._height = 400 + Math.floor(rand() * 700);
                var rot = (c % 4 === 0) ? 0 : rand() * Math.PI * 2;
                var cos = Math.cos(rot), sin = Math.sin(rot), margin = (c % 3) * 150;
                var cx = r._width / 2, cy = r._height / 2;
                settings.cheatMode = c % 5 === 0;
                r._nodesInBox = function() { return -1; };
                var full = this._sig(r._labelCandidates(cx, cy, cos, sin, margin));
                r._nodesInBox = function(l, rt, t, b) { var n = real.call(r, l, rt, t, b); if (n >= 0) grid++; return n; };
                var culled = this._sig(r._labelCandidates(cx, cy, cos, sin, margin));
                if (full !== culled && same) { same = false; firstBad = 'case ' + c + ' zoom ' + r.zoom; }
            }
            r._nodesInBox = real;
            this.check(same, 'name candidates from the index = the full loop\'s: same spells, order, places (140 views)' + (same ? '' : ' - ' + firstBad));
            r.zoom = 1.0; r.panX = 0; r.panY = 0;
            var probe = r._labelCandidates(r._width / 2, r._height / 2, 1, 0, 0);
            this.check(probe && isFinite(probe.fontSize) && probe.fontSize > 0 && probe.candidates.length > 0 &&
                isFinite(probe.candidates[0].x) && isFinite(probe.candidates[0].y), 'a real font size and places (not NaN)');
            this.check(grid >= 60, 'most of those views used the grid (' + grid + ' of 140)');

            // The cap: 50 between LABEL_MIN_ZOOM and LABEL_FOCUS_ZOOM, 150 above
            this.check(r.MAX_LABELS_FOCUS === 50 && r.MAX_LABELS === 150, 'MAX_LABELS_FOCUS 50, MAX_LABELS 150');
            r.panX = 0; r.panY = 0; r._width = 3000; r._height = 2000; settings.cheatMode = true;
            r.selectedNode = null; r.hoveredNode = null;
            var spread = function(count, step, every) {
                r.nodes = [];
                for (var i = 0; i < count; i++) {
                    r.nodes.push({ id: 'g' + i, formId: '0xg' + i, name: 'S' + i, school: 'Destruction',
                                   state: i % every === 0 ? 'learning' : 'available',
                                   x: (i % 15) * step - 7 * step, y: Math.floor(i / 15) * step - 4 * step });
                }
                r._cullDirty = true;
            };
            var ctx = { font: '10px x', measureText: function() { return { width: 10 }; } };
            r.zoom = 0.6;
            spread(120, 280, 6);
            r.renderLabels(ctx, 1500, 1000, 1, 0, 0);
            var placed = r._layerLabels, learning = 0;
            for (k = 0; k < placed.length; k++) if (placed[k].node.state === 'learning') learning++;
            this.check(placed.length === 50, 'zoom 0.6: 50 of 120 names placed (' + placed.length + ')');
            this.check(learning === 20, 'zoom 0.6: every name being learned among them (' + learning + ' of 20)');
            this._selectedPastCap(r, ctx, g);
            r.zoom = 1.0;
            spread(200, 120, 6);
            r.renderLabels(ctx, 1500, 1000, 1, 0, 0);
            this.check(r._layerLabels.length === 150, 'zoom 1.0: 150 of 200 names placed (' + r._layerLabels.length + ')');
            r.zoom = 0.4;
            this.check(r._labelCandidates(1500, 1000, 1, 0, 0) === null, 'below LABEL_MIN_ZOOM: no names');
        } finally {
            for (k = 0; k < keys.length; k++) {
                if (saved[keys[k]]) r[keys[k]] = saved[keys[k]].v; else delete r[keys[k]];
            }
            r._cull = null; r._cullDirty = true; r._layerLabels = [];
            g.TreeStyle = savedG.ts; g.EditMode = savedG.edit; settings.cheatMode = savedG.cheat;
            settings.schoolVisibility = savedG.vis; settings.showNodeNames = savedG.names; g.state = savedG.state;
        }
        return { passed: this.passed, failed: this.failed };
    },

    /**
     * The selected spell in the layer's margin while the cap is full of names in
     * view (r.nodes: the 120 of the zoom 0.6 case): renderLabels places its name
     * first (one of the 50), and a strip LayerScroll draws over it places it past
     * a cap already full (a drag bringing it in shows it named).
     */
    _selectedPastCap: function(r, ctx, g) {
        var sel = { id: 'sel', formId: '0xsel', name: 'Chosen', school: 'Destruction', state: 'available',
                    x: (-120 - 1500) / 0.6, y: 0 };    // screen x -120: in the 150 px margin, not in view
        r.nodes.push(sel); r._cullDirty = true;
        r.selectedNode = sel;
        var has = function(list) {
            for (var i = 0; i < list.length; i++) if (list[i].node === sel) return true;
            return false;
        };
        r.renderLabels(ctx, 1500, 1000, 1, 0, 150);
        this.check(has(r._layerLabels) && r._layerLabels.length === 50,
            'zoom 0.6, 120 learnable in view: the selected spell in the margin still among the 50 names (' + r._layerLabels.length + ')');
        var LS = g.LayerScroll;
        if (!LS || typeof LS._labels !== 'function') { this.check(false, 'LayerScroll loaded'); return; }
        // The layer's names made with nothing selected (50 in view: the cap full on
        // screen), then the spell selected and a strip drawn over it
        var savedHalo = g.TreeStyle.tokens.labelHaloWidth;
        g.TreeStyle.tokens.labelHaloWidth = 0;
        r.selectedNode = null;
        r.renderLabels(ctx, 1500, 1000, 1, 0, 150);
        r.selectedNode = sel;
        var gc = { font: '10px x', measureText: ctx.measureText, save: function() {}, restore: function() {},
                   setTransform: function() {}, translate: function() {}, beginPath: function() {},
                   rect: function() {}, clip: function() {} };
        var view = { cx: 1500, cy: 1000, cos: 1, sin: 0 };
        LS._found = undefined;
        try {
            LS._labels(r, gc, [0, 0, 100, 2300], 1, 150, view, { l: 0, r: 3000, t: 0, b: 2000 }, false);
            this.check(r._layerLabels.length === 51 && has(r._layerLabels),
                'a dragged strip over the margin names the selected spell with the cap (50 in view) already full');
        } finally {
            LS._found = undefined;
            g.TreeStyle.tokens.labelHaloWidth = savedHalo;
        }
        r.selectedNode = null;
    }
};

if (typeof window !== 'undefined') window.LabelCullTest = LabelCullTest;
