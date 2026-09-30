/**
 * Spell Learning Panel - PrismaUI Interface
 * Application logic: panel wiring, tabs, fullscreen, window position,
 * passive and early learning settings. Loads after the modules (see index.html);
 * initialization itself runs from modules/main.js.
 */
// =============================================================================
// INITIALIZATION
// =============================================================================

// Initialization lives in modules/main.js, which calls every function this
// block used to call and several more, each guarded. Running both meant every
// listener registered twice: one click on a settings toggle fired it twice, one
// Save wrote the config twice, and the Clear Tree double-click guard was
// defeated because a single click reached both copies of the handler.

// Fix Enter key in textareas - allow new lines
function initializeTextareaEnterKey() {
    var textareas = document.querySelectorAll('textarea');
    textareas.forEach(function(textarea) {
        textarea.addEventListener('keydown', function(e) {
            if (e.key === 'Enter' && !e.shiftKey && !e.ctrlKey) {
                // Allow default behavior (insert newline)
                e.stopPropagation();
                // Don't prevent default - we want the newline
            }
        });
        
        // Also handle keypress for better compatibility
        textarea.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                e.stopPropagation();
            }
        });
    });
    console.log('[SpellLearning] Textarea Enter key handling initialized for', textareas.length, 'textareas');
}

function initializePanel() {
    // Helper to safely add event listener (null-safe for removed elements)
    function safeAddListener(id, event, handler) {
        var el = document.getElementById(id);
        if (el) el.addEventListener(event, handler);
    }
    
    // Button event listeners (some may be removed during UI revamp)
    safeAddListener('scanBtn', 'click', onScanClick);
    safeAddListener('blacklistBtn', 'click', showBlacklistModal);
    safeAddListener('whitelistBtn', 'click', showWhitelistModal);
    safeAddListener('saveBtn', 'click', onSaveClick);

    safeAddListener('fullscreenBtn', 'click', toggleFullscreen);
    safeAddListener('closeBtn', 'click', onCloseClick);
    
    // Keyboard shortcuts - Escape and Tab close the panel
    initializeKeyboardShortcuts();
}

// =============================================================================
// FULLSCREEN TOGGLE
// =============================================================================

function toggleFullscreen() {
    var panel = document.getElementById('spellPanel');
    if (!panel) return;
    
    state.isFullscreen = !state.isFullscreen;
    panel.classList.toggle('fullscreen', state.isFullscreen);
    
    // Update fullscreen button icon
    var btn = document.getElementById('fullscreenBtn');
    if (btn) {
        btn.textContent = state.isFullscreen ? '[ ]' : '[ ]';
        btn.title = state.isFullscreen ? 'Exit Fullscreen' : 'Toggle Fullscreen';
    }
    
    // Save state
    settings.isFullscreen = state.isFullscreen;
    autoSaveSettings();
    if (typeof PanelSnap !== 'undefined') PanelSnap.apply();
    
    // Re-render tree if on tree tab
    if (state.currentTab === 'spellTree' && WheelRenderer.svg) {
        setTimeout(function() {
            WheelRenderer.updateTransform();
        }, 100);
    }
    
    console.log('[SpellLearning] Fullscreen:', state.isFullscreen);
}

// =============================================================================
// KEYBOARD SHORTCUTS
// =============================================================================

// One Escape press can arrive twice: as the view's keydown and from the
// plugin, which watches the game's input because the view does not always get
// the key (onNativeEscape). The second arrival within this window is the same press.
var ESCAPE_REPEAT_MS = 250;
var _lastEscapeAt = 0;

/**
 * Escape: with a spell selected on the tree, the first press only drops the
 * selection; the next one closes the panel.
 * @returns {boolean} false when it was the same press again
 */
function handleEscapePress() {
    var now = Date.now();
    if (now - _lastEscapeAt < ESCAPE_REPEAT_MS) return false;
    _lastEscapeAt = now;
    var onTree = state.currentTab === 'spellTree' &&
        document.querySelectorAll('.modal:not(.hidden)').length === 0;
    if (onTree && state.selectedNode && typeof clearSpellSelection === 'function') {
        clearSpellSelection();
        return true;
    }
    onCloseClick();
    return true;
}

// Called by the plugin when Escape is pressed with the panel open
window.onNativeEscape = function() {
    handleEscapePress();
};

function initializeKeyboardShortcuts() {
    document.addEventListener('keydown', function(e) {
        // Don't close if user is typing in an input/textarea
        var activeElement = document.activeElement;
        var isTyping = activeElement && (
            activeElement.tagName === 'INPUT' || 
            activeElement.tagName === 'TEXTAREA' ||
            activeElement.isContentEditable
        );
        
        // Escape: with a spell selected on the tree, the first press only drops
        // the selection; the next one closes (even when typing)
        if (e.key === 'Escape') {
            // A dialog's own field already used this press (Find Spell closes
            // itself): note the press so the plugin's copy of it does nothing
            if (e.defaultPrevented) {
                _lastEscapeAt = Date.now();
                return;
            }
            e.preventDefault();
            e.stopPropagation();
            handleEscapePress();
            return;
        }
        
        // Tab closes only when not typing in a field
        if (e.key === 'Tab' && !isTyping) {
            e.preventDefault();
            e.stopPropagation();
            onCloseClick();
            return;
        }
    });
    
    console.log('[SpellLearning] Keyboard shortcuts initialized (Escape/Tab to close)');
}

// =============================================================================
// TAB NAVIGATION
// =============================================================================

function initializeTabs() {
    if (state._tabsInitialized) return;
    state._tabsInitialized = true;

    // Header buttons toggle panels (Scan, Settings) over the default Spell Tree view
    var headerTabBtns = document.querySelectorAll('.header-btn[data-tab]');
    headerTabBtns.forEach(function(btn) {
        btn.addEventListener('click', function() {
            var tabId = this.getAttribute('data-tab');
            // Toggle: clicking active panel button returns to tree
            if (state.currentTab === tabId) {
                switchTab('spellTree');
            } else {
                switchTab(tabId);
            }
        });
    });

    // Return button — navigates back to spell tree view
    var returnBtn = document.getElementById('returnToTreeBtn');
    if (returnBtn) {
        returnBtn.addEventListener('click', function() {
            switchTab('spellTree');
        });
    }

    // Orphan repair button
    var orphanBtn = document.getElementById('orphanRepairBtn');
    if (orphanBtn) {
        orphanBtn.addEventListener('click', function() {
            if (typeof repairOrphans !== 'function') return;
            var result = repairOrphans();
            var msg = 'Repaired: removed ' + result.removedPrereqs + ' bad prereqs, reconnected ' +
                result.reconnectedSubtrees + ' subtrees (' + result.nodesRecovered + ' nodes recovered)';
            console.log('[OrphanRepair] ' + msg);
            if (typeof updateOrphanRepairButton === 'function') {
                updateOrphanRepairButton();
            }
        });
    }
}

function switchTab(tabId) {
    // Auto-save settings when leaving settings tab
    if (state.currentTab === 'settings' && tabId !== 'settings') {
        autoSaveSettings();
    }

    state.currentTab = tabId;

    // The tree is only drawn while its tab is in front (CanvasRenderer.startRenderLoop
    // refuses otherwise); it used to go on animating behind the settings page
    if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.canvas) {
        if (tabId === 'spellTree') {
            CanvasRenderer.startRenderLoop();
            CanvasRenderer._needsRender = true;
        } else {
            CanvasRenderer.stopRenderLoop();
        }
    }

    // Update header button active states
    document.querySelectorAll('.header-btn[data-tab]').forEach(function(btn) {
        btn.classList.toggle('active', btn.getAttribute('data-tab') === tabId);
    });

    document.querySelectorAll('.tab-content').forEach(function(content) {
        content.classList.remove('active');
    });

    // Show/hide Return button based on current tab
    var returnBtn = document.getElementById('returnToTreeBtn');
    if (returnBtn) {
        returnBtn.style.display = (tabId !== 'spellTree') ? '' : 'none';
    }

    if (tabId === 'spellScan') {
        document.getElementById('contentSpellScan').classList.add('active');
    } else if (tabId === 'spellTree') {
        document.getElementById('contentSpellTree').classList.add('active');
        // Initialize tree viewer if not done yet
        if (!state.treeInitialized) {
            initializeTreeViewer();
        }
        // Update transform on tab switch
        if (WheelRenderer.svg) {
            setTimeout(function() { WheelRenderer.updateTransform(); }, 50);
        }
    } else if (tabId === 'settings') {
        document.getElementById('contentSettings').classList.add('active');
    }
}

// =============================================================================
// TREE RULES
// =============================================================================

// The tree rules written into the scan export's llmPrompt: the player's saved
// rules (C++ sends them with updatePrompt when the panel is ready) or the default.
// The editor that changed them is not in index.html.
function getTreeRulesPrompt() {
    return state.originalPrompt || (typeof DEFAULT_TREE_RULES !== 'undefined' ? DEFAULT_TREE_RULES : '');
}

// =============================================================================
// DRAGGING & RESIZING
// =============================================================================

function applyWindowPositionAndSize() {
    var panel = document.getElementById('spellPanel');
    if (!panel) return;
    
    // Apply saved size
    if (settings.windowWidth && settings.windowHeight) {
        panel.style.width = settings.windowWidth + 'px';
        panel.style.height = settings.windowHeight + 'px';
        console.log('[SpellLearning] Applied window size:', settings.windowWidth, 'x', settings.windowHeight);
    }
    
    // Apply saved position
    if (settings.windowX !== null && settings.windowY !== null) {
        panel.style.transform = 'none';
        panel.style.left = settings.windowX + 'px';
        panel.style.top = settings.windowY + 'px';
        console.log('[SpellLearning] Applied window position:', settings.windowX, settings.windowY);
    }
    if (typeof PanelSnap !== 'undefined') PanelSnap.apply();
}

function applyFullscreenState() {
    var panel = document.getElementById('spellPanel');
    if (!panel) return;
    
    if (state.isFullscreen) {
        panel.classList.add('fullscreen');
        console.log('[SpellLearning] Applied fullscreen state: ON');
    } else {
        panel.classList.remove('fullscreen');
    }
    
    // Update fullscreen button icon
    var btn = document.getElementById('fullscreenBtn');
    if (btn) {
        btn.title = state.isFullscreen ? 'Exit Fullscreen' : 'Toggle Fullscreen';
    }
    if (typeof PanelSnap !== 'undefined') PanelSnap.apply();
}

function initializeDragging() {
    var panel = document.getElementById('spellPanel');
    var header = document.getElementById('panelHeader');
    
    var startX, startY, initialX, initialY;
    
    header.addEventListener('mousedown', function(e) {
        if (e.target.closest('.header-btn')) return;
        
        state.isDragging = true;
        startX = e.clientX;
        startY = e.clientY;
        
        // Whole pixels (PanelSnap): the margin that centred it goes too
        var rect = panel.getBoundingClientRect();
        initialX = Math.round(rect.left);
        initialY = Math.round(rect.top);
        
        panel.style.marginLeft = '';
        panel.style.marginTop = '';
        panel.style.transform = 'none';
        panel.style.left = initialX + 'px';
        panel.style.top = initialY + 'px';
        
        document.addEventListener('mousemove', onDrag);
        document.addEventListener('mouseup', onDragEnd);
    });
    
    function onDrag(e) {
        if (!state.isDragging) return;
        var dx = e.clientX - startX;
        var dy = e.clientY - startY;
        panel.style.left = Math.round(initialX + dx) + 'px';
        panel.style.top = Math.round(initialY + dy) + 'px';
    }
    
    function onDragEnd() {
        state.isDragging = false;
        document.removeEventListener('mousemove', onDrag);
        document.removeEventListener('mouseup', onDragEnd);
        
        // Save window position
        var rect = panel.getBoundingClientRect();
        settings.windowX = Math.round(rect.left);
        settings.windowY = Math.round(rect.top);
        console.log('[SpellLearning] Window position saved:', settings.windowX, settings.windowY);
        autoSaveSettings();
    }
}

function initializeResizing() {
    var panel = document.getElementById('spellPanel');
    var handle = document.getElementById('resizeHandle');
    
    var startX, startY, startWidth, startHeight;
    
    handle.addEventListener('mousedown', function(e) {
        state.isResizing = true;
        startX = e.clientX;
        startY = e.clientY;
        startWidth = panel.offsetWidth;
        startHeight = panel.offsetHeight;
        
        document.addEventListener('mousemove', onResize);
        document.addEventListener('mouseup', onResizeEnd);
        e.preventDefault();
    });
    
    function onResize(e) {
        if (!state.isResizing) return;
        var newWidth = Math.max(500, startWidth + (e.clientX - startX));
        var newHeight = Math.max(400, startHeight + (e.clientY - startY));
        panel.style.width = newWidth + 'px';
        panel.style.height = newHeight + 'px';
    }
    
    function onResizeEnd() {
        state.isResizing = false;
        document.removeEventListener('mousemove', onResize);
        document.removeEventListener('mouseup', onResizeEnd);
        
        // Save window size
        settings.windowWidth = panel.offsetWidth;
        settings.windowHeight = panel.offsetHeight;
        console.log('[SpellLearning] Window size saved:', settings.windowWidth, 'x', settings.windowHeight);
        autoSaveSettings();
        if (typeof PanelSnap !== 'undefined') PanelSnap.apply();
    }
}

// =============================================================================
// PASSIVE LEARNING SETTINGS
// =============================================================================

function initializePassiveLearningSettings() {
    // Enable toggle
    var enableToggle = document.getElementById('passiveLearningToggle');
    if (enableToggle) {
        enableToggle.checked = settings.passiveLearning.enabled;
        enableToggle.addEventListener('change', function() {
            settings.passiveLearning.enabled = this.checked;
            updatePassiveLearningVisibility();
            console.log('[SpellLearning] Passive learning enabled:', this.checked);
            autoSaveSettings();
        });
    }

    // Scope toggle (segmented)
    initSegmentedToggle('passiveScopeToggle', settings.passiveLearning.scope, function(value) {
        settings.passiveLearning.scope = value;
        console.log('[SpellLearning] Passive learning scope:', value);
        autoSaveSettings();
    });

    // XP per game hour slider
    var xpSlider = document.getElementById('passiveXpPerHourSlider');
    var xpValue = document.getElementById('passiveXpPerHourValue');
    if (xpSlider) {
        xpSlider.value = settings.passiveLearning.xpPerGameHour;
        if (xpValue) xpValue.textContent = settings.passiveLearning.xpPerGameHour;
        updateSliderFillGlobal(xpSlider);
        xpSlider.addEventListener('input', function() {
            var val = parseInt(this.value);
            settings.passiveLearning.xpPerGameHour = val;
            if (xpValue) xpValue.textContent = val;
            updateSliderFillGlobal(this);
            autoSaveSettings();
        });
    }

    // Max tier inputs
    var tierMap = {
        'passiveMaxNovice': 'novice',
        'passiveMaxApprentice': 'apprentice',
        'passiveMaxAdept': 'adept',
        'passiveMaxExpert': 'expert',
        'passiveMaxMaster': 'master'
    };
    for (var elId in tierMap) {
        (function(elementId, tierKey) {
            var input = document.getElementById(elementId);
            if (input) {
                input.value = settings.passiveLearning.maxByTier[tierKey];
                input.addEventListener('change', function() {
                    var val = Math.max(0, Math.min(100, parseInt(this.value) || 0));
                    this.value = val;
                    settings.passiveLearning.maxByTier[tierKey] = val;
                    console.log('[SpellLearning] Passive max ' + tierKey + ':', val);
                    autoSaveSettings();
                });
            }
        })(elId, tierMap[elId]);
    }

    // Initial visibility
    updatePassiveLearningVisibility();
    console.log('[SpellLearning] Passive learning settings initialized');
}

function updatePassiveLearningVisibility() {
    var controls = document.querySelector('.passive-learning-controls');
    if (!controls) return;
    var rows = controls.querySelectorAll('.setting-row-inline, .setting-row, .slider-row, .settings-subsection, .tier-xp-grid');
    var isEnabled = settings.passiveLearning.enabled;
    // Skip the first row (the enable toggle itself)
    for (var i = 1; i < rows.length; i++) {
        rows[i].style.opacity = isEnabled ? '1' : '0.5';
        rows[i].style.pointerEvents = isEnabled ? '' : 'none';
    }
}

function updatePassiveLearningUI() {
    var enableToggle = document.getElementById('passiveLearningToggle');
    if (enableToggle) enableToggle.checked = settings.passiveLearning.enabled;

    setSegmentedToggleValue('passiveScopeToggle', settings.passiveLearning.scope);

    var xpSlider = document.getElementById('passiveXpPerHourSlider');
    var xpValue = document.getElementById('passiveXpPerHourValue');
    if (xpSlider) {
        xpSlider.value = settings.passiveLearning.xpPerGameHour;
        if (xpValue) xpValue.textContent = settings.passiveLearning.xpPerGameHour;
        updateSliderFillGlobal(xpSlider);
    }

    var tierMap = {
        'passiveMaxNovice': 'novice',
        'passiveMaxApprentice': 'apprentice',
        'passiveMaxAdept': 'adept',
        'passiveMaxExpert': 'expert',
        'passiveMaxMaster': 'master'
    };
    for (var elId in tierMap) {
        var input = document.getElementById(elId);
        if (input) input.value = settings.passiveLearning.maxByTier[tierMap[elId]];
    }

    updatePassiveLearningVisibility();
}

// =============================================================================
// EARLY SPELL LEARNING SETTINGS
// =============================================================================

function initializeEarlyLearningSettings() {
    // Enable toggle
    var enabledToggle = document.getElementById('earlyLearningEnabledToggle');
    if (enabledToggle) {
        enabledToggle.checked = settings.earlySpellLearning.enabled;
        enabledToggle.addEventListener('change', function() {
            settings.earlySpellLearning.enabled = this.checked;
            updateEarlyLearningSettingsVisibility();
            console.log('[SpellLearning] Early learning enabled:', settings.earlySpellLearning.enabled);

        });
    }
    
    // Unlock threshold slider
    setupEarlyLearningSlider('unlockThreshold', 'unlockThreshold', '%');
    
    // Min effectiveness slider
    setupEarlyLearningSlider('minEffectiveness', 'minEffectiveness', '%');
    
    // Max effectiveness slider
    setupEarlyLearningSlider('maxEffectiveness', 'maxEffectiveness', '%');
    
    // Self-cast required slider
    setupEarlyLearningSlider('selfCastRequired', 'selfCastRequiredAt', '%');
    
    // Self-cast multiplier slider
    setupEarlyLearningSlider('selfCastMultiplier', 'selfCastXPMultiplier', '%');
    
    // Binary threshold slider
    setupEarlyLearningSlider('binaryThreshold', 'binaryEffectThreshold', '%');
    
    // Modify game display toggle
    var gameDisplayToggle = document.getElementById('modifyGameDisplayToggle');
    if (gameDisplayToggle) {
        gameDisplayToggle.checked = settings.earlySpellLearning.modifyGameDisplay !== false;
        gameDisplayToggle.addEventListener('change', function() {
            settings.earlySpellLearning.modifyGameDisplay = this.checked;
            console.log('[SpellLearning] Modify game display:', this.checked);

        });
    }
    
    // Power steps configuration
    initializePowerStepsUI();
    
    // Reset power steps button
    var resetPowerStepsBtn = document.getElementById('resetPowerStepsBtn');
    if (resetPowerStepsBtn) {
        resetPowerStepsBtn.addEventListener('click', function() {
            resetPowerStepsToDefaults();
        });
    }
    
    // Initial visibility
    updateEarlyLearningSettingsVisibility();
}

// Default power steps configuration
var DEFAULT_POWER_STEPS = [
    { xp: 25, power: 20, label: "Budding" },
    { xp: 40, power: 35, label: "Developing" },
    { xp: 55, power: 50, label: "Practicing" },
    { xp: 70, power: 65, label: "Advancing" },
    { xp: 85, power: 80, label: "Refining" }
];

function initializePowerStepsUI() {
    var container = document.getElementById('powerStepsContainer');
    if (!container) return;
    
    // Ensure powerSteps exists
    if (!settings.earlySpellLearning.powerSteps) {
        settings.earlySpellLearning.powerSteps = JSON.parse(JSON.stringify(DEFAULT_POWER_STEPS));
    }
    
    renderPowerSteps();
}

function renderPowerSteps() {
    var container = document.getElementById('powerStepsContainer');
    if (!container) return;
    
    container.innerHTML = '';
    
    var steps = settings.earlySpellLearning.powerSteps;
    
    steps.forEach(function(step, index) {
        var row = document.createElement('div');
        row.className = 'power-step-row';
        row.dataset.index = index;
        
        // Stage label
        var labelSpan = document.createElement('span');
        labelSpan.className = 'power-step-label';
        labelSpan.textContent = t('progression.stageN', {n: index + 1});
        
        // XP threshold input
        var xpInput = document.createElement('input');
        xpInput.type = 'number';
        xpInput.className = 'power-step-input';
        xpInput.value = step.xp;
        xpInput.min = 1;
        xpInput.max = 99;
        xpInput.dataset.index = index;
        xpInput.dataset.field = 'xp';
        xpInput.addEventListener('change', onPowerStepInputChange);
        
        var xpUnit = document.createElement('span');
        xpUnit.className = 'power-step-unit';
        xpUnit.textContent = t('progression.xpUnit');
        
        // Power level input
        var powerInput = document.createElement('input');
        powerInput.type = 'number';
        powerInput.className = 'power-step-input';
        powerInput.value = step.power;
        powerInput.min = 1;
        powerInput.max = 99;
        powerInput.dataset.index = index;
        powerInput.dataset.field = 'power';
        powerInput.addEventListener('change', onPowerStepInputChange);
        
        var powerUnit = document.createElement('span');
        powerUnit.className = 'power-step-unit';
        powerUnit.textContent = t('progression.powerUnit');
        
        // Name input
        var nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.className = 'power-step-name';
        nameInput.value = step.label;
        nameInput.placeholder = t('progression.stageNamePlaceholder');
        nameInput.dataset.index = index;
        nameInput.dataset.field = 'label';
        nameInput.addEventListener('change', onPowerStepInputChange);
        
        row.appendChild(labelSpan);
        row.appendChild(xpInput);
        row.appendChild(xpUnit);
        row.appendChild(powerInput);
        row.appendChild(powerUnit);
        row.appendChild(nameInput);
        
        container.appendChild(row);
    });
    
    // Add "Mastered" row (readonly)
    var masteredRow = document.createElement('div');
    masteredRow.className = 'power-step-row';
    masteredRow.style.opacity = '0.7';
    
    var masteredLabel = document.createElement('span');
    masteredLabel.className = 'power-step-label';
    masteredLabel.textContent = t('progression.stageFinal');
    masteredLabel.style.color = 'var(--accent-gold, #ffd700)';
    
    var masteredXp = document.createElement('span');
    masteredXp.className = 'power-step-unit';
    masteredXp.textContent = t('progression.fullXp');
    masteredXp.style.marginLeft = '10px';
    
    var masteredPower = document.createElement('span');
    masteredPower.className = 'power-step-unit';
    masteredPower.textContent = t('progression.fullPower');
    masteredPower.style.marginLeft = '20px';
    
    var masteredName = document.createElement('span');
    masteredName.className = 'power-step-unit';
    masteredName.textContent = t('progression.masteredFixed');
    masteredName.style.marginLeft = '20px';
    
    masteredRow.appendChild(masteredLabel);
    masteredRow.appendChild(masteredXp);
    masteredRow.appendChild(document.createElement('span')); // spacer
    masteredRow.appendChild(masteredPower);
    masteredRow.appendChild(document.createElement('span')); // spacer  
    masteredRow.appendChild(masteredName);
    
    container.appendChild(masteredRow);
}

function onPowerStepInputChange(e) {
    var index = parseInt(e.target.dataset.index);
    var field = e.target.dataset.field;
    var value = field === 'label' ? e.target.value : parseInt(e.target.value);
    
    if (field !== 'label') {
        value = Math.max(1, Math.min(99, value || 1));
        e.target.value = value;
    }
    
    settings.earlySpellLearning.powerSteps[index][field] = value;
    
    // Sort steps by XP threshold to maintain order
    settings.earlySpellLearning.powerSteps.sort(function(a, b) {
        return a.xp - b.xp;
    });
    
    // Re-render if order changed
    renderPowerSteps();
    
    console.log('[SpellLearning] Power step updated:', settings.earlySpellLearning.powerSteps);

}

function resetPowerStepsToDefaults() {
    settings.earlySpellLearning.powerSteps = JSON.parse(JSON.stringify(DEFAULT_POWER_STEPS));
    renderPowerSteps();
    console.log('[SpellLearning] Power steps reset to defaults');

}

function setupEarlyLearningSlider(elementBaseName, settingName, suffix) {
    var slider = document.getElementById(elementBaseName + 'Slider');
    var valueEl = document.getElementById(elementBaseName + 'Value');
    
    if (slider) {
        slider.value = settings.earlySpellLearning[settingName];
        if (valueEl) valueEl.textContent = settings.earlySpellLearning[settingName] + suffix;
        // Update slider fill visual
        updateSliderFillGlobal(slider);
        
        slider.addEventListener('input', function() {
            var value = parseInt(this.value);
            settings.earlySpellLearning[settingName] = value;
            if (valueEl) valueEl.textContent = value + suffix;
            // Update slider fill visual
            updateSliderFillGlobal(this);

        });
    }
}

function updateEarlyLearningSettingsVisibility() {
    var rows = [
        'unlockThresholdRow',
        'minEffectivenessRow', 
        'maxEffectivenessRow',
        'selfCastRequiredRow',
        'selfCastMultiplierRow',
        'binaryThresholdRow'
    ];
    
    var isEnabled = settings.earlySpellLearning.enabled;
    
    rows.forEach(function(rowId) {
        var row = document.getElementById(rowId);
        if (row) {
            row.style.opacity = isEnabled ? '1' : '0.5';
            row.style.pointerEvents = isEnabled ? '' : 'none';
        }
    });
}

function updateEarlyLearningUI() {
    // Update toggle
    var enabledToggle = document.getElementById('earlyLearningEnabledToggle');
    if (enabledToggle) enabledToggle.checked = settings.earlySpellLearning.enabled;
    
    // Update modifyGameDisplay toggle
    var gameDisplayToggle = document.getElementById('modifyGameDisplayToggle');
    if (gameDisplayToggle) {
        gameDisplayToggle.checked = settings.earlySpellLearning.modifyGameDisplay !== false;
    }
    
    // Update sliders
    var sliderMappings = [
        { element: 'unlockThreshold', setting: 'unlockThreshold' },
        { element: 'minEffectiveness', setting: 'minEffectiveness' },
        { element: 'maxEffectiveness', setting: 'maxEffectiveness' },
        { element: 'selfCastRequired', setting: 'selfCastRequiredAt' },
        { element: 'selfCastMultiplier', setting: 'selfCastXPMultiplier' },
        { element: 'binaryThreshold', setting: 'binaryEffectThreshold' }
    ];
    
    sliderMappings.forEach(function(mapping) {
        var slider = document.getElementById(mapping.element + 'Slider');
        var valueEl = document.getElementById(mapping.element + 'Value');
        if (slider && settings.earlySpellLearning[mapping.setting] !== undefined) {
            slider.value = settings.earlySpellLearning[mapping.setting];
            if (valueEl) valueEl.textContent = settings.earlySpellLearning[mapping.setting] + '%';
            // Update slider fill visual
            updateSliderFillGlobal(slider);
        }
    });
    
    // Update visibility
    updateEarlyLearningSettingsVisibility();
}

console.log('[SpellLearning] Script loaded');
