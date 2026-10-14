/**
 * Procedural Tree Builder Module
 *
 * What is left of the panel's old build paths (Procedural, Procedural+ and
 * Visual-First were removed 2026-09-27; the Classic growth mode in
 * modules/classic/ is the one builder):
 * - filterBlacklistedSpells / filterWhitelistedSpells: the spell filters the
 *   Classic build request applies
 * - onProceduralTreeComplete: C++ callback for ProceduralTreeGenerate, handed
 *   to the Classic growth mode
 *
 * Depends on:
 * - modules/state.js (state, settings)
 * - modules/uiHelpers.js (updateStatus, setStatusIcon)
 * - modules/buildProgress.js (BuildProgress)
 */

// =============================================================================
// TOME FILTER
// =============================================================================

/**
 * A spell the tree takes: some tome teaches it, as the scan page's "Tomes only"
 * switch did in its default (on) position before it went (2026-09-30); something
 * in the load order hands one of its tomes out (tomeUnreachable is not set: a
 * placed copy, a leveled list, a container, a recipe, a quest item, a script
 * property or a distribution config); and it is not equipped in the voice slot
 * (a mod's script or animation spell, which would take the shout's place when
 * learned); and it is not another plugin's copy of a spell the tree has
 * (duplicateOf names the kept record: same editor id and school, its tome reads
 * as the kept spell's - SpellScannerDuplicates.cpp); and it costs something to
 * cast and the spell gate does not leave it out (isTreeCandidate covers the rest).
 * The gate's "out" (gate.band: free to cast, or only dismisses a summon - the same
 * JudgeSpell the tree load and Spell Research use, SpellScannerGate.cpp) holds
 * unless the player put the spell back in (GateReview.isKept, settings.gateReviewed);
 * "review" stays in, GateReview lists it. A scan from before the gate has only
 * freeSpell (2026-10-05). The scan keeps every spell for the librarian and the
 * perk adapters and marks taughtByTome, tomeUnreachable, voiceSlot, freeSpell,
 * gate and duplicateOf; a scan from before the marks has no fields, and its
 * spells count as taken.
 * @param {Object} spell
 * @returns {boolean}
 */
function isTaughtByTome(spell) {
    if (!spell) return true;
    if (!isTreeCandidate(spell)) return false;
    var kept = typeof GateReview !== 'undefined' && GateReview.isKept(spell);
    return spell.gate ? (spell.gate.band !== 'out' || kept) : spell.freeSpell !== true;
}

/**
 * The tome filter before the spell gate: a tome teaches it, something hands the
 * tome out, not in the voice slot, not another plugin's copy. GateReview's
 * "left out" list starts from these.
 * @param {Object} spell
 * @returns {boolean}
 */
function isTreeCandidate(spell) {
    return !spell || (spell.taughtByTome !== false && spell.tomeUnreachable !== true &&
        spell.voiceSlot !== true && !spell.duplicateOf);
}

/** The spells the tree takes (isTaughtByTome). */
function filterTomeSpells(spells) {
    return spells.filter(isTaughtByTome);
}

// =============================================================================
// BLACKLIST FILTER
// =============================================================================

/** The plugin a spell comes from: its persistentId ("Plugin.esp|0x00123456"), else its plugin or source field */
function spellPluginName(spell) {
    if (spell.persistentId && spell.persistentId.indexOf('|') !== -1) return spell.persistentId.split('|')[0];
    return spell.plugin || spell.source || '';
}

/**
 * Filter blacklisted spells from a spell array.
 * Uses settings.spellBlacklist (saved persistently via UnifiedConfig). An entry with
 * plugin and localFormId matches by those, so it survives a load order change (as
 * Spell Research's filter in the DLL does); an older entry with only a formId
 * matches by formId.
 */
function filterBlacklistedSpells(spells) {
    if (!settings.spellBlacklist || settings.spellBlacklist.length === 0) {
        return spells;
    }

    var keys = {};
    var formIds = {};
    settings.spellBlacklist.forEach(function(entry) {
        if (entry.plugin && entry.localFormId) {
            keys[String(entry.plugin).toLowerCase() + ':' + String(entry.localFormId).toLowerCase()] = true;
        } else if (entry.formId) {
            formIds[entry.formId] = true;
        }
    });

    return spells.filter(function(spell) {
        var formId = spell.formId || spell.id;
        var plugin = spellPluginName(spell);
        if (plugin && formId && typeof getLocalFormId === 'function' &&
                keys[plugin.toLowerCase() + ':' + getLocalFormId(formId)]) return false;
        return !formIds[formId];
    });
}

window.filterBlacklistedSpells = filterBlacklistedSpells;

// =============================================================================
// WHITELIST FILTER
// =============================================================================

/**
 * Filter spells to exclude those from disabled plugins.
 * Uses settings.pluginWhitelist (saved persistently via UnifiedConfig).
 * All plugins are ENABLED by default - this filters OUT explicitly disabled ones,
 * so a plugin installed after the list was saved comes in (as in the DLL's
 * Spell Research filter). Blacklist is applied separately (filters individual spells).
 */
function filterWhitelistedSpells(spells) {
    if (!settings.pluginWhitelist || settings.pluginWhitelist.length === 0) {
        return spells;  // No whitelist configured = include all
    }

    var disabledMap = {};
    settings.pluginWhitelist.forEach(function(entry) {
        if (entry.enabled === false && entry.plugin) disabledMap[String(entry.plugin).toLowerCase()] = true;
    });

    return spells.filter(function(spell) {
        var plugin = spellPluginName(spell);
        // Can't determine plugin - include it to be safe
        return !plugin || disabledMap[plugin.toLowerCase()] !== true;
    });
}

window.filterWhitelistedSpells = filterWhitelistedSpells;

/**
 * Error handler for C++ tree build failures of the Classic growth mode
 * (the one builder since 2026-09-27).
 *
 * Retry runs the builder's own build again (retryBuild), so the retry sends
 * what a Build click sends: the blacklist, plugin whitelist and tome filters,
 * the chosen roots, the grid hint and the pending flag and Build button state.
 *
 * @param {string} error - Error string from C++
 * @param {Object|null} settingsModule - ClassicSettings (has .setStatusText)
 * @param {function|null} retryBuild - Starts the build again (TreeGrowthClassic.buildTree)
 * @param {string} logPrefix - Console log prefix e.g. '[ClassicGrowth]'
 */
function _handleBuildFailure(error, settingsModule, retryBuild, logPrefix) {
    console.error(logPrefix + ' C++ build failed:', error);
    var errorMsg = tOr('status.treeBuildFailed', { error: error }, 'Tree build failed: {{error}}') + '\n' +
        tOr('status.reportOnModPage', null, 'Please report this error on the mod page.');
    var retryFn = typeof retryBuild === 'function' ? function() {
        if (settingsModule) settingsModule.setStatusText(t('buildProgress.retrying'), 'working', 'buildProgress.retrying');
        retryBuild();
    } : null;
    if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
        BuildProgress.fail(errorMsg, retryFn);
    }
    if (settingsModule) {
        settingsModule.setStatusText(t('buildProgress.statusFailed', {error: error}), 'error', 'buildProgress.statusFailed', {error: error});
    }
    if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeBuildFailed', {error: error}), 'error', 'status.treeBuildFailed', {error: error});
    if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
}

/**
 * Callback from C++ when a ProceduralTreeGenerate build completes.
 * Only the Classic growth mode sends that request, and it sets
 * state._classicGrowthBuildPending first; a result nobody waits for is dropped.
 * A request C++ turned away because another build was still running comes back
 * with busy set: that says nothing about the build in flight, so it leaves the
 * pending flag, the progress modal and the Build button alone.
 * A build asked for while C++ still worked on an earlier one waits in
 * state._classicGrowthBuildQueued (classicMain.js buildTree): the earlier one's
 * result is then dropped and the waiting request goes out, the flag still set.
 */
window.onProceduralTreeComplete = function(resultStr) {
    console.log('[Procedural] C++ result received');

    try {
        var result = typeof resultStr === 'string' ? JSON.parse(resultStr) : resultStr;

        if (result && result.busy) {
            console.warn('[Procedural] C++ is still building an earlier request - this one was turned away');
            return;
        }

        if (!state._classicGrowthBuildPending) {
            console.warn('[Procedural] C++ result with no build waiting for it - ignored');
            return;
        }
        var queued = state._classicGrowthBuildQueued;
        if (queued) {
            state._classicGrowthBuildQueued = null;
            console.warn('[Procedural] Result of an earlier build dropped - sending the build asked for since');
            queued();
            return;
        }
        state._classicGrowthBuildPending = false;
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);

        if (result.success && result.treeData) {
            // Advance build progress: tree done → prereqs or finalize
            if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
                BuildProgress.setStage('prereqs');
            }
            var cgTreeData = typeof result.treeData === 'string' ? JSON.parse(result.treeData) : result.treeData;
            if (typeof TreeGrowthClassic !== 'undefined' && TreeGrowthClassic.loadTreeData) {
                TreeGrowthClassic.loadTreeData(cgTreeData);
                if (typeof TreeGrowth !== 'undefined') TreeGrowth._markDirty();
            }
            // Update notification bar with build result
            var cgSchools = cgTreeData && cgTreeData.schools ? Object.keys(cgTreeData.schools).length : 0;
            var cgSpells = 0;
            if (cgTreeData && cgTreeData.schools) { for (var s in cgTreeData.schools) { cgSpells += (cgTreeData.schools[s].nodes || []).length; } }
            if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeBuildComplete', {schools: cgSchools, spells: cgSpells}), 'success', 'status.treeBuildComplete', {schools: cgSchools, spells: cgSpells});
        } else {
            _handleBuildFailure(
                result.error || 'unknown',
                typeof ClassicSettings !== 'undefined' ? ClassicSettings : null,
                typeof TreeGrowthClassic !== 'undefined' ? function() { TreeGrowthClassic.buildTree(); } : null,
                '[ClassicGrowth]'
            );
        }
    } catch (e) {
        console.error('[Procedural] Error parsing C++ result:', e);
        // Whatever went wrong, let go of the build. A pending flag left standing
        // sends the NEXT build's result down the Classic branch, and a progress
        // modal with nothing left to close it sits over the panel for good.
        state._classicGrowthBuildPending = false;
        state._classicGrowthBuildQueued = null;
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
        if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
            BuildProgress.fail(tOr('status.buildResultUnreadable', null, "The tree builder's result could not be read"));
        }
        updateStatus(tOr('status.buildResultUnreadable', null, "The tree builder's result could not be read"),
            'status.buildResultUnreadable');
        setStatusIcon('X');
    }
};

