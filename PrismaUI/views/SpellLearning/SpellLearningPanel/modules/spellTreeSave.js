/**
 * SpellTreeSave - the one way the panel writes spell_tree.json (C++ SaveSpellTree).
 *
 * Every save goes through send(): Apply after a build (classicMain.js), Save
 * Tree in edit mode (uiHelpers.js saveTreeToFile), Import (treeViewerUI.js) and
 * orphan repair (orphanRepair.js). A tree with no spell in it is never sent.
 * Until 1.4.1 a build whose layout had nothing to place on (no preview grid,
 * the panel never opened) dropped every spell and Apply saved the empty tree
 * over the player's file without a word.
 *
 * Usage:
 *   SpellTreeSave.countSpellNodes(tree)        // spells over all schools
 *   SpellTreeSave.send(tree, 'apply')          // false: refused or no bridge
 *   SpellTreeSave.send(tree, 'edit', jsonText) // the text when the caller has it already
 *
 * Depends on: window.callCpp
 */

var SpellTreeSave = {

    /**
     * Spells in a saved-tree object: the nodes of every school.
     * @param {Object} tree - { schools: { name: { nodes: [] } } }
     * @returns {number}
     */
    countSpellNodes: function(tree) {
        if (!tree || !tree.schools || typeof tree.schools !== 'object') return 0;
        var count = 0;
        for (var name in tree.schools) {
            if (!tree.schools.hasOwnProperty(name)) continue;
            var nodes = tree.schools[name] ? tree.schools[name].nodes : null;
            if (Array.isArray(nodes)) count += nodes.length;
        }
        return count;
    },

    /**
     * Send a tree to C++ to be saved, unless it has no spells.
     * @param {Object} tree
     * @param {string} source - who saves (for the log line)
     * @param {string} [treeJson] - JSON.stringify(tree), when the caller made it already
     * @returns {boolean} whether the tree went to C++
     */
    send: function(tree, source, treeJson) {
        if (this.countSpellNodes(tree) === 0) {
            // console.error reaches SpellLearning.log whatever the developer mode
            console.error('[SpellTreeSave] ' + source + ': a tree with no spells was not saved - ' +
                'spell_tree.json is left as it was');
            return false;
        }
        if (!window.callCpp) {
            console.warn('[SpellTreeSave] ' + source + ': no C++ bridge - not saved');
            return false;
        }
        window.callCpp('SaveSpellTree', treeJson !== undefined ? treeJson : JSON.stringify(tree));
        return true;
    }
};

if (typeof window !== 'undefined') window.SpellTreeSave = SpellTreeSave;
