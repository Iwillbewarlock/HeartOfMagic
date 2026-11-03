/**
 * SpellCardMastery - the spell card's mastery rank row (ranks past mastery)
 *
 * A mastered spell keeps earning XP when the player casts it, toward the mastery ranks C++ counts
 * (ProgressionManagerMastery.cpp, MasteryMath.h). Under the state line of a mastered spell's card:
 * "Mastery rank 1/3" and a thin bar with the XP toward the next rank (full at the last rank).
 * A spell still being learned shows no row; nor does any spell while the ranks are switched off
 * (config.json mastery.enabled false) or when C++ sent no mastery data (an older DLL).
 *
 * The data: state.spellMastery (formId -> { xp, rank, ranks, floor, next }, the progress reply's
 * spellMastery and each progress update's `mastery`) and state.masteryInfo ({ enabled, ranks }, the
 * progress reply's `mastery`). A mastered spell with no mastery XP yet has no entry: rank 0 of
 * masteryInfo.ranks, an empty bar.
 *
 * view() takes everything it reads as arguments: tested under node (spellCardMasteryTest.js).
 * draw() is called by SpellCardNeeds.render, so the row follows every card redraw.
 *
 * Depends on: state, getCanonicalFormId (treeViewerUI.js), setI18n (i18n.js).
 */
var SpellCardMastery = {
    _doc: function() { return document; },
    _el: function(id) { return this._doc().getElementById(id); },

    /**
     * What the row shows, or null for no row.
     * @param {string} nodeState - the node's state ('unlocked' = mastered)
     * @param {?{xp:number, rank:number, ranks:number, floor:number, next:number}} entry
     * @param {?{enabled:boolean, ranks:number}} info
     * @returns {?{rank:number, ranks:number, pct:number}}
     */
    view: function(nodeState, entry, info) {
        if (nodeState !== 'unlocked') return null;
        if (!info || info.enabled === false) return null;
        var ranks = (entry && entry.ranks > 0) ? entry.ranks : (info.ranks > 0 ? info.ranks : 0);
        if (!(ranks > 0)) return null;
        var rank = (entry && entry.rank > 0) ? Math.min(Math.floor(entry.rank), ranks) : 0;
        var fraction = 0;
        if (entry && rank >= ranks) {
            fraction = 1;
        } else if (entry && typeof entry.xp === 'number' && entry.next > entry.floor) {
            fraction = (entry.xp - entry.floor) / (entry.next - entry.floor);
            fraction = Math.max(0, Math.min(1, fraction));
        }
        return { rank: rank, ranks: ranks, pct: Math.round(fraction * 100) };
    },

    /**
     * Redraw the row of the card on screen now: the hover peek's spell while one is previewed
     * (DetailsPeek), else the selected spell. With a canonical id, only when that card is that spell's:
     * an update for the selected spell must not draw its rank into another spell's preview.
     * @param {string} [canonId]
     */
    redrawShown: function(canonId) {
        if (typeof state === 'undefined' || !state) return;
        var peeking = typeof DetailsPeek !== 'undefined' && DetailsPeek.isPeeking();
        var shown = peeking ? DetailsPeek.peekNode : state.selectedNode;
        if (!shown) return;
        var canon = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(shown) : shown.formId;
        if (canonId !== undefined && canon !== canonId) return;
        this.draw(shown);
    },

    /** The row for this node: shown with its rank and bar, or hidden. */
    draw: function(node) {
        var row = this._el('spell-mastery');
        if (!row) return;
        var v = null;
        if (node && typeof state !== 'undefined' && state) {
            var canon = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
            var all = state.spellMastery || {};
            var entry = all[canon] || all[node.formId] || null;
            v = this.view(node.state, entry, state.masteryInfo || null);
        }
        if (!v) {
            row.classList.add('hidden');
            return;
        }
        row.classList.remove('hidden');
        var text = this._el('spell-mastery-text');
        if (text) setI18n(text, 'details.masteryRank', { rank: v.rank, ranks: v.ranks }, 'Mastery rank {{rank}}/{{ranks}}');
        var fill = this._el('spell-mastery-fill');
        if (fill) fill.style.width = v.pct + '%';
    }
};

window.SpellCardMastery = SpellCardMastery;
