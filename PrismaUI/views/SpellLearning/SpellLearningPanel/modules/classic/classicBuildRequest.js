/**
 * ClassicBuildRequest — the Classic build request to C++
 *
 * Puts together the ProceduralTreeGenerate request for a Classic build (the
 * scan filters, the grid hint from TreePreview, the selected roots and the
 * builder's fixed config) and sends it a frame later, so the progress modal
 * shows before JSON.stringify blocks. TreeGrowthClassic.buildTree() starts the
 * modal, the pending flag and the Build buttons first; a throw here means no
 * request and so no reply, and send() lets go of all three itself.
 *
 * Usage:
 *   ClassicBuildRequest.send(spellData);
 *
 * Depends on:
 *   classicSettings.js       (ClassicSettings.setStatusText)
 *   proceduralTreeBuilder.js (filterBlacklistedSpells, filterWhitelistedSpells)
 *   spellCopies.js           (SpellCopies.forTree - copies of one spell merged)
 *   treePreview.js           (TreePreview.getOutput, _flattenSelectedRoots)
 *   cppCallbacks.js          (ScanRef.compact)
 *   buildProgress.js (BuildProgress), treeGrowth.js (TreeGrowth.setBuilding), i18n.js (t)
 */

var ClassicBuildRequest = {

    /**
     * The builder settings every Classic request sends, in the order the
     * request has always listed them (grid_hint and selected_roots follow).
     */
    BUILDER_CONFIG: {
        shape: 'organic',
        density: 0.6,
        symmetry: 0.3,
        max_children_per_node: 3,
        top_themes_per_school: 8,
        prefer_vanilla_roots: true
    },

    /**
     * A chosen root that was merged into another copy (SpellCopies) becomes that
     * copy, or C++ would not find it and pick a root of its own.
     * @param {Object} roots - school -> {formId, ...} (TreePreview._flattenSelectedRoots)
     * @param {Object} copies - kept formId -> copy formIds
     */
    _rootsOffCopies: function (roots, copies) {
        var keptOf = {};
        for (var kept in copies) {
            if (!copies.hasOwnProperty(kept)) continue;
            for (var c = 0; c < copies[kept].length; c++) keptOf[copies[kept][c]] = kept;
        }
        for (var school in roots) {
            if (!roots.hasOwnProperty(school) || school === '_allRoots') continue;
            var root = roots[school];
            if (!root || !keptOf[root.formId]) continue;
            var moved = {};
            for (var key in root) {
                if (root.hasOwnProperty(key)) moved[key] = root[key];
            }
            moved.formId = keptOf[root.formId];
            roots[school] = moved;
        }
    },

    /**
     * Build the request from the scan and send it to C++.
     * @param {Object} spellData - state.lastSpellData (has .spells)
     */
    send: function (spellData) {
        // From here the modal, the pending flag and both Build buttons wait for
        // C++'s reply. A throw while the request is put together means no
        // request and so no reply: let go of all three, as a parse error does.
        var failRequest = function (e) {
            console.error('[ClassicGrowth] Build request not sent: ' + (e && e.message ? e.message : e));
            if (typeof state !== 'undefined') state._classicGrowthBuildPending = false;
            if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
            if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
                BuildProgress.fail(t('buildProgress.requestFailed'));
            }
            ClassicSettings.setStatusText(t('buildProgress.requestFailed'), 'error', 'buildProgress.requestFailed');
        };

        var spellsToProcess, config;
        try {
            // Apply the scan filters: tome, blacklist, whitelist, then merge copies
            spellsToProcess = spellData.spells;
            if (typeof filterTomeSpells === 'function') {
                spellsToProcess = filterTomeSpells(spellsToProcess);
            }
            if (typeof filterBlacklistedSpells === 'function') {
                spellsToProcess = filterBlacklistedSpells(spellsToProcess);
            }
            if (typeof filterWhitelistedSpells === 'function') {
                spellsToProcess = filterWhitelistedSpells(spellsToProcess);
            }
            // One tree spell for copies of the same spell; ClassicTreeOutput saves the copies on it
            var merged = SpellCopies.forTree(spellsToProcess);
            spellsToProcess = merged.spells;
            state.treeSpellCopies = merged.copies;
            console.log('[ClassicGrowth] Filtered spells: ' + spellsToProcess.length + '/' + spellData.spells.length);

            // Gather grid layout info so C++ can adapt branching
            var gridHint = null;
            if (typeof TreePreview !== 'undefined' && TreePreview.getOutput) {
                var previewOut = TreePreview.getOutput();
                if (previewOut) {
                    var avgPts = 0;
                    var schoolCount = previewOut.schools ? previewOut.schools.length : 0;
                    if (previewOut.gridPoints && schoolCount > 0) {
                        avgPts = Math.round(previewOut.gridPoints.length / schoolCount);
                    }
                    gridHint = {
                        mode: previewOut.mode || 'sun',
                        schoolCount: schoolCount,
                        avgPointsPerSchool: avgPts
                    };
                }
            }

            // Tier zones are not sent: C++ does not read them (ClassicLayout applies
            // TreeGrowthClassic.settings.tierZones when it places the result)
            config = {};
            for (var ck in this.BUILDER_CONFIG) {
                if (this.BUILDER_CONFIG.hasOwnProperty(ck)) config[ck] = this.BUILDER_CONFIG[ck];
            }
            config.grid_hint = gridHint;
            // Same kind together (0-100, 50 = the builder's own weights) and what tier means
            var cs = (typeof TreeGrowthClassic !== 'undefined' && TreeGrowthClassic.settings) || {};
            var defaults = (typeof TreeGrowthClassic !== 'undefined' && TreeGrowthClassic.BUILD_DEFAULTS) || { groupStrength: 50 };
            // groupStrength 0-100 maps to C++ group_strength 0-2: the default is 1, the weights as measured
            config.group_strength = (cs.groupStrength !== undefined ? cs.groupStrength : defaults.groupStrength) / defaults.groupStrength;
            config.order_by = cs.orderBy === 'cost' ? 'cost' : 'tier';
            config.selected_roots = typeof TreePreview !== 'undefined' ? TreePreview._flattenSelectedRoots() : {};
            this._rootsOffCopies(config.selected_roots, merged.copies);
        } catch (e) {
            failRequest(e);
            return;
        }

        // Defer to let UI render progress modal before blocking on JSON.stringify
        setTimeout(function() {
            try {
                window.callCpp('ProceduralTreeGenerate', JSON.stringify(ScanRef.compact({
                    command: 'build_tree_classic',
                    spells: spellsToProcess,
                    config: config
                })));
            } catch (e) {
                failRequest(e);
            }
        }, 0);
    }
};
