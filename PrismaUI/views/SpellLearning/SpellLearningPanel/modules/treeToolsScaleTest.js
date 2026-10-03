/**
 * TreeToolsScaleTest - node tests for the tree tool bar's size (treeToolsScale.js, run-tests.js).
 *
 * Checks: the clamp (1x - 3x, steps of 0.05, anything that is not a number gives
 * 1x), the scale a drag gives (bigger right/down, smaller left/up, within the
 * clamp), save -> load round trip, a config without the key, Reset to Defaults,
 * and that only a finished change saves.
 *
 * Depends on: treeToolsScale.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the globals `settings` and
 * `autoSaveSettings` for its own while it runs (document.querySelector is null
 * under node, so apply() has no bar to change).
 */

var TreeToolsScaleTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof g.TreeToolsScale !== 'undefined' || typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'treeToolsScale.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.TreeToolsScale === 'undefined') {
            this.check(false, 'treeToolsScale.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldSettings = g.settings, oldSave = g.autoSaveSettings;
        var T = g.TreeToolsScale;
        try {
            var saves = 0;
            g.autoSaveSettings = function() { saves++; };
            g.settings = {};

            this.check(T.clamp(0.5) === 1 && T.clamp(7) === 3 && T.clamp(2) === 2, 'clamped to 1x - 3x');
            this.check(T.clamp(1.234) === 1.25 && T.clamp(2.97) === 2.95, 'steps of 0.05');
            this.check(T.clamp(undefined) === 1 && T.clamp(null) === 1 && T.clamp('x') === 1 &&
                T.clamp(true) === 1 && T.clamp(NaN) === 1, 'not a number: 1x');
            this.check(T.clamp('1.5') === 1.5, 'a number saved as text still reads');

            this.check(T.scaleForDrag(1, 200, 40, 200, 40) === 2, 'dragging the corner by the bar\'s size doubles it');
            this.check(T.scaleForDrag(2, 400, 80, -200, -40) === 1, 'dragging back halves it');
            this.check(T.scaleForDrag(1, 200, 40, -500, -500) === 1 && T.scaleForDrag(1, 200, 40, 5000, 5000) === 3,
                'a drag stays within 1x - 3x');
            this.check(T.scaleForDrag(1, 200, 40, 0, 0) === 1, 'no move, no change');

            T.set(2.5, false);
            this.check(g.settings.treeToolsScale === 2.5 && saves === 0, 'a change during a drag does not save');
            T.set(2.5, true);
            this.check(saves === 1, 'a finished drag saves');
            var config = T.saveTo({});
            this.check(config.treeToolsScale === 2.5, 'saved into the config');
            g.settings = {};
            T.loadFrom(JSON.parse(JSON.stringify(config)));
            this.check(g.settings.treeToolsScale === 2.5, 'loaded back');
            T.loadFrom({});
            this.check(g.settings.treeToolsScale === 1, 'a config from before: 1x');
            T.loadFrom({ treeToolsScale: 9 });
            this.check(g.settings.treeToolsScale === 3, 'a saved value out of range is clamped');
            T.reset();
            this.check(g.settings.treeToolsScale === 1 && T.current() === 1, 'Reset to Defaults: 1x');
        } finally {
            g.settings = oldSettings;
            g.autoSaveSettings = oldSave;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TreeToolsScaleTest = TreeToolsScaleTest;
if (typeof global !== 'undefined') global.TreeToolsScaleTest = TreeToolsScaleTest;
