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
 *   - a new game counts as a loaded save; the panel open: nothing is held.
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
    }
};

if (typeof window !== 'undefined') window.AutoBuildHoldTest = AutoBuildHoldTest;
if (typeof global !== 'undefined') global.AutoBuildHoldTest = AutoBuildHoldTest;
