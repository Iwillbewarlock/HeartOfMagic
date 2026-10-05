/**
 * SpellCopiesTest - node tests for merging copies of one spell (run-tests.js).
 *
 * Copies are records with the same name (case ignored), school, Magicka cost and
 * effects (editor id, magnitude, duration, area); the effect's form does not
 * count. The first in scan order stays (a later copy the player knows instead,
 * when the first is not known), the others are listed on it. Spells that
 * only share a name, and spells with no effects listed, stay apart. The Classic
 * setting mergeCopies turns it off.
 *
 * Depends on: spellCopies.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the global TreeGrowthClassic
 * for its own while it runs.
 */

var SpellCopiesTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function' || typeof g.SpellCopies !== 'undefined') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'spellCopies.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    _spell: function(formId, name, opts) {
        opts = opts || {};
        return {
            formId: formId, name: name, school: opts.school || 'Conjuration',
            magickaCost: opts.cost !== undefined ? opts.cost : 120,
            effects: opts.effects || [{ editorId: 'HungerSummon', form: opts.form || 'A.esp|0x10',
                                        magnitude: 0, duration: 60, area: 0 }]
        };
    },

    _ids: function(spells) {
        return spells.map(function(s) { return s.formId; }).join(',');
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SpellCopies === 'undefined') {
            this.check(false, 'spellCopies.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var S = g.SpellCopies;

        // Three Conjure Hunger: the mod's own plugin, a merge and a copy with another effect form
        var spells = [
            this._spell('0x01', 'Conjure Hunger'),
            this._spell('0x02', 'Flames', { school: 'Destruction', cost: 14 }),
            this._spell('0x03', 'conjure hunger', { form: 'Merged.esp|0x99' }),
            this._spell('0x04', 'Conjure Hunger', { form: 'B.esp|0x20' })
        ];
        var merged = S.merge(spells);
        this.check(this._ids(merged.spells) === '0x01,0x02', 'Copies: one Conjure Hunger stays, the first in scan order');
        this.check(merged.copies['0x01'] && merged.copies['0x01'].join(',') === '0x03,0x04',
            'Copies: the others are listed on it');

        var learned = S.merge(spells, new Set(['0x04']));
        this.check(this._ids(learned.spells) === '0x04,0x02' && learned.copies['0x04'].join(',') === '0x01,0x03',
            'A later copy the player knows stays in the tree, in the first copy place');
        var both = S.merge(spells, new Set(['0x01', '0x04']));
        this.check(this._ids(both.spells) === '0x01,0x02', 'The first copy known too: it stays');

        var oldState = g.state;
        try {
            g.state = { playerKnownSpells: new Set(), spellProgress: { '0x03': { xp: 12, unlocked: false } } };
            var started = S.forTree(spells);
            this.check(this._ids(started.spells) === '0x03,0x02', 'A later copy with XP stays in the tree');
            g.state = { playerKnownSpells: new Set(['0x04']), spellProgress: { '0x01': { xp: 30, unlocked: false } } };
            this.check(this._ids(S.forTree(spells).spells) === '0x04,0x02', 'A known copy wins over one with XP only');
        } finally {
            g.state = oldState;
        }

        var apart = S.merge([
            this._spell('0x10', 'Fireball', { school: 'Destruction', cost: 133 }),
            this._spell('0x11', 'Fireball', { school: 'Destruction', cost: 160 }),
            this._spell('0x12', 'Fireball', { school: 'Destruction', cost: 133,
                effects: [{ editorId: 'FireDamage', magnitude: 60, duration: 0, area: 15 }] }),
            this._spell('0x13', 'Fireball', { school: 'Alteration', cost: 133 })
        ]);
        this.check(apart.spells.length === 4, 'Same name, other cost, effects or school: kept apart');

        var noEffects = S.merge([{ formId: '0x20', name: 'Old', school: 'Illusion' },
                                 { formId: '0x21', name: 'Old', school: 'Illusion' }]);
        this.check(noEffects.spells.length === 2, 'No effects listed (an old scan): never merged');

        var twoEffects = S.merge([
            this._spell('0x30', 'Mix', { effects: [{ editorId: 'A', magnitude: 1 }, { editorId: 'B', magnitude: 2 }] }),
            this._spell('0x31', 'Mix', { effects: [{ editorId: 'B', magnitude: 2 }, { editorId: 'A', magnitude: 1 }] })
        ]);
        this.check(twoEffects.spells.length === 1, 'Effects in another order: still copies');

        var oldClassic = g.TreeGrowthClassic;
        try {
            g.TreeGrowthClassic = { settings: { mergeCopies: false } };
            var off = S.forTree(spells);
            this.check(off.spells.length === 4 && Object.keys(off.copies).length === 0, 'Setting off: nothing merged');
            g.TreeGrowthClassic = { settings: {} };
            this.check(S.forTree(spells).spells.length === 2, 'Setting unset: merged (on by default)');
        } finally {
            g.TreeGrowthClassic = oldClassic;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SpellCopiesTest = SpellCopiesTest;
if (typeof global !== 'undefined') global.SpellCopiesTest = SpellCopiesTest;
