/**
 * AutoBuildHoldTest - node tests for the build at game start held during play
 * (run-tests.js): with a save loaded and the panel closed, the run's next step
 * (scan, build, Apply - each starts work on the game thread) waits for the panel
 * to open or the main menu.
 *
 *   - decided after a save load: the scan is held, the status line says why,
 *     the corner message is asked for (C++ "held"), the timeout is not running;
 *   - a save load mid-run: the step under way finishes, the next one is held
 *     (build after the scan, Apply after the build);
 *   - the panel opening resumes it a tick later, the main menu at once; the
 *     timeout runs again from the resume, and only then;
 *   - the player's own Scan, Build, Apply or Clear while it waits wins (yieldTo);
 *   - a new game counts as a loaded save; the panel open: nothing is held;
 *   - the spell tome pairs changed (onSpellTomesChanged): a failed run starts once
 *     more, held in game until the panel opens; a stale scan is marked; no retry
 *     after a finished run, after the player acted, twice, or on a bad payload.
 *
 * Depends on: autoBuildTest.js (loads autoBuild.js and autoBuildSetting.js).
 * Swaps the globals it stubs (timers included) and puts them back.
 */

var AutoBuildHoldTest = {

    passed: 0,
    failed: 0,
    _print: (typeof console !== 'undefined') ? console.log.bind(console) : function() {},

    check: function(ok, name) {
        if (ok) { this.passed++; this._print('  [PASS] ' + name); }
        else { this.failed++; this._print('  [FAIL] ' + name); }
    },

    _with: function(g, stubs, fn) {
        var old = {}, had = {};
        for (var k in stubs) {
            if (!stubs.hasOwnProperty(k)) continue;
            had[k] = Object.prototype.hasOwnProperty.call(g, k);
            old[k] = g[k];
            g[k] = stubs[k];
        }
        try { fn(); } finally {
            for (var k2 in stubs) {
                if (!stubs.hasOwnProperty(k2)) continue;
                if (had[k2]) g[k2] = old[k2]; else delete g[k2];
            }
        }
    },

    /** A world for one run: fake timers, the run's calls, events and status keys in `w`. */
    _world: function(g) {
        var w = { calls: [], events: [], status: [], timers: [], builtWith: null, appliedWith: null };
        w.stubs = {
            _panelVisible: false,
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] } },
            setTimeout: function(fn, ms) { w.timers.push({ fn: fn, ms: ms, live: true }); return w.timers.length; },
            clearTimeout: function(id) { if (w.timers[id - 1]) w.timers[id - 1].live = false; },
            startScan: function() { w.calls.push('scan'); return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') w.events.push(JSON.parse(arg).event); },
            updateScanStatus: function(text, type, key) { w.status.push(key); },
            tOr: function(key, params, fallback) { return fallback; },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            TreeGrowthClassic: {
                _treeData: null,
                buildTree: function(opts) { w.calls.push('build'); w.builtWith = opts; return true; },
                applyTree: function(opts) { w.calls.push('apply'); w.appliedWith = opts; return true; }
            },
            BuildProgress: undefined,
            console: { log: function() {}, warn: function() {}, error: function() {} }
        };
        /** The live timers of `ms` (0: a resume a tick later; RUN_TIMEOUT_MS: the run's timeout). */
        w.live = function(ms) { return w.timers.filter(function(t) { return t.live && t.ms === ms; }); };
        w.fire = function(ms) { w.live(ms).forEach(function(t) { t.live = false; t.fn(); }); };
        return w;
    },

    _begin: function(g, saveFirst) {
        var A = g.AutoBuild;
        A._resetForTest();
        if (saveFirst) A.onSaveGameLoaded();
        g.AutoBuildSetting.loadFrom({});
        A.noteConfigLoaded();
        A.notePresetsApplied();
        g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        if (typeof g.AutoBuild === 'undefined' || typeof g.AutoBuildSetting === 'undefined') {
            this.check(false, 'AutoBuild and AutoBuildSetting loaded (autoBuildTest.js)');
            return { passed: this.passed, failed: this.failed };
        }
        this._testDecidedInGame(g);
        this._testBetweenSteps(g);
        this._testMainMenu(g);
        this._testPlayerWins(g);
        this._testPanelOpen(g);
        this._testTomesChanged(g);
        this._testTomesChangedNoRetry(g);
        this._testTomesChangedDuringRun(g);
        return { passed: this.passed, failed: this.failed };
    },

    _testDecidedInGame: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g), timeout = A.RUN_TIMEOUT_MS;
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, true);
                self.check(A.isRunning() && w.calls.length === 0 && w.events.indexOf('held') !== -1 &&
                    w.status.indexOf('status.autoBuildHeld') !== -1,
                    'decided after a save load, panel closed: the scan is held and the status line says why (C++ "held": the corner message)');
                self.check(w.live(timeout).length === 0, 'held: the run\'s timeout is not running');
                w.fire(timeout);
                self.check(A.isRunning() && w.events.indexOf('failed') === -1, 'held: no timeout fires, however long it waits');

                A.onPanelShowing();
                self.check(w.calls.length === 0 && w.live(0).length === 1, 'the panel opens: the scan goes on a tick later');
                g._panelVisible = true;
                w.fire(0);
                self.check(w.calls.join(',') === 'scan' && w.events.indexOf('resumed') !== -1 && w.live(timeout).length === 1,
                    '...then it scans, and the timeout runs again from there');
                A.onSpellData();
                self.check(w.calls.join(',') === 'scan,build', 'the panel still open: the build follows at once');
            } finally { A._resetForTest(); }
        });
    },

    _testBetweenSteps: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, false);
                A.onSaveGameLoaded();   // Continue pressed during the scan: the scan finishes
                A.onSpellData();
                self.check(w.calls.join(',') === 'scan' && w.events[w.events.length - 1] === 'held',
                    'a save load during the scan: the scan comes back, the build is held');
                A.onPanelShowing();
                g._panelVisible = true;
                w.fire(0);
                self.check(w.calls.join(',') === 'scan,build', 'the panel opens: the held build goes on');
                g._panelVisible = false;   // closed again during the build, the save still loaded
                w.builtWith.onComplete('ok');
                self.check(w.calls.join(',') === 'scan,build' && w.events[w.events.length - 1] === 'held',
                    'the build done with the panel closed in game: Apply is held');
                A.onPanelShowing();
                g._panelVisible = true;
                w.fire(0);
                w.appliedWith.onSaved(4);
                self.check(w.calls.join(',') === 'scan,build,apply' && !A.isRunning() && w.events[w.events.length - 1] === 'done',
                    'the next opening: Apply goes on and the run is done');
            } finally { A._resetForTest(); }
        });
    },

    _testMainMenu: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, true);
                g.onMainMenuShown();
                self.check(w.calls.join(',') === 'scan' && w.events.indexOf('resumed') !== -1,
                    'held, then the main menu: the scan goes on at once, the panel still closed');
                A.onSpellData();
                w.builtWith.onComplete('ok');
                self.check(w.calls.join(',') === 'scan,build,apply', '...and nothing more is held at the main menu');
                A.onSaveGameLoaded();   // a save loads while Apply works: it finishes
                w.appliedWith.onSaved(2);
                self.check(!A.isRunning() && w.events[w.events.length - 1] === 'done', 'a save load during Apply: it finishes and saves');

                w.calls.length = 0;
                self._begin(g, false);
                g.onNewGameStarted();   // New Game pressed during the scan
                A.onSpellData();
                self.check(w.calls.join(',') === 'scan' && w.events[w.events.length - 1] === 'held',
                    'a new game started during the scan: the build is held as for a loaded save');
            } finally { A._resetForTest(); }
        });
    },

    _testPlayerWins: function(g) {
        var self = this, A = g.AutoBuild, players = ['scan', 'build', 'apply', 'clear'];
        for (var i = 0; i < players.length; i++) {
            var w = this._world(g), what = players[i];
            this._with(g, w.stubs, function() {
                try {
                    self._begin(g, true);   // held at the scan
                    g._panelVisible = true;
                    A.onPanelShowing();     // the panel opens; the player presses before the tick
                    A.yieldTo(what);
                    w.fire(0);
                    self.check(!A.isRunning() && w.calls.length === 0 && w.events[w.events.length - 1] === 'yielded',
                        'the player\'s ' + what + ' while the run waits: theirs wins, the held step never runs');
                } finally { A._resetForTest(); }
            });
        }
    },

    _testPanelOpen: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        w.stubs._panelVisible = true;
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, true);
                A.onSpellData();
                w.builtWith.onComplete('ok');
                self.check(w.calls.join(',') === 'scan,build,apply' && w.events.indexOf('held') === -1,
                    'a save loaded but the panel open (the game paused): nothing is held');
            } finally { A._resetForTest(); }
        });
    },

    /** A main menu run whose scan finds no spell (no tome pairs yet): it fails. */
    _failAtMainMenu: function(g, w) {
        this._begin(g, false);
        g.state.lastSpellData = { spells: [] };
        g.AutoBuild.onSpellData();
        g.state.lastSpellData = { spells: [{ formId: '0x01' }] };
        return !g.AutoBuild.isRunning() && w.events[w.events.length - 1] === 'failed';
    },

    _testTomesChanged: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        this._with(g, w.stubs, function() {
            try {
                self.check(self._failAtMainMenu(g, w), 'tomes: the main menu scan finds no spell - the run fails');
                // C++'s order at a save load: the tome pairs first, onSaveGameLoaded last
                g.onSpellTomesChanged(JSON.stringify({ generation: 2, pairedTomes: 40, stale: true, inGame: true }));
                self.check(w.events.indexOf('retry') !== -1 && A.isRunning() && w.calls.join(',') === 'scan' &&
                    w.events[w.events.length - 1] === 'held',
                    'tomes: the pairs changed at a save load (told before the save) - the failed run starts once more, its scan held');
                A.onSaveGameLoaded();
                self.check(w.status.indexOf('status.tomesChangedRescan') !== -1 && g.state.lastSpellData.tomesStale === 2,
                    'tomes: a stale scan is marked, with the status line');
                A.onPanelShowing();
                g._panelVisible = true;
                w.fire(0);
                self.check(w.calls.join(',') === 'scan,scan', 'tomes: the panel opens - the retry scans');
                g.state.lastSpellData = { spells: [] };
                A.onSpellData();
                g.onSpellTomesChanged({ generation: 3, pairedTomes: 41, stale: false });
                self.check(!A.isRunning() && w.events.filter(function(e) { return e === 'retry'; }).length === 1,
                    'tomes: a second change after the retry failed too - no second retry');
            } finally { A._resetForTest(); }
        });
    },

    _testTomesChangedDuringRun: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        w.stubs._panelVisible = true;
        var applies = 0;
        w.stubs.TreeGrowthClassic.buildTree = function(opts) {
            w.calls.push('build');
            w.builtWith = opts;
            // the C++ tree comes in through loadTreeData, as classicMain.js does
            g.TreeGrowthClassic._treeData = { built: w.calls.length };
            g.AutoBuild.noteTreeData(g.TreeGrowthClassic._treeData);
            return true;
        };
        w.stubs.TreeGrowthClassic.applyTree = function(opts) { w.calls.push('apply'); w.appliedWith = opts; return ++applies > 1; };
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, false);
                A.onSpellData();
                g.onSpellTomesChanged({ generation: 2, pairedTomes: 7, stale: false });
                self.check(w.events.indexOf('retry') === -1 && A.isRunning(), 'tomes: a change during a run waits for its end');
                w.builtWith.onComplete('ok');   // Apply places nothing: the run fails, its tree unsaved
                self.check(w.events.indexOf('failed') !== -1 && w.events.indexOf('retry') !== -1 && A.isRunning(),
                    'tomes: the run fails after the change - the kept change makes the one retry');
                self.check(g.TreeGrowthClassic._treeData === null, 'tomes: the failed run\'s unsaved tree is dropped');
                A.onSpellData();
                self.check(w.calls.join(',') === 'scan,build,apply,scan,build' && w.events.indexOf('yielded') === -1,
                    'tomes: the retry builds (its build not taken for the player\'s)');
                g.TreeGrowthClassic._treeData = { player: true };
                w.builtWith.onComplete('ok');
                w.appliedWith.onSaved(3);
                self.check(!A.isRunning() && w.events[w.events.length - 1] === 'done', 'tomes: the retry saves its tree');
            } finally { A._resetForTest(); g.TreeGrowthClassic._treeData = null; }
        });
        this._testTomesTimeoutInBuild(g);
    },

    /** The run's tree came in (loadTreeData), then the run timed out waiting for the locks: dropped, the retry builds. */
    _testTomesTimeoutInBuild: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        w.stubs._panelVisible = true;
        w.stubs.TreeGrowthClassic.buildTree = function(opts) {
            w.calls.push('build');
            w.builtWith = opts;
            g.TreeGrowthClassic._treeData = { built: w.calls.length };
            g.AutoBuild.noteTreeData(g.TreeGrowthClassic._treeData);
            return true;
        };
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, false);
                A.onSpellData();
                g.onSpellTomesChanged({ generation: 2, pairedTomes: 7, stale: false });
                w.fire(A.RUN_TIMEOUT_MS);   // still in the build step (PreReq Master's locks)
                self.check(g.TreeGrowthClassic._treeData === null && w.events.indexOf('retry') !== -1,
                    'tomes: a run timed out with its tree in - the tree is dropped, the retry starts');
                A.onSpellData();
                self.check(w.calls.join(',') === 'scan,build,scan,build' && w.events.indexOf('yielded') === -1,
                    'tomes: the retry builds instead of yielding to the dropped tree');
                g.TreeGrowthClassic._treeData = { player: true };
                A.yieldTo('build');
                A.noteTreeData({ late: true });
                self.check(g.TreeGrowthClassic._treeData.player === true, 'tomes: a player\'s tree is not taken for the run\'s');
            } finally { A._resetForTest(); g.TreeGrowthClassic._treeData = null; }
        });
    },

    _testTomesChangedNoRetry: function(g) {
        var self = this, A = g.AutoBuild, w = this._world(g);
        w.stubs._panelVisible = true;
        this._with(g, w.stubs, function() {
            try {
                self._begin(g, false);
                A.onSpellData();
                w.builtWith.onComplete('ok');
                w.appliedWith.onSaved(3);
                g.onSpellTomesChanged({ generation: 2, pairedTomes: 5, stale: false });
                self.check(!A.isRunning() && w.events.indexOf('retry') === -1, 'tomes: after a finished run - no retry');

                self._failAtMainMenu(g, w);
                A.yieldTo('apply');
                g.onSpellTomesChanged({ generation: 2, pairedTomes: 5, stale: false });
                self.check(!A.isRunning() && w.events.indexOf('retry') === -1,
                    'tomes: the player applied a tree after the failure - no retry over it');

                self._failAtMainMenu(g, w);
                g.onSpellTomesChanged('{not json');
                g.onSpellTomesChanged(null);
                self.check(!A.isRunning() && w.events.indexOf('retry') === -1, 'tomes: a bad payload is ignored');
            } finally { A._resetForTest(); }
        });
    }
};

if (typeof window !== 'undefined') window.AutoBuildHoldTest = AutoBuildHoldTest;
if (typeof global !== 'undefined') global.AutoBuildHoldTest = AutoBuildHoldTest;
