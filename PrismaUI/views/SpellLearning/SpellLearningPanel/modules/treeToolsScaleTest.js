/**
 * TreeToolsScaleTest - node tests for the tree tool bar's size (treeToolsScale.js, run-tests.js).
 *
 * Checks: the clamp (1x - 3x, steps of 0.05, anything that is not a number gives
 * 1x), the scale a drag gives (bigger right/down, smaller left/up, within the
 * clamp), save -> load round trip, a config without the key, Reset to Defaults,
 * that only a finished change saves, and a drag whose release never comes
 * (a move with no button held, the page losing focus) ending and saving.
 *
 * Depends on: treeToolsScale.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the globals `settings`,
 * `autoSaveSettings`, `document`, `addEventListener` and `removeEventListener`
 * for its own while it runs (the drag test gives document a fake bar).
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

            this._dragTests(g, T, function() { return saves; });
        } finally {
            g.settings = oldSettings;
            g.autoSaveSettings = oldSave;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

/** Drags with fake window listeners and a fake bar: a lost release still ends the drag */
TreeToolsScaleTest._dragTests = function(g, T, saveCount) {
    var oldDocument = g.document, oldAdd = g.addEventListener, oldRemove = g.removeEventListener;
    var listeners = {};
    try {
        g.addEventListener = function(type, fn) { (listeners[type] = listeners[type] || []).push(fn); };
        g.removeEventListener = function(type, fn) {
            listeners[type] = (listeners[type] || []).filter(function(f) { return f !== fn; });
        };
        var bar = { offsetWidth: 200, offsetHeight: 40, style: {}, parentElement: null };
        g.document = { querySelector: function() { return bar; } };
        var fire = function(type, ev) { (listeners[type] || []).slice().forEach(function(fn) { fn(ev); }); };
        var count = function() {
            return (listeners.mousemove || []).length + (listeners.mouseup || []).length + (listeners.blur || []).length;
        };
        var press = { button: 0, buttons: 1, clientX: 100, clientY: 100, preventDefault: function() {}, stopPropagation: function() {} };
        var pressNoButtons = { button: 0, buttons: 0, clientX: 100, clientY: 100, preventDefault: function() {}, stopPropagation: function() {} };
        var move = function(dx, buttons) {
            return { clientX: 100 + dx, clientY: 100, buttons: buttons, preventDefault: function() {} };
        };

        g.settings = { treeToolsScale: 1 };
        var before = saveCount();
        T._startDrag(press);
        this.check(count() === 3, 'a drag listens for move, release and focus loss');
        fire('mousemove', move(200, 1));
        this.check(T.current() > 1 && saveCount() === before, 'moving with the button held resizes, no save yet');
        var sized = T.current();
        fire('mousemove', move(400, 0));
        this.check(T._drag === null && count() === 0 && T.current() === sized && saveCount() === before + 1,
            'a move with no button held (the release was lost) ends the drag and saves');
        fire('mousemove', move(600, 1));
        this.check(T.current() === sized, 'after that the bar no longer follows the pointer');

        T._startDrag(press);
        fire('mousemove', move(-100, 1));
        fire('blur', {});
        this.check(T._drag === null && count() === 0 && saveCount() === before + 2, 'the page losing focus ends the drag');

        T._startDrag(press);
        fire('mousemove', move(50, undefined));
        this.check(T._drag !== null, 'a move with no buttons field: the drag goes on');
        fire('mouseup', { stopPropagation: function() {} });
        this.check(T._drag === null && count() === 0 && saveCount() === before + 3, 'a release ends it as before');

        // An older engine with no buttons field at all: the press reports nothing
        T._startDrag({ button: 0, clientX: 100, clientY: 100, preventDefault: function() {}, stopPropagation: function() {} });
        fire('mousemove', move(100, 0));
        this.check(T._drag !== null, 'a press with no buttons field: a move with buttons 0 does not end the drag');
        fire('mouseup', { stopPropagation: function() {} });
        before = saveCount() - 3;   // that release saved once: the counts below go on from here

        // An engine that sends buttons 0 on every event (Ultralight may): the
        // press reports 0 too, so 0 on a move says nothing about a lost release
        T._startDrag(pressNoButtons);
        fire('mousemove', move(200, 0));
        this.check(T._drag !== null && T.current() > 1 && saveCount() === before + 3,
            'buttons always 0: a move goes on resizing, the drag does not end');
        fire('mousemove', move(300, 0));
        this.check(T._drag !== null, 'buttons always 0: still dragging after more moves');
        fire('mouseup', { stopPropagation: function() {} });
        this.check(T._drag === null && count() === 0 && saveCount() === before + 4, 'buttons always 0: the release ends it');
    } finally {
        g.document = oldDocument;
        g.addEventListener = oldAdd;
        g.removeEventListener = oldRemove;
        T._drag = null;
    }
};

if (typeof window !== 'undefined') window.TreeToolsScaleTest = TreeToolsScaleTest;
if (typeof global !== 'undefined') global.TreeToolsScaleTest = TreeToolsScaleTest;
