/**
 * StableIdsTest - node tests for saved settings that name spells (run-tests.js).
 *
 * A runtime formId's top byte is the plugin's place in the load order. After a
 * plugin is inserted before it, a saved formId names another plugin's record,
 * so chosen roots go to the builder by plugin + localFormId and XP overrides
 * are keyed by persistentId. Each case here is that shift: GolemMod.esp moved
 * from 0x88 to 0x89, and 0x88 is now NewMod.esp.
 *
 * Depends on: uiHelpers.js (findScanSpellForEntry, getXPOverride, setXPOverride),
 * proceduralTreeBuilder.js (spellPluginName) and treePreview.js
 * (_flattenSelectedRoots) - under node this file runs them into the global
 * scope the way the page's script tags do. Swaps the globals `state`,
 * `settings` and `xpOverrides` while it runs.
 */

var StableIdsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['proceduralTreeBuilder.js', 'spellPluginName'], ['uiHelpers.js', 'findScanSpellForEntry'],
            ['treePreview.js', 'TreePreview']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.getXPOverride !== 'function' || typeof g.TreePreview === 'undefined' ||
                typeof g.TreePreview._flattenSelectedRoots !== 'function') {
            this.check(false, 'uiHelpers.js and treePreview.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldState = g.state, oldSettings = g.settings, oldOverrides = g.xpOverrides;
        try {
            g.state = { lastSpellData: { spells: [
                { formId: '0x89000812', plugin: 'GolemMod.esp', school: 'Conjuration' },
                { formId: '0x88000812', plugin: 'NewMod.esp', school: 'Conjuration' },
                { formId: '0xFE001ABC', persistentId: 'Light.esl|0x000ABC', school: 'Destruction' }
            ] } };
            g.settings = { selectedRoots: {
                // Chosen while GolemMod.esp was 0x88
                'Conjuration:0': { formId: '0x88000812', name: 'Golem', school: 'Conjuration', rootIndex: 0,
                    plugin: 'GolemMod.esp', localFormId: '000812' },
                // Its plugin is gone
                'Alteration:0': { formId: '0x88000900', name: 'Gone', school: 'Alteration', rootIndex: 0,
                    plugin: 'Gone.esp', localFormId: '000900' },
                // A light plugin's spell, found through the scan's persistentId plugin
                'Destruction:0': { formId: '0xFE000ABC', name: 'Spark', school: 'Destruction', rootIndex: 0,
                    plugin: 'light.esl', localFormId: 'abc' },
                // Saved before roots carried plugin + localFormId
                'Restoration:0': { formId: '0x01000123', name: 'Old', school: 'Restoration', rootIndex: 0 }
            } };

            var flat = g.TreePreview._flattenSelectedRoots();
            this.check(flat.Conjuration && flat.Conjuration.formId === '0x89000812',
                'a chosen root is sent with its formId after a load order shift');
            this.check(!flat.Alteration && !flat._allRoots.Alteration,
                'a chosen root whose spell is not in the scan is left out (no crash)');
            this.check(flat.Destruction && flat.Destruction.formId === '0xFE001ABC',
                'a light plugin root is found by its persistentId plugin');
            this.check(flat.Restoration && flat.Restoration.formId === '0x01000123',
                'a legacy root without plugin + localFormId is sent as saved');
            this.check(flat._allRoots.Conjuration[0].formId === '0x89000812',
                '_allRoots carries the current formId too');
            this.check(g.settings.selectedRoots['Conjuration:0'].formId === '0x88000812',
                'the saved root entry itself is not changed');

            g.state = { lastSpellData: null };
            flat = g.TreePreview._flattenSelectedRoots();
            this.check(flat.Conjuration && flat.Conjuration.formId === '0x88000812' && flat.Alteration,
                'with no scan the roots go as saved');

            // XP overrides by persistentId
            var golem = { formId: '0x89000812', persistentId: 'GolemMod.esp|0x000812' };
            var newSpell = { formId: '0x88000812', persistentId: 'NewMod.esp|0x000812' };
            g.xpOverrides = { 'GolemMod.esp|0x000812': 500 };
            this.check(g.getXPOverride(golem) === 500, 'an override is found by persistentId');
            this.check(g.getXPOverride(newSpell) === undefined,
                'a persistentId override does not follow the old formId to another spell');

            g.xpOverrides = { '0x89000812': 300 };
            this.check(g.getXPOverride(golem) === 300, 'a legacy formId-keyed override is still read');
            g.setXPOverride(golem, 700);
            this.check(g.xpOverrides['GolemMod.esp|0x000812'] === 700 && g.xpOverrides['0x89000812'] === undefined &&
                g.getXPOverride(golem) === 700, 'a new override is saved under the persistentId, replacing the formId key');

            var noPid = { formId: '0x01000123', persistentId: null };
            g.setXPOverride(noPid, 42);
            this.check(g.xpOverrides['0x01000123'] === 42 && g.getXPOverride(noPid) === 42,
                'a node without a persistentId keeps the formId key');
            this.check(g.getXPOverride(null) === undefined, 'no node, no override');
        } catch (e) {
            this.check(false, 'threw: ' + e.message);
        } finally {
            g.state = oldState;
            g.settings = oldSettings;
            g.xpOverrides = oldOverrides;
        }

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.StableIdsTest = StableIdsTest;
if (typeof global !== 'undefined') global.StableIdsTest = StableIdsTest;
