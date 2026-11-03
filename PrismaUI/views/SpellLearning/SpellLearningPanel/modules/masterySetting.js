/**
 * MasterySetting - Settings > Learning > "Mastery Ranks": the switch for the ranks past mastery.
 *
 * On (the default), casting a spell already mastered earns mastery XP toward its ranks
 * (ProgressionManagerMastery.cpp; the spell card's row, spellCardMastery.js). Off, no cast earns
 * any and the card shows no row; XP already earned stays saved. Only the switch is here: the
 * numbers (ranks, XP per tier, rank multipliers, skill factor, XP rate) are config.json's "mastery"
 * keys, edited by hand (docs/ARCHITECTURE.md, "Mastery ranks").
 *
 * Saved as config.mastery.enabled only: the C++ save merges objects key by key, so the hand-written
 * numbers stay. Not in settings presets.
 *
 * Depends on: settings, state (masteryInfo, optional), autoSaveSettings (optional)
 */

var MasterySetting = {

    DEFAULT_ENABLED: true,

    init: function() {
        var self = this;
        var toggle = document.getElementById('masteryRanksToggle');
        if (toggle) {
            toggle.addEventListener('change', function() {
                self.set(this.checked);
                if (typeof autoSaveSettings === 'function') autoSaveSettings();
            });
        }
        this.sync();
    },

    /** The switch as saved (true unless set to false). */
    enabled: function() {
        return !(settings.mastery && settings.mastery.enabled === false);
    },

    /** Set the switch; the card follows at once (C++ takes it with the save). */
    set: function(on) {
        if (!settings.mastery || typeof settings.mastery !== 'object') settings.mastery = {};
        settings.mastery.enabled = on !== false;
        if (typeof state !== 'undefined' && state && state.masteryInfo) state.masteryInfo.enabled = settings.mastery.enabled;
        this.sync();
    },

    sync: function() {
        var toggle = document.getElementById('masteryRanksToggle');
        if (toggle) toggle.checked = this.enabled();
    },

    /** Read the switch from a config: a boolean mastery.enabled, else on. */
    loadFrom: function(data) {
        var section = (data && data.mastery && typeof data.mastery === 'object') ? data.mastery : {};
        settings.mastery = { enabled: typeof section.enabled === 'boolean' ? section.enabled : this.DEFAULT_ENABLED };
        this.sync();
    },

    /** Write the switch into a config: mastery.enabled only, never mastery as a whole. */
    saveTo: function(config) {
        if (!config.mastery || typeof config.mastery !== 'object') config.mastery = {};
        config.mastery.enabled = this.enabled();
        return config;
    },

    reset: function() {
        this.set(this.DEFAULT_ENABLED);
    }
};

window.MasterySetting = MasterySetting;
