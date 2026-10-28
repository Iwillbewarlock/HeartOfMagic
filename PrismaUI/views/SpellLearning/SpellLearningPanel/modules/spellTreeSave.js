/**
 * SpellTreeSave - the one way the panel writes spell_tree.json (C++ SaveSpellTree).
 *
 * Every save goes through send(): Apply after a build (classicMain.js), Save
 * Tree in edit mode (uiHelpers.js saveTreeToFile), Import (treeViewerUI.js) and
 * orphan repair (orphanRepair.js). A tree with no spell in it is never sent:
 * the tree tab's status line says so (status.treeNoSpellsNotSaved). Until 1.5.0
 * a build whose layout had nothing to place on (no preview grid, the panel never
 * opened) dropped every spell and Apply saved the empty tree over the player's
 * file without a word.
 *
 * A save that is not the build at game start's own ends that run first
 * (AutoBuild.yieldTo). A save from a build (BUILT_SOURCES) goes to
 * SaveBuiltSpellTree, which also records the load order the tree was built
 * under and the spells its scan saw: the payload carries "seenSpells", the
 * persistentId of every spell of the scan the draft was built from, kept or
 * left out - the list the caller kept with the draft (a scan since does not
 * count: its spells were never offered to this tree), else state.lastSpellData (C++
 * moves it into spell_tree_loadorder.json and writes the tree without it; the
 * tree object is not changed). An Import goes to SaveImportedSpellTree (the seen
 * list belongs to another build: C++ clears it); an edit or a repair goes to
 * SaveSpellTree. Neither touches the record otherwise.
 *
 * Usage:
 *   SpellTreeSave.countSpellNodes(tree)        // spells over all schools
 *   SpellTreeSave.send(tree, 'apply')          // false: refused or no bridge
 *   SpellTreeSave.send(tree, 'apply', undefined, seen) // seen: seenSpells(draft's scan), kept with the draft
 *   SpellTreeSave.send(tree, 'edit', jsonText) // the text when the caller has it already
 *                                              // (a build's save makes its own, with seenSpells)
 *
 * Depends on: window.callCpp; state (state.js), AutoBuild, setTreeStatusKey (progressionUI.js) when loaded
 */

var SpellTreeSave = {

    AUTO_SOURCE: 'auto build',
    BUILT_SOURCES: ['apply', 'auto build'],
    IMPORT_SOURCE: 'import',

    /**
     * Spells in a saved-tree object: the nodes of every school.
     * @param {Object} tree - { schools: { name: { nodes: [] } } }
     * @returns {number}
     */
    countSpellNodes: function(tree) {
        if (!tree || !tree.schools || typeof tree.schools !== 'object' || Array.isArray(tree.schools)) return 0;
        var count = 0;
        for (var name in tree.schools) {
            if (!tree.schools.hasOwnProperty(name)) continue;
            var nodes = tree.schools[name] ? tree.schools[name].nodes : null;
            if (Array.isArray(nodes)) count += nodes.length;
        }
        return count;
    },

    /**
     * The persistentId of every spell of a scan, in its order.
     * @param {Object} [scan] - { spells: [] }; state.lastSpellData when not given
     * @returns {string[]} empty when there is no scan data
     */
    seenSpells: function(scan) {
        var data = scan || ((typeof state !== 'undefined' && state) ? state.lastSpellData : null);
        var spells = data && Array.isArray(data.spells) ? data.spells : [];
        var seen = [];
        for (var i = 0; i < spells.length; i++) {
            var id = spells[i] ? spells[i].persistentId : null;
            if (typeof id === 'string' && id !== '') seen.push(id);
        }
        return seen;
    },

    /**
     * Send a tree to C++ to be saved, unless it has no spells.
     * @param {Object} tree
     * @param {string} source - who saves (for the log line): 'apply', 'auto build', 'import'...
     * @param {string} [treeJson] - JSON.stringify(tree), when the caller made it already
     * @param {string[]} [seen] - a build's save: seenSpells() of the scan its draft was built from
     *   (the last scan when not given)
     * @returns {boolean} whether the tree went to C++
     */
    send: function(tree, source, treeJson, seen) {
        if (source !== this.AUTO_SOURCE && typeof AutoBuild !== 'undefined') AutoBuild.yieldTo(source);
        if (this.countSpellNodes(tree) === 0) {
            // console.error reaches SpellLearning.log whatever the developer mode
            console.error('[SpellTreeSave] ' + source + ': a tree with no spells was not saved - ' +
                'spell_tree.json is left as it was');
            if (typeof setTreeStatusKey === 'function') {
                setTreeStatusKey('status.treeNoSpellsNotSaved', null, 'The tree has no spells, so it was not saved.');
            }
            return false;
        }
        if (!window.callCpp) {
            console.warn('[SpellTreeSave] ' + source + ': no C++ bridge - not saved');
            return false;
        }
        if (this.BUILT_SOURCES.indexOf(source) !== -1) {
            // A copy with the seen list last: the tree object goes on to the viewer and later edit saves
            var payload = {};
            for (var key in tree) {
                if (tree.hasOwnProperty(key)) payload[key] = tree[key];
            }
            payload.seenSpells = Array.isArray(seen) ? seen : this.seenSpells();
            window.callCpp('SaveBuiltSpellTree', JSON.stringify(payload));
            return true;
        }
        var listener = source === this.IMPORT_SOURCE ? 'SaveImportedSpellTree' : 'SaveSpellTree';
        window.callCpp(listener, treeJson !== undefined ? treeJson : JSON.stringify(tree));
        return true;
    }
};

if (typeof window !== 'undefined') window.SpellTreeSave = SpellTreeSave;
