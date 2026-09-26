/**
 * ReverseUnlockSetting - Settings > Progression > "Known Higher Spells".
 *
 * A spell the player already knows (a higher spell from a tome, a vendor, another
 * mod) opens the spells below it: its direct prerequisites, or every spell down to
 * the root with "to the root" on. Opened spells cost a share of their XP, one
 * slider per tier of the opened spell (an Expert spell opened by a known Master
 * spell at 70%, say). With "own XP gain rates" on they also gain XP at their own
 * rates - overall multiplier, per-source multipliers and caps, the same controls
 * as for learning upward. The rule itself lives in recalculateNodeAvailability
 * (cppCallbacks.js), getRequiredXPForNode / getReverseUnlockXPShare
 * (progressionUI.js) and C++ ProgressionManager::IsUnlockedByKnownChild /
 * GetReverseUnlockXPShare / GetGainRates; this module is the controls and the
 * saved settings.
 *
 * Settings: reverseUnlock (bool), reverseUnlockToRoot (bool),
 * reverseUnlockXPNovice ... reverseUnlockXPMaster (0.1 - 1.0, shown in %),
 * reverseXpSeparate (bool), reverseXpGlobalMultiplier (x1 - x1000),
 * reverseXpMultiplierDirect/School/Any and reverseXpCapAny/School/Direct (0 - 100 %).
 *
 * Depends on: settings, autoSaveSettings, recalculateNodeAvailability (optional)
 */

var ReverseUnlockSetting = {

    TIERS: ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master'],
    MIN_PCT: 10,
    MAX_PCT: 100,

    DEFAULTS: {
        reverseUnlock: true,
        reverseUnlockToRoot: false,
        reverseUnlockXPNovice: 0.3,
        reverseUnlockXPApprentice: 0.4,
        reverseUnlockXPAdept: 0.5,
        reverseUnlockXPExpert: 0.7,
        reverseUnlockXPMaster: 0.8,
        reverseXpSeparate: false,
        reverseXpGlobalMultiplier: 1,
        reverseXpMultiplierDirect: 100,
        reverseXpMultiplierSchool: 50,
        reverseXpMultiplierAny: 10,
        reverseXpCapAny: 5,
        reverseXpCapSchool: 15,
        reverseXpCapDirect: 50
    },

    /** The gain sliders: setting, the upward setting it starts from, range, format. */
    GAIN: [
        { key: 'reverseXpGlobalMultiplier', from: 'xpGlobalMultiplier', id: 'reverseXpGlobalMultiplier', min: 1, max: 1000, times: true },
        { key: 'reverseXpMultiplierDirect', from: 'xpMultiplierDirect', id: 'reverseXpDirect', min: 0, max: 100 },
        { key: 'reverseXpMultiplierSchool', from: 'xpMultiplierSchool', id: 'reverseXpSchool', min: 0, max: 100 },
        { key: 'reverseXpMultiplierAny', from: 'xpMultiplierAny', id: 'reverseXpAny', min: 0, max: 100 },
        { key: 'reverseXpCapAny', from: 'xpCapAny', id: 'reverseXpCapAny', min: 0, max: 100 },
        { key: 'reverseXpCapSchool', from: 'xpCapSchool', id: 'reverseXpCapSchool', min: 0, max: 100 },
        { key: 'reverseXpCapDirect', from: 'xpCapDirect', id: 'reverseXpCapDirect', min: 0, max: 100 }
    ],

    /** Every saved key, for the config and for settings presets. */
    keys: function() {
        return Object.keys(this.DEFAULTS);
    },

    init: function() {
        var self = this;
        var enable = document.getElementById('reverseUnlockToggle');
        var toRoot = document.getElementById('reverseUnlockToRootToggle');
        var separate = document.getElementById('reverseXpSeparateToggle');
        if (!enable || !toRoot) return;

        enable.addEventListener('change', function() {
            settings.reverseUnlock = this.checked;
            self._changed();
        });
        toRoot.addEventListener('change', function() {
            settings.reverseUnlockToRoot = this.checked;
            self._changed();
        });
        this.TIERS.forEach(function(tier) {
            var slider = self._slider(tier);
            if (!slider) return;
            slider.addEventListener('input', function() {
                self._showPct(tier, parseInt(this.value, 10));
            });
            slider.addEventListener('change', function() {
                settings['reverseUnlockXP' + tier] = parseInt(this.value, 10) / 100;
                self._changed();
            });
        });
        if (separate) {
            separate.addEventListener('change', function() {
                settings.reverseXpSeparate = this.checked;
                if (this.checked && self._gainUntouched()) self._copyUpwardGain();
                self.sync();
                self._save();
            });
        }
        this.GAIN.forEach(function(g) {
            var slider = document.getElementById(g.id + 'Slider');
            if (!slider) return;
            slider.addEventListener('input', function() {
                settings[g.key] = parseInt(this.value, 10);
                self._showGain(g);
            });
            slider.addEventListener('change', function() {
                self._save();
            });
        });
        this.sync();
    },

    /** Put the saved values on the controls (after the config arrives). */
    sync: function() {
        var self = this;
        var enable = document.getElementById('reverseUnlockToggle');
        var toRoot = document.getElementById('reverseUnlockToRootToggle');
        var separate = document.getElementById('reverseXpSeparateToggle');
        if (enable) enable.checked = settings.reverseUnlock !== false;
        if (toRoot) toRoot.checked = settings.reverseUnlockToRoot === true;
        if (separate) separate.checked = settings.reverseXpSeparate === true;
        this.TIERS.forEach(function(tier) {
            var slider = self._slider(tier);
            if (!slider) return;
            var pct = Math.round((settings['reverseUnlockXP' + tier] || 1) * 100);
            pct = Math.max(self.MIN_PCT, Math.min(self.MAX_PCT, pct));
            slider.value = pct;
            self._showPct(tier, pct);
        });
        this.GAIN.forEach(function(g) {
            var slider = document.getElementById(g.id + 'Slider');
            if (slider) slider.value = settings[g.key];
            self._showGain(g);
        });
        this._enableDependents();
    },

    /** Read the saved settings from a config, keeping the defaults for what is missing or out of range. */
    loadFrom: function(data) {
        var d = this.DEFAULTS;
        var num = function(v, min, max, fallback) {
            return (typeof v === 'number' && isFinite(v) && v >= min && v <= max) ? v : fallback;
        };
        settings.reverseUnlock = data.reverseUnlock !== false;
        settings.reverseUnlockToRoot = data.reverseUnlockToRoot === true;
        this.TIERS.forEach(function(tier) {
            var key = 'reverseUnlockXP' + tier;
            settings[key] = num(data[key], 0.01, 1, d[key]);
        });
        settings.reverseXpSeparate = data.reverseXpSeparate === true;
        this.GAIN.forEach(function(g) {
            settings[g.key] = num(data[g.key], g.min, g.max, d[g.key]);
        });
        this.sync();
    },

    /** Write the settings into a config or a settings preset. */
    saveTo: function(config) {
        this.keys().forEach(function(key) {
            config[key] = settings[key];
        });
        return config;
    },

    reset: function() {
        var d = this.DEFAULTS;
        this.keys().forEach(function(key) {
            settings[key] = d[key];
        });
        this.sync();
    },

    _slider: function(tier) {
        return document.getElementById('reverseUnlockXp' + tier + 'Slider');
    },

    _showPct: function(tier, pct) {
        var out = document.getElementById('reverseUnlockXp' + tier + 'Value');
        if (out) out.textContent = pct + '%';
        this._fill(this._slider(tier), pct, this.MIN_PCT, this.MAX_PCT);
    },

    _showGain: function(g) {
        var v = settings[g.key];
        var out = document.getElementById(g.id + 'Value');
        if (out) out.textContent = g.times ? 'x' + v : v + '%';
        this._fill(document.getElementById(g.id + 'Slider'), v, g.min, g.max);
    },

    /** The filled part of the track, the way settingsPanel's sliders do it. */
    _fill: function(slider, v, min, max) {
        if (slider) slider.style.setProperty('--slider-fill', ((v - min) / (max - min) * 100) + '%');
    },

    /** No gain slider moved from its default yet. */
    _gainUntouched: function() {
        var d = this.DEFAULTS;
        return this.GAIN.every(function(g) { return settings[g.key] === d[g.key]; });
    },

    /** Turning the own rates on for the first time starts them from the upward ones. */
    _copyUpwardGain: function() {
        this.GAIN.forEach(function(g) {
            var v = settings[g.from];
            if (typeof v === 'number' && isFinite(v)) settings[g.key] = Math.max(g.min, Math.min(g.max, Math.round(v)));
        });
    },

    /** The range, the XP shares and the gain rates mean nothing with the rule off. */
    _enableDependents: function() {
        var self = this;
        var on = settings.reverseUnlock !== false;
        var gainOn = on && settings.reverseXpSeparate === true;
        var toRoot = document.getElementById('reverseUnlockToRootToggle');
        var separate = document.getElementById('reverseXpSeparateToggle');
        if (toRoot) toRoot.disabled = !on;
        if (separate) separate.disabled = !on;
        this.TIERS.forEach(function(tier) {
            var slider = self._slider(tier);
            if (slider) slider.disabled = !on;
        });
        this.GAIN.forEach(function(g) {
            var slider = document.getElementById(g.id + 'Slider');
            if (slider) slider.disabled = !gainOn;
        });
        var grid = document.getElementById('reverseXpGainControls');
        if (grid) grid.classList.toggle('disabled-controls', !gainOn);
    },

    _changed: function() {
        this._enableDependents();
        if (typeof recalculateNodeAvailability === 'function') recalculateNodeAvailability();
        if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
        if (state.selectedNode && typeof showSpellDetails === 'function') showSpellDetails(state.selectedNode);
        this._save();
    },

    _save: function() {
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
    }
};

window.ReverseUnlockSetting = ReverseUnlockSetting;
