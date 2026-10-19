/**
 * PanelTidyTest - node tests for the 1.6 panel tidy-up (run-tests.js).
 *
 * Checks: a Reset to Defaults asks for a second click (ResetConfirm: arms with a shown label, a click within
 * MIN_CONFIRM_MS - a double-click - is ignored, a later one resets once, each button gets its own label back,
 * the label comes back after ARM_MS, one button armed at a time, disarm() with no argument resets the armed
 * button; both buttons, the tab switch and panel hide wired to it); the tree footer's status line
 * (TreeStatusLine, setTreeStatusKey / setTreeStatus): a load or save that went well goes back to "Ready"
 * after SHOW_MS - also the save answer from C++ (updateTreeStatus "Tree saved") after an Import - any other
 * write cancels that, a warning or error is not covered by a success line of the same step (KEEP_WINDOW_MS);
 * onPrismaReady fills the version row; index.html: version at the foot of Settings and not in the title bar,
 * Import in the Scan tab's shared bar and not in the tree footer, the count once; the description keys
 * taken out gone from every language, the new key in each.
 *
 * Depends on: resetConfirm.js, treeStatusLine.js, cppStatusKeys.js, cppCallbacks.js - under node this file
 * runs them into the global scope the way the page's script tags do. Swaps setTimeout, clearTimeout,
 * setI18n, clearI18n, document and the status writers for fakes while it runs, and puts them back.
 */

var PanelTidyTest = {

    passed: 0,
    failed: 0,

    REMOVED_KEYS: ['settings.ui.sideDetailsDesc', 'settings.progression.overallXpMultiplierDesc',
        'settings.progression.passiveAppliesToDesc', 'settings.progression.xpPerGameHourDesc',
        'settings.earlyLearning.enableDesc', 'settings.tomeLearning.desc'],

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(rel) {
        var fs = require('fs'), path = require('path');
        return fs.readFileSync(path.join(__dirname, rel), 'utf8');
    },

    _run: function(rel) {
        var path = require('path');
        require('vm').runInThisContext(this._file(rel), { filename: path.join(__dirname, rel) });
    },

    /** An element with attributes, a class list and text */
    _el: function() {
        var attrs = {}, classes = {};
        return {
            textContent: '',
            children: {},
            setAttribute: function(n, v) { attrs[n] = String(v); },
            getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; },
            removeAttribute: function(n) { delete attrs[n]; },
            classList: {
                add: function(c) { classes[c] = true; },
                remove: function(c) { delete classes[c]; },
                contains: function(c) { return !!classes[c]; }
            },
            querySelector: function(sel) { return this.children[sel] || null; }
        };
    },

    /** Fake clock: `now` moves only when told, timers run only when asked */
    _clock: function() {
        var timers = [], next = 1;
        var clock = {
            now: 1000000,
            timers: timers,
            setTimeout: function(fn, ms) { timers.push({ id: next, fn: fn, ms: ms }); return next++; },
            clearTimeout: function(id) {
                for (var i = timers.length - 1; i >= 0; i--) if (timers[i].id === id) timers.splice(i, 1);
            },
            runAll: function() { while (timers.length) { var t = timers.shift(); clock.now += t.ms; t.fn(); } }
        };
        return clock;
    },

    /** setI18n as i18n.js has it, the English fallback standing in for the language */
    _setI18n: function(el, key, params, fallback) {
        if (!el) return;
        el.setAttribute('data-i18n', key);
        if (params) el.setAttribute('data-i18n-params', JSON.stringify(params));
        else el.removeAttribute('data-i18n-params');
        var text = fallback === undefined || fallback === null ? key : fallback;
        for (var p in (params || {})) text = text.split('{{' + p + '}}').join(params[p]);
        el.textContent = text;
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        var names = ['setTimeout', 'clearTimeout', 'setI18n', 'clearI18n', 'document', 'setTreeStatusKey', 'setTreeStatus',
            'callCpp', '_panelShownOnce', '_panelVisible'];
        var old = {}, had = {};
        names.forEach(function(n) { had[n] = Object.prototype.hasOwnProperty.call(g, n); old[n] = g[n]; });
        try {
            this._run('resetConfirm.js');
            this._run('treeStatusLine.js');
            if (typeof g.cppStatusKey !== 'function') this._run('cppStatusKeys.js');
            if (typeof g.onPrismaReady !== 'function') require('./cppCallbacks.js');
        } catch (e) {
            this.check(false, 'resetConfirm.js, treeStatusLine.js, cppStatusKeys.js, cppCallbacks.js load (' + e.message + ')');
            return { passed: this.passed, failed: this.failed };
        }
        var R = g.ResetConfirm, L = g.TreeStatusLine, rNow = R._now, lNow = L._now;
        try {
            this._resetConfirm(g);
            this._statusLine(g);
            this._version(g);
        } finally {
            names.forEach(function(n) { if (had[n]) g[n] = old[n]; else { g[n] = undefined; try { delete g[n]; } catch (e) {} } });
            R._now = rNow; R._timer = null; R._armed = null;
            L._now = lNow; L._timer = null; L._keptAt = null;
        }
        this._markup();
        this._languages();
        this._sources();
        return { passed: this.passed, failed: this.failed };
    },

    _resetConfirm: function(g) {
        var clock = this._clock(), R = g.ResetConfirm, self = this;
        g.setTimeout = clock.setTimeout;
        g.clearTimeout = clock.clearTimeout;
        g.setI18n = this._setI18n;
        R._now = function() { return clock.now; };
        var button = function(key, text) {
            var btn = self._el(), label = self._el();
            btn.children['.hom-label'] = label;
            self._setI18n(label, key, null, text);
            return { btn: btn, label: label };
        };
        var main = button('settings.actions.resetDefaults', 'Reset to Defaults');
        var steps = button('settings.earlyLearning.resetDefaults', 'Reset to Defaults');
        var btn = main.btn, label = main.label, resets = 0;
        var reset = function() { resets++; };

        var ran = R.click(btn, reset);
        this.check(!ran && resets === 0 && R.isArmed(btn) && btn.classList.contains('armed'),
            'Reset: the first click resets nothing and arms the button');
        this.check(label.getAttribute('data-i18n') === 'settings.actions.resetConfirm' &&
            label.textContent === 'Click again to reset',
            'Reset: armed, the label itself says to click again (shown, not a hover title)');
        this.check(clock.timers.length === 1 && clock.timers[0].ms === R.ARM_MS, 'Reset: armed for ARM_MS');

        clock.now += R.MIN_CONFIRM_MS - 1;
        ran = R.click(btn, reset);
        this.check(!ran && resets === 0 && R.isArmed(btn),
            'Reset: a second click within MIN_CONFIRM_MS (a double-click) does not reset, the button stays armed');
        clock.now += 1;
        ran = R.click(btn, reset);
        this.check(ran && resets === 1 && !R.isArmed(btn) && !btn.classList.contains('armed') && clock.timers.length === 0,
            'Reset: a second click from MIN_CONFIRM_MS on resets once and disarms (its timer dropped)');
        this.check(label.getAttribute('data-i18n') === 'settings.actions.resetDefaults' && label.textContent === 'Reset to Defaults' &&
            btn.getAttribute('data-reset-key') === null,
            'Reset: after resetting, the label is its own "Reset to Defaults" key again');

        R.click(btn, reset);
        clock.runAll();
        this.check(resets === 1 && !R.isArmed(btn) && label.textContent === 'Reset to Defaults',
            'Reset: no second click within ARM_MS: back to the plain button, nothing reset');
        R.click(btn, reset);
        clock.now += R.MIN_CONFIRM_MS;
        this.check(resets === 1 && R.isArmed(btn), 'Reset: a click after the timeout arms again, it does not reset');

        R.click(steps.btn, reset);
        this.check(!R.isArmed(btn) && label.getAttribute('data-i18n') === 'settings.actions.resetDefaults' &&
            R.isArmed(steps.btn) && clock.timers.length === 1,
            'Reset: arming the power steps reset disarms the other one (one armed at a time, one timer)');
        R.disarm();
        this.check(!R.isArmed(steps.btn) && !steps.btn.classList.contains('armed') && clock.timers.length === 0 &&
            steps.label.getAttribute('data-i18n') === 'settings.earlyLearning.resetDefaults',
            'Reset: disarm() with no argument (tab switch, panel hide) puts the armed button and its own label back');
        R.disarm();
        this.check(R.click(null, reset) === false && resets === 1, 'Reset: disarm with nothing armed and a missing button: nothing happens');
    },

    _statusLine: function(g) {
        var clock = this._clock(), L = g.TreeStatusLine, status = this._el();
        g.setTimeout = clock.setTimeout;
        g.clearTimeout = clock.clearTimeout;
        g.setI18n = this._setI18n;
        g.clearI18n = function(el) { el.removeAttribute('data-i18n'); el.removeAttribute('data-i18n-params'); };
        g.document = { getElementById: function(id) { return id === 'tree-status-text' ? status : null; } };
        L._now = function() { return clock.now; };
        var key = function() { return status.getAttribute('data-i18n'); };
        var settle = function() { clock.runAll(); clock.now += L.KEEP_WINDOW_MS; };

        g.setTreeStatusKey('status.loadedSpells', { found: 753 }, 'Loaded {{found}} spells');
        this.check(status.textContent === 'Loaded 753 spells' && clock.timers.length === 1 && clock.timers[0].ms === L.SHOW_MS,
            'Status: "Loaded 753 spells" shown, to clear after SHOW_MS');
        clock.runAll();
        this.check(key() === 'footer.statusReady' && status.textContent === 'Ready',
            'Status: then "Ready" (a key, so a language switch writes it again)');

        g.setTreeStatusKey('status.treeSavedCount', { count: 753 }, 'Tree saved ({{count}} spells)');
        g.updateTreeStatus('"Tree saved"');
        this.check(key() === 'status.treeSaved' && clock.timers.length === 1,
            'Status: Import, then the save answer from C++ ("Tree saved"): brief too, one timer');
        clock.runAll();
        this.check(key() === 'footer.statusReady', 'Status: and it goes back to "Ready" (it no longer stays for good)');

        settle();
        g.setTreeStatusKey('status.loadedSpells', { found: 1 }, 'Loaded {{found}} spells');
        g.setTreeStatusKey('status.changesSaved', null, 'Changes saved');
        clock.runAll();
        this.check(key() === 'status.changesSaved', 'Status: another line written meanwhile stays (the timer is cancelled)');

        settle();
        g.setTreeStatusKey('status.loadedSpells', { found: 1 }, 'Loaded {{found}} spells');
        g.setTreeStatus('Some DLL message');
        clock.runAll();
        this.check(status.textContent === 'Some DLL message' && clock.timers.length === 0,
            'Status: a plain line written meanwhile stays');

        settle();
        g.setTreeStatusKey('status.warningUnreachable', { count: 3 }, 'WARNING: {{count}} unreachable spells - regenerate tree');
        g.setTreeStatusKey('status.loadedSpells', { found: 9 }, 'Loaded {{found}} spells');
        g.setTreeStatusKey('status.treeSavedCount', { count: 9 }, 'Tree saved ({{count}} spells)');
        g.updateTreeStatus('"Tree saved"');
        clock.runAll();
        this.check(key() === 'status.warningUnreachable' && status.textContent === 'WARNING: 3 unreachable spells - regenerate tree',
            'Status: a brief after a warning does not cover it (Import: unreachable spells, loaded, saved)');

        clock.now += L.KEEP_WINDOW_MS;
        g.setTreeStatusKey('status.loadedSpells', { found: 9 }, 'Loaded {{found}} spells');
        this.check(key() === 'status.loadedSpells', 'Status: a success KEEP_WINDOW_MS later (a new step) replaces the old warning');

        settle();
        g.updateTreeStatus('"Save failed"');
        g.updateTreeStatus('"Tree saved"');
        this.check(key() === 'status.saveFailed', 'Status: an error from C++ is not covered by a success right after it');

        settle();
        g.setTreeStatus('Unknown DLL text');
        g.setTreeStatusKey('status.loadedSpells', { found: 2 }, 'Loaded {{found}} spells');
        this.check(status.textContent === 'Unknown DLL text', 'Status: a plain DLL line (could be an error) is kept the same way');

        settle();
        g.setTreeStatusKey('status.loadedSpellsNotFound', { found: 5, notFound: 2 }, 'x');
        clock.runAll();
        this.check(key() === 'status.loadedSpellsNotFound', 'Status: "spells not found" is not brief, it stays');

        g.document = { getElementById: function() { return null; } };
        var threw = false;
        try { g.setTreeStatusKey('status.loadedSpells', { found: 1 }); g.setTreeStatus('x'); } catch (e) { threw = true; }
        this.check(!threw, 'Status: no status element, no throw');
    },

    /** onPrismaReady (cppCallbacks.js) fills the version and shows its row; with no version the row stays hidden */
    _version: function(g) {
        var self = this, make = function() {
            var tag = self._el(), row = self._el(), body = self._el();
            row.classList.add('hidden');
            return { tag: tag, row: row, doc: {
                body: body,
                querySelector: function(sel) { return sel === '.version-tag' ? tag : null; },
                getElementById: function(id) { return id === 'settingsVersion' ? row : null; }
            } };
        };
        var quiet = console.log;
        g.callCpp = undefined;
        g._panelShownOnce = true;
        var a = make(), b = make();
        try {
            console.log = function() {};
            g.document = a.doc;
            g.onPrismaReady('1.6.0');
            g.document = b.doc;
            g.onPrismaReady(undefined);
        } finally { console.log = quiet; }
        this.check(a.tag.textContent === 'Librarian 1.6.0' && !a.row.classList.contains('hidden'),
            'Version: onPrismaReady writes "Librarian 1.6.0" and shows its row at the foot of Settings');
        this.check(b.tag.textContent === '' && b.row.classList.contains('hidden'),
            'Version: no version sent, the row stays hidden (no bare "Librarian")');
    },

    _markup: function() {
        var html = this._file('../index.html');
        var header = html.substring(html.indexOf('id="panelHeader"'), html.indexOf('id="panelContent"'));
        var settings = html.substring(html.indexOf('id="contentSettings"'), html.indexOf('id="resizeHandle"'));
        var scan = html.substring(html.indexOf('id="contentSpellScan"'), html.indexOf('id="contentSpellTree"'));
        var easy = scan.indexOf('id="scannerEasyContent"');
        var footer = html.substring(html.indexOf('class="tree-footer"'), html.indexOf('id="contentSettings"'));
        this.check(header.indexOf('version-tag') === -1, 'Version: not in the title bar');
        this.check(html.split('class="version-tag"').length === 2 && /id="settingsVersion"[^>]*>\s*<span class="version-tag">/.test(settings),
            'Version: one .version-tag, in the row at the foot of Settings');
        this.check(/class="settings-version hidden" id="settingsVersion"/.test(settings),
            'Version: its row starts hidden (onPrismaReady shows it with the version)');
        var importAt = scan.indexOf('id="import-btn"');
        this.check(html.split('id="import-btn"').length === 2 && importAt !== -1 && importAt < easy &&
            scan.lastIndexOf('class="scan-actions-sticky"', importAt) !== -1,
            'Import: once, in the Scan tab\'s shared action bar (above the Easy and Complex parts)');
        this.check(footer.indexOf('import-btn') === -1 && footer.indexOf('<button id=') === -1,
            'Import: no longer in the tree footer');
        this.check(footer.split('id="total-count"').length === 2 && footer.indexOf('id="tree-status-text"') !== -1,
            'Footer: the spell count once, the status line kept');
        this.check(/id="resetSettingsBtn"[\s\S]{0,200}<span class="hom-label" data-i18n="settings\.actions\.resetDefaults">/.test(settings) &&
            /id="resetPowerStepsBtn"[\s\S]{0,200}<span class="hom-label" data-i18n="settings\.earlyLearning\.resetDefaults">/.test(settings),
            'Reset: both Reset to Defaults buttons have a label span with its own key (ResetConfirm puts it back)');
        var left = this.REMOVED_KEYS.filter(function(k) { return html.indexOf('"' + k + '"') !== -1; });
        this.check(left.length === 0, 'Descriptions taken out are gone from index.html' + (left.length ? ' (' + left.join(', ') + ')' : ''));
    },

    _languages: function() {
        var fs = require('fs'), path = require('path'), dir = path.join(__dirname, '..', 'lang'), self = this;
        var files = fs.readdirSync(dir).filter(function(f) { return /\.json$/.test(f) && f.charAt(0) !== '_'; });
        var bad = [];
        files.forEach(function(f) {
            var json = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
            var js = fs.readFileSync(path.join(dir, f.replace(/\.json$/, '.js')), 'utf8');
            self.REMOVED_KEYS.forEach(function(k) {
                if (json.hasOwnProperty(k) || js.indexOf('"' + k + '"') !== -1) bad.push(f + ' still has ' + k);
            });
            if (!json['settings.actions.resetConfirm'] || js.indexOf('"settings.actions.resetConfirm"') === -1) {
                bad.push(f + ' lacks settings.actions.resetConfirm');
            }
        });
        this.check(files.length === 13 && bad.length === 0,
            'Languages (13): removed keys gone from .json and .js, resetConfirm in each' + (bad.length ? ' (' + bad.join('; ') + ')' : ''));
    },

    _sources: function() {
        var panel = this._file('settingsPanel.js'), script = this._file('../script.js'), cpp = this._file('cppCallbacks.js');
        var at = panel.indexOf("getElementById('resetSettingsBtn')");
        var wiring = at === -1 ? '' : panel.substring(at, panel.indexOf('}', panel.indexOf('addEventListener', at)) + 1);
        this.check(/ResetConfirm\.click\(resetSettingsBtn, resetSettings\)/.test(wiring) && !/resetSettings\(\)/.test(wiring),
            'Reset: the page\'s button goes through ResetConfirm, not straight to resetSettings()');
        at = script.indexOf("getElementById('resetPowerStepsBtn')");
        wiring = at === -1 ? '' : script.substring(at, script.indexOf('});', at));
        this.check(/ResetConfirm\.click\(resetPowerStepsBtn, resetPowerStepsToDefaults\)/.test(wiring) &&
            !/resetPowerStepsToDefaults\(\)/.test(wiring),
            'Reset: the power steps button goes through ResetConfirm too');
        var tab = script.substring(script.indexOf('function switchTab'), script.indexOf('\nfunction ', script.indexOf('function switchTab') + 1));
        var hideAt = cpp.indexOf('window.onPanelHiding = function');
        var hideEnd = cpp.indexOf('\nwindow.', hideAt + 1);
        var hide = hideAt === -1 ? '' : cpp.substring(hideAt, hideEnd === -1 ? cpp.length : hideEnd);
        this.check(/ResetConfirm\.disarm\(\)/.test(tab) && /ResetConfirm\.disarm\(\)/.test(hide),
            'Reset: switching tabs and hiding the panel disarm it');
        var progression = this._file('progressionUI.js');
        this.check(!/function setTreeStatus(Key)?\(/.test(progression),
            'Status: setTreeStatus / setTreeStatusKey only in treeStatusLine.js (no second copy in progressionUI.js)');
    }
};

if (typeof window !== 'undefined') window.PanelTidyTest = PanelTidyTest;
if (typeof global !== 'undefined') global.PanelTidyTest = PanelTidyTest;
