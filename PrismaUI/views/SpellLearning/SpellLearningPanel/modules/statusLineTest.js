/**
 * StatusLineTest - node tests for the status lines over a language switch
 * (run-tests.js).
 *
 * A stand-in page with the scan status bar and the builder status line inside
 * its language string, as index.html has them, and two languages. Checks that
 * the Easy page copy follows TreeGrowth.setStatusText, that a line written
 * with its key comes back in the new language after a switch (and one without
 * a key as written), that a cleared tree goes back to the idle line, and that
 * updateScanStatus keeps or rewrites its text by whether it had a key.
 *
 * Depends on: treeGrowthStatus.js (TreeGrowthStatus), easyMode.js
 * (_syncEasyStatus) and uiHelpers.js (updateScanStatus, relabelScanStatus) -
 * the last two are function declarations, so under node this file runs them
 * into the global scope the way the page's script tags do. Swaps the globals
 * `document` and `t` for its own while it runs.
 */

var StatusLineTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    STRINGS: {
        en: {
            'scanner.complexStatusWrap': 'Status: <span id="tgStatus">Waiting for scan...</span>',
            'easyMode.statusWrap': 'Status: <span id="easyStatus">Waiting for scan...</span>',
            'treeGrowth.treeBuilt': 'Tree built',
            'treeGrowth.nodesPlaced': '{{placed}}/{{total}} nodes placed',
            'buildProgress.statusApplied': 'Tree applied ({{count}} positioned)',
            'status.treeBuildComplete': 'Tree built ({{schools}} schools, {{spells}} spells)'
        },
        ko: {
            'scanner.complexStatusWrap': '상태: <span id="tgStatus">스캔 대기 중...</span>',
            'easyMode.statusWrap': '상태: <span id="easyStatus">스캔 대기 중...</span>',
            'treeGrowth.treeBuilt': '트리 구축 완료',
            'treeGrowth.nodesPlaced': '{{placed}}/{{total}} 노드 배치됨',
            'buildProgress.statusApplied': '트리 적용됨 ({{count}}개 배치)',
            'status.treeBuildComplete': '트리 구축 완료 ({{schools}}개 학파, {{spells}}개 주문)'
        }
    },

    /** A page: ids to elements; a wrap's innerHTML makes its span again, as the browser does. */
    _page: function() {
        var byId = {};
        var make = function(id) {
            var el = {
                id: id, textContent: '', className: '', style: { color: '' }, parentNode: null, _attrs: {},
                getAttribute: function(n) { return this._attrs.hasOwnProperty(n) ? this._attrs[n] : null; },
                setAttribute: function(n, v) { this._attrs[n] = String(v); },
                removeAttribute: function(n) { delete this._attrs[n]; },
                classList: { add: function(c) { el.className += ' ' + c; } }
            };
            if (id) byId[id] = el;
            return el;
        };
        var wrap = function(key) {
            var w = make(null);
            w.setAttribute('data-i18n-html', key);
            Object.defineProperty(w, 'innerHTML', { set: function(html) {
                var m = /id="([^"]+)">([^<]*)</.exec(html);
                var span = make(m[1]);
                span.textContent = m[2];
                span.parentNode = w;
            } });
            return w;
        };
        var page = {
            byId: byId,
            tgWrap: wrap('scanner.complexStatusWrap'),
            easyWrap: wrap('easyMode.statusWrap'),
            document: { getElementById: function(id) { return byId[id] || null; } }
        };
        make('scanStatusBar');
        make('scanStatusText').setAttribute('data-i18n', 'scanner.readyToScan');
        return page;
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = ['easyMode.js', 'uiHelpers.js'];
        for (var i = 0; i < files.length; i++) {
            var name = files[i] === 'easyMode.js' ? '_syncEasyStatus' : 'relabelScanStatus';
            if (typeof g[name] === 'function') continue;
            var file = path.join(__dirname, files[i]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var S = g.TreeGrowthStatus;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (!S || typeof g._syncEasyStatus !== 'function' || typeof g.relabelScanStatus !== 'function') {
            this.check(false, 'TreeGrowthStatus, easyMode.js and uiHelpers.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var self = this;
        var oldDocument = g.document, oldT = g.t;
        var lang = 'en';
        var page = this._page();
        g.document = page.document;
        g.t = function(key, params) {
            var s = self.STRINGS[lang][key];
            if (s === undefined) return key;
            for (var p in (params || {})) s = s.replace(new RegExp('\\{\\{' + p + '\\}\\}', 'g'), params[p]);
            return s;
        };
        // What a switch does to the two wraps: applyI18nToDOM sets their innerHTML
        var switchTo = function(code) {
            lang = code;
            page.tgWrap.innerHTML = g.t('scanner.complexStatusWrap');
            page.easyWrap.innerHTML = g.t('easyMode.statusWrap');
        };
        var tg = function() { return page.byId.tgStatus.textContent; };
        var easy = function() { return page.byId.easyStatus.textContent; };
        var bar = function() { return page.byId.scanStatusText; };
        var grow = S.mixInto({ _nodeCount: 10, _totalPool: 20 });
        try {
            switchTo('en');
            grow.setStatusText('probe', 'done');
            this.check(easy() === 'probe' && page.byId.easyStatus.style.color === S.STATUS_COLORS.done,
                'setStatusText: the Easy page copies the text and the colour');

            grow.setStatusText(g.t('buildProgress.statusApplied', {count: 7}), 'done', 'buildProgress.statusApplied', {count: 7});
            switchTo('ko');
            this.check(tg() === '스캔 대기 중...', 'the switch rebuilds the line as the idle text');
            grow.relabelStatus();
            this.check(tg() === '트리 적용됨 (7개 배치)' && easy() === tg(),
                'a keyed line comes back in the new language, with its values, on both pages');

            grow.showBuilt();
            switchTo('en'); grow.relabelStatus();
            this.check(tg() === 'Tree built — 10/20 nodes placed', 'the built line comes back in the new language');

            grow.setStatusText('Tree saved', 'done');
            switchTo('ko'); grow.relabelStatus();
            this.check(tg() === 'Tree saved', 'a line with no key comes back as written');

            grow.showBuilt();
            grow.resetStatus();
            this.check(tg() === '스캔 대기 중...' && easy() === '스캔 대기 중...', 'a cleared tree: both lines idle, current language');
            switchTo('en'); grow.relabelStatus();
            this.check(tg() === 'Waiting for scan...', '...and a switch after it does not bring "Tree built" back');

            var params = {schools: 5, spells: 1428};
            g.updateScanStatus(g.t('status.treeBuildComplete', params), 'success', 'status.treeBuildComplete', params);
            this.check(bar().getAttribute('data-i18n') === null && bar().textContent === 'Tree built (5 schools, 1428 spells)',
                'updateScanStatus: the text written, the bar\'s own data-i18n gone');
            lang = 'ko'; g.relabelScanStatus();
            this.check(bar().textContent === '트리 구축 완료 (5개 학파, 1428개 주문)' &&
                page.byId.scanStatusBar.className.indexOf('success') !== -1,
                'a keyed scan message comes back in the new language, values and colour kept');
            g.updateScanStatus('Saved from C++', '');
            lang = 'en'; g.relabelScanStatus();
            this.check(bar().textContent === 'Saved from C++', 'a scan message with no key is left as written');
        } finally {
            g.document = oldDocument;
            g.t = oldT;
            if (typeof g.updateScanStatus === 'function') g._scanStatus = null;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.StatusLineTest = StatusLineTest;
if (typeof global !== 'undefined') global.StatusLineTest = StatusLineTest;
