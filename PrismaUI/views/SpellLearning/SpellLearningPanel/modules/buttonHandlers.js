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
function startScan() {
    // Debounce: prevent rapid-fire scans (min 2s between scans)
    var now = Date.now();
    if (now - _lastScanTime < 2000) {
        console.log('[SpellLearning] Scan debounced - too soon since last scan');
        return;
    }
    _lastScanTime = now;

    // Always scan ALL spells - tome toggle is a client-side filter for primed count
    var statusMsg = 'Scanning all spells...';

    updateStatus(statusMsg);
    setStatusIcon('...');
    if (typeof updateScanStatus === 'function') updateScanStatus(statusMsg, 'working');

    var scanBtn = document.getElementById('scanBtn');
    if (scanBtn) {
        scanBtn.disabled = true;
        scanBtn.textContent = t('status.scanning');
    }

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
        updateStatus(t('status.nothingToExport'));
        setStatusIcon('!');
        if (typeof updateScanStatus === 'function') updateScanStatus(t('status.nothingToExport'), 'error');
        return;
    }

    if (window.callCpp) {
        if (typeof updateScanStatus === 'function') updateScanStatus(t('status.exportingScanData'), 'working');
        window.callCpp('SaveOutput', content);
            } else {
        updateStatus(t('status.cannotSaveNoBridge'));
        setStatusIcon('X');
    }
}

function onPasteTreeClick() {
    // Request clipboard content from C++ for tree import
    if (window.callCpp) {
        state.pasteTarget = 'import-textarea';
        window.callCpp('GetClipboard', '');
    } else {
        showImportError('Paste not available - C++ bridge required');
    }
}

function onCloseClick() {
    // Auto-save settings when close is requested
    autoSaveSettings();
    
    // Actually close the panel via C++
    if (window.callCpp) {
        window.callCpp('HidePanel', '');
    } else {
        updateStatus(t('status.pressHotkeyToClose'));
    }
}

// Called when panel is about to be hidden (from C++)
// onPanelHiding lives in cppCallbacks.js. A copy here was dead code: that file
// loads later and replaced this one, so the save it did never ran. The live
// handler stops the render loops and the polls, and saves at once rather than
// on a debounce, which is what a closing panel needs.
