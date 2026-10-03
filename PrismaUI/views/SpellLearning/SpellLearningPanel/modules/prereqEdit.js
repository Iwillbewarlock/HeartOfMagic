/**
 * PrereqEdit - the prerequisite lists of one spell, as edit mode changes them
 *
 * A spell has three lists:
 *
 *   hardPrereqs    every one is needed (Required, "H" on the card)
 *   softPrereqs    softNeeded of them are needed (Optional, "S")
 *   prerequisites  the links drawn as tree lines. A PreReq Master lock is hard
 *                  but drawn as a lock chain, and a cross-school bridge is soft
 *                  but drawn by BridgeView, so neither is in this list. C++
 *                  reads it only when hard and soft are both empty.
 *
 * An id is in at most one of hard and soft, and at most once in each list.
 * Every list is the spell's own array: before 1.2.1 a built tree used one array
 * for `prerequisites` and `softPrereqs`, so a new link went into both, a delete
 * took it out of both, and a hard/soft switch made a second entry. `repair`
 * cleans a tree saved that way when it loads; the calls below keep the rules
 * on every edit.
 *
 * Depends on nothing (node tests: prereqEditTest.js).
 */

var PrereqEdit = {

    /** The list without repeats, as a new array ([] for a missing one) */
    unique: function(list) {
        var out = [];
        if (!list) return out;
        for (var i = 0; i < list.length; i++) {
            if (out.indexOf(list[i]) === -1) out.push(list[i]);
        }
        return out;
    },

    /** softNeeded no larger than the soft list and not below 0 */
    _clamp: function(node) {
        var max = node.softPrereqs ? node.softPrereqs.length : 0;
        var n = node.softNeeded || 0;
        node.softNeeded = n > max ? max : (n < 0 ? 0 : n);
    },

    /**
     * Own arrays for all three lists, no repeats, and an id in both hard and
     * soft kept in hard only: that is what a link made by the old edit mode was
     * meant to be (it was added as Required and landed in Optional as well).
     * softNeeded keeps its value, held to the soft list's length.
     * @returns {number} entries dropped
     */
    repair: function(node) {
        if (!node) return 0;
        var before = (node.prerequisites ? node.prerequisites.length : 0) +
                     (node.hardPrereqs ? node.hardPrereqs.length : 0) +
                     (node.softPrereqs ? node.softPrereqs.length : 0);
        var hard = this.unique(node.hardPrereqs);
        var soft = this.unique(node.softPrereqs).filter(function(id) { return hard.indexOf(id) === -1; });
        node.prerequisites = this.unique(node.prerequisites);
        node.hardPrereqs = hard;
        node.softPrereqs = soft;
        this._clamp(node);
        return before - node.prerequisites.length - hard.length - soft.length;
    },

    /** 'hard', 'soft' or null */
    typeOf: function(node, id) {
        if (node.hardPrereqs && node.hardPrereqs.indexOf(id) !== -1) return 'hard';
        if (node.softPrereqs && node.softPrereqs.indexOf(id) !== -1) return 'soft';
        return null;
    },

    _drop: function(list, id) {
        if (!list) return [];
        return list.filter(function(x) { return x !== id; });
    },

    /**
     * A new link from `id`: drawn, and in hard (or soft) once.
     * @returns {boolean} false when `id` is already a prerequisite
     */
    add: function(node, id, asSoft) {
        if (this.typeOf(node, id) || (node.prerequisites && node.prerequisites.indexOf(id) !== -1)) return false;
        node.prerequisites = (node.prerequisites || []).concat([id]);
        if (asSoft) node.softPrereqs = (node.softPrereqs || []).concat([id]);
        else node.hardPrereqs = (node.hardPrereqs || []).concat([id]);
        return true;
    },

    /**
     * `id` out of all three lists.
     * @returns {{type: (string|null), linked: boolean, softNeeded: number}} what it was, for undo
     */
    remove: function(node, id) {
        var was = {
            type: this.typeOf(node, id),
            linked: !!(node.prerequisites && node.prerequisites.indexOf(id) !== -1),
            softNeeded: node.softNeeded || 0
        };
        node.prerequisites = this._drop(node.prerequisites, id);
        node.hardPrereqs = this._drop(node.hardPrereqs, id);
        node.softPrereqs = this._drop(node.softPrereqs, id);
        this._clamp(node);
        return was;
    },

    /**
     * Puts `id` back the way `remove` found it (`was` is its return value).
     */
    restore: function(node, id, was) {
        // The drawn list keeps its order when its part does not change
        var linked = !!(node.prerequisites && node.prerequisites.indexOf(id) !== -1);
        if (was.linked && !linked) node.prerequisites = (node.prerequisites || []).concat([id]);
        else if (!was.linked && linked) node.prerequisites = this._drop(node.prerequisites, id);
        node.hardPrereqs = this._drop(node.hardPrereqs, id);
        node.softPrereqs = this._drop(node.softPrereqs, id);
        if (was.type === 'soft') node.softPrereqs = node.softPrereqs.concat([id]);
        else if (was.type === 'hard') node.hardPrereqs = node.hardPrereqs.concat([id]);
        node.softNeeded = was.softNeeded;
        this._clamp(node);
    },

    /**
     * `id` into hard or soft only; `prerequisites` (the drawn line) is left as it is.
     */
    setType: function(node, id, type) {
        node.hardPrereqs = this._drop(node.hardPrereqs, id);
        node.softPrereqs = this._drop(node.softPrereqs, id);
        if (type === 'soft') node.softPrereqs = node.softPrereqs.concat([id]);
        else node.hardPrereqs = node.hardPrereqs.concat([id]);
        this._clamp(node);
    },

    /**
     * Hard to soft, soft to hard; an id in neither becomes hard.
     * @returns {{type: string, wasType: (string|null), softNeeded: number}} new type, and the old state for undo
     */
    toggle: function(node, id) {
        var wasType = this.typeOf(node, id);
        var softNeeded = node.softNeeded || 0;
        var type = wasType === 'hard' ? 'soft' : 'hard';
        this.setType(node, id, type);
        return { type: type, wasType: wasType, softNeeded: softNeeded };
    }
};
