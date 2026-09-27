/**
 * SpellLearning Configuration Module
 * 
 * Contains all configuration constants for the spell learning tree.
 * This module is loaded first and provides configuration for all other modules.
 * Depends on: state.js (for settings reference)
 */

// =============================================================================
// UNIFIED GRID CONFIGURATION - Single source of truth for all layout modules
// =============================================================================

var GRID_CONFIG = {
    nodeSize: 75,                    // Base node size in pixels
    
    // Multipliers (relative to nodeSize)
    baseRadiusMultiplier: 1.2,       // Starting radius = nodeSize * 1.2 (~90px)
    tierSpacingMultiplier: 0.7,      // Radial spacing = nodeSize * 0.7 (~52px) - 2x denser
    arcSpacingMultiplier: 0.75,      // Angular spacing = nodeSize * 0.75 (~56px) - 2x denser
    minNodeSpacingMultiplier: 0.7,   // Min node-to-node = nodeSize * 0.7 (~52px)
    
    // Computed values (call getComputedConfig() for these)
    maxTiers: 25,
    schoolPadding: 15,
    
    // Get computed pixel values
    getComputedConfig: function() {
        var ns = this.nodeSize;
        return {
            nodeSize: ns,
            baseRadius: Math.round(ns * this.baseRadiusMultiplier),
            tierSpacing: Math.round(ns * this.tierSpacingMultiplier),
            arcSpacing: Math.round(ns * this.arcSpacingMultiplier),
            minNodeSpacing: Math.round(ns * this.minNodeSpacingMultiplier),
            maxTiers: this.maxTiers,
            schoolPadding: this.schoolPadding
        };
    }
};

// =============================================================================
// TREE CONFIGURATION
// =============================================================================

var TREE_CONFIG = {
    wheel: {
        baseRadius: GRID_CONFIG.getComputedConfig().baseRadius,
        tierSpacing: GRID_CONFIG.getComputedConfig().tierSpacing,
        nodeWidth: GRID_CONFIG.nodeSize,
        nodeHeight: 28,
        minArcSpacing: GRID_CONFIG.getComputedConfig().arcSpacing,
        schoolPadding: GRID_CONFIG.schoolPadding
    },
    tierScaling: {
        enabled: true,
        baseWidth: 70,
        baseHeight: 26,
        widthIncrement: 12,
        heightIncrement: 5
    },
    zoom: {
        min: 0.1,
        max: 3,
        step: 0.2,
        wheelFactor: 0.001
    },
    animation: {
        rotateDuration: 400
    },
    schools: ['Destruction', 'Restoration', 'Alteration', 'Conjuration', 'Illusion'],
    
    // Function to get school color (uses settings)
    getSchoolColor: function(school) {
        return settings.schoolColors[school] || getOrAssignSchoolColor(school);
    },
    
    // Layout styles for spell trees
    layoutStyles: {
        radial: {
            name: 'Radial Fan',
            description: 'Nodes spread outward in a fan pattern. Best for balanced trees with many branches at each tier.',
            idealFor: 'Trees with 2-3 children per node, balanced branching'
        },
        focused: {
            name: 'Focused Beam',
            description: 'Nodes stay close to the center spoke line. Best for linear progressions with few branches.',
            idealFor: 'Linear spell chains, single-path progressions'
        },
        clustered: {
            name: 'Clustered Groups',
            description: 'Related spells cluster together in distinct groups. Best for trees with clear thematic divisions.',
            idealFor: 'Elemental branches (Fire/Frost/Shock), distinct spell families'
        },
        cascading: {
            name: 'Cascading Waterfall',
            description: 'Nodes cascade downward in staggered columns. Best for deep trees with consistent width.',
            idealFor: 'Many tiers, steady progression paths'
        },
        organic: {
            name: 'Organic Flow',
            description: 'Slightly randomized positions for a natural feel. Best for varied, unpredictable spell collections.',
            idealFor: 'Mixed spell types, modded spell packs'
        }
    }
};

// =============================================================================
// KEY CODES FOR HOTKEY MAPPING (DirectInput scancodes; the one copy - settingsPanel.js reads it)
// =============================================================================

var KEY_CODES = {
    'F1': 59, 'F2': 60, 'F3': 61, 'F4': 62, 'F5': 63, 'F6': 64,
    'F7': 65, 'F8': 66, 'F9': 67, 'F10': 68, 'F11': 87, 'F12': 88,
    '0': 11, '1': 2, '2': 3, '3': 4, '4': 5, '5': 6, '6': 7, '7': 8, '8': 9, '9': 10,
    'A': 30, 'B': 48, 'C': 46, 'D': 32, 'E': 18, 'F': 33, 'G': 34, 'H': 35,
    'I': 23, 'J': 36, 'K': 37, 'L': 38, 'M': 50, 'N': 49, 'O': 24, 'P': 25,
    'Q': 16, 'R': 19, 'S': 31, 'T': 20, 'U': 22, 'V': 47, 'W': 17, 'X': 45,
    'Y': 21, 'Z': 44,
    'NUMPAD0': 82, 'NUMPAD1': 79, 'NUMPAD2': 80, 'NUMPAD3': 81, 'NUMPAD4': 75,
    'NUMPAD5': 76, 'NUMPAD6': 77, 'NUMPAD7': 71, 'NUMPAD8': 72, 'NUMPAD9': 73,
    'HOME': 199, 'END': 207, 'INSERT': 210, 'DELETE': 211,
    'PAGEUP': 201, 'PAGEDOWN': 209
};

// Export for module pattern (though we're using globals for compatibility)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        GRID_CONFIG: GRID_CONFIG,
        TREE_CONFIG: TREE_CONFIG,
        KEY_CODES: KEY_CODES
    };
}
