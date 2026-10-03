/**
 * PrereqEditTest - node tests for editing a spell's prerequisites (run-tests.js)
 *
 * The 1.2.0 report: a link made in edit mode showed as both Required and
 * Optional, deleting one deleted both, and switching one between Required and
 * Optional made a second entry. Checks PrereqEdit's rules (an id once, in one
 * of hard and soft; `prerequisites` the drawn lines), EditMode's link, delete,
 * eraser, toggle and the undo of each on a stand-in tree, the repair of a tree
 * saved by 1.2.0, deleting a spell (no reference left in the saved tree), and
 * that ClassicTreeOutput gives every list its own array.
 *
 * Depends on: prereqEdit.js, editMode.js, classic/classicTreeOutput.js - under
 * node this file runs them into the global scope the way the page's script
 * tags do. Swaps the globals `state`, `saveTreeToFile` and CanvasRenderer's `_nodeMap`, `nodes`
 * and `edges` for its own while it runs.
 */

var PrereqEditTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['prereqEdit.js', 'PrereqEdit'], ['editMode.js', 'EditMode'],
                     ['classic/classicTreeOutput.js', 'ClassicTreeOutput']];
        // editMode.js waits for the page's load event; node has none
        var hadListener = typeof g.addEventListener === 'function';
        if (!hadListener) g.addEventListener = function() {};
        try {
            for (var i = 0; i < files.length; i++) {
                if (typeof g[files[i][1]] !== 'undefined') continue;
                var file = path.join(__dirname, files[i][0]);
                vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
            }
        } finally {
            if (!hadListener) delete g.addEventListener;
        }
    },

    _same: function(list, ids) {
        return (list || []).join() === ids.join();
    },

    /** P (parent, Optional), X (to be linked), S (the spell) */
    _tree: function() {
        var p = { formId: 'P', children: ['S'] };
        var x = { formId: 'X', children: [] };
        var s = { formId: 'S', children: [], prerequisites: ['P'], hardPrereqs: [], softPrereqs: ['P'], softNeeded: 1 };
        return { P: p, X: x, S: s };
    },

    _unit: function() {
        var PE = PrereqEdit;
        var n = { prerequisites: ['P'], hardPrereqs: [], softPrereqs: ['P'], softNeeded: 1 };
        this.check(PE.add(n, 'X') && this._same(n.hardPrereqs, ['X']) && this._same(n.softPrereqs, ['P']) &&
            this._same(n.prerequisites, ['P', 'X']), 'add: drawn and Required once, Optional untouched');
        this.check(!PE.add(n, 'X') && !PE.add(n, 'P') && n.prerequisites.length === 2, 'add: an existing prerequisite is refused');

        var t = PE.toggle(n, 'X');
        this.check(t.type === 'soft' && t.wasType === 'hard' && this._same(n.hardPrereqs, []) &&
            this._same(n.softPrereqs, ['P', 'X']) && this._same(n.prerequisites, ['P', 'X']), 'toggle H->S: moved, no repeat');
        t = PE.toggle(n, 'X');
        this.check(t.type === 'hard' && this._same(n.hardPrereqs, ['X']) && this._same(n.softPrereqs, ['P']),
            'toggle S->H: moved back, no repeat');
        t = PE.toggle(n, 'P');
        this.check(n.softNeeded === 0 && this._same(n.softPrereqs, []) && this._same(n.hardPrereqs, ['X', 'P']),
            'toggle of the last Optional: softNeeded held to the soft list');
        PE.restore(n, 'P', { type: t.wasType, linked: true, softNeeded: t.softNeeded });
        this.check(n.softNeeded === 1 && this._same(n.softPrereqs, ['P']) && this._same(n.hardPrereqs, ['X']),
            'restore: type and softNeeded back');

        var was = PE.remove(n, 'X');
        this.check(was.type === 'hard' && was.linked && this._same(n.hardPrereqs, []) && this._same(n.softPrereqs, ['P']) &&
            this._same(n.prerequisites, ['P']), 'remove: only that one, from every list');
        PE.restore(n, 'X', was);
        this.check(this._same(n.hardPrereqs, ['X']) && this._same(n.prerequisites, ['P', 'X']), 'restore after remove');

        // A bridge (soft, not drawn) taken out and put back stays undrawn
        var b = { prerequisites: [], hardPrereqs: [], softPrereqs: ['B'], softNeeded: 1 };
        was = PE.remove(b, 'B');
        PE.restore(b, 'B', was);
        this.check(!was.linked && b.prerequisites.length === 0 && this._same(b.softPrereqs, ['B']) && b.softNeeded === 1,
            'remove/restore of an undrawn bridge does not draw it');

        // A 1.2.0 tree: the link in both lists, toggles made repeats
        var shared = ['P', 'X', 'X'];
        var bad = { prerequisites: shared, hardPrereqs: ['X', 'X'], softPrereqs: shared, softNeeded: 2 };
        var dropped = PE.repair(bad);
        this.check(this._same(bad.hardPrereqs, ['X']) && this._same(bad.softPrereqs, ['P']) &&
            this._same(bad.prerequisites, ['P', 'X']) && bad.softNeeded === 1 && dropped === 4 &&
            bad.prerequisites !== bad.softPrereqs, 'repair: hard keeps it, repeats out, own arrays, softNeeded held');
        var good = { prerequisites: ['P'], hardPrereqs: [], softPrereqs: ['P'], softNeeded: 1 };
        this.check(PE.repair(good) === 0 && good.softNeeded === 1 && this._same(good.softPrereqs, ['P']), 'repair: a clean node unchanged');

        // A PreReq Master lock on the layout parent: hard as the lock, soft as the
        // line, and it met softNeeded. Out of soft, the bridge B stays optional.
        var locked = { prerequisites: ['P'], hardPrereqs: ['P'], softPrereqs: ['P', 'B'], softNeeded: 1, locks: [{ nodeId: 'P' }] };
        this.check(PE.repair(locked) === 1 && this._same(locked.hardPrereqs, ['P']) && this._same(locked.softPrereqs, ['B']) &&
            locked.softNeeded === 0, 'repair: a lock on the parent lowers softNeeded, the bridge stays optional');
        var lockedOnly = { prerequisites: ['P'], hardPrereqs: ['P'], softPrereqs: ['P'], softNeeded: 1 };
        PE.repair(lockedOnly, ['P']);
        this.check(this._same(lockedOnly.softPrereqs, []) && lockedOnly.softNeeded === 0, 'repair: lock ids passed in, soft emptied, need 0');
        var twoNeeded = { prerequisites: ['P', 'Q'], hardPrereqs: ['P'], softPrereqs: ['P', 'Q', 'B'], softNeeded: 2, locks: [{ nodeId: 'P' }] };
        PE.repair(twoNeeded);
        this.check(this._same(twoNeeded.softPrereqs, ['Q', 'B']) && twoNeeded.softNeeded === 1, 'repair: need 2 with the lock is need 1 without it');
        var notLock = { prerequisites: ['P', 'X'], hardPrereqs: ['X'], softPrereqs: ['P', 'X', 'B'], softNeeded: 1, locks: [{ nodeId: 'P' }] };
        PE.repair(notLock);
        this.check(this._same(notLock.softPrereqs, ['P', 'B']) && notLock.softNeeded === 1, 'repair: a 1.2.0 edit duplicate keeps softNeeded');

        // Into soft with nothing needed: needs 1, or the prerequisite would be gone
        var hs = { prerequisites: ['X'], hardPrereqs: ['X'], softPrereqs: [], softNeeded: 0 };
        PE.toggle(hs, 'X');
        var first = hs.softNeeded;
        PE.toggle(hs, 'X');
        var back = hs.softNeeded;
        PE.toggle(hs, 'X');
        this.check(first === 1 && back === 0 && hs.softNeeded === 1 && this._same(hs.softPrereqs, ['X']),
            'toggle H->S->H->S: softNeeded 1 each time it is soft');
        var fresh = { prerequisites: [], hardPrereqs: [], softPrereqs: [], softNeeded: 0 };
        PE.add(fresh, 'X');
        PE.toggle(fresh, 'X');
        this.check(fresh.softNeeded === 1 && this._same(fresh.softPrereqs, ['X']), 'a new Required link toggled to S needs 1');
        var softAdd = { prerequisites: [], hardPrereqs: [], softPrereqs: [], softNeeded: 0 };
        PE.add(softAdd, 'X', true);
        this.check(softAdd.softNeeded === 1, 'add as soft to an empty soft list needs 1');
        var undoable = { prerequisites: ['X'], hardPrereqs: ['X'], softPrereqs: [], softNeeded: 0 };
        var tt = PE.toggle(undoable, 'X');
        PE.restore(undoable, 'X', { type: tt.wasType, linked: true, softNeeded: tt.softNeeded });
        this.check(undoable.softNeeded === 0 && this._same(undoable.hardPrereqs, ['X']), 'undo of H->S puts softNeeded 0 back');

        // copyTo: copies, not the node's arrays
        var raw = {};
        PE.copyTo(raw, locked);
        this.check(this._same(raw.softPrereqs, ['B']) && raw.softNeeded === 0 && raw.softPrereqs !== locked.softPrereqs &&
            raw.hardPrereqs !== locked.hardPrereqs, 'copyTo: the lists as copies');
    },

    _editMode: function(g) {
        var t = this._tree();
        var map = new Map([['P', t.P], ['X', t.X], ['S', t.S]]);
        CanvasRenderer._nodeMap = map;
        CanvasRenderer.nodes = [t.P, t.X, t.S];
        CanvasRenderer.edges = [{ from: 'P', to: 'S' }];
        g.state = { selectedNode: null, treeData: null };
        EditMode.undoStack = [];
        var s = t.S;
        var self = this;
        var lines = function(from, to) {
            return CanvasRenderer.edges.filter(function(e) { return e.from === from && e.to === to; }).length;
        };

        // Link X -> S
        var added = EditMode.addPrerequisite(t.X, s);
        this.check(added && this._same(s.hardPrereqs, ['X']) && this._same(s.softPrereqs, ['P']) &&
            this._same(s.prerequisites, ['P', 'X']) && this._same(t.X.children, ['S']) && lines('X', 'S') === 1,
            'EditMode link: Required only, one line, one child entry');
        this.check(!EditMode.addPrerequisite(t.X, s) && lines('X', 'S') === 1, 'EditMode link twice: refused');

        // Toggle both ways, undo each
        EditMode.togglePrereqType(s, 'X');
        this.check(this._same(s.hardPrereqs, []) && this._same(s.softPrereqs, ['P', 'X']), 'EditMode toggle H->S');
        EditMode.togglePrereqType(s, 'X');
        this.check(this._same(s.hardPrereqs, ['X']) && this._same(s.softPrereqs, ['P']), 'EditMode toggle S->H');
        EditMode.undo();
        this.check(this._same(s.hardPrereqs, []) && this._same(s.softPrereqs, ['P', 'X']), 'undo toggle S->H');
        EditMode.undo();
        this.check(this._same(s.hardPrereqs, ['X']) && this._same(s.softPrereqs, ['P']), 'undo toggle H->S');

        // Toggle the last Optional to Required, undo restores softNeeded
        EditMode.togglePrereqType(s, 'P');
        this.check(s.softNeeded === 0 && this._same(s.hardPrereqs, ['X', 'P']), 'EditMode toggle the only Optional: softNeeded 0');
        EditMode.undo();
        this.check(s.softNeeded === 1 && this._same(s.softPrereqs, ['P']) && this._same(s.hardPrereqs, ['X']),
            'undo puts softNeeded back');

        // Delete the Required one: the Optional one stays
        EditMode.deletePrerequisite(s, 'X');
        this.check(this._same(s.hardPrereqs, []) && this._same(s.softPrereqs, ['P']) && this._same(s.prerequisites, ['P']) &&
            lines('X', 'S') === 0 && lines('P', 'S') === 1 && t.X.children.length === 0,
            'EditMode delete: only that prerequisite and its line');
        EditMode.undo();
        this.check(this._same(s.hardPrereqs, ['X']) && this._same(s.softPrereqs, ['P']) && this._same(s.prerequisites, ['P', 'X']) &&
            lines('X', 'S') === 1, 'undo delete: back as Required, line back');

        // Eraser on the Optional line, undo
        EditMode.removeEdge({ from: 'P', to: 'S' });
        this.check(this._same(s.softPrereqs, []) && s.softNeeded === 0 && this._same(s.hardPrereqs, ['X']) && lines('P', 'S') === 0,
            'EditMode eraser: only that line');
        EditMode.undo();
        this.check(this._same(s.softPrereqs, ['P']) && s.softNeeded === 1 && lines('P', 'S') === 1, 'undo eraser');

        // Undo the link itself
        EditMode.undo();
        this.check(this._same(s.hardPrereqs, []) && this._same(s.prerequisites, ['P']) && lines('X', 'S') === 0 &&
            t.X.children.length === 0, 'undo link');
        self.check(s.prerequisites !== s.softPrereqs && s.prerequisites !== s.hardPrereqs, 'EditMode: lists stay separate arrays');
    },

    /**
     * Deleting a spell: every reference to it leaves the saved tree, which has its
     * own arrays (treeLoad copies them), so nothing waits on it after a reload.
     * D is deleted; C its child (Required), L locked on it (PreReq Master, hard,
     * not a child), B a bridge target in another school (soft, not a child).
     */
    _deleteNode: function(g) {
        var D = '0x0000DEAD';
        var raw = { schools: {
            Destruction: { root: 'R', nodes: [
                { formId: 'R', children: [D, 'L'], prerequisites: [], hardPrereqs: [], softPrereqs: [], softNeeded: 0 },
                { formId: D, children: ['C'], prerequisites: ['R'], hardPrereqs: ['R'], softPrereqs: [], softNeeded: 0 },
                { formId: 'C', children: [], prerequisites: [D], hardPrereqs: [D], softPrereqs: [], softNeeded: 0 },
                { formId: 'L', children: [], prerequisites: ['R'], hardPrereqs: [D], softPrereqs: ['R'], softNeeded: 1,
                  locks: [{ nodeId: D, revealed: false }] }
            ] },
            Conjuration: { root: 'B', nodes: [
                { formId: 'B', children: [], prerequisites: [], hardPrereqs: [], softPrereqs: [D], softNeeded: 1 }
            ] }
        }, bridges: [{ from: D, to: 'B' }, { from: 'R', to: 'B' }] };
        // The panel's nodes: copies of the saved lists, no locks (treeLoad.js)
        var nodes = [];
        for (var sn in raw.schools) {
            raw.schools[sn].nodes.forEach(function(nd) {
                nodes.push({ formId: nd.formId, id: nd.formId, school: sn, children: nd.children.slice(),
                    prerequisites: nd.prerequisites.slice(), hardPrereqs: nd.hardPrereqs.slice(),
                    softPrereqs: nd.softPrereqs.slice(), softNeeded: nd.softNeeded });
            });
        }
        var map = new Map();
        nodes.forEach(function(n) { map.set(n.formId, n); });
        CanvasRenderer._nodeMap = map;
        CanvasRenderer.nodes = nodes.slice();
        CanvasRenderer.edges = [{ from: 'R', to: D }, { from: D, to: 'C' }, { from: 'R', to: 'L' }];
        g.state = { selectedNode: null, treeData: { rawData: raw, nodes: nodes.slice() } };
        var hadIndex = CanvasRenderer.buildSpatialIndex;
        CanvasRenderer.buildSpatialIndex = function() {};
        try {
            EditMode.deleteNode(map.get(D));
        } finally {
            CanvasRenderer.buildSpatialIndex = hadIndex;
        }
        var saved = JSON.stringify(raw);
        this.check(saved.indexOf(D) === -1, 'delete node: the saved tree names it nowhere (reload finds no reference)');
        var rawOf = function(id) {
            for (var s in raw.schools) {
                var ns = raw.schools[s].nodes;
                for (var i = 0; i < ns.length; i++) if (ns[i].formId === id) return ns[i];
            }
            return null;
        };
        var c = rawOf('C'), l = rawOf('L'), b = rawOf('B'), r = rawOf('R');
        this.check(!rawOf(D) && this._same(c.hardPrereqs, []) && this._same(c.prerequisites, []) && this._same(r.children, ['L']),
            'delete node: child and parent cleaned in the saved tree');
        this.check(this._same(l.hardPrereqs, []) && l.locks.length === 0 && this._same(l.softPrereqs, ['R']) && l.softNeeded === 1,
            'delete node: a lock on it gone, the rest kept');
        this.check(this._same(b.softPrereqs, []) && b.softNeeded === 0 && raw.bridges.length === 1 && raw.bridges[0].from === 'R',
            'delete node: a bridge from it gone (soft list and saved bridges)');
        this.check(this._same(map.get('C').hardPrereqs, []) && this._same(map.get('L').hardPrereqs, []) &&
            this._same(map.get('R').children, ['L']) && CanvasRenderer.nodes.length === 4 && CanvasRenderer.edges.length === 1,
            'delete node: the panel nodes, edges and list match');
    },

    _classicOutput: function() {
        var treeData = { schools: { Destruction: { root: 'R', nodes: [
            { formId: 'R', tier: 1 }, { formId: 'A', tier: 2 }, { formId: 'B', tier: 2, locks: [{ nodeId: 'A' }] }
        ] } } };
        var layoutData = { schools: { Destruction: { nodes: [
            { formId: 'R', x: 0, y: 0 }, { formId: 'A', x: 1, y: 1, parentFormId: 'R' }, { formId: 'B', x: 2, y: 2, parentFormId: 'R' }
        ] } } };
        var out = ClassicTreeOutput.build(treeData, layoutData, null).output;
        var nodes = out.schools.Destruction.nodes;
        var shared = false, i, j;
        var lists = [];
        for (i = 0; i < nodes.length; i++) {
            lists.push(nodes[i].children, nodes[i].prerequisites, nodes[i].hardPrereqs, nodes[i].softPrereqs);
        }
        for (i = 0; i < lists.length; i++) {
            for (j = i + 1; j < lists.length; j++) {
                if (lists[i] && lists[i] === lists[j]) shared = true;
            }
        }
        var b = nodes[2];
        this.check(!shared && this._same(b.prerequisites, ['R']) && this._same(b.softPrereqs, ['R']) && this._same(b.hardPrereqs, ['A']),
            'ClassicTreeOutput: no two lists share an array');
        b.prerequisites.push('Z');
        this.check(this._same(b.softPrereqs, ['R']), 'ClassicTreeOutput: a push to prerequisites leaves softPrereqs alone');
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.PrereqEdit === 'undefined' || typeof g.EditMode === 'undefined' || typeof g.ClassicTreeOutput === 'undefined') {
            this.check(false, 'prereqEdit.js, editMode.js and classicTreeOutput.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var old = {
            state: g.state, nodeMap: CanvasRenderer._nodeMap, nodes: CanvasRenderer.nodes, edges: CanvasRenderer.edges,
            log: console.log, save: g.saveTreeToFile
        };
        g.saveTreeToFile = function() { return false; };
        var self = this;
        // The modules' own log lines would bury the results
        var quiet = function() {
            var msg = arguments[0];
            if (typeof msg === 'string' && msg.indexOf('  [') === 0) old.log.apply(console, arguments);
        };
        console.log = quiet;
        try {
            this._unit();
            this._editMode(g);
            this._deleteNode(g);
            this._classicOutput();
        } finally {
            console.log = old.log;
            g.state = old.state;
            g.saveTreeToFile = old.save;
            CanvasRenderer._nodeMap = old.nodeMap;
            CanvasRenderer.nodes = old.nodes;
            CanvasRenderer.edges = old.edges;
            EditMode.undoStack = [];
        }
        return { passed: self.passed, failed: self.failed };
    }
};

if (typeof window !== 'undefined') window.PrereqEditTest = PrereqEditTest;
if (typeof global !== 'undefined') global.PrereqEditTest = PrereqEditTest;
