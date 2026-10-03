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
 *                  never reads it (the panel sends hard, soft and softNeeded
 *                  only); a saved node with hard and soft both empty gets them
 *                  from it at load (treeLoad.js: one entry hard, more: the
 *                  first hard, the rest soft with 1 needed).
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

    /** softNeeded raised to 1 when the soft list has one and none is needed */
    _needOne: function(node) {
        if (!node.softNeeded && node.softPrereqs && node.softPrereqs.length > 0) node.softNeeded = 1;
    },

    /** The ids of a PreReq Master lock list ({nodeId} entries or bare ids) */
    _lockIds: function(locks) {
        var ids = [];
        if (!locks) return ids;
        for (var i = 0; i < locks.length; i++) {
            var l = locks[i];
            var id = (l && typeof l === 'object') ? l.nodeId : l;
            if (id !== undefined && id !== null) ids.push(id);
        }
        return ids;
    },

    /**
     * Own arrays for all three lists, no repeats, and an id in both hard and
     * soft kept in hard only: that is what a link made by the old edit mode was
     * meant to be (it was added as Required and landed in Optional as well).
     * softNeeded keeps its value, held to the soft list's length.
     *
     * A PreReq Master lock on a layout parent is in both lists from the build
     * (hard as the lock, soft as the tree line), and that one already counted
     * toward softNeeded: the lock always meets it. Taken out of soft, softNeeded
     * goes down by one for each, so the rest (a bridge) does not become needed.
     * @param {Array} [locks] the node's lock list (default `node.locks`)
     * @returns {number} entries dropped
     */
    repair: function(node, locks) {
        if (!node) return 0;
        var before = (node.prerequisites ? node.prerequisites.length : 0) +
                     (node.hardPrereqs ? node.hardPrereqs.length : 0) +
                     (node.softPrereqs ? node.softPrereqs.length : 0);
        var lockIds = this._lockIds(locks === undefined ? node.locks : locks);
        var hard = this.unique(node.hardPrereqs);
        var allSoft = this.unique(node.softPrereqs);
        var soft = allSoft.filter(function(id) { return hard.indexOf(id) === -1; });
        var lockMoved = allSoft.filter(function(id) { return hard.indexOf(id) !== -1 && lockIds.indexOf(id) !== -1; }).length;
        node.prerequisites = this.unique(node.prerequisites);
        node.hardPrereqs = hard;
        node.softPrereqs = soft;
        if (lockMoved > 0) node.softNeeded = (node.softNeeded || 0) - lockMoved;
        this._clamp(node);
        return before - node.prerequisites.length - hard.length - soft.length;
    },

    /**
     * The three lists and softNeeded of `node` onto `raw` (the saved tree's
     * node), as copies, so the next save carries what a repair changed.
     */
    copyTo: function(raw, node) {
        if (!raw || !node) return;
        raw.prerequisites = (node.prerequisites || []).slice();
        raw.hardPrereqs = (node.hardPrereqs || []).slice();
        raw.softPrereqs = (node.softPrereqs || []).slice();
        raw.softNeeded = node.softNeeded || 0;
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
        if (asSoft) {
            node.softPrereqs = (node.softPrereqs || []).concat([id]);
            this._needOne(node);
        } else {
            node.hardPrereqs = (node.hardPrereqs || []).concat([id]);
        }
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
     * Every trace of spell `id` in `node`, for a deleted spell: out of the three
     * lists (`remove`), the tree's `children` and the PreReq Master `locks`, each
     * as a new array (the saved tree's node may hold the old one).
     * @returns {boolean} whether `node` named `id` anywhere
     */
    forget: function(node, id) {
        if (!node) return false;
        var named = !!this.typeOf(node, id) ||
            !!(node.prerequisites && node.prerequisites.indexOf(id) !== -1) ||
            !!(node.children && node.children.indexOf(id) !== -1) ||
            this._lockIds(node.locks).indexOf(id) !== -1;
        if (!named) return false;
        this.remove(node, id);
        if (node.children) node.children = this._drop(node.children, id);
        if (node.locks) {
            node.locks = node.locks.filter(function(l) {
                return ((l && typeof l === 'object') ? l.nodeId : l) !== id;
            });
        }
        return true;
    },

    /**
     * `forget` over every node of a saved tree (`{schools: {name: {nodes}}}`),
     * and its cross-school bridges to or from `id` out, so a reload finds no
     * reference to a deleted spell.
     * @returns {number} nodes that named `id`
     */
    forgetInTree: function(raw, id) {
        if (!raw) return 0;
        var count = 0;
        for (var schoolName in (raw.schools || {})) {
            var nodes = raw.schools[schoolName].nodes || [];
            for (var i = 0; i < nodes.length; i++) {
                if (this.forget(nodes[i], id)) count++;
            }
        }
        if (raw.bridges) {
            raw.bridges = raw.bridges.filter(function(b) { return b.from !== id && b.to !== id; });
        }
        return count;
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
     * Into soft with softNeeded 0 (the soft list was empty) needs 1: a soft list
     * with none needed is met by nothing at all (cppCallbacks.js), so the
     * prerequisite would be gone.
     */
    setType: function(node, id, type) {
        node.hardPrereqs = this._drop(node.hardPrereqs, id);
        node.softPrereqs = this._drop(node.softPrereqs, id);
        if (type === 'soft') {
            node.softPrereqs = node.softPrereqs.concat([id]);
            this._needOne(node);
        } else {
            node.hardPrereqs = node.hardPrereqs.concat([id]);
        }
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
