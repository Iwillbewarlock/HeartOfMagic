/**
 * LanguageRefreshTest - node tests for what a live language switch writes again
 * (refreshScriptTexts, languageSetting.js), for the texts scripts build:
 * - the tree's school tabs (TreeNav.relabelSchoolTabs): the short names and their
 *   tooltips in the new language; a modded school with no translation keeps its
 *   own name; the tab of the selected spell's school stays marked; no tabs, none built
 * - the Scan page's school list (updatePrimedCount): the full names in the new
 *   language, a modded school as it is
 * - the tree is asked to repaint, so the chapter titles on its canvas follow
 *
 * Depends on: i18n.js, spellSearch.js, uiHelpers.js, treeNav.js,
 * languageSetting.js - run in a sandbox of their own with stand-in elements.
 */

var LanguageRefreshTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** An element with the few things the scripts touch. */
    _element: function() {
        var attrs = {};
        var classes = {};
        var el = {
            children: [], style: {}, textContent: '', title: '', _html: '',
            setAttribute: function(n, v) { attrs[n] = String(v); },
            getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; },
            removeAttribute: function(n) { delete attrs[n]; },
            hasAttribute: function(n) { return attrs.hasOwnProperty(n); },
            addEventListener: function() {},
            appendChild: function(c) { this.children.push(c); return c; },
            querySelectorAll: function() { return this.children.slice(); },
            classList: {
                toggle: function(c, on) { if (on === undefined ? !classes[c] : on) classes[c] = true; else delete classes[c]; },
                contains: function(c) { return !!classes[c]; },
                add: function(c) { classes[c] = true; },
                remove: function(c) { delete classes[c]; }
            }
        };
        Object.defineProperty(el, 'innerHTML', {
            get: function() { return this._html; },
            set: function(v) { this._html = String(v); this.children = []; }
        });
        return el;
    },

    _sandbox: function(els) {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var self = this;
        var quiet = function() {};
        var doc = {
            documentElement: { setAttribute: function() {} },
            getElementById: function(id) { return els[id] || null; },
            createElement: function() { return self._element(); },
            querySelectorAll: function() { return []; }
        };
        var box = {
            console: { log: quiet, warn: quiet, error: quiet },
            document: doc,
            addEventListener: function() {},
            settings: { schoolVisibility: {} },
            state: { selectedNode: null, lastSpellData: null },
            _i18nPreload: {
                '_meta.locale': 'en',
                'chips.school.alteration': 'Alteration', 'chips.school.destruction': 'Destruction',
                'tree.schoolShort.alteration': 'Alter.', 'tree.schoolShort.destruction': 'Destr.',
                'tree.schoolTabsHint': 'Jump to school'
            }
        };
        box.window = box;
        ['i18n.js', 'spellSearch.js', 'uiHelpers.js', 'treeNav.js', 'languageSetting.js'].forEach(function(f) {
            vm.runInNewContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), box, { filename: f });
        });
        box.initI18n('en');
        box.CanvasRenderer = {
            schools: { Alteration: { spokeAngle: -90 }, Destruction: { spokeAngle: 30 }, 'Mystic Arts': { spokeAngle: 150 } },
            _getSchoolColor: function() { return '#888'; },
            canvas: {}, _needsRender: false
        };
        return box;
    },

    _toKorean: function(box) {
        box._i18nPreload = {
            '_meta.locale': 'ko',
            'chips.school.alteration': '변이 마법', 'chips.school.destruction': '파괴 마법',
            'tree.schoolShort.alteration': '변이', 'tree.schoolShort.destruction': '파괴',
            'tree.schoolTabsHint': '학파로 이동'
        };
        box.initI18n('ko');
        box.refreshScriptTexts();
    },

    _tabs: function(bar) {
        var out = {};
        bar.children.forEach(function(c) { out[c.getAttribute('data-school')] = c; });
        return out;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('LanguageRefreshTest');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        try {
            var bar = this._element();
            var list = this._element();
            var box = this._sandbox({ 'tree-school-tabs': bar, 'scanSchoolBreakdown': list, 'statPrimedSpells': this._element() });
            box.TreeNav.buildSchoolTabs();
            box.state.selectedNode = { school: 'Destruction' };
            box.TreeNav.setActiveSchool('Destruction');
            var tabs = this._tabs(bar);
            this.check(tabs.Alteration.textContent === 'Alter.' && tabs['Mystic Arts'].textContent === 'Mysti.',
                'tabs in English first (a modded school cut to five letters)');

            box.state.lastSpellData = { spells: [{ school: 'Destruction' }, { school: 'Destruction' }, { school: 'Alteration' }, { school: 'Mystic Arts' }] };
            box.updatePrimedCount();
            this.check(list.innerHTML.indexOf('Destruction') !== -1, 'the school list in English first');

            this._toKorean(box);
            tabs = this._tabs(bar);
            this.check(tabs.Alteration.textContent === '변이' && tabs.Destruction.textContent === '파괴',
                'a switch writes the tabs again in the new language');
            this.check(tabs.Alteration.title.indexOf('변이 마법') === 0 && tabs.Alteration.title.indexOf('학파로 이동') !== -1,
                'and their tooltips');
            this.check(tabs['Mystic Arts'].textContent === 'Mysti.', 'a modded school with no translation keeps its own name');
            this.check(tabs.Destruction.classList.contains('active') && !tabs.Alteration.classList.contains('active'),
                'the selected spell\'s school stays marked');
            this.check(list.innerHTML.indexOf('파괴 마법') !== -1 && list.innerHTML.indexOf('변이 마법') !== -1 &&
                       list.innerHTML.indexOf('Destruction') === -1, 'a switch writes the Scan page\'s school list again');
            this.check(list.innerHTML.indexOf('Mystic Arts') !== -1, 'the list keeps a modded school\'s own name');
            this.check(box.CanvasRenderer._needsRender === true, 'the tree is asked to repaint (its chapter titles are on the canvas)');

            var emptyBar = this._element();
            var box2 = this._sandbox({ 'tree-school-tabs': emptyBar });
            this._toKorean(box2);
            this.check(emptyBar.children.length === 0, 'no tree yet: a switch builds no tabs');
        } catch (e) {
            this.check(false, 'language refresh checked: ' + (e && e.message ? e.message : e));
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.LanguageRefreshTest = LanguageRefreshTest;
if (typeof global !== 'undefined') global.LanguageRefreshTest = LanguageRefreshTest;
