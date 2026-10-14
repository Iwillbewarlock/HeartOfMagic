/**
 * AutoBuild - the spell tree built at game start when there is none.
 *
 * With Settings > "Build Spell Tree at Start" on "When none" (AutoBuildSetting,
 * the default), a game start with no saved tree - spell_tree.json missing, or
 * saved without a single spell - scans and builds one in the closed panel, the
 * way the Scan tab's Scan, Build and Apply buttons would: the same scan, the
 * player's scanner preset (applied at start by onPresetsLoaded), the same build
 * request, PreReq Master's locks when it is on, the declutter pass and the same
 * save (TreeGrowthClassic.applyTree). No progress modal, no tab switch while the
 * panel is open. A saved tree is never rebuilt: when the load order changed
 * since it was built, the player is told once (the scan status line, and a
 * corner message in the game from the plugin) and can rebuild on the Scan tab.
 *
 * When it starts: once per game start, after all three of
 *   - onStartupTreeInfo (C++, at the end of LoadSpellTree): the tree file's
 *     state and the load order fingerprints (UIManagerAutoBuild.cpp),
 *   - the config (AutoBuildSetting.loadFrom -> noteConfigLoaded),
 *   - the scanner presets applied (onPresetsLoaded -> notePresetsApplied; after
 *     PRESETS_WAIT_MS without them it goes on with what is set).
 * That is at the main menu: onPrismaReady asks for all three. A build is not
 * started once a save has loaded (the scan holds the game thread for ~1.7 s).
 * A save load during a run (onSaveGameLoaded, which also resets the tree's
 * states) stops it: a step that comes back afterwards is dropped, and an Apply
 * still decluttering does not save (applyTree's isCurrent). spell_tree.json is
 * left as it was; the next game start builds again, or the Scan tab does.
 *
 * Every step goes to SpellLearning.log as "AutoBuild: <event> - <detail>"
 * (C++ AutoBuildEvent, written whatever the developer mode).
 *
 * Depends on: autoBuildSetting.js (AutoBuildSetting), buttonHandlers.js (startScan),
 *   treePreviewHidden.js (TreePreviewHidden), classic/classicMain.js
 *   (TreeGrowthClassic.buildTree/applyTree with { auto }), uiHelpers.js
 *   (updateScanStatus), i18n.js (tOr), state.js (state.lastSpellData)
 */

var AutoBuild = {

    BUILD: 'build',
    NOTICE: 'notice',
    NONE: 'none',

    // The tree file's state as C++ reports it (onStartupTreeInfo "tree")
    TREE_MISSING: 'missing',
    TREE_EMPTY: 'empty',
    TREE_PRESENT: 'present',

    PRESETS_WAIT_MS: 15000,   // scanner presets not in by then: go on with what is set
    RUN_TIMEOUT_MS: 600000,   // a run not done by then is dropped (scan + tome pass wait + build + locks + save)

    _info: null,
    _configReady: false,
    _presetsReady: false,
    _decided: false,
    _saveLoaded: false,
    _presetsTimer: null,
    _run: null,               // { stage: 'scan' | 'build' | 'apply', startedAt, generation }
    _generation: 0,           // +1 at every save load: a run started before one is stale
    _runTimer: null,

    // =========================================================================
    // DECISION (pure)
    // =========================================================================

    /**
     * What the game start does.
     * @param {string} mode - settings.autoBuild ('whenMissing' | 'off')
     * @param {Object|null} info - onStartupTreeInfo: { tree, nodes, loadOrder, builtLoadOrder, noticedLoadOrder }
     * @param {boolean} saveLoaded - a save has loaded already
     * @returns {{action: string, reason: string}} action BUILD, NOTICE or NONE
     */
    decide: function(mode, info, saveLoaded) {
        if (AutoBuildSetting.normalize(mode) === AutoBuildSetting.OFF) return this._result(this.NONE, 'the setting is off');
        if (!info || typeof info !== 'object') return this._result(this.NONE, 'nothing known about the saved tree');
        if (info.tree === this.TREE_MISSING || info.tree === this.TREE_EMPTY) {
            if (saveLoaded) return this._result(this.NONE, 'no saved tree, but a save is loaded already - build it on the Scan tab');
            return this._result(this.BUILD, info.tree === this.TREE_MISSING ? 'no saved tree' : 'the saved tree has no spells');
        }
        if (info.tree !== this.TREE_PRESENT) return this._result(this.NONE, 'the saved tree could not be read - left alone');
        var now = typeof info.loadOrder === 'string' ? info.loadOrder : '';
        var built = typeof info.builtLoadOrder === 'string' ? info.builtLoadOrder : '';
        if (!now || !built) return this._result(this.NONE, 'no load order recorded for the saved tree');
        if (built === now) return this._result(this.NONE, 'the saved tree matches the load order');
        if (info.noticedLoadOrder === now) return this._result(this.NONE, 'load order changed, the player was told already');
        return this._result(this.NOTICE, 'the load order changed since the tree was built');
    },

    _result: function(action, reason) {
        return { action: action, reason: reason };
    },

    // =========================================================================
    // WHEN TO DECIDE
    // =========================================================================

    /** C++'s facts about the saved tree (only the first, the game start's LoadSpellTree, counts). */
    onStartupTreeInfo: function(info) {
        if (this._info) return;
        this._info = info || {};
        var self = this;
        if (!this._presetsReady && !this._presetsTimer) {
            this._presetsTimer = setTimeout(function() {
                self._presetsTimer = null;
                if (self._presetsReady) return;
                self._report('presets', 'scanner presets not loaded after ' + self.PRESETS_WAIT_MS + ' ms - going on with the current settings');
                self._presetsReady = true;
                self._maybeDecide();
            }, this.PRESETS_WAIT_MS);
        }
        this._maybeDecide();
    },

    noteConfigLoaded: function() {
        this._configReady = true;
        this._maybeDecide();
    },

    notePresetsApplied: function() {
        this._presetsReady = true;
        if (this._presetsTimer) {
            clearTimeout(this._presetsTimer);
            this._presetsTimer = null;
        }
        this._maybeDecide();
    },

    onSaveGameLoaded: function() {
        this._saveLoaded = true;
        this._generation++;
        if (this._run) this._fail('a save loaded during the ' + this._run.stage + ' step - stopped, nothing saved');
    },

    /** The run is still the one going: not ended, and no save loaded since it started. */
    _current: function(run) {
        return !!run && this._run === run && run.generation === this._generation;
    },

    _maybeDecide: function() {
        if (this._decided || !this._info || !this._configReady || !this._presetsReady) return;
        this._decided = true;
        var d = this.decide(AutoBuildSetting.value(), this._info, this._saveLoaded);
        this._report('decided', d.action + ' (' + d.reason + ')');
        if (d.action === this.BUILD) this._start();
        else if (d.action === this.NOTICE) this._notice();
    },

    // =========================================================================
    // THE RUN
    // =========================================================================

    isRunning: function() {
        return this._run !== null;
    },

    _start: function() {
        var self = this;
        this._run = { stage: 'scan', startedAt: Date.now(), generation: this._generation };
        this._runTimer = setTimeout(function() {
            self._runTimer = null;
            self._fail('not done after ' + (self.RUN_TIMEOUT_MS / 1000) + ' s');
        }, this.RUN_TIMEOUT_MS);
        if (typeof startScan !== 'function') {
            this._fail('the panel has no scan function');
            return;
        }
        this._report('scan', 'scanning all spells');
        startScan();
    },

    /** updateSpellData finished (cppCallbacks.js): a scan came in. */
    onSpellData: function() {
        var run = this._run;
        if (!this._current(run) || run.stage !== 'scan') return;
        var data = typeof state !== 'undefined' ? state.lastSpellData : null;
        if (!data || !data.spells || data.spells.length === 0) {
            this._fail('the scan found no spells');
            return;
        }
        run.stage = 'build';
        if (!TreePreviewHidden.ensure()) {
            this._fail('no preview grid to place the spells on');
            return;
        }
        this._report('build', data.spells.length + ' spells scanned - building');
        var self = this;
        var started = TreeGrowthClassic.buildTree({
            auto: true,
            onComplete: function(summary) { if (self._current(run)) self._onBuilt(summary); },
            onFail: function(error) { if (self._current(run)) self._fail('the build failed: ' + error); }
        });
        if (!started) this._fail('the build did not start');
    },

    /** onScanFailed (uiHelpers.js). */
    onScanFailed: function(reason) {
        if (this._run && this._run.stage === 'scan') this._fail('the scan failed' + (reason ? ': ' + reason : ''));
    },

    _onBuilt: function(summary) {
        var run = this._run;
        if (!this._current(run) || run.stage !== 'build') return;
        run.stage = 'apply';
        this._report('apply', 'built' + (summary ? ' (' + summary + ')' : '') + ' - saving');
        var self = this;
        var sent = TreeGrowthClassic.applyTree({
            auto: true,
            isCurrent: function() { return self._current(run); },
            onSaved: function(count) { if (self._current(run)) self._finish(count + ' spells saved to spell_tree.json'); }
        });
        if (!sent) this._fail('no spell was placed - nothing saved');
    },

    _finish: function(detail) {
        if (!this._run) return;
        var secs = ((Date.now() - this._run.startedAt) / 1000).toFixed(1);
        this._report('done', detail + ' (' + secs + ' s)');
        this._end();
    },

    _fail: function(reason) {
        if (!this._run) return;
        this._report('failed', reason);
        if (typeof updateScanStatus === 'function') {
            var params = { error: reason };
            updateScanStatus(tOr('status.autoBuildFailed', params, 'Automatic tree build failed: {{error}}. Scan and build here instead.'),
                'error', 'status.autoBuildFailed', params);
        }
        this._end();
    },

    _end: function() {
        if (this._runTimer) {
            clearTimeout(this._runTimer);
            this._runTimer = null;
        }
        this._run = null;
    },

    _notice: function() {
        if (typeof updateScanStatus === 'function') {
            updateScanStatus(tOr('status.loadOrderChanged', null,
                'Load order changed since the tree was built. Scan and build here to rebuild it; the current tree keeps working.'),
                '', 'status.loadOrderChanged');
        }
        // The plugin keeps the corner message for the first save load (the main
        // menu has no corner) and remembers that this load order was told
        this._report('notice', 'load order ' + this._info.loadOrder + ', tree built for ' + this._info.builtLoadOrder);
    },

    /** One line in SpellLearning.log; "notice" also makes C++ show the corner message. */
    _report: function(event, detail) {
        console.log('[AutoBuild] ' + event + ': ' + detail);
        if (typeof window !== 'undefined' && window.callCpp) {
            window.callCpp('AutoBuildEvent', JSON.stringify({ event: event, detail: detail }));
        }
    },

    /** Back to a fresh game start (tests). */
    _resetForTest: function() {
        if (this._presetsTimer) clearTimeout(this._presetsTimer);
        if (this._runTimer) clearTimeout(this._runTimer);
        this._info = null;
        this._configReady = false;
        this._presetsReady = false;
        this._decided = false;
        this._saveLoaded = false;
        this._generation = 0;
        this._presetsTimer = null;
        this._run = null;
        this._runTimer = null;
    }
};

/** C++ (end of LoadSpellTree): { tree, nodes, loadOrder, builtLoadOrder, noticedLoadOrder }. */
window.onStartupTreeInfo = function(json) {
    var info = null;
    try {
        info = typeof json === 'string' ? JSON.parse(json) : json;
    } catch (e) {
        console.error('[AutoBuild] Startup tree info unreadable: ' + (e && e.message ? e.message : e));
    }
    AutoBuild.onStartupTreeInfo(info);
};

if (typeof window !== 'undefined') window.AutoBuild = AutoBuild;
