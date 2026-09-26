/**
 * OpenRefreshGateTest - node tests for OpenRefreshGate (run-tests.js).
 *
 * A three-spell tree and a stand-in CanvasRenderer that counts repaints:
 * opening after nothing changed repaints nothing, a moved XP ring or a state
 * changed while closed repaints, a reply outside an opening refreshes as
 * always, and replies that never come are covered by the fallback repaint.
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
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.OpenRefreshGateTest = OpenRefreshGateTest;
if (typeof global !== 'undefined') global.OpenRefreshGateTest = OpenRefreshGateTest;
