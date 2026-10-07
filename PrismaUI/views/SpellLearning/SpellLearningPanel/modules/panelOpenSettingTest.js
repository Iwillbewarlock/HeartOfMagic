/**
 * PanelOpenSettingTest - node tests for Settings > Hotkey: the None button, the
 * letter key warning and the magic menu switches (panelOpenSetting.js, run-tests.js).
 *
 * Checks: None (hotkeyCode 0, hotkey "") survives save -> load (0 is not turned
 * back into F8), None decided by the code alone, wrong codes back to F8, the guard
 * both ways (no hotkey locks the bookmark on, bookmark off locks None out, each
 * with its reason shown), a config with both off loads with the bookmark on, the
 * letter key warning, Reset to Defaults, and that a settings preset holds neither
 * the hotkey nor magicMenu (settingsPresets.js).
 *
 * Depends on: panelOpenSetting.js - under node this file runs it into the global
 * scope the way the page's script tag does. Swaps the globals `settings`,
 * `document` and `autoSaveSettings` for its own while it runs.
 */

var PanelOpenSettingTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof g.PanelOpenSetting !== 'undefined' || typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'panelOpenSetting.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    /** A stand-in element: what the module reads and writes, and its listeners. */
    _el: function() {
        var classes = {};
        return {
            checked: false, disabled: false, value: '',
            _listeners: {},
            addEventListener: function(type, fn) { this._listeners[type] = fn; },
            fire: function(type) { if (this._listeners[type]) this._listeners[type].call(this); },
            setAttribute: function() {}, removeAttribute: function() {},
            classList: {
                add: function(name) { classes[name] = true; },
                remove: function(name) { delete classes[name]; },
                toggle: function(name, on) { if (on) classes[name] = true; else delete classes[name]; },
                contains: function(name) { return !!classes[name]; }
            }
        };
    },

    /** settingsPresets.js keeps the hotkey and magicMenu out of what a preset saves and applies. */
    _presetsLeaveThemOut: function() {
        if (typeof require !== 'function') return true;
        var fs = require('fs'), path = require('path');
        var src = fs.readFileSync(path.join(__dirname, 'settingsPresets.js'), 'utf8');
        var saveStart = src.indexOf('preset.settings = {');
        var saved = saveStart >= 0 ? src.slice(saveStart, src.indexOf('};', saveStart)) : null;
        var applyStart = src.indexOf('var flatKeys = [');
        var applied = applyStart >= 0 ? src.slice(applyStart, src.indexOf('];', applyStart)) : null;
        if (saved === null || applied === null) return false;
        return !/hotkey|magicMenu/.test(saved) && !/hotkey|magicMenu/.test(applied);
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.PanelOpenSetting === 'undefined') {
            this.check(false, 'panelOpenSetting.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldSettings = g.settings, oldDocument = g.document, oldAutoSave = g.autoSaveSettings;
        var self = this;
        try {
            var els = {
                hotkeyInput: this._el(),
                changeHotkeyBtn: this._el(),
                resetHotkeyBtn: this._el(),
                noHotkeyBtn: this._el(),
                magicMenuBookmarkToggle: this._el(),
                magicMenuSpellInfoToggle: this._el(),
                magicMenuBookmarkRow: this._el(),
                hotkeyLetterWarning: this._el(),
                noHotkeyLockedReason: this._el(),
                magicMenuBookmarkLockedReason: this._el()
            };
            var shown = function(id) { return !els[id].classList.contains('hidden'); };
            var saves = 0;
            g.document = {
                getElementById: function(id) { return els[id] || null; },
                addEventListener: function() {}, removeEventListener: function() {}
            };
            g.autoSaveSettings = function() { saves++; };
            g.settings = { hotkey: 'F8', hotkeyCode: 66 };
            var P = g.PanelOpenSetting;

            P.init();
            self.check(g.settings.magicMenu.bookmark === true && g.settings.magicMenu.spellInfo === true &&
                els.magicMenuBookmarkToggle.checked && els.magicMenuSpellInfoToggle.checked &&
                els.hotkeyInput.value === 'F8' && !els.noHotkeyBtn.disabled && !els.magicMenuBookmarkToggle.disabled,
                'defaults: F8, both switches on, nothing locked');

            // None through the button, then save -> load
            els.noHotkeyBtn.fire('click');
            self.check(g.settings.hotkeyCode === 0 && g.settings.hotkey === '' && els.hotkeyInput.value === '' &&
                els.noHotkeyBtn.disabled && saves === 1, 'None: code 0, no name, saved, the button pressed');
            var saved = P.saveTo({});
            self.check(saved.hotkeyCode === 0 && saved.hotkey === '' && saved.magicMenu.bookmark === true,
                'None saved as hotkeyCode 0, hotkey ""');
            g.settings = { hotkey: 'F8', hotkeyCode: 66 };
            P.loadFrom(JSON.parse(JSON.stringify(saved)));
            self.check(g.settings.hotkeyCode === 0 && g.settings.hotkey === '', 'None loads back as None, not F8');
            P.loadFrom({ hotkeyCode: 0, hotkey: 'F8' });
            self.check(P.isNone() && g.settings.hotkey === '', 'code 0 is None whatever the name says');

            // No hotkey: the bookmark is locked on
            self.check(els.magicMenuBookmarkToggle.disabled && els.magicMenuBookmarkRow.classList.contains('disabled-controls') &&
                shown('magicMenuBookmarkLockedReason'), 'no hotkey: the bookmark switch greyed out with its reason');
            self.check(P.setMagicMenu('bookmark', false) === false && g.settings.magicMenu.bookmark === true &&
                els.magicMenuBookmarkToggle.checked, 'no hotkey: the bookmark cannot be turned off');
            self.check(P.setMagicMenu('spellInfo', false) === true && g.settings.magicMenu.spellInfo === false,
                'tags and XP can be turned off without a hotkey');

            // Bookmark off: None is locked out
            P.loadFrom({ hotkey: 'F8', hotkeyCode: 66, magicMenu: { bookmark: false, spellInfo: true } });
            self.check(g.settings.magicMenu.bookmark === false && els.noHotkeyBtn.disabled && shown('noHotkeyLockedReason') &&
                !shown('magicMenuBookmarkLockedReason') && !els.magicMenuBookmarkToggle.disabled,
                'bookmark off: None greyed out with its reason');
            self.check(P.setHotkey('', 0) === false && g.settings.hotkeyCode === 66, 'bookmark off: None refused');
            els.magicMenuBookmarkToggle.checked = true;
            els.magicMenuBookmarkToggle.fire('change');
            self.check(g.settings.magicMenu.bookmark === true && !els.noHotkeyBtn.disabled && !shown('noHotkeyLockedReason'),
                'bookmark back on: None can be chosen again');

            // Both off by hand: the bookmark comes back on
            P.loadFrom({ hotkey: '', hotkeyCode: 0, magicMenu: { bookmark: false, spellInfo: false } });
            self.check(P.isNone() && g.settings.magicMenu.bookmark === true && g.settings.magicMenu.spellInfo === false,
                'no hotkey and the bookmark off (hand-edited): loaded with the bookmark on');

            // Wrong or missing values
            P.loadFrom({});
            self.check(g.settings.hotkeyCode === 66 && g.settings.hotkey === 'F8' && g.settings.magicMenu.bookmark === true &&
                g.settings.magicMenu.spellInfo === true, 'a config from before the settings: F8, both on');
            P.loadFrom({ hotkeyCode: 'F5', hotkey: 'F5' });
            self.check(g.settings.hotkeyCode === 66 && g.settings.hotkey === 'F8', 'a code that is not a number is F8');
            P.loadFrom({ hotkeyCode: -3 });
            self.check(g.settings.hotkeyCode === 66, 'a negative code is F8');

            // Letter keys warn, others do not
            P.setHotkey('K', 37);
            self.check(shown('hotkeyLetterWarning') && g.settings.hotkeyCode === 37, 'a letter key is allowed, with the warning');
            P.setHotkey('F5', 63);
            self.check(!shown('hotkeyLetterWarning'), 'a function key: no warning');
            P.setHotkey('5', 6);
            self.check(!shown('hotkeyLetterWarning'), 'a number key: no warning');
            P.loadFrom({ hotkey: 'G', hotkeyCode: 34 });
            self.check(shown('hotkeyLetterWarning'), 'a letter key loaded from the config shows the warning too');

            // Reset to Defaults
            P.loadFrom({ hotkey: '', hotkeyCode: 0, magicMenu: { bookmark: true, spellInfo: false } });
            P.reset();
            self.check(g.settings.hotkeyCode === 66 && g.settings.hotkey === 'F8' && g.settings.magicMenu.spellInfo === true &&
                els.hotkeyInput.value === 'F8' && !els.magicMenuBookmarkToggle.disabled, 'Reset to Defaults: F8, both on');

            self.check(this._presetsLeaveThemOut(), 'a settings preset never saves or applies the hotkey or magicMenu');
        } finally {
            g.settings = oldSettings;
            g.document = oldDocument;
            g.autoSaveSettings = oldAutoSave;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof module !== 'undefined') module.exports = PanelOpenSettingTest;
if (typeof window !== 'undefined') window.PanelOpenSettingTest = PanelOpenSettingTest;
