/**
 * ResetConfirm - a Reset to Defaults button asks for a second click
 *
 * Used by Settings' Reset to Defaults (settingsPanel.js) and Early Learning's power steps reset (script.js).
 * The first click arms the button: its label reads "Click again to reset" (shown, not a hover title: Tab
 * closes the panel, so nothing may hide behind hover) and it turns red (panel-chrome.css `.btn.armed`).
 * A second click from MIN_CONFIRM_MS to ARM_MS after arming resets; a click sooner (the second half of a
 * double-click) is ignored, and after ARM_MS the label comes back. One button is armed at a time; hiding
 * the panel or switching tabs disarms it (disarm() with no argument). The label's own key and text are
 * kept on the button while armed and put back after, so each button keeps its own wording.
 * The same second-click pattern as deleting a preset (settingsPresets.js deleteSettingsPreset).
 *
 * Depends on: setI18n (i18n.js); the reset function is passed in by the caller
 */
var ResetConfirm = {
    ARM_MS: 3000,          // how long the armed button waits for the second click
    MIN_CONFIRM_MS: 400,   // a confirming click sooner than this after arming is a double-click: ignored

    CONFIRM_KEY: 'settings.actions.resetConfirm',
    CONFIRM_EN: 'Click again to reset',

    _timer: null,
    _armed: null,          // the armed button
    _armedAt: 0,

    _now: function() { return Date.now(); },

    /** The label span inside the button (its icon slot stays outside the translated text) */
    _label: function(btn) {
        return btn ? btn.querySelector('.hom-label') : null;
    },

    isArmed: function(btn) {
        return !!btn && btn.getAttribute('data-armed') === 'true';
    },

    /**
     * A click on the button: arms it, or runs onConfirm when it is armed.
     * @param {Element} btn
     * @param {function()} onConfirm
     * @returns {boolean} true when onConfirm ran
     */
    click: function(btn, onConfirm) {
        if (!btn) return false;
        if (this.isArmed(btn)) {
            if (this._now() - this._armedAt < this.MIN_CONFIRM_MS) return false;
            this.disarm(btn);
            onConfirm();
            return true;
        }
        this.disarm();
        var label = this._label(btn);
        if (label) {
            btn.setAttribute('data-reset-key', label.getAttribute('data-i18n') || '');
            btn.setAttribute('data-reset-text', label.textContent);
        }
        btn.setAttribute('data-armed', 'true');
        btn.classList.add('armed');
        setI18n(label, this.CONFIRM_KEY, null, this.CONFIRM_EN);
        this._armed = btn;
        this._armedAt = this._now();
        var self = this;
        this._timer = setTimeout(function() { self.disarm(btn); }, this.ARM_MS);
        return false;
    },

    /**
     * Back to the plain button and its own label.
     * @param {Element} [btn] - the armed button when left out
     */
    disarm: function(btn) {
        if (this._timer) {
            clearTimeout(this._timer);
            this._timer = null;
        }
        btn = btn || this._armed;
        if (btn === this._armed) this._armed = null;
        if (!btn || !this.isArmed(btn)) return;
        btn.removeAttribute('data-armed');
        btn.classList.remove('armed');
        var label = this._label(btn), key = btn.getAttribute('data-reset-key'), text = btn.getAttribute('data-reset-text');
        if (label && key) setI18n(label, key, null, text);
        else if (label) label.textContent = text || '';
        btn.removeAttribute('data-reset-key');
        btn.removeAttribute('data-reset-text');
    }
};
