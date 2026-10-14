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
 *   - the config (the end of onUnifiedConfigLoaded -> noteConfigLoaded, so the
 *     scan has the saved fields; after WAIT_MS without it nothing is built - the
 *     player's "Off" is in that config),
 *   - the scanner presets applied (onPresetsLoaded -> notePresetsApplied; after
 *     WAIT_MS without them it goes on with what is set).
 * That is at the main menu: onPrismaReady asks for all three. Only the first
 * onStartupTreeInfo counts: a later LoadSpellTree (Load Saved Tree on the tree
 * tab, or a second onPrismaReady if the view were reloaded) never starts a
 * second run. spell_tree.json is one file for every save, so a save load does
 * not stop a run (a new game is not sent to the panel at all) - scan, build and
 * Apply go on and save, and Apply's loadTreeData makes the tree live for the
 * loaded save (SetTreePrerequisites, then GetProgress / GetPlayerKnownSpells).
 * A decision made after a save has loaded still builds; the scan and the build
 * then run in game.
 *
 * The player wins: a Scan that goes out (past startScan's 2 s debounce), a
 * Build, an Apply or Clear with a tree to apply or clear, edit mode's spawn list
 * scan, or any tree save of theirs (Import, edit mode's Save Tree, orphan
 * repair: SpellTreeSave) ends a run first (yieldTo), without a failure on the
 * status line; so does a build of the player's found under way when the scan
 * comes back. The run takes the first full scan that comes back after it asked
 * (scanAsked): its own, or - scan results carry no request id, and C++ keeps
 * only the latest queued full scan - one the panel had sent just before the run
 * started, which is the same scan. The run's own scan is not debounced and the
 * run fails at once if it does not go out. While the run builds, opening the
 * panel shows the build's progress modal (BuildProgress.reveal), which covers
 * the panel as a build from the buttons does. A run also ends by finishing,
 * failing or RUN_TIMEOUT_MS; a step that comes back after that is dropped
 * (_current), and an Apply still decluttering then does not save (applyTree's
 * isCurrent).
 *
 * Every step goes to SpellLearning.log as "AutoBuild: <event> - <detail>"
 * (C++ AutoBuildEvent, written whatever the developer mode): config, presets,
 * decided, scan, build, shown, apply, done, failed, yielded, save loaded, notice.
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

    WAIT_MS: 15000,           // config or scanner presets not in by then: go on without them
    RUN_TIMEOUT_MS: 600000,   // a run not done by then is dropped (scan + tome pass wait + build + locks + save)

    _info: null,
    _configReady: false,
    _configMissing: false,    // the config wait ran out, or it was only partly applied: nothing is built
    _presetsReady: false,
    _decided: false,
    _saveLoaded: false,
    _waitTimers: [],
    _run: null,               // { stage: 'scan' | 'build' | 'apply', startedAt, scanAsked }
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
            return this._result(this.BUILD, (info.tree === this.TREE_MISSING ? 'no saved tree' : 'the saved tree has no spells') +
                (saveLoaded ? ', a save is loaded already - the scan and build run in game' : ''));
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
        this._waitFor('_configReady', 'config', 'not loaded after ' + this.WAIT_MS + ' ms - not building', '_configMissing');
        this._waitFor('_presetsReady', 'presets', 'scanner presets not loaded after ' + this.WAIT_MS + ' ms - going on with the current settings');
        this._maybeDecide();
    },

    /** After WAIT_MS without `flag`, report it, set `missedFlag` if given, and decide without it. */
    _waitFor: function(flag, event, detail, missedFlag) {
        if (this[flag]) return;
        var self = this;
        this._waitTimers.push(setTimeout(function() {
            if (self[flag]) return;
            self._report(event, detail);
            if (missedFlag) self[missedFlag] = true;
            self[flag] = true;
            self._maybeDecide();
        }, this.WAIT_MS));
    },

    noteConfigLoaded: function() {
        this._configReady = true;
        this._maybeDecide();
    },

    /** onUnifiedConfigLoaded threw part way: the player's choice may not be what was read. */
    noteConfigPartial: function() {
        if (this._configReady) return;
        this._report('config', 'partly applied - not building');
        this._configMissing = true;
        this.noteConfigLoaded();
    },

    /** onPanelShowing: a build of the run's under way shows its progress modal, as the buttons' does. */
    onPanelShowing: function() {
        if (this._run && this._run.stage === 'build' && typeof BuildProgress !== 'undefined' && BuildProgress.reveal()) {
            this._report('shown', 'the panel opened during the build - its progress is shown');
        }
    },

    notePresetsApplied: function() {
        this._presetsReady = true;
        this._maybeDecide();
    },

    onSaveGameLoaded: function() {
        this._saveLoaded = true;
        this._report('save loaded', this._run ? 'during the ' + this._run.stage + ' step - the run goes on' : 'no run under way');
    },

    /** The run is still the one going (not finished, failed, yielded or timed out). */
    _current: function(run) {
        return !!run && this._run === run;
    },

    _maybeDecide: function() {
        if (this._decided || !this._info || !this._configReady || !this._presetsReady) return;
        this._decided = true;
        this._clearWaits();
        var d = this._configMissing ? this._result(this.NONE, 'the config did not load - the setting is not known')
            : this.decide(AutoBuildSetting.value(), this._info, this._saveLoaded);
        this._report('decided', d.action + ' (' + d.reason + ')');
        if (d.action === this.BUILD) this._start();
        else if (d.action === this.NOTICE) this._notice();
    },

    _clearWaits: function() {
        for (var i = 0; i < this._waitTimers.length; i++) clearTimeout(this._waitTimers[i]);
        this._waitTimers = [];
    },

    // =========================================================================
    // THE RUN
    // =========================================================================

    isRunning: function() {
        return this._run !== null;
    },

    /**
     * The player did something to the tree themselves: a run under way ends now,
     * quietly - what it would still do comes back to a run that is gone.
     * @param {string} what - what the player did (for the log line)
     */
    yieldTo: function(what) {
        if (!this._run) return;
        this._report('yielded', what + ' (during the ' + this._run.stage + ' step)');
        this._end();
    },

    _start: function() {
        var self = this;
        this._run = { stage: 'scan', startedAt: Date.now() };
        this._runTimer = setTimeout(function() {
            self._runTimer = null;
            self._fail('not done after ' + (self.RUN_TIMEOUT_MS / 1000) + ' s');
        }, this.RUN_TIMEOUT_MS);
        if (typeof startScan !== 'function') {
            this._fail('the panel has no scan function');
            return;
        }
        this._report('scan', 'scanning all spells');
        this._run.scanAsked = true;   // the first full scan that comes back is this one's (onSpellData)
        if (!startScan({ auto: true })) this._fail('the scan did not go out');
    },

    /** updateSpellData finished (cppCallbacks.js): a scan came in. Only a full scan moves the run on. */
    onSpellData: function() {
        var run = this._run;
        if (!this._current(run) || run.stage !== 'scan' || !run.scanAsked) return;
        var data = typeof state !== 'undefined' ? state.lastSpellData : null;
        if (data && data.scanMode === 'spell_tomes') return;   // a tome scan: a filter list, not the tree's spells
        run.scanAsked = false;
        if (!data || !data.spells || data.spells.length === 0) {
            this._fail('the scan found no spells');
            return;
        }
        // A build of the player's under way (the Build button, or a tree built and not yet
        // applied): theirs wins, this run ends quietly
        if (state._classicGrowthBuildPending || (typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) ||
            (typeof TreeGrowthClassic !== 'undefined' && TreeGrowthClassic._treeData)) {
            this.yieldTo('a build of the player is under way');
            return;
        }
        run.stage = 'build';
        var self = this;
        try {
            if (!TreePreviewHidden.ensure()) {
                this._fail('no preview grid to place the spells on');
                return;
            }
            this._report('build', data.spells.length + ' spells scanned - building');
            var started = TreeGrowthClassic.buildTree({
                auto: true,
                onComplete: function(summary) { if (self._current(run)) self._onBuilt(summary); },
                onFail: function(error) { if (self._current(run)) self._fail('the build failed: ' + error); }
            });
            if (!started) this._fail('the build did not start');
        } catch (e) {
            this._fail('the build could not start: ' + (e && e.message ? e.message : e));
        }
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
        // The plugin shows the corner message at the first save load (the main
        // menu has none) and only then records that this load order was told
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
        this._clearWaits();
        if (this._runTimer) clearTimeout(this._runTimer);
        this._info = null;
        this._configReady = false;
        this._configMissing = false;
        this._presetsReady = false;
        this._decided = false;
        this._saveLoaded = false;
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
