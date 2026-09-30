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
            // Apply the scan filters: blacklist, whitelist
            spellsToProcess = spellData.spells;
            if (typeof filterBlacklistedSpells === 'function') {
                spellsToProcess = filterBlacklistedSpells(spellsToProcess);
            }
            if (typeof filterWhitelistedSpells === 'function') {
                spellsToProcess = filterWhitelistedSpells(spellsToProcess);
            }
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
            config.selected_roots = typeof TreePreview !== 'undefined' ? TreePreview._flattenSelectedRoots() : {};
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
