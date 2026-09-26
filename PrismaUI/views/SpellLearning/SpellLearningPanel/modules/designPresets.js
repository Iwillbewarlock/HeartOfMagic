/**
 * DesignPresets - the panel's look, one choice in Settings > UI Display > Design.
 * It used to be two: a UI theme (the stylesheet) and a design preset (the tree).
 * Now a design sets both:
 *   uiTheme  - the UI theme in themes/ it is built on (the panel's main
 *              stylesheet); DEFAULT_UI_THEME when left out
 *   tree     - TreeStyle tokens: how the spell tree is drawn (modules/treeStyle.js)
 *   cssFile  - an extra stylesheet laid over the theme, path relative to the panel
 *   css      - the same, written inline (a string, or an array of lines)
 *
 * Three are built in: Classic and Modern Dark (the two UI themes as they were,
 * with the tree as it was) and Arcane. Every other UI theme listed in
 * themes/manifest.json shows up as a design of its own (onThemesLoaded), so
 * old UI theme add-ons keep working. Add-ons add more by dropping a .json file into
 * Data/SKSE/Plugins/SpellLearning/presets/design/ - the plugin already lists
 * every preset folder (UIManager::OnLoadPresets), so one file per design, no
 * shared list to fight over between add-ons. See docs/DESIGN.md.
 *
 * Depends on: settings, TreeStyle, t() (optional), UI_THEMES/applyTheme (optional)
 */

var DesignPresets = {

    DEFAULT_ID: 'arcane',
    DEFAULT_UI_THEME: 'skyrim',

    BUILT_IN: {
        classic: {
            name: 'Classic',
            nameKey: 'design.classic.name',
            description: 'The original look: plain, and the lightest to draw.',
            descriptionKey: 'design.classic.desc',
            author: 'Heart of Magic',
            uiTheme: 'skyrim',
            tree: {}
        },
        modern: {
            name: 'Modern Dark',
            nameKey: 'design.modern.name',
            description: 'The original tree on the modern dark UI, with gradients and glow.',
            descriptionKey: 'design.modern.desc',
            author: 'Heart of Magic',
            // Colours and shapes over the Skyrim theme, like Arcane (the old
            // stand-alone modern stylesheet had fallen far behind it)
            uiTheme: 'skyrim',
            cssFile: 'themes/design-modern.css',
            tree: {}
        },
        arcane: {
            name: 'Arcane',
            nameKey: 'design.arcane.name',
            description: 'An open spellbook: a parchment page, ink lines, chapter titles, and the tree soaking out like ink when it opens.',
            descriptionKey: 'design.arcane.desc',
            author: 'Heart of Magic',
            // Leather cover round parchment pages: the panel's chrome goes dark
            // leather and gold, the tree, the spell card and the hover card parchment
            uiTheme: 'skyrim',
            cssFile: 'themes/design-arcane.css',
            tree: {
                // Page
                pageColor: '#e6d6b0',
                pageGrain: 0.9,
                pageGrainColor: '#7a5a2a',
                pageGlow: '#fff4d6',
                pageGlowAlpha: 0.35,
                pageGlowRadius: 0.6,
                pageEdge: '#5a3d18',
                pageEdgeAlpha: 0.55,

                // Ink: school colours darkened toward brown so they read on the page
                schoolInk: 0.3,
                schoolInkTone: '#2a1a08',
                learningColor: '#1d6f8a',

                // Labels
                labelFont: 'Georgia, "Palatino Linotype", "Book Antiqua", serif',
                labelMaxChars: 18,
                labelHalo: 'rgba(236, 224, 194, 0.9)',
                labelHaloWidth: 3,
                labelUnlocked: '#2a1c0c',
                labelAvailable: 'rgba(60, 40, 18, 0.9)',
                labelHidden: 'rgba(80, 60, 30, 0.45)',

                // Spells: learned ones filled with ink and edged in dark brown,
                // learnable ones outlined with a ring, locked ones a pencil sketch
                nodeFill: '#efe3c4',
                unlockedRim: '#3a2610',
                unlockedCore: '#f1e4c2',
                lockedStroke: '#7c6c52',
                mysteryFill: 'rgba(230, 218, 190, 0.9)',
                focusStroke: '#1c1208',
                ringTrack: '#3a2a14',
                availableAlpha: 1.0,
                availableRing: true,
                learningGlow: 0.3,

                // Heart: a parchment seal
                hubFill: '#efe3c4',
                hubRing: '#6b4a1c',
                hubText: '#4a3010',
                globeColor: '#7a5424',

                // Lines
                dimEdgeColor: '#b8a680',
                unlockedEdgeColor: '#5a3a14',
                unlockedEdgeAlpha: 0.75,
                unlockedEdgeWidth: 1.5,
                lockedEdgeColor: '#8a7a5e',
                lockedEdgeAlpha: 0.35,
                frontierEdgeAlpha: 0.6,
                selectedPathColor: '#8b1e1e',
                selectedPathAlpha: 0.85,
                selectedPathWidth: 2.5,
                hoverPathAlpha: 0.8,

                // Rubrics: chapter titles, sigil and heart runes in red ink
                accent: '#8b1e1e',
                selectionSigil: true,
                hubRunes: true,
                chapterTitles: true,
                dividerOrnament: true
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
     * An add-on cannot ship lines for lang/*.json, so its file may carry its own:
     * "names": { "ko": "...", "de": "..." } and "descriptions" the same way.
     */
    _localized: function(map) {
        if (!map || typeof map !== 'object' || typeof getLocale !== 'function') return '';
        var loc = getLocale();
        return (typeof map[loc] === 'string' && map[loc]) ? map[loc] : '';
    },

    nameOf: function(p) { return this._localized(p.names) || this._text(p.nameKey, p.name || '?'); },
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
