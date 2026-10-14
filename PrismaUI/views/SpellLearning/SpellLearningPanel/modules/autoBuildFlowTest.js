/**
 * AutoBuildFlowTest - node tests for the build at game start against the
 * panel's own code paths (run-tests.js); autoBuildTest.js has the decision and
 * the run's steps with stand-ins.
 *
 *   - a save load during the run: the real onSaveGameLoaded, applyTree and
 *     loadTreeData - reset, then MarkTreeBuilt, SaveSpellTree,
 *     SetTreePrerequisites and GetProgress, in that order;
 *   - the waits, with a fake clock: no config, no scanner presets, a run that
 *     never ends;
 *   - the player wins: their Build (real TreeGrowthClassic.buildTree and
 *     BuildProgress), an Import, or their Apply while the run's Apply declutters;
 *   - a tree without spells: refused with the tree tab's error, `schools` as an
 *     array counts no spells, only a build's save is marked built.
 *
 * Depends on: autoBuildTest.js (loads the modules), cppCallbacks.js and
 * treeLoad.js (loaded here). Swaps the globals it stubs and puts them back.
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
        g.AutoBuildSetting.loadFrom({});
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
            if (typeof g.loadTreeData !== 'function') {
                var vm = require('vm'), fs = require('fs'), path = require('path');
                var file = path.join(__dirname, 'treeLoad.js');
                vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
            }
        } catch (e) {
            this.check(false, 'cppCallbacks.js and treeLoad.js load (' + e.message + ')');
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
            startScan: function() {},
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
            console: this._quiet
        }, function() {
            try {
                var builtWith = null;
                self._with(g, { TreeGrowthClassic: Object.create(tgc) }, function() {
                    g.TreeGrowthClassic.buildTree = function(opts) { builtWith = opts; return true; };
                    self._startRun(g);
                    g.onSaveGameLoaded();             // Continue pressed during the scan
                    A.onSpellData();
                    g.TreeGrowthClassic._treeData = { schools: {} };
                    builtWith.onComplete('ok');       // the real applyTree, declutter pending
                    g.onSaveGameLoaded();             // and again while it declutters
                    pendingDeclutter();
                });
                // SetTreePrerequisites twice: { clear } and then the list
                var want = ['reset', 'reset', 'MarkTreeBuilt', 'SaveSpellTree', 'SetTreePrerequisites', 'SetTreePrerequisites', 'GetProgress'];
                var order = log.filter(function(n) { return want.indexOf(n) !== -1; });
                self.check(order.join(',') === want.join(',') && !A.isRunning(),
                    'save loads during the run: reset, then the tree saved, made live and its progress asked for');
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
            startScan: function() { scans++; },
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
            self.check(scans === 1 && events[0].event === 'config' && /using the default/.test(events[0].detail) &&
                events[1].event === 'decided' && /^build/.test(events[1].detail), 'config not loaded in time: decided with the default');

            fire(A.RUN_TIMEOUT_MS);
            var last = events[events.length - 1];
            self.check(!A.isRunning() && last.event === 'failed' && /not done after 600 s/.test(last.detail) &&
                status.indexOf('status.autoBuildFailed') !== -1, 'a run not done in time fails and says so');

            A._resetForTest(); events = []; scans = 0;
            g.AutoBuildSetting.loadFrom({});
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
        function reset() { events = []; status = []; cpp = []; shown = 0; sends = 0; pending = []; }
        reset();
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] }, treeData: null },
            _panelVisible: false,
            startScan: function() {},
            callCpp: function(name, arg) {
                if (name === 'AutoBuildEvent') events.push(JSON.parse(arg).event);
                else cpp.push(name);
                if (name === 'SaveSpellTree') sends++;
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
                tgc._treeData = { schools: {} };
                self._startRun(g);
                A.onSpellData();
                A._onBuilt('ok');                                   // the run's Apply, declutter pending
                var importSent = g.SpellTreeSave.send(self._output(), 'import');
                pending[0]();                                       // the run's declutter ends after it
                self.check(importSent && sends === 1 && cpp.indexOf('MarkTreeBuilt') === -1 && !A.isRunning() &&
                    events.indexOf('yielded') !== -1, 'an Import during the run: the run\'s Apply saves nothing over it');

                // The player's Apply while the run's Apply declutters
                reset();
                self._startRun(g);
                A.onSpellData();
                A._onBuilt('ok');
                tgc.applyTree();                                    // a second declutter (the first one is dropped)
                self.check(!A.isRunning() && events.indexOf('yielded') !== -1 && events.indexOf('failed') === -1,
                    'the player\'s Apply during the run\'s declutter: the run ends, no failure');
                pending[1]();
                pending[0]();
                self.check(sends === 1 && cpp.join(',').indexOf('MarkTreeBuilt,SaveSpellTree') !== -1,
                    'only the player\'s Apply saves, marked as a build');
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
            self.check(cpp.join(',') === 'SaveSpellTree,MarkTreeBuilt,SaveSpellTree',
                'only a build\'s save is marked built (the record of its load order)');
        });
    }
};

if (typeof window !== 'undefined') window.AutoBuildFlowTest = AutoBuildFlowTest;
if (typeof global !== 'undefined') global.AutoBuildFlowTest = AutoBuildFlowTest;
