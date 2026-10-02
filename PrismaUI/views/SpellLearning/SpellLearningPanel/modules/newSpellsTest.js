/**
 * NewSpellsTest - node tests for the footer's new spells and next goals
 * (newSpells.js, run-tests.js).
 *
 * Checks: nothing is new before the panel was closed once; a spell unlocked
 * between closing and opening is new, once; replies after the opening window
 * and edit-mode copies are not compared; seeing a card or "clear all" drops the
 * mark; a new tree resets it; the goals put learning targets first, then
 * learnable spells by XP, three at most; a goal's name hides like the card's;
 * the marks drawn are capped.
 *
 * Depends on: newSpells.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the globals `state`,
 * `settings`, `document`, `CanvasRenderer` and `setTimeout` while it runs.
 */

var NewSpellsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function' || typeof g.NewSpells !== 'undefined') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'newSpells.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    _node: function(formId, st, school, pct) {
        return { id: formId, formId: formId, name: 'Spell ' + formId, state: st, school: school || 'Destruction',
                 x: 0, y: 0, _pct: pct || 0 };
    },

    run: function() {
        console.log('[NewSpellsTest] new spells and next goals');
        var g = typeof global !== 'undefined' ? global : window;
        this._load(g);
        var N = g.NewSpells;
        var saved = { state: g.state, settings: g.settings, document: g.document,
                      CanvasRenderer: g.CanvasRenderer, setTimeout: g.setTimeout };
        var timers = 0;
        var frames = 0;
        try {
            g.setTimeout = function() { timers++; return 1; };
            g.document = { getElementById: function() { return null; } };
            g.settings = { revealName: 10, cheatMode: false, schoolVisibility: {} };
            g.CanvasRenderer = {
                zoom: 1, KNOWN_SIZE: 8,
                _getNodeProgressPct: function(n) { return n._pct; },
                _requestAnimationOnlyFrame: function() { frames++; },
                _worldToScreen: function(x, y) { return [x, y]; }
            };
            var a = this._node('0xA', 'unlocked');
            var b = this._node('0xB', 'available');
            var c = this._node('0xC', 'available');
            var copy = this._node('0xD', 'available');
            copy.originalFormId = '0xB';
            g.state = { treeData: { nodes: [a, b, c, copy] } };

            N.reset();
            b.state = 'unlocked';
            N.onShow();
            this.check(N.list().length === 0, 'nothing is new before the panel was closed once');

            N.onHide();
            c.state = 'unlocked';
            copy.state = 'unlocked';
            N.onShow();
            this.check(N.list().length === 1 && N.list()[0] === c, 'a spell unlocked while closed is new');
            this.check(N.isNew(c) && !N.isNew(copy), 'an edit-mode copy is not new on its own');
            this.check(timers === 1, 'its ring waits for the replies to settle');
            N.compare();
            this.check(N.list().length === 1, 'a second reply does not add it again');

            N.onHide();
            var e = this._node('0xE', 'unlocked');
            g.state.treeData.nodes.push(e);
            N._openUntil = 0;
            N.compare();
            this.check(!N.isNew(e), 'a reply outside the opening window is not compared');

            frames = 0;
            N.markSeen(c);
            this.check(!N.isNew(c) && frames === 1, 'seeing its card drops the mark (one animation frame)');
            N.onShow();
            this.check(N.isNew(e), 'next opening: the spell unlocked meanwhile is new');
            N.clearAll();
            this.check(N.list().length === 0, 'clear all');

            N.onHide();
            var f = this._node('0xF', 'unlocked');
            g.state.treeData.nodes.push(f);
            N.reset();
            N.onShow();
            this.check(N.list().length === 0, 'a new tree has nothing to compare with');

            // ----- goals -----
            var nodes = [
                this._node('L1', 'learning', 'Destruction', 0),
                this._node('A1', 'available', 'Destruction', 0.3),
                this._node('A2', 'available', 'Restoration', 0.8),
                this._node('A3', 'available', 'Restoration', 0.5),
                this._node('A4', 'available', 'Illusion', 0.95),
                this._node('A5', 'available', 'Destruction', 0),
                this._node('U1', 'unlocked', 'Destruction', 1),
                this._node('K1', 'locked', 'Destruction', 0.9),
                this._node('F1', 'available', 'Destruction', 1)
            ];
            g.state = { treeData: { nodes: nodes } };
            g.settings.schoolVisibility = { Illusion: false };
            var goals = N.goals().map(function(x) { return x.node.formId; });
            this.check(goals.join(',') === 'L1,A2,A3', 'goals: learning first, then most XP, three, hidden school out (' + goals.join(',') + ')');

            this.check(N.goalName(nodes[1], 0.05) === '???', 'a goal under the name threshold is ???');
            this.check(N.goalName(nodes[1], 0.3) === 'Spell A1', 'a goal past the threshold shows its name');
            this.check(N.goalName(nodes[0], 0) === 'Spell L1', 'a learning target always shows its name');
            g.settings.cheatMode = true;
            this.check(N.goalName(nodes[1], 0) === 'Spell A1', 'cheat mode shows every name');

            // ----- drawing: marks capped -----
            var fills = 0;
            var ctx = { save: function() {}, restore: function() {}, beginPath: function() {}, arc: function() {},
                        fill: function() { fills++; }, stroke: function() {} };
            N.reset();
            N._known = {};
            N._openUntil = Infinity;
            var many = [];
            for (var i = 0; i < 7; i++) many.push(this._node('M' + i, 'unlocked'));
            g.state = { treeData: { nodes: many } };
            N.compare();
            N._bursts = [];
            N.drawSpots(g.CanvasRenderer, {}, 1, ctx);
            this.check(fills === N.MAX_MARKS, 'marks drawn: ' + fills + ' of 7 (MAX_MARKS ' + N.MAX_MARKS + ')');
        } finally {
            g.state = saved.state;
            g.settings = saved.settings;
            g.document = saved.document;
            g.CanvasRenderer = saved.CanvasRenderer;
            g.setTimeout = saved.setTimeout;
            if (N) N.reset();
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.NewSpellsTest = NewSpellsTest;
if (typeof global !== 'undefined') global.NewSpellsTest = NewSpellsTest;
