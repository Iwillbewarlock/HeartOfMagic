/**
 * SpellLearning State Module
 * 
 * Contains all global state: settings, UI state, progression tracking.
 * Depends on: constants.js (for DEFAULT_TREE_RULES)
 */

// =============================================================================
// SETTINGS
// =============================================================================

// Available UI themes - populated dynamically from themes/ folder
// To add a theme: 
//   1. Create themes/mytheme.json with {id, name, description, cssFile}
//   2. Add "mytheme" to themes/manifest.json
var UI_THEMES = {};
var themesLoaded = false;

var settings = {
    hotkey: 'F8',
    hotkeyCode: 66,  // DirectInput scancode for F8
    pauseGameOnFocus: true,  // If false, game continues running when UI is open
    cheatMode: false,
    
    // Heart animation settings
    heartAnimationEnabled: true,
    heartPulseSpeed: 0.5,
    heartPulseDelay: 2.75,
    heartBgOpacity: 1.0,

    // Starfield settings
    starfieldEnabled: true,
    starfieldFixed: false,
    starfieldColor: '#ffffff',
    starfieldDensity: 250,
    starfieldMaxSize: 3,

    // Globe settings
    globeSize: 50,
    globeDensity: 50,
    globeDotMin: 0.5,
    globeDotMax: 1,
    globeColor: '#b8a878',
    magicTextColor: '#ffecb3',
    globeText: 'HEART',
    globeTextSize: 16,
    particleTrailEnabled: true,
    globeBgFill: true,
    globeParticleRadius: 50,  // Separate control for globe particle area radius
    particleCoreEnabled: false,  // Replace center text with vibrating particle core
    
    heartBgColor: '#000000',
    heartRingColor: '#b8a878',
    learningPathColor: '#00ffff',

    // Connection visibility settings
    showSelectionPath: true,      // Show white highlight path when node selected
    showBaseConnections: true,    // Show dim connection lines between all nodes
    // Camera (vanilla perk-menu style navigation)
    focusOnClick: true,           // Center the clicked node on screen
    focusZoomOnClick: true,       // Zoom in to focusZoom when centering (never zooms out)
    focusZoom: 1.0,               // Target zoom level for click focus (0.5 - 2.0)
    language: '',                 // Panel language code; '' = the default in lang/locale.js
    focusRotate: false,           // Turn the wheel so the focused school is on top (off: the view just travels there)
    detailsLayout: 'bottom',      // 'bottom' = info bar under the tree, 'side' = right sidebar
    detailsOnHover: true,         // Resting the cursor on a spell previews its card; the panel stays open (modules/detailsPeek.js)
    focusDimOthers: true,         // Fade nodes outside the selected node's path (other schools most)
    edgeStyle: 'straight',        // 'straight' or 'curved' (Bezier) edge rendering
    nodeSizeScaling: true,
    showNodeNames: true,
    nodeFontSize: 10,
    showSchoolDividers: true,
    strictPieSlices: true,  // Keep schools strictly in their pie slices (vs. allowing overlap)
    dividerFade: 50,      // 0-100, percentage of line length to fade out
    dividerSpacing: 3,    // pixels between parallel divider lines
    dividerLength: 800,   // length of divider lines in pixels
    dividerColorMode: 'school',  // 'school' or 'custom'
    dividerCustomColor: '#ffffff',
    verboseLogging: false,
    // UI Display settings
    uiTheme: 'skyrim',          // Current UI theme key
    reverseUnlock: true,        // A known spell opens its direct prerequisites (recalculateNodeAvailability, C++ IsUnlockedByKnownChild)
    reverseUnlockToRoot: false, // ...and every spell below them down to the root, not just the direct ones
    // ...and they cost this share of their XP, by the opened spell's tier (getRequiredXPForNode)
    reverseUnlockXPNovice: 0.3,
    reverseUnlockXPApprentice: 0.4,
    reverseUnlockXPAdept: 0.5,
    reverseUnlockXPExpert: 0.7,
    reverseUnlockXPMaster: 0.8,
    // ...and, with reverseXpSeparate, gain XP at their own rates (C++ GetGainRates)
    reverseXpSeparate: false,
    reverseXpGlobalMultiplier: 1,
    reverseXpMultiplierDirect: 100,
    reverseXpMultiplierSchool: 50,
    reverseXpMultiplierAny: 10,
    reverseXpCapAny: 5,
    reverseXpCapSchool: 15,
    reverseXpCapDirect: 50,
    // Design preset effects the player turned off (designEffectsSetting.js); empty = all on
    designEffects: {},
    animationsOff: false,       // Render popup: one switch that stills every moving part (renderSettings.js)
    starTwinkle: true,          // Render popup: stars drift and twinkle (off: drawn still)
    designPreset: 'arcane',     // Design preset id (modules/designPresets.js); add-ons add more in presets/design/
    learningColor: '#7890A8',   // Color for learning state nodes/lines
    fontSizeMultiplier: 1.0,    // Global font size multiplier (0.5 - 2.0)
    // Tree generation settings
    aggressivePathValidation: true,   // Strict reachability check (safe but simple trees)
    // Progression settings
    learningMode: 'perSchool',  // 'perSchool' or 'single'
    autoAdvanceLearning: true,  // Auto-select next spell when one is mastered
    autoAdvanceMode: 'branch',  // 'branch' = next in tree, 'random' = any available in school
    xpGlobalMultiplier: 1,
    // XP multipliers (how much XP per cast)
    xpMultiplierDirect: 100,
    xpMultiplierSchool: 50,
    xpMultiplierAny: 10,
    // XP caps (max % of total XP from each source)
    xpCapAny: 5,        // Max 5% from casting any spell
    xpCapSchool: 15,    // Max 15% from same-school spells
    xpCapDirect: 50,    // Max 50% from direct prerequisite casts
    // Remaining 50% must come from self-casting the learning target
    // Modded XP sources (registered by external mods, each with multiplier + cap)
    moddedXPSources: {},
    // Tier XP requirements
    xpNovice: 100,
    xpApprentice: 200,
    xpAdept: 400,
    xpExpert: 800,
    xpMaster: 1500,
    // Progressive reveal thresholds (%)
    revealName: 0,
    revealEffects: 25,
    revealDescription: 50,
    // Window position and size
    windowX: null,
    windowY: null,
    windowWidth: null,
    windowHeight: null,
    // School colors (dynamically grows with detected schools)
    schoolColors: {
        'Destruction': '#ef4444',
        'Restoration': '#facc15',
        'Alteration': '#22c55e',
        'Conjuration': '#a855f7',
        'Illusion': '#38bdf8'
    },
    // School visibility (which schools to show on tree)
    schoolVisibility: {
        // All schools visible by default, dynamically grows
    },
    // Discovery mode
    discoveryMode: true,
    showRootSpellNames: true,  // Show root spell names even in discovery mode (helps players know what to look for)
    // Early spell learning
    earlySpellLearning: {
        enabled: true,
        unlockThreshold: 25,
        minEffectiveness: 20,      // Derived from powerSteps[0].power
        maxEffectiveness: 80,      // Derived from last powerStep.power
        selfCastRequiredAt: 75,
        selfCastXPMultiplier: 150,
        binaryEffectThreshold: 80,
        modifyGameDisplay: true,   // Show "(Learning - X%)" in game menus
        // Configurable power steps (XP threshold -> power %)
        // Names avoid vanilla tier confusion
        powerSteps: [
            { xp: 25, power: 20, label: "Budding" },       // Stage 1
            { xp: 40, power: 35, label: "Developing" },    // Stage 2
            { xp: 55, power: 50, label: "Practicing" },    // Stage 3
            { xp: 70, power: 65, label: "Advancing" },     // Stage 4
            { xp: 85, power: 80, label: "Refining" }       // Stage 5
            // 100% XP = 100% power = "Mastered" (implicit)
        ]
    },
    // Spell Tome Learning settings
    spellTomeLearning: {
        enabled: true,                    // Master toggle for tome hook
        useProgressionSystem: true,       // true = XP/weakened spell, false = vanilla instant learn
        grantXPOnRead: true,              // Grant XP when reading tome
        autoSetLearningTarget: true,      // Auto-set spell as learning target
        showNotifications: true,          // Show in-game notifications
        xpPercentToGrant: 25,             // % of required XP to grant on tome read
        tomeInventoryBoost: true,         // Enable inventory boost feature
        tomeInventoryBoostPercent: 25,    // % bonus XP when tome is in inventory
        // Prerequisite requirements for tome learning
        requirePrereqs: true,             // Require tree prerequisites to be mastered
        requireAllPrereqs: true,          // Require ALL prereqs (vs just one)
        requireSkillLevel: false          // Require minimum skill level for spell tier
    },
    // In-game notification settings
    notifications: {
        weakenedSpellNotifications: true, // Show "X operating at Y% power" when casting weakened spells
        weakenedSpellInterval: 10         // Seconds between notifications (default 10)
    },
    // Passive learning settings
    passiveLearning: {
        enabled: false,
        scope: 'novice',        // 'all', 'root', 'novice'
        xpPerGameHour: 5,
        maxByTier: {
            novice: 100,
            apprentice: 75,
            adept: 50,
            expert: 25,
            master: 5
        }
    },
    // Spell blacklist (excluded from tree building)
    // Each entry: { formId: '0x...', name: 'Spell Name', school: 'School' }
    spellBlacklist: [],
    // Plugin whitelist (only scan spells from these plugins when enabled)
    // Each entry: { plugin: 'PluginName.esp', enabled: true, spellCount: 42 }
    // If empty or all disabled, all plugins are scanned (default behavior)
    pluginWhitelist: [],
    // User-selected root spells per school (optional override for tree building)
    // { "Destruction": { formId: "0x...", name: "Flames", plugin: "Skyrim.esm", localFormId: "012FCD" }, ... }
    // Missing school = auto-pick (default behavior)
    selectedRoots: {}
    // settings.treeGeneration is gone (2026-09-28): its builder fields went with
    // the removed builders and its last flag, bidirectionalSoftPrereqs, was the
    // inert Alternate Pathways toggle. An old config's treeGeneration is ignored.
};

// Settings presets (user-saved progression/early spell/tome configurations)
var settingsPresets = {};

// Scanner presets (user-saved tree building configurations)
var scannerPresets = {};

// Per-node XP requirement overrides (formId -> requiredXP)
var xpOverrides = {};

// =============================================================================
// UI STATE
// =============================================================================

var state = {
    isMinimized: false,
    isFullscreen: false,
    isDragging: false,
    isResizing: false,
    isSettingsOpen: false,
    currentTab: 'spellTree',
    lastSpellData: null,
    promptModified: false,
    originalPrompt: DEFAULT_TREE_RULES,
    // Field output settings
    fields: {
        editorId: true,
        magickaCost: true,
        minimumSkill: true,
        castingType: true,
        delivery: true,
        chargeTime: false,
        plugin: true,
        effects: true,
        effectNames: false,
        keywords: true,
        // MGEF structure per effect - what the tag librarian classifies on
        effectDetails: true
    },
    // Tree viewer state
    treeData: null,
    treeInitialized: false,
    clearTreePending: false,
    // Clipboard paste target
    pasteTarget: null,
    // Progression tracking
    learningTargets: {},  // school -> formId
    spellProgress: {},    // formId -> {xp, required, unlocked, ready}
    selectedNode: null,
    playerKnownSpells: new Set(),
    weakenedSpells: new Set()  // Spells the player has in weakened/early-learned state (not fully mastered)
};

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

// Global helper to update slider fill
function updateSliderFillGlobal(slider) {
    if (!slider) return;
    var percent = (slider.value - slider.min) / (slider.max - slider.min) * 100;
    slider.style.setProperty('--slider-fill', percent + '%');
}
