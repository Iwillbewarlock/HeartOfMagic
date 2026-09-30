/**
 * LevelFilterTest - node tests for filtering the tree by a spell's level
 * (run-tests.js).
 *
 * The spell card's level can be pressed like a keyword chip: "level.Adept"
 * lights every Adept spell. Checks the counts, which spells match, that the
 * card's level element becomes pressable only when the level is shown and
 * some spell has it, that one click handler serves every card fill, and that
 * a second press turns the filter off.
 *
 * Depends on: bridgeView.js (BridgeView) - under node this file runs it into
 * the global scope the way the page's script tag does. Swaps the globals
 * `state`, `document` and `CanvasRenderer` for its own while it runs.
 */

var LevelFilterTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['bridgeView.js', 'BridgeView'], ['treeParser.js', 'TreeParser']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    /** A stand-in element: the class list, attributes and listeners the card uses */
    _element: function() {
        var classes = {}, attrs = {}, listeners = {};
        return {
            title: '',
            classList: {
                add: function(c) { classes[c] = true; },
                remove: function(c) { delete classes[c]; },
                contains: function(c) { return !!classes[c]; }
            },
            setAttribute: function(k, v) { attrs[k] = String(v); },
            getAttribute: function(k) { return attrs.hasOwnProperty(k) ? attrs[k] : null; },
            removeAttribute: function(k) { delete attrs[k]; },
            addEventListener: function(type, fn) { (listeners[type] = listeners[type] || []).push(fn); },
            click: function() { var l = listeners.click || []; for (var i = 0; i < l.length; i++) l[i](); },
            listenerCount: function(type) { return (listeners[type] || []).length; }
        };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (!g.BridgeView || typeof g.BridgeView.bindLevel !== 'function') {
            this.check(false, 'bridgeView.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldState = g.state, oldDocument = g.document, oldRenderer = g.CanvasRenderer;
        var B = g.BridgeView;
        try {
            var nodes = [
                { id: 'a', formId: 'a', level: 'Adept', traits: ['element.fire'] },
                { id: 'b', formId: 'b', level: 'Adept', traits: ['element.frost'] },
                { id: 'c', formId: 'c', level: 'Novice', traits: ['element.fire'] },
                { id: 'd', formId: 'd', level: 'Unknown', traits: [] }
            ];
            g.state = { treeData: { nodes: nodes, rawData: {} } };
            g.document = {
                getElementById: function() { return null; },
                querySelectorAll: function() { return []; }
            };
            g.CanvasRenderer = { _needsRender: false };

            B.setTree(g.state.treeData);
            this.check(B.countOf('level.Adept') === 2 && B.countOf('level.Novice') === 1,
                'levels counted like traits');
            this.check(B.countOf('level.Unknown') === 0, 'Unknown level not counted');

            var el = this._element();
            B.bindLevel(el, nodes[0], false);
            this.check(!el.classList.contains('spell-chip-filter') && el.getAttribute('data-trait') === null,
                'hidden level (???) is not pressable');

            B.bindLevel(el, nodes[0], true);
            this.check(el.classList.contains('spell-chip-filter') && el.getAttribute('data-trait') === 'level.Adept',
                'shown level is pressable with its key');
            this.check(el.title === 'Adept (2)', 'title names the level and its count');

            el.click();
            this.check(B.isFilter('level.Adept') && g.CanvasRenderer._needsRender, 'press turns the filter on');
            var lit = nodes.filter(function(n) { return B.matchesFilter(n); }).map(function(n) { return n.id; });
            this.check(lit.join(',') === 'a,b', 'only the Adept spells match');

            B.bindLevel(el, nodes[2], true);
            this.check(el.listenerCount('click') === 1, 'one click handler across card fills');
            this.check(el.getAttribute('data-trait') === 'level.Novice' && !el.classList.contains('active'),
                'next card shows its own level, not pressed');
            el.click();
            this.check(B.isFilter('level.Novice'), 'pressing another level switches the filter');
            el.click();
            this.check(!B.hasFilter(), 'second press turns the filter off');

            B.bindLevel(el, nodes[3], true);
            this.check(!el.classList.contains('spell-chip-filter'), 'a level no spell counts is not pressable');

            // Levels that arrive after the tree was counted: the spell info comes
            // later, through TreeParser.updateNodeFromCache
            nodes.push({ id: 'e', formId: 'e', level: null, traits: [] });
            nodes.push({ id: 'f', formId: 'f', level: null, traits: [] });
            B.setTree(g.state.treeData);
            var oldCache = g.SpellCache, oldNames = g.SpellNames;
            g.SpellCache = { get: function(id) { return { name: id, skillLevel: 'Adept' }; } };
            g.SpellNames = undefined;
            try {
                g.TreeParser.updateNodeFromCache(nodes[4]);
                g.TreeParser.updateNodeFromCache(nodes[5]);
            } finally {
                g.SpellCache = oldCache;
                g.SpellNames = oldNames;
            }
            B.bindLevel(el, nodes[4], true);
            this.check(el.getAttribute('data-trait') === 'level.Adept' && B.countOf('level.Adept') === 4,
                'levels that came after the count are counted (Adept 2 -> 4)');

            // An active filter's pill shows a count: one redraw per batch of level changes
            var scheduled = 0, oldTimeout = g.setTimeout;
            g.setTimeout = function() { scheduled++; };
            try {
                B.countOf('level.Adept');
                B.invalidateCounts();
                this.check(scheduled === 0, 'no pill redraw with no filter on');
                B.toggleFilter('level.Adept');
                B.countOf('level.Adept'); // the pill counts when drawn (no pill element here)
                B.invalidateCounts();
                B.invalidateCounts();
                this.check(scheduled === 1, 'one pill redraw for a batch while a filter is on');
                B.toggleFilter('level.Adept');
            } finally {
                g.setTimeout = oldTimeout;
            }

            B.toggleFilter('element.fire');
            this.check(B.matchesFilter(nodes[0]) && !B.matchesFilter(nodes[1]), 'trait filter unchanged');
            B.toggleFilter('element.fire');
        } finally {
            g.state = oldState;
            g.document = oldDocument;
            g.CanvasRenderer = oldRenderer;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.LevelFilterTest = LevelFilterTest;
if (typeof global !== 'undefined') global.LevelFilterTest = LevelFilterTest;
