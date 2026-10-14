/**
 * AutoBuild - the spell tree built at game start when there is none.
 *
 * With Settings > "Build Spell Tree at Start" on "When none" (AutoBuildSetting,
 * the default), a game start with no saved tree - spell_tree.json missing, or
 * saved without a single spell - scans and builds one in the closed panel, the
 * way the Scan tab's Scan, Build and Apply buttons would: the same scan, the
 * player's scanner preset (applied at start by onPresetsLoaded), the same build
 * request, PreReq Master's locks when it is on, the declutter pass and the same
 * save (TreeGrowthClassic.applyTree). No tab switch, and no progress modal unless
 * the panel is open during the build step (see below). A saved tree is never rebuilt: when the load order changed
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
 * not stop a run, nor does a new game (onNewGameStarted), and Apply's
 * loadTreeData makes the tree live for the loaded save (SetTreePrerequisites,
 * then GetProgress / GetPlayerKnownSpells). But no step starts during play: with
 * a save loaded or a new game started and the panel closed, the next step (scan, build or Apply - each
 * starts work on the game thread) is held until the panel opens or the main menu
 * (onMainMenuShown), whichever comes first, and the scan status line says so
 * (status.autoBuildHeld; C++ shows hud.autoBuildHeld in the corner once). A step
 * already started finishes. This also holds a decision made in game.
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
 * the panel as a build from the buttons does (the scan and Apply steps show none).
 * A run also ends by finishing, failing or RUN_TIMEOUT_MS; a step that comes back
 * after that is dropped (_current), an Apply still decluttering then does not save
 * (applyTree's isCurrent), and a run ending in its build step never leaves the
 * progress up and drops the C++ build result still to come (_dropBuild).
 * config.json that C++ could not read (its defaults sent, marked configNotRead)
 * counts as a config that did not load: nothing is built.
 *
 * Every step goes to SpellLearning.log as "AutoBuild: <event> - <detail>"
 * (C++ AutoBuildEvent, written whatever the developer mode): config, presets,
 * decided, scan, build, shown, apply, done, failed, yielded, save loaded, notice,
 * new game, held, resumed ("held" also makes C++ show the corner message, once a session).
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
    _saveLoaded: false,       // a game is being played (a save loaded or a new game), until the main menu
    _waitTimers: [],
    _run: null,               // { stage: 'scan' | 'build' | 'apply', startedAt, scanAsked, held: { step, fn } | null }
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
                (saveLoaded ? ', a save is loaded already - its steps wait for the panel or the main menu' : ''));
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
        // C++ could not read config.json and sent its defaults (configNotRead): the
        // player's choice, "Off" perhaps, is in the file - nothing is built this session
        if (!this._configReady && !this._configMissing && typeof AutoBuildSetting !== 'undefined' && AutoBuildSetting.notRead) {
            this._report('config', 'config.json could not be read - the setting is not known, not building');
            this._configMissing = true;
        }
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

    /**
     * onPanelShowing: a build of the run's under way shows its progress modal, as the
     * buttons' does; a step held during play goes on (the panel pauses the game by
     * default, "Pause Game on Open"), a tick later - after the panel's own opening work.
     */
    onPanelShowing: function() {
        this._reveal();
        var run = this._run, self = this;
        if (run && run.held) setTimeout(function() { if (self._current(run)) self._resume(run, 'the panel opened'); }, 0);
    },

    _reveal: function() {
        if (this._run && this._run.stage === 'build' && typeof BuildProgress !== 'undefined' && BuildProgress.reveal()) {
            this._report('shown', 'the panel opened during the build - its progress is shown');
        }
    },

    /** C++ (the main menu opened: at start, or the player quit a game): nothing is played, a held step goes on. */
    onMainMenuShown: function() {
        this._saveLoaded = false;
        var run = this._run;
        if (run && run.held) this._resume(run, 'the main menu');
    },

    notePresetsApplied: function() {
        this._presetsReady = true;
        this._maybeDecide();
    },

    onSaveGameLoaded: function() {
        this._inGame('save loaded');
    },

    /** C++ kNewGame (onNewGameStarted): played as much as a loaded save. */
    onNewGameStarted: function() {
        this._inGame('new game');
    },

    _inGame: function(event) {
        this._saveLoaded = true;
        this._report(event, this._run ? 'during the ' + this._run.stage + ' step - a step under way finishes, ' +
            'the next one waits for the panel or the main menu' : 'no run under way');
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

    // =========================================================================
    // HELD DURING PLAY
    // =========================================================================
    //
    // Each step starts work on the game thread: the scan (CollectAllSpells, then
    // FinishScan's perk adapters and the large InteropCall), the build (its request,
    // then the tree reply and the lock scoring), Apply (SaveSpellTree, then
    // SetTreePrerequisites over the whole tree). With a save loaded (or a new game)
    // and the panel closed a step does not start: it waits for the panel to open or the main
    // menu, whichever comes first. A step already started finishes - C++ cannot be
    // stopped part way. The timeout does not run while a step waits.

    /** A save is loaded (or a new game started) and the panel is closed: the game is being played. */
    _mustHold: function() {
        return this._saveLoaded && typeof window !== 'undefined' && window._panelVisible === false;
    },

    /** Run the step now, or hold it (and say why) while the game is being played. */
    _step: function(run, name, fn) {
        if (!this._mustHold()) {
            fn();
            return;
        }
        run.held = { step: name, fn: fn };
        this._clearRunTimer();
        this._report('held', 'the ' + name + ' step waits for the panel to open or the main menu - a save is loaded');
        if (typeof updateScanStatus === 'function') {
            var english = 'The spell tree will finish building when you open this panel.';
            updateScanStatus(typeof tOr === 'function' ? tOr('status.autoBuildHeld', null, english) : english, '', 'status.autoBuildHeld');
        }
    },

    _resume: function(run, why) {
        var held = run.held;
        if (!held) return;
        run.held = null;
        this._armTimer();   // a whole RUN_TIMEOUT_MS from here
        this._report('resumed', 'the ' + held.step + ' step - ' + why);
        if (typeof updateScanStatus === 'function') {
            updateScanStatus(typeof tOr === 'function' ? tOr('buildProgress.statusBuilding', null, 'Building tree...') : 'Building tree...',
                'working', 'buildProgress.statusBuilding');
        }
        this._step(run, held.step, held.fn);
    },

    _armTimer: function() {
        var self = this;
        this._clearRunTimer();
        this._runTimer = setTimeout(function() {
            self._runTimer = null;
            self._fail('not done after ' + (self.RUN_TIMEOUT_MS / 1000) + ' s');
        }, this.RUN_TIMEOUT_MS);
    },

    _clearRunTimer: function() {
        if (this._runTimer) {
            clearTimeout(this._runTimer);
            this._runTimer = null;
        }
    },

    // =========================================================================
    // THE STEPS
    // =========================================================================

    _start: function() {
        var self = this, run = { stage: 'scan', startedAt: Date.now(), held: null };
        this._run = run;
        this._armTimer();
        if (typeof startScan !== 'function') {
            this._fail('the panel has no scan function');
            return;
        }
        this._step(run, 'scan', function() { self._scan(run); });
    },

    _scan: function(run) {
        this._report('scan', 'scanning all spells');
        run.scanAsked = true;   // the first full scan that comes back is this one's (onSpellData)
        var sent = false;
        try {
            sent = startScan({ auto: true });
        } catch (e) {
            // Not left in 'scan' until RUN_TIMEOUT_MS
            this._fail('the scan could not start: ' + (e && e.message ? e.message : e));
            return;
        }
        if (!sent) this._fail('the scan did not go out');
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
        var self = this;
        this._step(run, 'build', function() { self._build(run); });
    },

    _build: function(run) {
        var data = state.lastSpellData;
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
                // The progress says so itself (closed when silent, the failure on it when shown)
                onFail: function(error) { if (self._current(run)) self._fail('the build failed: ' + error, true); }
            });
            if (!started) this._fail('the build did not start');
            // The panel already open: the progress shows now, not at the next opening
            else if (typeof window !== 'undefined' && window._panelVisible !== false) this._reveal();
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
        var self = this;
        this._step(run, 'apply', function() { self._apply(run, summary); });
    },

    _apply: function(run, summary) {
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

    /**
     * @param {string} reason - for the log and the scan status line
     * @param {boolean} [fromProgress] - the build's progress failed itself (BuildProgress.fail):
     *   it is closed already when silent, or shows the failure; it is left as it is
     */
    _fail: function(reason, fromProgress) {
        if (!this._run) return;
        this._report('failed', reason);
        var params = { error: reason };
        var english = 'Automatic tree build failed: {{error}}. Scan and build here instead.';
        var text = typeof tOr === 'function' ? tOr('status.autoBuildFailed', params, english) : english.replace('{{error}}', function() { return reason; });
        if (typeof updateScanStatus === 'function') updateScanStatus(text, 'error', 'status.autoBuildFailed', params);
        // The builder line still says "Building tree..." (a failed build set its own line already)
        if (this._run.stage === 'build' && !fromProgress && typeof ClassicSettings !== 'undefined' && ClassicSettings.setStatusText) {
            ClassicSettings.setStatusText(text, 'error', 'status.autoBuildFailed', params);
        }
        this._end(fromProgress ? this.PROGRESS_KEEP : text);
    },

    PROGRESS_KEEP: { keep: true },   // _end: the build's progress already says how it ended

    /**
     * @param {string|Object} [progress] - when the run ends in its build step: the
     *   failure line for a progress shown on the panel, PROGRESS_KEEP, or nothing (a
     *   yield or a finish: a progress still up is closed)
     */
    _end: function(progress) {
        this._clearRunTimer();
        var run = this._run;
        this._run = null;   // first: the progress's callbacks below find no run to report to
        if (run && run.stage === 'build') this._dropBuild(progress);
    },

    /**
     * The run ended in its build step. Its progress never stays up: a silent one, or a
     * shown one on a yield, is closed; a shown one on a failure says why and gets its
     * Close button (BuildProgress.fail). Left active it would also hold Apply and Clear
     * back. A build still in C++ is the run's (a player's Build yields first, then
     * queues behind it): its result is dropped when it comes - not loaded as a tree,
     * no locks, no "Tree built" over the failure line - unless a build of the player's
     * queued since takes its place (classicMain.js buildTree overwrites the queue).
     */
    _dropBuild: function(progress) {
        if (progress !== this.PROGRESS_KEEP && typeof BuildProgress !== 'undefined' && BuildProgress.isActive()) {
            if (typeof progress === 'string' && !BuildProgress.isSilent()) BuildProgress.fail(progress);
            else BuildProgress.close();
        }
        if (typeof state !== 'undefined' && state._classicGrowthBuildPending && !state._classicGrowthBuildQueued) {
            state._classicGrowthBuildQueued = function() {
                state._classicGrowthBuildPending = false;
                if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(false);
                console.warn('[AutoBuild] The tree builder answered after the run ended - its result is dropped');
            };
        }
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

/** C++ (UIManagerAutoBuild.cpp): the main menu opened. */
window.onMainMenuShown = function() {
    AutoBuild.onMainMenuShown();
};

/** C++ (UIManagerAutoBuild.cpp): kNewGame. */
window.onNewGameStarted = function() {
    AutoBuild.onNewGameStarted();
};

if (typeof window !== 'undefined') window.AutoBuild = AutoBuild;
