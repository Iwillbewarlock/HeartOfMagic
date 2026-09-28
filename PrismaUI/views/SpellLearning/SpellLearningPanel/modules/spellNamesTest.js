/**
 * SpellNamesTest - node tests for the names in the spell card's lists
 * (run-tests.js).
 *
 * A stand-in tree with two spells named 불씨조각 (Flames from Skyrim.esm and
 * NoviceBoltSpells.esp's copy) and one unique name. Checks that a shared name
 * gets its plugin and a unique one does not, that a hidden name shows ??? with
 * no plugin, the plugin from SpellCache first and from persistentId before the
 * spell info is in, that the counts follow a new tree and a renamed spell, and
 * that TreeParser drops a spell listed as its own child or prerequisite.
 *
 * Depends on: spellNames.js (SpellNames, spellDisplayName) and treeParser.js
 * (TreeParser) - under node this file runs them into the global scope the way
 * the page's script tags do. Swaps the globals `state`, `settings`,
 * `SpellCache`, `_findNodeById` and (when missing) `TREE_CONFIG` for its own
 * while it runs.
 */

var SpellNamesTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['spellNames.js', 'SpellNames'], ['treeParser.js', 'TreeParser']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    /** Flames, NoviceBoltSpells' Flames, and Frostbite */
    _tree: function() {
        return { nodes: [
            { id: '0x00012FCD', formId: '0x00012FCD', name: '불씨조각', state: 'available',
              persistentId: 'Skyrim.esm|0x012FCD' },
            { id: '0xFEBD3800', formId: '0xFEBD3800', name: '불씨조각', state: 'locked',
              persistentId: 'NoviceBoltSpells.esp|0x000800' },
            { id: '0x0002B96B', formId: '0x0002B96B', name: '냉기조각', state: 'unlocked',
              persistentId: 'Skyrim.esm|0x02B96B' }
        ] };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (!g.SpellNames || typeof g.spellDisplayName !== 'function') {
            this.check(false, 'spellNames.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var old = { state: g.state, settings: g.settings, SpellCache: g.SpellCache, find: g._findNodeById };
        var cache = {};
        try {
            var tree = this._tree();
            var byId = {};
            tree.nodes.forEach(function(n) { byId[n.id] = n; });
            g.state = { treeData: tree };
            g.settings = { cheatMode: false };
            g.SpellCache = { get: function(id) { return cache[id]; } };
            g._findNodeById = function(id) { return byId[id] || null; };
            g.SpellNames.invalidate();
            this._checkNames(g, tree, byId, cache);
        } finally {
            g.state = old.state;
            g.settings = old.settings;
            g.SpellCache = old.SpellCache;
            g._findNodeById = old.find;
            g.SpellNames.invalidate();
        }
        this._checkParser(g);
        return { passed: this.passed, failed: this.failed };
    },

    _checkNames: function(g, tree, byId, cache) {
        var name = g.spellDisplayName;

        this.check(name('0x00012FCD') === '불씨조각 (Skyrim.esm)',
            'a shared name gets its plugin (persistentId before the spell info is in)');
        this.check(name('0x0002B96B') === '냉기조각', 'a name no other spell has stays as it is');

        this.check(name('0xFEBD3800') === '???', 'a locked spell shows ??? - and no plugin');
        this.check(name('0xFEBD3800', byId['0xFEBD3800'], false) === '???' &&
            name('0x00012FCD', byId['0x00012FCD'], false) === '???',
            'shown=false hides the name and the plugin whatever the state');
        g.settings.cheatMode = true;
        this.check(name('0xFEBD3800') === '불씨조각 (NoviceBoltSpells.esp)',
            'cheat mode names the locked copy, with its plugin');
        g.settings.cheatMode = false;

        cache['0x00012FCD'] = { plugin: 'Update.esm' };
        this.check(name('0x00012FCD') === '불씨조각 (Update.esm)', 'SpellCache\'s plugin comes before the persistentId');
        cache['0x00012FCD'] = { name: '불씨조각' };
        this.check(name('0x00012FCD') === '불씨조각 (Skyrim.esm)', 'spell info without a plugin: back to the persistentId');
        delete cache['0x00012FCD'];

        var noPid = byId['0x00012FCD'].persistentId;
        byId['0x00012FCD'].persistentId = null;
        this.check(name('0x00012FCD') === '불씨조각', 'no plugin known anywhere: the name alone');
        byId['0x00012FCD'].persistentId = noPid;

        this.check(name('0x99999999') === '???' && name('0x99999999', null, true) === '0x99999999',
            'a spell the tree does not have: ??? when hidden, its id when shown');

        // Counts follow the tree: a new tree object, then a renamed spell
        var other = { nodes: [tree.nodes[0], tree.nodes[2]] };
        g.state.treeData = other;
        this.check(name('0x00012FCD') === '불씨조각', 'a new tree without the copy: no plugin');
        g.state.treeData = tree;
        this.check(name('0x00012FCD') === '불씨조각 (Skyrim.esm)', 'back to the first tree: counted again');
        byId['0xFEBD3800'].name = 'Novice Firebolt';
        this.check(name('0x00012FCD') === '불씨조각 (Skyrim.esm)', 'same tree, renamed: counts kept until invalidated');
        g.SpellNames.invalidate();
        this.check(name('0x00012FCD') === '불씨조각', '...and counted again after invalidate()');
        byId['0xFEBD3800'].name = '불씨조각';
        g.SpellNames.invalidate();

        // An edit-mode duplicate of Frostbite is the same spell: no plugin for either
        var dup = { id: '0xDUP00001', formId: '0xDUP00001', originalFormId: '0x0002B96B', name: '냉기조각', state: 'unlocked' };
        tree.nodes.push(dup); byId[dup.id] = dup;
        g.SpellNames.invalidate();
        this.check(name('0x0002B96B') === '냉기조각' && name(dup.id) === '냉기조각',
            'an edit-mode duplicate is counted as its original');
        dup.originalFormId = null; dup.persistentId = 'Other.esp|0x000900';
        g.SpellNames.invalidate();
        this.check(name(dup.id) === '냉기조각 (Other.esp)', '...a different spell of that name is not');
        dup.originalFormId = '0x0002B96B'; dup.persistentId = null; dup.name = '불씨조각';
        g.SpellNames.invalidate();
        this.check(name(dup.id) === '불씨조각 (Skyrim.esm)', 'a duplicate is named by its original\'s plugin');
        tree.nodes.pop(); delete byId[dup.id];
        g.SpellNames.invalidate();

        byId['0x0002B96B'].name = null;
        this.check(name('0x0002B96B') === '0x0002B96B', 'an unnamed spell shows its formId');
        byId['0x0002B96B'].name = '냉기조각';
    },

    /** TreeParser: a spell is never its own child or prerequisite, in any of the four lists */
    _checkParser: function(g) {
        var P = g.TreeParser;
        if (!P || typeof P.parse !== 'function') { this.check(false, 'TreeParser loaded'); return; }
        var oldConfig = g.TREE_CONFIG;
        if (!g.TREE_CONFIG) g.TREE_CONFIG = { layoutStyles: { radial: {} } };
        var oldLog = console.log, oldWarn = console.warn;
        var lines = [];
        var keep = function() { lines.push(Array.prototype.join.call(arguments, ' ')); };
        try {
            var data = function(trust) {
                return { trustPrereqs: trust, schools: { Destruction: { root: 'A', nodes: [
                    { formId: 'A', isRoot: true, children: ['A', 'B'], prerequisites: [] },
                    { formId: 'B', children: ['B'], prerequisites: ['A', 'B'],
                      hardPrereqs: ['A', 'B'], softPrereqs: ['B'], softNeeded: 1,
                      persistentId: 'Mod.esp|0x000800' }
                ] } } };
            };
            for (var t = 0; t < 2; t++) {
                var trust = t === 1;
                var tag = 'TreeParser (' + (trust ? 'trusted' : 'untrusted') + '): ';
                lines = [];
                console.log = keep; console.warn = keep;
                var r = P.parse(data(trust));
                console.log = oldLog; console.warn = oldWarn;
                var a = P.nodes.get('A'), b = P.nodes.get('B');
                var selfEdge = r.edges.some(function(e) { return e.from === e.to; });
                this.check(r.success && a.children.join() === 'B' && b.children.length === 0 && !selfEdge,
                    tag + 'self children dropped, no self edge');
                this.check(b.prerequisites.join() === 'A' && b.hardPrereqs.join() === 'A' && b.softPrereqs.length === 0,
                    tag + 'self id dropped from prerequisites, hardPrereqs and softPrereqs');
                this.check(b.softNeeded === 0, tag + 'softNeeded no larger than the soft list left');
                this.check(!lines.some(function(l) { return /unobtainable/i.test(l); }),
                    tag + 'no "unobtainable spells" warning');
                this.check(b.persistentId === 'Mod.esp|0x000800', tag + 'persistentId kept on the node');
            }
            // A tree saved by the 6316b47 build: every link null
            lines = [];
            console.log = keep; console.warn = keep;
            var nulls = P.parse({ trustPrereqs: true, schools: { Destruction: { root: 'A', nodes: [
                { formId: 'A', isRoot: true, children: [null, 'B', 7], prerequisites: [] },
                { formId: 'B', children: [null], prerequisites: [null, 'A'], hardPrereqs: [null], softPrereqs: [null, 'A'] }
            ] } } });
            console.log = oldLog; console.warn = oldWarn;
            var nb = P.nodes.get('B');
            this.check(nulls.success && !nulls.edges.some(function(e) { return typeof e.to !== 'string' || typeof e.from !== 'string'; }) &&
                P.nodes.get('A').children.join() === 'B' && nb.children.length === 0 &&
                nb.prerequisites.join() === 'A' && nb.hardPrereqs.length === 0 && nb.softPrereqs.join() === 'A',
                'TreeParser: null and other non-string links dropped, no {to: null} edge');
            var same = ['B', 'C'];
            this.check(P.withoutId(null, 'A').length === 0 && P.withoutId(['B'], 'A').join() === 'B' &&
                P.withoutId(same, 'A') === same && P.withoutId([null, 'B'], 'A').join() === 'B' &&
                P.clampSoftNeeded(3, ['x']) === 1 && P.clampSoftNeeded(undefined, []) === 0,
                'withoutId / clampSoftNeeded edge cases');
        } finally {
            console.log = oldLog;
            console.warn = oldWarn;
            g.TREE_CONFIG = oldConfig;
        }
    }
};

if (typeof window !== 'undefined') window.SpellNamesTest = SpellNamesTest;
if (typeof global !== 'undefined') global.SpellNamesTest = SpellNamesTest;
