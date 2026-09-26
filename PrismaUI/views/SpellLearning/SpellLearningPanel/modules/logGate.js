/**
 * LogGate - console.log and console.info stay in the page unless developer mode is on.
 *
 * PrismaUI hands every console message to SpellLearning.dll
 * (UIManager::OnConsoleMessage), which writes info-level ones to the log only
 * in developer mode and drops them otherwise. Dropping them there still cost a
 * call across into native code per line, and the panel logs a lot: every
 * spell's progress on opening, every known spell, each step of a selection.
 * Now a player without developer mode pays nothing for them. Warnings and
 * errors always go through (the C++ side always keeps those).
 *
 * Load right after state.js (it reads settings.developerMode on each call, so
 * turning developer mode on in the settings takes effect at once).
 *
 * Depends on: settings (state.js)
 */

var LogGate = {

    _log: null,
    _info: null,

    /** Is anyone reading the info lines? */
    on: function() {
        return typeof settings !== 'undefined' && settings.developerMode === true;
    },

    install: function() {
        if (typeof console === 'undefined' || this._log) return;
        var self = this;
        this._log = console.log;
        this._info = console.info || console.log;
        console.log = function() {
            if (self.on()) self._log.apply(console, arguments);
        };
        console.info = function() {
            if (self.on()) self._info.apply(console, arguments);
        };
    }
};

LogGate.install();
window.LogGate = LogGate;
