/**
 * PrereqListsTest - node tests for a spell's prerequisite lists (run-tests.js).
 *
 * The Classic tree output once gave prerequisites and softPrereqs one array, so
 * the editor's pen put a new link in both the required and the optional list,
 * its delete took it out of both and a required -> optional switch left it
 * twice (Nexus report, 1.2.0). Checks: the output's lists are separate arrays,
 * a saved tree with that damage is repaired as it loads (PrereqLists.normalize
 * through TreeParser), the editor's add -> switch -> switch back -> delete
 * leaves the link in one list at a time, softNeeded stays usable, and the C++
 * entries give merged copies their spell's lists.
 *
 * Depends on: prereqLists.js, treeParser.js, classic/classicTreeOutput.js and
 * editMode.js - under node this file runs them into the global scope the way
 * the page's script tags do. Swaps the globals `state`, `CanvasRenderer` and
 * (when missing) `TREE_CONFIG` for its own while it runs.
 */

var PrereqListsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['prereqLists.js', 'PrereqLists'], ['treeParser.js', 'TreeParser'],
                     ['classic/classicTreeOutput.js', 'ClassicTreeOutput'], ['editMode.js', 'EditMode']];
        // editMode.js listens on window when it loads; node's global has no addEventListener
        var oldListen = g.addEventListener;
        if (typeof g.addEventListener !== 'function') g.addEventListener = function() {};
        try {
            for (var i = 0; i < files.length; i++) {
                if (typeof g[files[i][1]] !== 'undefined') continue;
                var file = path.join(__dirname, files[i][0]);
                vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
            }
        } finally {
            g.addEventListener = oldListen;
        }
    },

    _count: function(list, id) {
        var n = 0;
        for (var i = 0; list && i < list.length; i++) if (list[i] === id) n++;
        return n;
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.PrereqLists === 'undefined' || typeof g.TreeParser === 'undefined' ||
            typeof g.ClassicTreeOutput === 'undefined' || typeof g.EditMode === 'undefined') {
            this.check(false, 'prereqLists.js, treeParser.js, classicTreeOutput.js and editMode.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        this._normalize(g);
        this._classicOutput(g);
        this._editor(g);
        this._cppEntries(g);
        return { passed: this.passed, failed: this.failed };
    },

    _normalize: function(g) {
        var lists = g.PrereqLists.normalize(['a', 'b', 'a'], ['b', 'c', 'c', 'd']);
        this.check(lists.hard.join(',') === 'a,b' && lists.soft.join(',') === 'c,d',
            'normalize: repeats out, an id in both lists stays required only');
        var soft = ['x'];
        var again = g.PrereqLists.normalize([], soft);
        this.check(again.soft !== soft, 'normalize: always fresh arrays');
        this.check(g.PrereqLists.normalize(null, undefined).hard.length === 0, 'normalize: missing lists are empty');
    },

    _classicOutput: function(g) {
        var oldState = g.state, oldConfig = g.TREE_CONFIG;
        try {
            if (!g.TREE_CONFIG) g.TREE_CONFIG = { layoutStyles: { radial: {} } };
            g.state = { treeSpellCopies: { '0x02': ['0x09'] } };
            var built = g.ClassicTreeOutput.build(
                { schools: { Destruction: { root: '0x01', nodes: [
                    { formId: '0x01', tier: 1 }, { formId: '0x02', tier: 2 }] } } },
                { schools: { Destruction: { nodes: [
                    { formId: '0x01', x: 0, y: 0 }, { formId: '0x02', x: 10, y: 0, parentFormId: '0x01' }] } } },
                { mode: 'sun' });
            var nodes = built.output.schools.Destruction.nodes;
            var child = nodes[0].formId === '0x02' ? nodes[0] : nodes[1];
            this.check(child.prerequisites !== child.softPrereqs, 'Classic output: prerequisites and softPrereqs are separate arrays');
            this.check(child.softPrereqs.join(',') === '0x01', 'Classic output: the layout link is optional');
            this.check(child.copies && child.copies.join(',') === '0x09', 'Classic output: the merged copies are saved on their spell');

            // A saved tree with the old damage: a link in both lists, twice in optional
            var parsed = g.TreeParser.parse({ trustPrereqs: true, schools: { Destruction: { root: '0x01', nodes: [
                { formId: '0x01', isRoot: true, children: ['0x02'], prerequisites: [] },
                { formId: '0x02', children: [], prerequisites: ['0x01'],
                  hardPrereqs: ['0x01'], softPrereqs: ['0x01', '0x01'], softNeeded: 1 }] } } });
            var loaded = parsed && parsed.nodes ? (parsed.nodes.get ? parsed.nodes.get('0x02') :
                parsed.nodes.filter(function(n) { return n.formId === '0x02'; })[0]) : null;
            this.check(loaded && loaded.hardPrereqs.join(',') === '0x01' && loaded.softPrereqs.length === 0,
                'Loading a damaged tree: the link is required only');
            this.check(loaded && loaded.softNeeded === 0, 'Loading a damaged tree: softNeeded fits the emptied optional list');
        } catch (e) {
            this.check(false, 'Classic output and load: ' + e.message);
        } finally {
            g.state = oldState;
            g.TREE_CONFIG = oldConfig;
        }
    },

    _editor: function(g) {
        var oldState = g.state, oldRenderer = g.CanvasRenderer;
        try {
            var shared = ['0x01'];
            var parent = { formId: '0x01', children: ['0x03'] };
            var other = { formId: '0x02', children: [] };
            // As a fresh build left it: one array behind two names
            var node = { formId: '0x03', prerequisites: shared, hardPrereqs: [], softPrereqs: shared, softNeeded: 1 };
            var lists = g.PrereqLists.normalize(node.hardPrereqs, node.softPrereqs);
            node.hardPrereqs = lists.hard;
            node.softPrereqs = lists.soft;
            var map = new Map([['0x01', parent], ['0x02', other], ['0x03', node]]);
            g.CanvasRenderer = { _nodeMap: map, nodes: [parent, other, node], edges: [] };
            g.state = { treeData: null, selectedNode: null };
            g.EditMode.undoStack = [];

            g.EditMode.addPrerequisite(other, node);
            this.check(this._count(node.hardPrereqs, '0x02') === 1 && this._count(node.softPrereqs, '0x02') === 0,
                'Pen link: required only, not optional too');

            g.EditMode.togglePrereqType(node, '0x02');
            this.check(this._count(node.hardPrereqs, '0x02') === 0 && this._count(node.softPrereqs, '0x02') === 1,
                'Required -> optional: once in the optional list');

            g.EditMode.togglePrereqType(node, '0x02');
            this.check(this._count(node.hardPrereqs, '0x02') === 1 && this._count(node.softPrereqs, '0x02') === 0,
                'Optional -> required: back in one list');

            g.EditMode.deletePrerequisite(node, '0x02');
            this.check(this._count(node.hardPrereqs, '0x02') + this._count(node.softPrereqs, '0x02') === 0 &&
                node.softPrereqs.join(',') === '0x01', 'Delete: only that link goes');

            // A required one made optional where nothing optional was asked for counts
            var lone = { formId: '0x04', prerequisites: ['0x01'], hardPrereqs: ['0x01'], softPrereqs: [], softNeeded: 0 };
            map.set('0x04', lone);
            g.EditMode.togglePrereqType(lone, '0x01');
            this.check(lone.softNeeded === 1, 'Made optional: softNeeded at least 1');
            g.EditMode.undo();
            this.check(lone.softNeeded === 0 && lone.hardPrereqs.join(',') === '0x01', 'Undo: softNeeded as it was');
            g.EditMode.togglePrereqType(lone, '0x01');
            g.EditMode.deletePrerequisite(lone, '0x01');
            this.check(lone.softNeeded === 0, 'Last optional deleted: softNeeded back to 0');
        } catch (e) {
            this.check(false, 'Editor: ' + e.message);
        } finally {
            g.state = oldState;
            g.CanvasRenderer = oldRenderer;
            g.EditMode.undoStack = [];
        }
    },

    _cppEntries: function(g) {
        var entries = g.PrereqLists.cppEntries([
            { formId: '0x01', hardPrereqs: [], softPrereqs: [], softNeeded: 0 },
            { formId: '0x02', hardPrereqs: ['0x01'], softPrereqs: [], softNeeded: 0, copies: ['0x07', '0x08'] }]);
        this.check(entries.length === 4, 'C++ entries: one per spell and one per copy');
        this.check(entries[2].formId === '0x07' && entries[2].hardPrereqs.join(',') === '0x01' &&
            entries[3].formId === '0x08', 'C++ entries: a copy asks for its spell\'s prerequisites');
        this.check(entries[2].copyOf === '0x02' && entries[0].copyOf === undefined, 'C++ entries: a copy names its tree spell');
    }
};

if (typeof module !== 'undefined' && module.exports) {
    module.exports = PrereqListsTest;
}
if (typeof window !== 'undefined') {
    window.PrereqListsTest = PrereqListsTest;
}
