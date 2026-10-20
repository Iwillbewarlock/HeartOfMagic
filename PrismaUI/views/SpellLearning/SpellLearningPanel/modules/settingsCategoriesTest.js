/** Settings navigation: real node movement, four categories and session memory. */
var SettingsCategoriesTest = {
    passed: 0,
    failed: 0,
    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },
    _element: function() {
        var el = {
            children: [], attributes: {}, style: {}, className: '', id: '',
            hidden: false, scrollTop: 0, listeners: {}, parentNode: null,
            setAttribute: function(k, v) { this.attributes[k] = v; },
            getAttribute: function(k) { return this.attributes[k]; },
            addEventListener: function(k, fn) { this.listeners[k] = fn; },
            appendChild: function(child) {
                if (child.parentNode) {
                    var old = child.parentNode.children;
                    old.splice(old.indexOf(child), 1);
                }
                child.parentNode = this;
                this.children.push(child);
                return child;
            },
            insertBefore: function(child, before) {
                this.appendChild(child);
                if (before) {
                    this.children.splice(this.children.indexOf(child), 1);
                    this.children.splice(this.children.indexOf(before), 0, child);
                }
            },
            querySelector: function(selector) {
                var key = selector.match(/^\[data-i18n="([^"]+)"\]$/);
                for (var i = 0; i < this.children.length; i++) {
                    var child = this.children[i];
                    if ((key && child.attributes['data-i18n'] === key[1]) ||
                        (selector.charAt(0) === '.' && child.classList.contains(selector.slice(1)))) return child;
                    var found = child.querySelector(selector);
                    if (found) return found;
                }
                return null;
            }
        };
        el.classList = {
            contains: function(name) { return (' ' + el.className + ' ').indexOf(' ' + name + ' ') >= 0; },
            add: function(name) { if (!this.contains(name)) el.className += ' ' + name; },
            toggle: function(name, on) {
                el.className = el.className.split(' ').filter(function(x) { return x !== name; }).join(' ');
                if (on) this.add(name);
            }
        };
        return el;
    },
    run: function() {
        var g = typeof global !== 'undefined' ? global : window;
        this.passed = 0; this.failed = 0;
        if (typeof g.SettingsCategories === 'undefined' && typeof require === 'function') {
            var file = require('path').join(__dirname, 'settingsCategories.js');
            require('vm').runInThisContext(require('fs').readFileSync(file, 'utf8'), { filename: file });
        }
        var module = g.SettingsCategories;
        var oldDocument = g.document;
        var oldResizeObserver = g.ResizeObserver;
        var saved = {
            viewport: module.viewport, panels: module.panels, buttons: module.buttons,
            selected: module.selected, scrollPositions: module.scrollPositions
        };
        try {
            var self = this, ids = {}, root = this._element(), blocks = [];
            ids.contentSettings = root;
            var split = this._element(); split.className = 'settings-split'; root.appendChild(split);
            var presets = this._element(); ids.settingsPresetsRow = presets; root.appendChild(presets);
            for (var i = 0; i < module.BLOCKS.length; i++) {
                var block = this._element(); block.className = 'settings-block';
                var title = this._element(); title.setAttribute('data-i18n', module.BLOCKS[i][0]);
                block.appendChild(title); blocks.push(block);
                (i < 3 ? split : root).appendChild(block);
            }
            var modded = this._element(); ids.moddedXPSourcesSection = modded; blocks[4].appendChild(modded);
            var control = this._element(), clicks = 0;
            control.value = 'saved'; control.addEventListener('click', function() { clicks++; });
            blocks[0].appendChild(control);
            var version = this._element(); ids.settingsVersion = version; root.appendChild(version);
            var footer = this._element(); footer.className = 'settings-actions-sticky'; root.appendChild(footer);
            g.document = {
                getElementById: function(id) { return ids[id] || null; },
                createElement: function() { return self._element(); },
                addEventListener: function() {}
            };
            g.ResizeObserver = undefined;
            module.viewport = null; module.panels = {}; module.buttons = {};
            module.scrollPositions = {}; module.selected = module.DEFAULT_CATEGORY;
            module.init();
            this.check(Object.keys(module.panels).length === 4, 'exactly four one-level categories');
            this.check(module.selected === 'learning' && !module.panels.learning.hidden, 'Learning opens initially');
            this.check(module.panels.display.hidden && module.panels.tomes.hidden && module.panels.advanced.hidden,
                'only the selected category is visible');
            var allMapped = true;
            for (var b = 0; b < blocks.length; b++) {
                if (blocks[b].parentNode !== module.panels[module.BLOCKS[b][1]]) allMapped = false;
            }
            this.check(allMapped, 'all eight existing sections land in their documented categories');
            this.check(modded.parentNode === module.panels.advanced, 'modded XP sources move to Advanced');
            control.listeners.click();
            this.check(clicks === 1 && control.value === 'saved', 'moving controls preserves handlers and current values');
            this.check(presets.parentNode === root && footer.parentNode === root, 'presets and Save/Reset remain common');
            this.check(split.style.display === 'none', 'empty two-column scaffold is hidden');
            module.viewport.scrollTop = 240;
            module.buttons.display.listeners.click();
            this.check(module.selected === 'display' && module.viewport.scrollTop === 0, 'category click changes section and starts at its top');
            module.viewport.scrollTop = 90;
            module.select('learning');
            this.check(module.viewport.scrollTop === 240 && module.scrollPositions.display === 90, 'each category remembers its own scroll');
            module.select('display');
            this.check(module.viewport.scrollTop === 90 && module.buttons.display.attributes['aria-selected'] === 'true',
                'return restores position and selected state');
            var viewport = module.viewport;
            module.init();
            this.check(module.viewport === viewport && module.selected === 'display' && module.viewport.scrollTop === 90,
                'panel reinitialization preserves session category and scroll');
            this.check(module.select('unknown') === false && module.selected === 'display', 'invalid category leaves current selection alone');
            if (typeof require === 'function') {
                var html = require('fs').readFileSync(require('path').join(__dirname, '../index.html'), 'utf8');
                var settingsHtml = html.split('id="contentSettings"')[1].split('id="resizeHandle"')[0];
                var headers = settingsHtml.match(/class="settings-block-title" data-i18n="([^"]+)"/g) || [];
                this.check(headers.length === module.BLOCKS.length && headers.every(function(header) {
                    return module.BLOCKS.some(function(entry) { return header.indexOf('"' + entry[0] + '"') >= 0; });
                }), 'every actual Settings section is covered; new unmapped sections fail this test');
            }
        } finally {
            g.document = oldDocument;
            g.ResizeObserver = oldResizeObserver;
            module.viewport = saved.viewport; module.panels = saved.panels; module.buttons = saved.buttons;
            module.selected = saved.selected; module.scrollPositions = saved.scrollPositions;
        }
        return { passed: this.passed, failed: this.failed };
    }
};
if (typeof module !== 'undefined' && module.exports) module.exports = SettingsCategoriesTest;
if (typeof window !== 'undefined') window.SettingsCategoriesTest = SettingsCategoriesTest;
