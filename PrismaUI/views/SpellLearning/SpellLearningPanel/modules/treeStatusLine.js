/**
 * TreeStatusLine - the tree footer's status line (#tree-status-text): setTreeStatus, setTreeStatusKey
 *
 * Every write goes through here (moved from progressionUI.js in 1.6), so one rule covers all callers:
 *  - A success line that repeats what the footer already shows (BRIEF_KEYS: "Loaded 753 spells" next to
 *    "753 spells | 0 unlocked", "Tree saved") goes back to "Ready" after SHOW_MS.
 *  - Any other write cancels that timer, so a line written meanwhile is never wiped.
 *  - A warning or error (KEEP_KEYS, and plain text, which is a DLL message without a key) is not covered
 *    by a success line of the same step: a brief line arriving within KEEP_WINDOW_MS of it is dropped.
 *    An Import that finds unreachable spells writes the warning, then "Loaded" and "Tree saved" (the
 *    save's answer from C++) - the warning stays. Limit: the step is told by time, not by an id; a
 *    success that comes later than KEEP_WINDOW_MS (a new action) replaces the old warning, as it should,
 *    and one of the same step that came that late would too.
 *
 * Depends on: setI18n, clearI18n (i18n.js)
 */
var TreeStatusLine = {
    SHOW_MS: 4000,          // how long a brief success line stays before "Ready" comes back
    KEEP_WINDOW_MS: 10000,  // a brief line this soon after a warning belongs to the same step: dropped

    READY_KEY: 'footer.statusReady',
    READY_EN: 'Ready',

    BRIEF_KEYS: {
        'status.loadedSpells': true,
        'status.loadedTrustedTree': true,
        'status.treeSavedCount': true,
        'status.treeSaved': true
    },

    KEEP_KEYS: {
        'status.warningUnreachable': true,
        'status.loadedSpellsNotFound': true,
        'status.treeNoSpellsNotSaved': true,
        'status.treeBuildFailed': true,
        'status.saveFailed': true,
        'status.saveFileFailed': true,
        'status.savedTreeDamaged': true,
        'status.configUnreadable': true,
        'status.configNotSaved': true,
        'progression.failedUnlock': true,
        'editMode.rootNotDeleted': true
    },

    _timer: null,
    _keptAt: null,          // when the line now shown was a warning or error, its time

    _now: function() { return Date.now(); },

    _el: function() { return document.getElementById('tree-status-text'); },

    _cancel: function() {
        if (this._timer) {
            clearTimeout(this._timer);
            this._timer = null;
        }
    },

    /** A warning or error written within KEEP_WINDOW_MS is still on the line */
    _keeping: function() {
        return this._keptAt !== null && this._now() - this._keptAt < this.KEEP_WINDOW_MS;
    },

    write: function(key, params, fallback) {
        var el = this._el();
        if (!el) return;
        var brief = this.BRIEF_KEYS[key] === true;
        if (brief && this._keeping()) return;
        this._cancel();
        setI18n(el, key, params, fallback);
        this._keptAt = this.KEEP_KEYS[key] === true ? this._now() : null;
        if (!brief) return;
        var self = this;
        this._timer = setTimeout(function() {
            self._timer = null;
            setI18n(el, self.READY_KEY, null, self.READY_EN);
        }, this.SHOW_MS);
    },

    writeText: function(msg) {
        var el = this._el();
        if (!el) return;
        this._cancel();
        clearI18n(el);
        el.textContent = msg;
        this._keptAt = this._now();
    }
};

/** The status line as written, no key: a language switch leaves it alone (a DLL message without a key) */
function setTreeStatus(msg) {
    TreeStatusLine.writeText(msg);
}

/** The status line in the panel's language, written again by a language switch (setI18n) */
function setTreeStatusKey(key, params, fallback) {
    TreeStatusLine.write(key, params, fallback);
}
