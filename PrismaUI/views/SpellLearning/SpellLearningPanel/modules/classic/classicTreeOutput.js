/**
 * ClassicTreeOutput — the saved tree a Classic build becomes
 *
 * Turns the C++ Classic result and the layout ClassicLayout placed into the
 * spell tree JSON Apply saves: only placed nodes, their edges and prerequisites
 * from the layout's parents (the C++ builder's are dropped), PreReq Master's
 * locks as hard prerequisites, baked positions, and each school's spoke angles
 * from TreePreview's root directions. Cross-school bridges and the declutter
 * pass are applied afterwards by TreeGrowthClassic.applyTree().
 *
 * Usage:
 *   var built = ClassicTreeOutput.build(treeData, layoutData, baseData);
 *   // built.output (tree JSON), built.posCount (nodes with a layout position)
 *
 * Depends on: treeCore.js (TreeCore.getOutput, optional)
 */

var ClassicTreeOutput = {

    /** Globe the output names when TreeCore is not there. */
    DEFAULT_GLOBE: { x: 0, y: 0, radius: 45 },

    /** Width of a school's slice (degrees) when the tree has no schools. */
    DEFAULT_SLICE_DEG: 60,

    /** Positions and angles are saved with this many steps per unit (2 decimals). */
    ROUND_STEPS: 100,

    _round: function (v) {
        return Math.round(v * this.ROUND_STEPS) / this.ROUND_STEPS;
    },

    /**
     * @param {Object} treeData        - the C++ Classic result (TreeGrowthClassic._treeData)
     * @param {Object|null} layoutData - ClassicLayout.layoutAllSchools() output
     * @param {Object|null} baseData   - TreePreview.getOutput() (mode, rootNodes)
     * @returns {{output: Object, posCount: number}}
     */
    build: function (treeData, layoutData, baseData) {
        // Build lookups from layout data:
        //   posLookup:      formId → {x, y}
        //   childrenLookup: formId → [childFormId, ...]  (from layout's parentFormId)
        //   prereqLookup:   formId → [parentFormId]      (inverse of children)
        //   placedSet:      formId → true                 (nodes actually placed by layout)
        var posLookup = {};
        var childrenLookup = {};
        var prereqLookup = {};
        var placedSet = {};

        if (layoutData && layoutData.schools) {
            var layoutSchools = layoutData.schools;
            for (var lsName in layoutSchools) {
                if (!layoutSchools.hasOwnProperty(lsName)) continue;
                var lsNodes = layoutSchools[lsName].nodes || [];
                for (var li = 0; li < lsNodes.length; li++) {
                    var ln = lsNodes[li];
                    posLookup[ln.formId] = { x: ln.x, y: ln.y };
                    placedSet[ln.formId] = true;

                    // Build parent→children from layout's parentFormId
                    if (ln.parentFormId) {
                        if (!childrenLookup[ln.parentFormId]) childrenLookup[ln.parentFormId] = [];
                        childrenLookup[ln.parentFormId].push(ln.formId);

                        if (!prereqLookup[ln.formId]) prereqLookup[ln.formId] = [];
                        prereqLookup[ln.formId].push(ln.parentFormId);
                    }
                }
            }
        }

        var posCount = Object.keys(posLookup).length;
        var placedCount = Object.keys(placedSet).length;
        console.log('[ClassicGrowth] applyTree: posLookup=' + posCount +
                    ', placed=' + placedCount + ', childrenEdges=' + Object.keys(childrenLookup).length);

        var layoutMode = baseData ? baseData.mode : 'sun';

        // Build output JSON with layout-derived edges and positions
        var output = {
            version: treeData.version || '1.0',
            generator: 'PrismaUI ClassicGrowth',
            generatedAt: new Date().toISOString(),
            trustPrereqs: true,
            noRotate: (layoutMode === 'flat'),
            layoutMode: layoutMode,
            config: treeData.config || {},
            globe: (typeof TreeCore !== 'undefined' && TreeCore.getOutput)
                ? TreeCore.getOutput()
                : { x: this.DEFAULT_GLOBE.x, y: this.DEFAULT_GLOBE.y, radius: this.DEFAULT_GLOBE.radius },
            schools: {}
        };

        // Copy school_configs and seed if present
        if (treeData.seed) output.seed = treeData.seed;
        if (treeData.school_configs) output.school_configs = treeData.school_configs;

        // Get root directions from TreePreview baseData for spoke angles
        var rootDirBySchool = {};
        if (baseData && baseData.rootNodes) {
            for (var rni = 0; rni < baseData.rootNodes.length; rni++) {
                var rn = baseData.rootNodes[rni];
                if (rn.school && rn.dir !== undefined) {
                    rootDirBySchool[rn.school] = rn.dir; // radians
                }
            }
        }
        var numSchools = Object.keys(treeData.schools || {}).length;
        var sliceAngle = numSchools > 0 ? 360 / numSchools : this.DEFAULT_SLICE_DEG;

        var srcSchools = treeData.schools || {};
        for (var schoolName in srcSchools) {
            if (!srcSchools.hasOwnProperty(schoolName)) continue;
            var src = srcSchools[schoolName];
            var srcNodes = src.nodes || [];

            var schoolRootId = src.root || (srcNodes.length > 0 ? srcNodes[0].formId : '');
            var outNodes = [];
            var nodesWithPos = 0;

            for (var i = 0; i < srcNodes.length; i++) {
                var sn = srcNodes[i];

                // Only include nodes that were actually placed by the layout
                if (!placedSet[sn.formId]) continue;

                // Use layout-derived children and prereqs instead of C++ builder's
                var layoutChildren = childrenLookup[sn.formId] || [];
                var layoutPrereqs = prereqLookup[sn.formId] || [];

                // Prereq rework: regular prereqs = soft (need any 1 of N)
                // Lock prereqs = hard (mandatory)
                var lockHardPrereqs = [];
                var lockData = [];
                // Locks are stored directly on the node by PreReqMaster
                if (sn.locks && sn.locks.length > 0) {
                    lockData = sn.locks;
                    lockHardPrereqs = sn.locks.map(function(l) { return l.nodeId; });
                }

                var outNode = {
                    formId: sn.formId,
                    children: layoutChildren,
                    prerequisites: layoutPrereqs,
                    hardPrereqs: lockHardPrereqs,
                    softPrereqs: layoutPrereqs,
                    softNeeded: layoutPrereqs.length > 0 ? 1 : 0,
                    tier: sn.tier || 1
                };
                if (lockData.length > 0) outNode.locks = lockData;
                if (sn.skillLevel) outNode.skillLevel = sn.skillLevel;
                if (sn.theme) outNode.theme = sn.theme;
                if (sn.name) outNode.name = sn.name;
                if (sn.formId === schoolRootId) {
                    outNode.isRoot = true;
                    outNode.prerequisites = [];
                    outNode.softPrereqs = [];
                    outNode.softNeeded = 0;
                }

                // Bake layout position
                var pos = posLookup[sn.formId];
                if (pos) {
                    outNode.x = this._round(pos.x);
                    outNode.y = this._round(pos.y);
                    nodesWithPos++;
                }
                outNodes.push(outNode);
            }

            console.log('[ClassicGrowth] School "' + schoolName + '": ' +
                        outNodes.length + ' nodes, ' + nodesWithPos + ' with positions');

            output.schools[schoolName] = {
                root: schoolRootId,
                layoutStyle: src.layoutStyle || 'classic',
                nodes: outNodes
            };

            // Copy color if present
            if (src.color) output.schools[schoolName].color = src.color;

            // Bake spoke angle from root direction so CanvasRendererV2 rotates correctly
            var dirRad = rootDirBySchool[schoolName];
            if (dirRad !== undefined && !isNaN(dirRad)) {
                var spokeDeg = dirRad * 180 / Math.PI; // convert radians to degrees
                output.schools[schoolName].spokeAngle = this._round(spokeDeg);
                output.schools[schoolName].startAngle = this._round(spokeDeg - sliceAngle / 2);
                output.schools[schoolName].endAngle = this._round(spokeDeg + sliceAngle / 2);
                output.schools[schoolName].rootDirection = dirRad;
            }
        }

        return { output: output, posCount: posCount };
    }
};
