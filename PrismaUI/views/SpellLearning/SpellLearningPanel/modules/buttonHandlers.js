/**
 * Button Handlers Module
 * Handles click events for major UI buttons
 * 
 * Depends on:
 * - modules/state.js (state, settings)
 * - modules/treeParser.js (TreeParser)
 * - modules/wheelRenderer.js (WheelRenderer)
 * - modules/uiHelpers.js (updateStatus, setStatusIcon)
 * - modules/spellCache.js (SpellCache)
 * - modules/homIcons.js (HomIcons.setLabel)
 * 
 * Exports (global):
 * - initializeButtonHandlers()
 * - onScanSpells()
 * - onLearnSpell()
 * - onUnlockSpell()
 * - onResetProgress()
 * - onExportTree()
 * - onImportTree()
 */

// =============================================================================
// BUTTON HANDLERS
// =============================================================================

function onScanClick() {
    console.log('[SpellLearning] Scan button clicked');
    startScan();
}

var _lastScanTime = 0;
/**
 * @param {Object} [options] - { auto: true }: the build at game start's own scan -
 *   not debounced (C++ keeps only the latest queued full scan anyway); any other
 *   scan that goes out ends that build first (autoBuild.js)
 * @returns {boolean} whether a scan went out
 */
function startScan(options) {
    var auto = !!(options && options.auto);
    // Debounce: prevent rapid-fire scans (min 2s between scans)
    var now = Date.now();
    if (!auto && now - _lastScanTime < 2000) {
        console.log('[SpellLearning] Scan debounced - too soon since last scan');
        return false;
    }
    _lastScanTime = now;
    if (!auto && typeof AutoBuild !== 'undefined') AutoBuild.yieldTo('scan');

    // Always scan ALL spells - tome toggle is a client-side filter for primed count
    var statusMsg = t('status.scanningAllSpells');

    updateStatus(statusMsg, 'status.scanningAllSpells');
    setStatusIcon('...');
    if (typeof updateScanStatus === 'function') updateScanStatus(statusMsg, 'working', 'status.scanningAllSpells');

    var scanBtn = document.getElementById('scanBtn');
    if (scanBtn) {
        scanBtn.disabled = true;
        HomIcons.setLabel(scanBtn, t('status.scanning'));
    }
    if (typeof ScanStage !== 'undefined') ScanStage.refresh();

    // Always include plugin field — needed for whitelist filtering even if user preset doesn't show it
    var scanFields = {};
    for (var key in state.fields) { scanFields[key] = state.fields[key]; }
    scanFields.plugin = true;

    var scanConfig = {
        fields: scanFields,
        treeRulesPrompt: getTreeRulesPrompt(),
        scanMode: 'all'
    };

    if (window.callCpp) {
        window.callCpp('ScanSpells', JSON.stringify(scanConfig));
    } else {
        console.warn('[SpellLearning] C++ bridge not ready, using mock data');
        setTimeout(function() {
            var mockData = {
                scanTimestamp: new Date().toISOString(),
                scanMode: 'all_spells',
                spellCount: 3,
                treeRulesPrompt: getTreeRulesPrompt(),
                spells: [
                    { formId: '0x00012FCD', name: 'Flames', school: 'Destruction', skillLevel: 'Novice' },
                    { formId: '0x00012FCE', name: 'Healing', school: 'Restoration', skillLevel: 'Novice' },
                    { formId: '0x00012FCF', name: 'Oakflesh', school: 'Alteration', skillLevel: 'Novice' }
                ]
            };
            updateSpellData(JSON.stringify(mockData));
        }, 500);
    }
    return true;
}

/**
 * The scan result as text, made when a button asks for it. The scan itself no
 * longer pretty-prints its ~20 MB into the hidden textarea, because doing that
 * froze the panel on every scan and then held the copy for the session.
 */
function _scanResultText() {
    var el = document.getElementById('outputArea');
    if (el && el.value && el.value.trim().length > 0) return el.value;   // pasted by hand
    if (typeof state === 'undefined' || !state.lastSpellData) return '';
    try {
        return JSON.stringify(state.lastSpellData, null, 2);
    } catch (e) {
        console.error('[SpellLearning] Could not render the scan result: ' + (e && e.message ? e.message : e));
        return '';
    }
}

function onSaveClick() {
    var outputAreaEl = document.getElementById('outputArea');
    var content = _scanResultText();
    
    if (!content || content.trim().length === 0) {
        updateStatus(t('status.nothingToExport'), 'status.nothingToExport');
        setStatusIcon('!');
        if (typeof updateScanStatus === 'function') updateScanStatus(t('status.nothingToExport'), 'error', 'status.nothingToExport');
        return;
    }

    if (window.callCpp) {
        if (typeof updateScanStatus === 'function') updateScanStatus(t('status.exportingScanData'), 'working', 'status.exportingScanData');
        window.callCpp('SaveOutput', content);
            } else {
        updateStatus(t('status.cannotSaveNoBridge'), 'status.cannotSaveNoBridge');
        setStatusIcon('X');
    }
}

function onPasteTreeClick() {
    // Request clipboard content from C++ for tree import
    if (window.callCpp) {
        state.pasteTarget = 'import-textarea';
        window.callCpp('GetClipboard', '');
    } else {
        showImportError(tOr('status.pasteUnavailable', null, 'Paste is not available here'));
    }
}

function onCloseClick() {
    if (typeof dismissFindSpell === 'function') dismissFindSpell();
    // Auto-save settings when close is requested
    autoSaveSettings();
    
    // Actually close the panel via C++
    if (window.callCpp) {
        window.callCpp('HidePanel', '');
    } else {
        updateStatus(t('status.pressHotkeyToClose'), 'status.pressHotkeyToClose');
    }
}

// Called when panel is about to be hidden (from C++)
// onPanelHiding lives in cppCallbacks.js. A copy here was dead code: that file
// loads later and replaced this one, so the save it did never ran. The live
// handler stops the render loops and the polls, and saves at once rather than
// on a debounce, which is what a closing panel needs.
