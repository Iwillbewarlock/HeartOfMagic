/**
 * Shape Profiles Module - shape configuration for tree layout
 *
 * This module defines how different shape profiles affect node placement.
 * Used by wheelRenderer (getSchoolVisualModifier). The shape masks, school
 * default shapes and config helpers went with the JS tree builders that used
 * them (2026-09-27).
 *
 * Exports (global):
 * - SHAPE_PROFILES: Configuration for each shape type
 * - getShapeProfile(shapeName): Get profile by name
 */

// =============================================================================
// SHAPE PROFILES
// =============================================================================

/**
 * Shape profiles define visual characteristics for each layout style.
 * These values are used consistently across all layout modules.
 */
var SHAPE_PROFILES = {
    organic: {
        name: 'Organic Flow',
        description: 'Natural, flowing growth pattern with gentle variation',

        // Jitter settings (relative to base values)
        radiusJitter: 0.20,        // 20% radius variation
        angleJitter: 12,           // 12 degrees angle variation

        // Spacing multipliers
        tierSpacingMult: 0.9,      // Slightly compact tiers
        spreadMult: 0.95,          // Almost full sector width

        // Behavior flags
        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: false,

        // Density control
        densityMult: 1.0,          // Normal density
        innerDensityBoost: 1.2,    // Slightly denser near center
        outerDensityFade: 0.8      // Slightly sparser at edges
    },

    spiky: {
        name: 'Spiky Crystals',
        description: 'Dramatic, angular spikes reaching outward',

        radiusJitter: 0.15,        // Moderate - keep spikes straight
        angleJitter: 3,            // Very tight - stay on ray line

        tierSpacingMult: 1.6,      // VERY elongated outward
        spreadMult: 0.3,           // VERY narrow — 3 thin rays

        fillPieSlice: false,       // Let spikes poke out
        curveEdges: false,
        clusterNodes: false,

        densityMult: 0.6,          // Sparse
        innerDensityBoost: 1.0,
        outerDensityFade: 0.4      // Very sparse at tips
    },

    radial: {
        name: 'Radial Fan',
        description: 'Uniform, evenly-spread radial pattern',

        radiusJitter: 0.08,        // Very uniform
        angleJitter: 3,            // Evenly spread

        tierSpacingMult: 0.85,     // Compact tiers
        spreadMult: 1.0,           // Full pie usage

        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: false,

        densityMult: 1.1,          // Slightly denser than organic
        innerDensityBoost: 1.0,
        outerDensityFade: 1.0      // Uniform density
    },

    mountain: {
        name: 'Mountain Peak',
        description: 'Packed base tapering to a peak',

        radiusJitter: 0.15,
        angleJitter: 10,

        tierSpacingMult: 0.55,     // Very compressed tiers (wide packed base)
        spreadMult: 1.1,           // Use MORE than full pie width at base

        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: false,
        taperSpread: true,         // Narrows toward tips
        taperAmount: 0.15,         // Only 15% width at peak (very narrow)
        fillTriangle: true,        // Fill triangular area

        densityMult: 1.5,          // Very dense packing
        innerDensityBoost: 2.0,    // Extremely dense at base
        outerDensityFade: 0.3      // Very sparse at peak
    },

    cloud: {
        name: 'Cloud Clusters',
        description: 'Loose clusters with organic spacing',

        radiusJitter: 0.30,        // Clustered groupings
        angleJitter: 18,           // Irregular

        tierSpacingMult: 1.0,
        spreadMult: 0.85,

        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: true,        // Group related nodes
        clusterSize: 3,            // Nodes per cluster
        clusterSpacing: 1.5,       // Space between clusters

        densityMult: 0.9,
        innerDensityBoost: 1.1,
        outerDensityFade: 0.7
    },

    cascade: {
        name: 'Cascading Waterfall',
        description: 'Clear tier separation with staggered columns',

        radiusJitter: 0.06,
        angleJitter: 4,

        tierSpacingMult: 1.3,      // Clear tier separation
        spreadMult: 1.0,

        fillPieSlice: true,
        curveEdges: false,         // Straight edges for clarity
        clusterNodes: false,

        densityMult: 1.0,
        innerDensityBoost: 1.0,
        outerDensityFade: 1.0
    },

    linear: {
        name: 'Linear Beam',
        description: 'Focused narrow progression',

        radiusJitter: 0.05,
        angleJitter: 2,

        tierSpacingMult: 1.1,
        spreadMult: 0.5,           // Narrow focused beam

        fillPieSlice: false,       // Intentionally narrow
        curveEdges: true,
        clusterNodes: false,

        densityMult: 0.8,          // Linear = fewer nodes per tier
        innerDensityBoost: 1.0,
        outerDensityFade: 1.0
    },

    grid: {
        name: 'Perfect Grid',
        description: 'Uniform grid layout with no variation',

        radiusJitter: 0.0,         // Perfect grid
        angleJitter: 0,

        tierSpacingMult: 0.95,
        spreadMult: 1.0,

        fillPieSlice: true,
        curveEdges: false,
        clusterNodes: false,

        densityMult: 1.0,
        innerDensityBoost: 1.0,
        outerDensityFade: 1.0
    },

    tree: {
        name: 'Natural Tree',
        description: 'Thick visible trunk from root, expanding into wide dense canopy at outer tiers',

        radiusJitter: 0.08,
        angleJitter: 4,            // Moderate — visible trunk width

        tierSpacingMult: 1.1,      // Slightly elongated for trunk visibility
        spreadMult: 1.0,           // Full sector width for wide canopy

        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: false,

        densityMult: 1.0,
        innerDensityBoost: 0.4,    // Moderate trunk density (visible thickness)
        outerDensityFade: 2.0      // Very dense canopy
    },

    swords: {
        name: 'Crossed Swords',
        description: 'Two broad blade wedges with a gap between them, like crossed swords',

        radiusJitter: 0.08,
        angleJitter: 3,

        tierSpacingMult: 1.4,      // Elongated blades reaching outward
        spreadMult: 0.6,           // Moderate width — two blades fill partial sector

        fillPieSlice: false,       // Blades don't fill the whole pie
        curveEdges: false,
        clusterNodes: false,

        densityMult: 0.8,
        innerDensityBoost: 1.5,    // Dense at hilt
        outerDensityFade: 0.5      // Taper at blade tips
    },

    portals: {
        name: 'Portal Doorway',
        description: 'Organic fill with a huge arched doorway hole in the center, like a conjuration portal',

        radiusJitter: 0.15,
        angleJitter: 10,

        tierSpacingMult: 0.85,     // Compact — dense fill with holes
        spreadMult: 0.95,          // Nearly full sector (holes remove density)

        fillPieSlice: true,
        curveEdges: true,
        clusterNodes: false,

        densityMult: 1.2,          // Dense base (holes remove nodes, so start dense)
        innerDensityBoost: 1.0,
        outerDensityFade: 0.9
    },

    explosion: {
        name: 'Fire Explosion',
        description: 'Dense core bursting outward into scattered flames — like a fireball detonating',

        radiusJitter: 0.22,        // High — irregular flame edges
        angleJitter: 12,           // Moderate — flames spread around

        tierSpacingMult: 1.2,      // Slightly elongated — flames reach outward
        spreadMult: 0.3,           // Narrow at core (mask handles the blast expansion)

        fillPieSlice: false,       // Irregular explosion shape
        curveEdges: false,
        clusterNodes: false,

        densityMult: 1.3,          // Dense core
        innerDensityBoost: 2.5,    // VERY dense packed center (fireball core)
        outerDensityFade: 0.35     // Sparse trailing flames/debris at edges
    }
};

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Get a shape profile by name
 * @param {string} shapeName
 * @returns {Object} - Shape profile or organic as default
 */
function getShapeProfile(shapeName) {
    return SHAPE_PROFILES[shapeName] || SHAPE_PROFILES.organic;
}

// =============================================================================
// EXPORTS
// =============================================================================

window.SHAPE_PROFILES = SHAPE_PROFILES;
window.getShapeProfile = getShapeProfile;

console.log('[ShapeProfiles] Module loaded with', Object.keys(SHAPE_PROFILES).length, 'shapes');
