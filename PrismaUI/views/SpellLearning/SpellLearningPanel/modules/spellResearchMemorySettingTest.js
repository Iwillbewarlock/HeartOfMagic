/**
 * SpellResearchMemorySettingTest - node tests for Settings > Spell Tome Learning >
 * practice and memory with Spell Research (spellResearchMemorySetting.js, run-tests.js).
 *
 * Checks: the defaults (both on, 25 minutes, 25%), a config without the section or
 * with wrong values (clamped 0..240 and 0..100, a wrong type back to the default),
 * save -> load round trip into config.spellResearch.memoryExchange with a hand-set
 * spellResearch.enabled left in place, a number greyed out while its switch is off,
 * a typed number clamped and saved, Reset to Defaults, and the rows shown and hidden
 * with the Spell Research line (spellResearchStatus.js render) - only when its state
 * says memoryReady: true.
 *
 * Depends on: spellResearchMemorySetting.js and spellResearchStatus.js - under node
 * this file runs them into the global scope the way the page's script tags do. Swaps
 * the globals `settings`, `document`, `t` and `autoSaveSettings` for its own while it runs.
 */

var SpellResearchMemorySettingTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [];
        if (typeof g.SpellResearchMemorySetting === 'undefined') files.push('spellResearchMemorySetting.js');
        if (typeof g.SpellResearchStatus === 'undefined') files.push('spellResearchStatus.js');
        for (var i = 0; i < files.length; i++) {
            var file = path.join(__dirname, files[i]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    /** A stand-in element: what the modules read and write, and its listeners. */
    _el: function() {
        var classes = {};
        return {
            checked: false, disabled: false, value: '', textContent: '', style: {},
            _listeners: {},
            addEventListener: function(type, fn) { this._listeners[type] = fn; },
            fire: function(type) { if (this._listeners[type]) this._listeners[type].call(this); },
            classList: {
                toggle: function(name, on) { if (on) classes[name] = true; else delete classes[name]; },
                contains: function(name) { return !!classes[name]; }
            }
        };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SpellResearchMemorySetting === 'undefined') {
            this.check(false, 'spellResearchMemorySetting.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldSettings = g.settings, oldDocument = g.document, oldAutoSave = g.autoSaveSettings, oldT = g.t;
        var self = this;
        try {
            var els = {
                srPracticeToMemoryRow: this._el(),
                srPracticeToMemoryToggle: this._el(),
                srMemoryMinutesRow: this._el(),
                srMemoryMinutesInput: this._el(),
                srMemoryToPracticeRow: this._el(),
                srMemoryToPracticeToggle: this._el(),
                srMaxPracticeBoostRow: this._el(),
                srMaxPracticeBoostInput: this._el(),
                spellResearchStatusRow: this._el(),
                spellResearchStatusText: this._el()
            };
            var saves = 0;
            g.document = { getElementById: function(id) { return els[id] || null; } };
            g.autoSaveSettings = function() { saves++; };
            g.t = function(key) { return key; };
            g.settings = {};
            var SM = g.SpellResearchMemorySetting;

            SM.init();
            var m = g.settings.spellResearchMemory;
            self.check(m.practiceToMemory === true && m.memoryToPractice === true && m.memoryMinutes === 25 &&
                m.maxPracticeBoost === 25, 'no saved settings: both on, 25 minutes, 25%');
            self.check(els.srPracticeToMemoryToggle.checked && els.srMemoryToPracticeToggle.checked &&
                els.srMemoryMinutesInput.value === 25 && els.srMaxPracticeBoostInput.value === 25 &&
                !els.srMemoryMinutesInput.disabled, 'the controls show the defaults');

            // A switch off greys out its number, the number kept
            els.srPracticeToMemoryToggle.checked = false;
            els.srPracticeToMemoryToggle.fire('change');
            self.check(g.settings.spellResearchMemory.practiceToMemory === false && saves === 1 &&
                els.srMemoryMinutesInput.disabled && els.srMemoryMinutesRow.classList.contains('disabled-controls') &&
                !els.srMaxPracticeBoostInput.disabled, 'practice to memory off: saved, its minutes greyed out, the boost not');
            els.srMemoryToPracticeToggle.checked = false;
            els.srMemoryToPracticeToggle.fire('change');
            self.check(els.srMaxPracticeBoostInput.disabled && els.srMaxPracticeBoostRow.classList.contains('disabled-controls'),
                'memory to practice off: the boost greyed out');

            // Typed numbers: clamped, a non-number back to the default
            els.srMemoryMinutesInput.value = '300';
            els.srMemoryMinutesInput.fire('change');
            self.check(g.settings.spellResearchMemory.memoryMinutes === 240 && els.srMemoryMinutesInput.value === 240,
                'minutes over 240 are 240');
            els.srMaxPracticeBoostInput.value = '-5';
            els.srMaxPracticeBoostInput.fire('change');
            self.check(g.settings.spellResearchMemory.maxPracticeBoost === 0, 'a negative boost is 0');
            els.srMaxPracticeBoostInput.value = 'abc';
            els.srMaxPracticeBoostInput.fire('change');
            self.check(g.settings.spellResearchMemory.maxPracticeBoost === 25 && saves === 5,
                'a boost that is not a number is the default, every change saved');

            // Save -> load, spellResearch.enabled set by hand stays
            g.settings.spellResearchMemory.memoryMinutes = 40;
            g.settings.spellResearchMemory.maxPracticeBoost = 10;
            var config = SM.saveTo({ spellResearch: { enabled: false } });
            var ex = config.spellResearch && config.spellResearch.memoryExchange;
            self.check(ex && ex.practiceToMemory === false && ex.memoryMinutes === 40 && ex.memoryToPractice === false &&
                ex.maxPracticeBoost === 10, 'saved into config.spellResearch.memoryExchange');
            self.check(config.spellResearch.enabled === false && Object.keys(config.spellResearch).length === 2,
                'spellResearch.enabled is left as it was, nothing else written beside memoryExchange');
            self.check(SM.saveTo({}).spellResearch.memoryExchange.memoryMinutes === 40, 'a config without the section gets one');
            g.settings = {};
            SM.loadFrom(JSON.parse(JSON.stringify(config)));
            self.check(g.settings.spellResearchMemory.memoryMinutes === 40 && g.settings.spellResearchMemory.practiceToMemory === false &&
                els.srPracticeToMemoryToggle.checked === false && els.srMemoryMinutesInput.value === 40 &&
                els.srMemoryMinutesInput.disabled, 'loaded back with the controls');

            SM.loadFrom({});
            m = g.settings.spellResearchMemory;
            self.check(m.practiceToMemory && m.memoryToPractice && m.memoryMinutes === 25 && m.maxPracticeBoost === 25,
                'a config from before the setting: the defaults');
            SM.loadFrom({ spellResearch: { memoryExchange: { practiceToMemory: 'no', memoryMinutes: 500,
                memoryToPractice: 0, maxPracticeBoost: 150 } } });
            m = g.settings.spellResearchMemory;
            self.check(m.practiceToMemory === true && m.memoryMinutes === 240 && m.memoryToPractice === true &&
                m.maxPracticeBoost === 100, 'wrong types are the defaults, numbers clamped to 240 and 100');
            SM.loadFrom({ spellResearch: { memoryExchange: { memoryMinutes: '30', maxPracticeBoost: null } } });
            self.check(g.settings.spellResearchMemory.memoryMinutes === 25 && g.settings.spellResearchMemory.maxPracticeBoost === 25,
                'a number saved as text or null is the default (as the DLL reads it)');

            // Reset to Defaults
            g.settings.spellResearchMemory = { practiceToMemory: false, memoryMinutes: 5, memoryToPractice: false, maxPracticeBoost: 1 };
            SM.reset();
            m = g.settings.spellResearchMemory;
            self.check(m.practiceToMemory && m.memoryToPractice && m.memoryMinutes === 25 && m.maxPracticeBoost === 25 &&
                !els.srMemoryMinutesInput.disabled, 'Reset to Defaults turns both back on at 25');

            // Shown with the Spell Research line, hidden without it
            if (typeof g.SpellResearchStatus !== 'undefined') {
                var SRS = g.SpellResearchStatus;
                SRS.receive(null);
                self.check(els.srPracticeToMemoryRow.style.display === 'none' && els.srMaxPracticeBoostRow.style.display === 'none',
                    'no Spell Research state: the rows are hidden');
                SRS.receive({ state: 'upToDate', srSpells: 10, time: '12:00', memoryReady: true });
                self.check(els.srPracticeToMemoryRow.style.display === '' && els.srMemoryMinutesRow.style.display === '' &&
                    els.srMemoryToPracticeRow.style.display === '' && els.srMaxPracticeBoostRow.style.display === '' &&
                    els.spellResearchStatusRow.style.display === '', 'a Spell Research state with memoryReady shows the line and the four rows');
                SRS.receive({ state: 'upToDate', srSpells: 10, time: '12:00', memoryReady: false });
                self.check(els.spellResearchStatusRow.style.display === '' && els.srPracticeToMemoryRow.style.display === 'none' &&
                    els.srMaxPracticeBoostRow.style.display === 'none',
                    'memoryReady false (the memory part did not install): the line shows, the rows stay hidden');
                SRS.receive({ state: 'notImported', time: '12:00' });
                self.check(els.srMemoryMinutesRow.style.display === 'none', 'a state without memoryReady hides the rows');
                SRS.receive({ state: 'added', added: 3, srSpells: 13, time: '12:01', memoryReady: true });
                SRS.receive('not json');
                self.check(els.srMemoryMinutesRow.style.display === 'none', 'bad input hides the rows again');
                SRS._last = null;
            } else {
                self.check(false, 'spellResearchStatus.js loaded');
            }
        } finally {
            g.settings = oldSettings;
            g.document = oldDocument;
            g.autoSaveSettings = oldAutoSave;
            g.t = oldT;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof module !== 'undefined') module.exports = SpellResearchMemorySettingTest;
if (typeof window !== 'undefined') window.SpellResearchMemorySettingTest = SpellResearchMemorySettingTest;
