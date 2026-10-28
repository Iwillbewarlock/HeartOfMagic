/**
 * SchoolBridgesTest - node tests for what a tree takes from the scan as it is applied (run-tests.js).
 *
 * SchoolBridges.applyToOutput bakes each node's persistentId and editorId from the
 * scan (state.lastSpellData): the editor id is how a node whose plugin was renamed is
 * found again on load (SpellScannerFormId.cpp JudgeNode). An empty editor id (no
 * powerofthree's Tweaks) is not baked, and a scan spell with no node bakes nothing.
 *
 * Depends on: schoolBridges.js - under node this file runs it into the global scope
 * the way the page's script tag does.
 */

var SchoolBridgesTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function' || typeof g.SchoolBridges !== 'undefined') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'schoolBridges.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.SchoolBridges === 'undefined') {
            this.check(false, 'schoolBridges.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var hadState = Object.prototype.hasOwnProperty.call(g, 'state');
        var oldState = g.state;
        g.state = { lastSpellData: { spells: [
            { formId: '0x05000800', persistentId: 'Fire.esp|0x000800', editorId: 'FireBoltNew' },
            { formId: '0x05000801', persistentId: 'Fire.esp|0x000801', editorId: '' },
            { formId: '0x05000900', persistentId: 'Fire.esp|0x000900', editorId: 'NotInTree' }
        ] } };
        try {
            var output = { schools: { Destruction: { root: '0x05000800', nodes: [
                { formId: '0x05000800' },
                { formId: '0x05000801' }
            ] } } };
            g.SchoolBridges.applyToOutput(output, { bridges: [] });
            var nodes = output.schools.Destruction.nodes;
            this.check(nodes[0].editorId === 'FireBoltNew' && nodes[0].persistentId === 'Fire.esp|0x000800',
                'applyToOutput bakes editorId');
            this.check(!('editorId' in nodes[1]) && nodes[1].persistentId === 'Fire.esp|0x000801',
                'an empty editor id (no powerofthree\'s Tweaks) is not baked');
            this.check(nodes.length === 2 && JSON.stringify(output).indexOf('NotInTree') < 0,
                'a scan spell with no node bakes nothing');
        } finally {
            if (hadState) g.state = oldState; else delete g.state;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SchoolBridgesTest = SchoolBridgesTest;
if (typeof global !== 'undefined') global.SchoolBridgesTest = SchoolBridgesTest;
