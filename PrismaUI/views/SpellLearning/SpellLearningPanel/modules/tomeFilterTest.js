/**
 * TomeFilterTest - node tests for the tree's tome filter (run-tests.js).
 *
 * The scan keeps every spell for the librarian and the perk adapters and marks
 * taughtByTome and voiceSlot; the tree and the primed count take only spells a
 * tome teaches that are not in the voice slot. A scan from before the marks has
 * no fields and keeps all its spells.
 *
 * Depends on: proceduralTreeBuilder.js (isTaughtByTome, filterTomeSpells) and
 * uiHelpers.js (getPrimedSpells) - under node this file runs them into the
 * global scope the way the page's script tags do. Swaps the globals `state` and
 * `settings` for its own while it runs.
 */

var TomeFilterTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['proceduralTreeBuilder.js', 'filterTomeSpells'], ['uiHelpers.js', 'getPrimedSpells']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    _ids: function(spells) {
        return spells.map(function(s) { return s.formId; }).join(',');
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.filterTomeSpells !== 'function' || typeof g.getPrimedSpells !== 'function') {
            this.check(false, 'proceduralTreeBuilder.js and uiHelpers.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldState = g.state, oldSettings = g.settings;
        try {
            var spells = [
                { formId: '0x01', plugin: 'Skyrim.esm', school: 'Destruction', taughtByTome: true },
                { formId: '0x02', plugin: 'Skyrim.esm', school: 'Destruction', taughtByTome: false },
                { formId: '0x03', plugin: 'Mod.esp', school: 'Alteration' },
                { formId: '0x05', plugin: 'Mod.esp', school: 'Alteration', taughtByTome: true, voiceSlot: true }
            ];
            this.check(this._ids(g.filterTomeSpells(spells)) === '0x01,0x03',
                'the tree drops spells no tome teaches and voice slot spells, keeps a scan without the marks');
            this.check(g.filterTomeSpells([{ formId: '0x04' }]).length === 1,
                'a scan from before the mark keeps every spell');

            g.state = { lastSpellData: { spells: spells } };
            g.settings = { spellBlacklist: [], pluginWhitelist: [] };
            this.check(this._ids(g.getPrimedSpells()) === '0x01,0x03',
                'the primed count leaves out spells no tome teaches');
        } finally {
            g.state = oldState;
            g.settings = oldSettings;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TomeFilterTest = TomeFilterTest;
if (typeof global !== 'undefined') global.TomeFilterTest = TomeFilterTest;
