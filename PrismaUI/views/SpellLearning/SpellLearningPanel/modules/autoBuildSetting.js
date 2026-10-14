/**
 * AutoBuildSetting - Settings > Hotkey > "Build Spell Tree at Start"
 * (settings.autoBuild, saved in config.json as "autoBuild").
 *
 *   "whenMissing" (default) - with no saved tree (spell_tree.json missing or
 *                  without spells) the game start scans and builds one
 *                  (AutoBuild); a tree built for another load order only gets
 *                  a notice, it is never rebuilt by itself
 *   "off"        - nothing at game start, not even the notice
 *
 * Not part of a settings preset (like the hotkey): it is about this install,
 * not about how the game plays.
 *
 * Depends on: settings (state.js), initSegmentedToggle / setSegmentedToggleValue
 *             (settingsPanel.js), autoSaveSettings (all optional at load)
 */

var AutoBuildSetting = {

    WHEN_MISSING: 'whenMissing',
    OFF: 'off',
    DEFAULT: 'whenMissing',
    CONTROL_ID: 'autoBuildToggle',

    /** loadFrom has run: the saved value (or the default for a config without it) is in. */
    loaded: false,

    /** A value this setting knows, else the default. */
    normalize: function(value) {
        return (value === this.WHEN_MISSING || value === this.OFF) ? value : this.DEFAULT;
    },

    /** The setting now. */
    value: function() {
        return this.normalize(typeof settings !== 'undefined' ? settings.autoBuild : undefined);
    },

    /** Wire the control (Settings tab, once). */
    init: function() {
        var self = this;
        if (typeof initSegmentedToggle !== 'function') return;
        initSegmentedToggle(this.CONTROL_ID, this.value(), function(value) {
            settings.autoBuild = self.normalize(value);
            if (typeof autoSaveSettings === 'function') autoSaveSettings();
        });
    },

    syncControl: function() {
        if (typeof setSegmentedToggleValue === 'function') setSegmentedToggleValue(this.CONTROL_ID, this.value());
    },

    /**
     * The config C++ sent is its defaults standing in for a config.json it could not
     * read (UIManagerConfig.cpp marks it configNotRead): the value above is not the
     * player's, and the game start builds nothing (AutoBuild.noteConfigLoaded).
     */
    notRead: false,

    loadFrom: function(data) {
        settings.autoBuild = this.normalize(data ? data.autoBuild : undefined);
        this.notRead = !!(data && data.configNotRead === true);
        this.loaded = true;
        this.syncControl();
        // The game start's decision hears of it at the end of onUnifiedConfigLoaded
        // (settingsPanel.js), once the rest of the config - the scan fields - is in
    },

    saveTo: function(config) {
        config.autoBuild = this.value();
    },

    reset: function() {
        settings.autoBuild = this.DEFAULT;
        this.syncControl();
    }
};

if (typeof window !== 'undefined') window.AutoBuildSetting = AutoBuildSetting;
