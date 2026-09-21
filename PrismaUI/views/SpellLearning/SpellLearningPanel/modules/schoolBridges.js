/**
 * SchoolBridges - what the layout does with the cross school bridges
 *
 * C++ (TreeBuilderBridges.cpp) hands every build a `bridges` list - pairs of
 * spells from different schools that share what they are - and `schoolLinks`,
 * how much kin each pair of schools has. This module turns them into two
 * things, the same way for every build mode:
 *
 *  1. School order  - schools with the most in common become neighbours
 *  2. Prerequisites - the bridge's source becomes one more soft prerequisite
 *                     of its target: an extra way in, never a requirement
 *
 * Tried and dropped (2026-09-22): nudging bridged spells and themes toward the
 * neighbour's border inside the classic layout. Placement there follows the
 * parent along a spoke; the nudge moved nothing measurable.
 *
 * Loaded after treePreview.js and before the *Main.js growth modules.
 */
var SchoolBridges = {

    // Beyond this many schools every order cannot be tried (8! = 40320)
    MAX_SCHOOLS_FOR_FULL_SEARCH: 8,

    // =========================================================================
    // 1. SCHOOL ORDER
    // =========================================================================

    _pairKey: function (a, b) {
        return a < b ? a + '|' + b : b + '|' + a;
    },

    /**
     * How much each pair of schools has in common. `schoolLinks` counts every
     * spell with kin in the other school; the bridge list is capped per pair
     * and says little (most pairs are simply full), so it is only the fallback.
     */
    pairCounts: function (treeData) {
        var counts = {};
        var links = treeData.schoolLinks || [];
        for (var l = 0; l < links.length; l++) {
            counts[this._pairKey(links[l].a, links[l].b)] = links[l].kin || 0;
        }
        if (links.length > 0) return counts;

        var bridges = treeData.bridges || [];
        for (var i = 0; i < bridges.length; i++) {
            var key = this._pairKey(bridges[i].fromSchool, bridges[i].toSchool);
            counts[key] = (counts[key] || 0) + 1;
        }
        return counts;
    },

    /** Bridges between neighbours when the schools stand in this order. */
    _orderScore: function (order, counts, circular) {
        var score = 0;
        var last = circular ? order.length : order.length - 1;
        for (var i = 0; i < last; i++) {
            score += counts[this._pairKey(order[i], order[(i + 1) % order.length])] || 0;
        }
        return score;
    },

    /**
     * The order that puts the most bridges between neighbours. The first
     * school keeps its place (a circle has no start), ties keep the order
     * the schools came in, so the same load order always gives the same tree.
     */
    orderSchools: function (names, treeData, circular) {
        if (!treeData || names.length < 4) return names.slice();
        if (names.length > this.MAX_SCHOOLS_FOR_FULL_SEARCH) return names.slice();

        var counts = this.pairCounts(treeData);
        var self = this;
        var best = names.slice();
        var bestScore = this._orderScore(best, counts, circular);

        var rest = names.slice(circular ? 1 : 0);
        var head = circular ? [names[0]] : [];
        var permute = function (done, left) {
            if (left.length === 0) {
                var score = self._orderScore(done, counts, circular);
                if (score > bestScore) { bestScore = score; best = done.slice(); }
                return;
            }
            for (var i = 0; i < left.length; i++) {
                var next = left.slice();
                var picked = next.splice(i, 1);
                permute(done.concat(picked), next);
            }
        };
        permute(head, rest);
        return best;
    },

    /**
     * Reorders the preview's schools and redraws it, so that the roots and
     * sectors every layout reads are already in the new order.
     * @returns {boolean} true when the order changed
     */
    applyOrderToPreview: function (treeData) {
        if (!treeData || !treeData.bridges || typeof TreePreview === 'undefined') return false;
        var schoolData = TreePreview.schoolData;
        if (!schoolData) return false;

        var names = Object.keys(schoolData);
        var circular = TreePreview.activeMode !== 'flat';
        var ordered = this.orderSchools(names, treeData, circular);
        if (ordered.join('|') === names.join('|')) return false;

        var reordered = {};
        for (var i = 0; i < ordered.length; i++) reordered[ordered[i]] = schoolData[ordered[i]];
        TreePreview.schoolData = reordered;
        if (typeof TreePreview._render === 'function') TreePreview._render();
        console.log('[SchoolBridges] School order: ' + ordered.join(' > '));
        return true;
    },

    // =========================================================================
    // 2. PREREQUISITES
    // =========================================================================

    /**
     * Adds the bridges to a finished tree. A target keeps needing one of its
     * soft prerequisites; the bridge only makes the list longer. Roots and
     * spells without prerequisites are open anyway and are left alone.
     */
    applyToOutput: function (output, treeData) {
        if (!output || !treeData || !treeData.bridges) return;

        var nodeById = {};
        for (var schoolName in output.schools) {
            if (!output.schools.hasOwnProperty(schoolName)) continue;
            var nodes = output.schools[schoolName].nodes || [];
            for (var n = 0; n < nodes.length; n++) nodeById[nodes[n].formId] = nodes[n];
        }

        var open = function (sourceId, targetId) {
            var target = nodeById[targetId];
            if (!target || target.isRoot || !target.softNeeded) return false;
            var soft = target.softPrereqs || [];
            if (soft.indexOf(sourceId) >= 0) return true;
            // A copy: the layout shares one array between prerequisites and softPrereqs
            target.softPrereqs = soft.concat([sourceId]);
            return true;
        };

        var kept = [];
        for (var i = 0; i < treeData.bridges.length; i++) {
            var bridge = treeData.bridges[i];
            if (!nodeById[bridge.from] || !nodeById[bridge.to]) continue;
            var forward = open(bridge.from, bridge.to);
            var back = bridge.twoWay ? open(bridge.to, bridge.from) : false;
            if (forward || back) kept.push(bridge);
        }
        output.bridges = kept;
        console.log('[SchoolBridges] ' + kept.length + ' of ' + treeData.bridges.length + ' bridges applied');
    }
};
