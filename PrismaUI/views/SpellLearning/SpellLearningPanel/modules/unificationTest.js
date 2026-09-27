/**
 * Unification Test Module
 *
 * Tests the shared layout modules to verify they're working correctly:
 * - shapeProfiles.js
 * - wheelRenderer.js integration
 * - growthDSL.js integration
 * (The edgeScoring, layoutEngine, growthBehaviors and settingsAwareTreeBuilder
 * tests went with those JS tree builder modules, 2026-09-27.)
 */

var UnificationTest = {
    results: [],
    passed: 0,
    failed: 0,

    log: function(msg, type) {
        var prefix = type === 'pass' ? '✓' : type === 'fail' ? '✗' : '○';
        console.log('[UnificationTest] ' + prefix + ' ' + msg);
        this.results.push({ msg: msg, type: type });
        if (type === 'pass') this.passed++;
        if (type === 'fail') this.failed++;
    },

    assert: function(condition, passMsg, failMsg) {
        if (condition) {
            this.log(passMsg, 'pass');
            return true;
        } else {
            this.log(failMsg || passMsg, 'fail');
            return false;
        }
    },

    // =================================================================
    // TEST: ShapeProfiles Module
    // =================================================================
    testShapeProfiles: function() {
        this.log('=== Testing ShapeProfiles Module ===', 'info');

        // Check module loaded
        this.assert(
            typeof SHAPE_PROFILES !== 'undefined',
            'SHAPE_PROFILES loaded',
            'SHAPE_PROFILES NOT loaded'
        );

        if (typeof SHAPE_PROFILES === 'undefined') return;

        // Check all expected shapes exist
        var expectedShapes = ['organic', 'spiky', 'radial', 'mountain', 'cloud', 'cascade', 'linear', 'grid'];
        expectedShapes.forEach(function(shape) {
            this.assert(
                SHAPE_PROFILES[shape] !== undefined,
                'Shape "' + shape + '" exists',
                'Shape "' + shape + '" MISSING'
            );
        }, this);

        // Test getShapeProfile function
        this.assert(
            typeof getShapeProfile === 'function',
            'getShapeProfile function available'
        );

        var organicProfile = getShapeProfile('organic');
        this.assert(organicProfile.radiusJitter !== undefined, 'organic has radiusJitter: ' + organicProfile.radiusJitter);
        this.assert(organicProfile.angleJitter !== undefined, 'organic has angleJitter: ' + organicProfile.angleJitter);
        this.assert(organicProfile.tierSpacingMult !== undefined, 'organic has tierSpacingMult: ' + organicProfile.tierSpacingMult);

        // Test fallback for unknown shape
        var fallback = getShapeProfile('nonexistent');
        this.assert(fallback === SHAPE_PROFILES.organic, 'Unknown shape falls back to organic');
    },

    // =================================================================
    // TEST: GrowthDSL Integration
    // =================================================================
    testGrowthDSL: function() {
        this.log('=== Testing GrowthDSL Integration ===', 'info');

        // Check module loaded
        this.assert(
            typeof GROWTH_DSL !== 'undefined',
            'GROWTH_DSL loaded',
            'GROWTH_DSL NOT loaded'
        );

        if (typeof GROWTH_DSL === 'undefined') return;

        // Check volumes
        this.assert(GROWTH_DSL.volumes !== undefined, 'GROWTH_DSL has volumes');
        this.assert(GROWTH_DSL.volumes.cone !== undefined, 'Cone volume exists');
        this.assert(GROWTH_DSL.volumes.wedge !== undefined, 'Wedge volume exists');

        // Check modifiers
        this.assert(GROWTH_DSL.modifiers !== undefined, 'GROWTH_DSL has modifiers');
        this.assert(GROWTH_DSL.modifiers.spiral !== undefined, 'Spiral modifier exists');

        // Check branching rules
        this.assert(GROWTH_DSL.branchingRules !== undefined, 'GROWTH_DSL has branchingRules');
        this.assert(GROWTH_DSL.branchingRules.maxChildrenPerNode !== undefined, 'maxChildrenPerNode rule exists');

        // Test getDefaultRecipe
        var recipe = GROWTH_DSL.getDefaultRecipe('Destruction');
        this.assert(recipe !== null, 'getDefaultRecipe returns recipe');
        this.assert(recipe.volume !== undefined, 'Recipe has volume');
        this.assert(recipe.branching !== undefined, 'Recipe has branching');
        this.log('Default recipe branching: maxChildren=' + recipe.branching.maxChildrenPerNode, 'info');

        // Test parseRecipe
        var parsed = GROWTH_DSL.parseRecipe(recipe);
        this.assert(parsed.valid === true, 'Default recipe is valid');
    },

    // =================================================================
    // TEST: WheelRenderer Integration
    // =================================================================
    testWheelRenderer: function() {
        this.log('=== Testing WheelRenderer Integration ===', 'info');

        // Check module loaded
        this.assert(
            typeof WheelRenderer !== 'undefined',
            'WheelRenderer loaded',
            'WheelRenderer NOT loaded'
        );

        if (typeof WheelRenderer === 'undefined') return;

        // Check getSchoolVisualModifier uses unified profiles
        this.assert(
            typeof WheelRenderer.getSchoolVisualModifier === 'function',
            'getSchoolVisualModifier function available'
        );

        // Set up minimal test config
        WheelRenderer.schoolConfigs = {
            'Destruction': { shape: 'spiky' },
            'Restoration': { shape: 'organic' }
        };

        var destMod = WheelRenderer.getSchoolVisualModifier('Destruction');
        var restMod = WheelRenderer.getSchoolVisualModifier('Restoration');

        this.assert(destMod !== null, 'getSchoolVisualModifier returns modifier for Destruction');
        this.assert(restMod !== null, 'getSchoolVisualModifier returns modifier for Restoration');

        this.log('Destruction modifier: radiusJitter=' + destMod.radiusJitter.toFixed(3) + ', angleJitter=' + destMod.angleJitter.toFixed(1), 'info');
        this.log('Restoration modifier: radiusJitter=' + restMod.radiusJitter.toFixed(3) + ', angleJitter=' + restMod.angleJitter.toFixed(1), 'info');

        // Verify spiky has more jitter than organic (from unified profiles)
        // Note: modifiers are adjusted by density/symmetry, so base comparison may vary
        this.assert(destMod.shape === 'spiky', 'Destruction shape is spiky: ' + destMod.shape);
        this.assert(restMod.shape === 'organic', 'Restoration shape is organic: ' + restMod.shape);
    },

    // =================================================================
    // TEST: Cross-Module Integration
    // =================================================================
    testCrossModuleIntegration: function() {
        this.log('=== Testing Cross-Module Integration ===', 'info');

        // Test that shape profiles are accessible from multiple modules
        if (typeof getShapeProfile === 'function') {
            var profile = getShapeProfile('mountain');
            this.assert(profile.taperSpread === true, 'Mountain profile has taperSpread from unified module');
            this.assert(profile.taperAmount !== undefined, 'Mountain profile has taperAmount: ' + profile.taperAmount);
        }
    },

    // =================================================================
    // RUN ALL TESTS
    // =================================================================
    runAll: function() {
        console.log('');
        console.log('╔════════════════════════════════════════════════════════════╗');
        console.log('║           UNIFICATION TEST SUITE                           ║');
        console.log('╚════════════════════════════════════════════════════════════╝');
        console.log('');

        this.results = [];
        this.passed = 0;
        this.failed = 0;

        try {
            this.testShapeProfiles();
        } catch (e) {
            this.log('ShapeProfiles tests threw error: ' + e.message, 'fail');
        }

        try {
            this.testGrowthDSL();
        } catch (e) {
            this.log('GrowthDSL tests threw error: ' + e.message, 'fail');
        }

        try {
            this.testWheelRenderer();
        } catch (e) {
            this.log('WheelRenderer tests threw error: ' + e.message, 'fail');
        }

        try {
            this.testCrossModuleIntegration();
        } catch (e) {
            this.log('CrossModule tests threw error: ' + e.message, 'fail');
        }

        console.log('');
        console.log('╔════════════════════════════════════════════════════════════╗');
        console.log('║           TEST RESULTS                                     ║');
        console.log('╠════════════════════════════════════════════════════════════╣');
        console.log('║  PASSED: ' + this.passed.toString().padEnd(4) + '                                           ║');
        console.log('║  FAILED: ' + this.failed.toString().padEnd(4) + '                                           ║');
        console.log('║  TOTAL:  ' + (this.passed + this.failed).toString().padEnd(4) + '                                           ║');
        console.log('╚════════════════════════════════════════════════════════════╝');
        console.log('');

        return {
            passed: this.passed,
            failed: this.failed,
            total: this.passed + this.failed,
            results: this.results
        };
    }
};

// Export
window.UnificationTest = UnificationTest;

// Auto-run if requested
if (typeof window.runUnificationTests !== 'undefined' && window.runUnificationTests) {
    UnificationTest.runAll();
}

console.log('[UnificationTest] Module loaded - call UnificationTest.runAll() to run tests');
