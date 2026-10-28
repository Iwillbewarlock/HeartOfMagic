/**
 * AutoBuild - what C++ tells the build at game start: the spell tome pairs changed
 * (onSpellTomesChanged), the tree graft (onTreeGrafted), and the window.* entry points
 * C++ calls, each reading its JSON once (_fromCpp).
 *
 * Split out of autoBuild.js to keep it under the size limit; the methods are added to
 * AutoBuild, so callers still write AutoBuild.onTreeGrafted() and so on. Load right
 * after autoBuild.js (which holds the run, its state fields and _resetForTest; see its
 * head for the tome pairs and the graft).
 *
 * Depends on: AutoBuild (autoBuild.js), uiHelpers.js (updateScanStatus, scanStatusKey),
 *   i18n.js (t, tOr), state.js (state.lastSpellData)
 */

(function() {
    var methods = {
        // =========================================================================
        // TOME PAIRS CHANGED
        // =========================================================================

        /**
         * C++ (SpellTomes.h): the spell tome pairs changed (a save's or an Import's Spell
         * Research pairs). See the head of autoBuild.js.
         * @param {{generation: number, pairedTomes: number, stale: boolean, inGame: boolean}} info - inGame:
         *   a save load or an Import, told before onSaveGameLoaded (C++ sends that one last): played from now
         */
        onSpellTomesChanged: function(info) {
            if (!info || typeof info !== 'object') return;
            if (info.inGame === true) this._saveLoaded = true;
            if (info.stale) this._markScanStale(info);
            if (this._retried) return;
            if (this._run) {
                this._pendingTomes = info;   // retried by _fail if this run fails; dropped when it finishes or yields
                return;
            }
            if (this._lastFailed) this._retry(info);
        },

        _retry: function(info) {
            this._retried = true;
            this._lastFailed = false;
            this._report('retry', 'the spell tome pairs changed (generation ' + info.generation + ', ' +
                info.pairedTomes + ' paired tomes) - the failed build runs once more');
            this._start();
        },

        _markScanStale: function(info) {
            if (typeof state !== 'undefined' && state.lastSpellData) state.lastSpellData.tomesStale = info.generation;
            if (typeof updateScanStatus === 'function') {
                var english = 'Spell Research brought spell tomes the last scan did not have. Scan again to take their spells into the tree.';
                updateScanStatus(typeof tOr === 'function' ? tOr('status.tomesChangedRescan', null, english) : english, '', 'status.tomesChangedRescan');
            }
        },

        // =========================================================================
        // THE TREE GRAFT
        // =========================================================================

        /**
         * C++ (TreeGraftRun.cpp): a tree graft at the main menu made the saved tree this load
         * order's (its new spells joined the tree). The startup info came before the graft:
         * from now on it says the tree was built for this load order, so no notice is decided,
         * and a notice already on the scan line for this load order goes.
         * @param {boolean} [reload] - C++ moved the tree file into place (a graft journal's
         *   recovery, spell_tree.json missing until then): a run, which would save over it, ends,
         *   and the tree is loaded again as at start; its startup info decides afresh
         */
        onTreeGrafted: function(loadOrder, reload) {
            if (reload === true) {
                var run = this._run;
                this.yieldTo('C++ put the saved tree file back');
                this._dropRunTree(run);
                this._clearWaits();   // the first info's config and presets waits: the reload's info starts its own
                this._info = null;
                this._decided = false;
                if (typeof window !== 'undefined' && window.callCpp) window.callCpp('LoadSpellTree', '');
            }
            if (typeof loadOrder !== 'string' || !loadOrder) return;
            this._graftedLoadOrder = loadOrder;
            if (!this._info || this._info.loadOrder !== loadOrder) return;
            this._info.builtLoadOrder = loadOrder;
            if (typeof scanStatusKey === 'function' && scanStatusKey() === 'status.loadOrderChanged' &&
                    typeof updateScanStatus === 'function') {
                updateScanStatus(typeof t === 'function' ? t('scanner.readyToScan') : 'Ready to scan', '', 'scanner.readyToScan');
            }
            this._report('grafted', 'the tree is load order ' + loadOrder + "'s now - no load order notice");
        },

        // =========================================================================
        // C++'S MESSAGES
        // =========================================================================

        /**
         * A message's JSON text (or what C++ sent already parsed), read once.
         * @param {string|Object} json
         * @param {string} what - the message, for the console line when it does not parse
         * @returns {*} the value, or null when it does not parse
         */
        _fromCpp: function(json, what) {
            try {
                return typeof json === 'string' ? JSON.parse(json) : json;
            } catch (e) {
                console.error('[AutoBuild] ' + what + ' unreadable: ' + (e && e.message ? e.message : e));
                return null;
            }
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) AutoBuild[key] = methods[key];
    }
})();

/** C++ (end of LoadSpellTree): { tree, nodes, loadOrder, builtLoadOrder, noticedLoadOrder }. */
window.onStartupTreeInfo = function(json) {
    AutoBuild.onStartupTreeInfo(AutoBuild._fromCpp(json, 'Startup tree info'));
};

/** C++ (TreeGraftRun.cpp, through UIManagerAutoBuild.cpp): { loadOrder, reload } - see AutoBuild.onTreeGrafted. */
window.onTreeGrafted = function(json) {
    var info = AutoBuild._fromCpp(json, 'Tree graft info');
    AutoBuild.onTreeGrafted(info && info.loadOrder, !!(info && info.reload === true));
};

/** C++ (UIManagerAutoBuild.cpp): the main menu opened. */
window.onMainMenuShown = function() {
    AutoBuild.onMainMenuShown();
};

/** C++ (SpellTomes.cpp, through UIManagerAutoBuild.cpp): { generation, pairedTomes, stale, reason }. */
window.onSpellTomesChanged = function(json) {
    AutoBuild.onSpellTomesChanged(AutoBuild._fromCpp(json, 'Spell tome change'));
};

/** C++ (UIManagerAutoBuild.cpp): kNewGame. */
window.onNewGameStarted = function() {
    AutoBuild.onNewGameStarted();
};
