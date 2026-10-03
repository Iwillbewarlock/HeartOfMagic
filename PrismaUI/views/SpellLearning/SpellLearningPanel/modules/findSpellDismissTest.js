/**
 * FindSpellDismissTest - node tests for when the Find Spell dropdown goes
 * away by itself (findSpell.js, run-tests.js).
 *
 * Checks: dismissFindSpell hides a shown dropdown (and the button's mark),
 * leaves a hidden one alone, and works whatever tab is in front;
 * isFindSpellOpen is false for a dropdown left behind another tab, so Escape
 * is not used up by it; and the four places that must dismiss it do (the tab
 * switch, the close button, the panel hiding, a tree load - source check).
 *
 * Depends on: findSpell.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the globals `document` and
 * `state` for small fakes while it runs.
 */

var FindSpellDismissTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(rel) {
        var fs = require('fs'), path = require('path');
        return fs.readFileSync(path.join(__dirname, rel), 'utf8');
    },

    _load: function(g) {
        if (typeof g.dismissFindSpell === 'function' || typeof require !== 'function') return;
        var path = require('path');
        require('vm').runInThisContext(this._file('findSpell.js'), { filename: path.join(__dirname, 'findSpell.js') });
    },

    /** An element with a class list, enough for findSpell.js */
    _el: function(classes) {
        var set = {};
        (classes || []).forEach(function(c) { set[c] = true; });
        return {
            classList: {
                contains: function(c) { return !!set[c]; },
                add: function(c) { set[c] = true; },
                remove: function(c) { delete set[c]; }
            },
            blur: function() {}
        };
    },

    /** A named function's text, roughly: from its header to the next top-level function or window.* */
    _body: function(text, header) {
        var at = text.indexOf(header);
        if (at === -1) return '';
        var ends = [text.indexOf('\nfunction ', at + header.length), text.indexOf('\nwindow.', at + header.length)]
            .filter(function(i) { return i !== -1; });
        return text.substring(at, ends.length ? Math.min.apply(null, ends) : text.length);
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.dismissFindSpell !== 'function') {
            this.check(false, 'findSpell.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var oldDocument = g.document, oldState = g.state;
        try {
            var panel = this._el([]), btn = this._el(['active']), input = this._el([]);
            var els = { 'find-spell-panel': panel, 'find-spell-btn': btn, 'find-spell-search': input };
            g.document = { getElementById: function(id) { return els[id] || null; }, activeElement: null };
            g.state = { currentTab: 'spellTree' };

            this.check(g.isFindSpellOpen() === true, 'shown on the tree tab: open');
            g.state.currentTab = 'settings';
            this.check(g.isFindSpellOpen() === false, 'left behind another tab: not open, Escape goes on');
            g.dismissFindSpell();
            this.check(panel.classList.contains('hidden') && !btn.classList.contains('active'),
                'dismiss hides it (and the button mark) from any tab');
            g.state.currentTab = 'spellTree';
            this.check(g.isFindSpellOpen() === false, 'still hidden back on the tree tab');
            g.dismissFindSpell();
            this.check(panel.classList.contains('hidden'), 'a hidden dropdown stays hidden');

            var callsDismiss = /dismissFindSpell\(\)/;
            this.check(callsDismiss.test(this._body(this._file('../script.js'), 'function switchTab(')), 'a tab switch dismisses it');
            this.check(callsDismiss.test(this._body(this._file('buttonHandlers.js'), 'function onCloseClick(')), 'the close button dismisses it');
            this.check(callsDismiss.test(this._body(this._file('cppCallbacks.js'), 'window.onPanelHiding = function(')),
                'the panel hiding (hotkey too) dismisses it');
            this.check(callsDismiss.test(this._body(this._file('treeLoad.js'), 'function loadTreeData(')), 'a tree load dismisses it');
        } finally {
            g.document = oldDocument;
            g.state = oldState;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.FindSpellDismissTest = FindSpellDismissTest;
if (typeof global !== 'undefined') global.FindSpellDismissTest = FindSpellDismissTest;
