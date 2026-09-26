/**
 * OpenRefreshGate - opening the panel repaints the tree only if something changed.
 *
 * Opening the panel asked for the whole tree to be drawn again up to three
 * times, on three different frames: once as it opened (onPanelShowing), once
 * when the progress came back from the game (onProgressData, whenever there
 * are learning targets) and once when the known spells came back
 * (onPlayerKnownSpells). In the game each full repaint of a big tree is a
 * frame of 100 ms or more, and most of the time nothing had changed since the
 * panel closed.
 *
 * Now: when the panel closes, what the tree shows is noted (signature: every
 * spell's state, and the XP ring of the ones being learned or learnable). As
 * it opens with both replies on their way, the repaint waits; each reply,
 * after it has updated the states, asks reply() and the tree is drawn again
 * only if the note no longer matches. What changed while the panel was closed
 * (updateSpellState and onSpellUnlocked keep the state but skip the drawing)
 * shows up as a mismatch too. If the replies do not both come within WAIT_MS,
 * the tree is drawn again anyway.
 *
 * Depends on: state (state.js), CanvasRenderer (_getNodeProgressPct, _needsRender)
 */

var OpenRefreshGate = {

    WAIT_MS: 2000,          // replies later than this: repaint anyway
    RING_STEPS: 1000,       // an XP ring counts as changed at a tenth of a percent

    _hideSig: null,         // the tree as it was when the panel closed (null: never closed)
    _pending: 0,            // replies still to come for this opening
    _timer: null,

    /** What the tree layer shows, as a string. */
    signature: function() {
        if (typeof state === 'undefined' || !state.treeData || !state.treeData.nodes) return '';
        var nodes = state.treeData.nodes, parts = [];
        var ring = typeof CanvasRenderer !== 'undefined' && CanvasRenderer._getNodeProgressPct;
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i], s = n.state;
            if (ring && (s === 'available' || s === 'learning')) {
                s += Math.round(CanvasRenderer._getNodeProgressPct(n) * this.RING_STEPS);
            }
            parts.push(s);
        }
        return nodes.length + ':' + parts.join(',');
    },

    /** onPanelHiding: note what the tree shows. */
    onHide: function() {
        this._release(false);
        this._hideSig = this.signature();
    },

    /**
     * onPanelShowing, with GetProgress and GetPlayerKnownSpells just asked for:
     * true = do not repaint now, the replies decide. False (the panel never
     * closed before: the tree was never drawn) = repaint as always.
     */
    hold: function() {
        if (this._hideSig === null) return false;
        this._release(false);
        this._pending = 2;
        var self = this;
        this._timer = setTimeout(function() { self._release(true); }, this.WAIT_MS);
        return true;
    },

    /**
     * A reply has updated the states: should the tree be drawn again? Always
     * true outside an opening (the reply came for another reason).
     */
    reply: function() {
        if (!this._pending) return true;
        this._pending--;
        var sig = this.signature();
        var changed = sig !== this._hideSig;
        // The other reply is measured against what this one has drawn
        this._hideSig = sig;
        if (!this._pending) this._release(false);
        return changed;
    },

    _release: function(repaint) {
        if (this._timer) clearTimeout(this._timer);
        this._timer = null;
        this._pending = 0;
        if (repaint && typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
    }
};

window.OpenRefreshGate = OpenRefreshGate;
