/**
 * SpellLearning Growth DSL Module
 * 
 * Growth recipe vocabulary: volumes, modifiers, constraints, the default
 * recipe per school and parseRecipe. The recipe-to-settings helpers went with
 * the JS tree builders (2026-09-27); the LLM Growth Style Generator that wrote
 * recipes (its panel was not in index.html) with its prompt builder
 * (generateLLMPrompt, getAPIDocumentation) on 2026-09-28.
 */

// =============================================================================
// GROWTH DSL - LLM-Driven Procedural Tree Generation
// =============================================================================

var GROWTH_DSL = {
    // Volume types for bounding shapes
    volumes: {
        cone: {
            name: 'Cone',
            description: 'Conical shape - wide at base, narrow at top',
            params: ['height', 'baseRadius', 'topRadius'],
            defaults: { height: 400, baseRadius: 200, topRadius: 50 }
        },
        cube: {
            name: 'Cube',
            description: 'Rectangular box shape',
            params: ['width', 'height', 'depth'],
            defaults: { width: 300, height: 400, depth: 300 }
        },
        sphere: {
            name: 'Sphere',
            description: 'Spherical shape - uniform in all directions',
            params: ['radius'],
            defaults: { radius: 250 }
        },
        cylinder: {
            name: 'Cylinder',
            description: 'Cylindrical shape - constant width',
            params: ['radius', 'height'],
            defaults: { radius: 150, height: 400 }
        },
        wedge: {
            name: 'Wedge',
            description: 'Pie slice shape - fits within a sector',
            params: ['radius', 'angle'],
            defaults: { radius: 350, angle: 72 }
        }
    },
    
    // Growth style modifiers
    modifiers: {
        spiral: {
            name: 'Spiral',
            description: 'Add rotational twist as depth increases',
            params: ['tightness', 'direction'],
            defaults: { tightness: 0.5, direction: 1 }
        },
        gravity: {
            name: 'Gravity',
            description: 'Pull nodes toward a direction',
            params: ['direction', 'strength'],
            defaults: { direction: 'down', strength: 0.3 }
        },
        attractTo: {
            name: 'Attract To Point',
            description: 'Pull nodes toward a specific point',
            params: ['x', 'y', 'strength'],
            defaults: { x: 0, y: 0, strength: 0.2 }
        },
        repelFrom: {
            name: 'Repel From Point',
            description: 'Push nodes away from a point',
            params: ['x', 'y', 'strength'],
            defaults: { x: 0, y: 0, strength: 0.2 }
        },
        wind: {
            name: 'Wind',
            description: 'Directional displacement',
            params: ['angle', 'intensity'],
            defaults: { angle: 45, intensity: 0.3 }
        },
        taper: {
            name: 'Taper',
            description: 'Reduce spacing as depth increases',
            params: ['startScale', 'endScale'],
            defaults: { startScale: 1.0, endScale: 0.3 }
        }
    },
    
    // Constraint types
    constraints: {
        clampHeight: {
            name: 'Clamp Height',
            description: 'Limit vertical extent',
            params: ['maxHeight'],
            defaults: { maxHeight: 400 }
        },
        constrainToVolume: {
            name: 'Constrain To Volume',
            description: 'Kill branches outside bounding shape',
            params: ['volumeType'],
            defaults: { volumeType: 'cone' }
        },
        forceSymmetry: {
            name: 'Force Symmetry',
            description: 'Mirror nodes across axis',
            params: ['axis'],
            defaults: { axis: 'vertical' }
        },
        minSpacing: {
            name: 'Minimum Spacing',
            description: 'Prevent node overlap',
            params: ['distance'],
            defaults: { distance: 30 }
        }
    },
    
    // Visual options
    visualOptions: {
        nodeShapes: ['circle', 'hexagon', 'diamond', 'pill', 'rectangle'],
        edgeStyles: ['straight', 'curved', 'organic', 'stepped'],
        tierSpacings: ['linear', 'exponential', 'logarithmic', 'fibonacci']
    },
    
    // Branching structure rules (affects tree topology, not just visuals)
    branchingRules: {
        maxChildrenPerNode: {
            name: 'Max Children Per Node',
            description: 'Maximum branches from any single spell',
            range: [1, 5],
            default: 3
        },
        allowCrossTierConnections: {
            name: 'Cross-Tier Connections',
            description: 'Allow spells to connect to non-adjacent tiers',
            default: false
        },
        allowBackwardBranches: {
            name: 'Backward Branches',
            description: 'Higher tier spells can unlock lower tier spells',
            default: false
        },
        clusterSimilarSpells: {
            name: 'Cluster Similar',
            description: 'Group related spell variants together',
            default: true
        },
        fillEmptySpaces: {
            name: 'Fill Empty Spaces',
            description: 'Position nodes to minimize gaps in layout',
            default: true
        },
        preferWideOverDeep: {
            name: 'Wide vs Deep',
            description: 'Favor wide shallow trees over narrow deep ones',
            default: true
        }
    },
    
    // Generate a default recipe for a school
    getDefaultRecipe: function(schoolName) {
        return {
            volume: {
                type: 'wedge',
                radius: 350,
                angle: 72
            },
            growth: {
                style: 'radial',
                tightness: 0.6,
                branchingAngle: 30,
                depthBias: 'center',
                symmetry: 'radial',
                randomness: 0.15
            },
            branching: {
                maxChildrenPerNode: 3,
                allowCrossTierConnections: false,
                allowBackwardBranches: false,
                clusterSimilarSpells: true,
                fillEmptySpaces: true,
                preferWideOverDeep: true
            },
            visual: {
                nodeShape: 'pill',
                edgeStyle: 'curved',
                tierSpacing: 'linear',
                colorGradient: true
            },
            modifiers: [],
            constraints: [
                { type: 'minSpacing', distance: 25 }
            ],
            rationale: 'Default balanced layout'
        };
    },
    
    // Parse and validate a growth recipe
    parseRecipe: function(recipeJson) {
        try {
            var recipe = typeof recipeJson === 'string' ? JSON.parse(recipeJson) : recipeJson;
            
            // Validate required fields
            if (!recipe.volume || !recipe.volume.type) {
                return { valid: false, error: 'Missing volume type' };
            }
            
            if (!this.volumes[recipe.volume.type]) {
                return { valid: false, error: 'Unknown volume type: ' + recipe.volume.type };
            }
            
            // Apply defaults for missing fields
            var defaults = this.getDefaultRecipe('');
            recipe.growth = Object.assign({}, defaults.growth, recipe.growth || {});
            recipe.branching = Object.assign({}, defaults.branching, recipe.branching || {});
            recipe.visual = Object.assign({}, defaults.visual, recipe.visual || {});
            recipe.modifiers = recipe.modifiers || [];
            recipe.constraints = recipe.constraints || [];
            
            return { valid: true, recipe: recipe };
        } catch (e) {
            return { valid: false, error: 'JSON parse error: ' + e.message };
        }
    }
};

// =============================================================================
// EXPORTS
// =============================================================================

window.GROWTH_DSL = GROWTH_DSL;

console.log('[GrowthDSL] Module loaded');
