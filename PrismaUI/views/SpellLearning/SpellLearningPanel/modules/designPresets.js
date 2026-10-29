/**
 * DesignPresets - the panel's look, one choice in Settings > UI Display > Design.
 * It used to be two: a UI theme (the stylesheet) and a design preset (the tree).
 * Now a design sets both:
 *   uiTheme  - the UI theme in themes/ it is built on (the panel's main
 *              stylesheet); DEFAULT_UI_THEME when left out
 *   tree     - TreeStyle tokens: how the spell tree is drawn (modules/treeStyle.js)
 *   cssFile  - an extra stylesheet laid over the theme, path relative to the panel
 *   css      - the same, written inline (a string, or an array of lines)
 *   icons    - folder of the 19 icon PNGs (modules/homIcons.js), ending in "/"
 *
 * One is built in: Skald, the default - the panel applies it at load, before
 * any file arrives, so the default cannot be a file. Every other UI theme listed
 * in themes/manifest.json shows up as a design of its own (onThemesLoaded), so
 * old UI theme add-ons keep working. Add-ons add more by dropping a .json file into
 * Data/SKSE/Plugins/SpellLearning/presets/design/ - the plugin already lists
 * every preset folder (UIManager::OnLoadPresets), so one file per design, no
 * shared list to fight over between add-ons. Classic, Modern Dark, Arcane,
 * Chalkboard and Night Grimoire are such add-ons, one archive each (sources in
 * addons/designs/). See docs/DESIGN.md.
 *
 * Depends on: settings, TreeStyle, t() (optional), UI_THEMES/applyTheme (optional), homIcons.js (optional)
 */

var DesignPresets = {

    DEFAULT_ID: 'skald',
    DEFAULT_UI_THEME: 'skyrim',

    BUILT_IN: {
        skald: {
            name: 'Skald',
            description: 'Skyrim\'s own menus: smoky black, thin pale lines, knotwork corners and wide-set capital titles, with the school colours softened to quiet tints.',
            descriptionKey: 'design.skald.desc',
            author: 'Heart of Magic',
            uiTheme: 'skyrim',
            cssFile: 'themes/design-darkbook.css',
            icons: 'themes/icons-line/',
            css: [
                ':root {',
                '    --book-cover-top: #121212;',
                '    --book-cover-bottom: #070708;',
                '    --book-cover: #0b0b0c;',
                '    --book-cover-glow: rgba(207, 201, 184, 0.08);',
                '    --book-edge: #4a4740;',
                '    --book-gold: #cfc9b8;',
                '    --book-gold-bright: #fffbef;',
                '    --book-text: #e8e4d8;',
                '    --book-text-soft: #cbc5b6;',
                '    --book-text-faint: #8f8a7e;',
                '    --book-text-bright: #fffbef;',
                '    --book-tools: rgba(8, 8, 9, 0.94);',
                '    --book-page: #0e0e0f;',
                '    --book-page-edge: #080809;',
                '    --book-well: rgba(0, 0, 0, 0.55);',
                '    --preview-bg: #080809;',
                '}',
                '/* No tree yet: the five school marks in a knotwork plate (themes/skald/empty.png) */',
                '.empty-state { background: #080809; }',
                '.empty-icon { width: 360px; height: 240px; max-width: 80%; font-size: 0; opacity: 1; margin-bottom: 6px;',
                '    background: url("themes/skald/empty.png") center / contain no-repeat; }',
                '.empty-state .empty-text { color: #e8e4d8; letter-spacing: 2px; text-transform: uppercase; }',
                '.empty-state .empty-hint { color: rgba(232, 228, 216, 0.7); }',
                '/* Skyrim\'s menus set everything in a narrow sans. Windows\' own Bahnschrift comes closest; no font file',
                '   is shipped, so a system without it falls through to the Skyrim Edge theme\'s narrow faces, and',
                '   Korean, Japanese and Chinese text to the system\'s sans. */',
                ':root {',
                '    --skald-font: "Bahnschrift SemiCondensed", "Bahnschrift", "Futura Condensed", "Roboto Condensed", "Arial Narrow", sans-serif;',
                '    --book-serif: var(--skald-font);',
                '}',
                'body, button, input, select, textarea, .btn { font-family: var(--skald-font); }',
                '/* Titles in wide-set capitals, like the game\'s menu headings */',
                '.panel-title { letter-spacing: 5px; text-transform: uppercase; }',
                '.settings-block-title, .subsection-title, .popup-section-title, .modal-header h2, .modal-header h3,',
                '.howto-header h3, .howto-section h4, .heart-settings-popup .popup-header { letter-spacing: 2px; text-transform: uppercase; }',
                '/* Pale, not gold: the chosen tab, toggle and badge in the line colour */',
                '.school-tab.active { background: rgba(232, 228, 216, 0.14); color: #fffbef; }',
                '.heart-settings-popup .popup-header { background: rgba(232, 228, 216, 0.06); }',
                '.segmented-toggle .seg-btn.active { background: rgba(232, 228, 216, 0.16); color: #fffbef; }',
                '.segmented-toggle .seg-btn:hover:not(.active) { background: rgba(232, 228, 216, 0.08); color: #cbc5b6; }',
                '.renderer-badge.canvas { background: rgba(232, 228, 216, 0.12); color: #e8e4d8; }',
                '/* Colour only where it marks a state (2026-10-29 polish). These rules are Skald\'s alone: Classic is the',
                '   bare theme and Night Grimoire and Chalkboard share design-darkbook.css, so the base sheets stay. */',
                '.scan-stat-value, .slider-value { color: var(--text-bright); }',
                '.settings-subsection, .settings-split .settings-subsection { background: transparent; border-left: 0;',
                '    border-top: 1px solid var(--divider); }',
                '/* Counts and tags as plain boxes in the text colour; a dot where the chip marks a state */',
                '.version-tag { background: transparent; color: var(--text-secondary); }',
                '.blacklist-count, .whitelist-count { background: transparent; border: 1px solid var(--divider); border-radius: 0;',
                '    color: var(--text-secondary); }',
                '.footer-chip { border-radius: 0; }',
                '.footer-chip.is-learning { border-color: var(--divider); }',
                '.footer-chip.is-learning::before { content: ""; flex: none; width: 6px; height: 6px; border-radius: 50%;',
                '    background: var(--node-learning-border); }',
                '.footer-chip-new { color: var(--text-secondary); border-color: var(--divider); }',
                '.scanner-preset-chip, .easy-preset-chip { border-radius: 0; border-color: var(--divider); background: transparent;',
                '    color: var(--text-secondary); }',
                '.scanner-preset-chip:hover, .easy-preset-chip:hover { background: var(--btn-bg-hover); border-color: var(--book-edge);',
                '    color: var(--text-primary); }',
                '.scanner-preset-chip.active, .easy-preset-chip.selected { background: transparent; border-color: var(--book-edge);',
                '    color: var(--text-bright); box-shadow: none; }',
                '.scanner-preset-chip.active::before, .easy-preset-chip.selected::before { content: ""; flex: none; width: 6px;',
                '    height: 6px; margin-right: 4px; border-radius: 50%; background: var(--accent-gold); }',
                '.spell-need-chip { border-radius: 0; }',
                '.spell-need-chip.spell-need-lock { border-color: var(--divider); }',
                '/* No glow, fade or blend: flat lines and fills */',
                '.howto-tab { text-shadow: none; }',
                '.scan-actions-bottom::after { display: none; }',
                '.legend-item.unlocked .legend-dot { background: var(--accent-gold); }',
                '/* Thin bars: the build bar a 2px line with no frame (a 1px border each side left it no inside), the slider',
                '   track 3px; figures in even columns */',
                '.build-progress-bar { height: 2px; border: 0; }',
                '.build-progress-fill.error { background: var(--status-error); }',
                '.setting-slider { height: 3px; }',
                '.spell-panel { font-variant-numeric: tabular-nums; }'
            ],
            tree: {
                pageColor: '#0b0b0c',
                pageGrain: 0.35,
                pageGrainColor: '#4a4740',
                // A flat page: no light in the middle ('' paints no glow bands at all) and no darkened edge
                pageGlow: '',
                pageGlowAlpha: 0,
                pageEdge: '#000000',
                pageEdgeAlpha: 0,
                schoolInk: 0.4,
                schoolInkTone: '#cbc5b6',
                labelFont: 'var(--skald-font)',
                labelMaxChars: 18,
                labelHalo: 'rgba(5, 5, 6, 0.9)',
                labelHaloWidth: 3,
                labelUnlocked: '#f2eee2',
                labelAvailable: 'rgba(232, 228, 216, 0.85)',
                labelHidden: 'rgba(203, 197, 182, 0.4)',
                nodeFill: '#0e0e0f',
                lockedStroke: '#6f6b62',
                mysteryFill: 'rgba(14, 14, 15, 0.9)',
                focusStroke: '#fffbef',
                ringTrack: '#e8e4d8',
                availableAlpha: 1,
                availableRing: true,
                learningGlow: 0.5,
                hubFill: '#000000',
                hubRing: '#cfc9b8',
                hubText: '#e8e4d8',
                globeColor: '#cfc9b8',
                dimEdgeColor: '#1c1b19',
                unlockedEdgeAlpha: 0.6,
                unlockedEdgeWidth: 1.6,
                lockedEdgeColor: '#6f6b62',
                lockedEdgeAlpha: 0.22,
                frontierEdgeAlpha: 0.45,
                selectedPathColor: '#fffbef',
                selectedPathAlpha: 0.9,
                selectedPathWidth: 2.2,
                hoverPathAlpha: 0.8,
                accent: '#cfc9b8',
                chapterTitles: true,
                dividerOrnament: true,
                pageOrnament: 'themes/skald/corner.png'
            }
        }
    },

    _external: {},        // id -> preset, from presets/design/*.json
    _fromThemes: {},      // id -> preset, one per UI theme no built-in covers
    _appliedId: null,

    /** Every preset: built-in, then UI themes, then add-ons. Ids are lower case. */
    all: function() {
        var out = {};
        var k;
        for (k in this.BUILT_IN) if (this.BUILT_IN.hasOwnProperty(k)) out[k] = this.BUILT_IN[k];
        for (k in this._fromThemes) if (this._fromThemes.hasOwnProperty(k) && !out[k]) out[k] = this._fromThemes[k];
        for (k in this._external) if (this._external.hasOwnProperty(k) && !out[k]) out[k] = this._external[k];
        return out;
    },

    /**
     * The UI themes are loaded (themes/manifest.json). A theme no built-in design
     * is built on becomes a design of its own with the plain tree, so a UI theme
     * add-on still shows up; then the saved design is applied again, since its
     * stylesheet could not be switched before the themes were known.
     */
    onThemesLoaded: function() {
        var covered = {};
        var k;
        for (k in this.BUILT_IN) if (this.BUILT_IN.hasOwnProperty(k)) covered[this.BUILT_IN[k].uiTheme] = true;
        this._fromThemes = {};
        if (typeof UI_THEMES !== 'undefined') {
            for (k in UI_THEMES) {
                if (!UI_THEMES.hasOwnProperty(k) || covered[k]) continue;
                this._fromThemes[this.THEME_DESIGN_PREFIX + k.toLowerCase()] = {
                    name: UI_THEMES[k].name || k,
                    description: UI_THEMES[k].description || '',
                    author: UI_THEMES[k].author || '',
                    uiTheme: k,
                    tree: {}
                };
            }
        }
        this.populateSelector();
        this.apply(settings.designPreset);
    },

    THEME_DESIGN_PREFIX: 'ui-',

    /**
     * The design a saved config asks for. Configs from before the merge have only
     * uiTheme: the modern theme becomes Modern Dark, any other add-on theme its own
     * design, Skyrim the default. A config from the days of both choices that paired
     * Classic with the modern theme becomes Modern Dark.
     * @param {Object} data - the saved config
     * @returns {string}
     */
    savedChoice: function(data) {
        var design = (data && typeof data.designPreset === 'string') ? data.designPreset.toLowerCase() : '';
        var ui = (data && typeof data.uiTheme === 'string') ? data.uiTheme : '';
        if (design === 'classic' && ui === 'default') return 'modern';
        if (design) return design;
        if (ui === 'default') return 'modern';
        if (ui && ui !== this.DEFAULT_UI_THEME) return this.THEME_DESIGN_PREFIX + ui.toLowerCase();
        return this.DEFAULT_ID;
    },

    /** Whether the current design's render block sets this key (with a usable value). */
    renderHas: function(key) {
        var r = this._current && this._current.render;
        return !!(r && r[key] !== undefined && r[key] !== null);
    },

    /**
     * A renderer value for the current design: its `render` block if it sets the
     * key, else the player's setting, else the fallback. The render popup no
     * longer offers these (colours, sizes, particle and star counts, text), so a
     * design is one coherent look. A value of the wrong type (an add-on writing
     * "60" for 60) is converted when it can be and skipped when it cannot, as
     * TreeStyle.set does for tree tokens.
     * @param {string} key - a settings key (globeSize, starfieldDensity, heartRingColor ...)
     * @param {*} fallback - also gives the expected type
     */
    renderValue: function(key, fallback) {
        var r = this._current && this._current.render;
        var v = this._typed(r ? r[key] : undefined, fallback);
        if (v !== undefined) return v;
        v = this._typed(typeof settings !== 'undefined' ? settings[key] : undefined, fallback);
        return v !== undefined ? v : fallback;
    },

    _typed: function(v, fallback) {
        if (v === undefined || v === null) return undefined;
        if (fallback === undefined || typeof v === typeof fallback) return v;
        if (typeof fallback === 'number') {
            var n = parseFloat(v);
            return isFinite(n) ? n : undefined;
        }
        if (typeof fallback === 'boolean') {
            if (v === 'true') return true;
            if (v === 'false') return false;
            return undefined;
        }
        if (typeof fallback === 'string' && typeof v === 'number') return String(v);
        return undefined;
    },

    _text: function(key, fallback) {
        if (!key || typeof t !== 'function') return fallback;
        var s = t(key);
        return s === key ? fallback : s;
    },

    /**
     * An add-on cannot ship lines for lang/*.json, so its file may carry its own
     * descriptions: "descriptions": { "ko": "...", "de": "..." }. Design names are
     * not translated (nameOf: the preset's own "name", English for the shipped ones).
     */
    _localized: function(map) {
        if (!map || typeof map !== 'object' || typeof getLocale !== 'function') return '';
        var loc = getLocale();
        return (typeof map[loc] === 'string' && map[loc]) ? map[loc] : '';
    },

    nameOf: function(p) { return p.name || '?'; },
    descriptionOf: function(p) { return this._localized(p.descriptions) || this._text(p.descriptionKey, p.description || ''); },

    /**
     * Apply a preset by id. An id that is not loaded (yet) falls back to the
     * default, but stays in settings, so an add-on preset that arrives later
     * from disk is applied then.
     * @param {string} id
     */
    apply: function(id) {
        var all = this.all();
        var key = (id || '').toLowerCase();
        var preset = all[key] || all[this.DEFAULT_ID];
        var appliedKey = all[key] ? key : this.DEFAULT_ID;

        TreeStyle.set(preset.tree || {});
        this._applyCss(preset);
        // The design's icon folder (design.icons); none or a bad path -> the line set
        if (typeof HomIcons !== 'undefined') HomIcons.setPath(preset.icons);

        // The UI theme underneath. settings.uiTheme follows the design; before the
        // themes are loaded the stylesheet stays, and onThemesLoaded applies it.
        var uiTheme = preset.uiTheme || this.DEFAULT_UI_THEME;
        settings.uiTheme = uiTheme;
        if (typeof UI_THEMES !== 'undefined' && UI_THEMES[uiTheme] && typeof applyTheme === 'function') {
            applyTheme(uiTheme);
        }

        this._appliedId = appliedKey;
        this._current = preset;
        this._syncSelector();
        // The design's render block (heart, globe, starfield values) reaches the renderer
        if (typeof applyHeartSettingsToRenderer === 'function') applyHeartSettingsToRenderer();
        if (typeof applyGlobeSettings === 'function') applyGlobeSettings();
        // Which design effects this design has (Render settings > Design Effects)
        if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.sync();
    },

    /** The preset's own stylesheet and inline CSS; whatever the last preset added goes. */
    _applyCss: function(preset) {
        var link = document.getElementById('design-preset-css');
        var href = typeof preset.cssFile === 'string' ? preset.cssFile : '';
        if (href) {
            if (!link) {
                link = document.createElement('link');
                link.id = 'design-preset-css';
                link.rel = 'stylesheet';
                // The design's label font may be a CSS variable of this sheet: the
                // tree's names are drawn again once it is in
                link.onload = function() { if (typeof TreeStyle !== 'undefined') TreeStyle.fontsChanged(); };
                document.head.appendChild(link);
            }
            if (link.getAttribute('href') !== href) link.setAttribute('href', href);
        } else if (link) {
            link.parentNode.removeChild(link);
        }

        var style = document.getElementById('design-preset-style');
        var css = preset.css;
        if (Object.prototype.toString.call(css) === '[object Array]') css = css.join('\n');
        if (typeof css === 'string' && css) {
            if (!style) {
                style = document.createElement('style');
                style.id = 'design-preset-style';
                document.head.appendChild(style);
            }
            style.textContent = css;
            // After the stylesheet, so the inline CSS wins over it (a shared
            // sheet's defaults, a palette set per preset)
            document.head.appendChild(style);
        } else if (style) {
            style.parentNode.removeChild(style);
        }
    },

    /**
     * Presets read from disk (onPresetsLoaded, type 'design').
     * @param {Array} entries - [{ key, data }]
     */
    onLoaded: function(entries) {
        this._external = {};
        for (var i = 0; i < entries.length; i++) {
            var data = entries[i] && entries[i].data;
            if (!data || typeof data !== 'object') continue;
            var id = String(data.id || entries[i].key || '').toLowerCase();
            if (!id || this.BUILT_IN[id]) continue;   // built-ins cannot be replaced
            if (!data.name) data.name = entries[i].key || id;
            this._external[id] = data;
        }
        this.populateSelector();
        // The saved choice may be one of these
        this.apply(settings.designPreset);
    },

    /** Ask the plugin for presets/design/*.json. Harmless outside the game. */
    requestFromDisk: function() {
        if (window.callCpp) window.callCpp('LoadPresets', JSON.stringify({ type: 'design' }));
    },

    // =========================================================================
    // SETTINGS UI
    // =========================================================================

    populateSelector: function() {
        var select = document.getElementById('designPresetSelect');
        if (!select) return;
        var all = this.all();
        select.innerHTML = '';
        for (var id in all) {
            if (!all.hasOwnProperty(id)) continue;
            var p = all[id];
            var option = document.createElement('option');
            option.value = id;
            option.textContent = this.nameOf(p) + (p.author && p.author !== 'Heart of Magic' ? ' - ' + p.author : '');
            select.appendChild(option);
        }
        this._syncSelector();
    },

    _syncSelector: function() {
        var select = document.getElementById('designPresetSelect');
        if (select && this._appliedId && select.value !== this._appliedId) select.value = this._appliedId;
        var desc = document.getElementById('designPresetDesc');
        var p = this._appliedId ? this.all()[this._appliedId] : null;
        if (desc && p) desc.textContent = this.descriptionOf(p);
    },

    initSelector: function() {
        var select = document.getElementById('designPresetSelect');
        if (!select || select._designBound) return;
        select._designBound = true;
        this.populateSelector();
        var self = this;
        select.addEventListener('change', function() {
            settings.designPreset = this.value;
            self.apply(this.value);
            if (typeof scheduleAutoSave === 'function') scheduleAutoSave();
        });
    }
};

// The tree gets the default look before the saved settings arrive
DesignPresets.apply(DesignPresets.DEFAULT_ID);

window.DesignPresets = DesignPresets;
