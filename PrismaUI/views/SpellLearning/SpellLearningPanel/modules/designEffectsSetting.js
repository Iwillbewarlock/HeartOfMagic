/**
 * DesignEffectsSetting - Render settings > "Design Effects".
 *
 * A design preset can add effects to the tree: a still page instead of the
 * starfield, the selection sigil, the glow behind the
 * spell being learned, the runes round the heart. Each can be turned off here,
 * and stays off in every design (TreeStyle.setEffectsOff). A switch the current
 * design has no use for is greyed out.
 *
 * The starfield controls below are greyed out too while a page is drawn - the
 * page replaces the starfield - with a note saying so.
 *
 * Setting: designEffects { page, sigil, glow, runes } (true = on).
 *
 * Depends on: settings, TreeStyle, autoSaveSettings (optional)
 */

var DesignEffectsSetting = {

    /** The effects, from TreeStyle.EFFECTS; the switch for `key` is #design-effect-<key>. */
    _effects: function() {
        return (typeof TreeStyle !== 'undefined' && TreeStyle.EFFECTS) || [];
    },

    _box: function(fx) {
        return document.getElementById('design-effect-' + fx.key);
    },

    init: function() {
        var self = this;
        this._effects().forEach(function(fx) {
            var box = self._box(fx);
            if (!box) return;
            box.addEventListener('change', function() {
                self._flags()[fx.key] = this.checked;
                self._apply();
                if (typeof autoSaveSettings === 'function') autoSaveSettings();
            });
        });
        this._apply();
    },

    /** Put the saved switches on the controls and grey out what the design does not use. */
    sync: function() {
        var flags = this._flags();
        var used = (typeof TreeStyle !== 'undefined' && TreeStyle.overrides) || {};
        var self = this;
        this._effects().forEach(function(fx) {
            var box = self._box(fx);
            if (!box) return;
            box.checked = flags[fx.key] !== false;
            var inUse = fx.uses(used);
            box.disabled = !inUse;
            // The chip (or cell) round the switch, without Element.closest (older engines)
            var cell = box.parentNode;
            while (cell && cell !== document && !(cell.className && /\b(render-chip|popup-cell)\b/.test(String(cell.className)))) cell = cell.parentNode;
            if (cell && cell !== document) {
                if (!inUse) cell.classList.add('disabled-controls'); else cell.classList.remove('disabled-controls');
            }
        });

        // A page is drawn instead of the starfield: its controls do nothing then
        var pageShown = typeof TreeStyle !== 'undefined' && TreeStyle.tokens && !!TreeStyle.tokens.pageColor;
        var stars = document.getElementById('starfield-controls');
        if (stars) { if (pageShown) stars.classList.add('disabled-controls'); else stars.classList.remove('disabled-controls'); }
        var note = document.getElementById('starfield-page-note');
        if (note) { if (pageShown) note.classList.remove('hidden'); else note.classList.add('hidden'); }
    },

    loadFrom: function(data) {
        var saved = (data && typeof data.designEffects === 'object' && data.designEffects) || {};
        var flags = {};
        this._effects().forEach(function(fx) { flags[fx.key] = saved[fx.key] !== false; });
        settings.designEffects = flags;
        this._apply();
    },

    saveTo: function(config) {
        config.designEffects = JSON.parse(JSON.stringify(this._flags()));
        return config;
    },

    reset: function() {
        settings.designEffects = {};
        this._apply();
    },

    _flags: function() {
        if (!settings.designEffects || typeof settings.designEffects !== 'object') settings.designEffects = {};
        return settings.designEffects;
    },

    /**
     * The render popup's master "still everything" switch (RenderSettings): the
     * moving effects go off on top of the player's own switches, which keep
     * their values.
     * @param {boolean} on
     */
    MASTER_STILLS: { sigil: true, glow: true, runes: true },
    applyMaster: function(on) {
        this._master = on === true;
        this._apply();
    },

    /** Tell TreeStyle which effects are off, then refresh the controls. */
    _apply: function() {
        var flags = this._flags();
        var master = this._master === true;
        var self = this;
        var off = {};
        this._effects().forEach(function(fx) {
            off[fx.key] = flags[fx.key] === false || (master && self.MASTER_STILLS[fx.key] === true);
        });
        if (typeof TreeStyle !== 'undefined' && TreeStyle.setEffectsOff) TreeStyle.setEffectsOff(off);
        this.sync();
    }
};

window.DesignEffectsSetting = DesignEffectsSetting;
