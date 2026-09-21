/**
 * SchoolBridges - what the build does with the cross school bridges
 *
 * C++ (TreeBuilderBridges.cpp) hands every build a `bridges` list: pairs of
 * spells from different schools that share what they are. On save, each
 * bridge's source becomes one more soft prerequisite of its target - an extra
 * way in, never a requirement - and the list itself is copied into the saved
 * tree for the viewer to draw. Every spell's traits are baked in at the same
 * time, because after a restart the scan is gone and the saved tree is all the
 * viewer has.
 *
 * Two attempts to make the bridges move things were tried and both are gone:
 *
 *  - Nudging bridged spells and themes toward the neighbour school's border
 *    (2026-09-22). Classic placement follows the parent along a spoke, so the
 *    nudge moved nothing measurable.
 *  - Reordering the schools around the wheel so the ones with the most kin
 *    became neighbours (2026-09-22). It worked, but it rearranged the whole
 *    picture, and the author asked for the familiar shape back. The schools
 *    keep the order the scan finds them in.
 *
 * Loaded after treePreview.js and before the *Main.js growth modules.
 */
var SchoolBridges = {

    /**
     * Adds the bridges to a finished tree. A target keeps needing one of its
     * soft prerequisites; the bridge only makes the list longer. Roots and
     * spells without prerequisites are open anyway and are left alone.
     */
    applyToOutput: function (output, treeData) {
        if (!output || !treeData) return;
        var bridges = treeData.bridges || [];

        var nodeById = {};
        for (var schoolName in output.schools) {
            if (!output.schools.hasOwnProperty(schoolName)) continue;
            var nodes = output.schools[schoolName].nodes || [];
            for (var n = 0; n < nodes.length; n++) nodeById[nodes[n].formId] = nodes[n];
        }

        // The viewer's keyword filter needs every spell's traits, and after a
        // restart the scan is gone - only the saved tree is left.
        var spells = (typeof state !== 'undefined' && state.lastSpellData && state.lastSpellData.spells) || [];
        for (var s = 0; s < spells.length; s++) {
            var owner = nodeById[spells[s].formId];
            if (owner && spells[s].traits) owner.traits = spells[s].traits;
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
        for (var i = 0; i < bridges.length; i++) {
            var bridge = bridges[i];
            if (!nodeById[bridge.from] || !nodeById[bridge.to]) continue;
            var forward = open(bridge.from, bridge.to);
            var back = bridge.twoWay ? open(bridge.to, bridge.from) : false;
            if (forward || back) kept.push(bridge);
        }
        output.bridges = kept;
        console.log('[SchoolBridges] ' + kept.length + ' of ' + bridges.length + ' bridges applied');
    }
};
