/**
 * SpellCopies - one tree spell for copies of the same spell
 *
 * A load order can hold the same spell more than once as separate records, each
 * taught by its own tome: a mod installed twice (its own plugin and a merge that
 * carries it), a mod that copies another's spells, a patch that adds a second
 * record instead of overriding the first. The tree then showed the spell two or
 * three times and each copy could be learned into the spellbook (Nexus report,
 * three Conjure Hunger, 1.2.0).
 *
 * Two spells are copies when the record data says so: same name (case ignored),
 * same school, same Magicka cost and the same effects - each effect's editor id
 * (or name), magnitude, duration and area. The effect's form is left out, since
 * a duplicated plugin's effects are records of their own too. Spells that only
 * share a name (a mod's stronger Fireball) stay apart; the spell card names
 * their plugins (SpellNames).
 *
 * The first copy in scan order (load order) stays in the tree, unless the player
 * knows a later copy, or has XP in it, and not the first: then that one stays, so the tree
 * shows the spell learned and its children open (C++ checks the tree spell's
 * id). The others are listed on it as `copies` and saved with the tree. Their tomes still teach them,
 * so PrereqLists.cppEntries gives each copy the prerequisites of the spell it was
 * merged into, and C++ has a copy's tome teach the tree spell (copyOf, SpellTomeHook).
 *
 * Usage:
 *   var merged = SpellCopies.merge(spells);   // { spells: [...], copies: { keptId: [copyId, ...] } }
 *
 * Off with the Classic setting mergeCopies (TreeGrowthClassic.settings).
 */

var SpellCopies = {

    /** Cost and magnitudes are compared rounded to this many decimals */
    ROUND_DIGITS: 1,

    _num: function (v) {
        var f = Math.pow(10, this.ROUND_DIGITS);
        return Math.round((Number(v) || 0) * f) / f;
    },

    /**
     * What makes two records the same spell, or null for a spell with no
     * effects listed (a scan from before effects were read): never merged.
     * @param {Object} spell
     * @returns {string|null}
     */
    key: function (spell) {
        if (!spell || !spell.name || !spell.effects || !spell.effects.length) return null;
        var parts = [];
        for (var i = 0; i < spell.effects.length; i++) {
            var e = spell.effects[i] || {};
            parts.push([e.editorId || e.name || '', this._num(e.magnitude),
                        this._num(e.duration), this._num(e.area)].join(':'));
        }
        parts.sort();
        return [String(spell.name).toLowerCase(), spell.school || '',
                this._num(spell.magickaCost), parts.join(',')].join('|');
    },

    /**
     * @param {Array} spells - in scan order
     * @param {Set} [known] - formIds the player knows (state.playerKnownSpells): kept first
     * @param {Set} [started] - formIds with XP (state.spellProgress): kept when none is known
     * @returns {{spells: Array, copies: Object}} the spells kept, and for each kept
     *   spell that had copies its formId -> the copies' formIds
     */
    merge: function (spells, known, started) {
        var has = function (set, spell) { return !!(set && set.has && set.has(spell.formId)); };
        var groups = {};    // key -> [spell, ...] in scan order
        var slots = [];     // one per kept place: a spell with no key, or a key
        for (var i = 0; i < spells.length; i++) {
            var k = this.key(spells[i]);
            if (k === null) { slots.push({ spell: spells[i] }); continue; }
            if (!groups[k]) { groups[k] = []; slots.push({ key: k }); }
            groups[k].push(spells[i]);
        }

        var kept = [];
        var copies = {};
        for (var s = 0; s < slots.length; s++) {
            if (!slots[s].key) { kept.push(slots[s].spell); continue; }
            var group = groups[slots[s].key];
            var keep = this._pick(group, known, has) || this._pick(group, started, has) || group[0];
            kept.push(keep);
            for (var c = 0; c < group.length; c++) {
                if (group[c] === keep) continue;
                if (!copies[keep.formId]) copies[keep.formId] = [];
                copies[keep.formId].push(group[c].formId);
            }
        }
        return { spells: kept, copies: copies };
    },

    /** The first of the group in the set, or null. @private */
    _pick: function (group, set, has) {
        for (var i = 0; i < group.length; i++) {
            if (has(set, group[i])) return group[i];
        }
        return null;
    },

    /** Whether the tree merges copies (on unless the Classic setting turns it off) */
    isEnabled: function () {
        return typeof TreeGrowthClassic === 'undefined' || !TreeGrowthClassic.settings ||
            TreeGrowthClassic.settings.mergeCopies !== false;
    },

    /**
     * The tree's spells with copies merged when the setting is on; a copy the
     * player knows stays, else one with XP (startedIds).
     * @param {Array} spells
     * @returns {{spells: Array, copies: Object}}
     */
    forTree: function (spells) {
        if (!this.isEnabled()) return { spells: spells, copies: {} };
        var hasState = typeof state !== 'undefined' && state;
        return this.merge(spells, hasState ? state.playerKnownSpells : null, this.startedIds());
    },

    /**
     * The spells with XP or unlocked (state.spellProgress), so a copy being learned
     * before the merge stays the tree's spell and keeps its progress.
     * @returns {Set|null}
     */
    startedIds: function () {
        if (typeof state === 'undefined' || !state || typeof Set === 'undefined') return null;
        var ids = new Set();
        var progress = state.spellProgress || {};
        for (var id in progress) {
            if (progress.hasOwnProperty(id) && progress[id] && (progress[id].xp > 0 || progress[id].unlocked)) ids.add(id);
        }
        return ids;
    }
};

window.SpellCopies = SpellCopies;
