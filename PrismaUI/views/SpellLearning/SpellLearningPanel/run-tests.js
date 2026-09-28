/**
 * Node.js Test Runner for Unification Tests
 *
 * Run with: node run-tests.js
 */

// Mock browser globals
global.window = global;
global.document = {
    getElementById: function() { return null; },
    querySelector: function() { return null; },
    createElementNS: function() { return { setAttribute: function() {} }; }
};
global.console = console;

// Mock settings
global.settings = {
    schoolColors: {},
    schoolVisibility: {},
    schoolConfigs: {}
};

// Mock callCpp
global.callCpp = function(method, data) {
    return null;
};

// Track loaded modules; a module that fails to load fails the run
var loadedModules = [];
var failedModules = [];
// Every module asked for, loaded or not: the test suites are read from it (SUITES below)
var requestedModules = [];

/**
 * @param {string} name
 * @param {string} path
 * @param {boolean} [asScript] run in the global scope, as a browser <script> does, so its
 *   top-level vars are globals (constants.js: state.js reads DEFAULT_TREE_RULES)
 */
function loadModule(name, path, asScript) {
    requestedModules.push(name);
    try {
        if (asScript) {
            require('vm').runInThisContext(require('fs').readFileSync(require('path').join(__dirname, path), 'utf8'), { filename: path });
        } else {
            require(path);
        }
        loadedModules.push(name);
        console.log('✓ Loaded: ' + name);
    } catch (e) {
        failedModules.push(name);
        console.log('✗ Failed to load ' + name + ': ' + e.message);
    }
}

console.log('');
console.log('╔════════════════════════════════════════════════════════════╗');
console.log('║     SpellLearning Module Loader                            ║');
console.log('╚════════════════════════════════════════════════════════════╝');
console.log('');

// Load modules in order
loadModule('constants', './modules/constants.js', true);
loadModule('state', './modules/state.js');
loadModule('config', './modules/config.js');
loadModule('shapeProfiles', './modules/shapeProfiles.js');
loadModule('layoutLineClear', './modules/layoutLineClear.js');
loadModule('layoutLineGrid', './modules/layoutLineGrid.js');
loadModule('layoutDeclutter', './modules/layoutDeclutter.js');
loadModule('layoutDeclutterTest', './modules/layoutDeclutterTest.js');
loadModule('openRefreshGate', './modules/openRefreshGate.js');
loadModule('openRefreshGateTest', './modules/openRefreshGateTest.js');
loadModule('layerScroll', './modules/layerScroll.js');
loadModule('layerBuild', './modules/layerBuild.js');
loadModule('layerScrollTest', './modules/layerScrollTest.js');
loadModule('canvasRendererV2', './modules/canvasRendererV2.js');
loadModule('canvasRendererData', './modules/canvasRendererData.js');
loadModule('canvasRendererEdges', './modules/canvasRendererEdges.js');
loadModule('canvasRendererFrame', './modules/canvasRendererFrame.js');
loadModule('canvasCullTest', './modules/canvasCullTest.js');
loadModule('layerFlowTest', './modules/layerFlowTest.js');
loadModule('pageBuildTest', './modules/pageBuildTest.js');
loadModule('treeGrowthStatus', './modules/treeGrowthStatus.js');
loadModule('statusLineTest', './modules/statusLineTest.js');
loadModule('spellNamesTest', './modules/spellNamesTest.js');

// Mock WheelRenderer minimally
global.WheelRenderer = {
    schoolConfigs: {},
    shapeVisualModifiers: {
        organic: { radiusJitter: 0.2, angleJitter: 12, tierSpacingMult: 0.9, spreadMult: 0.95 },
        spiky: { radiusJitter: 0.35, angleJitter: 20, tierSpacingMult: 1.4, spreadMult: 0.6 }
    },
    getSchoolVisualModifier: function(schoolName) {
        var cfg = this.schoolConfigs[schoolName];
        var shape = cfg ? cfg.shape : 'organic';

        var modifier;
        if (typeof getShapeProfile === 'function') {
            modifier = getShapeProfile(shape);
        } else {
            modifier = this.shapeVisualModifiers[shape] || this.shapeVisualModifiers.organic;
        }

        var density = cfg ? (cfg.density || 0.6) : 0.6;
        var symmetry = cfg ? (cfg.symmetry || 0.3) : 0.3;
        var densityFactor = 1.5 - density;
        var symmetryFactor = 1 - symmetry * 0.8;

        return {
            radiusJitter: modifier.radiusJitter * densityFactor * symmetryFactor,
            angleJitter: modifier.angleJitter * densityFactor * symmetryFactor,
            tierSpacingMult: modifier.tierSpacingMult * (0.6 + density * 0.8),
            spreadMult: modifier.spreadMult * (0.5 + density * 0.7),
            curveEdges: modifier.curveEdges,
            taperSpread: modifier.taperSpread || false,
            shape: shape
        };
    }
};

console.log('');
console.log('Loaded ' + loadedModules.length + ' modules');
console.log('');

// Load and run tests
loadModule('unificationTest', './modules/unificationTest.js');

console.log('');
console.log('Running tests...');
console.log('');

// The suites: every module loaded above whose name ends in "Test" (unificationTest
// runs through runAll instead), as [label, global] - spellNamesTest gives
// ['SpellNames', 'SpellNamesTest']. A new *Test.js needs only its loadModule call.
// Every suite must be there: one that did not load counts as a failure instead of being skipped.
var SUITES = requestedModules.filter(function(name) {
    return /Test$/.test(name) && name !== 'unificationTest';
}).map(function(name) {
    var global_ = name.charAt(0).toUpperCase() + name.slice(1);
    return [global_.slice(0, -'Test'.length), global_];
});

var failed = 0;
if (typeof UnificationTest !== 'undefined') {
    try {
        failed += UnificationTest.runAll().failed;
    } catch (e) {
        console.log('Unification: THREW ' + (e && e.message ? e.message : e));
        failed++;
    }
} else {
    console.log('ERROR: UnificationTest not loaded');
    failed++;
}
for (var s = 0; s < SUITES.length; s++) {
    var suite = global[SUITES[s][1]];
    if (!suite || typeof suite.run !== 'function') {
        console.log(SUITES[s][0] + ': NOT RUN (' + SUITES[s][1] + ' not loaded)');
        failed++;
        continue;
    }
    // A suite that throws fails and the rest still run
    try {
        var r = suite.run();
        failed += r.failed;
        console.log(SUITES[s][0] + ': ' + r.passed + ' passed, ' + r.failed + ' failed');
    } catch (e) {
        console.log(SUITES[s][0] + ': THREW ' + (e && e.message ? e.message : e));
        failed++;
    }
}
if (failedModules.length > 0) {
    console.log('');
    console.log('FAILED TO LOAD: ' + failedModules.join(', '));
}

process.exit(failed > 0 || failedModules.length > 0 ? 1 : 0);
