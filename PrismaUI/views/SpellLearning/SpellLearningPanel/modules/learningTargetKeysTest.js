/**
 * LearningTargetKeysTest - node tests for finding a learning target by spell
 * rather than by school key (learningTargetKeys.js, run-tests.js).
 *
 * Checks: the key holding a spell when it is the node's school and when it is
 * another one (a save from before kept a multi-school spell under its costliest
 * effect's school), the canonical id, the preferred key first, no match; the
 * dropped-key callback forgetting only a key that still holds that spell; and
 * that the Learn button and the spell card look the spell up this way and send
 * its formId with the clear.
 *
 * Depends on: learningTargetKeys.js - under node this file runs it into the
 * global scope the way the page's script tag does. Swaps the global `state`
 * for its own while it runs.
 */

var LearningTargetKeysTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(name) {
        var fs = require('fs'), path = require('path');
        return fs.readFileSync(path.join(__dirname, name), 'utf8');
    },

    _load: function(g) {
        if (typeof g.LearningTargetKeys !== 'undefined' || typeof require !== 'function') return;
        var path = require('path');
        require('vm').runInThisContext(this._file('learningTargetKeys.js'),
            { filename: path.join(__dirname, 'learningTargetKeys.js') });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.LearningTargetKeys === 'undefined') {
            this.check(false, 'learningTargetKeys.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var K = g.LearningTargetKeys;
        var oldState = g.state;
        try {
            // A multi-school spell: first effect Alteration (its tree node's school),
            // costliest effect Destruction (the key a tome used before)
            var targets = { Destruction: '0x0001A001', Illusion: '0x0002B002' };
            this.check(K.schoolOf(targets, '0x0001A001', null, 'Alteration') === 'Destruction',
                'a spell held under another school key than its node has is found');
            this.check(K.schoolOf(targets, '0x0002B002', null, 'Illusion') === 'Illusion', 'its own school key');
            this.check(K.schoolOf(targets, '0x0003C003', '0x0002B002', 'Illusion') === 'Illusion', 'by the canonical id');
            this.check(K.schoolOf({ A: '0x1', B: '0x1' }, '0x1', null, 'B') === 'B', 'the node school key first');
            this.check(K.schoolOf(targets, '0x9', null, 'Alteration') === null && K.schoolOf(null, '0x1') === null &&
                K.schoolOf(targets, '') === null, 'no key holds it: null');

            g.state = { learningTargets: { Destruction: '0x0001A001', Alteration: '0x0005E005' } };
            g.onLearningTargetDropped(JSON.stringify({ school: 'Destruction', formId: '0x0001A001' }));
            this.check(!g.state.learningTargets.hasOwnProperty('Destruction'), 'a dropped key is forgotten');
            g.onLearningTargetDropped(JSON.stringify({ school: 'Alteration', formId: '0x0001A001' }));
            this.check(g.state.learningTargets.Alteration === '0x0005E005', 'a key holding another spell stays');
            var oldError = console.error;
            console.error = function() {};
            try { g.onLearningTargetDropped('not json'); } finally { console.error = oldError; }
            this.check(g.state.learningTargets.Alteration === '0x0005E005', 'a bad message changes nothing');

            // The callers: by id, and the clear carries the formId (source check)
            var learn = this._file('progressionUI.js');
            this.check(/LearningTargetKeys\.schoolOf\(state\.learningTargets, node\.formId/.test(learn) &&
                /'ClearLearningTarget', JSON\.stringify\(\{ school: heldSchool, formId: node\.formId \}\)/.test(learn),
                'the Learn button finds the key by id and sends the formId with the clear');
            this.check(/LearningTargetKeys\.schoolOf\(state\.learningTargets, node\.formId, canonId/.test(this._file('spellProgressView.js')),
                'the spell card finds the target by id');
        } finally {
            g.state = oldState;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.LearningTargetKeysTest = LearningTargetKeysTest;
if (typeof global !== 'undefined') global.LearningTargetKeysTest = LearningTargetKeysTest;
