/**
 * I18nRelabelTest - node tests for text a script writes with setI18n (i18n.js):
 * a language switch (initI18n + applyI18nToDOM, what switchLocale runs) writes it
 * again in the new language, with its {{values}}, as text or as a title; text
 * written after clearI18n stays as written; a key the language lacks shows the
 * English fallback. And no element in index.html that a switch rewrites (data-i18n)
 * holds another element, which the rewrite would drop.
 *
 * Depends on: i18n.js - run in its own sandbox with a few fake elements; index.html.
 */

var I18nRelabelTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _element: function() {
        var attrs = {};
        return {
            textContent: '',
            setAttribute: function(n, v) { attrs[n] = String(v); if (n === 'title') this.title = String(v); },
            getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; },
            removeAttribute: function(n) { delete attrs[n]; },
            hasAttribute: function(n) { return attrs.hasOwnProperty(n); }
        };
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var els = [this._element(), this._element(), this._element()];
        var doc = {
            documentElement: { setAttribute: function() {} },
            querySelectorAll: function(sel) {
                var name = sel.replace(/^\[|\]$/g, '');
                return els.filter(function(e) { return e.hasAttribute(name); });
            }
        };
        var win = { _i18nPreload: { '_meta.locale': 'en', 'a.count': '{{count}} spells', 'a.tip': 'Full: {{v}}' } };
        var quiet = function() {};
        var sandbox = { window: win, document: doc, console: { log: quiet, warn: quiet, error: quiet } };
        try {
            var file = path.join(__dirname, 'i18n.js');
            vm.runInNewContext(fs.readFileSync(file, 'utf8'), sandbox, { filename: file });
            win.initI18n('en');
            win.setI18n(els[0], 'a.count', { count: 3 }, '{{count}} spells');
            win.setI18n(els[1], 'a.tip', { v: 7 }, 'Full: {{v}}', 'title');
            win.setI18n(els[2], 'a.count', { count: 1 }, '{{count}} spells');
            win.clearI18n(els[2]);
            els[2].textContent = 'written by hand';
            this.check(els[0].textContent === '3 spells' && els[1].title === 'Full: 7', 'setI18n writes the text and the title');

            win._i18nPreload = { '_meta.locale': 'ko', 'a.count': '주문 {{count}}개', 'a.tip': '최대: {{v}}' };
            win.initI18n('ko');
            win.applyI18nToDOM();
            this.check(els[0].textContent === '주문 3개', 'a switch writes the text again with its values');
            this.check(els[1].title === '최대: 7', 'a switch writes the title again with its values');
            this.check(els[2].textContent === 'written by hand', 'text written after clearI18n stays as written');

            var missing = this._element();
            win.setI18n(missing, 'a.new', { n: 2 }, 'New {{n}}');
            this.check(missing.textContent === 'New 2', 'a key the language lacks shows the English fallback');
        } catch (e) {
            this.check(false, 'i18n.js relabel checked: ' + e.message);
        }
        this._markup(fs, path);
        return { passed: this.passed, failed: this.failed };
    },

    /**
     * applyI18nToDOM writes a data-i18n element's textContent, which drops every
     * element inside it: the spell card's "(need 2 of 3)" count and LOCK badge
     * never showed while they sat inside their labels. data-i18n-html is meant
     * to have its children replaced, so it is not checked.
     */
    _markup: function(fs, path) {
        var html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
        // The element's own text up to the next tag; a tag that is not a closing one is a child
        var re = /<([a-z0-9]+)\b[^>]*\sdata-i18n="([^"]*)"[^>]*>[^<]*<(?!\/)/g, m, nested = [];
        while ((m = re.exec(html))) nested.push(m[2]);
        this.check(nested.length === 0, 'no element in index.html a translation rewrites holds another element' +
            (nested.length ? ' (' + nested.join(', ') + ')' : ''));
        this.check(/<span id="soft-needed-count">/.test(html) && /class="lock-label"/.test(html),
            'the optional count and the LOCK badge are still in index.html');
    }
};

if (typeof window !== 'undefined') window.I18nRelabelTest = I18nRelabelTest;
if (typeof global !== 'undefined') global.I18nRelabelTest = I18nRelabelTest;
