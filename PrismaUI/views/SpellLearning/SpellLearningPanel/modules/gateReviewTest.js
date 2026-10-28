/**
 * GateReviewTest - node tests for the spell gate's review list (run-tests.js).
 *
 * Tome spells marked gate.band "review" go to one list, "out" ones to the other
 * (an included one leaves it and comes back into the tree); unmarked spells are not
 * listed, nor a blacklisted one (by plugin:localFormId or formId) or one the
 * player kept; reasons read in the panel's language with an English fallback.
 *
 * Depends on: gateReview.js - under node this file runs it (and proceduralTreeBuilder.js
 * for isTaughtByTome) into the global scope the way the page's script tags do.
 */

var GateReviewTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['proceduralTreeBuilder.js', 'isTaughtByTome'], ['gateReview.js', 'GateReview']];
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
        if (typeof g.GateReview === 'undefined') {
            this.check(false, 'gateReview.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var oldLocal = g.getLocalFormId;
        g.getLocalFormId = function(id) { return String(id).slice(-6).toLowerCase(); };
        try {
            var R = g.GateReview;
            var spells = [
                { formId: '0x01000801', plugin: 'Mounts.esp', taughtByTome: true, gate: { band: 'review', reasons: ['horseSummon'] } },
                { formId: '0x01000802', plugin: 'Mounts.esp', taughtByTome: true, gate: { band: 'out', reasons: ['longSummon'] } },
                { formId: '0x01000803', plugin: 'Magic.esp', taughtByTome: true },
                { formId: '0x01000804', plugin: 'Tools.esp', taughtByTome: true, gate: { band: 'review', reasons: ['cheapScript'] } },
                { formId: '0x01000805', plugin: 'Tools.esp', taughtByTome: true, gate: { band: 'review', reasons: ['uniqueSummon'] } },
                { formId: '0x01000806', plugin: 'Tools.esp', taughtByTome: false, gate: { band: 'review', reasons: ['cheapScript'] } }
            ];
            // The panel hands pending the tree candidates (getPrimedSpells(isTreeCandidate))
            var cands = spells.filter(g.isTreeCandidate);
            var ids = function(list) { return list.map(function(s) { return s.formId.slice(-1); }).join(','); };
            this.check(ids(R.pending(cands, [], [])) === '1,4,5', 'review spells only, in the tree\'s reach');
            this.check(ids(R.pending(cands, [{ plugin: 'Tools.esp', localFormId: '000804' }], [])) === '1,5',
                'a blacklisted spell (plugin:localFormId) leaves the list');
            this.check(ids(R.pending(cands, [{ formId: '0x01000805' }], ['mounts.esp:000801'])) === '4',
                'a blacklisted formId and a kept spell leave the list');
            this.check(g.isTaughtByTome(spells[1]) === false && g.isTaughtByTome(spells[0]) === true,
                'the tree takes review spells and leaves gate "out" spells');
            this.check(ids(R.pending(cands, [], [], 'out')) === '2', 'the "left out" list takes the out spells');
            var oldSettings = g.settings;
            g.settings = { gateReviewed: ['mounts.esp:000802'] };
            try {
                this.check(g.isTaughtByTome(spells[1]) === true && R.pending(cands, [], g.settings.gateReviewed, 'out').length === 0,
                    'an included out spell comes back into the tree and leaves the list');
                this.check(g.isTaughtByTome({ formId: '0x01000809', plugin: 'X.esp', freeSpell: true }) === false,
                    'a scan from before the gate: freeSpell still leaves a spell out');
                // The tree build's plugin and spell filters (also the panel's primed list, and the DLL's for Spell Research)
                g.settings = {
                    pluginWhitelist: [{ plugin: 'A.esp', enabled: true }, { plugin: 'B.esp', enabled: false }],
                    spellBlacklist: [{ formId: '0x02000801', plugin: 'D.esp', localFormId: '000801' }, { formId: '0x05000999' }]
                };
                var plug = function(list) { return list.map(function(s) { return s.plugin; }).join(','); };
                this.check(plug(g.filterWhitelistedSpells([{ plugin: 'A.esp' }, { plugin: 'B.esp' }, { plugin: 'New.esp' }])) === 'A.esp,New.esp',
                    'plugin list: only a plugin turned off leaves; one installed after the list was saved comes in');
                this.check(plug(g.filterBlacklistedSpells([{ plugin: 'D.esp', formId: '0x07000801' }, { plugin: 'E.esp', formId: '0x02000801' },
                        { plugin: 'F.esp', formId: '0x05000999' }])) === 'E.esp',
                    'blacklist: plugin and local id survive a load order change; an old formId-only entry still matches');
            } finally {
                g.settings = oldSettings;
            }
            this._checkQuestOnly(g, R);
            this._checkPluginKeys(g, R);
            this._checkMigration(g, R);
            this.check(['free', 'banishOnly', 'longSummon', 'horseSummon', 'uniqueSummon', 'nonHostileSummon', 'cheapScript']
                .every(function(r) { return R.reasonText(r) !== r; }), 'every reason the DLL gives has words');
            this.check(R.reasonText('rule:adultFramework').length > 0 && R.reasonText('somethingNew') === 'somethingNew',
                'reasons read in words; an unknown one as it is');
        } finally {
            g.getLocalFormId = oldLocal;
        }
        return { passed: this.passed, failed: this.failed };
    },

    /** A fake element for _renderList: children, text, click handlers */
    _fakeElement: function() {
        return {
            children: [], textContent: '', className: '', _click: null,
            appendChild: function(c) { this.children.push(c); },
            addEventListener: function(type, fn) { if (type === 'click') this._click = fn; },
            set innerHTML(v) { this.children = []; }
        };
    },

    /** Spells whose tomes only a quest hands out (scan field questOnly): out of the tree, listed, Include puts one back */
    _checkQuestOnly: function(g, R) {
        var ids = function(list) { return list.map(function(s) { return s.formId.slice(-2); }).join(','); };
        var q1 = { formId: '0x01000811', plugin: 'Quests.esp', taughtByTome: true, questOnly: true };
        var q2 = { formId: '0x01000812', plugin: 'Quests.esp', taughtByTome: true, questOnly: true,
            gate: { band: 'review', reasons: ['cheapScript'] } };
        var plain = { formId: '0x01000813', plugin: 'Old.esp', taughtByTome: true };
        var hasList = typeof R.questOnly === 'function';
        var oldSettings = g.settings, oldRender = R.render, oldKeep = R.keep, oldSave = g.autoSaveSettings,
            oldCount = g.updatePrimedCount, oldDoc = g.document;
        try {
            g.settings = { gateReviewed: [] };
            this.check(!g.isTreeCandidate(q1) && !g.isTaughtByTome(q1) && !g.isTaughtByTome(q2) &&
                hasList && ids(R.questOnly([q1, q2, plain], [], [])) === '11,12',
                'quest-only: left out of the tree');
            R.render = function() {};
            g.autoSaveSettings = function() {};
            g.updatePrimedCount = function() {};
            R.keep(q1);
            this.check(g.isTreeCandidate(q1) && g.isTaughtByTome(q1) && !g.isTaughtByTome(q2) &&
                hasList && ids(R.questOnly([q1, q2], [], g.settings.gateReviewed)) === '12',
                'quest-only: kept (Include) goes back in');
            this.check(hasList && ids(R.questOnly([q1, q2], [{ plugin: 'Quests.esp', localFormId: '000811' }], [])) === '12' &&
                ids(R.questOnly([q1, q2], [{ formId: '0x01000812' }], [])) === '11',
                'quest-only: a blacklisted one is not listed');
            this.check(g.isTreeCandidate(plain) && g.isTaughtByTome(plain) && hasList && R.questOnly([plain], [], []).length === 0,
                'quest-only: a spell without the field is a candidate');
            // The list: a row for a spell the gate never marked; its Include button keeps it
            var els = {}, self = this, kept = [], drew = true;
            g.document = {
                getElementById: function(id) { return els[id] || (els[id] = self._fakeElement()); },
                createElement: function() { return self._fakeElement(); }
            };
            R.keep = function(s) { kept.push(s); };
            try {
                R._renderList('quest-only', [q1], ['modals.gateReview.questEmpty', 'No quest-only spells in the last scan.'],
                    [['modals.gateReview.include', 'Include', function(spell) { R.keep(spell); }]], false);
            } catch (e) { drew = false; }
            var row = drew && els['quest-only-entries'].children[0];
            var button = row && row.children[1];
            if (button && button._click) button._click();
            this.check(!!button && els['quest-only-count'].textContent === 1 &&
                row.children[0].children[1].textContent === 'Quests.esp' &&
                button.textContent === 'Include' && kept.length === 1 && kept[0] === q1,
                'quest-only: the list shows a spell the gate did not mark; Include keeps it');
        } finally {
            R.keep = oldKeep;
            R.render = oldRender;
            g.autoSaveSettings = oldSave;
            g.updatePrimedCount = oldCount;
            g.settings = oldSettings;
            g.document = oldDoc;
        }
    },

    /** The DLL's key for a plugin name (SpellScannerGate.cpp PluginKey): its UTF-8 bytes, A-Z folded */
    _dllFold: function(name) {
        if (typeof Buffer === 'undefined') return null;
        var bytes = Buffer.from(String(name), 'utf8');
        for (var i = 0; i < bytes.length; i++) {
            if (bytes[i] >= 0x41 && bytes[i] <= 0x5A) bytes[i] += 0x20;
        }
        return bytes.toString('utf8');
    },

    /** Non-ASCII plugin names: the panel's key is the DLL's, so an included spell comes back in both */
    _checkPluginKeys: function(g, R) {
        var self = this;
        var names = ['Mounts.esp', '마법 확장.esp', 'Заклинания.ESP', 'ÜberMagic.esp', 'Magie_Élémentaire.esl'];
        var sameAsDll = names.every(function(n) {
            var dll = self._dllFold(n);
            return dll === null || g.foldPluginName(n) === dll;
        });
        this.check(sameAsDll, 'plugin keys: the panel folds A-Z only, the same bytes the DLL makes (Korean, Cyrillic, accented)');
        this.check(g.foldPluginName('Заклинания.ESP') === 'Заклинания.esp' && g.foldPluginName('ÜberMagic.esp') === 'Übermagic.esp',
            'plugin keys: a Cyrillic or accented capital is kept, A-Z is folded');
        var ru = { formId: '0x01000801', plugin: 'Заклинания.ESP', taughtByTome: true, gate: { band: 'out', reasons: ['free'] } };
        var ko = { formId: '0x01000802', plugin: '마법 확장.esp', taughtByTome: true, gate: { band: 'out', reasons: ['free'] } };
        this.check(R.key(ru) === 'Заклинания.esp:000801' && R.key(ko) === '마법 확장.esp:000802',
            'plugin keys: Keep writes the DLL\'s key');
        this.check(R.pending([ru, ko], [], [R.key(ru), R.key(ko)], 'out').length === 0,
            'plugin keys: included spells of non-ASCII plugins leave the "left out" list');
        this.check(R.pending([ru], [], ['заклинания.esp:000801'], 'out').length === 1,
            'plugin keys: a key saved before (toLowerCase) is not the key (migrateKeys replaces it)');
        this.check(R.pending([ru, ko], [{ plugin: 'заклинания.esp', localFormId: '000801' }], [], 'out').length === 2,
            'plugin keys: a blacklist entry folds the same way (a lowercase Cyrillic name is another name)');
        this.check(R.pending([ru], [{ plugin: 'Заклинания.esp', localFormId: '000801' }], [], 'out').length === 0,
            'plugin keys: a blacklist entry differing only in A-Z case matches');
        var oldSettings = g.settings, oldRender = R.render, oldSave = g.autoSaveSettings, oldCount = g.updatePrimedCount;
        try {
            g.settings = { gateReviewed: [] };
            R.render = function() {};
            g.autoSaveSettings = function() {};
            g.updatePrimedCount = function() {};
            R.keep(ru);
            this.check(g.settings.gateReviewed.length === 1 && g.settings.gateReviewed[0] === 'Заклинания.esp:000801',
                'plugin keys: Keep saves the DLL\'s key');
        } finally {
            R.render = oldRender;
            g.autoSaveSettings = oldSave;
            g.updatePrimedCount = oldCount;
            g.settings = oldSettings;
        }
    },

    /** Keys saved with toLowerCase before 2026-10-17 become the DLL's key, once, saved once */
    _checkMigration: function(g, R) {
        var oldSettings = g.settings, oldSave = g.autoSaveSettings, oldReady = R._configReady, oldState = g.state;
        var saves = 0;
        var ru = { formId: '0x01000801', plugin: 'Заклинания.ESP' };
        var de = { formId: '0x01000802', plugin: 'ÜberMagic.esp' };
        var en = { formId: '0x01000803', plugin: 'Mounts.ESP' };
        try {
            g.autoSaveSettings = function() { saves++; };
            g.settings = { gateReviewed: ['заклинания.esp:000801', 'Übermagic.esp:000802', 'mounts.esp:000803', 'übermagic.esp:000802', 'gone.esp:000999'] };
            R._configReady = false;
            this.check(R.migrateKeys([ru, de, en]) === 0 && saves === 0 && g.settings.gateReviewed[0] === 'заклинания.esp:000801',
                'migration: nothing before the config is read');
            R._configReady = true;
            var replaced = R.migrateKeys([ru, de, en]);
            var list = g.settings.gateReviewed;
            this.check(replaced === 1 && saves === 1 && list[0] === 'Заклинания.esp:000801',
                'migration: an old Cyrillic key becomes the DLL\'s key, saved once');
            this.check(list[1] === 'Übermagic.esp:000802' && list[3] === 'übermagic.esp:000802' && list[2] === 'mounts.esp:000803',
                'migration: a spell kept under its new key is untouched (its old key too); an ASCII key is the same key');
            this.check(list[4] === 'gone.esp:000999' && list.length === 5, 'migration: an old key no scanned spell has stays');
            this.check(R.migrateKeys([ru, de, en]) === 0 && saves === 1, 'migration: a second run changes and saves nothing');
            this.check(R.isKept(ru) && R.pending([{ formId: ru.formId, plugin: ru.plugin, gate: { band: 'out' } }], [], list, 'out').length === 0,
                'migration: the migrated spell is kept and leaves the "left out" list');
            g.settings = { gateReviewed: ['заклинания.esp:000801'] };
            g.state = { lastSpellData: { spells: [ru] } };
            R.configLoaded(false);
            this.check(saves === 1 && g.settings.gateReviewed[0] === 'заклинания.esp:000801',
                'migration: a config C++ could not read (configNotRead) migrates nothing');
            R.configLoaded(true);
            this.check(saves === 2 && g.settings.gateReviewed[0] === 'Заклинания.esp:000801',
                'migration: a whole config with a scan here migrates at once');
        } finally {
            g.autoSaveSettings = oldSave;
            g.settings = oldSettings;
            g.state = oldState;
            R._configReady = oldReady;
        }
    }
};

if (typeof window !== 'undefined') window.GateReviewTest = GateReviewTest;
if (typeof global !== 'undefined') global.GateReviewTest = GateReviewTest;
