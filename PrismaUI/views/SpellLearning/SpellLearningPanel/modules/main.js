/**
 * SpellLearning Main Entry Point
 * 
 * Initializes all modules and sets up event listeners.
 * This should be loaded LAST after all other modules.
 * 
 * Load order:
 * 1. constants.js - Default prompts, profiles, palettes
 * 2. config.js - TREE_CONFIG
 * 3. state.js - settings, state objects
 * 4. spellCache.js - SpellCache
 * 5. colorUtils.js - Color management
 * 6. uiHelpers.js - UI utilities
 * 7. growthDSL.js - Growth recipe system
 * 8. treeParser.js - TreeParser
 * 9. script.js - WheelRenderer and all app logic (temporary until fully modularized)
 * 10. main.js - This file (initialization)
 */

// =============================================================================
// INITIALIZATION
// =============================================================================

document.addEventListener('DOMContentLoaded', function() {
    console.log('[SpellLearning] Panel initializing (modular)...');
    
    // Apply translations to DOM elements with data-i18n attributes
    if (typeof applyI18nToDOM === 'function') applyI18nToDOM();
    
    // Initialize all components
    try {
        // Core UI
        if (typeof initializePanel === 'function') initializePanel();
        if (typeof initializeTabs === 'function') initializeTabs();
        if (typeof initializePromptEditor === 'function') initializePromptEditor();
        
        // Drag and resize
        if (typeof initializeDragging === 'function') initializeDragging();
        if (typeof initializeResizing === 'function') initializeResizing();
        if (typeof PanelSnap !== 'undefined') PanelSnap.init();   // whole pixels (panelSnap.js)
        
        // Tree viewer
        if (typeof initializeTreeViewer === 'function') initializeTreeViewer();
        
        // Settings
        if (typeof initializeSettings === 'function') {
            try {
                initializeSettings();
            } catch (settingsErr) {
                console.error('[SpellLearning] Settings init error (non-fatal):', settingsErr);
            }
        }
        
        // Heart settings popup (initialized independently to survive settings init errors)
        if (typeof initializeHeartSettings === 'function') {
            try {
                initializeHeartSettings();
            } catch (heartErr) {
                console.error('[SpellLearning] Heart settings init error:', heartErr);
            }
        }

        // Its own step: it used to be wired from inside initializeHeartSettings,
        // so any throw in there left the player unable to change language at all
        if (typeof initializeLanguageSelect === 'function') {
            try {
                initializeLanguageSelect();
            } catch (langErr) {
                console.error('[SpellLearning] Language picker init error:', langErr);
            }
        }
        
        // Pre Req Master
        if (typeof PreReqMaster !== 'undefined' && PreReqMaster.init) PreReqMaster.init();

        // Easy Mode (scanner tab sub-mode)
        if (typeof initializeEasyMode === 'function') initializeEasyMode();
        
        // Textarea enter key handling
        if (typeof initializeTextareaEnterKey === 'function') initializeTextareaEnterKey();
        
        console.log('[SpellLearning] Panel initialized successfully');
    } catch (e) {
        console.error('[SpellLearning] Initialization error:', e);
    }
});

// =============================================================================
// MODULE VERIFICATION
// =============================================================================

// Verify all required globals exist
(function verifyModules() {
    var required = [
        'DEFAULT_TREE_RULES',
        'DEFAULT_COLOR_PALETTE',
        'KEY_CODES',
        'TREE_CONFIG',
        'settings',
        'state',
        'SpellCache',
        'TreeParser',
        'GROWTH_DSL'
    ];
    
    var missing = required.filter(function(name) {
        return typeof window[name] === 'undefined';
    });
    
    if (missing.length > 0) {
        console.warn('[SpellLearning] Missing globals:', missing.join(', '));
    } else {
        console.log('[SpellLearning] All required modules loaded');
    }
})();
