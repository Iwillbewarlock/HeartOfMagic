/**
 * ConfigSaveTest - node tests for the settings' load and save (run-tests.js)
 *
 * The 2026-10-17 check found three ways a setting was lost: the XP caps were saved
 * but never read back (the next save put 5/15/50 back), a config load that threw
 * part way left the blacklist, whitelist, roots and kept spells empty and the next
 * save wrote that over the file, and an autosave still waiting when the panel
 * closed. Runs settingsPanel.js's own onUnifiedConfigLoaded, saveUnifiedConfig,
 * autoSaveSettings and saveSettings on a fresh `settings` and `state` (state.js
 * run in a context of its own), with the C++ bridge and the timers replaced. Also
 * migrateLoweredWhitelist (plugin list entries an older panel lowered).
 *
 * Depends on: constants.js (global, run-tests.js), state.js, settingsPanel.js, cppStatusKeys.js - under
 * node this file runs settingsPanel.js into the global scope the way the page's
 * script tag does. Swaps `settings`, `state`, `callCpp`, `setTimeout`,
 * `clearTimeout`, DesignPresets, the setting modules and the few page functions
 * onUnifiedConfigLoaded calls (STUBS) for its own while it runs.
 */

var ConfigSaveTest = {

    passed: 0,
    failed: 0,

    // Page functions from other files onUnifiedConfigLoaded calls
    STUBS: ['applyWindowPositionAndSize', 'applyFullscreenState', 'applySchoolColorsToCSS',
            'updateEarlyLearningUI', 'applySavedLanguage'],
    // Modules the load and the save call when they are there; left out here
    ABSENT: ['AutoBuildSetting', 'AutoBuild', 'GateReview', 'PanelOpenSetting', 'ReverseUnlockSetting',
             'DesignEffectsSetting', 'RenderSettings', 'TreeBuilderSetting', 'NotificationSetting',
             'TreeToolsScale', 'CanvasRenderer', 'WheelRenderer'],

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return false;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['settingsPanel.js', 'noteConfigLoadResult'], ['cppStatusKeys.js', 'cppStatusKey'],
                     ['proceduralTreeBuilder.js', 'filterWhitelistedSpells']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] === 'function') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
        return typeof g.onUnifiedConfigLoaded === 'function';
    },

    /** A fresh `settings` and `state` as the page starts with them (state.js) */
    _freshState: function() {
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var ctx = { console: console };
        ctx.window = ctx;
        vm.createContext(ctx);
        vm.runInContext(fs.readFileSync(path.join(__dirname, 'constants.js'), 'utf8'), ctx);
        vm.runInContext(fs.readFileSync(path.join(__dirname, 'state.js'), 'utf8'), ctx);
        return { settings: ctx.settings, state: ctx.state };
    },

    /** Runs fn(env) with the page's globals replaced; env.sent lists [method, text] */
    _with: function(g, fn) {
        var self = this, fresh = this._freshState();
        var env = { sent: [], status: [], timers: [], cleared: [], throwIn: null };
        var over = {
            settings: fresh.settings,
            state: fresh.state,
            callCpp: function(method, text) { env.sent.push([method, text]); },
            updateStatus: function(message) { env.status.push(message); },
            setTimeout: function(cb, ms) { env.timers.push({ cb: cb, ms: ms }); return env.timers.length; },
            clearTimeout: function(id) { env.cleared.push(id); },
            DesignPresets: { DEFAULT_ID: 'skald', savedChoice: function() { return 'skald'; },
                             apply: function() {}, requestFromDisk: function() {} },
            _presetsLoadRequested: true,
            // No page: every element lookup finds nothing (the load skips the controls)
            document: { getElementById: function() { return null; }, querySelector: function() { return null; },
                        querySelectorAll: function() { return []; }, addEventListener: function() {},
                        documentElement: { style: { setProperty: function() {} } } }
        };
        this.STUBS.forEach(function(name) {
            over[name] = function() { if (env.throwIn === name) throw new Error('stub ' + name + ' threw'); };
        });
        this.ABSENT.forEach(function(name) { over[name] = undefined; });
        var saved = {}, had = {};
        Object.keys(over).forEach(function(k) { had[k] = Object.prototype.hasOwnProperty.call(g, k); saved[k] = g[k]; g[k] = over[k]; });
        var oldErr = console.error, oldWarn = console.warn, oldLog = console.log;
        console.error = function() {};
        console.warn = function() {};
        // The panel's own lines (the whole settings object on every load) stay out of the run's output
        console.log = function(first) {
            if (typeof first === 'string' && first.indexOf('[SpellLearning]') === 0) return;
            oldLog.apply(console, arguments);
        };
        g.autoSaveTimer = null;
        g._lastSavedConfigText = null;
        g._configLoadFailed = false;
        try {
            fn.call(self, env);
        } finally {
            console.error = oldErr;
            console.warn = oldWarn;
            console.log = oldLog;
            g.autoSaveTimer = null;
            g._lastSavedConfigText = null;
            g._configLoadFailed = false;
            Object.keys(over).forEach(function(k) { if (had[k]) g[k] = saved[k]; else delete g[k]; });
        }
    },

    _saves: function(env) {
        return env.sent.filter(function(s) { return s[0] === 'SaveUnifiedConfig'; });
    },

    _lastSaved: function(env) {
        var saves = this._saves(env);
        return saves.length ? JSON.parse(saves[saves.length - 1][1]) : null;
    },

    _testXpCaps: function(g) {
        this._with(g, function(env) {
            g.onUnifiedConfigLoaded(JSON.stringify({ xpCapAny: 7, xpCapSchool: 20, xpCapDirect: 60 }));
            this.check(g.settings.xpCapAny === 7 && g.settings.xpCapSchool === 20 && g.settings.xpCapDirect === 60,
                'xpCap: saved caps are read back');
            g.saveUnifiedConfig();
            var cfg = this._lastSaved(env);
            this.check(cfg && cfg.xpCapAny === 7 && cfg.xpCapSchool === 20 && cfg.xpCapDirect === 60,
                'xpCap: the next save keeps them (it wrote 5/15/50 before)');
        });
        this._with(g, function() {
            g.settings.xpCapAny = 33;
            g.onUnifiedConfigLoaded(JSON.stringify({ learningMode: 'perSchool' }));
            this.check(g.settings.xpCapAny === 5 && g.settings.xpCapSchool === 15 && g.settings.xpCapDirect === 50,
                'xpCap: a config without them gives the defaults 5/15/50');
            g.onUnifiedConfigLoaded(JSON.stringify({ xpCapAny: 0, xpCapSchool: 100, xpCapDirect: 12.5 }));
            this.check(g.settings.xpCapAny === 0 && g.settings.xpCapSchool === 100 && g.settings.xpCapDirect === 12.5,
                'xpCap: the range ends 0 and 100 and a fraction are kept');
        });
        this._with(g, function() {
            g.onUnifiedConfigLoaded(JSON.stringify({ xpCapAny: 250, xpCapSchool: -4, xpCapDirect: 'lots' }));
            this.check(g.settings.xpCapAny === 100 && g.settings.xpCapSchool === 0 && g.settings.xpCapDirect === 50,
                'xpCap: a hand-edited value is held to 0-100, text gives the default');
        });
    },

    _testLoadFailedBlocksSave: function(g) {
        this._with(g, function(env) {
            env.throwIn = 'applyFullscreenState';   // a throw part way through the load
            g.onUnifiedConfigLoaded(JSON.stringify({ spellBlacklist: [{ plugin: 'A.esp', localFormId: '0x000801' }] }));
            var logged = env.sent.filter(function(s) {
                return s[0] === 'LogMessage' && JSON.parse(s[1]).level === 'error';
            });
            env.sent.length = 0;
            g.saveUnifiedConfig();
            g.saveSettings();
            this.check(this._saves(env).length === 0, 'load failed part way: no save goes out (the file keeps the blacklist)');
            this.check(logged.length === 1 && /NOT saved/.test(JSON.parse(logged[0][1]).message),
                'load failed part way: the reason goes to SpellLearning.log as an error');
            this.check(env.status.length === 1 && typeof cppStatusKey === 'function' && cppStatusKey(env.status[0]) === 'status.configNotSaved' &&
                env.status[0].indexOf('Error') === 0 && !/saved/i.test(env.status[0]),
                'load failed part way: the status bar shows the DLL\'s translated "changes are NOT written" line, as an error');

            env.throwIn = null;
            g.onUnifiedConfigLoaded(JSON.stringify({ xpCapAny: 9 }));
            g.saveUnifiedConfig();
            this.check(this._saves(env).length === 1 && this._lastSaved(env).xpCapAny === 9,
                'a later load that goes in whole lifts the block');
        });
        this._with(g, function(env) {
            g.onUnifiedConfigLoaded('null');
            g.saveUnifiedConfig();
            this.check(this._saves(env).length === 0, 'a config with nothing in it blocks saves too');
            g.onUnifiedConfigLoaded('{not json');
            g.saveUnifiedConfig();
            this.check(this._saves(env).length === 0, 'a config that does not parse blocks saves');
        });
        this._with(g, function(env) {
            g.onUnifiedConfigLoaded(JSON.stringify({ configNotRead: true }));
            g.saveUnifiedConfig();
            this.check(this._saves(env).length === 1,
                'configNotRead (C++ could not read the file) still saves: C++ writes nothing and applies it for the session');
        });
        var src = require('fs').readFileSync(require('path').join(__dirname, 'settingsPanel.js'), 'utf8');
        var fin = src.indexOf('} finally {', src.indexOf('window.onUnifiedConfigLoaded = function'));
        this.check(/noteConfigLoadResult\(applied, loadError\)/.test(src.slice(fin, src.indexOf('};', fin))),
            'onUnifiedConfigLoaded reports every load (whole or not) in its finally');
    },

    _testFlushOnHide: function(g) {
        this._with(g, function(env) {
            g.onUnifiedConfigLoaded(JSON.stringify({ xpCapAny: 5 }));
            g.saveUnifiedConfig();
            env.sent.length = 0;
            g.settings.xpCapAny = 8;
            g.autoSaveSettings();   // a slider moved: the save waits 500 ms
            this.check(env.timers.length === 1 && this._saves(env).length === 0, 'an autosave waits');
            g.saveSettings();       // the panel closes (onPanelHiding -> saveSettings)
            this.check(this._saves(env).length === 1 && this._lastSaved(env).xpCapAny === 8,
                'closing the panel saves the waiting change at once');
            this.check(env.cleared.indexOf(1) !== -1 && g.autoSaveTimer === null, 'the waiting autosave is cancelled');
        });
        this._with(g, function(env) {
            g.onUnifiedConfigLoaded(JSON.stringify({}));
            g.saveSettings();
            g.saveSettings();
            this.check(this._saves(env).length === 1 && env.cleared.length === 0,
                'nothing waiting: one save, the same text twice is sent once');
        });
        this._with(g, function(env) {
            env.throwIn = 'applySavedLanguage';
            g.onUnifiedConfigLoaded(JSON.stringify({}));
            g.autoSaveSettings();
            g.saveSettings();
            this.check(this._saves(env).length === 0 && g.autoSaveTimer === null,
                'after a failed load closing the panel saves nothing, and nothing is left waiting');
        });
        var cb = require('fs').readFileSync(require('path').join(__dirname, 'cppCallbacks.js'), 'utf8');
        var hide = cb.slice(cb.indexOf('window.onPanelHiding = function('));
        hide = hide.slice(0, hide.indexOf('\n};'));
        this.check(/saveSettings\(\)/.test(hide), 'onPanelHiding saves through saveSettings');
    },

    _testWhitelistMigration: function(g) {
        this._with(g, function(env) {
            var list = [{ plugin: 'заклинания.esp', enabled: false }, { plugin: 'skyrim.esm', enabled: true },
                        { plugin: 'Übermagic.esp', enabled: false }];
            var n = g.migrateLoweredWhitelist(list, ['Заклинания.esp', 'Skyrim.esm', 'Übermagic.esp']);
            this.check(n === 1 && list[0].plugin === 'Заклинания.esp' && list[0].enabled === false,
                'plugin list: a Cyrillic name an older panel lowered takes the scanned name, still off');
            this.check(list[1].plugin === 'skyrim.esm' && list[2].plugin === 'Übermagic.esp',
                'plugin list: an ASCII lowered name (folds the same) and an exact name are left alone');
            this.check(env.timers.length === 1, 'plugin list: a rename is saved (autosave)');
            var twice = [{ plugin: 'Заклинания.esp', enabled: true }, { plugin: 'заклинания.esp', enabled: false }];
            this.check(g.migrateLoweredWhitelist(twice, ['Заклинания.esp']) === 0 && twice[1].plugin === 'заклинания.esp',
                'plugin list: no rename when the scanned name has its own entry');
            this.check(g.migrateLoweredWhitelist(null, ['A.esp']) === 0 && g.migrateLoweredWhitelist([], []) === 0,
                'plugin list: no list or no scan: nothing done');

            // A build before the plugin list was opened: the lowered entry still turns its plugin off
            g.settings.pluginWhitelist = [{ plugin: 'заклинания.esp', enabled: false }, { plugin: 'Übermagic.esp', enabled: true }];
            var spells = [{ formId: '0x01000801', plugin: 'Заклинания.esp' }, { formId: '0x02000802', plugin: 'Übermagic.esp' },
                          { formId: '0x03000803', plugin: 'Other.esp' }, { formId: '0x04000804' }];
            var kept = g.filterWhitelistedSpells(spells).map(function(s) { return s.formId; });
            this.check(kept.join() === '0x02000802,0x03000803,0x04000804',
                'plugin list filter: a lowered "off" entry still leaves its plugin out; others and plugin-less spells stay');
        });
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        console.log('[ConfigSaveTest]');
        var g = (typeof global !== 'undefined') ? global : window;
        if (!this._load(g)) {
            this.check(false, 'settingsPanel.js loads');
            return { passed: this.passed, failed: this.failed };
        }
        this._testXpCaps(g);
        this._testLoadFailedBlocksSave(g);
        this._testFlushOnHide(g);
        this._testWhitelistMigration(g);
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.ConfigSaveTest = ConfigSaveTest;
if (typeof module !== 'undefined') module.exports = ConfigSaveTest;
