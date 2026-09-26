/**
 * RenderSettings - the render popup on the tree (the gear in the zoom bar).
 *
 * One short page of what a player reaches for, as chips (checkboxes):
 *   Still everything - one master switch that stills every moving part
 *   Moving parts     - heart pulse, particle trail, star twinkle, and the
 *                      design's moving effects: opening, sigil, glow, runes
 *                      (designEffectsSetting.js)
 *   On the tree      - page, starfield, base lines, names
 * and a note on what costs frames: only what moves costs every frame; names
 * and lines only when the tree is redrawn (zoom, a change).
 *
 * Camera and selection behaviour, curved edges and the name size live in
 * Settings > UI Display > Tree; this module still puts their saved values
 * back (SWITCHES, syncControls), wherever the controls are.
 *
 * Colours, sizes, particle counts, star density and the like are no longer
 * here: the design preset decides them (its `render` block, DesignPresets.renderValue),
 * so a design is one coherent look and a player is not handed forty knobs.
 * The individual switches are wired in settingsPanel.js (initializeHeartSettings)
 * and designEffectsSetting.js; this module owns the master switch, the star
 * twinkle switch, and putting saved values back on every control listed here.
 *
 * Settings: animationsOff (bool, default false), starTwinkle (bool, default true),
 * renderSettingsVersion (see afterLoad).
 *
 * Depends on: settings, autoSaveSettings, applyHeartSettingsToRenderer,
 *             applyGlobeSettings, DesignEffectsSetting (all optional)
 */

var RenderSettings = {

    /** What the master switch stills: key -> the value that stills it (read through stilled()). */
    STILL: { heartAnimationEnabled: false, particleTrailEnabled: false, starTwinkle: false, globeSpin: false },

    /** Switches (popup and Settings > UI Display > Tree) and the setting each shows: on = true unless saved false, off = false unless saved true. */
    SWITCHES: [
        ['heart-animation-enabled', 'heartAnimationEnabled', 'on'],
        ['popup-particle-trail', 'particleTrailEnabled', 'on'],
        ['render-star-twinkle', 'starTwinkle', 'on'],
        ['starfield-enabled', 'starfieldEnabled', 'on'],
        ['show-base-connections', 'showBaseConnections', 'on'],
        ['popup-show-node-names', 'showNodeNames', 'on'],
        ['focus-on-click', 'focusOnClick', 'on'],
        ['focus-zoom-on-click', 'focusZoomOnClick', 'on'],
        ['focus-dim-others', 'focusDimOthers', 'on'],
        ['show-selection-path', 'showSelectionPath', 'on'],
        ['focus-rotate', 'focusRotate', 'off']
    ],

    /**
     * Values the popup used to offer and no longer does: a design's render block
     * decides them now. A config from before (no renderSettingsVersion) has
     * whatever the player once set; with no control left to change it, it would
     * stick for good and beat every design, so it goes back to the shipped value
     * once (afterLoad), and on Reset to Defaults.
     */
    DESIGN_OWNED: {
        heartPulseSpeed: 0.5, heartPulseDelay: 2.75,
        heartBgColor: '#000000', heartRingColor: '#b8a878', learningPathColor: '#00ffff',
        globeSize: 50, globeParticleRadius: 50, globeDensity: 50, globeDotMin: 0.5, globeDotMax: 1,
        globeColor: '#b8a878', magicTextColor: '#ffecb3', globeText: 'HEART', globeTextSize: 16,
        globeBgFill: true, particleCoreEnabled: false,
        starfieldColor: '#ffffff', starfieldDensity: 250, starfieldMaxSize: 3, starfieldSeed: 42,
        starfieldBgColor: '#000000'
    },
    VERSION: 2,

    init: function() {
        var self = this;

        var master = document.getElementById('render-animations-off');
        if (master) {
            master.addEventListener('change', function() {
                settings.animationsOff = this.checked;
                self._applyMaster();
                self.sync();
                if (typeof autoSaveSettings === 'function') autoSaveSettings();
            });
        }

        var twinkle = document.getElementById('render-star-twinkle');
        if (twinkle) {
            twinkle.addEventListener('change', function() {
                settings.starTwinkle = this.checked;
                if (typeof applyHeartSettingsToRenderer === 'function') applyHeartSettingsToRenderer();
                if (typeof autoSaveSettings === 'function') autoSaveSettings();
            });
        }

        this.syncControls();
    },

    /** Grey out what the master switch holds. */
    sync: function() {
        var off = settings.animationsOff === true;
        var master = document.getElementById('render-animations-off');
        if (master) master.checked = off;
        var held = document.getElementById('render-animation-parts');
        if (held) { if (off) held.classList.add('disabled-controls'); else held.classList.remove('disabled-controls'); }
    },

    /** Put the saved values on every popup control (the config arrives after the popup is wired). */
    syncControls: function() {
        this.SWITCHES.forEach(function(sw) {
            var box = document.getElementById(sw[0]);
            if (box) box.checked = sw[2] === 'on' ? settings[sw[1]] !== false : settings[sw[1]] === true;
        });
        var curved = document.getElementById('edge-style-toggle');
        if (curved) curved.checked = settings.edgeStyle === 'curved';
        var font = document.getElementById('tree-node-font-size');
        var fontVal = document.getElementById('tree-node-font-size-val');
        var fontSize = settings.nodeFontSize || 10;
        if (font) font.value = fontSize;
        if (fontVal) fontVal.textContent = fontSize;
        var zoom = document.getElementById('tree-focus-zoom');
        var zoomVal = document.getElementById('tree-focus-zoom-val');
        var focusZoom = (typeof settings.focusZoom === 'number' && settings.focusZoom > 0) ? settings.focusZoom : 1.0;
        if (zoom) zoom.value = focusZoom;
        if (zoomVal) zoomVal.textContent = Math.round(focusZoom * 100) + '%';
        if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.sync();
        this.sync();
    },

    /** From the config, early in the load (before the renderer keys). */
    loadFrom: function(data) {
        settings.animationsOff = !!(data && data.animationsOff === true);
        settings.starTwinkle = !(data && data.starTwinkle === false);
        this._applyMaster();
        this.sync();
    },

    /** At the end of the config load: retire old values, then show everything as loaded. */
    afterLoad: function(data) {
        if (!data || data.renderSettingsVersion !== this.VERSION) this._resetDesignOwned();
        settings.renderSettingsVersion = this.VERSION;
        this.syncControls();
    },

    saveTo: function(config) {
        config.animationsOff = settings.animationsOff === true;
        config.starTwinkle = settings.starTwinkle !== false;
        config.renderSettingsVersion = this.VERSION;
        return config;
    },

    reset: function() {
        settings.animationsOff = false;
        settings.starTwinkle = true;
        this._resetDesignOwned();
        this._applyMaster();
        this.syncControls();
    },

    _resetDesignOwned: function() {
        var d = this.DESIGN_OWNED;
        for (var k in d) {
            if (d.hasOwnProperty(k)) settings[k] = d[k];
        }
    },

    /** The master switch: still every moving part, or let each part's own switch decide again. */
    _applyMaster: function() {
        if (typeof applyHeartSettingsToRenderer === 'function') applyHeartSettingsToRenderer();
        if (typeof applyGlobeSettings === 'function') applyGlobeSettings();
        if (typeof DesignEffectsSetting !== 'undefined') DesignEffectsSetting.applyMaster(settings.animationsOff === true);
    },

    /**
     * The value a renderer setting has with the master switch in mind: stilled
     * while animationsOff is on, the player's otherwise. Used by the apply
     * functions in settingsPanel.js.
     * @param {string} key - a STILL key
     * @param {*} value - the player's value
     */
    stilled: function(key, value) {
        if (settings.animationsOff === true && this.STILL.hasOwnProperty(key)) return this.STILL[key];
        return value;
    }
};

window.RenderSettings = RenderSettings;
