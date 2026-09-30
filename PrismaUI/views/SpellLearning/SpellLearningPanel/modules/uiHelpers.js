/**
 * SpellLearning UI Helpers Module
 * 
 * Utility functions: the scan status bar, the unlocked count, saving the tree,
 * primed spells, preset naming, local form ids, tier XP and editor id words.
 * Depends on: state.js
 */

// =============================================================================
// STATUS UPDATES
// =============================================================================

var _scanStatus = null;   // {key, params, type} of the last keyed message

/**
 * @param {string} message
 * @param {string} [type] - 'working', 'success', 'error' (the bar's class)
 * @param {string} [i18nKey] - the message's language key, so a language switch
 *   says it again in the new language (relabelScanStatus); without one the
 *   switch leaves the text alone (it used to put "Ready to scan" back over
 *   "Tree built": the bar's own data-i18n goes once a script writes it)
 * @param {Object} [params] - the key's {{variables}}
 */
function updateScanStatus(message, type, i18nKey, params) {
    _scanStatus = i18nKey ? { key: i18nKey, params: params || null, type: type || '' } : null;
    var bar = document.getElementById('scanStatusBar');
    var text = document.getElementById('scanStatusText');
    if (!bar || !text) return;
    text.textContent = message;
    text.removeAttribute('data-i18n');
    bar.className = 'scan-status-bar';
    if (type) bar.classList.add(type);
}

// English for keys newer than some community translations, so a missing key shows this, not the key
var _scanStatusFallback = { 'status.scanFailedUnknown': 'Scan failed (unknown error)' };

/** A scan bar message in the current language (tOr with the English fallback when there is one). */
function scanStatusText(key, params) {
    var fallback = _scanStatusFallback[key];
    if (fallback !== undefined && typeof tOr === 'function') return tOr(key, params, fallback);
    var text = t(key, params);
    return (text === key && fallback !== undefined) ? fallback : text;
}

/** After a language switch (languageSetting.js): a keyed message in the new language. */
function relabelScanStatus() {
    var s = _scanStatus;
    if (s && typeof t === 'function') updateScanStatus(scanStatusText(s.key, s.params), s.type, s.key, s.params);
}

/** The Scan button after a scan: enabled, with its own label. */
function restoreScanButton() {
    var scanBtn = document.getElementById('scanBtn');
    if (scanBtn) {
        scanBtn.disabled = false;
        scanBtn.innerHTML = '<span class="btn-icon">[*]</span>' + t('buttons.scanSpells');
    }
}

/**
 * C++ says a scan threw: {"mode":"all"|"tomes","reason":"..."} (an older plain
 * JSON-quoted string or bare text is a full scan's reason). The Scan button is
 * disabled while a scan runs and only spell data enables it again, so a scan
 * that dies has to hand the button back here, or the panel sits on
 * "Scanning..." with no word of what happened (the log has the full reason).
 * A full scan also ends edit mode's "Scanning game spells..." wait. The panel
 * no longer asks for tome scans (the tome filter went, 2026-09-30: the scan
 * leaves out what a player does not learn); a "tomes" failure is only logged.
 * @param {string|Object} message
 */
window.onScanFailed = function(message) {
    var data = message;
    if (typeof data === 'string' && (data.charAt(0) === '"' || data.charAt(0) === '{')) {
        try { data = JSON.parse(data); } catch (e) {}
    }
    var mode = 'all';
    var reason = data;
    if (data && typeof data === 'object') {
        mode = data.mode === 'tomes' ? 'tomes' : 'all';
        reason = data.reason;
    }
    reason = String(reason === undefined || reason === null ? '' : reason);
    if (mode === 'tomes') {
        // No panel control asks for a tome scan since the tome filter went (2026-09-30)
        console.warn('[SpellLearning] A tome scan failed' + (reason ? ' (' + reason + ')' : ''));
        return;
    }
    restoreScanButton();
    // No reason (C++ caught something that is not a std::exception): "unknown error", in the panel's language
    var key = reason ? 'status.scanFailed' : 'status.scanFailedUnknown';
    var params = reason ? { error: reason } : null;
    var text = scanStatusText(key, params);
    updateScanStatus(text, 'error', key, params);
    setStatusIcon('X');
    // Edit mode asked for this scan and shows a wait line until spells arrive
    var spawnList = document.getElementById('spawn-spell-list');
    var waiting = spawnList && spawnList.querySelector ? spawnList.querySelector('.spawn-loading') : null;
    if (waiting) waiting.textContent = text;
};

/**
 * The footer's unlocked count: every tree node in the unlocked state, spells
 * mastered through XP and spells the player already knew alike. The one count
 * every caller uses, so a relock or a known-spells reply cannot leave two
 * different numbers behind.
 */
function updateUnlockedCount() {
    var el = document.getElementById('unlocked-count');
    if (!el) return;
    var nodes = (state.treeData && state.treeData.nodes) || [];
    var count = 0;
    for (var i = 0; i < nodes.length; i++) {
        if (nodes[i].state === 'unlocked') count++;
    }
    el.textContent = count;
}

function setStatusIcon(icon) {
    var statusIcon = document.getElementById('statusIcon');
    if (statusIcon) {
        statusIcon.textContent = icon;
    }
}

// =============================================================================
// SAVED TREE
// =============================================================================

// Write the tree the panel holds to the plugin's tree file (Save Tree in edit
// mode). Returns false when there is nothing to save.
function saveTreeToFile() {
    if (!state.treeData || !state.treeData.rawData) {
        console.warn('[SpellLearning] No tree data to save');
        return false;
    }

    if (!window.callCpp) {
        console.warn('[SpellLearning] Cannot save - callCpp not available');
        return false;
    }

    var treeJson = JSON.stringify(state.treeData.rawData);
    console.log('[SpellLearning] Saving tree to file, size:', treeJson.length, 'chars,',
                Object.keys(state.treeData.rawData.schools || {}).length, 'schools');

    window.callCpp('SaveSpellTree', treeJson);
    return true;
}

// =============================================================================
// DRAGGING, RESIZING, MINIMIZE, CLOSE
// =============================================================================
// initializeDragging and initializeResizing live in script.js, onCloseClick in
// buttonHandlers.js; copies here were shadowed by those later files and never
// ran. The minimize button is not in index.html.

// =============================================================================
// LOCAL FORM ID — strips load-order prefix for stable cross-session matching
// =============================================================================

/**
 * Extract the local (plugin-relative) formId from a full runtime formId.
 * Regular plugins: 0xXXyyyyyy → yyyyyy (strip top byte)
 * ESL/light plugins: 0xFExxxyyyy → yyyy (strip FE + light index, keep low 12 bits)
 * Returns lowercase hex string without 0x prefix.
 */
function getLocalFormId(formIdStr) {
    if (!formIdStr) return '';
    // Remove 0x prefix if present
    var hex = formIdStr.replace(/^0x/i, '').toLowerCase();
    // Pad to 8 chars
    while (hex.length < 8) hex = '0' + hex;
    // ESL: top byte is FE → local ID is the last 3 hex chars (12 bits)
    if (hex.substring(0, 2) === 'fe') {
        return hex.substring(5); // last 3 hex chars
    }
    // Regular: strip top byte → last 6 hex chars (24 bits)
    return hex.substring(2);
}

// =============================================================================
// PRIMED SPELL FILTERING (after blacklist/whitelist/tome filters)
// =============================================================================

/**
 * Get all primed spells (post-blacklist/whitelist/tome filtering).
 * @returns {Array} Filtered spell objects from state.lastSpellData
 */
function getPrimedSpells() {
    var data = state.lastSpellData;
    if (!data || !data.spells || data.spells.length === 0) return [];

    // Build blacklist lookup — use stable plugin:localFormId keys, fall back to raw formId
    var blacklistKeys = {};
    var blacklistFormIds = {};
    if (settings.spellBlacklist) {
        settings.spellBlacklist.forEach(function(entry) {
            if (entry.plugin && entry.localFormId) {
                blacklistKeys[entry.plugin.toLowerCase() + ':' + entry.localFormId] = true;
            } else if (entry.formId) {
                blacklistFormIds[entry.formId] = true;
            }
        });
    }

    // Build whitelist lookup (case-insensitive) - only filter if whitelist has enabled entries
    var whitelistActive = false;
    var whitelistPlugins = {};
    if (settings.pluginWhitelist && settings.pluginWhitelist.length > 0) {
        settings.pluginWhitelist.forEach(function(entry) {
            if (entry.enabled) {
                whitelistActive = true;
                whitelistPlugins[entry.plugin.toLowerCase()] = true;
            }
        });
    }


    return data.spells.filter(function(spell) {
        var stableKey = spell.plugin ? spell.plugin.toLowerCase() + ':' + getLocalFormId(spell.formId) : '';
        if (stableKey && blacklistKeys[stableKey]) return false;
        if (blacklistFormIds[spell.formId]) return false;
        if (whitelistActive && spell.plugin && !whitelistPlugins[spell.plugin.toLowerCase()]) return false;
        return true;
    });
}

/**
 * Get primed spells filtered to a specific school.
 * @param {string} school - School name (e.g. "Destruction")
 * @returns {Array} Filtered spell objects for that school
 */
function getPrimedSpellsForSchool(school) {
    return getPrimedSpells().filter(function(s) {
        return (s.school || 'Unknown') === school;
    });
}

function updatePrimedCount() {
    var primed = getPrimedSpells();

    var el = document.getElementById('statPrimedSpells');
    if (el) el.textContent = primed.length;

    // Update school breakdown to reflect filtered (primed) counts
    var schoolCounts = {};
    primed.forEach(function(s) {
        var sch = s.school || 'Unknown';
        schoolCounts[sch] = (schoolCounts[sch] || 0) + 1;
    });

    var breakdownEl = document.getElementById('scanSchoolBreakdown');
    if (breakdownEl) {
        var schoolColors = {
            'Destruction': 'var(--destruction)',
            'Restoration': 'var(--restoration)',
            'Alteration': 'var(--alteration)',
            'Conjuration': 'var(--conjuration)',
            'Illusion': 'var(--illusion)'
        };
        var html = '';
        var sortedSchools = Object.keys(schoolCounts).sort(function(a, b) {
            return schoolCounts[b] - schoolCounts[a];
        });
        sortedSchools.forEach(function(school) {
            var color = schoolColors[school] || 'var(--text-muted)';
            html += '<div class="scan-school-row">' +
                '<span class="scan-school-dot" style="background:' + color + '"></span>' +
                '<span class="scan-school-name">' + school + '</span>' +
                '<span class="scan-school-count">' + schoolCounts[school] + '</span>' +
                '</div>';
        });
        breakdownEl.innerHTML = html;
    }
}

// =============================================================================
// PRESET NAME PROMPT (replaces native prompt() which doesn't work in Ultralight)
// =============================================================================

/**
 * Show an in-page modal to ask the user for a preset name.
 * @param {string} title  - Modal title (e.g. "Save Scanner Preset")
 * @param {Function} onConfirm - Called with the trimmed name string
 */
function showPresetNamePrompt(title, onConfirm) {
    var modal = document.getElementById('preset-name-modal');
    var titleEl = document.getElementById('preset-name-title');
    var input = document.getElementById('preset-name-input');
    var confirmBtn = document.getElementById('preset-name-confirm');
    var cancelBtn = document.getElementById('preset-name-cancel');
    var closeBtn = document.getElementById('preset-name-close');
    var backdrop = modal ? modal.querySelector('.modal-backdrop') : null;

    if (!modal || !input) {
        console.warn('[PresetPrompt] Modal elements not found');
        return;
    }

    titleEl.textContent = title || 'Save Preset';
    input.value = '';
    modal.classList.remove('hidden');

    // Focus input after brief delay for animation
    setTimeout(function() { input.focus(); }, 50);

    function cleanup() {
        modal.classList.add('hidden');
        confirmBtn.removeEventListener('click', onSave);
        cancelBtn.removeEventListener('click', onCancel);
        closeBtn.removeEventListener('click', onCancel);
        backdrop.removeEventListener('click', onCancel);
        input.removeEventListener('keydown', onKeydown);
    }

    function onSave() {
        var name = input.value.trim();
        if (!name) return;
        cleanup();
        onConfirm(name);
    }

    function onCancel() {
        cleanup();
    }

    function onKeydown(e) {
        if (e.key === 'Enter') {
            e.preventDefault();
            onSave();
        } else if (e.key === 'Escape') {
            e.preventDefault();
            onCancel();
        }
    }

    confirmBtn.addEventListener('click', onSave);
    cancelBtn.addEventListener('click', onCancel);
    closeBtn.addEventListener('click', onCancel);
    if (backdrop) backdrop.addEventListener('click', onCancel);
    input.addEventListener('keydown', onKeydown);
}

// =============================================================================
// PRESET FILE I/O — Shared callback from C++ preset handlers
// =============================================================================

/**
 * Case-insensitive lookup for a preset key in a presets object.
 * Returns the actual key if found, or null.
 */
function _findPresetKeyCaseInsensitive(presetsObj, name) {
    if (!name || !presetsObj) return null;
    if (presetsObj[name]) return name;
    var lowerName = name.toLowerCase();
    for (var key in presetsObj) {
        if (presetsObj.hasOwnProperty(key) && key.toLowerCase() === lowerName) return key;
    }
    return null;
}

/**
 * Callback invoked by C++ OnLoadPresets.
 * Receives JSON: { type: "scanner"|"settings", presets: [{ key, data }, ...] }
 * Dispatches to the appropriate preset module to populate its in-memory store.
 */
window.onPresetsLoaded = function(resultStr) {
    try {
        var result = typeof resultStr === 'string' ? JSON.parse(resultStr) : resultStr;
        var type = result.type;
        var presets = result.presets || [];

        var logMsg = '[Presets] onPresetsLoaded: type=' + type + ', count=' + presets.length;
        console.log(logMsg);
        if (window.callCpp) {
            try { window.callCpp('LogMessage', JSON.stringify({ level: 'info', message: logMsg })); } catch(e) {}
        }

        if (type === 'scanner') {
            // Clear and repopulate
            for (var k in scannerPresets) {
                if (scannerPresets.hasOwnProperty(k)) delete scannerPresets[k];
            }
            for (var i = 0; i < presets.length; i++) {
                var entry = presets[i];
                scannerPresets[entry.key] = entry.data;
                console.log('[Presets] Scanner preset loaded: key=' + entry.key);
            }
            if (typeof updateScannerPresetsUI === 'function') {
                updateScannerPresetsUI();
            }
            // Auto-apply saved scanner preset (or "Default" fallback)
            var scannerTarget = (typeof _activeScannerPreset !== 'undefined') ? _activeScannerPreset : '';
            var scannerKey = _findPresetKeyCaseInsensitive(scannerPresets, scannerTarget)
                          || _findPresetKeyCaseInsensitive(scannerPresets, 'Default');
            var applyLog = '[Presets] Scanner apply: target=' + scannerTarget + ', found=' + scannerKey +
                           ', applyScannerPreset=' + (typeof applyScannerPreset);
            console.log(applyLog);
            if (window.callCpp) {
                try { window.callCpp('LogMessage', JSON.stringify({ level: 'info', message: applyLog })); } catch(e) {}
            }
            if (scannerKey && typeof applyScannerPreset === 'function') {
                console.log('[Presets] Auto-applying scanner preset: ' + scannerKey);
                applyScannerPreset(scannerKey);
            }
            // Sync Easy Mode chip selection
            if (typeof _easySelectedPreset !== 'undefined' && typeof _activeScannerPreset !== 'undefined') {
                _easySelectedPreset = _activeScannerPreset || scannerKey || '';
                if (typeof updateEasyPresetChips === 'function') updateEasyPresetChips();
            }
        } else if (type === 'design') {
            // Design presets: no in-memory store of their own here, DesignPresets keeps them
            if (typeof DesignPresets !== 'undefined') DesignPresets.onLoaded(presets);
        } else if (type === 'settings') {
            // Clear and repopulate
            for (var k2 in settingsPresets) {
                if (settingsPresets.hasOwnProperty(k2)) delete settingsPresets[k2];
            }
            for (var j = 0; j < presets.length; j++) {
                var entry2 = presets[j];
                settingsPresets[entry2.key] = entry2.data;
            }
            // Built-in seeding removed — presets come exclusively from files on disk.
            // DEFAULT.json is bundled with the mod in the RELEASE folder.
            if (typeof updateSettingsPresetsUI === 'function') {
                updateSettingsPresetsUI();
            }
            // Track active preset name for UI highlighting, but do NOT auto-apply.
            // config.json is the source of truth — applying a preset here would
            // overwrite the user's saved settings (discoveryMode, requireSkillLevel, etc.)
            var settingsTarget = (typeof _activeSettingsPreset !== 'undefined') ? _activeSettingsPreset : '';
            var settingsKey = _findPresetKeyCaseInsensitive(settingsPresets, settingsTarget)
                           || _findPresetKeyCaseInsensitive(settingsPresets, 'Default');
            if (settingsKey) {
                _activeSettingsPreset = settingsKey;
                if (typeof updateSettingsPresetsUI === 'function') {
                    updateSettingsPresetsUI();
                }
            }
        }
    } catch (e) {
        console.error('[Presets] Failed to parse onPresetsLoaded:', e);
    }
};

// =============================================================================
// XP UTILITIES
// =============================================================================

/**
 * Background of the scan screen's tree previews (treePreview, treeGrowth,
 * prereqMaster): the design's --preview-bg, or the old near-black. The previews
 * repaint every frame, so the value is read at most once a second - a design or
 * theme switch shows up within that.
 * @returns {string} CSS colour
 */
var _previewBg = { value: '', readAt: 0 };
var PREVIEW_BG_DEFAULT = '#0a0a0f';
var PREVIEW_BG_REREAD_MS = 1000;
function getPreviewBackground() {
    var now = Date.now();
    if (!_previewBg.value || now - _previewBg.readAt > PREVIEW_BG_REREAD_MS) {
        var v = '';
        try { v = getComputedStyle(document.documentElement).getPropertyValue('--preview-bg').trim(); } catch (e) {}
        // An engine may hand back a variable pointing at another one unresolved
        // ("var(--book-page)"), which a canvas would ignore: the default then
        _previewBg.value = (v && v.indexOf('var(') < 0) ? v : PREVIEW_BG_DEFAULT;
        _previewBg.readAt = now;
    }
    return _previewBg.value;
}

/**
 * Editor id as words: "LUN_MoonFire" -> "moon fire". Names are translated and
 * editor ids are not, so anything that matches English words has to read these
 * too or it finds nothing on a translated load order.
 * @param {Object} holder - anything with an editorId (a spell or one effect)
 * @returns {string} lower case words, empty when there is no editor id
 */
function editorIdWords(holder) {
    var id = holder && holder.editorId;
    if (!id) return '';
    return String(id)
        .replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')   // an acronym runs into a word: "WTIce" -> "WT Ice"
        .replace(/([A-Za-z])([0-9])/g, '$1 $2')       // "DES100" -> "DES 100"
        .replace(/([0-9])([A-Za-z])/g, '$1 $2')       // "03Headless" -> "03 Headless"
        .replace(/([a-z])([A-Z])/g, '$1 $2')          // camelCase
        .replace(/[^A-Za-z0-9]+/g, ' ').toLowerCase();
}

/**
 * Every editor id of a spell, its own and its player facing effects', as words.
 * @param {Object} spell - a scanned spell
 * @returns {string}
 */
function spellIdWords(spell) {
    if (!spell) return '';
    var parts = [editorIdWords(spell)];
    var effects = spell.effects || [];
    for (var i = 0; i < effects.length; i++) {
        if (effects[i] && effects[i].flags && effects[i].flags.hideInUI) continue;
        parts.push(editorIdWords(effects[i]));
    }
    return parts.join(' ');
}