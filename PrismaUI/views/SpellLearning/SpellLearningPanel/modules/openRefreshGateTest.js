/**
 * OpenRefreshGateTest - node tests for OpenRefreshGate (run-tests.js).
 *
 * A three-spell tree and a stand-in CanvasRenderer that counts repaints:
 * opening after nothing changed repaints nothing, a moved XP ring or a state
 * changed while closed repaints, a reply outside an opening refreshes as
 * always, and replies that never come are covered by the fallback repaint.
 * Then the hidden-panel pushes (onResetTreeStates, onLearningTargetSet) with the
 * real handlers: no drawing while hidden, at most one repaint on opening.
 *
 * Depends on: OpenRefreshGate (swaps the globals `state` and `CanvasRenderer`
 * for its own while it runs)
 */

var OpenRefreshGateTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var G = g.OpenRefreshGate || (typeof window !== 'undefined' && window.OpenRefreshGate);
        if (!G) { this.check(false, 'OpenRefreshGate loaded'); return { passed: this.passed, failed: this.failed }; }
        var oldState = g.state, oldRenderer = g.CanvasRenderer;
        var repaints = 0;
        var nodes = [{ state: 'unlocked' }, { state: 'available' }, { state: 'locked' }];
        g.state = { treeData: { nodes: nodes } };
        g.CanvasRenderer = {
            _getNodeProgressPct: function(n) { return n.pct || 0; },
            set _needsRender(v) { if (v) repaints++; }
        };
        G._hideSig = null;
        try {
            this.check(G.hold() === false, 'never closed: repaints as always');
            G.onHide();
            this.check(G.hold() === true, 'closed before: the repaint waits for the replies');
            this.check(G.reply() === false && G.reply() === false, 'nothing changed: neither reply repaints');
            this.check(G.reply() === true, 'a reply outside an opening refreshes as always');
            G.onHide(); G.hold();
            nodes[1].pct = 0.5;
            this.check(G.reply() === true, 'an XP ring moved: repaints');
            this.check(G.reply() === false, 'the second reply, same as the first: no second repaint');
            G.onHide(); G.hold();
            nodes[2].state = 'available';
            this.check(G.reply() === true, 'a state changed while closed: repaints');
            G.reply();
            G.onHide(); G.hold();
            G._release(true);    // what the WAIT_MS timer does when no reply comes
            this.check(repaints === 1 && G.reply() === true, 'no replies: repainted anyway, and the gate is open again');
        } finally {
            G._release(false);
            G._hideSig = null;
            g.state = oldState;
            g.CanvasRenderer = oldRenderer;
        }
        this.runHidden(g, G);
        return { passed: this.passed, failed: this.failed };
    },

    /**
     * The game's pushes while the panel is hidden (cppCallbacks.js onResetTreeStates
     * on a save load, progressionUI.js onLearningTargetSet on a tome read) change
     * the states but draw nothing; onPanelShowing draws nothing at once, and of
     * its two replies exactly one repaints when the tree changed.
     */
    runHidden: function(g, G) {
        if (typeof require !== 'function') return;   // browser runner: node only
        try {
            if (typeof g.onResetTreeStates !== 'function') require('./cppCallbacks.js');
            if (typeof g.onLearningTargetSet !== 'function') require('./progressionUI.js');
        } catch (e) {
            this.check(false, 'cppCallbacks.js and progressionUI.js load (' + e.message + ')');
            return;
        }
        // Everything the handlers touch, swapped for stand-ins and put back after
        var names = ['state', 'CanvasRenderer', 'WheelRenderer', 'SmartRenderer', 'TrustedRenderer', 'callCpp',
            'setTreeStatusKey', 'NewSpells', 'TreeStyle', 'TreeGrowth', 'TreePreview', 'PreReqMaster',
            '_panelVisible', '_panelShownOnce'];
        var saved = {}, had = {}, i;
        for (i = 0; i < names.length; i++) { had[names[i]] = names[i] in g; saved[names[i]] = g[names[i]]; delete g[names[i]]; }
        var draws = 0, repaints = 0, asked = [];
        var nodes = [
            { formId: '0xA', state: 'unlocked', prerequisites: [] },
            { formId: '0xB', state: 'available', prerequisites: ['0xA'] },
            { formId: '0xC', state: 'locked', prerequisites: ['0xB'] }
        ];
        g.state = { treeData: { nodes: nodes }, spellProgress: {}, learningTargets: {},
            playerKnownSpells: new Set(), selectedNode: null };
        g.WheelRenderer = { render: function() { draws++; }, updateNodeStates: function() { draws++; } };
        g.CanvasRenderer = {
            startRenderLoop: function() {},
            _getNodeProgressPct: function() { return 0; },
            set _needsRender(v) { if (v) repaints++; }
        };
        g.callCpp = function(name) { asked.push(name); };
        g.setTreeStatusKey = function() {};
        G._hideSig = null;
        try {
            // Save loaded while closed
            g._panelVisible = false;
            G.onHide();
            g.onResetTreeStates();
            this.check(draws === 0 && repaints === 0, 'hidden save load: no redraw');
            this.check(nodes[0].state === 'available' && nodes[2].state === 'locked', 'hidden save load: the states are still reset');
            g.onPanelShowing();
            this.check(draws === 0 && repaints === 0 && asked.join() === 'GetProgress,GetPlayerKnownSpells',
                'opening after it: nothing drawn at once, progress and known spells asked for');
            nodes[0].state = 'unlocked'; nodes[1].state = 'available';    // what the replies bring back
            this.check(G.reply() === false && G.reply() === false, 'replies back to what the tree showed: no repaint');

            // A tome read while closed
            g._panelVisible = false;
            G.onHide();
            g.onLearningTargetSet(JSON.stringify({ school: 'Alteration', formId: '0xC', spellName: 'C' }));
            this.check(draws === 0 && repaints === 0, 'hidden tome read: no redraw');
            this.check(g.state.learningTargets.Alteration === '0xC' && nodes[2].state === 'available' &&
                !!g.state.spellProgress['0xC'], 'hidden tome read: target, progress and state updated at once');
            g.onPanelShowing();
            this.check(draws === 0 && repaints === 0, 'opening after it: nothing drawn at once');
            var first = G.reply(), second = G.reply();
            this.check(first === true && second === false, 'the tree changed: exactly one reply repaints');

            // Shown: the handler draws at once, as before
            g.onLearningTargetSet(JSON.stringify({ school: 'Alteration', formId: '0xB', spellName: 'B' }));
            this.check(draws === 1, 'shown tome read: redrawn at once');
        } catch (e) {
            this.check(false, 'hidden handlers threw: ' + (e && e.message ? e.message : e));
        } finally {
            G._release(false);
            G._hideSig = null;
            for (i = 0; i < names.length; i++) {
                if (had[names[i]]) g[names[i]] = saved[names[i]]; else delete g[names[i]];
            }
        }
    }
};

if (typeof window !== 'undefined') window.OpenRefreshGateTest = OpenRefreshGateTest;
if (typeof global !== 'undefined') global.OpenRefreshGateTest = OpenRefreshGateTest;
