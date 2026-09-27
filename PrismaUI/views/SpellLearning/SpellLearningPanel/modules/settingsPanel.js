/**
 * Settings Panel Module
 * Handles all settings UI initialization and config management
 * 
 * Depends on:
 * - modules/constants.js (KEY_CODES)
 * - modules/state.js (settings, settingsPresets, xpOverrides)
 * - modules/colorUtils.js (applySchoolColorsToCSS)
 * - modules/uiHelpers.js (updateStatus, updateSliderFillGlobal)
 * 
 * Exports (global):
 * - initializeSettings()
 * - loadSettings()
 * - saveSettings()
 * - autoSaveSettings()
 * - saveUnifiedConfig()
 * - resetSettings()
 * - window.onUnifiedConfigLoaded
 */

// =============================================================================
// SEGMENTED TOGGLE HELPER
// =============================================================================

/**
 * Initialize a segmented toggle control.
 * @param {string} containerId - The id of the .segmented-toggle div
 * @param {string} activeValue - The currently active value
 * @param {function} onChange - Callback with selected value string
 */
function initSegmentedToggle(containerId, activeValue, onChange) {
    var container = document.getElementById(containerId);
    if (!container) return;
    var btns = container.querySelectorAll('.seg-btn');
    // Set initial active state
    for (var i = 0; i < btns.length; i++) {
        if (btns[i].getAttribute('data-value') === activeValue) {
            btns[i].classList.add('active');
        } else {
            btns[i].classList.remove('active');
        }
    }
    // Click handlers
    container.addEventListener('click', function(e) {
        var btn = e.target.closest('.seg-btn');
        if (!btn || btn.classList.contains('active')) return;
        var siblings = container.querySelectorAll('.seg-btn');
        for (var j = 0; j < siblings.length; j++) siblings[j].classList.remove('active');
        btn.classList.add('active');
        if (onChange) onChange(btn.getAttribute('data-value'));
    });
}

/**
 * Set the active button on a segmented toggle without triggering callbacks.
 */
function setSegmentedToggleValue(containerId, value) {
    var container = document.getElementById(containerId);
    if (!container) return;
    var btns = container.querySelectorAll('.seg-btn');
    for (var i = 0; i < btns.length; i++) {
        if (btns[i].getAttribute('data-value') === value) {
            btns[i].classList.add('active');
        } else {
            btns[i].classList.remove('active');
        }
    }
}

/**
 * Enable or disable (dim) a segmented toggle.
 */
function setSegmentedToggleEnabled(containerId, enabled) {
    var container = document.getElementById(containerId);
    if (!container) return;
    container.style.opacity = enabled ? '1' : '0.5';
    container.style.pointerEvents = enabled ? '' : 'none';
}

// =============================================================================
// SETTINGS PANEL
// =============================================================================

/**
 * Update visibility of developer-only elements based on developer mode setting.
 * @param {boolean} enabled - Whether developer mode is enabled
 */
function updateDeveloperModeVisibility(enabled) {
    console.log('[SpellLearning] Updating developer mode visibility:', enabled);
    
    if (typeof PerfMeter !== 'undefined') PerfMeter.syncVisibility();

    // Get all elements with dev-only class
    var devOnlyElements = document.querySelectorAll('.dev-only');
    devOnlyElements.forEach(function(el) {
        if (enabled) {
            el.classList.remove('hidden');
            el.style.display = '';
        } else {
            el.classList.add('hidden');
            el.style.display = 'none';
        }
    });
    
    // Show/hide debug options section in settings
    var debugOptionsSection = document.getElementById('debugOptionsSection');
    if (debugOptionsSection) {
        if (enabled) {
            debugOptionsSection.classList.remove('hidden');
        } else {
            debugOptionsSection.classList.add('hidden');
        }
    }
}

function initializeSettings() {
    // Load saved settings
    loadSettings();
    
    // Verbose logging toggle
    var verboseToggle = document.getElementById('verboseLogToggle');
    if (verboseToggle) {
        verboseToggle.checked = settings.verboseLogging;
        verboseToggle.addEventListener('change', function() {
            settings.verboseLogging = this.checked;
        });
    }
    
    // Debug grid toggle - shows grid candidate positions
    var debugGridToggle = document.getElementById('debugGridToggle');
    if (debugGridToggle) {
        debugGridToggle.checked = settings.showDebugGrid || false;
        debugGridToggle.addEventListener('change', function() {
            settings.showDebugGrid = this.checked;
            console.log('[SpellLearning] Debug grid:', settings.showDebugGrid);
            
            // Update SVG renderer
            if (typeof WheelRenderer !== 'undefined') {
                WheelRenderer.showDebugGrid = this.checked;
                if (WheelRenderer.debugGridLayer) {
                    WheelRenderer.debugGridLayer.style.display = this.checked ? 'block' : 'none';
                }
                if (this.checked) {
                    WheelRenderer.renderDebugGrid();
                }
            }
            
            // Update Canvas renderer
            if (typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer.showDebugGrid = this.checked;
                CanvasRenderer._needsRender = true;
            }
        });
    }
    
    // Developer mode toggle - shows/hides advanced options
    var devModeToggle = document.getElementById('developerModeToggle');
    var debugOptionsSection = document.getElementById('debugOptionsSection');
    if (devModeToggle) {
        devModeToggle.checked = settings.developerMode || false;
        updateDeveloperModeVisibility(settings.developerMode || false);
        
        devModeToggle.addEventListener('change', function() {
            settings.developerMode = this.checked;
            console.log('[SpellLearning] Developer mode:', settings.developerMode);
            updateDeveloperModeVisibility(settings.developerMode);
        });
    }
    
    // Cheat mode toggle - includes all debug features
    var cheatToggle = document.getElementById('cheatModeToggle');
    var cheatInfo = document.getElementById('cheatModeInfo');
    if (cheatToggle) {
        cheatToggle.checked = settings.cheatMode;
        if (cheatInfo) cheatInfo.classList.toggle('hidden', !settings.cheatMode);
        
        cheatToggle.addEventListener('change', function() {
            settings.cheatMode = this.checked;
            console.log('[SpellLearning] Cheat mode:', settings.cheatMode);
            if (cheatInfo) cheatInfo.classList.toggle('hidden', !settings.cheatMode);
            autoSaveSettings();
            // Re-render tree to show/hide all spell names
            if (state.treeData) {
                WheelRenderer.render();
                // Also refresh canvas renderer
                if (typeof CanvasRenderer !== 'undefined') {
                    CanvasRenderer.refresh();
                }
                if (typeof SmartRenderer !== 'undefined') {
                    SmartRenderer.refresh();
                }
            }
            // Update button visibility if node is selected
            if (state.selectedNode) {
                showSpellDetails(state.selectedNode);
                updateDetailsProgression(state.selectedNode);
            }
        });
    }
    
    // Discovery mode toggle
    var discoveryModeToggle = document.getElementById('discoveryModeToggle');
    if (discoveryModeToggle) {
        discoveryModeToggle.checked = settings.discoveryMode;
        discoveryModeToggle.addEventListener('change', function() {
            settings.discoveryMode = this.checked;
            console.log('[SpellLearning] Discovery mode:', settings.discoveryMode);
            // Re-render tree to show/hide locked nodes
            if (state.treeData) {
                WheelRenderer.render();
                // Also refresh canvas renderer (rebuilds discovery visibility)
                if (typeof CanvasRenderer !== 'undefined') {
                    CanvasRenderer.refresh();
                }
                if (typeof SmartRenderer !== 'undefined') {
                    SmartRenderer.refresh();
                }
            }
        });
    }
    
    // Show root spell names toggle (for discovery mode)
    var showRootNamesToggle = document.getElementById('showRootSpellNamesToggle');
    if (showRootNamesToggle) {
        showRootNamesToggle.checked = settings.showRootSpellNames;
        showRootNamesToggle.addEventListener('change', function() {
            settings.showRootSpellNames = this.checked;
            console.log('[SpellLearning] Show root spell names:', settings.showRootSpellNames);
            // Re-render tree
            if (state.treeData) {
                WheelRenderer.render();
            }
        });
    }
    
    // ===========================================================================
    // SPELL TOME LEARNING SETTINGS
    // ===========================================================================
    
    // Use Progression System toggle (Vanilla vs XP system)
    var useProgressionToggle = document.getElementById('useProgressionSystemToggle');
    if (useProgressionToggle) {
        useProgressionToggle.checked = settings.spellTomeLearning.useProgressionSystem;
        useProgressionToggle.addEventListener('change', function() {
            settings.spellTomeLearning.useProgressionSystem = this.checked;
            console.log('[SpellLearning] Use progression system:', this.checked);
            // Update description
            var modeDesc = document.getElementById('tomeLearningModeDesc');
            if (modeDesc) {
                if (this.checked) {
                    modeDesc.textContent = t('settings.tomeLearning.progressionModeDesc');
                } else {
                    modeDesc.textContent = t('settings.tomeLearning.vanillaModeDesc');
                }
            }
            // Show/hide progression-specific settings
            var xpGrantRow = document.getElementById('tomeXpGrantRow');
            if (xpGrantRow) xpGrantRow.style.display = this.checked ? '' : 'none';
            
            scheduleAutoSave();
        });
        // Initial visibility
        var xpGrantRow = document.getElementById('tomeXpGrantRow');
        if (xpGrantRow) xpGrantRow.style.display = settings.spellTomeLearning.useProgressionSystem ? '' : 'none';
    }
    
    // Tome XP Grant slider
    var tomeXpGrantSlider = document.getElementById('tomeXpGrantSlider');
    var tomeXpGrantValue = document.getElementById('tomeXpGrantValue');
    if (tomeXpGrantSlider) {
        tomeXpGrantSlider.value = settings.spellTomeLearning.xpPercentToGrant;
        if (tomeXpGrantValue) tomeXpGrantValue.textContent = settings.spellTomeLearning.xpPercentToGrant + '%';
        updateSliderFillGlobal(tomeXpGrantSlider);
        
        tomeXpGrantSlider.addEventListener('input', function() {
            var value = parseInt(this.value);
            settings.spellTomeLearning.xpPercentToGrant = value;
            if (tomeXpGrantValue) tomeXpGrantValue.textContent = value + '%';
            updateSliderFillGlobal(this);
            scheduleAutoSave();
        });
    }
    
    // Tome Inventory Boost toggle
    var tomeInventoryBoostToggle = document.getElementById('tomeInventoryBoostToggle');
    if (tomeInventoryBoostToggle) {
        tomeInventoryBoostToggle.checked = settings.spellTomeLearning.tomeInventoryBoost;
        tomeInventoryBoostToggle.addEventListener('change', function() {
            settings.spellTomeLearning.tomeInventoryBoost = this.checked;
            console.log('[SpellLearning] Tome inventory boost:', this.checked);
            // Show/hide boost slider
            var boostRow = document.getElementById('tomeInventoryBoostRow');
            if (boostRow) boostRow.style.display = this.checked ? '' : 'none';
            scheduleAutoSave();
        });
        // Initial visibility
        var boostRow = document.getElementById('tomeInventoryBoostRow');
        if (boostRow) boostRow.style.display = settings.spellTomeLearning.tomeInventoryBoost ? '' : 'none';
    }
    
    // Tome Inventory Boost Percent slider
    var tomeBoostSlider = document.getElementById('tomeInventoryBoostSlider');
    var tomeBoostValue = document.getElementById('tomeInventoryBoostValue');
    if (tomeBoostSlider) {
        tomeBoostSlider.value = settings.spellTomeLearning.tomeInventoryBoostPercent;
        if (tomeBoostValue) tomeBoostValue.textContent = '+' + settings.spellTomeLearning.tomeInventoryBoostPercent + '%';
        updateSliderFillGlobal(tomeBoostSlider);
        
        tomeBoostSlider.addEventListener('input', function() {
            var value = parseInt(this.value);
            settings.spellTomeLearning.tomeInventoryBoostPercent = value;
            if (tomeBoostValue) tomeBoostValue.textContent = '+' + value + '%';
            updateSliderFillGlobal(this);
            scheduleAutoSave();
        });
    }
    
    // Require Prerequisites toggle
    var requirePrereqsToggle = document.getElementById('tomeRequirePrereqsToggle');
    if (requirePrereqsToggle) {
        requirePrereqsToggle.checked = settings.spellTomeLearning.requirePrereqs;
        requirePrereqsToggle.addEventListener('change', function() {
            settings.spellTomeLearning.requirePrereqs = this.checked;
            console.log('[SpellLearning] Tome require prereqs:', this.checked);
            // Show/hide child setting
            var allPrereqsRow = document.getElementById('tomeRequireAllPrereqsRow');
            if (allPrereqsRow) allPrereqsRow.style.display = this.checked ? '' : 'none';
            scheduleAutoSave();
        });
        // Initial visibility
        var allPrereqsRow = document.getElementById('tomeRequireAllPrereqsRow');
        if (allPrereqsRow) allPrereqsRow.style.display = settings.spellTomeLearning.requirePrereqs ? '' : 'none';
    }
    
    // Require ALL Prerequisites toggle (child setting)
    var requireAllPrereqsToggle = document.getElementById('tomeRequireAllPrereqsToggle');
    if (requireAllPrereqsToggle) {
        requireAllPrereqsToggle.checked = settings.spellTomeLearning.requireAllPrereqs;
        requireAllPrereqsToggle.addEventListener('change', function() {
            settings.spellTomeLearning.requireAllPrereqs = this.checked;
            console.log('[SpellLearning] Tome require ALL prereqs:', this.checked);
            scheduleAutoSave();
        });
    }
    
    // Require Skill Level toggle
    var requireSkillLevelToggle = document.getElementById('tomeRequireSkillLevelToggle');
    if (requireSkillLevelToggle) {
        requireSkillLevelToggle.checked = settings.spellTomeLearning.requireSkillLevel;
        requireSkillLevelToggle.addEventListener('change', function() {
            settings.spellTomeLearning.requireSkillLevel = this.checked;
            console.log('[SpellLearning] Tome require skill level:', this.checked);
            scheduleAutoSave();
        });
    }
    
    // =========================================================================
    // NOTIFICATION SETTINGS
    // =========================================================================
    
    // Ensure notifications object exists
    if (!settings.notifications) {
        settings.notifications = {
            weakenedSpellNotifications: true,
            weakenedSpellInterval: 10
        };
    }
    
    // Weakened spell notifications toggle
    var weakenedNotificationsToggle = document.getElementById('weakenedNotificationsToggle');
    var notificationIntervalRow = document.getElementById('notificationIntervalRow');
    if (weakenedNotificationsToggle) {
        weakenedNotificationsToggle.checked = settings.notifications.weakenedSpellNotifications;
        // Show/hide interval row based on toggle state
        if (notificationIntervalRow) {
            notificationIntervalRow.style.display = weakenedNotificationsToggle.checked ? 'flex' : 'none';
        }
        
        weakenedNotificationsToggle.addEventListener('change', function() {
            settings.notifications.weakenedSpellNotifications = this.checked;
            // Show/hide interval row
            if (notificationIntervalRow) {
                notificationIntervalRow.style.display = this.checked ? 'flex' : 'none';
            }
            console.log('[SpellLearning] Weakened spell notifications:', this.checked);
            scheduleAutoSave();
        });
    }
    
    // Notification interval slider
    var notificationIntervalSlider = document.getElementById('notificationIntervalSlider');
    var notificationIntervalValue = document.getElementById('notificationIntervalValue');
    if (notificationIntervalSlider) {
        notificationIntervalSlider.value = settings.notifications.weakenedSpellInterval || 10;
        if (notificationIntervalValue) {
            notificationIntervalValue.textContent = notificationIntervalSlider.value + 's';
        }
        updateSliderFillGlobal(notificationIntervalSlider);
        
        notificationIntervalSlider.addEventListener('input', function() {
            var value = parseInt(this.value);
            settings.notifications.weakenedSpellInterval = value;
            if (notificationIntervalValue) {
                notificationIntervalValue.textContent = value + 's';
            }
            updateSliderFillGlobal(this);
            console.log('[SpellLearning] Notification interval:', value, 'seconds');
            scheduleAutoSave();
        });
    }
    
    // UI Theme selector
    initializeThemeSelector();

    // Design preset selector (whole-panel look; add-ons add presets/design/*.json)
    DesignPresets.initSelector();

    // Known higher spells: open the spells below, how far, at what XP
    if (typeof ReverseUnlockSetting !== 'undefined') ReverseUnlockSetting.init();
    if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.init();
    
    // Learning color picker
    var learningColorPicker = document.getElementById('learningColorPicker');
    var learningColorValue = document.getElementById('learningColorValue');
    if (learningColorPicker) {
        learningColorPicker.value = settings.learningColor || '#7890A8';
        if (learningColorValue) learningColorValue.textContent = learningColorPicker.value.toUpperCase();
        applyLearningColor(settings.learningColor || '#7890A8');
        
        // While the colour is being picked only its label follows; the colour is
        // applied (four page-wide CSS variables, a tree render) once it is chosen
        learningColorPicker.addEventListener('input', function() {
            if (learningColorValue) learningColorValue.textContent = this.value.toUpperCase();
        });
        learningColorPicker.addEventListener('change', function() {
            settings.learningColor = this.value;
            if (learningColorValue) learningColorValue.textContent = this.value.toUpperCase();
            applyLearningColor(this.value);
            console.log('[SpellLearning] Learning color:', settings.learningColor);
            // Re-render tree with new color
            if (state.treeData) {
                WheelRenderer.render();
            }
            scheduleAutoSave();
        });
    }
    
    // Font size multiplier slider
    var fontSizeSlider = document.getElementById('fontSizeSlider');
    var fontSizeValue = document.getElementById('fontSizeValue');
    if (fontSizeSlider) {
        fontSizeSlider.value = settings.fontSizeMultiplier || 1.0;
        if (fontSizeValue) fontSizeValue.textContent = (settings.fontSizeMultiplier || 1.0).toFixed(1) + 'x';
        updateSliderFillGlobal(fontSizeSlider);
        applyFontSizeMultiplier(settings.fontSizeMultiplier || 1.0);
        
        // Dragging moves the label; the size (a layout of the whole page) is
        // applied when the slider is let go
        fontSizeSlider.addEventListener('input', function() {
            if (fontSizeValue) fontSizeValue.textContent = parseFloat(this.value).toFixed(1) + 'x';
            updateSliderFillGlobal(this);
        });
        fontSizeSlider.addEventListener('change', function() {
            var value = parseFloat(this.value);
            settings.fontSizeMultiplier = value;
            if (fontSizeValue) fontSizeValue.textContent = value.toFixed(1) + 'x';
            updateSliderFillGlobal(this);
            applyFontSizeMultiplier(value);
            console.log('[SpellLearning] Font size multiplier:', settings.fontSizeMultiplier);
            scheduleAutoSave();
        });
    }
    
    // Side details panel toggle (off = bottom info bar)
    var sideDetailsToggle = document.getElementById('sideDetailsToggle');
    if (sideDetailsToggle) {
        sideDetailsToggle.checked = settings.detailsLayout === 'side';
        sideDetailsToggle.addEventListener('change', function() {
            settings.detailsLayout = this.checked ? 'side' : 'bottom';
            if (typeof TreeNav !== 'undefined') TreeNav.applyDetailsLayout();
            console.log('[SpellLearning] Details layout:', settings.detailsLayout);
            scheduleAutoSave();
        });
    }

    // Hover preview toggle (off = the card only opens on a click)
    var hoverDetailsToggle = document.getElementById('hoverDetailsToggle');
    if (hoverDetailsToggle) {
        hoverDetailsToggle.checked = settings.detailsOnHover !== false;
        hoverDetailsToggle.addEventListener('change', function() {
            settings.detailsOnHover = this.checked;
            if (typeof DetailsPeek !== 'undefined') DetailsPeek.applyLayout();
            console.log('[SpellLearning] Details on hover:', settings.detailsOnHover);
            scheduleAutoSave();
        });
    }

    // Tree Generation Settings
    var aggressivePathValidationToggle = document.getElementById('aggressivePathValidationToggle');
    if (aggressivePathValidationToggle) {
        aggressivePathValidationToggle.checked = settings.aggressivePathValidation;
        aggressivePathValidationToggle.addEventListener('change', function() {
            settings.aggressivePathValidation = this.checked;
            console.log('[SpellLearning] Aggressive path validation:', settings.aggressivePathValidation);
            scheduleAutoSave();
        });
    }
    
    // Early Spell Learning Settings
    try { initializeEarlyLearningSettings(); } catch(e) { console.error('[SpellLearning] Early learning settings init error:', e); }

    // Passive Learning Settings
    try { initializePassiveLearningSettings(); } catch(e) { console.error('[SpellLearning] Passive learning settings init error:', e); }

    // Settings Presets
    try { if (typeof initializeSettingsPresets === 'function') initializeSettingsPresets(); } catch(e) { console.error('[SpellLearning] Settings presets init error:', e); }

    // Scanner Presets
    try { if (typeof initializeScannerPresets === 'function') initializeScannerPresets(); } catch(e) { console.error('[SpellLearning] Scanner presets init error:', e); }

    // Hotkey configuration
    var hotkeyInput = document.getElementById('hotkeyInput');
    var changeHotkeyBtn = document.getElementById('changeHotkeyBtn');
    var resetHotkeyBtn = document.getElementById('resetHotkeyBtn');
    
    if (hotkeyInput && changeHotkeyBtn) {
        hotkeyInput.value = settings.hotkey;
        
        changeHotkeyBtn.addEventListener('click', function() {
            hotkeyInput.classList.add('listening');
            hotkeyInput.value = t('settingsPanel.pressAKey');
            
            function onKeyDown(e) {
                e.preventDefault();
                var keyName = e.key.toUpperCase();
                
                // Check if it's a valid key we support
                if (KEY_CODES[keyName] || KEY_CODES[e.key]) {
                    settings.hotkey = keyName;
                    settings.hotkeyCode = KEY_CODES[keyName] || KEY_CODES[e.key];
                    hotkeyInput.value = keyName;
                    console.log('[SpellLearning] Hotkey changed to:', keyName, '(code:', settings.hotkeyCode, ')');
                } else {
                    hotkeyInput.value = settings.hotkey;
                    console.log('[SpellLearning] Unsupported key:', e.key);
                }
                
                hotkeyInput.classList.remove('listening');
                document.removeEventListener('keydown', onKeyDown);
            }
            
            document.addEventListener('keydown', onKeyDown);
        });
        
        resetHotkeyBtn.addEventListener('click', function() {
            settings.hotkey = 'F8';
            settings.hotkeyCode = 66;
            hotkeyInput.value = 'F8';
            hotkeyInput.classList.remove('listening');
        });
    }
    
    // Pause Game on Focus toggle
    var pauseGameToggle = document.getElementById('pauseGameOnFocusToggle');
    if (pauseGameToggle) {
        // Default to true (checked) if not set
        pauseGameToggle.checked = settings.pauseGameOnFocus !== false;
        
        pauseGameToggle.addEventListener('change', function() {
            settings.pauseGameOnFocus = this.checked;
            console.log('[SpellLearning] Pause game on focus:', settings.pauseGameOnFocus);
            
            // Notify C++ immediately
            if (window.callCpp) {
                window.callCpp('SetPauseGameOnFocus', settings.pauseGameOnFocus ? 'true' : 'false');
            }
        });
    }
    
    // Heart Animation Settings Popup
    initializeHeartSettings();
    
    // Progression settings - Learning Mode (segmented toggle)
    initSegmentedToggle('learningModeToggle', settings.learningMode, function(value) {
        settings.learningMode = value;
        console.log('[SpellLearning] Learning mode:', value);
        autoSaveSettings();
    });

    // Progression settings - Auto-Advance Learning Target
    var autoAdvanceToggle = document.getElementById('autoAdvanceLearningToggle');
    if (autoAdvanceToggle) {
        autoAdvanceToggle.checked = settings.autoAdvanceLearning;
        autoAdvanceToggle.addEventListener('change', function() {
            settings.autoAdvanceLearning = this.checked;
            setSegmentedToggleEnabled('autoAdvanceModeToggle', this.checked);
            autoSaveSettings();
        });
    }
    initSegmentedToggle('autoAdvanceModeToggle', settings.autoAdvanceMode || 'branch', function(value) {
        settings.autoAdvanceMode = value;
        autoSaveSettings();
    });
    setSegmentedToggleEnabled('autoAdvanceModeToggle', settings.autoAdvanceLearning);

    // Progression settings - XP Multiplier Sliders
    function updateSliderFill(slider) {
        var percent = (slider.value - slider.min) / (slider.max - slider.min) * 100;
        slider.style.setProperty('--slider-fill', percent + '%');
    }
    
    function setupSlider(sliderId, valueId, settingKey) {
        var slider = document.getElementById(sliderId);
        var valueDisplay = document.getElementById(valueId);
        
        if (slider && valueDisplay) {
            slider.value = settings[settingKey];
            valueDisplay.textContent = settings[settingKey] + '%';
            updateSliderFill(slider);
            
            slider.addEventListener('input', function() {
                settings[settingKey] = parseInt(this.value);
                valueDisplay.textContent = this.value + '%';
                updateSliderFill(this);
            });

            // Let go: the tree and the card follow the new threshold (once, not
            // per step of the drag), and it is saved
            slider.addEventListener('change', function() {
                // Re-render tree labels when reveal thresholds change
                if (settingKey === 'revealName' || settingKey === 'revealEffects' || settingKey === 'revealDescription') {
                    if (typeof CanvasRenderer !== 'undefined') { CanvasRenderer._needsRender = true; }
                    if (typeof SmartRenderer !== 'undefined' && SmartRenderer.refresh) { SmartRenderer.refresh(); }
                    if (typeof WheelRenderer !== 'undefined' && WheelRenderer.updateNodeStates) { WheelRenderer.updateNodeStates(); }
                    // Refresh detail panel if a node is selected
                    if (state.selectedNode && typeof showSpellDetails === 'function') {
                        showSpellDetails(state.selectedNode);
                    }
                }
                console.log('[SpellLearning] ' + settingKey + ':', settings[settingKey]);
                autoSaveSettings();
            });
        }
    }
    
    // Global XP multiplier slider (shows "x1" format instead of "%")
    var globalMultSlider = document.getElementById('xpGlobalMultiplierSlider');
    var globalMultValue = document.getElementById('xpGlobalMultiplierValue');
    if (globalMultSlider && globalMultValue) {
        globalMultSlider.value = settings.xpGlobalMultiplier;
        globalMultValue.textContent = 'x' + settings.xpGlobalMultiplier;
        updateSliderFill(globalMultSlider);
        
        globalMultSlider.addEventListener('input', function() {
            settings.xpGlobalMultiplier = parseInt(this.value);
            globalMultValue.textContent = 'x' + this.value;
            updateSliderFill(this);
        });
        
        globalMultSlider.addEventListener('change', function() {
            console.log('[SpellLearning] Global XP multiplier:', settings.xpGlobalMultiplier);
            autoSaveSettings();
        });
    }
    
    setupSlider('xpDirectSlider', 'xpDirectValue', 'xpMultiplierDirect');
    setupSlider('xpSchoolSlider', 'xpSchoolValue', 'xpMultiplierSchool');
    setupSlider('xpAnySlider', 'xpAnyValue', 'xpMultiplierAny');
    
    // XP Cap sliders
    setupSlider('xpCapAnySlider', 'xpCapAnyValue', 'xpCapAny');
    setupSlider('xpCapSchoolSlider', 'xpCapSchoolValue', 'xpCapSchool');
    setupSlider('xpCapDirectSlider', 'xpCapDirectValue', 'xpCapDirect');
    
    // Tier XP requirement inputs
    function setupXPInput(inputId, settingKey) {
        var input = document.getElementById(inputId);
        
        if (input) {
            input.value = settings[settingKey];
            
            input.addEventListener('change', function() {
                var val = parseInt(this.value) || 1;
                val = Math.max(1, Math.min(99999, val));  // Clamp to valid range
                this.value = val;
                settings[settingKey] = val;
                console.log('[SpellLearning] ' + settingKey + ':', settings[settingKey]);
                if (typeof RequiredXPSync !== 'undefined') RequiredXPSync.sync();
                autoSaveSettings();
            });
            
            // Also save on blur
            input.addEventListener('blur', function() {
                var val = parseInt(this.value) || 1;
                val = Math.max(1, Math.min(99999, val));
                this.value = val;
                settings[settingKey] = val;
                });
        }
    }
    
    setupXPInput('xpNoviceInput', 'xpNovice');
    setupXPInput('xpApprenticeInput', 'xpApprentice');
    setupXPInput('xpAdeptInput', 'xpAdept');
    setupXPInput('xpExpertInput', 'xpExpert');
    setupXPInput('xpMasterInput', 'xpMaster');
    
    // Progressive reveal threshold sliders
    setupSlider('revealNameSlider', 'revealNameValue', 'revealName');
    setupSlider('revealEffectsSlider', 'revealEffectsValue', 'revealEffects');
    setupSlider('revealDescSlider', 'revealDescValue', 'revealDescription');
    
    // Save settings button
    var saveSettingsBtn = document.getElementById('saveSettingsBtn');
    if (saveSettingsBtn) {
        saveSettingsBtn.addEventListener('click', function() {
            saveSettings();
            console.log('[SpellLearning] Settings saved');
        });
    }
    
    // Reset settings button
    var resetSettingsBtn = document.getElementById('resetSettingsBtn');
    if (resetSettingsBtn) {
        resetSettingsBtn.addEventListener('click', function() {
            resetSettings();
        });
    }
    
    // Apply saved school colors to CSS
    applySchoolColorsToCSS();
}

function loadSettings() {
    // Load unified config from C++ (all settings in one file)
    if (window.callCpp) {
        window.callCpp('LoadUnifiedConfig', '');
    }
}

function saveSettings() {
    // Save unified config to C++ (all settings in one file)
    saveUnifiedConfig();
}

// Auto-save settings (debounced to avoid excessive saves)
var autoSaveTimer = null;
/**
 * Alias used by ~30 UI handlers (theme, font size, layout, ...). It was never
 * defined, so those handlers threw after applying their change and the value
 * only got persisted by some later save. Route it to the debounced autosave.
 */
function scheduleAutoSave() {
    autoSaveSettings();
}

function autoSaveSettings() {
    // Clear any pending save
    if (autoSaveTimer) {
        clearTimeout(autoSaveTimer);
    }
    // Save after a brief delay
    autoSaveTimer = setTimeout(function() {
        saveUnifiedConfig();
        console.log('[SpellLearning] Settings auto-saved');
        autoSaveTimer = null;
    }, 500);
}

var _lastSavedConfigText = null;

function saveUnifiedConfig() {
    if (!window.callCpp) return;
    
    var unifiedConfig = {
        // Panel settings
        hotkey: settings.hotkey,
        hotkeyCode: settings.hotkeyCode,
        developerMode: settings.developerMode,
        cheatMode: settings.cheatMode,
        nodeSizeScaling: settings.nodeSizeScaling,
        showNodeNames: settings.showNodeNames,
        showSchoolDividers: settings.showSchoolDividers,
        dividerFade: settings.dividerFade,
        dividerSpacing: settings.dividerSpacing,
        dividerLength: settings.dividerLength,
        dividerColorMode: settings.dividerColorMode,
        dividerCustomColor: settings.dividerCustomColor,
        verboseLogging: settings.verboseLogging,
        // UI Display settings
        uiTheme: settings.uiTheme,
        designPreset: settings.designPreset,
        learningColor: settings.learningColor,
        fontSizeMultiplier: settings.fontSizeMultiplier,
        aggressivePathValidation: settings.aggressivePathValidation,
        
        // Progression settings
        learningMode: settings.learningMode,
        autoAdvanceLearning: settings.autoAdvanceLearning,
        autoAdvanceMode: settings.autoAdvanceMode,
        xpGlobalMultiplier: settings.xpGlobalMultiplier,
        xpMultiplierDirect: settings.xpMultiplierDirect,
        xpMultiplierSchool: settings.xpMultiplierSchool,
        xpMultiplierAny: settings.xpMultiplierAny,
        // XP caps (max contribution from each source)
        xpCapAny: settings.xpCapAny,
        xpCapSchool: settings.xpCapSchool,
        xpCapDirect: settings.xpCapDirect,
        // Modded XP sources
        moddedXPSources: settings.moddedXPSources,
        // Tier XP requirements
        xpNovice: settings.xpNovice,
        xpApprentice: settings.xpApprentice,
        xpAdept: settings.xpAdept,
        xpExpert: settings.xpExpert,
        xpMaster: settings.xpMaster,
        // Progressive reveal thresholds
        revealName: settings.revealName,
        revealEffects: settings.revealEffects,
        revealDescription: settings.revealDescription,
        
        // Field output settings for spell scan
        fields: state.fields,
        
        // Scan mode
        scanModeTomes: document.getElementById('scanModeTomes') ? 
            document.getElementById('scanModeTomes').checked : true,
        
        // Per-node XP overrides
        xpOverrides: xpOverrides,
        
        // Window position and size
        windowX: settings.windowX,
        windowY: settings.windowY,
        windowWidth: settings.windowWidth,
        windowHeight: settings.windowHeight,
        isFullscreen: state.isFullscreen,
        
        // School colors
        schoolColors: settings.schoolColors,
        schoolVisibility: settings.schoolVisibility,
        
        // Active preset names (preset data now in individual files)
        activeSettingsPreset: typeof _activeSettingsPreset !== 'undefined' ? _activeSettingsPreset : 'Default',
        
        // Discovery mode
        discoveryMode: settings.discoveryMode,
        showRootSpellNames: settings.showRootSpellNames,
        
        // Early spell learning
        earlySpellLearning: settings.earlySpellLearning,

        // Passive learning
        passiveLearning: settings.passiveLearning,

        // Spell tome learning
        spellTomeLearning: settings.spellTomeLearning,
        
        // Heart animation settings
        heartAnimationEnabled: settings.heartAnimationEnabled,
        heartPulseSpeed: settings.heartPulseSpeed,
        heartPulseDelay: settings.heartPulseDelay,
        heartBgOpacity: settings.heartBgOpacity,
        heartBgColor: settings.heartBgColor,
        heartRingColor: settings.heartRingColor,
        
        // Camera settings
        focusOnClick: settings.focusOnClick,
        focusZoomOnClick: settings.focusZoomOnClick,
        showSelectionPath: settings.showSelectionPath,
        showBaseConnections: settings.showBaseConnections,
        focusZoom: settings.focusZoom,
        focusRotate: settings.focusRotate,
        language: settings.language || '',
        detailsLayout: settings.detailsLayout,
        detailsOnHover: settings.detailsOnHover,
        focusDimOthers: settings.focusDimOthers,
        uiPatchDefaults: settings.uiPatchDefaults || 1,   // Marker: patch defaults (tomes-only scan) already applied once

        // Starfield settings
        starfieldEnabled: settings.starfieldEnabled,
        starfieldFixed: settings.starfieldFixed,
        starfieldSeed: settings.starfieldSeed,
        starfieldColor: settings.starfieldColor,
        starfieldBgColor: settings.starfieldBgColor,
        starfieldDensity: settings.starfieldDensity,
        starfieldMaxSize: settings.starfieldMaxSize,
        // Globe settings
        globeSize: settings.globeSize,
        globeDensity: settings.globeDensity,
        globeDotMin: settings.globeDotMin,
        globeDotMax: settings.globeDotMax,
        globeColor: settings.globeColor,
        magicTextColor: settings.magicTextColor,
        globeText: settings.globeText,
        globeTextSize: settings.globeTextSize,
        particleTrailEnabled: settings.particleTrailEnabled,
        globeBgFill: settings.globeBgFill,
        globeParticleRadius: settings.globeParticleRadius,
        nodeFontSize: settings.nodeFontSize,

        // Spell blacklist & plugin whitelist
        spellBlacklist: settings.spellBlacklist || [],
        pluginWhitelist: settings.pluginWhitelist || [],

        // User-selected root spells per school
        selectedRoots: settings.selectedRoots || {},

        // Active scanner preset name (preset data now in individual files)
        activeScannerPreset: typeof _activeScannerPreset !== 'undefined' ? _activeScannerPreset : ''
    };
    // Known higher spells (reverse unlock): what they open, XP share, own gain rates
    if (typeof ReverseUnlockSetting !== 'undefined') ReverseUnlockSetting.saveTo(unifiedConfig);
    if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.saveTo(unifiedConfig);
    if (typeof RenderSettings !== 'undefined') RenderSettings.saveTo(unifiedConfig);

    // Closing the panel saves, and so do many handlers that change nothing.
    // Each save made C++ read, merge and rewrite config.json and re-apply
    // every setting. The same text twice in a row is skipped.
    var configText = JSON.stringify(unifiedConfig);
    if (configText === _lastSavedConfigText) return;
    _lastSavedConfigText = configText;

    console.log('[SpellLearning] Saving unified config');
    window.callCpp('SaveUnifiedConfig', configText);
}

function resetSettings() {
    settings.hotkey = 'F8';
    settings.hotkeyCode = 66;
    settings.developerMode = false;
    settings.cheatMode = false;
    settings.nodeSizeScaling = true;
    settings.showNodeNames = true;
    settings.showSchoolDividers = true;
    settings.verboseLogging = false;
    // UI Display defaults
    settings.uiTheme = 'skyrim';
    settings.designPreset = DesignPresets.DEFAULT_ID;
    DesignPresets.apply(settings.designPreset);
    settings.learningColor = '#7890A8';
    settings.fontSizeMultiplier = 1.0;
    settings.learningMode = 'perSchool';
    settings.autoAdvanceLearning = true;
    settings.autoAdvanceMode = 'branch';
    if (typeof ReverseUnlockSetting !== 'undefined') ReverseUnlockSetting.reset();
    if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.reset();
    if (typeof RenderSettings !== 'undefined') RenderSettings.reset();
    settings.xpGlobalMultiplier = 1;
    settings.xpMultiplierDirect = 100;
    settings.xpMultiplierSchool = 50;
    settings.xpMultiplierAny = 10;
    settings.xpNovice = 100;
    settings.xpApprentice = 200;
    settings.xpAdept = 400;
    settings.xpExpert = 800;
    settings.xpMaster = 1500;
    settings.revealName = 0;
    settings.revealEffects = 25;
    settings.revealDescription = 50;
    
    // Clear XP overrides
    xpOverrides = {};
    
    // Update UI
    var cheatToggle = document.getElementById('cheatModeToggle');
    var verboseToggle = document.getElementById('verboseLogToggle');
    var hotkeyInput = document.getElementById('hotkeyInput');
    var cheatInfo = document.getElementById('cheatModeInfo');
    
    var devModeToggle = document.getElementById('developerModeToggle');
    if (devModeToggle) devModeToggle.checked = false;
    if (cheatToggle) cheatToggle.checked = false;
    if (verboseToggle) verboseToggle.checked = false;
    updateDeveloperModeVisibility(false);
    if (hotkeyInput) hotkeyInput.value = 'F8';
    if (cheatInfo) cheatInfo.classList.add('hidden');
    
    // Update progression settings UI
    var xpDirectSlider = document.getElementById('xpDirectSlider');
    var xpSchoolSlider = document.getElementById('xpSchoolSlider');
    var xpAnySlider = document.getElementById('xpAnySlider');
    var globalMultSlider = document.getElementById('xpGlobalMultiplierSlider');

    // Helper to update slider fill visual
    function updateSliderFillReset(slider) {
        if (!slider) return;
        var percent = (slider.value - slider.min) / (slider.max - slider.min) * 100;
        slider.style.setProperty('--slider-fill', percent + '%');
    }

    setSegmentedToggleValue('learningModeToggle', 'perSchool');

    // Auto-advance reset
    var autoAdvanceToggle = document.getElementById('autoAdvanceLearningToggle');
    if (autoAdvanceToggle) autoAdvanceToggle.checked = true;
    setSegmentedToggleValue('autoAdvanceModeToggle', 'branch');
    setSegmentedToggleEnabled('autoAdvanceModeToggle', true);

    // Global multiplier
    if (globalMultSlider) {
        globalMultSlider.value = 1;
        updateSliderFillReset(globalMultSlider);
        var globalMultValue = document.getElementById('xpGlobalMultiplierValue');
        if (globalMultValue) globalMultValue.textContent = 'x1';
    }
    
    if (xpDirectSlider) {
        xpDirectSlider.value = 100;
        updateSliderFillReset(xpDirectSlider);
        var xpDirectValue = document.getElementById('xpDirectValue');
        if (xpDirectValue) xpDirectValue.textContent = '100%';
    }
    if (xpSchoolSlider) {
        xpSchoolSlider.value = 50;
        updateSliderFillReset(xpSchoolSlider);
        var xpSchoolValue = document.getElementById('xpSchoolValue');
        if (xpSchoolValue) xpSchoolValue.textContent = '50%';
    }
    if (xpAnySlider) {
        xpAnySlider.value = 10;
        updateSliderFillReset(xpAnySlider);
        var xpAnyValue = document.getElementById('xpAnyValue');
        if (xpAnyValue) xpAnyValue.textContent = '10%';
    }
    
    // Reset tier XP inputs
    var tierInputDefaults = {
        'xpNoviceInput': 100,
        'xpApprenticeInput': 200,
        'xpAdeptInput': 400,
        'xpExpertInput': 800,
        'xpMasterInput': 1500
    };
    for (var inputId in tierInputDefaults) {
        var input = document.getElementById(inputId);
        if (input) input.value = tierInputDefaults[inputId];
    }
    
    // Reset reveal sliders
    var revealSliderDefaults = [
        { id: 'revealNameSlider', valueId: 'revealNameValue', val: 0 },
        { id: 'revealEffectsSlider', valueId: 'revealEffectsValue', val: 25 },
        { id: 'revealDescSlider', valueId: 'revealDescValue', val: 50 }
    ];
    revealSliderDefaults.forEach(function(cfg) {
        var slider = document.getElementById(cfg.id);
        var valueEl = document.getElementById(cfg.valueId);
        if (slider) {
            slider.value = cfg.val;
            updateSliderFillReset(slider);
            if (valueEl) valueEl.textContent = cfg.val + '%';
        }
    });
    
    // Re-render tree
    if (state.treeData) {
        WheelRenderer.render();
    }

    // Persist reset to C++
    saveSettings();

    console.log('[SpellLearning] Settings reset to defaults and saved');
}

// C++ callback for loading unified config
window.onUnifiedConfigLoaded = function(dataStr) {
    console.log('[SpellLearning] Unified config received');
    try {
        var data = typeof dataStr === 'string' ? JSON.parse(dataStr) : dataStr;
        if (!data) return;
        
        // === Panel Settings ===
        settings.hotkey = data.hotkey || 'F8';
        settings.hotkeyCode = data.hotkeyCode || 66;
        settings.developerMode = data.developerMode || false;
        settings.cheatMode = data.cheatMode || false;
        settings.nodeSizeScaling = data.nodeSizeScaling !== false;  // default true
        settings.showNodeNames = data.showNodeNames !== false;  // default true
        settings.showSchoolDividers = data.showSchoolDividers !== false;  // default true
        settings.dividerFade = data.dividerFade !== undefined ? data.dividerFade : 50;
        settings.dividerSpacing = data.dividerSpacing !== undefined ? data.dividerSpacing : 3;
        settings.dividerLength = data.dividerLength !== undefined ? data.dividerLength : 800;
        settings.dividerColorMode = data.dividerColorMode || 'school';
        settings.dividerCustomColor = data.dividerCustomColor || '#ffffff';
        settings.verboseLogging = data.verboseLogging || false;
        // UI Display settings
        // Known higher spells (reverse unlock): what they open, XP share, own gain rates
        if (typeof ReverseUnlockSetting !== 'undefined') ReverseUnlockSetting.loadFrom(data);
        if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.loadFrom(data);
        if (typeof RenderSettings !== 'undefined') RenderSettings.loadFrom(data);
        // One design sets the UI theme too (DesignPresets); older configs are carried over
        settings.designPreset = DesignPresets.savedChoice(data);
        DesignPresets.apply(settings.designPreset);
        settings.learningColor = data.learningColor || '#7890A8';
        settings.fontSizeMultiplier = data.fontSizeMultiplier !== undefined ? data.fontSizeMultiplier : 1.0;
        settings.aggressivePathValidation = data.aggressivePathValidation !== false;  // default true
        
        // === Progression Settings ===
        settings.learningMode = data.learningMode || 'perSchool';
        settings.autoAdvanceLearning = data.autoAdvanceLearning !== false;  // default true
        settings.autoAdvanceMode = data.autoAdvanceMode || 'branch';
        settings.xpGlobalMultiplier = data.xpGlobalMultiplier !== undefined ? data.xpGlobalMultiplier : 1;
        settings.xpMultiplierDirect = data.xpMultiplierDirect !== undefined ? data.xpMultiplierDirect : 100;
        settings.xpMultiplierSchool = data.xpMultiplierSchool !== undefined ? data.xpMultiplierSchool : 50;
        settings.xpMultiplierAny = data.xpMultiplierAny !== undefined ? data.xpMultiplierAny : 10;
        // Tier XP requirements
        settings.xpNovice = data.xpNovice !== undefined ? data.xpNovice : 100;
        settings.xpApprentice = data.xpApprentice !== undefined ? data.xpApprentice : 200;
        settings.xpAdept = data.xpAdept !== undefined ? data.xpAdept : 400;
        settings.xpExpert = data.xpExpert !== undefined ? data.xpExpert : 800;
        settings.xpMaster = data.xpMaster !== undefined ? data.xpMaster : 1500;
        // Progressive reveal thresholds
        settings.revealName = data.revealName !== undefined ? data.revealName : 0;
        settings.revealEffects = data.revealEffects !== undefined ? data.revealEffects : 25;
        settings.revealDescription = data.revealDescription !== undefined ? data.revealDescription : 50;
        
        // Per-node XP overrides
        if (data.xpOverrides && typeof data.xpOverrides === 'object') {
            xpOverrides = data.xpOverrides;
            console.log('[SpellLearning] Loaded XP overrides for', Object.keys(xpOverrides).length, 'spells');
        } else {
            xpOverrides = {};
        }

        // Modded XP sources
        if (data.moddedXPSources && typeof data.moddedXPSources === 'object') {
            settings.moddedXPSources = data.moddedXPSources;
            rebuildModdedXPSourcesUI();
            console.log('[SpellLearning] Loaded modded XP sources:', Object.keys(settings.moddedXPSources).length);
        }

        // Window position and size
        settings.windowX = data.windowX !== undefined ? data.windowX : null;
        settings.windowY = data.windowY !== undefined ? data.windowY : null;
        settings.windowWidth = data.windowWidth !== undefined ? data.windowWidth : null;
        settings.windowHeight = data.windowHeight !== undefined ? data.windowHeight : null;
        
        // Fullscreen state
        state.isFullscreen = data.isFullscreen || false;
        settings.isFullscreen = state.isFullscreen;
        
        // Apply window position and size if saved
        applyWindowPositionAndSize();
        
        // Apply fullscreen state
        applyFullscreenState();
        
        // School colors
        if (data.schoolColors && typeof data.schoolColors === 'object') {
            // Merge with defaults (keep any new schools that might have been added)
            for (var school in data.schoolColors) {
                settings.schoolColors[school] = data.schoolColors[school];
            }
            console.log('[SpellLearning] Loaded colors for', Object.keys(settings.schoolColors).length, 'schools');
        }
        
        // School visibility
        if (data.schoolVisibility && typeof data.schoolVisibility === 'object') {
            for (var school in data.schoolVisibility) {
                settings.schoolVisibility[school] = data.schoolVisibility[school];
            }
            console.log('[SpellLearning] Loaded visibility for', Object.keys(settings.schoolVisibility).length, 'schools');
        }
        
        // Active preset names (preset data now loaded from individual files)
        if (data.activeSettingsPreset && typeof _activeSettingsPreset !== 'undefined') {
            _activeSettingsPreset = data.activeSettingsPreset;
        }
        if (data.activeScannerPreset && typeof _activeScannerPreset !== 'undefined') {
            _activeScannerPreset = data.activeScannerPreset;
        }

        // LEGACY MIGRATION: Removed. Preset files are now bundled with the mod.
        // Old embedded presets in config.json are simply ignored.
        // If data.settingsPresets or data.scannerPresets exist, we no longer migrate them
        // to avoid overwriting user's customized preset files on every load.
        
        // Discovery mode
        settings.discoveryMode = data.discoveryMode !== undefined ? data.discoveryMode : true;
        var discoveryModeToggle = document.getElementById('discoveryModeToggle');
        if (discoveryModeToggle) discoveryModeToggle.checked = settings.discoveryMode;
        
        // Show root spell names in discovery mode
        settings.showRootSpellNames = data.showRootSpellNames !== undefined ? data.showRootSpellNames : true;
        var showRootNamesToggle = document.getElementById('showRootSpellNamesToggle');
        if (showRootNamesToggle) showRootNamesToggle.checked = settings.showRootSpellNames;
        
        // Tree generation settings
        var aggressivePathValidationToggle = document.getElementById('aggressivePathValidationToggle');
        if (aggressivePathValidationToggle) aggressivePathValidationToggle.checked = settings.aggressivePathValidation;
        
        // Apply school colors to CSS
        applySchoolColorsToCSS();
        
        // Update UI toggles
        var cheatToggle = document.getElementById('cheatModeToggle');
        var verboseToggle = document.getElementById('verboseLogToggle');
        var hotkeyInput = document.getElementById('hotkeyInput');
        var cheatInfo = document.getElementById('cheatModeInfo');
        
        var devModeToggle = document.getElementById('developerModeToggle');
        if (devModeToggle) devModeToggle.checked = settings.developerMode;
        updateDeveloperModeVisibility(settings.developerMode);
        
        if (cheatToggle) cheatToggle.checked = settings.cheatMode;

        // Update learning color UI
        var learningColorPicker = document.getElementById('learningColorPicker');
        var learningColorValue = document.getElementById('learningColorValue');
        if (learningColorPicker) {
            learningColorPicker.value = settings.learningColor;
            if (learningColorValue) learningColorValue.textContent = settings.learningColor.toUpperCase();
            applyLearningColor(settings.learningColor);
        }
        
        // Update font size UI
        var fontSizeSlider = document.getElementById('fontSizeSlider');
        var fontSizeValue = document.getElementById('fontSizeValue');
        if (fontSizeSlider) {
            fontSizeSlider.value = settings.fontSizeMultiplier;
            if (fontSizeValue) fontSizeValue.textContent = settings.fontSizeMultiplier.toFixed(1) + 'x';
            updateSliderFillGlobal(fontSizeSlider);
            applyFontSizeMultiplier(settings.fontSizeMultiplier);
        }

        // Update details layout UI
        var sideDetailsToggleEl = document.getElementById('sideDetailsToggle');
        if (sideDetailsToggleEl) sideDetailsToggleEl.checked = settings.detailsLayout === 'side';
        var hoverDetailsToggleEl = document.getElementById('hoverDetailsToggle');
        if (hoverDetailsToggleEl) hoverDetailsToggleEl.checked = settings.detailsOnHover !== false;

        // Early spell learning settings
        if (data.earlySpellLearning && typeof data.earlySpellLearning === 'object') {
            var el = data.earlySpellLearning;
            settings.earlySpellLearning.enabled = el.enabled !== undefined ? el.enabled : true;
            settings.earlySpellLearning.unlockThreshold = el.unlockThreshold !== undefined ? el.unlockThreshold : 25;
            settings.earlySpellLearning.selfCastRequiredAt = el.selfCastRequiredAt !== undefined ? el.selfCastRequiredAt : 75;
            settings.earlySpellLearning.selfCastXPMultiplier = el.selfCastXPMultiplier !== undefined ? el.selfCastXPMultiplier : 150;
            settings.earlySpellLearning.binaryEffectThreshold = el.binaryEffectThreshold !== undefined ? el.binaryEffectThreshold : 80;
            settings.earlySpellLearning.modifyGameDisplay = el.modifyGameDisplay !== undefined ? el.modifyGameDisplay : true;
            // Load power steps if present
            if (el.powerSteps && Array.isArray(el.powerSteps)) {
                settings.earlySpellLearning.powerSteps = el.powerSteps;
            }
        }
        updateEarlyLearningUI();
        // Update power steps UI if function exists
        if (typeof renderPowerSteps === 'function') renderPowerSteps();

        // Passive learning settings
        if (data.passiveLearning && typeof data.passiveLearning === 'object') {
            var pl = data.passiveLearning;
            settings.passiveLearning.enabled = pl.enabled !== undefined ? pl.enabled : false;
            settings.passiveLearning.scope = pl.scope || 'novice';
            settings.passiveLearning.xpPerGameHour = pl.xpPerGameHour !== undefined ? pl.xpPerGameHour : 5;
            if (pl.maxByTier && typeof pl.maxByTier === 'object') {
                settings.passiveLearning.maxByTier.novice = pl.maxByTier.novice !== undefined ? pl.maxByTier.novice : 100;
                settings.passiveLearning.maxByTier.apprentice = pl.maxByTier.apprentice !== undefined ? pl.maxByTier.apprentice : 75;
                settings.passiveLearning.maxByTier.adept = pl.maxByTier.adept !== undefined ? pl.maxByTier.adept : 50;
                settings.passiveLearning.maxByTier.expert = pl.maxByTier.expert !== undefined ? pl.maxByTier.expert : 25;
                settings.passiveLearning.maxByTier.master = pl.maxByTier.master !== undefined ? pl.maxByTier.master : 5;
            }
        }
        if (typeof updatePassiveLearningUI === 'function') updatePassiveLearningUI();

        // Spell tome learning settings
        if (data.spellTomeLearning && typeof data.spellTomeLearning === 'object') {
            var stl = data.spellTomeLearning;
            settings.spellTomeLearning.enabled = stl.enabled !== undefined ? stl.enabled : true;
            settings.spellTomeLearning.useProgressionSystem = stl.useProgressionSystem !== undefined ? stl.useProgressionSystem : true;
            settings.spellTomeLearning.grantXPOnRead = stl.grantXPOnRead !== undefined ? stl.grantXPOnRead : true;
            settings.spellTomeLearning.autoSetLearningTarget = stl.autoSetLearningTarget !== undefined ? stl.autoSetLearningTarget : true;
            settings.spellTomeLearning.showNotifications = stl.showNotifications !== undefined ? stl.showNotifications : true;
            settings.spellTomeLearning.xpPercentToGrant = stl.xpPercentToGrant !== undefined ? stl.xpPercentToGrant : 25;
            settings.spellTomeLearning.tomeInventoryBoost = stl.tomeInventoryBoost !== undefined ? stl.tomeInventoryBoost : true;
            settings.spellTomeLearning.tomeInventoryBoostPercent = stl.tomeInventoryBoostPercent !== undefined ? stl.tomeInventoryBoostPercent : 25;
            // Learning requirements
            settings.spellTomeLearning.requirePrereqs = stl.requirePrereqs !== undefined ? stl.requirePrereqs : true;
            settings.spellTomeLearning.requireAllPrereqs = stl.requireAllPrereqs !== undefined ? stl.requireAllPrereqs : true;
            settings.spellTomeLearning.requireSkillLevel = stl.requireSkillLevel !== undefined ? stl.requireSkillLevel : false;
        }
        updateSpellTomeLearningUI();
        
        // Load notification settings
        if (data.notifications) {
            var notif = data.notifications;
            if (!settings.notifications) {
                settings.notifications = { weakenedSpellNotifications: true, weakenedSpellInterval: 10 };
            }
            settings.notifications.weakenedSpellNotifications = notif.weakenedSpellNotifications !== undefined ? notif.weakenedSpellNotifications : true;
            settings.notifications.weakenedSpellInterval = notif.weakenedSpellInterval !== undefined ? notif.weakenedSpellInterval : 10;
        }
        updateNotificationsUI();
        
        // Update settings presets UI
        if (typeof updateSettingsPresetsUI === 'function') {
            updateSettingsPresetsUI();
        }
        
        if (verboseToggle) verboseToggle.checked = settings.verboseLogging;
        if (hotkeyInput) hotkeyInput.value = settings.hotkey;
        if (cheatInfo) cheatInfo.classList.toggle('hidden', !settings.cheatMode);
        
        // Update progression settings UI
        var xpDirectSlider = document.getElementById('xpDirectSlider');
        var xpSchoolSlider = document.getElementById('xpSchoolSlider');
        var xpAnySlider = document.getElementById('xpAnySlider');

        // Helper to update slider fill visual
        function updateSliderFillVisual(slider) {
            if (!slider) return;
            var percent = (slider.value - slider.min) / (slider.max - slider.min) * 100;
            slider.style.setProperty('--slider-fill', percent + '%');
        }

        setSegmentedToggleValue('learningModeToggle', settings.learningMode);

        // Auto-advance learning target
        var autoAdvanceToggle = document.getElementById('autoAdvanceLearningToggle');
        if (autoAdvanceToggle) autoAdvanceToggle.checked = settings.autoAdvanceLearning;
        setSegmentedToggleValue('autoAdvanceModeToggle', settings.autoAdvanceMode);
        setSegmentedToggleEnabled('autoAdvanceModeToggle', settings.autoAdvanceLearning);

        // Global multiplier slider
        var globalMultSlider = document.getElementById('xpGlobalMultiplierSlider');
        var globalMultValue = document.getElementById('xpGlobalMultiplierValue');
        if (globalMultSlider) {
            globalMultSlider.value = settings.xpGlobalMultiplier;
            updateSliderFillVisual(globalMultSlider);
            if (globalMultValue) globalMultValue.textContent = 'x' + settings.xpGlobalMultiplier;
        }
        
        if (xpDirectSlider) {
            xpDirectSlider.value = settings.xpMultiplierDirect;
            updateSliderFillVisual(xpDirectSlider);
            var xpDirectValue = document.getElementById('xpDirectValue');
            if (xpDirectValue) xpDirectValue.textContent = settings.xpMultiplierDirect + '%';
        }
        if (xpSchoolSlider) {
            xpSchoolSlider.value = settings.xpMultiplierSchool;
            updateSliderFillVisual(xpSchoolSlider);
            var xpSchoolValue = document.getElementById('xpSchoolValue');
            if (xpSchoolValue) xpSchoolValue.textContent = settings.xpMultiplierSchool + '%';
        }
        if (xpAnySlider) {
            xpAnySlider.value = settings.xpMultiplierAny;
            updateSliderFillVisual(xpAnySlider);
            var xpAnyValue = document.getElementById('xpAnyValue');
            if (xpAnyValue) xpAnyValue.textContent = settings.xpMultiplierAny + '%';
        }
        
        // XP Cap sliders
        var xpCapAnySlider = document.getElementById('xpCapAnySlider');
        var xpCapSchoolSlider = document.getElementById('xpCapSchoolSlider');
        var xpCapDirectSlider = document.getElementById('xpCapDirectSlider');
        
        if (xpCapAnySlider) {
            xpCapAnySlider.value = settings.xpCapAny;
            updateSliderFillVisual(xpCapAnySlider);
            var xpCapAnyValue = document.getElementById('xpCapAnyValue');
            if (xpCapAnyValue) xpCapAnyValue.textContent = settings.xpCapAny + '%';
        }
        if (xpCapSchoolSlider) {
            xpCapSchoolSlider.value = settings.xpCapSchool;
            updateSliderFillVisual(xpCapSchoolSlider);
            var xpCapSchoolValue = document.getElementById('xpCapSchoolValue');
            if (xpCapSchoolValue) xpCapSchoolValue.textContent = settings.xpCapSchool + '%';
        }
        if (xpCapDirectSlider) {
            xpCapDirectSlider.value = settings.xpCapDirect;
            updateSliderFillVisual(xpCapDirectSlider);
            var xpCapDirectValue = document.getElementById('xpCapDirectValue');
            if (xpCapDirectValue) xpCapDirectValue.textContent = settings.xpCapDirect + '%';
        }
        
        // Update tier XP inputs
        var tierInputs = [
            { id: 'xpNoviceInput', key: 'xpNovice' },
            { id: 'xpApprenticeInput', key: 'xpApprentice' },
            { id: 'xpAdeptInput', key: 'xpAdept' },
            { id: 'xpExpertInput', key: 'xpExpert' },
            { id: 'xpMasterInput', key: 'xpMaster' }
        ];
        
        tierInputs.forEach(function(cfg) {
            var input = document.getElementById(cfg.id);
            if (input) {
                input.value = settings[cfg.key];
            }
        });
        
        // Update reveal threshold sliders
        var revealSliders = [
            { id: 'revealNameSlider', valueId: 'revealNameValue', key: 'revealName', suffix: '%' },
            { id: 'revealEffectsSlider', valueId: 'revealEffectsValue', key: 'revealEffects', suffix: '%' },
            { id: 'revealDescSlider', valueId: 'revealDescValue', key: 'revealDescription', suffix: '%' }
        ];
        
        revealSliders.forEach(function(cfg) {
            var slider = document.getElementById(cfg.id);
            var valueEl = document.getElementById(cfg.valueId);
            if (slider) {
                slider.value = settings[cfg.key];
                updateSliderFillVisual(slider);
                if (valueEl) valueEl.textContent = settings[cfg.key] + cfg.suffix;
            }
        });
        
        // === Field Settings ===
        if (data.fields) {
            // Merge over the defaults rather than replace them: a settings file
            // written before a field existed (effectDetails, 2026-09) would
            // otherwise wipe that field out and the scan would silently omit it.
            for (var savedField in data.fields) {
                state.fields[savedField] = data.fields[savedField];
            }

            // Update field checkboxes
            for (var fieldName in data.fields) {
                var checkbox = document.getElementById('field_' + fieldName);
                if (checkbox) {
                    checkbox.checked = data.fields[fieldName];
                }
            }
        }
        
        // === Scan Mode ===
        // UI patch default: "Tomes only" is ON. Configs saved before the patch
        // carry an unchecked value from the old default, so honor a saved value
        // only once the patch marker has been written.
        var scanModeCheckbox = document.getElementById('scanModeTomes');
        if (scanModeCheckbox) {
            if (data.uiPatchDefaults && data.scanModeTomes !== undefined) {
                scanModeCheckbox.checked = data.scanModeTomes;
            } else {
                scanModeCheckbox.checked = true;
            }
        }
        settings.uiPatchDefaults = data.uiPatchDefaults || 1;
        
        // === Heart Animation Settings ===
        settings.heartAnimationEnabled = data.heartAnimationEnabled !== false;
        settings.heartPulseSpeed = data.heartPulseSpeed !== undefined ? data.heartPulseSpeed : 1;
        settings.heartPulseDelay = data.heartPulseDelay !== undefined ? data.heartPulseDelay : 0.75;
        settings.heartBgOpacity = data.heartBgOpacity !== undefined ? data.heartBgOpacity : 1.0;
        settings.heartBgColor = data.heartBgColor || '#000000';
        settings.heartRingColor = data.heartRingColor || '#b8a878';
        
        // === Camera Settings ===
        settings.focusOnClick = data.focusOnClick !== false;          // default true
        // Were never saved: they came back on with every start
        settings.showSelectionPath = data.showSelectionPath !== false;     // default true
        settings.showBaseConnections = data.showBaseConnections !== false; // default true
        settings.focusZoomOnClick = data.focusZoomOnClick !== false;  // default true
        settings.focusZoom = (typeof data.focusZoom === 'number' && data.focusZoom > 0) ? data.focusZoom : 1.0;
        settings.focusRotate = data.focusRotate === true;             // default false

        // === Language === ('' = whatever lang/locale.js says)
        settings.language = (typeof data.language === 'string') ? data.language : '';
        applySavedLanguage();
        settings.detailsLayout = data.detailsLayout === 'side' ? 'side' : 'bottom';
        if (typeof TreeNav !== 'undefined') TreeNav.applyDetailsLayout();
        settings.detailsOnHover = data.detailsOnHover !== false;   // default true
        if (typeof DetailsPeek !== 'undefined') DetailsPeek.applyLayout();
        settings.focusDimOthers = data.focusDimOthers !== false;   // default true

        // === Starfield Settings ===
        settings.starfieldEnabled = data.starfieldEnabled !== false;
        settings.starfieldFixed = data.starfieldFixed === true;
        settings.starfieldSeed = data.starfieldSeed !== undefined ? data.starfieldSeed : 42;
        settings.starfieldColor = data.starfieldColor || '#ffffff';
        settings.starfieldBgColor = data.starfieldBgColor || '#000000';
        settings.starfieldDensity = data.starfieldDensity !== undefined ? data.starfieldDensity : 100;
        settings.starfieldMaxSize = data.starfieldMaxSize !== undefined ? data.starfieldMaxSize : 2;
        
        // === Globe Settings ===
        settings.globeSize = data.globeSize !== undefined ? data.globeSize : 50;
        settings.globeDensity = data.globeDensity !== undefined ? data.globeDensity : 50;
        settings.globeDotMin = data.globeDotMin !== undefined ? data.globeDotMin : 0.5;
        settings.globeDotMax = data.globeDotMax !== undefined ? data.globeDotMax : 1;
        settings.globeColor = data.globeColor || '#b8a878';
        settings.magicTextColor = data.magicTextColor || '#ffecb3';
        settings.globeText = data.globeText || 'HEART';
        settings.globeTextSize = data.globeTextSize !== undefined ? data.globeTextSize : 16;
        settings.particleTrailEnabled = data.particleTrailEnabled !== false;
        settings.globeBgFill = data.globeBgFill !== false;
        settings.globeParticleRadius = data.globeParticleRadius !== undefined ? data.globeParticleRadius : 50;
        settings.nodeFontSize = data.nodeFontSize !== undefined ? data.nodeFontSize : 10;

        // Spell blacklist
        if (data.spellBlacklist && Array.isArray(data.spellBlacklist)) {
            settings.spellBlacklist = data.spellBlacklist;
            console.log('[SpellLearning] Loaded spell blacklist:', settings.spellBlacklist.length, 'entries');
        } else {
            settings.spellBlacklist = [];
        }

        // Plugin whitelist
        if (data.pluginWhitelist && Array.isArray(data.pluginWhitelist)) {
            settings.pluginWhitelist = data.pluginWhitelist;
            console.log('[SpellLearning] Loaded plugin whitelist:', settings.pluginWhitelist.length, 'entries');
        } else {
            settings.pluginWhitelist = [];
        }

        // Selected root spells per school
        if (data.selectedRoots && typeof data.selectedRoots === 'object' && !Array.isArray(data.selectedRoots)) {
            settings.selectedRoots = data.selectedRoots;
            console.log('[SpellLearning] Loaded selected roots:', Object.keys(settings.selectedRoots).length, 'schools');
        } else {
            settings.selectedRoots = {};
        }

        // An old config's treeGeneration block is ignored (and left in the file):
        // nothing reads it since the Alternate Pathways toggle went (2026-09-28)

        // Render popup: retire values it no longer offers, show the loaded ones
        if (typeof RenderSettings !== 'undefined') RenderSettings.afterLoad(data);

        // Apply heart settings to renderer
        applyHeartSettingsToRenderer();
        applyGlobeSettings();
        
        console.log('[SpellLearning] Unified config loaded:', {
            settings: settings,
            fields: state.fields
        });
        
        // NOW load presets from individual files.
        // This runs AFTER active preset names are set and legacy migration is done,
        // ensuring correct ordering: config loaded → migrate legacy → load files → apply.
        // Guard: only load presets once (LoadUnifiedConfig may be called from both
        // initializeSettings and onPrismaReady, causing onUnifiedConfigLoaded to fire twice).
        if (window.callCpp && !window._presetsLoadRequested) {
            window._presetsLoadRequested = true;
            console.log('[SpellLearning] Loading preset files from disk...');
            window.callCpp('LoadPresets', JSON.stringify({ type: 'settings' }));
            window.callCpp('LoadPresets', JSON.stringify({ type: 'scanner' }));
            DesignPresets.requestFromDisk();
        }
        
    } catch (e) {
        console.error('[SpellLearning] Failed to parse unified config:', e);
    }
};

// Export updateDeveloperModeVisibility for use by other modules (e.g., when school controls are created)
window.updateDeveloperModeVisibility = updateDeveloperModeVisibility;

// =============================================================================
// MODDED XP SOURCES - Dynamic UI for external mod XP sources
// =============================================================================

/**
 * Add a single modded XP source row to the UI.
 * Called when a source is registered (from C++) or loaded from config.
 */
function addModdedXPSourceUI(sourceId, displayName, multiplier, cap, enabled) {
    var section = document.getElementById('moddedXPSourcesSection');
    var list = document.getElementById('moddedXPSourcesList');
    if (!section || !list) return;

    section.style.display = '';  // Show section

    // Check if already exists
    if (document.getElementById('moddedSrc_' + sourceId)) return;

    var row = document.createElement('div');
    row.id = 'moddedSrc_' + sourceId;
    row.className = 'modded-xp-source-row';
    row.innerHTML =
        '<div class="modded-source-header">' +
            '<label class="toggle-switch toggle-sm">' +
                '<input type="checkbox" id="moddedEnabled_' + sourceId + '"' + (enabled ? ' checked' : '') + '>' +
                '<span class="toggle-slider"></span>' +
            '</label>' +
            '<span class="modded-source-name">' + displayName + '</span>' +
        '</div>' +
        '<div class="slider-grid slider-grid-2">' +
            '<div class="slider-compact">' +
                '<span class="slider-compact-label">Multiplier</span>' +
                '<div class="slider-compact-control">' +
                    '<input type="range" id="moddedMult_' + sourceId + '" min="0" max="200" value="' + multiplier + '" class="setting-slider">' +
                    '<span id="moddedMultVal_' + sourceId + '" class="slider-value">' + multiplier + '%</span>' +
                '</div>' +
            '</div>' +
            '<div class="slider-compact">' +
                '<span class="slider-compact-label">Cap</span>' +
                '<div class="slider-compact-control">' +
                    '<input type="range" id="moddedCap_' + sourceId + '" min="0" max="100" value="' + cap + '" class="setting-slider">' +
                    '<span id="moddedCapVal_' + sourceId + '" class="slider-value">' + cap + '%</span>' +
                '</div>' +
            '</div>' +
        '</div>';
    list.appendChild(row);

    // Wire enable toggle
    var enableToggle = document.getElementById('moddedEnabled_' + sourceId);
    if (enableToggle) {
        enableToggle.addEventListener('change', function() {
            if (settings.moddedXPSources[sourceId]) {
                settings.moddedXPSources[sourceId].enabled = this.checked;
            }
            if (typeof scheduleAutoSave === 'function') scheduleAutoSave();
        });
    }

    // Wire multiplier slider
    var multSlider = document.getElementById('moddedMult_' + sourceId);
    var multVal = document.getElementById('moddedMultVal_' + sourceId);
    if (multSlider) {
        updateSliderFillGlobal(multSlider);
        multSlider.addEventListener('input', function() {
            if (multVal) multVal.textContent = this.value + '%';
            updateSliderFillGlobal(this);
            if (settings.moddedXPSources[sourceId]) {
                settings.moddedXPSources[sourceId].multiplier = parseInt(this.value);
            }
        });
        multSlider.addEventListener('change', function() {
            if (typeof scheduleAutoSave === 'function') scheduleAutoSave();
        });
    }

    // Wire cap slider
    var capSlider = document.getElementById('moddedCap_' + sourceId);
    var capVal = document.getElementById('moddedCapVal_' + sourceId);
    if (capSlider) {
        updateSliderFillGlobal(capSlider);
        capSlider.addEventListener('input', function() {
            if (capVal) capVal.textContent = this.value + '%';
            updateSliderFillGlobal(this);
            if (settings.moddedXPSources[sourceId]) {
                settings.moddedXPSources[sourceId].cap = parseInt(this.value);
            }
        });
        capSlider.addEventListener('change', function() {
            if (typeof scheduleAutoSave === 'function') scheduleAutoSave();
        });
    }
}

/**
 * Rebuild all modded XP source UI rows from settings.moddedXPSources.
 * Called when loading config or applying presets.
 */
// Internal sources that use the modded cap system but have their own UI section
var INTERNAL_XP_SOURCES = { 'passive': true };

function rebuildModdedXPSourcesUI() {
    var list = document.getElementById('moddedXPSourcesList');
    var section = document.getElementById('moddedXPSourcesSection');
    if (list) list.innerHTML = '';

    var hasAny = false;
    for (var srcId in settings.moddedXPSources) {
        if (!settings.moddedXPSources.hasOwnProperty(srcId)) continue;
        if (INTERNAL_XP_SOURCES[srcId]) continue;
        var src = settings.moddedXPSources[srcId];
        addModdedXPSourceUI(srcId, src.displayName || srcId, src.multiplier, src.cap, src.enabled);
        hasAny = true;
    }

    if (section) section.style.display = hasAny ? '' : 'none';
}

/**
 * C++ -> JS callback when a modded XP source is registered.
 */
window.onModdedXPSourceRegistered = function(dataStr) {
    try {
        var data = typeof dataStr === 'string' ? JSON.parse(dataStr) : dataStr;
        if (INTERNAL_XP_SOURCES[data.sourceId]) return;
        if (!settings.moddedXPSources[data.sourceId]) {
            settings.moddedXPSources[data.sourceId] = {
                displayName: data.displayName,
                enabled: data.enabled !== false,
                multiplier: data.multiplier || 100,
                cap: data.cap || 25
            };
        }
        addModdedXPSourceUI(data.sourceId, data.displayName,
            data.multiplier || 100, data.cap || 25, data.enabled !== false);
    } catch (e) {
        console.error('[SpellLearning] Failed to parse modded XP source data:', e);
    }
};

// =============================================================================
// UI THEME SYSTEM - Auto-discovery from themes/ folder
// =============================================================================

/**
 * A theme's stylesheet as the page must link it. The theme files write cssFile
 * relative to themes/ ("../styles-skyrim.css"), but the link sits in index.html, one
 * folder up: taken as it was, "../styles-skyrim.css" pointed outside the panel, never
 * loaded, and switching to that theme left the old stylesheet in place.
 * @param {Object} theme - a themes/*.json definition
 * @returns {string} path relative to the panel
 */
function themeCssPath(theme) {
    var css = theme.cssFile || (theme.id + '.css');
    if (css.indexOf('../') === 0) return css.substring(3);
    return 'themes/' + css;
}

/**
 * Load all available themes from the themes/ folder
 * Reads manifest.json to get theme list, then loads each theme definition
 */
function loadThemesFromFolder() {
    return new Promise(function(resolve, reject) {
        console.log('[SpellLearning] Loading themes from themes/ folder...');
        
        // Fetch the manifest
        fetch('themes/manifest.json')
            .then(function(response) {
                if (!response.ok) {
                    throw new Error('Failed to load themes manifest: ' + response.status);
                }
                return response.json();
            })
            .then(function(manifest) {
                if (!manifest.themes || !Array.isArray(manifest.themes)) {
                    throw new Error('Invalid manifest: missing themes array');
                }
                
                console.log('[SpellLearning] Found', manifest.themes.length, 'themes in manifest');
                
                // Load each theme definition
                var themePromises = manifest.themes.map(function(themeId) {
                    return fetch('themes/' + themeId + '.json')
                        .then(function(response) {
                            if (!response.ok) {
                                console.warn('[SpellLearning] Failed to load theme:', themeId);
                                return null;
                            }
                            return response.json();
                        })
                        .then(function(themeData) {
                            if (themeData && themeData.id) {
                                return themeData;
                            }
                            return null;
                        })
                        .catch(function(err) {
                            console.warn('[SpellLearning] Error loading theme', themeId + ':', err);
                            return null;
                        });
                });
                
                return Promise.all(themePromises);
            })
            .then(function(themes) {
                // Filter out failed loads and populate UI_THEMES
                UI_THEMES = {};
                themes.forEach(function(theme) {
                    if (theme && theme.id) {
                        UI_THEMES[theme.id] = {
                            name: theme.name || theme.id,
                            file: themeCssPath(theme),
                            description: theme.description || '',
                            author: theme.author || '',
                            version: theme.version || '1.0'
                        };
                    }
                });
                
                themesLoaded = true;
                console.log('[SpellLearning] Loaded', Object.keys(UI_THEMES).length, 'themes:', Object.keys(UI_THEMES).join(', '));
                resolve(UI_THEMES);
            })
            .catch(function(err) {
                console.error('[SpellLearning] Failed to load themes:', err);
                // Fall back to built-in themes
                UI_THEMES = {
                    'skyrim': {
                        name: 'Skyrim Edge',
                        file: 'styles-skyrim.css',
                        description: 'Native Skyrim-style flat UI with muted tones'
                    }
                };
                themesLoaded = true;
                console.log('[SpellLearning] Using fallback themes');
                resolve(UI_THEMES);
            });
    });
}

/**
 * Load the UI themes for the designs. The theme has no selector of its own any
 * more: a design sets it (DesignPresets.onThemesLoaded).
 */
function initializeThemeSelector() {
    loadThemesFromFolder().then(function() {
        if (typeof DesignPresets !== 'undefined') DesignPresets.onThemesLoaded();
    });
}

/**
 * Apply a UI theme by swapping the stylesheet
 * @param {string} themeKey - Key from UI_THEMES
 */
function applyTheme(themeKey) {
    var theme = UI_THEMES[themeKey];
    if (!theme) {
        console.error('[SpellLearning] Unknown theme:', themeKey);
        return;
    }
    
    // Find the current stylesheet link
    // By id: an add-on theme's stylesheet need not have "styles" in its name
    var styleLink = document.getElementById('ui-theme-css') ||
        document.querySelector('link[rel="stylesheet"][href*="styles"]');
    if (!styleLink) {
        console.error('[SpellLearning] Could not find stylesheet link');
        return;
    }
    
    // Get current href to check if already applied
    var currentHref = styleLink.getAttribute('href');
    var newHref = theme.file;
    
    // Normalize paths for comparison
    if (currentHref === newHref || currentHref.endsWith(newHref.replace('../', ''))) {
        console.log('[SpellLearning] Theme already applied:', themeKey);
        return;
    }
    
    console.log('[SpellLearning] Switching theme from', currentHref, 'to', newHref);
    
    // Create a new link element for the new stylesheet
    var newLink = document.createElement('link');
    newLink.rel = 'stylesheet';
    newLink.href = newHref;
    newLink.id = 'ui-theme-css';
    
    // When the new stylesheet loads, remove the old one
    newLink.onload = function() {
        styleLink.remove();
        console.log('[SpellLearning] Theme applied:', themeKey);
        
        // Re-apply dynamic styles that might be overwritten
        if (settings.learningColor) {
            applyLearningColor(settings.learningColor);
        }
        if (settings.fontSizeMultiplier) {
            applyFontSizeMultiplier(settings.fontSizeMultiplier);
        }
    };
    
    newLink.onerror = function() {
        console.error('[SpellLearning] Failed to load theme stylesheet:', newHref);
    };
    
    // Insert the new link after the old one
    styleLink.parentNode.insertBefore(newLink, styleLink.nextSibling);
}

// =============================================================================
// UI DISPLAY HELPERS
// =============================================================================

/**
 * Apply learning color to CSS variables
 * @param {string} color - Hex color value
 */
function applyLearningColor(color) {
    if (!color) return;
    
    var root = document.documentElement;
    root.style.setProperty('--learning-color', color);
    root.style.setProperty('--node-learning-border', color);
    
    // Parse hex to RGB for transparent versions
    var r = parseInt(color.slice(1, 3), 16);
    var g = parseInt(color.slice(3, 5), 16);
    var b = parseInt(color.slice(5, 7), 16);
    
    root.style.setProperty('--node-learning-bg', 'rgba(' + r + ', ' + g + ', ' + b + ', 0.2)');
    root.style.setProperty('--node-learning-glow', 'rgba(' + r + ', ' + g + ', ' + b + ', 0.5)');
    
    console.log('[SpellLearning] Applied learning color:', color);
}

/**
 * Apply font size multiplier to the entire UI
 * @param {number} multiplier - Font size multiplier (0.7 - 1.5)
 */
function applyFontSizeMultiplier(multiplier) {
    if (!multiplier || multiplier < 0.5 || multiplier > 2) {
        multiplier = 1.0;
    }
    
    var root = document.documentElement;
    root.style.setProperty('--font-size-multiplier', multiplier);
    
    // Apply to body font size (base is 14px in Skyrim theme)
    var baseFontSize = 14;
    document.body.style.fontSize = (baseFontSize * multiplier) + 'px';
    
    console.log('[SpellLearning] Applied font size multiplier:', multiplier);
}

// =============================================================================
// SPELL TOME LEARNING UI UPDATE
// =============================================================================

/**
 * Update spell tome learning UI elements from settings
 */
function updateSpellTomeLearningUI() {
    var stl = settings.spellTomeLearning;
    
    // Main toggle - Vanilla vs Progression system
    var progressionToggle = document.getElementById('useProgressionSystemToggle');
    if (progressionToggle) progressionToggle.checked = stl.useProgressionSystem;
    
    // Tome inventory boost toggle
    var inventoryBoostToggle = document.getElementById('tomeInventoryBoostToggle');
    if (inventoryBoostToggle) inventoryBoostToggle.checked = stl.tomeInventoryBoost;
    
    // XP percent to grant slider
    var xpGrantSlider = document.getElementById('tomeXpGrantSlider');
    if (xpGrantSlider) {
        xpGrantSlider.value = stl.xpPercentToGrant;
        var xpGrantValue = document.getElementById('tomeXpGrantValue');
        if (xpGrantValue) xpGrantValue.textContent = stl.xpPercentToGrant + '%';
        updateSliderFillGlobal(xpGrantSlider);
    }
    
    // Inventory boost percent slider
    var boostSlider = document.getElementById('tomeInventoryBoostSlider');
    if (boostSlider) {
        boostSlider.value = stl.tomeInventoryBoostPercent;
        var boostValue = document.getElementById('tomeInventoryBoostValue');
        if (boostValue) boostValue.textContent = '+' + stl.tomeInventoryBoostPercent + '%';
        updateSliderFillGlobal(boostSlider);
    }
    
    // Learning requirements toggles
    var requirePrereqsToggle = document.getElementById('tomeRequirePrereqsToggle');
    if (requirePrereqsToggle) requirePrereqsToggle.checked = stl.requirePrereqs;
    
    var requireAllPrereqsToggle = document.getElementById('tomeRequireAllPrereqsToggle');
    if (requireAllPrereqsToggle) requireAllPrereqsToggle.checked = stl.requireAllPrereqs;
    
    var requireSkillLevelToggle = document.getElementById('tomeRequireSkillLevelToggle');
    if (requireSkillLevelToggle) requireSkillLevelToggle.checked = stl.requireSkillLevel;
    
    // Show/hide child setting based on parent
    var allPrereqsRow = document.getElementById('tomeRequireAllPrereqsRow');
    if (allPrereqsRow) allPrereqsRow.style.display = stl.requirePrereqs ? '' : 'none';
    
    // Update description based on mode
    var modeDesc = document.getElementById('tomeLearningModeDesc');
    if (modeDesc) {
        if (stl.useProgressionSystem) {
            modeDesc.textContent = 'Reading tomes grants XP and gives early access to weakened spells. Keep tomes to practice!';
        } else {
            modeDesc.textContent = 'Vanilla behavior: Reading tomes instantly teaches spells and consumes the book.';
        }
    }
}

// =============================================================================
// NOTIFICATIONS UI UPDATE
// =============================================================================

/**
 * Update notification settings UI elements from settings
 */
function updateNotificationsUI() {
    // Ensure settings exist
    if (!settings.notifications) {
        settings.notifications = { weakenedSpellNotifications: true, weakenedSpellInterval: 10 };
    }
    var notif = settings.notifications;
    
    // Weakened spell notifications toggle
    var weakenedToggle = document.getElementById('weakenedNotificationsToggle');
    if (weakenedToggle) weakenedToggle.checked = notif.weakenedSpellNotifications;
    
    // Notification interval slider
    var intervalSlider = document.getElementById('notificationIntervalSlider');
    if (intervalSlider) {
        intervalSlider.value = notif.weakenedSpellInterval;
        var intervalValue = document.getElementById('notificationIntervalValue');
        if (intervalValue) intervalValue.textContent = notif.weakenedSpellInterval + 's';
        updateSliderFillGlobal(intervalSlider);
    }
    
    // Show/hide interval row based on toggle
    var intervalRow = document.getElementById('notificationIntervalRow');
    if (intervalRow) {
        intervalRow.style.display = notif.weakenedSpellNotifications ? 'flex' : 'none';
    }
}

// =============================================================================
// HEART ANIMATION SETTINGS
// =============================================================================

/**
 * Initialize heart animation settings popup
 * Guarded against double-initialization (multiple calls are safe).
 */
function initializeHeartSettings() {
    // Guard against double init — multiple handlers would toggle-cancel each other
    if (window._heartSettingsInitialized) {
        console.log('[HeartSettings] Already initialized, skipping');
        return;
    }
    
    var settingsBtn = document.getElementById('heart-settings-btn');
    var popup = document.getElementById('heart-settings-popup');
    var closeBtn = document.getElementById('heart-settings-close');
    
    if (!settingsBtn || !popup) {
        console.log('[HeartSettings] Missing elements - btn:', !!settingsBtn, 'popup:', !!popup);
        return;
    }
    
    window._heartSettingsInitialized = true;
    console.log('[HeartSettings] Initializing...');
    
    // Toggle popup visibility
    settingsBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        e.preventDefault();
        var isHidden = popup.style.display === 'none' || popup.style.display === '';
        popup.style.display = isHidden ? 'block' : 'none';
        console.log('[HeartSettings] Toggled popup:', isHidden ? 'open' : 'closed');
    });
    
    // Close button
    if (closeBtn) {
        closeBtn.addEventListener('click', function() {
            popup.style.display = 'none';
        });
    }
    
    // Close when clicking outside (but not on color picker)
    document.addEventListener('click', function(e) {
        if (popup.style.display !== 'none' && !popup.contains(e.target) && e.target !== settingsBtn) {
            // Don't close if clicking on color picker popup
            var colorPickerPopup = document.querySelector('.color-picker-popup');
            if (colorPickerPopup && colorPickerPopup.contains(e.target)) return;
            popup.style.display = 'none';
        }
    });
    
    // Animation enabled toggle
    var animToggle = document.getElementById('heart-animation-enabled');
    if (animToggle) {
        animToggle.checked = settings.heartAnimationEnabled !== false;
        animToggle.addEventListener('change', function() {
            settings.heartAnimationEnabled = this.checked;
            applyHeartSettingsToRenderer();
            autoSaveSettings();
        });
    }
    
    // Background opacity - no longer a slider, controlled by globeBgFill toggle
    // heartBgOpacity is kept as a fixed value (1.0) for renderer compatibility
    
    // =========================================================================
    // STARFIELD SETTINGS
    // =========================================================================
    
    // Starfield enabled toggle
    var starfieldEnabled = document.getElementById('starfield-enabled');
    if (starfieldEnabled) {
        starfieldEnabled.checked = settings.starfieldEnabled !== false;
        starfieldEnabled.addEventListener('change', function() {
            settings.starfieldEnabled = this.checked;
            applyHeartSettingsToRenderer();
            autoSaveSettings();
        });
    }
    
    // === Connection Lines Settings ===

    // Selection path toggle
    var showSelectionPath = document.getElementById('show-selection-path');
    if (showSelectionPath) {
        showSelectionPath.checked = settings.showSelectionPath !== false;
        showSelectionPath.addEventListener('change', function() {
            settings.showSelectionPath = this.checked;
            if (state.treeData && typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer._needsRender = true;
            }
            autoSaveSettings();
        });
    }

    // Base connections toggle
    var showBaseConnections = document.getElementById('show-base-connections');
    if (showBaseConnections) {
        showBaseConnections.checked = settings.showBaseConnections !== false;
        showBaseConnections.addEventListener('change', function() {
            settings.showBaseConnections = this.checked;
            if (state.treeData && typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer._needsRender = true;
            }
            autoSaveSettings();
        });
    }

    // === Camera Settings ===

    // Center clicked node toggle
    var focusOnClickToggle = document.getElementById('focus-on-click');
    if (focusOnClickToggle) {
        focusOnClickToggle.checked = settings.focusOnClick !== false;
        focusOnClickToggle.addEventListener('change', function() {
            settings.focusOnClick = this.checked;
            autoSaveSettings();
        });
    }

    // Zoom on click toggle
    var focusZoomToggle = document.getElementById('focus-zoom-on-click');
    if (focusZoomToggle) {
        focusZoomToggle.checked = settings.focusZoomOnClick !== false;
        focusZoomToggle.addEventListener('change', function() {
            settings.focusZoomOnClick = this.checked;
            autoSaveSettings();
        });
    }

    // Rotate the wheel on focus toggle
    var focusRotateToggle = document.getElementById('focus-rotate');
    if (focusRotateToggle) {
        focusRotateToggle.checked = settings.focusRotate === true;
        focusRotateToggle.addEventListener('change', function() {
            settings.focusRotate = this.checked;
            autoSaveSettings();
        });
    }

    // Dim others (focus + context) toggle
    var focusDimToggle = document.getElementById('focus-dim-others');
    if (focusDimToggle) {
        focusDimToggle.checked = settings.focusDimOthers !== false;
        focusDimToggle.addEventListener('change', function() {
            settings.focusDimOthers = this.checked;
            if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
            autoSaveSettings();
        });
    }

    // Focus zoom level slider
    var focusZoomSlider = document.getElementById('tree-focus-zoom');
    var focusZoomVal = document.getElementById('tree-focus-zoom-val');
    if (focusZoomSlider) {
        var focusZoomValue = (typeof settings.focusZoom === 'number' && settings.focusZoom > 0) ? settings.focusZoom : 1.0;
        focusZoomSlider.value = focusZoomValue;
        if (focusZoomVal) focusZoomVal.textContent = Math.round(focusZoomValue * 100) + '%';
        focusZoomSlider.addEventListener('input', function() {
            settings.focusZoom = parseFloat(this.value);
            if (focusZoomVal) focusZoomVal.textContent = Math.round(settings.focusZoom * 100) + '%';
            autoSaveSettings();
        });
    }

    // Edge style toggle (straight vs curved)
    var edgeStyleToggle = document.getElementById('edge-style-toggle');
    if (edgeStyleToggle) {
        edgeStyleToggle.checked = settings.edgeStyle === 'curved';
        edgeStyleToggle.addEventListener('change', function() {
            settings.edgeStyle = this.checked ? 'curved' : 'straight';
            if (state.treeData && typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer._needsRender = true;
            }
            autoSaveSettings();
        });
    }

    // === Globe Settings ===

    // Particle trail toggle
    var particleTrailToggle = document.getElementById('popup-particle-trail');
    if (particleTrailToggle) {
        particleTrailToggle.checked = settings.particleTrailEnabled !== false;
        particleTrailToggle.addEventListener('change', function() {
            settings.particleTrailEnabled = this.checked;
            applyGlobeSettings();
            autoSaveSettings();
        });
    }

    // Show node names toggle
    var showNodeNamesPopup = document.getElementById('popup-show-node-names');
    if (showNodeNamesPopup) {
        showNodeNamesPopup.checked = settings.showNodeNames !== false;
        showNodeNamesPopup.addEventListener('change', function() {
            settings.showNodeNames = this.checked;
            if (typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer._needsRender = true;
            }
            autoSaveSettings();
        });
    }

    // Node font size slider
    var nodeFontSize = document.getElementById('tree-node-font-size');
    var nodeFontSizeVal = document.getElementById('tree-node-font-size-val');
    if (nodeFontSize) {
        nodeFontSize.value = settings.nodeFontSize || 10;
        if (nodeFontSizeVal) nodeFontSizeVal.textContent = settings.nodeFontSize || 10;
        nodeFontSize.addEventListener('input', function() {
            settings.nodeFontSize = parseInt(this.value);
            if (nodeFontSizeVal) nodeFontSizeVal.textContent = this.value;
            if (typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer._needsRender = true;
            }
            autoSaveSettings();
        });
    }

    // === PRM Enable/Disable Toggle (inside Pre Req Master tab) ===
    // Disables the content area below the tab bar, not the tab bar itself
    var prmEnabled = document.getElementById('prmEnabled');
    var prmSplit = document.querySelector('#prmContent .prm-split');
    if (prmEnabled && prmSplit) {
        if (!prmEnabled.checked) {
            prmSplit.classList.add('disabled');
        }
        // Stop toggle clicks from triggering the parent tab's click
        prmEnabled.closest('.prm-enable-toggle').addEventListener('click', function(e) {
            e.stopPropagation();
        });
        prmEnabled.addEventListener('change', function() {
            if (this.checked) {
                prmSplit.classList.remove('disabled');
            } else {
                prmSplit.classList.add('disabled');
            }
        });
    }

    // Apply initial settings to renderer
    applyHeartSettingsToRenderer();
    applyGlobeSettings();
    if (typeof RenderSettings !== 'undefined') RenderSettings.init();
    console.log('[HeartSettings] Initialized successfully');

}

/**
 * Apply globe settings to the Globe3D module
 */
function applyGlobeSettings() {
    if (typeof Globe3D !== 'undefined') {
        // The design's render block decides these (DesignPresets.renderValue)
        var rv = renderValue;
        // A design that sets the core size but not the particle radius means the
        // one size for both; the player's particle radius counts only when the
        // design says nothing about either
        var designSetsSize = typeof DesignPresets !== 'undefined' && DesignPresets.renderHas &&
            DesignPresets.renderHas('globeSize') && !DesignPresets.renderHas('globeParticleRadius');
        var globeRadius = designSetsSize ? rv('globeSize', 0) : (rv('globeParticleRadius', 0) || rv('globeSize', 0) || 50);
        var sizeChanged = Globe3D.radius !== globeRadius;
        var countChanged = Globe3D.particleCount !== (rv('globeDensity', 0) || 50);
        var dotMinChanged = Globe3D.dotSizeMin !== (rv('globeDotMin', 0) || 0.5);
        var dotMaxChanged = Globe3D.dotSizeMax !== (rv('globeDotMax', 0) || 1);

        Globe3D.radius = globeRadius;
        Globe3D.globeCenterZ = -globeRadius;
        Globe3D.particleCount = rv('globeDensity', 0) || 50;

        // Store size range for particle initialization
        Globe3D.dotSizeMin = rv('globeDotMin', 0) || 0.5;
        Globe3D.dotSizeMax = rv('globeDotMax', 0) || 1;

        // Particle trail enabled
        Globe3D.trailEnabled = stilled('particleTrailEnabled', settings.particleTrailEnabled !== false);

        // Reinitialize if particle count, size, or dot sizes changed
        if (countChanged || sizeChanged || dotMinChanged || dotMaxChanged) {
            Globe3D.init();
        }

        if (typeof CanvasRenderer !== 'undefined') {
            CanvasRenderer._needsRender = true;
        }
    }
}

/**
 * Apply heart settings to the canvas renderer
 */
/**
 * A renderer value: the design's render block if it sets the key
 * (DesignPresets.renderValue), else the player's setting, else the fallback.
 */
function renderValue(key, fallback) {
    if (typeof DesignPresets !== 'undefined' && DesignPresets.renderValue) return DesignPresets.renderValue(key, fallback);
    return settings[key] !== undefined ? settings[key] : fallback;
}

/** A moving part's switch with the master "still everything" switch in mind (RenderSettings). */
function stilled(key, value) {
    if (typeof RenderSettings !== 'undefined' && RenderSettings.stilled) return RenderSettings.stilled(key, value);
    return value;
}

function applyHeartSettingsToRenderer() {
    var rv = renderValue;
    if (typeof CanvasRenderer !== 'undefined') {
        // Heart settings
        CanvasRenderer._heartbeatSpeed = rv('heartPulseSpeed', 1);
        CanvasRenderer._heartPulseDelay = rv('heartPulseDelay', 0.75);
        CanvasRenderer._heartAnimationEnabled = stilled('heartAnimationEnabled', settings.heartAnimationEnabled !== false);
        CanvasRenderer._heartBgOpacity = 1.0;
        CanvasRenderer._heartBgColor = rv('heartBgColor', '') || '#000000';
        CanvasRenderer._heartRingColor = rv('heartRingColor', '') || '#b8a878';
        CanvasRenderer._learningPathColor = rv('learningPathColor', '') || '#00ffff';

        // Globe colors and text
        CanvasRenderer._globeColor = rv('globeColor', '') || rv('heartRingColor', '') || '#b8a878';
        CanvasRenderer._magicTextColor = rv('magicTextColor', '') || rv('heartRingColor', '') || '#ffecb3';
        CanvasRenderer._globeText = rv('globeText', '') || 'HEART';
        CanvasRenderer._globeTextSize = rv('globeTextSize', 0) || 16;
        CanvasRenderer._particleCoreEnabled = rv('particleCoreEnabled', false) === true;
        CanvasRenderer._globeBgFill = rv('globeBgFill', true) !== false;

        // Starfield settings
        CanvasRenderer._starfieldEnabled = settings.starfieldEnabled !== false;
        CanvasRenderer._starfieldFixed = settings.starfieldFixed === true;
        // Stars and globe held still: the twinkle switch, or "still everything"
        CanvasRenderer._starsStill = stilled('starTwinkle', settings.starTwinkle !== false) === false;
        CanvasRenderer._globeStill = stilled('globeSpin', true) === false;
        CanvasRenderer._starfieldColor = rv('starfieldColor', '') || '#ffffff';
        CanvasRenderer._starfieldDensity = rv('starfieldDensity', 0) || 200;
        CanvasRenderer._starfieldMaxSize = rv('starfieldMaxSize', 0) || 2.5;
        CanvasRenderer._starfieldSeed = rv('starfieldSeed', 0) || 42;
        CanvasRenderer._bgColor = rv('starfieldBgColor', '') || '#000000';

        CanvasRenderer._needsRender = true;
    }

    // Apply background color to tree container
    var bg = rv('starfieldBgColor', '');
    if (bg) {
        var treeContainer = document.getElementById('tree-container');
        if (treeContainer) {
            treeContainer.style.background = bg;
        }
    }
}

// =============================================================================
// SPELL BLACKLIST PANEL
// =============================================================================

function showBlacklistModal() {
    var modal = document.getElementById('blacklist-modal');
    if (!modal) return;

    modal.classList.remove('hidden');

    var searchInput = document.getElementById('blacklist-search');
    if (searchInput) {
        searchInput.value = '';
        searchInput.focus();
    }

    var dropdown = document.getElementById('blacklist-search-results');
    if (dropdown) dropdown.classList.add('hidden');

    renderBlacklistEntries();
    setupBlacklistListeners();
}

function hideBlacklistModal() {
    var modal = document.getElementById('blacklist-modal');
    if (modal) modal.classList.add('hidden');
    autoSaveSettings();
    if (typeof updatePrimedCount === 'function') updatePrimedCount();
}

/**
 * Load all available spells for blacklist search.
 * Reuses the same 3-source pattern from editMode.js loadAllSpells().
 */
function loadBlacklistSpellSources() {
    var allSpells = [];
    var seenIds = {};

    function addSpell(formId, name, school) {
        if (!formId || seenIds[formId]) return;
        seenIds[formId] = true;
        allSpells.push({ formId: formId, name: name || formId, school: school || 'Unknown' });
    }

    // Source 1: Scanned spell data
    if (state.lastSpellData && state.lastSpellData.spells) {
        state.lastSpellData.spells.forEach(function(spell) {
            addSpell(spell.formId || spell.id, spell.name || spell.spellName, spell.school);
        });
    }

    // Source 2: CanvasRenderer nodes
    if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.nodes) {
        CanvasRenderer.nodes.forEach(function(node) {
            addSpell(node.formId || node.id, node.name, node.school);
        });
    }

    // Source 3: Tree rawData
    if (state.treeData && state.treeData.rawData && state.treeData.rawData.schools) {
        var rawSchools = state.treeData.rawData.schools;
        for (var schoolName in rawSchools) {
            var school = rawSchools[schoolName];
            if (school.nodes) {
                school.nodes.forEach(function(node) {
                    addSpell(node.formId || node.id || node.spellId, node.name || node.spellName, schoolName);
                });
            }
        }
    }

    return allSpells;
}

function renderBlacklistSearchResults(searchTerm) {
    var dropdown = document.getElementById('blacklist-search-results');
    if (!dropdown) return;

    if (!searchTerm || searchTerm.length < 2) {
        dropdown.classList.add('hidden');
        return;
    }

    var allSpells = loadBlacklistSpellSources();
    var term = searchTerm.toLowerCase();

    var blacklistedIds = {};
    (settings.spellBlacklist || []).forEach(function(entry) {
        blacklistedIds[entry.formId] = true;
    });

    var filtered = allSpells.filter(function(spell) {
        if (blacklistedIds[spell.formId]) return false;
        var nameMatch = (spell.name || '').toLowerCase().indexOf(term) !== -1;
        var schoolMatch = (spell.school || '').toLowerCase().indexOf(term) !== -1;
        var idMatch = (spell.formId || '').toLowerCase().indexOf(term) !== -1;
        return nameMatch || schoolMatch || idMatch;
    });

    filtered.sort(function(a, b) {
        return (a.name || '').localeCompare(b.name || '');
    });

    filtered = filtered.slice(0, 10);

    if (filtered.length === 0) {
        dropdown.innerHTML = '<div class="spawn-no-results">No matching spells found</div>';
        dropdown.classList.remove('hidden');
        return;
    }

    dropdown.innerHTML = '';
    filtered.forEach(function(spell) {
        var item = document.createElement('div');
        item.className = 'spawn-spell-item';
        item.innerHTML =
            '<div class="spawn-spell-name">' + (spell.name || spell.formId) + '</div>' +
            '<div class="spawn-spell-info"><span>' + (spell.school || 'Unknown') + '</span></div>';

        item.addEventListener('click', function() {
            addToBlacklist(spell);
        });

        dropdown.appendChild(item);
    });

    dropdown.classList.remove('hidden');
}

function addToBlacklist(spell) {
    if (!settings.spellBlacklist) settings.spellBlacklist = [];

    // Use stable plugin:localFormId key for matching (survives load order changes)
    var localId = typeof getLocalFormId === 'function' ? getLocalFormId(spell.formId) : '';
    var plugin = spell.plugin || '';

    var exists = settings.spellBlacklist.some(function(entry) {
        // Match by stable key if available, fall back to raw formId
        if (entry.plugin && entry.localFormId && plugin && localId) {
            return entry.plugin.toLowerCase() === plugin.toLowerCase() && entry.localFormId === localId;
        }
        return entry.formId === spell.formId;
    });

    if (!exists) {
        settings.spellBlacklist.push({
            formId: spell.formId,
            name: spell.name || spell.formId,
            school: spell.school || 'Unknown',
            plugin: plugin,
            localFormId: localId
        });
        console.log('[SpellLearning] Blacklisted spell:', spell.name, '(' + plugin + ':' + localId + ')');
    }

    var searchInput = document.getElementById('blacklist-search');
    if (searchInput) searchInput.value = '';
    var dropdown = document.getElementById('blacklist-search-results');
    if (dropdown) dropdown.classList.add('hidden');

    renderBlacklistEntries();
    autoSaveSettings();
}

function removeFromBlacklist(plugin, localFormId, formId) {
    if (!settings.spellBlacklist) return;

    settings.spellBlacklist = settings.spellBlacklist.filter(function(entry) {
        // Match by stable key if available
        if (plugin && localFormId && entry.plugin && entry.localFormId) {
            return !(entry.plugin.toLowerCase() === plugin.toLowerCase() && entry.localFormId === localFormId);
        }
        // Fall back to raw formId
        return entry.formId !== formId;
    });

    console.log('[SpellLearning] Removed from blacklist:', plugin + ':' + localFormId);
    renderBlacklistEntries();
    autoSaveSettings();
}

function clearBlacklist() {
    settings.spellBlacklist = [];
    console.log('[SpellLearning] Blacklist cleared');
    renderBlacklistEntries();
    autoSaveSettings();
}

function renderBlacklistEntries() {
    var container = document.getElementById('blacklist-entries');
    var countEl = document.getElementById('blacklist-count');
    if (!container) return;

    var blacklist = settings.spellBlacklist || [];

    if (countEl) countEl.textContent = blacklist.length;

    if (blacklist.length === 0) {
        container.innerHTML = '<div class="blacklist-empty">No spells blacklisted</div>';
        return;
    }

    container.innerHTML = '';
    blacklist.forEach(function(entry) {
        var div = document.createElement('div');
        div.className = 'blacklist-entry';
        var subtitle = (entry.school || '');
        if (entry.plugin) subtitle += (subtitle ? ' - ' : '') + entry.plugin;
        div.innerHTML =
            '<div class="blacklist-entry-info">' +
                '<div class="blacklist-entry-name">' + (entry.name || entry.formId) + '</div>' +
                '<div class="blacklist-entry-school">' + subtitle + '</div>' +
            '</div>' +
            '<button class="blacklist-remove-btn" title="Remove from blacklist">&times;</button>';

        var removeBtn = div.querySelector('.blacklist-remove-btn');
        removeBtn.addEventListener('click', function() {
            removeFromBlacklist(entry.plugin, entry.localFormId, entry.formId);
        });

        container.appendChild(div);
    });
}

function setupBlacklistListeners() {
    var modal = document.getElementById('blacklist-modal');
    var searchInput = document.getElementById('blacklist-search');
    var closeBtn = document.getElementById('blacklist-modal-close');
    var doneBtn = document.getElementById('blacklist-done');
    var clearBtn = document.getElementById('blacklist-clear-all');
    var backdrop = modal ? modal.querySelector('.modal-backdrop') : null;

    // Clone search input to remove old listeners
    if (searchInput) {
        var newSearch = searchInput.cloneNode(true);
        searchInput.parentNode.replaceChild(newSearch, searchInput);
        searchInput = newSearch;

        searchInput.addEventListener('input', function() {
            renderBlacklistSearchResults(this.value);
        });
    }

    if (closeBtn) closeBtn.onclick = function() { hideBlacklistModal(); };
    if (doneBtn) doneBtn.onclick = function() { hideBlacklistModal(); };
    if (clearBtn) clearBtn.onclick = function() { clearBlacklist(); };
    if (backdrop) backdrop.onclick = function() { hideBlacklistModal(); };
}

// Export blacklist functions
window.showBlacklistModal = showBlacklistModal;
window.hideBlacklistModal = hideBlacklistModal;

// =============================================================================
// WHITELIST MODAL - Plugin filtering for spell scanning
// =============================================================================

// Base game plugins that are always shown at the top
var BASE_GAME_PLUGINS = [
    'Skyrim.esm',
    'Update.esm',
    'Dawnguard.esm',
    'HearthFires.esm',
    'Dragonborn.esm'
];

/**
 * Extract plugin name from a spell object.
 * Uses persistentId format: "PluginName.esp|0x00123456"
 */
function extractPluginFromSpell(spell) {
    // Debug: log first spell's fields to see what's available
    if (!extractPluginFromSpell._logged && spell) {
        console.log('[Whitelist] Sample spell fields:', Object.keys(spell));
        console.log('[Whitelist] Sample spell data:', JSON.stringify(spell).substring(0, 500));
        extractPluginFromSpell._logged = true;
    }

    if (spell.persistentId && spell.persistentId.includes('|')) {
        return spell.persistentId.split('|')[0];
    }
    // Fallback: try source field if available
    if (spell.source) {
        return spell.source;
    }
    // Fallback: try plugin field
    if (spell.plugin) {
        return spell.plugin;
    }
    return null;
}

/**
 * Analyze cached spells and build plugin spell counts.
 * Returns: { 'PluginName.esp': 42, ... }
 */
function buildPluginSpellCounts() {
    var counts = {};

    // Debug: check state availability
    console.log('[Whitelist] state exists:', typeof state !== 'undefined');
    console.log('[Whitelist] state.lastSpellData exists:', state && typeof state.lastSpellData !== 'undefined');
    console.log('[Whitelist] state.lastSpellData.spells exists:', state && state.lastSpellData && typeof state.lastSpellData.spells !== 'undefined');

    if (state && state.lastSpellData) {
        console.log('[Whitelist] lastSpellData keys:', Object.keys(state.lastSpellData));
    }

    // Get spells from state.lastSpellData (populated after scan)
    var allSpells = [];
    if (state && state.lastSpellData && state.lastSpellData.spells) {
        allSpells = state.lastSpellData.spells;
    }
    // Fallback to scannedSpellData global
    if (allSpells.length === 0 && typeof scannedSpellData !== 'undefined' && scannedSpellData) {
        allSpells = scannedSpellData;
    }

    console.log('[Whitelist] Building plugin counts from ' + allSpells.length + ' spells');

    allSpells.forEach(function(spell) {
        var plugin = extractPluginFromSpell(spell);
        if (plugin) {
            counts[plugin] = (counts[plugin] || 0) + 1;
        }
    });

    console.log('[Whitelist] Found plugins:', Object.keys(counts));
    return counts;
}

/**
 * Show the whitelist modal and populate it with plugins.
 */
function showWhitelistModal() {
    var modal = document.getElementById('whitelist-modal');
    if (!modal) return;

    modal.classList.remove('hidden');

    var searchInput = document.getElementById('whitelist-search');
    if (searchInput) {
        searchInput.value = '';
    }

    renderWhitelistEntries();
    setupWhitelistListeners();
}

/**
 * Hide the whitelist modal and save settings.
 */
function hideWhitelistModal() {
    var modal = document.getElementById('whitelist-modal');
    if (modal) modal.classList.add('hidden');
    autoSaveSettings();
    if (typeof updatePrimedCount === 'function') updatePrimedCount();
}

/**
 * Render all plugin entries in the whitelist modal.
 */
function renderWhitelistEntries() {
    var baseContainer = document.getElementById('whitelist-base-entries');
    var modContainer = document.getElementById('whitelist-mod-entries');
    var baseCountEl = document.getElementById('whitelist-base-count');
    var modCountEl = document.getElementById('whitelist-mod-count');

    if (!baseContainer || !modContainer) return;

    // Build current plugin spell counts
    var pluginCounts = buildPluginSpellCounts();
    var allPlugins = Object.keys(pluginCounts).sort(function(a, b) {
        return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    // Separate base game and mod plugins
    var basePlugins = [];
    var modPlugins = [];

    allPlugins.forEach(function(plugin) {
        var isBase = BASE_GAME_PLUGINS.some(function(bp) {
            return bp.toLowerCase() === plugin.toLowerCase();
        });
        if (isBase) {
            basePlugins.push(plugin);
        } else {
            modPlugins.push(plugin);
        }
    });

    // Also add base game plugins that might not have spells but should be shown
    BASE_GAME_PLUGINS.forEach(function(bp) {
        var exists = basePlugins.some(function(p) {
            return p.toLowerCase() === bp.toLowerCase();
        });
        if (!exists) {
            basePlugins.push(bp);
        }
    });

    // Get current whitelist state
    var whitelist = settings.pluginWhitelist || [];
    var whitelistMap = {};
    whitelist.forEach(function(entry) {
        whitelistMap[entry.plugin.toLowerCase()] = entry.enabled;
    });

    // Render base game plugins
    baseContainer.innerHTML = '';
    var enabledBaseCount = 0;

    basePlugins.forEach(function(plugin) {
        var count = pluginCounts[plugin] || 0;
        var isEnabled = whitelistMap[plugin.toLowerCase()] !== false; // Default to enabled
        if (isEnabled) enabledBaseCount++;

        // Ensure base game plugins are actually in the whitelist array (not just visually checked)
        if (isEnabled && whitelistMap[plugin.toLowerCase()] === undefined) {
            updateWhitelistEntry(plugin, true);
        }

        var div = createWhitelistEntry(plugin, count, isEnabled);
        baseContainer.appendChild(div);
    });

    if (baseCountEl) {
        baseCountEl.textContent = enabledBaseCount + '/' + basePlugins.length;
    }

    // Render mod plugins
    modContainer.innerHTML = '';
    var enabledModCount = 0;

    if (modPlugins.length === 0) {
        modContainer.innerHTML = '<div class="whitelist-empty">Scan spells first to see mod plugins</div>';
    } else {
        modPlugins.forEach(function(plugin) {
            var count = pluginCounts[plugin] || 0;
            var isEnabled = whitelistMap[plugin.toLowerCase()] !== false; // Default to enabled for all plugins
            if (isEnabled) enabledModCount++;

            // Ensure mod plugins are actually in the whitelist array (not just visually checked)
            if (isEnabled && whitelistMap[plugin.toLowerCase()] === undefined) {
                updateWhitelistEntry(plugin, true);
            }

            var div = createWhitelistEntry(plugin, count, isEnabled);
            modContainer.appendChild(div);
        });
    }

    if (modCountEl) {
        modCountEl.textContent = enabledModCount + '/' + modPlugins.length;
    }
}

/**
 * Create a single whitelist entry DOM element.
 */
function createWhitelistEntry(plugin, count, isEnabled) {
    var div = document.createElement('div');
    div.className = 'whitelist-entry';
    div.dataset.plugin = plugin.toLowerCase();

    div.innerHTML =
        '<div class="whitelist-entry-left">' +
            '<input type="checkbox" class="whitelist-checkbox" ' + (isEnabled ? 'checked' : '') + '>' +
            '<span class="whitelist-entry-name">' + plugin + '</span>' +
        '</div>' +
        '<span class="whitelist-entry-count">(' + count + ' spells)</span>';

    // Click anywhere on the row to toggle
    div.addEventListener('click', function(e) {
        var checkbox = div.querySelector('.whitelist-checkbox');
        if (e.target !== checkbox) {
            checkbox.checked = !checkbox.checked;
        }
        updateWhitelistEntry(plugin, checkbox.checked);
    });

    return div;
}

/**
 * Update a plugin's whitelist status.
 */
function updateWhitelistEntry(plugin, enabled) {
    if (!settings.pluginWhitelist) settings.pluginWhitelist = [];

    var found = false;
    settings.pluginWhitelist.forEach(function(entry) {
        if (entry.plugin.toLowerCase() === plugin.toLowerCase()) {
            entry.enabled = enabled;
            found = true;
        }
    });

    if (!found) {
        settings.pluginWhitelist.push({
            plugin: plugin,
            enabled: enabled,
            spellCount: 0
        });
    }

    // Update counts display
    updateWhitelistCounts();
}

/**
 * Update the count displays in the whitelist modal.
 */
function updateWhitelistCounts() {
    var baseContainer = document.getElementById('whitelist-base-entries');
    var modContainer = document.getElementById('whitelist-mod-entries');
    var baseCountEl = document.getElementById('whitelist-base-count');
    var modCountEl = document.getElementById('whitelist-mod-count');

    if (baseContainer && baseCountEl) {
        var baseEntries = baseContainer.querySelectorAll('.whitelist-entry');
        var checkedBase = baseContainer.querySelectorAll('.whitelist-checkbox:checked').length;
        baseCountEl.textContent = checkedBase + '/' + baseEntries.length;
    }

    if (modContainer && modCountEl) {
        var modEntries = modContainer.querySelectorAll('.whitelist-entry');
        var checkedMod = modContainer.querySelectorAll('.whitelist-checkbox:checked').length;
        modCountEl.textContent = checkedMod + '/' + modEntries.length;
    }
}

/**
 * Set all plugins to enabled or disabled.
 */
function setAllWhitelist(enabled) {
    var entries = document.querySelectorAll('#whitelist-modal .whitelist-entry');
    entries.forEach(function(entry) {
        var checkbox = entry.querySelector('.whitelist-checkbox');
        var plugin = entry.dataset.plugin;
        if (checkbox && plugin) {
            checkbox.checked = enabled;
            updateWhitelistEntry(plugin, enabled);
        }
    });
}

/**
 * Enable only base game plugins.
 */
function setBaseOnlyWhitelist() {
    var entries = document.querySelectorAll('#whitelist-modal .whitelist-entry');
    entries.forEach(function(entry) {
        var checkbox = entry.querySelector('.whitelist-checkbox');
        var plugin = entry.dataset.plugin;
        if (checkbox && plugin) {
            var isBase = BASE_GAME_PLUGINS.some(function(bp) {
                return bp.toLowerCase() === plugin.toLowerCase();
            });
            checkbox.checked = isBase;
            updateWhitelistEntry(plugin, isBase);
        }
    });
}

/**
 * Filter visible entries based on search term.
 */
function filterWhitelistEntries(searchTerm) {
    var entries = document.querySelectorAll('#whitelist-modal .whitelist-entry');
    var term = searchTerm.toLowerCase();

    entries.forEach(function(entry) {
        var plugin = entry.dataset.plugin || '';
        if (!term || plugin.indexOf(term) !== -1) {
            entry.classList.remove('hidden');
        } else {
            entry.classList.add('hidden');
        }
    });
}

/**
 * Set up event listeners for the whitelist modal.
 */
function setupWhitelistListeners() {
    var modal = document.getElementById('whitelist-modal');
    var searchInput = document.getElementById('whitelist-search');
    var closeBtn = document.getElementById('whitelist-modal-close');
    var doneBtn = document.getElementById('whitelist-done');
    var allBtn = document.getElementById('whitelist-all');
    var noneBtn = document.getElementById('whitelist-none');
    var baseOnlyBtn = document.getElementById('whitelist-base-only');
    var backdrop = modal ? modal.querySelector('.modal-backdrop') : null;

    // Clone search input to remove old listeners
    if (searchInput) {
        var newSearch = searchInput.cloneNode(true);
        searchInput.parentNode.replaceChild(newSearch, searchInput);
        searchInput = newSearch;

        searchInput.addEventListener('input', function() {
            filterWhitelistEntries(this.value);
        });
    }

    if (closeBtn) closeBtn.onclick = function() { hideWhitelistModal(); };
    if (doneBtn) doneBtn.onclick = function() { hideWhitelistModal(); };
    if (allBtn) allBtn.onclick = function() { setAllWhitelist(true); };
    if (noneBtn) noneBtn.onclick = function() { setAllWhitelist(false); };
    if (baseOnlyBtn) baseOnlyBtn.onclick = function() { setBaseOnlyWhitelist(); };
    if (backdrop) backdrop.onclick = function() { hideWhitelistModal(); };
}

// Export whitelist functions
window.showWhitelistModal = showWhitelistModal;
window.hideWhitelistModal = hideWhitelistModal;
window.extractPluginFromSpell = extractPluginFromSpell;
