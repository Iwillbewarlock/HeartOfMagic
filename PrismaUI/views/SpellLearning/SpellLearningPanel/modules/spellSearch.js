/**
 * SpellSearch - which spells on the tree the Find Spell search lists, best first.
 *
 * Used by findSpell.js; pure apart from reading the globals below
 * (spellSearchTest.js covers it).
 *
 * A spell matches when the text is in its shown (translated) name or its
 * school - the English name the tree keys it by or the school's name in the
 * panel's language (chips.school.*) - ignoring case and spaces: "헝거소환"
 * finds "헝거 소환". Its formId matches only for a text that looks like one
 * (starts with 0x, or 4+ hex digits), so a short text such as "fe" does not
 * list every ESL spell (0xFE...). Order: an exact name match first, then names
 * that start with the text, then the other name or school matches, then the
 * formId matches; alphabetical by name within each group.
 *
 * Locked spells: in discovery mode (without cheat mode) they are left out
 * before anything is matched, so a search cannot give one away. Cheat mode
 * lists them all. The raw tree data (used before the renderer has its nodes)
 * has no spell states, so in that mode it lists nothing rather than guess.
 *
 * Depends on (read when called, all optional): state, settings (state.js),
 * CanvasRenderer (the tree's nodes), t (i18n.js).
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
    RANK_FORMID: 3,

    /** Hex digits a text needs (without 0x) before it is matched against formIds */
    FORMID_MIN_DIGITS: 4,

    /** Lower case, no spaces: the form both the text and the fields are compared in */
    fold: function(text) {
        return String(text || '').toLowerCase().replace(/\s+/g, '');
    },

    /** Whether a folded text is meant as a formId: "0x..." or FORMID_MIN_DIGITS+ hex digits */
    looksLikeFormId: function(q) {
        if (!q) return false;
        if (q.indexOf('0x') === 0) return q.length > 2;   // "0x" alone would match every formId
        return q.length >= this.FORMID_MIN_DIGITS && /^[0-9a-f]+$/.test(q);
    },

    /** The school's name in the panel's language (chips.school.*), else as the tree has it */
    schoolLabel: function(school) {
        if (!school) return '';
        var key = 'chips.school.' + String(school).toLowerCase();
        var v = (typeof t === 'function') ? t(key) : key;
        return (v && v !== key) ? v : String(school);
    },

    /** The renderer's nodes (they carry each spell's state), or null when it has none */
    _rendererNodes: function() {
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer && CanvasRenderer.nodes &&
            CanvasRenderer.nodes.length > 0) {
            return CanvasRenderer.nodes;
        }
        return null;
    },

    /** The nodes on the tree: the renderer's, else the loaded tree's raw data (no states) */
    treeNodes: function() {
        var live = this._rendererNodes();
        if (live) return live;
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
     * Where the text is in a name, spaces and case ignored, as [start, end) in
     * the name as written ("헝거소환" in "헝거 소환" is the whole name); null if
     * it is not there. For the highlight in the list.
     */
    matchRange: function(text, term) {
        var q = this.fold(term);
        if (!q) return null;
        var s = String(text || '');
        var folded = '';
        var at = [];   // at[i]: the index in s of folded[i]
        for (var i = 0; i < s.length; i++) {
            var ch = s.charAt(i);
            if (/\s/.test(ch)) continue;
            var low = ch.toLowerCase();
            for (var k = 0; k < low.length; k++) {
                folded += low.charAt(k);
                at.push(i);
            }
        }
        var start = folded.indexOf(q);
        if (start === -1) return null;
        return [at[start], at[start + q.length - 1] + 1];
    },

    /**
     * The spells the list shows for a text, best first.
     * @param {Array} nodes - tree nodes (treeNodes() when null)
     * @param {string} text - what was typed ('' lists every spell, alphabetical)
     * @param {number} [limit] - most results (LIMIT)
     * @returns {Array<{node, name, rank}>}
     */
    search: function(nodes, text, limit) {
        var hideLocked = this.hidesLocked();
        if (!nodes) {
            // The raw tree data has no states: a locked spell could not be told apart
            if (hideLocked && !this._rendererNodes()) return [];
            nodes = this.treeNodes();
        }
        var q = this.fold(text);
        var byFormId = this.looksLikeFormId(q);
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
                } else if (fName.indexOf(q) !== -1 ||
                           this.fold(node.school).indexOf(q) !== -1 ||
                           this.fold(this.schoolLabel(node.school)).indexOf(q) !== -1) {
                    rank = this.RANK_OTHER;
                } else if (byFormId && this.fold(node.formId).indexOf(q) !== -1) {
                    rank = this.RANK_FORMID;
                } else {
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
