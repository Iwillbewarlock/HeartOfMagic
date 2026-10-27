/**
 * PanelLayoutTest - node tests for the 2.0 panel layout, items 6 and 7 (run-tests.js).
 *
 * Settings categories (settingsCategories.js): one page at a time, the button marked, an unknown
 * category changes nothing, each category's scroll position kept across a category switch and the
 * Settings tab closing and opening, the Presets row moving to the Learning or Spell Tomes page, the
 * list as a row on a narrow page, every setting still in index.html on one of the four pages with
 * developer options only under Advanced.
 * Scan stage (scanStage.js): the stage from the page's state, the main button and what it presses
 * (Easy's or Complex's Build), the summary's numbers with the left-out reasons adding up to the
 * difference, after a build the builder's unplaced spells, the filter lines (excluded count; plugins
 * all / N of M / none), "applied" set by Apply and dropped by a new draft, the details fold; markup:
 * the relayed buttons kept, Alternate Pathways only in Developer Mode, the hooks in place.
 *
 * Depends on: settingsCategories.js, scanStage.js, proceduralTreeBuilder.js (the real filters),
 * constants.js' foldPluginName - each run in its own sandbox with a fake document.
 */

var PanelLayoutTest = {

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

    /** A fake element: attributes, classes, children, listeners */
    el: function(tag, attrs) {
        var a = {}, classes = {}, listeners = {};
        var e = {
            tagName: (tag || 'div').toUpperCase(), children: [], parentNode: null, style: {},
            textContent: '', disabled: false, scrollTop: 0, clientWidth: 0, clicks: 0,
            setAttribute: function(n, v) { a[n] = String(v); if (n === 'class') { classes = {}; String(v).split(/\s+/).forEach(function(c) { if (c) classes[c] = true; }); } },
            getAttribute: function(n) { return a.hasOwnProperty(n) ? a[n] : null; },
            removeAttribute: function(n) { delete a[n]; },
            classList: {
                add: function(c) { classes[c] = true; },
                remove: function(c) { delete classes[c]; },
                contains: function(c) { return !!classes[c]; },
                toggle: function(c, on) { if (on === undefined) on = !classes[c]; if (on) classes[c] = true; else delete classes[c]; return on; }
            },
            addEventListener: function(type, fn) { (listeners[type] = listeners[type] || []).push(fn); },
            click: function() { e.clicks++; (listeners.click || []).forEach(function(fn) { fn({}); }); },
            appendChild: function(c) { if (c.parentNode) c.parentNode._remove(c); c.parentNode = e; e.children.push(c); return c; },
            insertBefore: function(c, ref) {
                if (c.parentNode) c.parentNode._remove(c);
                c.parentNode = e;
                var i = e.children.indexOf(ref);
                if (i === -1) e.children.push(c); else e.children.splice(i, 0, c);
                return c;
            },
            _remove: function(c) { var i = e.children.indexOf(c); if (i !== -1) e.children.splice(i, 1); c.parentNode = null; },
            get firstChild() { return e.children[0] || null; },
            set innerHTML(v) { e.children.forEach(function(c) { c.parentNode = null; }); e.children = []; e.textContent = ''; },
            get innerHTML() { return ''; },
            /** Its own text, then its children's (as the DOM has it after textContent and appendChild) */
            text: function() {
                return e.textContent + e.children.map(function(c) { return c.text ? c.text() : c.textContent; }).join('');
            }
        };
        for (var k in (attrs || {})) e.setAttribute(k, attrs[k]);
        return e;
    },

    /** A fake document over a list of elements (by id, by class, by [data-settings-cat]) */
    doc: function(els) {
        var self = this;
        var all = els.slice();
        var match = function(e, sel) {
            var m = /^\.([\w-]+)(?:\[data-settings-cat="(\w+)"\])?$/.exec(sel);
            if (!m) return false;
            return e.classList.contains(m[1]) && (!m[2] || e.getAttribute('data-settings-cat') === m[2]);
        };
        return {
            getElementById: function(id) { for (var i = 0; i < all.length; i++) if (all[i].getAttribute('id') === id) return all[i]; return null; },
            querySelectorAll: function(sel) { return all.filter(function(e) { return match(e, sel); }); },
            querySelector: function(sel) { return all.filter(function(e) { return match(e, sel); })[0] || null; },
            createElement: function(tag) { var e = self.el(tag); all.push(e); return e; },
            createTextNode: function(t) { return { textContent: t, parentNode: null }; },
            add: function(e) { all.push(e); return e; }
        };
    },

    /** setI18n as i18n.js has it, the English fallback standing in for the language */
    setI18n: function(el, key, params, fallback) {
        if (!el) return;
        el.setAttribute('data-i18n', key);
        if (params) el.setAttribute('data-i18n-params', JSON.stringify(params)); else el.removeAttribute('data-i18n-params');
        var text = fallback === undefined || fallback === null ? key : fallback;
        for (var p in (params || {})) text = text.split('{{' + p + '}}').join(params[p]);
        el.textContent = text;
    },

    _sandbox: function(files, extra) {
        var vm = require('vm'), path = require('path');
        var quiet = function() {};
        var box = { window: {}, console: { log: quiet, warn: quiet, error: quiet }, setI18n: this.setI18n };
        for (var k in (extra || {})) box[k] = extra[k];
        vm.createContext(box);
        var self = this;
        files.forEach(function(f) { vm.runInContext(self._file(f), box, { filename: path.join(__dirname, f) }); });
        return box;
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['Settings categories', this._categories], ['Settings markup', this._settingsMarkup],
            ['Scan stage', this._scanStage], ['Scan page', this._scanPage], ['Scan markup', this._scanMarkup], ['Tree frame', this._treeFrame]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    },

    // ======================================================================= item 6

    _categories: function() {
        var self = this, cats = ['display', 'learning', 'tomes', 'advanced'];
        var content = this.el('div', { id: 'contentSettings', 'class': 'tab-content active' });
        content.clientWidth = 1300;
        var btns = cats.map(function(c) { return self.el('button', { 'class': 'settings-cat-btn', 'data-settings-cat': c }); });
        var pages = cats.map(function(c) { return self.el('div', { 'class': 'settings-cat-page', 'data-settings-cat': c }); });
        var presets = this.el('div', { id: 'settingsPresetsRow' });
        pages[1].appendChild(this.el('div', { 'class': 'settings-block' }));
        pages[1].insertBefore(presets, pages[1].firstChild);
        pages[2].appendChild(this.el('div', { id: 'spellTomeLearningBlock' }));
        var disarmed = 0;
        var box = this._sandbox(['settingsCategories.js'], { ResetConfirm: { disarm: function() { disarmed++; } } });
        var S = box.SettingsCategories, doc = this.doc([content, presets].concat(btns, pages));
        S._doc = function() { return doc; };
        S.init();
        var shown = function() { return pages.filter(function(p) { return p.classList.contains('active'); }).map(function(p) { return p.getAttribute('data-settings-cat'); }); };
        this.check(shown().join() === 'display' && btns[0].classList.contains('active') && btns[0].getAttribute('aria-selected') === 'true',
            'Categories: opens on Display, its button marked, only its page shown');

        btns[1].click();
        this.check(shown().join() === 'learning' && !btns[0].classList.contains('active') && btns[1].classList.contains('active') &&
            btns[0].getAttribute('aria-selected') === 'false', 'Categories: a click on Learning shows only Learning and moves the mark');
        this.check(disarmed === 1, 'Categories: switching disarms a waiting Reset to Defaults');

        content.scrollTop = 640;
        btns[3].click();
        this.check(shown().join() === 'advanced' && content.scrollTop === 0, 'Categories: Advanced opens at its own top');
        content.scrollTop = 120;
        btns[1].click();
        this.check(content.scrollTop === 640, 'Categories: back to Learning, it is where it was left (640)');
        btns[3].click();
        this.check(content.scrollTop === 120, 'Categories: and Advanced where it was left (120)');

        // The Settings tab closes (switchTab calls remember) and the page loses its scroll, then opens again
        S.remember();
        content.classList.remove('active');
        content.scrollTop = 0;
        content.classList.add('active');
        S.onShow();
        this.check(shown().join() === 'advanced' && content.scrollTop === 120,
            'Categories: the tab opened again shows the category and position it was left on');
        content.classList.remove('active');
        content.scrollTop = 999;
        S.remember();
        this.check(S._scroll.advanced === 120, 'Categories: remember() with the tab hidden keeps the last position');
        content.classList.add('active');

        btns[2].click();
        this.check(presets.parentNode === pages[2] && pages[2].children[0] === presets,
            'Categories: the Presets row moves to the top of Spell Tomes (it drives those settings too)');
        btns[0].click();
        this.check(presets.parentNode === pages[2], 'Categories: Display leaves the Presets row where it was');
        btns[1].click();
        this.check(presets.parentNode === pages[1] && pages[1].children[0] === presets, 'Categories: and Learning takes it back, on top');

        var before = S.current;
        S.select('nonsense');
        S.show('alsoNot');
        this.check(S.current === before && shown().join() === before, 'Categories: an unknown name changes nothing');

        content.clientWidth = 600;
        S.layout();
        this.check(content.classList.contains('settings-narrow'), 'Categories: a page narrower than NARROW_PX gets the list as a row on top');
        content.clientWidth = 1300;
        S.layout();
        this.check(!content.classList.contains('settings-narrow'), 'Categories: wide again, the list goes back to the left column');
    },

    _settingsMarkup: function() {
        var html = this._file('../index.html');
        var settings = html.substring(html.indexOf('id="contentSettings"'), html.indexOf('id="resizeHandle"'));
        var pageOf = {}, cats = ['display', 'learning', 'tomes', 'advanced'];
        cats.forEach(function(c) {
            var at = settings.indexOf('class="settings-cat-page' + (c === 'display' ? ' active' : '') + '" id="settingsCat');
            var start = settings.indexOf('data-settings-cat="' + c + '">');
            var next = cats.indexOf(c) < 3 ? settings.indexOf('data-settings-cat="' + cats[cats.indexOf(c) + 1] + '">') : settings.indexOf('id="settingsVersion"');
            pageOf[c] = start === -1 || at === -1 ? '' : settings.substring(start, next);
        });
        this.check(cats.every(function(c) { return pageOf[c].length > 0; }), 'Settings markup: four pages, Display first and shown');
        // Every control id in Settings, and where each must be
        var want = {
            display: ['hotkeyInput', 'pauseGameOnFocusToggle', 'autoBuildToggle', 'focus-on-click', 'focus-zoom-on-click', 'tree-focus-zoom',
                'focus-rotate', 'focus-dim-others', 'show-selection-path', 'edge-style-toggle', 'tree-node-font-size', 'uiLanguageSelect',
                'designPresetSelect', 'learningColorPicker', 'fontSizeSlider', 'sideDetailsToggle', 'hoverDetailsToggle'],
            learning: ['settingsPresetsRow', 'learningModeToggle', 'discoveryModeToggle', 'showRootSpellNamesToggle', 'autoAdvanceLearningToggle',
                'autoAdvanceModeToggle', 'xpGlobalMultiplierSlider', 'xpDirectSlider', 'xpSchoolSlider', 'xpAnySlider', 'xpCapAnySlider',
                'xpCapSchoolSlider', 'xpCapDirectSlider', 'xpNoviceInput', 'xpMasterInput', 'reverseUnlockToggle', 'reverseUnlockToRootToggle',
                'reverseUnlockXpNoviceSlider', 'reverseXpSeparateToggle', 'reverseXpGlobalMultiplierSlider', 'passiveLearningToggle',
                'passiveScopeToggle', 'passiveXpPerHourSlider', 'passiveMaxNovice', 'revealNameSlider', 'revealEffectsSlider', 'revealDescSlider',
                'earlyLearningEnabledToggle', 'modifyGameDisplayToggle', 'unlockThresholdSlider', 'selfCastRequiredSlider',
                'selfCastMultiplierSlider', 'binaryThresholdSlider', 'showNotificationsToggle', 'notificationLanguageSelect',
                'weakenedNotificationsToggle', 'notificationIntervalSlider', 'powerStepsContainer', 'resetPowerStepsBtn', 'moddedXPSourcesSection'],
            tomes: ['spellResearchStatusRow', 'useProgressionSystemToggle', 'tomeXpGrantSlider', 'tomeInventoryBoostToggle', 'tomeInventoryBoostSlider',
                'tomeRequirePrereqsToggle', 'tomeRequireSkillLevelToggle', 'tomeStudySkillXPToggle'],
            advanced: ['developerModeToggle', 'cheatModeToggle', 'verboseLogToggle', 'debugGridToggle', 'aggressivePathValidationToggle']
        };
        var misplaced = [];
        cats.forEach(function(c) {
            want[c].forEach(function(id) { if (pageOf[c].indexOf('id="' + id + '"') === -1) misplaced.push(id + ' not on ' + c); });
        });
        this.check(misplaced.length === 0, 'Settings markup: every setting on its page' + (misplaced.length ? ' (' + misplaced.join(', ') + ')' : ''));
        var inputs = settings.match(/<(input|select)\b[^>]*\sid="([^"]+)"/g) || [];
        var known = [].concat(want.display, want.learning, want.tomes, want.advanced).concat(['notificationIntervalSlider']);
        var stray = inputs.map(function(s) { return /id="([^"]+)"/.exec(s)[1]; }).filter(function(id) {
            return cats.every(function(c) { return pageOf[c].indexOf('id="' + id + '"') === -1; });
        });
        this.check(stray.length === 0 && inputs.length >= 40, inputs.length + ' controls, none outside the four pages' + (stray.length ? ' (' + stray.join(', ') + ')' : ''));
        var devInLearning = ['developerModeToggle', 'cheatModeToggle', 'verboseLogToggle', 'debugGridToggle', 'aggressivePathValidationToggle'].filter(function(id) {
            return pageOf.learning.indexOf('id="' + id + '"') !== -1 || pageOf.display.indexOf('id="' + id + '"') !== -1 || pageOf.tomes.indexOf('id="' + id + '"') !== -1;
        });
        this.check(devInLearning.length === 0 && pageOf.advanced.indexOf('settings-block dev-only') !== -1,
            'Settings markup: developer and debug options only under Advanced (Tree Generation still .dev-only)');
        this.check(/id="saveSettingsBtn"/.test(settings) && /id="resetSettingsBtn"/.test(settings) && settings.indexOf('id="saveSettingsBtn"') > settings.indexOf('id="settingsCatAdvanced"'),
            'Settings markup: Save and Reset stay, under the pages');
        var sheet = this._file('../panel-settings.css');
        this.check((sheet.match(/grid-template-rows:[^;]*minmax\(min-content, 1fr\)/g) || []).length === 2 && !/minmax\(0, 1fr\) auto auto/.test(sheet),
            'Settings CSS: the page row is at least its content (wide and narrow), so a long page pushes the sticky Save/Reset bar down instead of scrolling it away');
        var script = this._file('../script.js');
        this.check(/SettingsCategories\.remember\(\)/.test(script) && /SettingsCategories\.onShow\(\)/.test(script) && /SettingsCategories\.init\(\)/.test(this._file('main.js')),
            'Settings markup: switchTab keeps and puts back the scroll, main.js wires the list');
    },

    // ======================================================================= item 7

    _scanBox: function(extra) {
        var box = this._sandbox(['constants.js', 'proceduralTreeBuilder.js', 'scanStage.js'],
            Object.assign({ settings: { spellBlacklist: [], pluginWhitelist: [] }, state: { lastSpellData: null },
                getLocalFormId: function(f) { return String(f).replace(/^0x/i, '').toLowerCase().slice(-6); } }, extra || {}));
        return box;
    },

    /**
     * The gap line in every language file: the excluded list as the whole gap reads as that reason alone, and the
     * lead of two or more ("15 left out:") does not reuse the excluded list's own word ("12개 제외: 제외 목록에 12개")
     */
    _checkGapLanguages: function(S) {
        var fs = require('fs'), path = require('path'), dir = path.join(__dirname, '..', 'lang');
        var files = fs.readdirSync(dir).filter(function(f) { return /^[a-z-]+\.json$/.test(f); });
        var fill = function(s, p) { for (var k in (p || {})) s = s.split('{{' + k + '}}').join(p[k]); return s; };
        var alone = [], repeats = [];
        // The stem each language names the excluded list with (scan.gapExcluded, scan.excludedCount)
        var EXCLUDED_WORD = { en: 'exclu', de: 'sperr', es: 'exclu', fr: 'exclu', it: 'esclu', ja: '除外', ko: '제외', pl: 'wyklucz',
            'pt-br': 'exclu', ru: 'исключ', tr: 'dışlan', 'zh-cn': '排除', 'zh-tw': '排除' };
        // The gap is keys, the same in every language: the excluded list as the whole gap is that one line
        var one = S.summary('apply', { found: 753, tomeOut: 0, excluded: 12, pluginsOut: 0, inTree: 741 }, 741).gap;
        this.check(one.length === 1 && one[0][0] === 'scan.gapExcluded', 'Gap line: the excluded list as the whole gap (after a build too) is one line, no lead');
        files.forEach(function(f) {
            var L = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
            // ...which, standing alone, must carry the number itself
            if (typeof L['scan.gapExcluded'] !== 'string' || L['scan.gapExcluded'].indexOf('{{count}}') === -1) alone.push(f);
            var lead = fill(L['scan.gapLeftOut'] || '', { count: 15 }).toLowerCase();
            var word = EXCLUDED_WORD[f.slice(0, -5)];
            if (!word || lead.indexOf(word) !== -1) repeats.push(f + ' (' + lead + ')');
        });
        this.check(files.length === 13 && alone.length === 0, 'Gap line, every language: scan.gapExcluded is there and holds its {{count}} (it stands alone)' + (alone.length ? ': ' + alone.join(', ') : ''));
        this.check(repeats.length === 0, 'Gap line, every language: "N left out:" does not reuse the word of the excluded list' + (repeats.length ? ': ' + repeats.join(', ') : ''));
    },

    _spells: function() {
        var s = [];
        for (var i = 0; i < 10; i++) s.push({ formId: '0x0001000' + i, name: 'A' + i, school: 'Destruction', plugin: 'Skyrim.esm' });
        for (var j = 0; j < 4; j++) s.push({ formId: '0x0200000' + j, name: 'B' + j, school: 'Illusion', plugin: 'Mod.esp' });
        s.push({ formId: '0x00020000', name: 'Power', school: 'Illusion', plugin: 'Skyrim.esm', voiceSlot: true });
        s.push({ formId: '0x00020001', name: 'NPC', school: 'Illusion', plugin: 'Skyrim.esm', taughtByTome: false });
        s.push({ formId: '0x00020002', name: 'Copy', school: 'Illusion', plugin: 'Skyrim.esm', duplicateOf: '0x00010000' });
        return s;
    },

    _scanStage: function() {
        var box = this._scanBox(), S = box.ScanStage;
        var st = function(o) { return S.stageOf({ scanning: !!o.scanning, hasSpells: !!o.hasSpells, building: !!o.building, built: !!o.built, applied: !!o.applied }); };
        this.check(st({}) === 'scan' && st({ scanning: true }) === 'scanning' && st({ hasSpells: true }) === 'build' &&
            st({ hasSpells: true, building: true }) === 'building' && st({ hasSpells: true, built: true }) === 'apply' &&
            st({ hasSpells: true, built: true, applied: true }) === 'applied' && st({ hasSpells: true, built: true, scanning: true }) === 'scanning',
            'Stage: scan, scanning, build, building, apply, applied from the page\'s state');
        this.check(S.mainButton('scan')[0] === 'buttons.scanSpells' && S.mainButton('build')[2] === 'build' && S.mainButton('building')[0] === 'buttons.buildTree' &&
            S.mainButton('apply')[0] === 'buttons.applyTree' && S.mainButton('applied')[2] === null && S.mainButton('scanning')[2] === null,
            'Stage: the main button reads Scan Spells, Build Tree, Apply Tree, then Tree applied (pressing nothing)');

        var spells = this._spells();
        box.settings.spellBlacklist = [{ plugin: 'Skyrim.esm', localFormId: '010001' }, { formId: '0x00010002' }];
        box.settings.pluginWhitelist = [{ plugin: 'MOD.ESP', enabled: false }, { plugin: 'Skyrim.esm', enabled: true }];
        var c = S.counts(spells);
        this.check(c.found === 17 && c.tomeOut === 3 && c.excluded === 2 && c.pluginsOut === 4 && c.inTree === 8,
            'Counts: 17 found, 3 not player spells, 2 excluded (plugin+id and a legacy formId), 4 from the plugin turned off (A-Z folded), 8 in');
        box.state.lastSpellData = { spells: spells };
        var primed = spells.filter(box.isTaughtByTome);
        primed = box.filterWhitelistedSpells(box.filterBlacklistedSpells(primed));
        this.check(primed.length === c.inTree, 'Counts: "go in the tree" is the build\'s own filter result (getPrimedSpells order)');

        var sum = S.summary('build', c, 0);
        var text = function(list) { return list.map(function(p) { var t = p[2]; for (var k in (p[1] || {})) t = t.split('{{' + k + '}}').join(p[1][k]); return t; }); };
        this.check(text(sum.parts).join(' | ') === '17 spells found | 8 go in the tree | not built yet', 'Summary before a build: found, in the tree, not built yet');
        var gap = sum.gap.slice(1).reduce(function(n, p) { return n + p[1].count; }, 0);
        this.check(sum.gap[0][0] === 'scan.gapLeftOut' && sum.gap[0][1].count === 9 && gap === 9 && sum.gap.length === 4,
            'Summary: "9 left out" and its three reasons add up to found minus in the tree');
        var built = S.summary('apply', c, 6);
        this.check(text(built.parts).join(' | ') === '17 spells found | 6 in the new tree | not applied yet' &&
            built.gap[0][1].count === 11 && built.gap[built.gap.length - 1][0] === 'scan.gapNotPlaced' && built.gap[built.gap.length - 1][1].count === 2,
            'Summary after a build: the placed count, and the 2 the builder did not place among the 11 left out');
        this.check(text(S.summary('applied', c, 8).parts).join(' | ') === '17 spells found | 8 in your tree | applied', 'Summary applied: in your tree, applied');
        var none = S.summary('build', { found: 5, tomeOut: 0, excluded: 0, pluginsOut: 0, inTree: 5 }, 0);
        this.check(none.gap.length === 0 && S.summary('scan', c, 0).parts[0][0] === 'scan.summaryNoScan', 'Summary: no gap line when nothing is left out; before a scan, one line saying so');
        var saved = S.summary('scan', c, 0, 640);
        this.check(saved.parts.length === 1 && saved.parts[0][0] === 'scan.summaryTreeNoScan' && saved.parts[0][1].count === 640 && saved.gap.length === 0,
            'Summary: a saved tree and no scan this session says the tree is there (640 spells), not "No scan yet"');
        var one = S.summary('build', { found: 20, tomeOut: 0, excluded: 12, pluginsOut: 0, inTree: 8 }, 0);
        this.check(one.gap.length === 1 && one.gap[0][0] === 'scan.gapExcluded' && one.gap[0][1].count === 12,
            'Summary: one reason that is the whole gap says it alone ("12 on your excluded list", no "12 left out:")');
        this._checkGapLanguages(S);

        var p = S.pluginState(spells, box.settings.pluginWhitelist);
        this.check(p.total === 2 && p.off === 1 && S.pluginLine(p)[0] === 'scan.pluginsSome' && S.pluginLine(p)[1].on === 1,
            'Plugins line: 1 of 2 (the list\'s entry matched A-Z folded)');
        this.check(S.pluginLine(S.pluginState(spells, []))[0] === 'scan.pluginsAll' &&
            S.pluginLine(S.pluginState(spells, [{ plugin: 'Skyrim.esm', enabled: true }]))[0] === 'scan.pluginsAll',
            'Plugins line: no list, or nothing turned off, reads "all"');
        this.check(S.pluginLine(S.pluginState(spells, [{ plugin: 'skyrim.esm', enabled: false }, { plugin: 'Mod.esp', enabled: false }]))[0] === 'scan.pluginsNone',
            'Plugins line: every plugin turned off says no spell goes in the tree');
    },

    _scanPage: function() {
        var self = this;
        var ids = ['scanStageBtn', 'scanStageLabel', 'scanBtn', 'scanAgainBtn', 'scanClearDraftBtn', 'tgBuildBtn', 'tgApplyBtn', 'tgClearBtn', 'easyBuildBtn',
            'scannerComplexContent', 'scanSummary', 'scanSummaryGap', 'scanDetailsToggle', 'scanDetailsArrow', 'scanResultsBody', 'blacklistLabel', 'whitelistLabel'];
        var els = {};
        ids.forEach(function(id) { els[id] = self.el('div', { id: id }); });
        els.scannerComplexContent.style.display = 'none';
        var growth = { _building: false, _treeBuilt: false, _nodeCount: 0 };
        var box = this._scanBox({ TreeGrowth: growth }), S = box.ScanStage, doc = this.doc(ids.map(function(id) { return els[id]; }));
        S._doc = function() { return doc; };
        S.init();
        this.check(els.scanStageLabel.getAttribute('data-i18n') === 'buttons.scanSpells' && !els.scanStageBtn.disabled &&
            els.scanAgainBtn.classList.contains('hidden') && els.scanSummary.text().indexOf('No scan yet') === 0 &&
            els.scanDetailsToggle.classList.contains('hidden'),
            'Page before a scan: "No scan yet", the main button Scan Spells, no Scan again, no Scan details to unfold');
        box.state.treeData = { nodes: [{}, {}, {}] };
        S.refresh();
        this.check(els.scanSummary.text() === 'Your tree has 3 spells · scan to build a new one' && els.scanStageLabel.getAttribute('data-i18n') === 'buttons.scanSpells',
            'Page with a saved tree, no scan yet: the line names the tree, the button still scans');
        box.state.treeData = null;
        els.scanStageBtn.click();
        this.check(els.scanBtn.clicks === 1, 'Page: the main button presses the real Scan button');
        // A scan C++ reports running without the Scan button (edit mode's spawn list): the bar's line is out of sight
        box.scanStatusKey = function() { return 'status.scanningAllSpells'; };
        S.refresh();
        this.check(S.stage() === 'scanning' && els.scanSummary.text() === 'Scanning all spells...',
            'Page: a scan C++ says is running (edit mode asked, the Scan button untouched) reads as scanning');
        box.scanStatusKey = function() { return 'status.scannedSpellsSchools'; };
        box.state.lastSpellData = { spells: [] };
        box.state.treeData = { nodes: [{}, {}, {}] };
        S.refresh();
        this.check(els.scanSummary.text() === '0 spells found' && S.stage() === 'scan',
            'Page: a scan that found no spell says "0 spells found", not "No scan yet" or the saved tree (the bar\'s count is out of sight)');
        box.state.lastSpellData = null;
        box.state.treeData = null;
        delete box.scanStatusKey;

        box.state.lastSpellData = { spells: this._spells() };
        box.settings.spellBlacklist = [{ formId: '0x00010003' }];
        S.refresh();
        this.check(els.scanStageLabel.getAttribute('data-i18n') === 'buttons.buildTree' && !els.scanAgainBtn.classList.contains('hidden') &&
            els.scanSummary.text() === '17 spells found · 13 go in the tree · not built yet' &&
            !els.scanSummaryGap.classList.contains('hidden') && els.scanSummaryGap.text().indexOf('4 left out:') === 0,
            'Page scanned: Build Tree, Scan again shown, the summary line, the gap line with 4 left out');
        this.check(els.blacklistLabel.textContent === 'Excluded spells: 1' && els.whitelistLabel.textContent === 'Plugins: all',
            'Page: the filter lines say their state (1 excluded, plugins all)');
        els.scanStageBtn.click();
        this.check(els.easyBuildBtn.clicks === 1 && els.tgBuildBtn.clicks === 0, 'Page Easy: Build presses Easy\'s Build (it applies the chosen preset first)');
        els.easyBuildBtn.disabled = true;
        S.refresh();
        this.check(!els.scanStageBtn.disabled, 'Page Easy: the main button follows the Complex Build its Easy copy mirrors (the copy catches up after a refresh, MutationObserver)');
        els.easyBuildBtn.disabled = false;
        els.scannerComplexContent.style.display = '';
        els.scanStageBtn.click();
        this.check(els.tgBuildBtn.clicks === 1, 'Page Complex: Build presses the Complex page\'s Build (its own settings)');
        els.tgBuildBtn.disabled = true;
        S.refresh();
        this.check(els.scanStageBtn.disabled, 'Page: the main button is off while the button it presses is');
        els.tgBuildBtn.disabled = false;

        growth._building = true;
        S.refresh();
        this.check(els.scanStageBtn.disabled && els.scanSummary.text().indexOf('being built') !== -1, 'Page building: the button waits, the summary says it is being built');
        growth._building = false; growth._treeBuilt = true; growth._nodeCount = 13;
        S.onDraftChanged();
        this.check(els.scanStageLabel.getAttribute('data-i18n') === 'buttons.applyTree' && !els.scanClearDraftBtn.classList.contains('hidden'),
            'Page built: Apply Tree, and Clear draft tree shown');
        els.scanStageBtn.click();
        els.scanClearDraftBtn.click();
        this.check(els.tgApplyBtn.clicks === 1 && els.tgClearBtn.clicks === 1, 'Page: Apply and Clear draft tree press the real Apply and Clear');
        S.markApplied();
        this.check(els.scanStageLabel.getAttribute('data-i18n') === 'scan.stageApplied' && els.scanStageBtn.disabled &&
            els.scanSummary.text().indexOf('in your tree') !== -1 && !els.scanClearDraftBtn.classList.contains('hidden'),
            'Page applied: "Tree applied", nothing to press, "in your tree"; Clear draft tree stays, the way back to Build');
        S.onScanned();
        this.check(S.stage() === 'apply' && els.scanStageLabel.getAttribute('data-i18n') === 'buttons.applyTree' && !els.scanClearDraftBtn.classList.contains('hidden'),
            'Page: a scan after Apply - not "applied" any more: the draft can be applied or cleared (Clear draft tree leads back to Build)');
        S.markApplied();
        S.onDraftChanged();
        this.check(S.stage() === 'apply', 'Page: a new draft is not applied any more');
        els.tgClearBtn.disabled = true;
        S.refresh();
        this.check(els.scanClearDraftBtn.classList.contains('hidden'), 'Page: no Clear draft tree while the real Clear is off');

        this.check(els.scanResultsBody.classList.contains('hidden') && els.scanDetailsToggle.getAttribute('aria-expanded') === 'false',
            'Details: folded at first');
        els.scanDetailsToggle.click();
        this.check(!els.scanResultsBody.classList.contains('hidden') && els.scanDetailsToggle.getAttribute('aria-expanded') === 'true' &&
            els.scanDetailsArrow.textContent === '▾', 'Details: a click opens them (arrow down)');
        S.refresh();
        this.check(!els.scanResultsBody.classList.contains('hidden'), 'Details: a refresh keeps them open');
    },

    _scanMarkup: function() {
        var html = this._file('../index.html');
        var scan = html.substring(html.indexOf('id="contentSpellScan"'), html.indexOf('id="contentSpellTree"'));
        var relays = ['scanBtn', 'easyBuildBtn', 'easyApplyBtn', 'easyClearBtn', 'tgBuildBtn', 'tgApplyBtn', 'tgClearBtn'].filter(function(id) {
            return !new RegExp('id="' + id + '" class="[^"]*scan-relay-target').test(scan);
        });
        this.check(relays.length === 0, 'Scan markup: the real buttons stay (hidden relays the stage row and easyMode.js press)' + (relays.length ? ' (' + relays.join(', ') + ')' : ''));
        this.check(/class="prm-tab not-implemented dev-only hidden" data-prm-tab="altpaths"/.test(scan),
            'Scan markup: Alternate Pathways (not made yet) shows only in Developer Mode');
        this.check(/id="easyReplayBtn"/.test(scan.substring(scan.indexOf('easy-preview-caption'))) && /data-scanner-mode="complex"/.test(scan),
            'Scan markup: Replay beside the preview; COMPLEX one click away');
        var hooks = [[this._file('treeGrowth.js'), /ScanStage\.onDraftChanged\(\)/], [this._file('classic/classicMain.js'), /ScanStage\.markApplied\(\)/],
            [this._file('cppCallbacks.js'), /state\.lastSpellData = data;[\s\S]{0,200}ScanStage\.onScanned\(\)/],
            [this._file('prereqMaster.js'), /function onLocksChanged\(\) \{[\s\S]{0,600}ScanStage\.onDraftChanged\(\)/],
            [this._file('treeViewerUI.js'), /SpellTreeSave\.send\(data, 'import'\)\) \{[\s\S]{0,300}ScanStage\.onDraftChanged\(\)/],
            [this._file('uiHelpers.js'), /ScanStage\.refresh\(\)/], [this._file('buttonHandlers.js'), /ScanStage\.refresh\(\)/]];
        this.check(hooks.every(function(h) { return h[1].test(h[0]); }), 'Scan markup: the draft, its locks, Apply, Import, a new scan, the filters and the scan button tell ScanStage');
        var css = this._file('../panel-scan.css');
        this.check(/\.scan-relay-target \{ display: none; \}/.test(css) && !/border-right: 1px/.test(css),
            'Scan CSS: relays out of sight, no vertical rule added');
    },

    // ======================================================================= item 9

    _treeFrame: function() {
        var self = this;
        var btn = this.el('button', { id: 'tree-legend-btn', 'aria-expanded': 'false' });
        var legend = this.el('div', { id: 'tree-legend', 'class': 'tree-legend hidden' });
        var box = this._sandbox(['treeLegend.js']), T = box.TreeLegend, doc = this.doc([btn, legend]);
        T._doc = function() { return doc; };
        T.init();
        this.check(!T.isOpen(), 'Legend: closed at first (the footer shows only "?")');
        btn.click();
        this.check(T.isOpen() && btn.getAttribute('aria-expanded') === 'true' && btn.classList.contains('active'), 'Legend: a click on "?" opens the key');
        btn.click();
        this.check(!T.isOpen() && btn.getAttribute('aria-expanded') === 'false', 'Legend: a second click closes it');
        T.toggle(true);
        T.close();
        this.check(!T.isOpen(), 'Legend: close() (a tab switch) shuts it');

        var html = this._file('../index.html');
        var footer = html.substring(html.indexOf('class="tree-footer"'), html.indexOf('id="contentSettings"'));
        var key = footer.substring(footer.indexOf('id="tree-legend"'));
        this.check(/id="tree-status-text"/.test(footer) && footer.indexOf('id="tree-status-text"') < footer.indexOf('id="tree-legend-btn"'),
            'Footer: the status line stays in it, the "?" beside it');
        this.check(footer.split('class="state-legend"').length === 2 && key.indexOf('class="state-legend"') !== -1 &&
            /<button type="button" id="tree-legend-btn"[^>]*aria-controls="tree-legend"/.test(footer),
            'Footer: the node states key only behind "?" (a button that says what it opens)');
        this.check(/id="renderer-badge" class="renderer-badge canvas dev-only hidden"/.test(html), 'Tools: the renderer read-out only in Developer Mode');
        this.check(/TreeLegend\.close\(\)/.test(this._file('../script.js')) && /TreeLegend\.init\(\)/.test(this._file('main.js')),
            'Legend: wired at start, closed by a tab switch');
        this.check(/tab\.style\.color = schoolColor;/.test(this._file('treeNav.js')), 'School jumps: labels in their school colour (treeNav.js)');
        var sheets = ['design-arcane.css', 'design-darkbook.css'].map(function(f) { return self._file('../themes/' + f); });
        this.check(sheets.every(function(css) {
            return !/0 0 0 3px|0 0 0 4px|inset 0 0 22px/.test(css) && /\.tree-container\.canvas-shown \{\s*box-shadow: none;\s*outline: 0;/.test(css);
        }), 'Frame: Arcane and the dark books keep their edge colour as one line (no gutter ring, shadow or second outline)');
        var css = this._file('../panel-tree.css');
        this.check(/#contentSpellTree \.tree-top-tools \.zoom-controls \{\s*background: transparent;\s*border: 0;/.test(css) &&
            /#contentSpellTree \.tree-footer \{[^}]*background: transparent;[^}]*border-top: 0;/.test(css),
            'Frame: the tool bar has no box, the footer no band');
        var status = this._file('treeStatusLine.js');
        this.check(/getElementById\('tree-status-text'\)/.test(status), 'Status: TreeStatusLine still writes the footer status (its rules: panelTidyTest)');
        this.check(/hide: function\(\) \{[^}]*TreeToolsScale\.setCornerArt\(false\)/.test(this._file('canvasRendererV2.js')),
            'Tools: the canvas taken away (CanvasRenderer.hide) puts them back in their corner, not left beside corner drawings no longer drawn');
        var arcane = this._file('../themes/design-arcane.css');
        this.check(/#howto-tab \{\s*--accent-gold: #C9A25A;\s*--accent-gold-hover: #DDB86C;/.test(arcane),
            'Arcane: the How to learn tab takes the gold of the leather back (inside .howto-panel the golds are ink, brown on brown)');
    }
};

if (typeof window !== 'undefined') window.PanelLayoutTest = PanelLayoutTest;
if (typeof global !== 'undefined') global.PanelLayoutTest = PanelLayoutTest;
