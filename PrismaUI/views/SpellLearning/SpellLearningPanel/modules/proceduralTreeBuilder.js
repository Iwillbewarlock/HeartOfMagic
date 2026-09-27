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
// BLACKLIST FILTER
// =============================================================================

/**
 * Filter blacklisted spells from a spell array.
 * Uses settings.spellBlacklist (saved persistently via UnifiedConfig).
 */
function filterBlacklistedSpells(spells) {
    if (!settings.spellBlacklist || settings.spellBlacklist.length === 0) {
        return spells;
    }

    var blacklistedIds = {};
    settings.spellBlacklist.forEach(function(entry) {
        blacklistedIds[entry.formId] = true;
    });

    var filtered = spells.filter(function(spell) {
        var formId = spell.formId || spell.id;
        return !blacklistedIds[formId];
    });

    var removedCount = spells.length - filtered.length;
    if (removedCount > 0) {
        console.log('[Procedural] Filtered ' + removedCount + ' blacklisted spells (' + filtered.length + ' remaining)');
    }

    return filtered;
}

window.filterBlacklistedSpells = filterBlacklistedSpells;

// =============================================================================
// WHITELIST FILTER
// =============================================================================

/**
 * Filter spells to exclude those from disabled plugins.
 * Uses settings.pluginWhitelist (saved persistently via UnifiedConfig).
 * All plugins are ENABLED by default - this filters OUT explicitly disabled ones.
 * Blacklist is applied separately (filters individual spells).
 */
function filterWhitelistedSpells(spells) {
    if (!settings.pluginWhitelist || settings.pluginWhitelist.length === 0) {
        return spells;  // No whitelist configured = include all
    }

    // Get DISABLED plugins (whitelist is opt-out, not opt-in)
    var disabledPlugins = settings.pluginWhitelist.filter(function(entry) {
        return entry.enabled === false;
    });

    if (disabledPlugins.length === 0) {
        return spells;  // Nothing disabled = include all
    }

    // Build lookup of disabled plugin names (lowercase for case-insensitive comparison)
    var disabledMap = {};
    disabledPlugins.forEach(function(entry) {
        disabledMap[entry.plugin.toLowerCase()] = true;
    });

    var filtered = spells.filter(function(spell) {
        var plugin = null;

        // Extract plugin from persistentId: "PluginName.esp|0x00123456"
        if (spell.persistentId && spell.persistentId.includes('|')) {
            plugin = spell.persistentId.split('|')[0];
        }
        // Fallback: try source field
        else if (spell.source) {
            plugin = spell.source;
        }

        if (!plugin) {
            // Can't determine plugin - include it to be safe
            return true;
        }

        // Include if NOT in disabled list
        return disabledMap[plugin.toLowerCase()] !== true;
    });

    var removedCount = spells.length - filtered.length;
    if (removedCount > 0) {
        console.log('[Procedural] Whitelist filtered ' + removedCount + ' spells from disabled plugins (' + filtered.length + ' remaining)');
    }

    return filtered;
}

window.filterWhitelistedSpells = filterWhitelistedSpells;

/**
 * Error handler for C++ tree build failures of the Classic growth mode
 * (the one builder since 2026-09-27).
 *
 * @param {string} error - Error string from C++
 * @param {string} pendingKey - State key to set for retry (e.g. '_classicGrowthBuildPending')
 * @param {Object|null} settingsModule - ClassicSettings (has .setStatusText)
 * @param {Object} retryConfig - Config to pass on retry {command, config}
 * @param {string} logPrefix - Console log prefix e.g. '[ClassicGrowth]'
 */
function _handleBuildFailure(error, pendingKey, settingsModule, retryConfig, logPrefix) {
    console.error(logPrefix + ' C++ build failed:', error);
    var errorMsg = 'Tree build failed: ' + error + '\nPlease report this error on the mod page.';
    var retryFn = function() {
        if (state.lastSpellData && state.lastSpellData.spells && window.callCpp) {
            state[pendingKey] = true;
            if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(true);
            var hasPRM = typeof PreReqMaster !== 'undefined' && PreReqMaster.isEnabled && PreReqMaster.isEnabled();
            if (typeof BuildProgress !== 'undefined') BuildProgress.start(hasPRM);
            if (settingsModule) settingsModule.setStatusText('Retrying with fallback...', '#f59e0b');
            // Defer to let UI render before blocking on JSON.stringify
            setTimeout(function() {
                window.callCpp('ProceduralTreeGenerate', JSON.stringify(ScanRef.compact({
                    command: retryConfig.command || 'build_tree_classic',
                    spells: state.lastSpellData.spells,
                    config: retryConfig.config || {},
                    fallback: true
                })));
            }, 0);
        }
    };
    if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
        BuildProgress.fail(errorMsg, retryFn);
    }
    if (settingsModule) {
        settingsModule.setStatusText('Build failed: ' + error, '#ef4444');
    }
    if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeBuildFailed', {error: error}), 'error');
    if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
}

/**
 * Callback from C++ when a ProceduralTreeGenerate build completes.
 * Only the Classic growth mode sends that request, and it sets
 * state._classicGrowthBuildPending first; a result nobody waits for is dropped.
 * A request C++ turned away because another build was still running comes back
 * with busy set: that says nothing about the build in flight, so it leaves the
 * pending flag, the progress modal and the Build button alone.
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
            if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeBuildComplete', {schools: cgSchools, spells: cgSpells}), 'success');
        } else {
            _handleBuildFailure(
                result.error || 'unknown',
                '_classicGrowthBuildPending',
                typeof ClassicSettings !== 'undefined' ? ClassicSettings : null,
                { command: 'build_tree_classic', config: { shape: 'organic', density: 0.6, symmetry: 0.3, max_children_per_node: 3, top_themes_per_school: 8, prefer_vanilla_roots: true } },
                '[ClassicGrowth]'
            );
        }
    } catch (e) {
        console.error('[Procedural] Error parsing C++ result:', e);
        // Whatever went wrong, let go of the build. A pending flag left standing
        // sends the NEXT build's result down the Classic branch, and a progress
        // modal with nothing left to close it sits over the panel for good.
        state._classicGrowthBuildPending = false;
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
        if (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
            BuildProgress.fail('Result parse error');
        }
        updateStatus('Result parse error');
        setStatusIcon('X');
    }
};

