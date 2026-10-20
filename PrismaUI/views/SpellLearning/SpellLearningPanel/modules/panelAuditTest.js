/**
 * PanelAuditTest - node tests for the 2.0 panel audit pass (run-tests.js).
 *
 * Item 1, bad data and long words (German, Russian, a 60-character spell name, the 900 px default
 * panel): what the headless overflow check found, pinned where node can see it - the Settings page
 * in one column below SettingsCategories.ONE_COLUMN_PX, settings rows that wrap their control under
 * the text, no clamped description or "..." slider label, the category list as wide as its longest
 * name, the last power step's figures over two columns (no spacer cells), the spell card's chips as
 * wrapping inline boxes no wider than the card, cheat mode's XP boxes wrapping, the canvas hint
 * clear of the sidebar card, the Complex page's bar at the foot solid.
 * Item 2, every state on one page (lab/ui-states/states.html): every state the card's badge can take
 * (locked, available, learning, unlocked, weakened) is a filled box, none a bare word.
 *
 * Depends on: settingsCategories.js (in a sandbox with a fake document), the panel's CSS and
 * script.js read as text.
 */

var PanelAuditTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(rel) {
        var fs = require('fs'), path = require('path');
        return fs.readFileSync(path.join(__dirname, rel), 'utf8').replace(/\r\n/g, '\n');
    },

    /** The body of the first CSS rule whose selector list matches `selector` (a RegExp), or '' */
    _rule: function(css, selector) {
        var re = /([^{}]+)\{([^{}]*)\}/g, m;
        while ((m = re.exec(css))) {
            if (selector.test(m[1].replace(/\/\*[\s\S]*?\*\//g, '').trim())) return m[2];
        }
        return '';
    },

    /** A fake element with what SettingsCategories.layout touches */
    _el: function(width) {
        var classes = { 'tab-content': true, active: true };
        return {
            clientWidth: width,
            classList: {
                contains: function(c) { return !!classes[c]; },
                toggle: function(c, on) { if (on) classes[c] = true; else delete classes[c]; return on; }
            }
        };
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['Settings one column', this._oneColumn], ['Settings rows', this._settingsRows],
            ['Power steps', this._powerSteps], ['Spell card', this._card], ['Tree and Scan', this._treeScan],
            ['State badges', this._badges]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    },

    // ======================================================================= item 1

    _oneColumn: function() {
        var vm = require('vm'), box = { window: {}, console: { log: function() {} } };
        vm.createContext(box);
        vm.runInContext(this._file('settingsCategories.js'), box);
        var S = box.SettingsCategories;
        var content = null;
        S._content = function() { return content; };
        var at = function(width) {
            content = PanelAuditTest._el(width);
            S.layout();
            return [content.classList.contains('settings-narrow'), content.classList.contains('settings-onecol')];
        };
        this.check(S.ONE_COLUMN_PX > S.NARROW_PX, 'ONE_COLUMN_PX (' + S.ONE_COLUMN_PX + ') is wider than NARROW_PX (' + S.NARROW_PX + ')');
        var wide = at(1490), dflt = at(870), narrow = at(600), unset = at(0);
        this.check(!wide[0] && !wide[1], 'the full-size panel (1490 px page): category column, blocks side by side');
        this.check(!dflt[0] && dflt[1], 'the 900 px default panel (870 px page): category column, blocks one under another');
        this.check(narrow[0] && narrow[1], 'a narrow page: the list a row on top, one column');
        this.check(!unset[0] && !unset[1], 'a page not laid out yet (width 0) gets neither');
        var edge = at(S.ONE_COLUMN_PX - 1), over = at(S.ONE_COLUMN_PX);
        this.check(edge[1] && !over[1], 'one column below ONE_COLUMN_PX only');
        var css = this._file('../panel-settings.css');
        var onecol = this._rule(css, /settings-onecol \.settings-split[\s\S]*settings-onecol \.setting-pair/);
        this.check(/grid-template-columns:\s*minmax\(0,\s*1fr\)/.test(onecol), 'panel-settings.css: .settings-onecol puts the split blocks and the pairs in one column');
    },

    _settingsRows: function() {
        var css = this._file('../panel-settings.css');
        var rows = this._rule(css, /#contentSettings \.setting-row:not\(\.slider-row\),\s*#contentSettings \.setting-row-inline$/);
        this.check(/flex-wrap:\s*wrap/.test(rows), 'a settings row wraps its control under its text when both do not fit');
        var info = this._rule(css, /\.setting-row:not\(\.slider-row\) > \.setting-info,/);
        this.check(/flex:\s*1 1 12em/.test(info) && /min-width:\s*0/.test(info), 'the row text takes the room and may shrink (no fixed max-content width)');
        var ctl = this._rule(css, /\.setting-row:not\(\.slider-row\) > :not\(\.setting-info\)$/);
        this.check(/flex:\s*0 0 auto/.test(ctl), 'a row\'s control keeps its size (a segmented switch is never cut)');
        var pair = this._rule(css, /#contentSettings \.setting-pair \.setting-desc$/);
        this.check(/display:\s*block/.test(pair) && /overflow:\s*visible/.test(pair), 'a paired setting\'s description is not clamped to two lines');
        var label = this._rule(css, /#contentSettings \.slider-compact-label$/);
        this.check(/white-space:\s*normal/.test(label) && /text-overflow:\s*clip/.test(label), 'a compact slider\'s label wraps instead of ending in "..."');
        var grid3 = this._rule(css, /#contentSettings \.slider-grid-3$/);
        this.check(/repeat\(3,\s*minmax\(0,\s*1fr\)\)/.test(grid3), 'slider grid columns may shrink below their content');
        var page = css.match(/#contentSettings\.active \{[^}]*minmax\(10\.5em, max-content\)/);
        this.check(!!page, 'the category list is as wide as its longest name ("Дополнительно" spilled out of 10.5em)');
        this.check(css.indexOf('-webkit-line-clamp') === -1, 'panel-settings.css clamps no text');
    },

    _powerSteps: function() {
        var src = this._file('../script.js');
        var start = src.indexOf('function renderPowerSteps');
        var end = src.indexOf('\nfunction ', start + 10);
        var body = src.slice(start, end === -1 ? undefined : end);
        this.check(/masteredRow\.className = 'power-step-row power-step-final'/.test(body), 'the last power step row is marked power-step-final');
        this.check(!/spacer/.test(body) && !/style\.marginLeft/.test(body), 'no spacer cells or pushed-in figures (they ran into the next column)');
        var css = this._file('../panel-settings.css');
        this.check(/\.power-step-row\.power-step-final \.power-step-unit \{ grid-column: span 2; \}/.test(css), 'its figures span the input and unit columns');
        this.check(/minmax\(5\.1em, max-content\) 3\.6em minmax\(5\.1em, max-content\)/.test(css), 'the unit columns grow to their longest word');
    },

    _card: function() {
        var css = this._file('../panel-card.css');
        var chip = this._rule(css, /^\.spell-need-chip$/);
        this.check(/display:\s*inline-block/.test(chip) && /max-width:\s*100%/.test(chip), 'a Still needed chip is an inline box no wider than the card');
        this.check(/overflow-wrap:\s*break-word/.test(chip), 'a long name breaks inside the chip');
        this.check(!/inline-flex/.test(chip), 'no flex row in the chip (its parts wrapped as columns side by side)');
        var lockmark = this._rule(css, /^\.spell-need-lockmark$/);
        this.check(/display:\s*inline-block/.test(lockmark) && /margin-right/.test(lockmark), 'the LOCK badge keeps its own box and a gap before the name');
        var cheat = this._rule(css, /\.progress-section\.cheat-xp \.progress-label$/);
        this.check(/flex-wrap:\s*wrap/.test(cheat), 'cheat mode\'s XP boxes wrap in the sidebar');
    },

    _treeScan: function() {
        var tree = this._file('../panel-tree.css');
        var hint = this._rule(tree, /details-open:not\(\.details-bottom\) \.tree-instructions$/);
        this.check(/max-width:\s*calc\(100% - 19\.65em/.test(hint) && /left:\s*calc\(\(100% - 19\.65em\) \/ 2\)/.test(hint),
            'the canvas hint stays in the part of the tree the sidebar card leaves free');
        var card = this._file('../styles-skyrim.css');
        this.check(/\.details-panel \{[^}]*width: 15\.72em;/.test(card), 'the sidebar is still 15.72em (the hint\'s 19.65em = 15.72 / 0.8)');
        var scan = this._file('../panel-scan.css');
        var bar = this._rule(scan, /#contentSpellScan \.scan-actions-bottom$/);
        this.check(/background:\s*var\(--panel-bg-solid/.test(bar) && !/gradient/.test(bar), 'the Complex page\'s bar at the foot is solid');
    }
};

// ======================================================================= item 2

/** Every state the card's badge can take is a filled box (Learning had no rule, Unlocked an undefined token) */
PanelAuditTest._badges = function() {
    var css = this._file('../styles-skyrim.css') + '\n' + this._file('../panel-card.css');
    var self = this;
    ['locked', 'available', 'learning', 'unlocked', 'weakened'].forEach(function(st) {
        var re = new RegExp('\\.state-badge\\.' + st + ' \\{[^}]*background:\\s*var\\(--[\\w-]+', 'g');
        var found = css.match(re) || [];
        self.check(found.length > 0, 'the ' + st + ' badge has a background (' + found.length + ' rule' + (found.length === 1 ? '' : 's') + ')');
    });
    var card = this._file('../panel-card.css');
    this.check(/\.state-badge\.unlocked \{ background: var\(--node-unlocked-bg, var\(--node-learning-bg/.test(card),
        'Unlocked falls back to a token the theme defines (none defines --node-unlocked-bg)');
    var learning = this._rule(card, /^\.state-badge\.learning$/);
    this.check(/color:\s*var\(--learning-color/.test(learning), 'Learning in the tree\'s learning colour');
};

if (typeof window !== 'undefined') window.PanelAuditTest = PanelAuditTest;
if (typeof global !== 'undefined') global.PanelAuditTest = PanelAuditTest;
