/**
 * SpellSearch - which spells on the tree the Find Spell search lists, best first.
 *
 * Used by findSpell.js; pure apart from reading the globals below
 * (spellSearchTest.js covers it).
 *
 * A spell matches when the text is in its shown (translated) name, its school
 * or its formId, ignoring case and spaces: "헝거소환" finds "헝거 소환". Order:
 * an exact name match first, then names that start with the text, then every
 * other match; alphabetical by name within each group.
 *
 * Locked spells: in discovery mode (without cheat mode) they are left out
 * before anything is matched, so a search cannot give one away. Cheat mode
 * lists them all.
 *
 * Depends on (read when called, all optional): state, settings (state.js),
 * CanvasRenderer (the tree's nodes).
 *
 * Exports (global): SpellSearch
 */

var SpellSearch = {

    /** Most spells the list shows */
    LIMIT: 80,

    /** Groups: lower comes first */
    RANK_EXACT: 0,
    RANK_PREFIX: 1,
    RANK_OTHER: 2,

    /** Lower case, no spaces: the form both the text and the fields are compared in */
    fold: function(text) {
        return String(text || '').toLowerCase().replace(/\s+/g, '');
    },

    /** The nodes on the tree: the renderer's, else the loaded tree's raw data */
    treeNodes: function() {
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.nodes && CanvasRenderer.nodes.length > 0) {
            return CanvasRenderer.nodes;
        }
        var nodes = [];
        var raw = (typeof state !== 'undefined' && state.treeData && state.treeData.rawData) ? state.treeData.rawData : null;
        if (!raw || !raw.schools) return nodes;
        for (var schoolName in raw.schools) {
            if (!raw.schools.hasOwnProperty(schoolName)) continue;
            var list = raw.schools[schoolName].nodes || [];
            for (var i = 0; i < list.length; i++) {
                var n = list[i];
                nodes.push({
                    formId: n.formId || n.id || n.spellId,
                    name: n.name || n.spellName || n.formId,
                    school: schoolName,
                    level: n.level || n.skillLevel || '',
                    x: n.x,
                    y: n.y
                });
            }
        }
        return nodes;
    },

    /** Discovery mode without cheat mode: locked spells stay out of the list */
    hidesLocked: function() {
        var s = (typeof settings !== 'undefined' && settings) ? settings : {};
        return s.discoveryMode !== false && !s.cheatMode;
    },

    _byName: function(a, b) {
        var na = a.name.toLowerCase(), nb = b.name.toLowerCase();
        return na < nb ? -1 : na > nb ? 1 : 0;
    },

    /**
     * The spells the list shows for a text, best first.
     * @param {Array} nodes - tree nodes (treeNodes() when null)
     * @param {string} text - what was typed ('' lists every spell, alphabetical)
     * @param {number} [limit] - most results (LIMIT)
     * @returns {Array<{node, name, rank}>}
     */
    search: function(nodes, text, limit) {
        if (!nodes) nodes = this.treeNodes();
        var q = this.fold(text);
        var hideLocked = this.hidesLocked();
        var hits = [];
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            if (!node) continue;
            if (hideLocked && node.state === 'locked') continue;
            var name = String(node.name || node.formId || '');
            var rank = this.RANK_OTHER;
            if (q) {
                var fName = this.fold(name);
                if (fName === q) {
                    rank = this.RANK_EXACT;
                } else if (fName.indexOf(q) === 0) {
                    rank = this.RANK_PREFIX;
                } else if (fName.indexOf(q) === -1 &&
                           this.fold(node.school).indexOf(q) === -1 &&
                           this.fold(node.formId).indexOf(q) === -1) {
                    continue;
                }
            }
            hits.push({ node: node, name: name, rank: rank });
        }
        var self = this;
        hits.sort(function(a, b) {
            return a.rank !== b.rank ? a.rank - b.rank : self._byName(a, b);
        });
        return hits.slice(0, limit || this.LIMIT);
    }
};

window.SpellSearch = SpellSearch;
