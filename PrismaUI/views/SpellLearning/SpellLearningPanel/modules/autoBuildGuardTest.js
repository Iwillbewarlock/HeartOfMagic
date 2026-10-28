/**
 * AutoBuildGuardTest - node tests for the guards around the build at game start
 * (run-tests.js), against the panel's own code where it can run under node:
 *
 *   - the run's scan is not debounced, and a scan that does not go out fails the run;
 *   - a build of the player's under way when the scan comes back: the run yields;
 *   - a config that threw part way: nothing is built;
 *   - opening the panel during the run's build shows its progress modal;
 *   - Apply and Clear are refused while a build's progress is active;
 *   - Pre Req Master's lock scores for a tree that was cleared or built again
 *     are dropped (prereqMaster.js in a sandbox of its own);
 *   - the dev harness saves a SaveBuiltSpellTree and a SaveImportedSpellTree like a SaveSpellTree;
 *   - the "still building" line has an English fallback (11 languages lack the key);
 *     a failed C++ lock scoring ends the build's progress; a run that ends while
 *     building closes its silent progress; the panel already open shows it at once;
 *   - a tree loaded with the panel closed is drawn when it opens, not before;
 *   - a scan that throws fails the run at once; a run ending in its build step
 *     drops the C++ result still to come (a player's Build queued since still goes
 *     out) and never leaves the progress up (failed when shown, closed on a yield);
 *   - a config.json C++ could not read (configNotRead): nothing is built;
 *   - every language file has the auto build's keys, in lang/*.json and the preloads.
 *
 * Depends on: autoBuildTest.js / autoBuildFlowTest.js (load the modules,
 * buttonHandlers.js among them), canvasRendererV2.js (run-tests.js).
 */

var AutoBuildGuardTest = {

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

    /** Stand-ins for a run, with what reached C++ and the status line in `out`. */
    _stubs: function(out, extra) {
        var stubs = {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] }, fields: {} },
            callCpp: function(name, arg) {
                if (name === 'AutoBuildEvent') out.events.push(JSON.parse(arg).event);
                else out.cpp.push(name);
            },
            t: function(key) { return key; },
            tOr: function(key, params, fallback) { return fallback; },
            updateStatus: function() {},
            setStatusIcon: function() {},
            updateScanStatus: function(text, type, key) { out.status.push(key); },
            getTreeRulesPrompt: function() { return ''; },
            HomIcons: { setLabel: function() {} },
            document: { getElementById: function(id) { return id === 'build-progress-modal' ? out.modal : null; } },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            ClassicSettings: { setStatusText: function(text, tone, key) { out.status.push(key); }, setTreeBuilt: function() {} },
            ClassicBuildRequest: { send: function() { out.cpp.push('ProceduralTreeGenerate'); } },
            TreeGrowth: { setBuilding: function() {}, _markDirty: function() {} },
            PreReqMaster: undefined,
            console: this._quiet
        };
        for (var k in extra) if (extra.hasOwnProperty(k)) stubs[k] = extra[k];
        return stubs;
    },

    _out: function() {
        var out = { events: [], cpp: [], status: [], shown: 0 };
        out.modal = { classList: { remove: function() { out.shown++; }, add: function() {} } };
        return out;
    },

    _startRun: function(g) {
        var A = g.AutoBuild;
        A._resetForTest();
        g.AutoBuildSetting.loadFrom({});
        A.noteConfigLoaded();
        A.notePresetsApplied();
        g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };   // node only
        if (typeof g.AutoBuild === 'undefined' || typeof g.startScan !== 'function' || typeof g.CanvasRenderer === 'undefined') {
            this.check(false, 'AutoBuild, startScan and CanvasRenderer loaded (autoBuildFlowTest.js, run-tests.js)');
            return { passed: this.passed, failed: this.failed };
        }
        this._testScanGoesOut(g);
        this._testPlayerBuildUnderWay(g);
        this._testConfigPartial(g);
        this._testRevealOnShow(g);
        this._testRefuseWhileBuilding(g);
        this._testStalePrmResult(g);
        this._testHarness(g);
        this._testHiddenShow(g);
        this._testRound5(g);
        this._testRunEnds(g);
        this._testConfigNotRead(g);
        this._testLangKeys();
        return { passed: this.passed, failed: this.failed };
    },

    _testScanGoesOut: function(g) {
        var self = this, A = g.AutoBuild, out = this._out();
        this._with(g, this._stubs(out, {}), function() {
            g._lastScanTime = Date.now();   // a scan went out a moment ago
            self._startRun(g);
            self.check(out.cpp.indexOf('ScanSpells') !== -1 && A.isRunning(),
                'the run\'s scan goes out even right after another scan (no debounce)');
            A._resetForTest();
        });
        out = this._out();
        this._with(g, this._stubs(out, { startScan: function() { return false; } }), function() {
            self._startRun(g);
            self.check(!A.isRunning() && out.events[out.events.length - 1] === 'failed' &&
                out.status.indexOf('status.autoBuildFailed') !== -1, 'a scan that does not go out fails the run at once');
            A._resetForTest();
        });
    },

    _testPlayerBuildUnderWay: function(g) {
        var self = this, A = g.AutoBuild, tgc = g.TreeGrowthClassic, oldTree = tgc._treeData;
        var cases = [['the player\'s progress modal', function() { g.BuildProgress.start(false); }],
            ['a built tree not yet applied', function() { tgc._treeData = { schools: {} }; }]];
        for (var i = 0; i < cases.length; i++) {
            var out = this._out();
            this._with(g, this._stubs(out, {}), function() {
                try {
                    g._lastScanTime = 0;
                    self._startRun(g);
                    cases[i][1]();
                    A.onSpellData();
                    self.check(!A.isRunning() && out.events.indexOf('yielded') !== -1 && out.events.indexOf('failed') === -1 &&
                        out.cpp.indexOf('ProceduralTreeGenerate') === -1, cases[i][0] + ' when the scan comes back: the run yields quietly');
                } finally {
                    g.BuildProgress.close();
                    tgc._treeData = oldTree;
                    A._resetForTest();
                }
            });
        }
    },

    _testConfigPartial: function(g) {
        var self = this, A = g.AutoBuild, out = this._out();
        this._with(g, this._stubs(out, {}), function() {
            A._resetForTest();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            g.AutoBuildSetting.loadFrom({ autoBuild: 'whenMissing' });
            A.noteConfigPartial();   // onUnifiedConfigLoaded threw after reading the value
            self.check(!A.isRunning() && out.cpp.indexOf('ScanSpells') === -1 && out.events.join(',') === 'config,decided',
                'a config applied only part way: nothing is built');
            A._resetForTest();
        });
        var src = require('fs').readFileSync(require('path').join(__dirname, 'settingsPanel.js'), 'utf8');
        var fin = src.indexOf('} finally {', src.indexOf('window.onUnifiedConfigLoaded = function'));
        var body = src.slice(fin, src.indexOf('};', fin));
        self.check(/if \(applied\) AutoBuild\.noteConfigLoaded\(\);/.test(body) && /else AutoBuild\.noteConfigPartial\(\);/.test(body),
            'onUnifiedConfigLoaded: whole config -> noteConfigLoaded, part of it -> noteConfigPartial');
    },

    _testRevealOnShow: function(g) {
        var self = this, A = g.AutoBuild, out = this._out(), built = 0;
        this._with(g, this._stubs(out, {
            _panelVisible: false, _panelShownOnce: false,
            CanvasRenderer: undefined, OpenRefreshGate: undefined, TrustedRenderer: undefined, TreeStyle: undefined,
            NewSpells: undefined, SmartRenderer: undefined, WheelRenderer: undefined, TreePreview: undefined
        }), function() {
            try {
                g._lastScanTime = 0;
                self._startRun(g);
                A.onSpellData();   // the real buildTree({ auto }): silent progress
                var silentShown = out.shown;
                g.onPanelShowing();
                self.check(silentShown === 0 && out.shown === 1 && out.events.indexOf('shown') !== -1 && g.BuildProgress.isActive(),
                    'opening the panel during the run\'s build shows its progress modal');
                var oldApply = g.TreeGrowthClassic.applyTree;
                g.TreeGrowthClassic.applyTree = function() { built++; return true; };
                try { g.BuildProgress.complete('done'); } finally { g.TreeGrowthClassic.applyTree = oldApply; }
                self.check(built === 1, 'the shown build still hands its result to the run');
            } finally {
                g.BuildProgress.close();
                g.state._classicGrowthBuildPending = false;
                A._resetForTest();
            }
        });
    },

    _testRefuseWhileBuilding: function(g) {
        var self = this, tgc = g.TreeGrowthClassic, oldTree = tgc._treeData, out = this._out();
        this._with(g, this._stubs(out, {}), function() {
            try {
                var tree = { schools: { A: { nodes: [{ formId: '0x01' }] } } };
                tgc._treeData = tree;
                g.BuildProgress.start(true, { silent: true });   // a build still applying its locks
                var applied = tgc.applyTree(), cleared = tgc.clearTree();
                self.check(applied === false && cleared === false && tgc._treeData === tree && out.cpp.length === 0 &&
                    out.status.filter(function(k) { return k === 'buildProgress.statusStillBuilding'; }).length === 2,
                    'Apply and Clear while a build\'s progress is active: refused, and the status line says why');
            } finally {
                g.BuildProgress.close();
                tgc._treeData = oldTree;
            }
        });
    },

    /** prereqMaster.js in a sandbox: autoApplyLocks sends the scores request for this tree. */
    _testStalePrmResult: function(g) {
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var calls = [], progress = [];
        var element = function() {
            var noop = function() {};
            return { checked: true, value: '', textContent: '', style: {}, addEventListener: noop, appendChild: noop,
                setAttribute: noop, querySelectorAll: function() { return []; }, querySelector: function() { return null; },
                getContext: function() { return null; },
                classList: { add: noop, remove: noop, toggle: noop, contains: function() { return false; } } };
        };
        var nodes = [];
        for (var i = 0; i < 20; i++) {
            nodes.push({ formId: '0x' + (i + 1), name: 'S' + i, tier: Math.floor(i / 4), desc: 'fire ' + i,
                effects: ['Fire Damage'], x: i * 10, y: i * 5, prerequisites: i ? ['0x' + i] : [],
                children: i < 19 ? ['0x' + (i + 2)] : [] });
        }
        var tree = { schools: { Destruction: { root: '0x1', nodes: nodes } } };
        var sandbox = {
            console: { log: function() {}, warn: function(m) { calls.push('warn:' + m); }, error: function() {} },
            setTimeout: function(fn) { fn(); return 0; }, clearTimeout: function() {},
            document: { getElementById: element, querySelectorAll: function() { return []; }, createElement: element,
                addEventListener: function() {} },
            settings: {}, state: {}, Map: Map, Set: Set, Math: Math, JSON: JSON,
            t: function(k) { return k; }, tOr: function(k, p, f) { return f; }, setI18n: function() {},
            TreeGrowth: { modes: { classic: { _treeData: tree } }, activeMode: 'classic' },
            TreeGrowthClassic: { _treeData: tree },
            BuildProgress: { isActive: function() { return true; }, setStage: function(s) { progress.push(s); },
                setDetail: function() {}, complete: function() { progress.push('complete'); } },
            callCpp: function(name) { calls.push(name); }
        };
        sandbox.window = sandbox;
        vm.createContext(sandbox);
        vm.runInContext(fs.readFileSync(path.join(__dirname, 'prereqMaster.js'), 'utf8'), sandbox, { filename: 'prereqMaster.js' });
        sandbox.PreReqMaster.autoApplyLocks();
        var sent = calls.indexOf('PreReqMasterScore') !== -1;
        this._prmSandbox = sandbox;
        this._prmProgress = progress;
        this._prmTree = tree;
        sandbox.TreeGrowthClassic._treeData = { schools: {} };   // cleared or built again meanwhile
        sandbox.onPreReqMasterComplete(JSON.stringify({ success: true, scores: [] }));
        this.check(sent && progress.length === 0 && calls.some(function(c) { return /no longer|cleared or built again/.test(c); }),
            'lock scores for a tree that was cleared or built again: dropped, no locks, no progress step');

        calls.length = 0;
        sandbox.TreeGrowthClassic._treeData = tree;
        sandbox.TreeGrowth.modes.classic._treeData = tree;
        sandbox.PreReqMaster.autoApplyLocks();
        sandbox.onPreReqMasterComplete(JSON.stringify({ success: true, scores: [] }));
        this.check(progress.length > 0 && !calls.some(function(c) { return /cleared or built again/.test(c); }),
            'lock scores for the tree still there are taken as before');

        // Request ids: tree1 asked (#a), tree2 asked while #a is scored (busy), #a's answer
        // is dropped and #b goes again, #b's answer is taken for tree2
        var sent2 = [];
        sandbox.callCpp = function(name, text) { calls.push(name); if (name === 'PreReqMasterScore') sent2.push(JSON.parse(text)); };
        calls.length = 0; progress.length = 0;
        sandbox.PreReqMaster.autoApplyLocks();
        var idA = sent2.length ? sent2[sent2.length - 1].requestId : null;
        var tree2 = JSON.parse(JSON.stringify(tree));
        sandbox.TreeGrowthClassic._treeData = tree2;
        sandbox.TreeGrowth.modes.classic._treeData = tree2;
        sandbox.PreReqMaster.autoApplyLocks();
        var idB = sent2.length ? sent2[sent2.length - 1].requestId : null;
        sandbox.onPreReqMasterComplete(JSON.stringify({ success: false, busy: true, error: 'busy' }));
        var before = sent2.length;
        sandbox.onPreReqMasterComplete(JSON.stringify({ success: true, scores: [], requestId: idA }));
        this.check(typeof idA === 'number' && typeof idB === 'number' && idA !== idB && progress.length === 0 &&
            sent2.length === before + 1 && sent2[sent2.length - 1].requestId === idB,
            'PRM: an answer to an earlier request is dropped, and the request the busy scorer turned away goes again');
        sandbox.onPreReqMasterComplete(JSON.stringify({ success: true, scores: [], requestId: idB }));
        this.check(progress.length > 0, 'PRM: the answer to the latest request is taken');
        sandbox.callCpp = function(name) { calls.push(name); };
    },

    _testHarness: function(g) {
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var noop = function() {};
        var sandbox = {
            console: this._quiet, setTimeout: function() { return 0; }, Blob: function() {},
            URL: { createObjectURL: function() { return 'blob:x'; }, revokeObjectURL: noop },
            document: { getElementById: function() { return null; }, createElement: function() { return { click: noop }; },
                body: { appendChild: noop, removeChild: noop } },
            addEventListener: noop
        };
        sandbox.window = sandbox;
        vm.createContext(sandbox);
        vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'dev-harness-bridge.js'), 'utf8'), sandbox,
            { filename: 'dev-harness-bridge.js' });
        sandbox.callCpp('SaveBuiltSpellTree', '{"built":1}');
        var built = sandbox._lastSavedTree;
        sandbox.callCpp('SaveImportedSpellTree', '{"imported":1}');
        var imported = sandbox._lastSavedTree;
        sandbox.callCpp('SaveSpellTree', '{"plain":1}');
        this.check(built === '{"built":1}' && imported === '{"imported":1}' && sandbox._lastSavedTree === '{"plain":1}',
            'the dev harness saves SaveBuiltSpellTree, SaveImportedSpellTree and SaveSpellTree alike');
    },

    /** CanvasRenderer.show (canvasRendererV2.js) on a stand-in renderer. */
    _testHiddenShow: function(g) {
        var self = this;
        function renderer() {
            var r = { canvas: { parentNode: {} }, container: { classList: { add: function() {} } }, nodes: [], forced: 0,
                updateCanvasSize: function() {}, startRenderLoop: function() {}, _needsRender: false };
            r.forceRender = function() { r.forced++; };
            return r;
        }
        this._with(g, { FxLayer: undefined, document: { getElementById: function() { return null; } },
            setTimeout: function() { return 0; }, console: this._quiet, _panelVisible: false }, function() {
            var hidden = renderer();
            g.CanvasRenderer.show.call(hidden);
            g._panelVisible = true;
            var shown = renderer();
            g.CanvasRenderer.show.call(shown);
            self.check(hidden.forced === 0 && hidden._needsRender === true && shown.forced === 1,
                'a tree shown with the panel closed is marked to draw, not drawn; with it open it is drawn at once');
        });
        var src = require('fs').readFileSync(require('path').join(__dirname, 'treeLoad.js'), 'utf8');
        this.check(/if \(window\._panelVisible !== false\) CanvasRenderer\.forceRender\(\);/.test(src),
            'loadTreeData does not force a frame with the panel closed');
    }
};

AutoBuildGuardTest._testRound5 = function(g) {
    var self = this, A = g.AutoBuild, tgc = g.TreeGrowthClassic, oldTree = tgc._treeData, texts = [];
    var out = this._out();
    // A language without the key: tOr gives the English fallback, t would give the key
    this._with(g, this._stubs(out, {
        ClassicSettings: { setStatusText: function(text) { texts.push(text); }, setTreeBuilt: function() {} }
    }), function() {
        try {
            tgc._treeData = { schools: {} };
            g.BuildProgress.start(false, { silent: true });
            tgc.applyTree();
            self.check(texts.length === 1 && texts[0] !== 'buildProgress.statusStillBuilding' && /still being built/.test(texts[0]),
                'the still-building line falls back to English where the language has no such key');
        } finally {
            g.BuildProgress.close();
            tgc._treeData = oldTree;
        }
    });

    var sb = this._prmSandbox, progress = this._prmProgress;
    if (sb) {
        progress.length = 0;
        sb.TreeGrowthClassic._treeData = this._prmTree;
        sb.PreReqMaster.autoApplyLocks();
        sb.onPreReqMasterComplete(JSON.stringify({ success: false, error: 'scorer broke' }));
        this.check(progress.indexOf('finalize') !== -1 && progress.indexOf('complete') !== -1,
            'a failed C++ lock scoring still ends the build\'s progress (Apply and Clear are not held back)');
    }

    out = this._out();
    this._with(g, this._stubs(out, { _panelVisible: false }), function() {
        try {
            g._lastScanTime = 0;
            self._startRun(g);
            A.onSpellData();   // silent build under way
            var wasSilent = g.BuildProgress.isSilent();
            A._fail('not done after 600 s');
            self.check(wasSilent && !g.BuildProgress.isActive(), 'a run that ends while building closes its silent progress');
        } finally {
            g.BuildProgress.close();
            g.state._classicGrowthBuildPending = false;
            A._resetForTest();
        }
    });

    out = this._out();
    this._with(g, this._stubs(out, { _panelVisible: true }), function() {
        try {
            g._lastScanTime = 0;
            self._startRun(g);
            A.onSpellData();   // the panel is open as the build starts
            self.check(out.shown === 1 && out.events.indexOf('shown') !== -1 && !g.BuildProgress.isSilent() && g.BuildProgress.isActive(),
                'the panel already open when the run builds: its progress shows at once');
        } finally {
            g.BuildProgress.close();
            g.state._classicGrowthBuildPending = false;
            A._resetForTest();
        }
    });
};

/** How a run ends: a scan that throws, and a build step cut short (timeout, yield) with C++ still building. */
AutoBuildGuardTest._testRunEnds = function(g) {
    var self = this, A = g.AutoBuild, out = this._out();
    this._with(g, this._stubs(out, { startScan: function() { throw new Error('no scan bridge'); } }), function() {
        try {
            self._startRun(g);
            self.check(!A.isRunning() && out.events[out.events.length - 1] === 'failed' && out.status.indexOf('status.autoBuildFailed') !== -1,
                'startScan throws: the run fails at once (not left in the scan step until the timeout)');
        } finally { A._resetForTest(); }
    });

    // [panel open, how it ends] -> what is left
    var cases = [[false, 'fail'], [true, 'fail'], [true, 'yield']];
    for (var i = 0; i < cases.length; i++) {
        var c = cases[i], loaded = [], sent = 0, doneShown = 0;
        out = this._out();
        var doneBtn = { textContent: '', parentNode: null, classList: { remove: function() { doneShown++; }, add: function() {} } };
        this._with(g, this._stubs(out, {
            _panelVisible: c[0],
            TreeGrowthClassic: Object.create(g.TreeGrowthClassic),
            ClassicBuildRequest: { send: function() { sent++; } },
            document: { getElementById: function(id) {
                return id === 'build-progress-modal' ? out.modal : (id === 'build-progress-done-btn' ? doneBtn : null);
            } }
        }), function() {
            var label = (c[0] ? 'shown' : 'silent') + ' build, run ' + (c[1] === 'fail' ? 'timed out' : 'yielded to a Scan');
            try {
                g.TreeGrowthClassic.loadTreeData = function(data) { loaded.push(data.tag); };
                g._lastScanTime = 0;
                self._startRun(g);
                A.onSpellData();   // the run's build request goes to C++
                var revealed = c[0] && g.BuildProgress.isActive() && !g.BuildProgress.isSilent();
                if (c[1] === 'fail') A._fail('not done after 600 s');
                else A.yieldTo('scan');
                var failedOnce = out.events.filter(function(e) { return e === 'failed'; }).length === (c[1] === 'fail' ? 1 : 0);
                if (c[0] && c[1] === 'fail') {
                    self.check(revealed && failedOnce && doneShown === 1, label + ': the progress says it failed and can be closed');
                } else {
                    self.check((!c[0] || revealed) && failedOnce && !g.BuildProgress.isActive(), label + ': the progress is closed');
                }
                if (c[1] === 'fail') {
                    // updateScanStatus and the builder line (ClassicSettings) both record their key in out.status
                    self.check(out.status.filter(function(k) { return k === 'status.autoBuildFailed'; }).length === 2,
                        label + ': the builder line says it failed too (not "Building tree...")');
                }
                self.check(g.state._classicGrowthBuildPending && typeof g.state._classicGrowthBuildQueued === 'function',
                    label + ': the build still in C++ is marked to be dropped');
                g.onProceduralTreeComplete(JSON.stringify({ success: true, treeData: { tag: 'late', schools: {} } }));
                self.check(loaded.length === 0 && !g.state._classicGrowthBuildPending && !g.state._classicGrowthBuildQueued &&
                    out.status.indexOf('status.treeBuildComplete') === -1 && sent === 1,
                    label + ': its late result is dropped - no tree, no "Tree built", nothing waiting');
            } finally {
                g.BuildProgress.close();
                g.state._classicGrowthBuildPending = false;
                g.state._classicGrowthBuildQueued = null;
                A._resetForTest();
            }
        });
    }

    // Timed out with C++ still building, then the player's Build: theirs goes out when C++ answers
    out = this._out();
    var loadedP = [], sentP = 0;
    this._with(g, this._stubs(out, {
        _panelVisible: false,
        TreeGrowthClassic: Object.create(g.TreeGrowthClassic),
        ClassicBuildRequest: { send: function() { sentP++; } }
    }), function() {
        try {
            g.TreeGrowthClassic.loadTreeData = function(data) { loadedP.push(data.tag); };
            g._lastScanTime = 0;
            self._startRun(g);
            A.onSpellData();
            A._fail('not done after 600 s');
            g.TreeGrowthClassic.buildTree();   // the player's Build, while C++ still has the run's
            g.onProceduralTreeComplete(JSON.stringify({ success: true, treeData: { tag: 'late', schools: {} } }));
            self.check(loadedP.length === 0 && sentP === 2 && g.state._classicGrowthBuildPending,
                'after the run ended: a Build of the player\'s queued meanwhile goes out when the late result comes (not dropped)');
            g.onProceduralTreeComplete(JSON.stringify({ success: true, treeData: { tag: 'player', schools: {} } }));
            self.check(loadedP.join(',') === 'player', '...and its own result is the tree shown');
        } finally {
            g.BuildProgress.close();
            g.state._classicGrowthBuildPending = false;
            g.state._classicGrowthBuildQueued = null;
            A._resetForTest();
        }
    });
};

/** C++ sent its defaults for a config.json it could not read (UIManagerConfig.cpp, configNotRead). */
AutoBuildGuardTest._testConfigNotRead = function(g) {
    var self = this, A = g.AutoBuild, out = this._out();
    this._with(g, this._stubs(out, {}), function() {
        try {
            A._resetForTest();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            g.AutoBuildSetting.loadFrom({ autoBuild: 'whenMissing', configNotRead: true });
            A.noteConfigLoaded();
            self.check(!A.isRunning() && out.cpp.indexOf('ScanSpells') === -1 && out.events.join(',') === 'config,decided',
                'config.json not read (defaults marked configNotRead): nothing is built, the log says why');
            g.AutoBuildSetting.loadFrom({ autoBuild: 'whenMissing' });
            self.check(g.AutoBuildSetting.notRead === false, 'a config that was read clears the mark');
        } finally { A._resetForTest(); }
    });
};

/** The auto build's keys in every language (11 lacked them; a switch showed the bare key). */
AutoBuildGuardTest._testLangKeys = function() {
    var fs = require('fs'), path = require('path');
    var dir = path.join(__dirname, '..', 'lang');
    var keys = ['buildProgress.statusNothingPlaced', 'buildProgress.statusStillBuilding', 'hud.loadOrderChanged',
        'settings.autoBuild.label', 'settings.autoBuild.desc', 'settings.autoBuild.whenMissing', 'settings.autoBuild.off',
        'status.loadOrderChanged', 'status.autoBuildFailed', 'status.treeNoSpellsNotSaved',
        'status.autoBuildHeld', 'hud.autoBuildHeld'];
    var files = fs.readdirSync(dir).filter(function(f) { return /\.json$/.test(f); });
    var bad = [];
    for (var i = 0; i < files.length; i++) {
        var code = files[i].replace(/\.json$/, '');
        var json = JSON.parse(fs.readFileSync(path.join(dir, files[i]), 'utf8'));
        var preload = fs.existsSync(path.join(dir, code + '.js')) ? fs.readFileSync(path.join(dir, code + '.js'), 'utf8') : '';
        for (var k = 0; k < keys.length; k++) {
            var v = json[keys[k]];
            if (typeof v !== 'string' || !v) bad.push(code + ' ' + keys[k]);
            else if (preload.indexOf(JSON.stringify(keys[k])) === -1) bad.push(code + '.js ' + keys[k]);
        }
        if (typeof json['status.autoBuildFailed'] === 'string' && json['status.autoBuildFailed'].indexOf('{{error}}') === -1) {
            bad.push(code + ' status.autoBuildFailed without {{error}}');
        }
    }
    this.check(files.length >= 13 && bad.length === 0,
        'the auto build\'s ' + keys.length + ' keys in all ' + files.length + ' languages and their preloads' + (bad.length ? ' - missing: ' + bad.join(', ') : ''));
};

if (typeof window !== 'undefined') window.AutoBuildGuardTest = AutoBuildGuardTest;
if (typeof global !== 'undefined') global.AutoBuildGuardTest = AutoBuildGuardTest;
