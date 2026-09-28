/**
 * SpellNames - how a spell is named in the spell card's lists.
 *
 * Two plugins can give two spells the same name: NoviceBoltSpells.esp adds its
 * own "Flames" (in the Korean game both read 불씨조각), and the builder, which
 * scores name and effect alike, links such spells to each other. The card then
 * lists Flames under Flames' own Unlocks. When a name is carried by more than
 * one spell in the loaded tree, the lists show the plugin after it:
 * "불씨조각 (NoviceBoltSpells.esp)". A name no other spell has stays as it is.
 *
 * What is counted: the different spells carrying a name, locked ones too, so a
 * label does not change as the player unlocks spells. An edit-mode duplicate
 * (originalFormId) is the same spell as its original: counted once with it, and
 * named by the original's plugin.
 *
 * Used by the card's Unlocks and prerequisite lists (treeViewerUI.js) and the
 * "Paths to other schools" list (bridgeView.js).
 *
 * Depends on (read when called, all optional): state.js (state, settings),
 * spellCache.js (SpellCache - `plugin` of GetSpellInfo), treeViewerUI.js
 * (_findNodeById).
 *
 * Exports (global): SpellNames, spellDisplayName(id, node, shown)
 */

var SpellNames = {

    /** How many different spells in the tree carry each name, keyed 'n:' + name */
    _counts: {},
    /** The tree the counts were made for; a new tree makes them again */
    _treeRef: null,

    /** The name a list shows for a node: its name, or its formId when it has none */
    baseName: function(node) {
        return String(node.name || node.formId || node.id || '');
    },

    /** The spell a node stands for: an edit-mode duplicate stands for its original */
    spellIdOf: function(node) {
        return String(node.originalFormId || node.formId || node.id || '');
    },

    /** Count again at the next call: names came in (SpellCache), or edit mode added or removed a node */
    invalidate: function() {
        this._treeRef = null;
    },

    _treeData: function() {
        return (typeof state !== 'undefined' && state) ? state.treeData : null;
    },

    _countsFor: function(treeData) {
        if (!treeData || !treeData.nodes) return {};
        if (treeData !== this._treeRef) {
            var counts = {};
            var seen = {};
            var nodes = treeData.nodes;
            for (var i = 0; i < nodes.length; i++) {
                if (!nodes[i]) continue;
                var key = 'n:' + this.baseName(nodes[i]);
                var spellKey = key + '|' + this.spellIdOf(nodes[i]);
                if (seen[spellKey]) continue;
                seen[spellKey] = true;
                counts[key] = (counts[key] || 0) + 1;
            }
            this._counts = counts;
            this._treeRef = treeData;
        }
        return this._counts;
    },

    /** True when more than one spell in the loaded tree has this name */
    isShared: function(name) {
        var counts = this._countsFor(this._treeData());
        return (counts['n:' + name] || 0) > 1;
    },

    /**
     * The plugin a node's spell comes from: SpellCache's `plugin` once the spell
     * info is in, before that the plugin half of its persistentId
     * ("Plugin.esp|0x000800"). A duplicate asks for its original's. Empty when
     * neither is known.
     */
    pluginOf: function(node) {
        if (!node) return '';
        var id = this.spellIdOf(node);
        if (typeof SpellCache !== 'undefined' && SpellCache && typeof SpellCache.get === 'function') {
            var data = SpellCache.get(id);
            if (data && data.plugin) return String(data.plugin);
        }
        var pid = node.persistentId;
        if (!pid && node.originalFormId && typeof _findNodeById === 'function') {
            var original = _findNodeById(node.originalFormId);
            pid = original ? original.persistentId : null;
        }
        if (typeof pid === 'string' && pid.indexOf('|') > 0) return pid.split('|')[0];
        return '';
    },

    /** Whether a list may name this spell: cheat mode, or the spell is not locked */
    isShown: function(node) {
        var cheat = typeof settings !== 'undefined' && settings && settings.cheatMode;
        return !!(cheat || (node && node.state !== 'locked'));
    },

    /**
     * The text for one spell in a list.
     * @param {string} id - the formId the list holds
     * @param {Object} [node] - its node; looked up in the loaded tree when left out
     * @param {boolean} [shown] - whether its name may show; SpellNames.isShown when left out
     * @returns {string} '???' when hidden (no plugin either), the id when the tree
     *   does not have it, else the name with " (Plugin.esp)" when another spell shares it
     */
    label: function(id, node, shown) {
        if (node === undefined) {
            node = (typeof _findNodeById === 'function' && this._treeData()) ? _findNodeById(id) : null;
        }
        if (shown === undefined) shown = this.isShown(node);
        if (!shown) return '???';
        if (!node) return String(id);
        var name = this.baseName(node);
        if (this.isShared(name)) {
            var plugin = this.pluginOf(node);
            if (plugin) return name + ' (' + plugin + ')';
        }
        return name;
    }
};

/** Shorthand for SpellNames.label */
function spellDisplayName(id, node, shown) {
    return SpellNames.label(id, node, shown);
}

if (typeof window !== 'undefined') {
    window.SpellNames = SpellNames;
    window.spellDisplayName = spellDisplayName;
}
