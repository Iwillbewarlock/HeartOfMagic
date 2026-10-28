/**
 * SpellTreeSaveTest - node tests for what SpellTreeSave.send hands C++ (run-tests.js).
 *
 * A build's save (Apply, the build at game start) carries "seenSpells": the
 * persistentId of every spell of the scan the build used, kept or left out
 * (C++ moves it into spell_tree_loadorder.json and writes the tree without it);
 * the tree object itself is not changed. An edit mode save carries none, and an
 * Import goes to its own listener (C++ clears the seen list there).
 *
 * Depends on: spellTreeSave.js - under node this file runs it into the global scope
 * the way the page's script tags do.
 */

var SpellTreeSaveTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function' || typeof g.SpellTreeSave !== 'undefined') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'spellTreeSave.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    _tree: function() {
        return { schools: { Destruction: { root: '0x01', nodes: [{ formId: '0x01', persistentId: 'A.esp|0x000801' }] } } };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SpellTreeSave === 'undefined') {
            this.check(false, 'spellTreeSave.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var S = g.SpellTreeSave;
        var sent = [];
        var names = ['callCpp', 'state', 'AutoBuild', 'setTreeStatusKey'], old = {}, had = {};
        for (var i = 0; i < names.length; i++) {
            had[names[i]] = Object.prototype.hasOwnProperty.call(g, names[i]);
            old[names[i]] = g[names[i]];
        }
        g.callCpp = function(name, text) { sent.push({ name: name, text: text }); };
        g.AutoBuild = undefined;
        g.setTreeStatusKey = function() {};
        g.state = { lastSpellData: { spells: [
            { formId: '0x01000801', persistentId: 'A.esp|0x000801' },   // in the tree
            { formId: '0x01000802', persistentId: 'A.esp|0x000802' },   // left out by the build: seen all the same
            { formId: '0x01000803' },                                   // no persistentId: not listed
            { formId: '0x02000804', persistentId: 'B.esp|0x000804' }
        ] } };
        try {
            var tree = this._tree();
            var ok = S.send(tree, 'apply');
            var payload = sent.length === 1 ? JSON.parse(sent[0].text) : null;
            this.check(ok && sent[0].name === 'SaveBuiltSpellTree' && payload &&
                JSON.stringify(payload.seenSpells) === '["A.esp|0x000801","A.esp|0x000802","B.esp|0x000804"]',
                'apply: payload has seenSpells of every scanned spell with a persistentId');
            this.check(payload && payload.schools && payload.schools.Destruction.nodes.length === 1 && !('seenSpells' in tree),
                'apply: the tree goes along whole, and the tree object gets no seenSpells');

            sent.length = 0;
            S.send(this._tree(), 'auto build');
            payload = sent.length === 1 ? JSON.parse(sent[0].text) : null;
            this.check(payload && sent[0].name === 'SaveBuiltSpellTree' && Array.isArray(payload.seenSpells) && payload.seenSpells.length === 3,
                'auto build: the same seenSpells as an Apply');

            // Build, a rescan, then Apply: the list kept with the draft, not the newer scan's
            sent.length = 0;
            var draftSeen = S.seenSpells({ spells: [{ persistentId: 'A.esp|0x000801' }, { persistentId: 'A.esp|0x000809' }] });
            S.send(this._tree(), 'apply', undefined, draftSeen);
            payload = sent.length === 1 ? JSON.parse(sent[0].text) : null;
            this.check(payload && JSON.stringify(payload.seenSpells) === '["A.esp|0x000801","A.esp|0x000809"]',
                'apply after a rescan: the seen list of the scan the draft was built from, not state.lastSpellData');

            sent.length = 0;
            g.state = { lastSpellData: null };
            S.send(this._tree(), 'apply');
            payload = sent.length === 1 ? JSON.parse(sent[0].text) : null;
            this.check(payload && Array.isArray(payload.seenSpells) && payload.seenSpells.length === 0,
                'apply with no scan data: seenSpells present and empty');

            sent.length = 0;
            g.state = { lastSpellData: { spells: [{ formId: '0x01', persistentId: 'A.esp|0x000801' }] } };
            S.send(this._tree(), 'edit mode save');
            var text = JSON.stringify(this._tree());
            S.send(this._tree(), 'edit mode save', text);
            this.check(sent.length === 2 && sent[0].name === 'SaveSpellTree' && sent[0].text.indexOf('seenSpells') === -1 &&
                sent[1].text === text, 'edit save: no seenSpells');

            sent.length = 0;
            S.send(this._tree(), 'import');
            S.send(this._tree(), 'orphan repair');
            this.check(sent.length === 2 && sent[0].name === 'SaveImportedSpellTree' && sent[0].text.indexOf('seenSpells') === -1 &&
                sent[1].name === 'SaveSpellTree' && sent[1].text.indexOf('seenSpells') === -1,
                'import: its own listener (C++ clears the seen list), no seenSpells; a repair: SaveSpellTree');
        } finally {
            for (var j = 0; j < names.length; j++) {
                if (had[names[j]]) g[names[j]] = old[names[j]]; else delete g[names[j]];
            }
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SpellTreeSaveTest = SpellTreeSaveTest;
if (typeof global !== 'undefined') global.SpellTreeSaveTest = SpellTreeSaveTest;
