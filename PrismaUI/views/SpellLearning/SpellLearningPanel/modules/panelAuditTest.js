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
 * Item 3, contrast and size: every design's quiet text, faint and gold inks and the dark books' blue and red at
 * 4.5:1 on their own backgrounds (worked out here from the tokens, the dark presets' palettes read too: Skald's
 * from designPresets.js, the add-ons' from addons/designs/), the dark books' quiet text from the soft ink,
 * switches and badges on the theme's colours, the LOCK mark 11px, the school name in the card's ink with the
 * colour on its dot.
 *
 * Depends on: settingsCategories.js (in a sandbox with a fake document), the panel's CSS and
 * script.js read as text; the add-on designs' files from their source trees (addons/designs/<add-on>/).
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

    /** A file of an add-on design's source tree (addons/designs/<addon>/, Data layout), from the Data root */
    _addonFile: function(addon, rel) {
        return this._file('../../../../../addons/designs/' + addon + '/' + rel);
    },

    _addonPanelFile: function(addon, rel) {
        return this._addonFile(addon, 'PrismaUI/views/SpellLearning/SpellLearningPanel/' + rel);
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
            ['State badges', this._badges], ['Quiet text', this._quietText], ['Controls', this._controls]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    },

    // ======================================================================= item 1

    _oneColumn: function() {
        var vm = require('vm'), box = { window: {}, console: { log: function() {} }, settings: { fontSizeMultiplier: 1 } };
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
        box.settings.fontSizeMultiplier = 1.5;
        var big = at(1490);
        box.settings.fontSizeMultiplier = 1;
        this.check(!big[0] && big[1], 'Font Size 1.5x: the full-size page (1490 px = 993 px of 1.0x text) goes to one column');
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
        // Item 4: in a three-up row the text stacks over its switch, so a flex basis there is a height
        var triple = this._rule(css, /#contentSettings \.setting-triple > \.setting-row > \.setting-info$/);
        this.check(/flex:\s*0 0 auto/.test(triple), 'a three-up row\'s text takes no 12em height (its switches sat far below their labels at 2560 x 1440)');
        this.check(/\.setting-triple > \.setting-row \{[^}]*flex-direction: column/.test(this._file('../styles-skyrim.css')),
            'three-up rows still stack (else the rule above is moot)');
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
        this.check(!/masteredRow\.style\.opacity/.test(body), 'the last row is not faded (its quiet figures read 3:1 at 70%)');
        var css = this._file('../panel-settings.css');
        this.check(/\.power-step-row\.power-step-final \.power-step-unit \{ grid-column: span 2; color: var\(--text-secondary\); \}/.test(css),
            'its figures span the input and unit columns, in the secondary ink (not faded quiet ink)');
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
        // a background from a token the theme defines (not --node-unlocked-bg, which none does)
        var re = new RegExp('\\.state-badge\\.' + st + '[^{]*\\{[^}]*background:\\s*var\\(--(?!node-unlocked-bg)[\\w-]+', 'g');
        var found = css.match(re) || [];
        self.check(found.length > 0, 'the ' + st + ' badge has a background from a defined token (' + found.length + ' rule' + (found.length === 1 ? '' : 's') + ')');
    });
    var card = this._file('../panel-card.css');
    var learning = this._rule(card, /^\.state-badge\.learning,\s*\.tooltip-state\.learning$/);
    var unlocked = this._rule(card, /^\.state-badge\.unlocked,\s*\.tooltip-state\.unlocked$/);
    this.check(/border-left:\s*3px solid var\(--learning-color/.test(learning) && /color:\s*var\(--text-primary/.test(learning),
        'Learning: the player\'s learning colour on the edge, the word in the card\'s ink (the colour read 2.9:1 as text on Chalkboard)');
    this.check(/border-left:\s*3px solid var\(--accent-gold/.test(unlocked) && /background:\s*var\(--btn-bg/.test(unlocked),
        'Unlocked (mastered): a filled box with a gold edge, apart from Learning\'s');
    this.check(/\.tooltip-state\.locked \{ color: var\(--text-secondary/.test(card), 'the hover card\'s Locked tag reads --text-secondary like the badge');
};

// ======================================================================= item 3

/** WCAG 2 contrast of two #rrggbb colours */
PanelAuditTest._contrast = function(a, b) {
    var lum = function(hex) {
        var c = [1, 3, 5].map(function(i) {
            var v = parseInt(hex.slice(i, i + 2), 16) / 255;
            return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        });
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    };
    var x = lum(a), y = lum(b);
    return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
};

/** A custom property's #rrggbb value in a sheet (the first one), or null */
PanelAuditTest._token = function(css, name) {
    var m = new RegExp('--' + name + ':\\s*(#[0-9a-fA-F]{6})').exec(css);
    return m ? m[1].toLowerCase() : null;
};

/** The quiet text of every design reads 4.5:1 on its own panel colour (the computed audit's fixes, pinned) */
PanelAuditTest._quietText = function() {
    var self = this, need = 4.5;
    var classic = this._file('../styles-skyrim.css'), modern = this._addonPanelFile('ModernDark', 'themes/design-modern.css');
    var arcane = this._addonPanelFile('Arcane', 'themes/design-arcane.css'), dark = this._file('../themes/design-darkbook.css');
    var pairs = [
        ['Classic quiet text on the panel', this._token(classic, 'text-muted'), this._token(classic, 'panel-bg-solid')],
        ['Classic quiet text on a chip (#141414)', this._token(classic, 'text-muted'), '#141414'],
        ['Modern Dark quiet text on the panel', this._token(modern, 'text-muted'), this._token(modern, 'panel-bg-solid')],
        ['Arcane quiet text on the leather', this._token(arcane, 'text-muted'), this._token(arcane, 'panel-bg-solid')],
        ['Arcane faint ink on the page\'s edge', this._token(arcane, 'arcane-ink-faint'), this._token(arcane, 'arcane-parchment-edge')],
        ['Arcane gold ink on the page\'s edge', (/\.details-panel,[\s\S]*?--accent-gold:\s*(#[0-9a-f]{6})/i.exec(arcane) || [])[1], this._token(arcane, 'arcane-parchment-edge')],
        // #353e37: Chalkboard's settings sub-heading band over its page, as the audit measured it (a
        // translucent band, so in no file); the lightest slate the blue and red sit on
        ['Dark books\' blue on Chalkboard\'s sub-heading band', this._token(dark, 'accent-blue'), '#353e37'],
        ['Dark books\' red on Chalkboard\'s sub-heading band', this._token(dark, 'accent-red'), '#353e37'],
        // #3f413c: the Weakened badge's own tint over Chalkboard's card, as measured
        ['Dark books\' Weakened on its badge (Chalkboard)', this._token(dark, 'weakened-color'), '#3f413c'],
        ['Arcane\'s Learning badge ink on the page\'s edge', (/\.state-badge\.learning\s+\{ color: (#[0-9a-f]{6})/.exec(arcane) || [])[1], this._token(arcane, 'arcane-parchment-edge')],
        ['Arcane\'s Unlocked badge ink on the page\'s edge', (/\.state-badge\.unlocked\s+\{ color: (#[0-9a-f]{6})/.exec(arcane) || [])[1], this._token(arcane, 'arcane-parchment-edge')]
    ];
    pairs.forEach(function(p) {
        var ok = !!(p[1] && p[2]);
        var r = ok ? self._contrast(p[1].toLowerCase(), p[2].toLowerCase()) : 0;
        self.check(ok && r >= need, p[0] + ': ' + (ok ? r.toFixed(2) + ':1 (' + p[1] + ' on ' + p[2] + ')' : 'token not found'));
    });
    // The dark books read their quiet text and idle status from the soft ink: every shipped dark preset's soft
    // ink on its page and cover (the presets' palettes cannot be changed here)
    this.check(/--text-muted:\s*var\(--book-text-soft\)/.test(dark) && /--status-idle:\s*var\(--book-text-soft\)/.test(dark),
        'design-darkbook.css: quiet text and the idle status from --book-text-soft');
    // Skald is built in (designPresets.js, run in a sandbox); the add-on dark books are files in their source trees
    var vm = require('vm'), noop = function() {};
    var box = { window: {}, settings: {}, TreeStyle: { set: noop, fontsChanged: noop },
        document: { getElementById: function() { return null; }, head: { appendChild: noop },
            createElement: function() { return { setAttribute: noop, getAttribute: function() { return null; } }; } } };
    vm.createContext(box);
    vm.runInContext(this._file('designPresets.js'), box);
    var designs = { Skald: box.DesignPresets.BUILT_IN.skald };
    ['Chalkboard', 'NightGrimoire'].forEach(function(name) {
        designs[name] = JSON.parse(self._addonFile(name, 'SKSE/Plugins/SpellLearning/presets/design/' + name + '.json'));
    });
    ['Chalkboard', 'NightGrimoire', 'Skald'].forEach(function(name) {
        var data = designs[name];
        var css = Array.isArray(data.css) ? data.css.join('\n') : String(data.css || '');
        var soft = self._token(css, 'book-text-soft'), page = self._token(css, 'book-page'), cover = self._token(css, 'book-cover-bottom');
        var worst = Math.min(self._contrast(soft, page), self._contrast(soft, cover));
        self.check(worst >= need, name + ': soft ink ' + soft + ' on its page and cover, worst ' + worst.toFixed(2) + ':1');
    });
};

/** Controls take the theme's colours (no gold at 40%), the LOCK mark is 11px, the school name keeps the card's ink */
PanelAuditTest._controls = function() {
    var classic = this._file('../styles-skyrim.css'), card = this._file('../panel-card.css');
    var seg = this._rule(classic, /^\.segmented-toggle \.seg-btn$/);
    this.check(/color:\s*var\(--text-secondary\)/.test(seg) && !/color:\s*rgba/.test(seg), 'an option not chosen in a segmented switch reads --text-secondary');
    this.check(/color:\s*var\(--accent-gold-hover\)/.test(this._rule(classic, /^\.segmented-toggle \.seg-btn\.active$/)), 'the chosen option reads --accent-gold-hover');
    this.check(/color:\s*var\(--text-secondary\)/.test(this._rule(classic, /^\.state-badge\.locked$/)), 'the Locked badge reads --text-secondary on its grey box');
    this.check(!/opacity/.test(this._rule(classic, /^\.hidden-info$/)), 'hidden info ("???") is not faded below its colour');
    var mark = this._rule(card, /^\.spell-need-lockmark$/), chip = this._rule(card, /^\.spell-need-chip$/);
    var size = parseFloat((/font-size:\s*([\d.]+)em/.exec(mark) || [])[1]) * parseFloat((/font-size:\s*([\d.]+)em/.exec(chip) || [])[1]) * 16;
    this.check(size >= 11, 'the LOCK mark in a chip is ' + size.toFixed(1) + 'px at the 16px body font (pressable, at least 11px)');
    this.check(/-webkit-text-fill-color:\s*var\(--text-primary/.test(this._rule(card, /^\.spell-meta \.school-badge$/)), 'the card\'s school name in its ink, the colour on the dot');
    var arcane = this._addonPanelFile('Arcane', 'themes/design-arcane.css');
    this.check(/#tree-school-tabs \.school-tab \{ -webkit-text-fill-color: var\(--text-primary\); \}/.test(arcane) && /#tree-school-tabs \.school-tab::before \{[^}]*background: currentColor/.test(arcane),
        'Arcane\'s school labels: the name in the cover\'s ink, the school colour on a dot');
};

if (typeof window !== 'undefined') window.PanelAuditTest = PanelAuditTest;
if (typeof global !== 'undefined') global.PanelAuditTest = PanelAuditTest;
