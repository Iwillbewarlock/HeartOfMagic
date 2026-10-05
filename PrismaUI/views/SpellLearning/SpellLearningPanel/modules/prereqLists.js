/**
 * PrereqLists - a spell's required (hard) and optional (soft) prerequisites
 *
 * Each node keeps three lists: hardPrereqs (all must be mastered), softPrereqs
 * (softNeeded of them must be) and prerequisites (every link, the panel's own
 * list; C++ reads only the first two). They must be three separate arrays: the
 * Classic tree output once gave prerequisites and softPrereqs the same one, and
 * the editor's pen then put a new link in both the required and the optional
 * list, its delete took it out of both, and a required -> optional switch left
 * it twice in the optional list (Nexus report, 1.2.0). Trees saved with that
 * damage are repaired as they load (normalize).
 *
 * Usage:
 *   var lists = PrereqLists.normalize(nd.hardPrereqs, nd.softPrereqs);
 *   window.callCpp('SetTreePrerequisites', JSON.stringify(PrereqLists.cppEntries(nodes)));
 */

var PrereqLists = {

    /**
     * Fresh hard and soft lists: no repeats, and an id that is in both stays
     * required only.
     * @param {Array} hard
     * @param {Array} soft
     * @returns {{hard: Array, soft: Array}}
     */
    normalize: function (hard, soft) {
        var seen = {};
        var outHard = [];
        var outSoft = [];
        var i;
        for (i = 0; hard && i < hard.length; i++) {
            if (seen[hard[i]]) continue;
            seen[hard[i]] = true;
            outHard.push(hard[i]);
        }
        for (i = 0; soft && i < soft.length; i++) {
            if (seen[soft[i]]) continue;
            seen[soft[i]] = true;
            outSoft.push(soft[i]);
        }
        return { hard: outHard, soft: outSoft };
    },

    /**
     * The entries SetTreePrerequisites takes: one per node, and one per copy
     * merged into a node (node.copies, SpellCopies) naming that node (copyOf: C++
     * then has the copy's tome teach the tree spell, SpellTomeHook) and carrying
     * its lists, so the copy never opens with fewer prerequisites than its spell.
     * @param {Array} nodes
     * @returns {Array}
     */
    cppEntries: function (nodes) {
        var entries = [];
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            var entry = {
                formId: node.formId || node.id,
                hardPrereqs: node.hardPrereqs || [],
                softPrereqs: node.softPrereqs || [],
                softNeeded: node.softNeeded || 0
            };
            entries.push(entry);
            var copies = node.copies || [];
            for (var c = 0; c < copies.length; c++) {
                entries.push({
                    formId: copies[c],
                    copyOf: entry.formId,
                    hardPrereqs: entry.hardPrereqs,
                    softPrereqs: entry.softPrereqs,
                    softNeeded: entry.softNeeded
                });
            }
        }
        return entries;
    }
};

window.PrereqLists = PrereqLists;
