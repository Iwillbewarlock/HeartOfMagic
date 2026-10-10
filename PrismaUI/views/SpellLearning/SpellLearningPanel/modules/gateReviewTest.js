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
            this.check(['free', 'banishOnly', 'longSummon', 'horseSummon', 'uniqueSummon', 'nonHostileSummon', 'cheapScript']
                .every(function(r) { return R.reasonText(r) !== r; }), 'every reason the DLL gives has words');
            this.check(R.reasonText('rule:adultFramework').length > 0 && R.reasonText('somethingNew') === 'somethingNew',
                'reasons read in words; an unknown one as it is');
        } finally {
            g.getLocalFormId = oldLocal;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.GateReviewTest = GateReviewTest;
if (typeof global !== 'undefined') global.GateReviewTest = GateReviewTest;
