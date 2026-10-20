/**
 * TreeLegendTest - click disclosure, status retention, school labels and
 * text-palette contrast. Uses isolated browser globals so other suites are unchanged.
 */
var TreeLegendTest = {
    passed: 0,
    failed: 0,

    check: function(ok, label) {
        if (ok) { this.passed++; console.log('  [PASS] ' + label); }
        else { this.failed++; console.log('  [FAIL] ' + label); }
    },

    _file: function(name) {
        return require('fs').readFileSync(require('path').join(__dirname, name), 'utf8');
    },

    _element: function() {
        var attrs = {}, handlers = {};
        return {
            textContent: '', hidden: false, style: {}, handlers: handlers, children: [],
            setAttribute: function(key, value) { attrs[key] = String(value); },
            getAttribute: function(key) { return attrs[key]; },
            removeAttribute: function(key) { delete attrs[key]; },
            addEventListener: function(key, callback) {
                if (!handlers[key]) handlers[key] = [];
                handlers[key].push(callback);
            },
            click: function() { (handlers.click || []).forEach(function(fn) { fn(); }); },
            appendChild: function(child) { this.children.push(child); },
            querySelectorAll: function() { return this.children; },
            classList: { toggle: function() {} }
        };
    },

    _contrast: function(foreground, background) {
        function luminance(hex) {
            var channels = [1, 3, 5].map(function(offset) {
                var channel = parseInt(hex.substr(offset, 2), 16) / 255;
                return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
            });
            return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
        }
        var a = luminance(foreground), b = luminance(background);
        return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        var vm = require('vm'), self = this;
        var button = this._element(), legend = this._element(), status = this._element();
        var schoolBar = this._element();
        var elements = { 'tree-legend-btn': button, 'tree-legend': legend,
            'tree-status-text': status, 'tree-school-tabs': schoolBar };
        var timers = [], clock = 10000;
        var box = {
            document: { getElementById: function(id) { return elements[id] || null; },
                createElement: function() { return self._element(); } },
            setTimeout: function(fn) { timers.push(fn); return timers.length; },
            clearTimeout: function(id) { timers[id - 1] = null; },
            Date: { now: function() { return clock; } },
            setI18n: function(el, key, params, fallback) { el.textContent = fallback || key; },
            clearI18n: function() {},
            addEventListener: function() {},
            console: { log: function() {} },
            settings: {},
            t: function(key) {
                if (key === 'chips.school.destruction') return '파괴마법';
                if (key === 'tree.schoolShort.destruction') return '파괴';
                return key;
            },
            CanvasRenderer: {
                schools: { Destruction: { spokeAngle: 0 } },
                _getSchoolColor: function() { return '#123456'; }
            }
        };
        box.window = box;
        vm.createContext(box);
        vm.runInContext(this._file('treeLegend.js'), box);
        vm.runInContext(this._file('treeStatusLine.js'), box);
        this.check(legend.hidden && button.getAttribute('aria-expanded') === 'false',
            'Legend starts folded with matching accessible state');
        button.click();
        this.check(!legend.hidden && button.getAttribute('aria-expanded') === 'true',
            'A click opens the legend without hover or Tab');
        button.click();
        this.check(legend.hidden, 'A second click closes the legend');
        box.TreeLegend.init();
        this.check(button.handlers.click.length === 1, 'Repeated init does not add duplicate click handlers');

        box.setTreeStatusKey('status.loadedSpells', {}, 'Loaded 12 spells');
        button.click();
        this.check(status.textContent === 'Loaded 12 spells' && !status.hidden,
            'Opening legend leaves the successful status visible');
        timers.forEach(function(fn) { if (fn) fn(); });
        this.check(status.textContent === 'Ready', 'Successful status still clears to Ready');
        box.setTreeStatusKey('status.warningUnreachable', {}, 'Warning: unreachable spells');
        button.click();
        box.setTreeStatusKey('status.treeSaved', {}, 'Tree saved');
        this.check(status.textContent === 'Warning: unreachable spells' && !status.hidden,
            'Closing legend preserves warning against follow-up save success');
        box.setTreeStatus('Import failed');
        button.click();
        this.check(status.textContent === 'Import failed' && !status.hidden,
            'Plain DLL error remains visible with legend open');

        vm.runInContext(this._file('treeNav.js'), box);
        box.TreeNav.buildSchoolTabs();
        this.check(schoolBar.children[0].textContent === '파괴' &&
            schoolBar.children[0].getAttribute('aria-label') === '파괴마법',
            'School text keeps translated short label and full accessible name');
        box.TreeNav.setActiveSchool('Destruction');
        this.check(schoolBar.children[0].getAttribute('aria-pressed') === 'true',
            'Selected school is expressed accessibly as well as visually');

        var css = this._file('../panelFrames.css');
        var palettes = {
            classic: '#171717', modern: '#11192d', 'night-grimoire': '#2a2448',
            chalkboard: '#34403b', skald: '#1e1d1a'
        };
        // Read the actual new school tokens, not a second copy of their values.
        var tokenGroups = css.split('--panel-school-destruction:');
        var darkColors = ('--panel-school-destruction:' + tokenGroups[1]).match(/--panel-school-\w+: #[0-9a-f]{6}/g);
        var lightColors = ('--panel-school-destruction:' + tokenGroups[2]).match(/--panel-school-\w+: #[0-9a-f]{6}/g);
        Object.keys(palettes).forEach(function(design) {
            var minimum = Math.min.apply(Math, darkColors.map(function(token) {
                return self._contrast(token.slice(-7), palettes[design]);
            }));
            self.check(minimum >= 4.5, design + ' school labels have at least 4.5:1 contrast (' + minimum.toFixed(2) + ')');
        });
        var arcaneMinimum = Math.min.apply(Math, lightColors.map(function(token) {
            return self._contrast(token.slice(-7), '#dccb9f');
        }));
        this.check(arcaneMinimum >= 4.5, 'Arcane school ink has at least 4.5:1 contrast (' + arcaneMinimum.toFixed(2) + ')');
        this.check(/#tree-status-text\s*\{[^}]*white-space:\s*normal/.test(css) &&
            !/#tree-status-text\s*\{[^}]*display:\s*none/.test(css),
            'Long status messages wrap and are never folded with legend');
        this.check(/#tree-legend\[hidden\]\s*\{\s*display:\s*none/.test(css),
            'Folded legend wins against design display rules');
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TreeLegendTest = TreeLegendTest;
