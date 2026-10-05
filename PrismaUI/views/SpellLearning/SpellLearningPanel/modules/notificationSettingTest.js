/**
 * NotificationSettingTest - node tests for Settings > Show Notifications and the
 * weakened spell alerts under it (notificationSetting.js, run-tests.js).
 *
 * Checks: the defaults (all on, 10 s), a config without the section or with
 * wrong values, save -> load round trip into config.notifications, Reset to
 * Defaults, the weakened alert toggle and interval slider greyed out (disabled)
 * while Show Notifications is off and back on with it, the interval row hidden
 * with the alerts off, Notification Language (panel by default, English kept
 * through save and load, unknown values back to panel, greyed out with the rest).
 *
 * Depends on: notificationSetting.js - under node this file runs it into the
 * global scope the way the page's script tag does. Swaps the globals `settings`,
 * `document` and `autoSaveSettings` for its own while it runs.
 */

var NotificationSettingTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof g.NotificationSetting !== 'undefined' || typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var file = path.join(__dirname, 'notificationSetting.js');
        vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
    },

    /** A stand-in element: what the module reads and writes, and its listeners. */
    _el: function() {
        var classes = {};
        return {
            checked: false, disabled: false, value: '', textContent: '', style: {},
            _listeners: {},
            addEventListener: function(type, fn) { this._listeners[type] = fn; },
            fire: function(type) { if (this._listeners[type]) this._listeners[type].call(this); },
            classList: {
                toggle: function(name, on) { if (on) classes[name] = true; else delete classes[name]; },
                contains: function(name) { return !!classes[name]; }
            }
        };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.NotificationSetting === 'undefined') {
            this.check(false, 'notificationSetting.js loaded');
            return { passed: this.passed, failed: this.failed };
        }

        var oldSettings = g.settings, oldDocument = g.document, oldAutoSave = g.autoSaveSettings;
        var self = this;
        try {
            var els = {
                showNotificationsToggle: this._el(),
                weakenedNotificationsToggle: this._el(),
                notificationIntervalSlider: this._el(),
                notificationIntervalValue: this._el(),
                notificationIntervalRow: this._el(),
                weakenedNotificationsRow: this._el(),
                notificationLanguageSelect: this._el(),
                notificationLanguageRow: this._el()
            };
            var saves = 0;
            g.document = { getElementById: function(id) { return els[id] || null; } };
            g.autoSaveSettings = function() { saves++; };
            g.settings = {};
            var NS = g.NotificationSetting;

            NS.init();
            var n = g.settings.notifications;
            self.check(n.enabled === true && n.weakenedSpellNotifications === true && n.weakenedSpellInterval === 10,
                'no saved settings: all on, every 10 s');
            self.check(els.showNotificationsToggle.checked && els.weakenedNotificationsToggle.checked &&
                !els.weakenedNotificationsToggle.disabled && els.notificationIntervalValue.textContent === '10s',
                'the controls show the defaults');

            // Master off: the sub-option stays as chosen but is greyed out
            els.showNotificationsToggle.checked = false;
            els.showNotificationsToggle.fire('change');
            self.check(g.settings.notifications.enabled === false && saves === 1, 'turning Show Notifications off saves it');
            self.check(els.weakenedNotificationsToggle.disabled && els.notificationIntervalSlider.disabled &&
                els.weakenedNotificationsRow.classList.contains('disabled-controls'),
                'with Show Notifications off the weakened alert controls are disabled');
            self.check(els.weakenedNotificationsToggle.checked === true,
                'the weakened alert choice is kept while greyed out');
            els.showNotificationsToggle.checked = true;
            els.showNotificationsToggle.fire('change');
            self.check(!els.weakenedNotificationsToggle.disabled && !els.notificationIntervalSlider.disabled &&
                !els.weakenedNotificationsRow.classList.contains('disabled-controls'),
                'turning it back on enables them again');

            els.weakenedNotificationsToggle.checked = false;
            els.weakenedNotificationsToggle.fire('change');
            self.check(els.notificationIntervalRow.style.display === 'none', 'alerts off hide the interval');

            // Notification Language
            self.check(g.settings.notifications.language === 'panel' && els.notificationLanguageSelect.value === 'panel',
                'notifications follow the panel language by default');
            var before = saves;
            els.notificationLanguageSelect.value = 'en';
            els.notificationLanguageSelect.fire('change');
            self.check(g.settings.notifications.language === 'en' && saves === before + 1, 'choosing English saves it');
            els.notificationLanguageSelect.value = 'xx';
            els.notificationLanguageSelect.fire('change');
            self.check(g.settings.notifications.language === 'panel', 'an unknown choice falls back to the panel language');
            g.settings.notifications.language = 'en';

            // Save -> load
            g.settings.notifications.enabled = false;
            g.settings.notifications.weakenedSpellInterval = 30;
            var config = NS.saveTo({});
            self.check(config.notifications && config.notifications.enabled === false &&
                config.notifications.weakenedSpellNotifications === false &&
                config.notifications.weakenedSpellInterval === 30 && config.notifications.language === 'en',
                'saved into config.notifications');
            g.settings = {};
            NS.loadFrom(JSON.parse(JSON.stringify(config)));
            self.check(g.settings.notifications.enabled === false && g.settings.notifications.weakenedSpellInterval === 30 &&
                els.showNotificationsToggle.checked === false && els.weakenedNotificationsToggle.disabled &&
                g.settings.notifications.language === 'en' && els.notificationLanguageSelect.value === 'en' &&
                els.notificationLanguageSelect.disabled && els.notificationLanguageRow.classList.contains('disabled-controls'),
                'loaded back with the controls (language greyed out with Show Notifications off)');

            NS.loadFrom({});
            self.check(g.settings.notifications.enabled === true && g.settings.notifications.language === 'panel',
                'a config from before the setting: on, in the panel language');
            NS.loadFrom({ notifications: { enabled: 'x', weakenedSpellInterval: 500 } });
            self.check(g.settings.notifications.enabled === true && g.settings.notifications.weakenedSpellInterval === 10,
                'wrong values fall back to the defaults');

            // Reset to Defaults
            g.settings.notifications = { enabled: false, weakenedSpellNotifications: false, weakenedSpellInterval: 45 };
            NS.reset();
            self.check(g.settings.notifications.enabled === true && g.settings.notifications.weakenedSpellNotifications === true &&
                g.settings.notifications.weakenedSpellInterval === 10 && els.showNotificationsToggle.checked &&
                !els.weakenedNotificationsToggle.disabled, 'Reset to Defaults turns everything back on');
        } finally {
            g.settings = oldSettings;
            g.document = oldDocument;
            g.autoSaveSettings = oldAutoSave;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof module !== 'undefined') module.exports = NotificationSettingTest;
if (typeof window !== 'undefined') window.NotificationSettingTest = NotificationSettingTest;
