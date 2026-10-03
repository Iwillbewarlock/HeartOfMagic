/**
 * TreeBuilderSetting - the Classic builder options a player sets, shown in the
 * tree builder's settings (ClassicSettings.buildHTML) and saved with the other
 * settings in UnifiedConfig (settingsPanel.js: loadFrom, saveTo, reset).
 *
 *   Type grouping (settings.treeTypeGrouping, 0..2, default 1) - how strongly
 *   spells of one kind (bound weapons, runes) and one element (fire, frost,
 *   shock) stay on one branch; 0 builds as 1.2.0 did. Sent as the build
 *   config's "type_grouping" (C++ BuildConfig::typeGrouping, see
 *   docs/TREE_BUILDING_SYSTEM.md, "Type grouping").
 *
 *   Tiers by Magicka cost (settings.treeTiersByCost, default off) - each
 *   school's spells go into five tiers by Magicka cost rank instead of by their
 *   level (spells without a cost keep their level's tier). Sent as
 *   "tiers_by_cost" (BuildConfig::tiersByCost). The spells' levels - XP, the
 *   spell card - stay as they are.
 *
 * Takes effect at the next build; a built tree does not change.
 *
 * Depends on: settings (state.js), treePreviewUtils.js (settingHTML, bindInput),
 *             i18n.js (t), autoSaveSettings (all optional at load)
 */

var TreeBuilderSetting = {

    DEFAULT_TYPE_GROUPING: 1,
    MAX_TYPE_GROUPING: 2,
    TYPE_GROUPING_STEP: 0.1,

    /** The saved strength, in range and on the step (a bad value gives the default). */
    typeGrouping: function() {
        var v = (typeof settings !== 'undefined') ? Number(settings.treeTypeGrouping) : NaN;
        if (!isFinite(v)) return this.DEFAULT_TYPE_GROUPING;
        v = Math.max(0, Math.min(this.MAX_TYPE_GROUPING, v));
        return Number((Math.round(v / this.TYPE_GROUPING_STEP) * this.TYPE_GROUPING_STEP).toFixed(1));
    },

    /** Whether the tiers go by Magicka cost (off unless saved true). */
    tiersByCost: function() {
        return typeof settings !== 'undefined' && settings.treeTiersByCost === true;
    },

    /** Put the player's options into a build request's config. */
    applyTo: function(config) {
        config.type_grouping = this.typeGrouping();
        config.tiers_by_cost = this.tiersByCost();
        return config;
    },

    /** The controls, for the Classic settings panel. */
    html: function() {
        return '' +
            '<div class="tree-preview-settings-grid">' +
                TreePreviewUtils.settingHTML(t('preview.classic.typeGrouping'), 'tgClassicTypeGrouping',
                    0, this.MAX_TYPE_GROUPING, this.TYPE_GROUPING_STEP, this.typeGrouping().toFixed(1)) +
            '</div>' +
            '<div class="tree-preview-hint">' +
                t('preview.classic.typeGroupingHint') +
            '</div>' +
            '<div class="tree-preview-option">' +
                '<label>' +
                    '<input type="checkbox" id="tgClassicTiersByCost"' + (this.tiersByCost() ? ' checked' : '') + '>' +
                    t('preview.classic.tiersByCost') +
                '</label>' +
            '</div>' +
            '<div class="tree-preview-hint">' +
                t('preview.classic.tiersByCostHint') +
            '</div>';
    },

    /** Wire the controls html() made. */
    bind: function() {
        var self = this;
        TreePreviewUtils.bindInput('tgClassicTypeGrouping', function(v) {
            settings.treeTypeGrouping = Number(Number(v).toFixed(1));
            var field = document.getElementById('tgClassicTypeGrouping');
            if (field) field.value = self.typeGrouping().toFixed(1);
            if (typeof autoSaveSettings === 'function') autoSaveSettings();
        });
        var byCost = document.getElementById('tgClassicTiersByCost');
        if (byCost) {
            byCost.addEventListener('change', function() {
                settings.treeTiersByCost = this.checked;
                if (typeof autoSaveSettings === 'function') autoSaveSettings();
            });
        }
    },

    /** Show the saved values on the controls (the config can arrive after the panel is built). */
    syncControls: function() {
        var field = typeof document !== 'undefined' ? document.getElementById('tgClassicTypeGrouping') : null;
        if (field) field.value = this.typeGrouping().toFixed(1);
        var byCost = typeof document !== 'undefined' ? document.getElementById('tgClassicTiersByCost') : null;
        if (byCost) byCost.checked = this.tiersByCost();
    },

    loadFrom: function(data) {
        settings.treeTypeGrouping = (data && data.treeTypeGrouping !== undefined) ?
            data.treeTypeGrouping : this.DEFAULT_TYPE_GROUPING;
        settings.treeTypeGrouping = this.typeGrouping();
        settings.treeTiersByCost = !!(data && data.treeTiersByCost === true);
        this.syncControls();
    },

    saveTo: function(config) {
        config.treeTypeGrouping = this.typeGrouping();
        config.treeTiersByCost = this.tiersByCost();
    },

    reset: function() {
        settings.treeTypeGrouping = this.DEFAULT_TYPE_GROUPING;
        settings.treeTiersByCost = false;
        this.syncControls();
    }
};
