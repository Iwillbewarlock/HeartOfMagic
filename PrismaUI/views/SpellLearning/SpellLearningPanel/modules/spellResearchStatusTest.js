/**
 * SpellResearchStatusTest - node tests for Settings > Spell Tome Learning >
 * "Spell Research" (spellResearchStatus.js, run-tests.js).
 *
 * Checks: the row stays hidden until the plugin sends a state, each state's
 * line with its numbers (JSON text or an object), one spell added in its own
 * line ("1 spell", not "1 spells"; a language without that line uses its plural
 * line before English), the English text when the
 * language has no key, an unknown state or bad JSON hiding the row again, and
 * window.onSpellResearchStatus reaching the module.
 *
 * Depends on: spellResearchStatus.js - under node this file runs it into the
 * global scope the way the page's script tag does. Swaps the globals
 * `document` and `t` for its own while it runs.
 */

var SpellResearchStatusTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof g.SpellResearchStatus !== 'undefined' || typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'spellResearchStatus.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SpellResearchStatus === 'undefined') {
            this.check(false, 'spellResearchStatus.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldDocument = g.document, oldT = g.t;
        try {
            var row = { style: { display: 'none' } }, out = { textContent: '' };
            g.document = {
                getElementById: function(id) {
                    return id === 'spellResearchStatusRow' ? row : (id === 'spellResearchStatusText' ? out : null);
                }
            };
            // A language with one key (and, when oneKey, the one-spell key; with noKeys, none):
            // the others fall back to English
            var oneKey = false, noKeys = false;
            g.t = function(key, params) {
                if (oneKey && key === 'settings.tomeLearning.srAddedOne') return params.time + ' - one/' + params.total;
                if (noKeys || key !== 'settings.tomeLearning.srAdded') return key;
                return params.time + ' - ' + params.added + '/' + params.total;
            };
            var S = g.SpellResearchStatus;
            S._last = null;

            S.render();
            this.check(row.style.display === 'none', 'hidden before the plugin says anything (no Spell Research)');

            S.receive('{"state":"added","added":1408,"srSpells":1530,"time":"20:26","reason":"import"}');
            this.check(row.style.display === '' && out.textContent === '20:26 - 1408/1530',
                'added: shown in the language, with its numbers (JSON text)');

            S.receive({ state: 'added', added: 2, srSpells: 1530, time: '20:30' });
            this.check(out.textContent === '20:30 - 2/1530', 'two spells added: the plural line');
            S.receive({ state: 'added', added: 1, srSpells: 1530, time: '20:31' });
            this.check(out.textContent === '20:31 - 1/1530',
                'one spell added, the language lacks the one-spell key: its plural line, not English');
            oneKey = true;
            S.receive({ state: 'added', added: 1, srSpells: 1530, time: '20:32' });
            this.check(out.textContent === '20:32 - one/1530', 'one spell added: the language\'s one-spell key');
            oneKey = false;
            noKeys = true;
            S.receive({ state: 'added', added: 1, srSpells: 1530, time: '20:33' });
            this.check(out.textContent === '1 spell added at 20:33 (1530 spells in Spell Research).',
                'one spell added, the language has neither key: English "1 spell"');
            noKeys = false;

            S.receive({ state: 'upToDate', srSpells: 1530, time: '21:02' });
            this.check(out.textContent === 'All tome spells are in Spell Research (1530 spells, checked at 21:02).',
                'up to date: the English text when the language lacks the key (object)');

            S.receive({ state: 'notImported' });
            this.check(out.textContent.indexOf('Import Spells') >= 0, 'not imported: tells where to press');

            S.receive({ state: 'something' });
            this.check(row.style.display === 'none', 'an unknown state hides the row');
            S.receive('{bad');
            this.check(row.style.display === 'none', 'bad JSON hides the row');

            g.onSpellResearchStatus('{"state":"noCatalog"}');
            this.check(row.style.display === '' && out.textContent.indexOf('scan') >= 0,
                'window.onSpellResearchStatus reaches the module');
        } finally {
            g.document = oldDocument;
            g.t = oldT;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof module !== 'undefined') module.exports = SpellResearchStatusTest;
if (typeof window !== 'undefined') window.SpellResearchStatusTest = SpellResearchStatusTest;
