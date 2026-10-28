/**
 * AutoBuildFlowTest - node tests for the build at game start against the
 * panel's own code paths (run-tests.js); autoBuildTest.js has the decision and
 * the run's steps with stand-ins.
 *
 *   - a save load during the run: the real onSaveGameLoaded, applyTree and
 *     loadTreeData - the build held until the main menu, a load during Apply lets
 *     it finish: reset, then SaveBuiltSpellTree, SetTreePrerequisites and
 *     GetProgress, in that order;
 *   - the waits, with a fake clock: no config (nothing built), no scanner
 *     presets, a run that never ends; the config arriving last;
 *   - which scan feeds the run (its own; not a tome scan, not the player's),
 *     a throw while the build starts, the Sun grid cache after ensure();
 *   - the player wins: their Build (real TreeGrowthClassic.buildTree and
 *     BuildProgress), an Import, or their Apply while the run's Apply declutters;
 *     an Apply, Clear or Scan that does nothing does not end the run; the
 *     player's Build waits for the run's C++ build, whose result is dropped;
 *   - a tree without spells: refused with the tree tab's error, `schools` as an
 *     array counts no spells, only a build's save goes to SaveBuiltSpellTree
 *     (an Import's to SaveImportedSpellTree).
 *
 * Depends on: autoBuildTest.js (loads the modules), cppCallbacks.js, treeLoad.js,
 * buttonHandlers.js and proceduralTreeBuilder.js (loaded here). Swaps the
 * globals it stubs and puts them back.
 */

var AutoBuildFlowTest = {

    passed: 0,
    failed: 0,
    _print: (typeof console !== 'undefined') ? console.log.bind(console) : function() {},
    _quiet: { log: function() {}, warn: function() {}, error: function() {} },

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

    /** A built tree as ClassicTreeOutput gives it: positions and trustPrereqs, so loadTreeData's fast path. */
    _output: function() {
        return { trustPrereqs: true, schools: { Destruction: { root: '0x01', nodes: [
            { formId: '0x01', x: 0, y: 0, tier: 0, children: ['0x02'] },
            { formId: '0x02', x: 10, y: 0, tier: 1, prerequisites: ['0x01'] }
        ] } } };
    },

    /** An element that takes whatever loadTreeData writes. */
    _element: function() {
        var noop = function() {};
        return { textContent: '', style: {}, setAttribute: noop, appendChild: noop, querySelector: function() { return null; },
            querySelectorAll: function() { return []; },
            classList: { add: noop, remove: noop, toggle: noop, contains: function() { return false; } } };
    },

    /** Starts a run the stand-in way: decided build, scan in. */
    _startRun: function(g) {
        var A = g.AutoBuild;
        A._resetForTest();
        g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
        A.notePresetsApplied();
        g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };   // node only
        try {
            if (typeof g.onSaveGameLoaded !== 'function') require('./cppCallbacks.js');
            var vm = require('vm'), fs = require('fs'), path = require('path');
            var scripts = [['treeLoad.js', 'loadTreeData'], ['buttonHandlers.js', 'startScan'],
                ['proceduralTreeBuilder.js', 'onProceduralTreeComplete']];
            for (var i = 0; i < scripts.length; i++) {
                if (typeof g[scripts[i][1]] === 'function') continue;
                var file = path.join(__dirname, scripts[i][0]);
                vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
            }
        } catch (e) {
            this.check(false, 'cppCallbacks.js, treeLoad.js, buttonHandlers.js, proceduralTreeBuilder.js load (' + e.message + ')');
            return { passed: this.passed, failed: this.failed };
        }
        if (typeof g.AutoBuild === 'undefined' || typeof g.TreeGrowthClassic === 'undefined') {
            this.check(false, 'AutoBuild and TreeGrowthClassic loaded (autoBuildTest.js loads them)');
            return { passed: this.passed, failed: this.failed };
        }
        this._testSaveLoadOrder(g);
        this._testWaits(g);
        this._testYield(g);
        this._testEmptyTree(g);
        this._testScans(g);
        this._testStartThrows(g);
        this._testSunCache(g);
        this._testConfigLast(g);
        this._testNoopYield(g);
        this._testQueuedBuild(g);
        return { passed: this.passed, failed: this.failed };
    },

    _testSaveLoadOrder: function(g) {
        var self = this, A = g.AutoBuild, log = [], pendingDeclutter = null;
        var tgc = g.TreeGrowthClassic, oldTree = tgc._treeData;
        var realReset = g.onResetTreeStates;
        this._with(g, {
            settings: { autoBuild: 'whenMissing', selectedRoots: {} },
            state: { lastSpellData: { spells: [{ formId: '0x01' }, { formId: '0x02' }] }, treeData: null },
            _panelVisible: false,
            startScan: function() { return true; },
            callCpp: function(name) { if (name !== 'AutoBuildEvent' && name !== 'LogMessage') log.push(name); },
            onResetTreeStates: function() { log.push('reset'); realReset(); },
            document: { getElementById: function(id) { return id === 'tree-container' ? null : self._element(); },
                querySelector: function() { return null; } },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            ClassicTreeOutput: { build: function() { return { output: self._output(), posCount: 2 }; } },
            LayoutDeclutter: { applyAsync: function(out, done) { pendingDeclutter = done; } },
            ClassicSettings: { setStatusText: function() {} },
            TreeParser: { withoutId: function(list, id) { return (list || []).filter(function(x) { return x !== id; }); },
                clampSoftNeeded: function(n) { return n || 0; } },
            PrereqEdit: { repair: function() { return 0; } },
            SpellCache: { requestBatch: function() {} },
            switchTab: function() {},
            setTreeStatusKey: function() {},
            updateScanStatus: function() {},
            updateUnlockedCount: function() {},
            _logToSKSE: function() {},
            tOr: function(key, params, fallback) { return fallback; },
            t: function(key) { return key; },
            TreePreview: undefined,
            CanvasRenderer: { startRenderLoop: function() { log.push('renderLoop'); }, _needsRender: false },
            OpenRefreshGate: undefined, TreeGrowth: undefined, PreReqMaster: undefined, TrustedRenderer: undefined,
            TreeStyle: undefined, NewSpells: undefined, SmartRenderer: undefined, WheelRenderer: undefined,
            console: this._quiet
        }, function() {
            try {
                var builtWith = null, heldThere = false;
                self._with(g, { TreeGrowthClassic: Object.create(tgc) }, function() {
                    g.TreeGrowthClassic.buildTree = function(opts) { builtWith = opts; return true; };
                    self._startRun(g);
                    g.onSaveGameLoaded();             // Continue pressed during the scan
                    A.onSpellData();                  // the build step waits: a save is loaded, the panel closed
                    heldThere = builtWith === null;
                    g.onMainMenuShown();              // back to the main menu: the build goes on
                    g.TreeGrowthClassic._treeData = { schools: {} };
                    builtWith.onComplete('ok');       // the real applyTree, declutter pending
                    g.onSaveGameLoaded();             // and again while it declutters
                    pendingDeclutter();
                });
                // SetTreePrerequisites twice: { clear } and then the list. With the panel closed
                // progress and known spells wait for its opening (onPanelShowing)
                var want = ['reset', 'reset', 'SaveBuiltSpellTree', 'SetTreePrerequisites', 'SetTreePrerequisites'];
                var order = log.filter(function(n) { return want.indexOf(n) !== -1; });
                self.check(heldThere && order.join(',') === want.join(',') && log.indexOf('GetProgress') === -1 && !A.isRunning(),
                    'save loads during the run: the build held until the main menu, a load during Apply\x27s declutter lets it finish - ' +
                    'reset, then the tree saved and made live; progress waits for the panel');
                g.onPanelShowing();
                self.check(log.indexOf('GetProgress') !== -1 && log.indexOf('GetPlayerKnownSpells') !== -1 &&
                    log.indexOf('renderLoop') !== -1, 'opening the panel asks for progress and known spells and starts drawing');
                self.check(g.state.treeData && g.state.treeData.nodes.length === 2,
                    'the saved tree is the one the panel holds after the save load');
            } finally {
                tgc._treeData = oldTree;
                A._resetForTest();
            }
        });
    },

    _testWaits: function(g) {
        var self = this, A = g.AutoBuild, timers = [], events = [], status = [], scans = 0;
        function fire(ms) {
            var due = timers.filter(function(t) { return !t.cleared && t.ms === ms; });
            due.forEach(function(t) { t.cleared = true; t.fn(); });
            return due.length;
        }
        this._with(g, {
            setTimeout: function(fn, ms) { var t = { fn: fn, ms: ms }; timers.push(t); return t; },
            clearTimeout: function(t) { if (t) t.cleared = true; },
            settings: { autoBuild: 'whenMissing' },
            startScan: function() { scans++; return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') events.push(JSON.parse(arg)); },
            updateScanStatus: function(text, type, key) { status.push(key); },
            tOr: function(key, params, fallback) { return fallback; },
            console: this._quiet
        }, function() {
            A._resetForTest();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });   // no config ever
            self.check(scans === 0, 'no config yet: no decision');
            fire(A.WAIT_MS);
            self.check(scans === 0 && !A.isRunning() && events[0].event === 'config' && /not building/.test(events[0].detail) &&
                events[1].event === 'decided' && /^none/.test(events[1].detail), 'config not loaded in time: nothing is built');

            A._resetForTest(); events = []; status = [];
            g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            fire(A.RUN_TIMEOUT_MS);
            var last = events[events.length - 1];
            self.check(!A.isRunning() && last.event === 'failed' && /not done after 600 s/.test(last.detail) &&
                status.indexOf('status.autoBuildFailed') !== -1, 'a run not done in time fails and says so');

            A._resetForTest(); events = []; scans = 0;
            g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });   // no presets ever
            fire(A.WAIT_MS);
            self.check(scans === 1 && events[0].event === 'presets' && events[1].event === 'decided',
                'scanner presets not loaded in time: decided with the current settings');
            A._resetForTest();
        });
    },

    _testYield: function(g) {
        var self = this, A = g.AutoBuild, events, status, cpp, shown, sends;
        var tgc = g.TreeGrowthClassic, oldTree = tgc._treeData, pending;
        var modal = { classList: { remove: function() { shown++; }, add: function() {} } };
        function reset() {
            events = []; status = []; cpp = []; shown = 0; sends = 0; pending = [];
            if (g.state) { g.state._classicGrowthBuildPending = false; g.state._classicGrowthBuildQueued = null; }
            tgc._treeData = null;
        }
        reset();
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] }, treeData: null },
            _panelVisible: false,
            startScan: function() { return true; },
            callCpp: function(name, arg) {
                if (name === 'AutoBuildEvent') events.push(JSON.parse(arg).event);
                else cpp.push(name);
                if (name === 'SaveSpellTree' || name === 'SaveBuiltSpellTree' || name === 'SaveImportedSpellTree') sends++;
            },
            document: { getElementById: function(id) { return id === 'build-progress-modal' ? modal : null; } },
            updateScanStatus: function(text, type, key) { status.push(key); },
            tOr: function(key, params, fallback) { return fallback; },
            t: function(key) { return key; },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            ClassicSettings: { setStatusText: function() {}, setTreeBuilt: function() {} },
            ClassicBuildRequest: { send: function() {} },
            TreeGrowth: { setBuilding: function() {}, _markDirty: function() {} },
            ClassicTreeOutput: { build: function() { return { output: self._output(), posCount: 2 }; } },
            LayoutDeclutter: { applyAsync: function(out, done) { pending.push(done); } },
            loadTreeData: function() {},
            setTreeStatusKey: function() {},
            TreePreview: undefined,
            PreReqMaster: undefined,
            console: this._quiet
        }, function() {
            try {
                // The player's Build while the run's build is under way
                self._startRun(g);
                A.onSpellData();                                   // real buildTree({ auto }): silent progress
                var autoActive = g.BuildProgress.isActive();
                tgc.buildTree();                                   // the player's Build
                self.check(autoActive && !A.isRunning() && events.indexOf('yielded') !== -1 && events.indexOf('failed') === -1 &&
                    status.indexOf('status.autoBuildFailed') === -1 && shown === 1 && g.BuildProgress.isActive(),
                    'the player\'s Build during the run: the run yields quietly, the player\'s build shows its modal');
                g.BuildProgress.complete('done');
                // Not silent: the modal closes itself 2.5 s later, so only what the run did is checked
                self.check(sends === 0 && !A.isRunning() && events.indexOf('apply') === -1,
                    'the player\'s build completes; the run applies nothing');
                g.BuildProgress.close();

                // An Import while the run's Apply declutters
                reset();
                self._startRun(g);
                A.onSpellData();
                tgc._treeData = { schools: {} };
                g.BuildProgress.complete('ok');                     // the run's build done: its Apply, declutter pending
                var importSent = g.SpellTreeSave.send(self._output(), 'import');
                pending[0]();                                       // the run's declutter ends after it
                self.check(importSent && sends === 1 && cpp.indexOf('SaveBuiltSpellTree') === -1 && !A.isRunning() &&
                    events.indexOf('yielded') !== -1, 'an Import during the run: the run\'s Apply saves nothing over it');

                // The player's Apply while the run's Apply declutters
                reset();
                self._startRun(g);
                A.onSpellData();
                tgc._treeData = { schools: {} };                     // the C++ tree came back
                g.BuildProgress.complete('ok');
                tgc.applyTree();                                    // a second declutter (the first one is dropped)
                self.check(!A.isRunning() && events.indexOf('yielded') !== -1 && events.indexOf('failed') === -1,
                    'the player\'s Apply during the run\'s declutter: the run ends, no failure');
                pending[1]();
                pending[0]();
                self.check(sends === 1 && cpp.indexOf('SaveBuiltSpellTree') !== -1,
                    'only the player\'s Apply saves, as a build');
            } finally {
                tgc._treeData = oldTree;
                g.BuildProgress.close();
                A._resetForTest();
            }
        });
    },

    _testEmptyTree: function(g) {
        var self = this, keys = [], cpp = [];
        this._with(g, {
            callCpp: function(name) { cpp.push(name); },
            setTreeStatusKey: function(key) { keys.push(key); },
            console: this._quiet
        }, function() {
            var S = g.SpellTreeSave;
            var empty = { schools: { Destruction: { nodes: [] } } };
            self.check(!S.send(empty, 'import') && !S.send(empty, 'edit mode save') && cpp.length === 0 &&
                keys.length === 2 && keys[0] === 'status.treeNoSpellsNotSaved' && keys[1] === keys[0],
                'Import and edit mode save of a tree without spells: refused, the tree tab says so');
            self.check(S.countSpellNodes({ schools: [{ nodes: [{ formId: '0x01' }] }] }) === 0,
                '`schools` as an array counts no spells (as C++)');
            cpp.length = 0;
            S.send({ schools: { A: { nodes: [{ formId: '0x01' }] } } }, 'import');
            S.send({ schools: { A: { nodes: [{ formId: '0x01' }] } } }, 'apply');
            S.send({ schools: { A: { nodes: [{ formId: '0x01' }] } } }, 'auto build');
            S.send({ schools: { A: { nodes: [{ formId: '0x01' }] } } }, 'edit mode save');
            self.check(cpp.join(',') === 'SaveImportedSpellTree,SaveBuiltSpellTree,SaveBuiltSpellTree,SaveSpellTree',
                'a build\'s save goes to SaveBuiltSpellTree (its load order recorded), an Import to SaveImportedSpellTree, ' +
                'any other to SaveSpellTree');
        });
    },
    /** What the real startScan (buttonHandlers.js) needs, with its ScanSpells calls counted in `out`. */
    _scanStubs: function(out, extra) {
        var stubs = {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: null, fields: { name: true } },
            callCpp: function(name, arg) {
                if (name === 'AutoBuildEvent') out.events.push(JSON.parse(arg).event);
                else if (name === 'ScanSpells') out.scans.push(JSON.parse(arg));
            },
            t: function(key) { return key; },
            tOr: function(key, params, fallback) { return fallback; },
            updateStatus: function() {},
            setStatusIcon: function() {},
            updateScanStatus: function(text, type, key) { out.status.push(key); },
            getTreeRulesPrompt: function() { return ''; },
            HomIcons: { setLabel: function() {} },
            document: { getElementById: function() { return null; } },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            TreeGrowthClassic: { buildTree: function() { out.builds++; return true; } },
            console: this._quiet
        };
        for (var k in extra) if (extra.hasOwnProperty(k)) stubs[k] = extra[k];
        return stubs;
    },

    _testScans: function(g) {
        var self = this, A = g.AutoBuild, out = { events: [], scans: [], status: [], builds: 0 };
        this._with(g, this._scanStubs(out, {}), function() {
            g._lastScanTime = 0;
            self._startRun(g);
            self.check(out.scans.length === 1 && out.scans[0].scanMode === 'all' && out.events.indexOf('yielded') === -1,
                'the run\'s own scan goes out through startScan without ending the run');

            g.state.lastSpellData = { scanMode: 'spell_tomes', spells: [{ formId: '0x01' }] };
            A.onSpellData();
            self.check(A.isRunning() && out.builds === 0, 'a tome scan does not feed the run');

            g.startScan();   // the player's Scan within 2 s of the run's: debounced, nothing happens
            self.check(A.isRunning() && out.scans.length === 1 && out.events.indexOf('yielded') === -1,
                'a debounced Scan press does not end the run');

            g._lastScanTime = 0;
            g.startScan();   // the player's Scan that goes out
            g.state.lastSpellData = { spells: [{ formId: '0x01' }] };
            A.onSpellData();   // whichever full scan comes back now
            self.check(!A.isRunning() && out.scans.length === 2 && out.events.indexOf('yielded') !== -1 && out.builds === 0 &&
                out.status.indexOf('status.autoBuildFailed') === -1, 'the player\'s Scan ends the run; its result feeds no build');

            out.builds = 0;
            g._lastScanTime = 0;
            self._startRun(g);
            A.onSpellData();   // the run's scan
            A._run.stage = 'scan';   // as if a second scan came back to a run still scanning
            A.onSpellData();
            self.check(out.builds === 1, 'only the first full scan after the run asked feeds it');
            A._resetForTest();
        });
    },

    _testStartThrows: function(g) {
        var self = this, A = g.AutoBuild;
        var cases = [
            ['ensure()', { ensure: function() { throw new Error('boom'); } }, null],
            ['buildTree()', null, { buildTree: function() { throw new Error('bang'); } }]
        ];
        for (var i = 0; i < cases.length; i++) {
            var out = { events: [], scans: [], status: [], builds: 0 }, extra = { state: { lastSpellData: { spells: [{ formId: '0x01' }] } } };
            if (cases[i][1]) extra.TreePreviewHidden = cases[i][1];
            if (cases[i][2]) extra.TreeGrowthClassic = cases[i][2];
            this._with(g, this._scanStubs(out, extra), function() {
                g._lastScanTime = 0;
                self._startRun(g);
                A.onSpellData();
                self.check(!A.isRunning() && out.events[out.events.length - 1] === 'failed' &&
                    out.status.indexOf('status.autoBuildFailed') !== -1, cases[i][0] + ' throws: the run fails and says so');
                A._resetForTest();
            });
        }
    },

    _testSunCache: function(g) {
        var self = this, sun = g.TreePreviewSun;
        var preview = Object.create(g.TreePreview);
        preview.modes = { sun: sun };
        preview.activeMode = 'sun';
        preview.schoolData = { Destruction: 10, Restoration: 10 };
        preview._width = 0;
        preview._height = 0;
        this._with(g, { settings: { selectedRoots: {} }, console: this._quiet }, function() {
            try {
                sun._lastRenderData = null;
                sun._gridPointCache = null;
                var out = g.TreePreviewHidden.ensure(preview);
                self.check(out && out.gridPoints.length > 0 && sun._gridPointCache === null && sun._gridPointCacheKey === '',
                    'after ensure() the Sun grid point cache is empty (the preview on screen works its own out)');
            } finally {
                sun._lastRenderData = null;
                sun._gridPointCache = null;
            }
        });
    },

    _testConfigLast: function(g) {
        var self = this, A = g.AutoBuild, fieldsSeen = null;
        var out = { events: [], scans: [], status: [], builds: 0 };
        this._with(g, this._scanStubs(out, {
            startScan: function() { fieldsSeen = JSON.stringify(g.state.fields); return true; }
        }), function() {
            A._resetForTest();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            g.AutoBuildSetting.loadFrom({ autoBuild: 'whenMissing' });   // the value, first thing in onUnifiedConfigLoaded
            var early = fieldsSeen;
            g.state.fields = { name: true, effects: true };                 // the saved fields, later in it
            A.noteConfigLoaded();                                            // its finally
            self.check(early === null && fieldsSeen === JSON.stringify({ name: true, effects: true }),
                'config arriving last: the scan starts after the saved fields are in');
            A._resetForTest();
        });
        if (typeof require !== 'function') return;
        var src = require('fs').readFileSync(require('path').join(__dirname, 'settingsPanel.js'), 'utf8');
        var start = src.indexOf('window.onUnifiedConfigLoaded = function');
        var fields = src.indexOf('state.fields[savedField] = data.fields[savedField]', start);
        var fin = src.indexOf('} finally {', fields);
        var note = src.indexOf('AutoBuild.noteConfigLoaded()', start);
        self.check(start !== -1 && fields !== -1 && fin !== -1 && note > fin,
            'onUnifiedConfigLoaded tells the build at game start in its finally, after the saved fields');
    },

    _testNoopYield: function(g) {
        var self = this, A = g.AutoBuild, tgc = g.TreeGrowthClassic, oldTree = tgc._treeData;
        var out = { events: [], scans: [], status: [], builds: 0 };
        this._with(g, this._scanStubs(out, {
            TreeGrowthClassic: tgc,
            ClassicSettings: { setStatusText: function() {}, setTreeBuilt: function() {} },
            TreeGrowth: { _markDirty: function() {} }
        }), function() {
            try {
                g._lastScanTime = 0;
                self._startRun(g);
                tgc._treeData = null;
                tgc.applyTree();   // nothing built: nothing to apply
                tgc.clearTree();   // nothing to clear
                self.check(A.isRunning() && out.events.indexOf('yielded') === -1, 'an Apply or Clear with no tree does not end the run');
                tgc._treeData = { schools: {} };
                tgc.clearTree();
                self.check(!A.isRunning() && out.events.indexOf('yielded') !== -1, 'a Clear of a built tree ends it');
            } finally {
                tgc._treeData = oldTree;
                A._resetForTest();
            }
        });
    },

    /** The player's Build while C++ still builds the run's request (C++ would answer it busy). */
    _testQueuedBuild: function(g) {
        var self = this, A = g.AutoBuild, tgc = g.TreeGrowthClassic, sent = 0, loaded = [], shown = 0;
        var modal = { classList: { remove: function() { shown++; }, add: function() {} } };
        var out = { events: [], scans: [], status: [], builds: 0 };
        this._with(g, this._scanStubs(out, {
            state: { lastSpellData: { spells: [{ formId: '0x01' }] }, fields: {} },
            TreeGrowthClassic: Object.create(tgc),
            ClassicSettings: { setStatusText: function() {} },
            ClassicBuildRequest: { send: function() { sent++; } },
            _panelVisible: false,
            TreeGrowth: { setBuilding: function() {}, _markDirty: function() {} },
            document: { getElementById: function(id) { return id === 'build-progress-modal' ? modal : null; } },
            PreReqMaster: undefined
        }), function() {
            try {
                g.TreeGrowthClassic.loadTreeData = function(data) { loaded.push(data.tag); };
                g._lastScanTime = 0;
                self._startRun(g);
                A.onSpellData();                       // the run's build goes to C++
                g.TreeGrowthClassic.buildTree();       // the player's Build
                self.check(sent === 1 && g.state._classicGrowthBuildPending && typeof g.state._classicGrowthBuildQueued === 'function' &&
                    !A.isRunning() && shown === 1, 'the player\'s Build waits while C++ builds the run\'s request; its modal shows');
                g.onProceduralTreeComplete(JSON.stringify({ success: true, treeData: { tag: 'run', schools: {} } }));
                self.check(loaded.length === 0 && sent === 2 && g.state._classicGrowthBuildPending && !g.state._classicGrowthBuildQueued &&
                    g.BuildProgress.isActive(), 'the run\'s result is dropped and the player\'s request goes out');
                g.onProceduralTreeComplete(JSON.stringify({ busy: true }));
                self.check(loaded.length === 0 && g.state._classicGrowthBuildPending, 'a busy answer leaves the waiting build alone');
                g.onProceduralTreeComplete(JSON.stringify({ success: true, treeData: { tag: 'player', schools: {} } }));
                self.check(loaded.join(',') === 'player' && !g.state._classicGrowthBuildPending,
                    'the player\'s result is the one shown');

                g.state._classicGrowthBuildPending = true;   // a player's build in C++ ...
                self._startRun(g);
                A.onSpellData();                             // ... the run does not queue behind it
                self.check(sent === 2 && !A.isRunning() && out.events[out.events.length - 1] === 'yielded' && out.status.indexOf('status.autoBuildFailed') === -1,
                    'the run does not wait behind the player\'s build');
            } finally {
                g.state._classicGrowthBuildPending = false;
                g.state._classicGrowthBuildQueued = null;
                g.BuildProgress.close();
                A._resetForTest();
            }
        });
    }
};

if (typeof window !== 'undefined') window.AutoBuildFlowTest = AutoBuildFlowTest;
if (typeof global !== 'undefined') global.AutoBuildFlowTest = AutoBuildFlowTest;
