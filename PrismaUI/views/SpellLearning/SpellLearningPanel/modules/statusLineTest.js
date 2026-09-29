/**
 * StatusLineTest - node tests for the status lines over a language switch
 * (run-tests.js).
 *
 * A stand-in page with the scan status bar and the builder status line inside
 * its language string, as index.html has them, and two languages. Checks that
 * the Easy page copy follows TreeGrowth.setStatusText, that a line written
 * with its key comes back in the new language after a switch (and one without
 * a key as written), that a cleared tree goes back to the idle line, and that
 * updateScanStatus keeps or rewrites its text by whether it had a key. Also
 * the spell card's Magicka as the node takes it (TreeParser.updateNodeFromCache).
 *
 * Depends on: treeGrowthStatus.js (TreeGrowthStatus), easyMode.js
 * (_syncEasyStatus), uiHelpers.js (updateScanStatus, relabelScanStatus), cppCallbacks.js
 * (updateStatus) and treeParser.js (TreeParser) - under node this file runs those into the
 * global scope the way the page's script tags do. Swaps the globals
 * `document`, `t` and `SpellCache` for its own while it runs.
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
            'status.treeBuildComplete': 'Tree built ({{schools}} schools, {{spells}} spells)',
            'status.scanFailed': 'Scan failed: {{error}}',
            'status.scannedSpellsSchools': '{{count}} spells scanned across {{schools}} schools',
            'buttons.scanSpells': 'Scan Spells'
        },
        ko: {
            'scanner.complexStatusWrap': '상태: <span id="tgStatus">스캔 대기 중...</span>',
            'easyMode.statusWrap': '상태: <span id="easyStatus">스캔 대기 중...</span>',
            'treeGrowth.treeBuilt': '트리 구축 완료',
            'treeGrowth.nodesPlaced': '{{placed}}/{{total}} 노드 배치됨',
            'buildProgress.statusApplied': '트리 적용됨 ({{count}}개 배치)',
            'status.treeBuildComplete': '트리 구축 완료 ({{schools}}개 학파, {{spells}}개 주문)',
            'status.scanFailed': '스캔 실패: {{error}}',
            'status.scannedSpellsSchools': '{{schools}}개 학파에서 {{count}}개 주문 스캔됨',
            'buttons.scanSpells': '주문 스캔'
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
        make('scanBtn');
        var spawn = make('spawn-spell-list');
        spawn.waiting = make(null);
        spawn.querySelector = function(sel) { return sel === '.spawn-loading' ? this.waiting : null; };
        make('scanStatusText').setAttribute('data-i18n', 'scanner.readyToScan');
        return page;
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['easyMode.js', '_syncEasyStatus'], ['uiHelpers.js', 'relabelScanStatus'], ['treeParser.js', 'TreeParser'], ['cppCallbacks.js', 'updateStatus']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
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
            for (var p in (params || {})) s = s.replace(new RegExp('\\{\\{' + p + '\\}\\}', 'g'), function() { return String(params[p]); });
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
            page.byId.tgStatus.textContent = 'untouched';
            grow.relabelStatus();
            this.check(tg() === 'untouched' && easy() === 'untouched',
                'relabelStatus with nothing written yet: the line stays, the Easy page copies it');

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
            this._checkScanFailed(g, page, function(code) { lang = code; });
            this._checkCost(g);
        } finally {
            g.document = oldDocument;
            g.t = oldT;
            if (typeof g.updateScanStatus === 'function') g._scanStatus = null;
        }
        return { passed: this.passed, failed: this.failed };
    },

    /** A scan that threw: the Scan button comes back and the bar says why, in the current language. */
    _checkScanFailed: function(g, page, setLang) {
        var btn = page.byId.scanBtn, bar = page.byId.scanStatusText;
        setLang('en');
        btn.disabled = true; btn.innerHTML = 'Scanning...';
        // C++ sends {mode, reason}; a system message can carry non-Latin text
        var reason = 'No mapping for the Unicode character exists in the target multi-byte code page';
        g.onScanFailed(JSON.stringify({ mode: 'all', reason: reason }));
        this.check(btn.disabled === false && btn.innerHTML.indexOf('Scan Spells') !== -1,
            'onScanFailed (all): the Scan button is enabled again');
        this.check(bar.textContent === 'Scan failed: ' + reason &&
            page.byId.scanStatusBar.className.indexOf('error') !== -1,
            'onScanFailed (all): the bar names the reason, as an error');
        setLang('ko'); g.relabelScanStatus();
        this.check(bar.textContent.indexOf('스캔 실패: ') === 0, 'onScanFailed: the message comes back in the new language');
        setLang('en');
        // The old payload shapes: a JSON-quoted string and bare text are a full scan's reason
        g.onScanFailed('"' + reason + '"');
        this.check(bar.textContent === 'Scan failed: ' + reason, 'onScanFailed: an old JSON-quoted string still works');
        g.onScanFailed('unquoted reason');
        this.check(bar.textContent === 'Scan failed: unquoted reason', 'onScanFailed: a reason that is not JSON is used as it is');
        // Edit mode's wait line ends with the failure of a full scan
        var wait = page.byId['spawn-spell-list'].waiting;
        wait.textContent = 'Scanning game spells...';
        g.onScanFailed(JSON.stringify({ mode: 'all', reason: 'boom' }));
        this.check(wait.textContent === 'Scan failed: boom', "onScanFailed (all): edit mode's wait line says the scan failed");
        // Korean text and markup: kept literally (textContent, never parsed as HTML)
        var text = '스캔 폴더 화염.ini <b>x</b>';
        g.onScanFailed(JSON.stringify({ mode: 'all', reason: text }));
        this.check(bar.textContent === 'Scan failed: ' + text, 'onScanFailed: Korean text and "<b>x</b>" held literally in textContent');
        // $ patterns in a reason reach the bar as written (the fake t() uses a replacer function too)
        g.onScanFailed(JSON.stringify({ mode: 'all', reason: 'cost $& of $1 and $$' }));
        this.check(bar.textContent === 'Scan failed: cost $& of $1 and $$', 'onScanFailed: a reason with $ patterns, end to end');
        // A tome scan, in the real order: the good message, C++'s keyless "Scanning spell tomes...",
        // then the failure. The good message and its colour come back, keyed; the stale tome list is dropped
        var oldState = g.state;
        g.state = { lastSpellData: { spellCount: 1428, spells: [{ school: 'Destruction' }, { school: 'Illusion' }, { school: 'Destruction' }] },
            tomedSpellIds: { '0x1': true } };
        var cur = 'en';
        var scanned = function() { return cur === 'ko' ? '2개 학파에서 1428개 주문 스캔됨' : '1428 spells scanned across 2 schools'; };
        try {
            g.updateScanStatus(g.t('status.scannedSpellsSchools', { count: 1428, schools: 2 }), 'success',
                'status.scannedSpellsSchools', { count: 1428, schools: 2 });
            var goodClass = page.byId.scanStatusBar.className;
            g.updateStatus('"Scanning spell tomes..."');
            this.check(bar.textContent === 'Scanning spell tomes...', 'the tome scan starts: C++ writes its keyless line over the bar');
            btn.disabled = true; btn.innerHTML = 'Scanning...';
            wait.textContent = 'Scanning game spells...';
            var warn = console.warn, warned = 0;
            console.warn = function() { warned++; };
            try { g.onScanFailed(JSON.stringify({ mode: 'tomes', reason: 'boom' })); } finally { console.warn = warn; }
            this.check(bar.textContent === scanned() && page.byId.scanStatusBar.className === goodClass,
                'onScanFailed (tomes): the scanned message and its colour are back');
            this.check(warned === 1 && wait.textContent === 'Scanning game spells...' && btn.disabled === true,
                'onScanFailed (tomes): a console warning; the wait line and the button (a tome scan disables nothing) stay');
            this.check(g.state.tomedSpellIds === null, 'onScanFailed (tomes): the stale tomed-spell list is dropped (the tome filter is off)');
            setLang('ko'); cur = 'ko'; g.relabelScanStatus();
            this.check(bar.textContent === scanned(), 'onScanFailed (tomes): the restored message follows a language switch');
            setLang('en'); cur = 'en';
            g.state.lastSpellData = null;
            g.updateStatus('"Scanning spell tomes..."');
            console.warn = function() {};
            try { g.onScanFailed(JSON.stringify({ mode: 'tomes', reason: 'boom' })); } finally { console.warn = warn; }
            this.check(bar.textContent === 'Scanning spell tomes...', 'onScanFailed (tomes): with no scan data the bar is left as it is');
        } finally {
            g.state = oldState;
        }
        btn.disabled = false;
        this._checkParamDollar();
        setLang('en');
    },

    /** i18n.js's t(): a value with $ patterns (an error text) goes into {{error}} as it is. */
    _checkParamDollar: function() {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var win = { _i18nPreload: { '_meta.locale': 'en', 'status.scanFailed': 'Scan failed: {{error}}' } };
        var sandbox = { window: win, document: { documentElement: { setAttribute: function() {} } },
            console: { log: function() {}, warn: function() {}, error: function() {} } };
        try {
            var file = path.join(__dirname, 'i18n.js');
            vm.runInNewContext(fs.readFileSync(file, 'utf8'), sandbox, { filename: file });
            win.initI18n('en');
            var reason = 'cost $& of $1 and $$ and $` here';
            this.check(win.t('status.scanFailed', { error: reason }) === 'Scan failed: ' + reason,
                'i18n t(): a value with $&, $1, $$ is inserted as written');
        } catch (e) {
            this.check(false, 'i18n t() checked: ' + e.message);
        }
    },

    /** The card's Magicka: whole points, at least 1 for any cost, 0 for none */
    _checkCost: function(g) {
        var P = g.TreeParser;
        if (!P) { this.check(false, 'TreeParser loaded'); return; }
        var oldCache = g.SpellCache;
        var data = {};
        g.SpellCache = { get: function() { return data; } };
        var cost = function(d) { data = d; var n = { formId: '0x1' }; P.updateNodeFromCache(n); return n.cost; };
        try {
            this.check(cost({ cost: 307.7013549804 }) === 308 && cost({ magickaCost: '312.3168334960' }) === 312,
                'Magicka: a float cost shows whole points (the cost field or its magickaCost alias)');
            this.check(cost({ cost: 0.3 }) === 1 && cost({ cost: 0.5 }) === 1, 'Magicka: a cost under a point shows 1, not "?"');
            this.check(cost({ cost: -4 }) === 0 && cost({ cost: 'abc' }) === 0 && cost({}) === 0,
                'Magicka: a negative, unreadable or missing cost is 0');
        } finally {
            g.SpellCache = oldCache;
        }
    }
};

if (typeof window !== 'undefined') window.StatusLineTest = StatusLineTest;
if (typeof global !== 'undefined') global.StatusLineTest = StatusLineTest;
