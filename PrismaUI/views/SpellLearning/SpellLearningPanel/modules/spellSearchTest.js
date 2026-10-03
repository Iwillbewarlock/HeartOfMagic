/**
 * SpellSearchTest - node tests for the Find Spell matcher (spellSearch.js, run-tests.js).
 *
 * Checks: a match on the shown name, school and formId, case and spaces
 * ignored (Korean too), the order (exact, then prefix, then the rest,
 * alphabetical in each), the 80 cap, locked spells left out in discovery mode
 * and listed in cheat mode or with discovery mode off.
 *
 * Depends on: spellSearch.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the global `settings` for
 * its own while it runs.
 */

var SpellSearchTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof g.SpellSearch !== 'undefined' || typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'spellSearch.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    _names: function(hits) {
        return hits.map(function(h) { return h.name; }).join('|');
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SpellSearch === 'undefined') {
            this.check(false, 'spellSearch.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldSettings = g.settings;
        var S = g.SpellSearch;
        try {
            g.settings = { discoveryMode: true, cheatMode: false };
            var nodes = [
                { formId: '0x01', name: 'Flames', school: 'Destruction', state: 'unlocked' },
                { formId: '0x02', name: 'Flame Atronach', school: 'Conjuration', state: 'available' },
                { formId: '0x03', name: 'Firebolt', school: 'Destruction', state: 'learning' },
                { formId: '0x04', name: '헝거 소환', school: 'Conjuration', state: 'available' },
                { formId: '0x05', name: 'Secret Fire', school: 'Destruction', state: 'locked' },
                { formId: '0x06', name: 'Incinerate', school: 'Destruction', state: 'available' }
            ];

            this.check(this._names(S.search(nodes, 'atronach')) === 'Flame Atronach', 'the shown name matches');
            this.check(this._names(S.search(nodes, 'FIREBOLT')) === 'Firebolt', 'case is ignored');
            this.check(this._names(S.search(nodes, '헝거소환')) === '헝거 소환' &&
                this._names(S.search(nodes, '헝거 소환')) === '헝거 소환', 'spaces are ignored (Korean as typed)');
            this.check(this._names(S.search(nodes, 'flame atro')) === 'Flame Atronach', 'spaces in the text are ignored too');
            this.check(this._names(S.search(nodes, 'conjuration')) === 'Flame Atronach|헝거 소환', 'the school matches');
            this.check(this._names(S.search(nodes, '0x06')) === 'Incinerate', 'the formId matches');

            // Order: exact, prefix, the rest; alphabetical within each
            this.check(this._names(S.search(nodes, 'flames')) === 'Flames', 'an exact match');
            this.check(this._names(S.search(nodes, 'fl')) === 'Flame Atronach|Flames', 'prefix matches alphabetical');
            var nodes2 = [
                { formId: '0x21', name: 'Wall of Flames', school: 'Destruction', state: 'available' },
                { formId: '0x22', name: 'Flames', school: 'Destruction', state: 'available' },
                { formId: '0x23', name: 'Flame Cloak', school: 'Destruction', state: 'available' },
                { formId: '0x24', name: 'Aflame', school: 'Destruction', state: 'available' },
                { formId: '0x25', name: 'Flame Thrall', school: 'Conjuration', state: 'available' }
            ];
            this.check(this._names(S.search(nodes2, 'flames')) === 'Flames|Wall of Flames',
                'exact first, then the rest');
            this.check(this._names(S.search(nodes2, 'flame')) === 'Flame Cloak|Flame Thrall|Flames|Aflame|Wall of Flames',
                'prefix before the rest, alphabetical within each');

            // Locked spells
            this.check(S.search(nodes, 'secret').length === 0 && this._names(S.search(nodes, 'fire')) === 'Firebolt',
                'in discovery mode a locked spell is not found');
            this.check(this._names(S.search(nodes, '')).indexOf('Secret Fire') === -1, 'nor listed with an empty text');
            g.settings.cheatMode = true;
            this.check(this._names(S.search(nodes, 'secret')) === 'Secret Fire', 'cheat mode lists locked spells');
            g.settings = { discoveryMode: false, cheatMode: false };
            this.check(this._names(S.search(nodes, 'secret')) === 'Secret Fire', 'with discovery mode off locked spells are listed');
            g.settings = { discoveryMode: true, cheatMode: false };

            this.check(this._names(S.search(nodes, '')) === 'Firebolt|Flame Atronach|Flames|Incinerate|헝거 소환',
                'an empty text lists every spell alphabetical');
            var many = [];
            for (var i = 0; i < 120; i++) many.push({ formId: '0x' + (1000 + i), name: 'Spell ' + i, state: 'available' });
            this.check(S.search(many, 'spell').length === 80 && S.search(many, '').length === 80, 'at most 80');
            this.check(S.search(nodes, 'nothing like it').length === 0, 'no match, nothing');
        } finally {
            g.settings = oldSettings;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SpellSearchTest = SpellSearchTest;
if (typeof global !== 'undefined') global.SpellSearchTest = SpellSearchTest;
