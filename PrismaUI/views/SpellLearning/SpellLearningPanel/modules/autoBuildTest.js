/**
 * AutoBuildTest - node tests for the tree built at game start (run-tests.js).
 *
 *   - SpellTreeSave: a tree without spells is never sent to SaveSpellTree, and
 *     Apply (TreeGrowthClassic.applyTree) with nothing placed sends nothing;
 *   - AutoBuild.decide: no tree -> build, matching load order -> nothing,
 *     changed load order -> notice once, setting off -> nothing;
 *   - the game start: nothing before the tree info, the config and the scanner
 *     presets are all in; then a scan, or the notice, or nothing; the run goes
 *     scan -> build (silent) -> apply (auto) -> done, through save loads, and a
 *     later startup info starts no second run;
 *   - TreePreviewHidden: the preview's output without its render loop, for
 *     every Sun grid and the Flat mode, equal to a drawn frame's.
 *
 * Depends on: spellTreeSave.js, autoBuildSetting.js, autoBuild.js, colorUtils.js,
 * treePreviewHidden.js, the preview modes (treePreviewSun.js, sunGrid*.js,
 * treePreviewFlat.js, treePreview.js) and classic/classicTreeOutput.js +
 * classic/classicMain.js - run into the global scope the way the page's script
 * tags do. Swaps the globals it stubs and puts them back.
 */

var AutoBuildTest = {

    passed: 0,
    failed: 0,

    // The real console's log: checks inside _with print while the code under test is quiet
    _print: (typeof console !== 'undefined') ? console.log.bind(console) : function() {},

    check: function(ok, name) {
        if (ok) { this.passed++; this._print('  [PASS] ' + name); }
        else { this.failed++; this._print('  [FAIL] ' + name); }
    },

    _FILES: [
        ['spellTreeSave.js', 'SpellTreeSave'],
        ['buildProgress.js', 'BuildProgress'],
        ['autoBuildSetting.js', 'AutoBuildSetting'],
        ['autoBuild.js', 'AutoBuild'],
        ['colorUtils.js', 'hexToRgba'],
        ['treePreviewUtils.js', 'TreePreviewUtils'],
        ['sunGridLinear.js', 'SunGridLinear'],
        ['sunGridEqualArea.js', 'SunGridEqualArea'],
        ['sunGridFibonacci.js', 'SunGridFibonacci'],
        ['sunGridSquare.js', 'SunGridSquare'],
        ['treePreviewSun.js', 'TreePreviewSun'],
        ['treePreviewFlat.js', 'TreePreviewFlat'],
        ['treePreview.js', 'TreePreview'],
        ['treePreviewHidden.js', 'TreePreviewHidden'],
        ['classic/classicTreeOutput.js', 'ClassicTreeOutput'],
        ['classic/classicMain.js', 'TreeGrowthClassic']
    ],

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        for (var i = 0; i < this._FILES.length; i++) {
            if (typeof g[this._FILES[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, this._FILES[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    /** Swap globals for the length of fn, then put them back. */
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

    _tree: function(count) {
        var nodes = [];
        for (var i = 0; i < count; i++) nodes.push({ formId: '0x0' + i });
        return { schools: { Destruction: { nodes: nodes }, Restoration: { nodes: [] } } };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        for (var i = 0; i < this._FILES.length; i++) {
            if (typeof g[this._FILES[i][1]] === 'undefined') {
                this.check(false, this._FILES[i][0] + ' loaded');
                return { passed: this.passed, failed: this.failed };
            }
        }
        this._testSaveGuard(g);
        this._testApplyGuard(g);
        this._testSilentProgress(g);
        this._testDecide(g);
        this._testStartup(g);
        this._testRun(g);
        this._testSaveLoadGoesOn(g);
        this._testApplyStale(g);
        this._testHiddenPreview(g);
        this._testHiddenMatchesVisible(g);
        return { passed: this.passed, failed: this.failed };
    },

    _testSaveGuard: function(g) {
        var self = this, calls = [];
        this._with(g, {
            callCpp: function(name, arg) { calls.push([name, arg]); },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            self.check(g.SpellTreeSave.countSpellNodes(self._tree(3)) === 3 &&
                g.SpellTreeSave.countSpellNodes({ schools: { A: { nodes: 'x' } } }) === 0 &&
                g.SpellTreeSave.countSpellNodes(null) === 0, 'spells counted over all schools; nonsense counts 0');
            var sentEmpty = g.SpellTreeSave.send(self._tree(0), 'test');
            self.check(sentEmpty === false && calls.length === 0, 'a tree with no spells is refused: SaveSpellTree not called');
            var sent = g.SpellTreeSave.send(self._tree(2), 'test');
            self.check(sent === true && calls.length === 1 && calls[0][0] === 'SaveSpellTree' &&
                JSON.parse(calls[0][1]).schools.Destruction.nodes.length === 2, 'a tree with spells goes to SaveSpellTree');
        });
    },

    _testApplyGuard: function(g) {
        var self = this, calls = [], status = [], declutters = 0;
        var tgc = g.TreeGrowthClassic;
        var oldTree = tgc._treeData, oldLayout = tgc._layoutData;
        this._with(g, {
            callCpp: function(name) { calls.push(name); },
            t: function(key) { return key; },
            tOr: function(key, params, fallback) { return fallback; },
            ClassicSettings: { setStatusText: function(text, tone, key) { status.push(key); } },
            LayoutDeclutter: { applyAsync: function(out, done) { declutters++; done(); } },
            TreePreview: undefined,
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            try {
                // The build at game start before this fix: a C++ tree, but no layout (no preview grid)
                tgc._treeData = { schools: { Destruction: { root: '0x01', nodes: [{ formId: '0x01' }, { formId: '0x02' }] } } };
                tgc._layoutData = null;
                var sent = tgc.applyTree({ auto: true, onSaved: function() {} });
                self.check(sent === false && calls.indexOf('SaveSpellTree') === -1 && declutters === 0 &&
                    status.indexOf('buildProgress.statusNothingPlaced') !== -1,
                    'Apply with no spell placed saves nothing and says so (manual and automatic alike)');
            } finally {
                tgc._treeData = oldTree;
                tgc._layoutData = oldLayout;
            }
        });
    },

    _testSilentProgress: function(g) {
        var self = this, shown = 0, done = [], failed = [];
        var modal = { classList: { remove: function() { shown++; }, add: function() {} } };
        this._with(g, {
            document: { getElementById: function(id) { return id === 'build-progress-modal' ? modal : null; } },
            t: function(key) { return key; },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            var BP = g.BuildProgress;
            BP.start(true, { silent: true, onComplete: function(s) { done.push(s); }, onFail: function(e) { failed.push(e); } });
            BP.setStage('prereqs');
            BP.complete('3 locks applied');
            BP.complete('again');
            self.check(shown === 0 && done.join('|') === '3 locks applied' && !BP.isActive(),
                'silent build progress: no modal, onComplete once, inactive after');
            BP.start(false, { silent: true, onComplete: function(s) { done.push(s); }, onFail: function(e) { failed.push(e); } });
            BP.fail('no reply', function() {});
            self.check(shown === 0 && failed.join('|') === 'no reply' && !BP.isActive() && done.length === 1,
                'silent build failure: onFail once, inactive after');
            BP.start(false);
            self.check(shown === 1 && BP.isActive(), 'a build from the buttons still shows the modal');
            BP.close();
        });
    },

    _testDecide: function(g) {
        var A = g.AutoBuild;
        var missing = { tree: 'missing', nodes: 0, loadOrder: 'aa', builtLoadOrder: '', noticedLoadOrder: '' };
        var same = { tree: 'present', nodes: 9, loadOrder: 'aa', builtLoadOrder: 'aa', noticedLoadOrder: '' };
        var changed = { tree: 'present', nodes: 9, loadOrder: 'bb', builtLoadOrder: 'aa', noticedLoadOrder: '' };
        var told = { tree: 'present', nodes: 9, loadOrder: 'bb', builtLoadOrder: 'aa', noticedLoadOrder: 'bb' };
        var oldTree = { tree: 'present', nodes: 9, loadOrder: 'bb', builtLoadOrder: '', noticedLoadOrder: '' };
        this.check(A.decide('whenMissing', missing, false).action === A.BUILD, 'no saved tree: build');
        this.check(A.decide('whenMissing', { tree: 'empty', loadOrder: 'aa' }, false).action === A.BUILD,
            'a saved tree without spells: build');
        this.check(A.decide('whenMissing', same, false).action === A.NONE, 'tree there, same load order: nothing');
        this.check(A.decide('whenMissing', changed, false).action === A.NOTICE, 'tree there, load order changed: notice only');
        this.check(A.decide('whenMissing', told, false).action === A.NONE, 'that change told once already: nothing');
        this.check(A.decide('whenMissing', oldTree, false).action === A.NONE, 'a tree from before the record (unknown): nothing');
        this.check(A.decide('off', missing, false).action === A.NONE && A.decide('off', changed, false).action === A.NONE,
            'setting off: nothing, not even the notice');
        this.check(A.decide('nonsense', missing, false).action === A.BUILD, 'an unknown setting value is the default');
        this.check(A.decide('whenMissing', { tree: 'unreadable' }, false).action === A.NONE,
            'a damaged tree file is left alone');
        this.check(A.decide('whenMissing', missing, true).action === A.BUILD, 'no saved tree after a save has loaded: build anyway');
        this.check(A.decide('whenMissing', { tree: 'present', nodes: 9, loadOrder: 'bb' }, false).action === A.NONE,
            'tree there, no load order record at all (no spell_tree_loadorder.json): no notice');
    },

    /** One game start with stubs; returns what reached the panel and C++. */
    _start: function(g, setting, info, order) {
        var out = { scans: 0, events: [], status: [] };
        var A = g.AutoBuild;
        this._with(g, {
            settings: { autoBuild: setting },
            startScan: function() { out.scans++; return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') out.events.push(JSON.parse(arg).event); },
            updateScanStatus: function(text, type, key) { out.status.push(key); },
            tOr: function(key, params, fallback) { return fallback; },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            A._resetForTest();
            g.AutoBuildSetting.loaded = false;
            for (var i = 0; i < order.length; i++) {
                if (order[i] === 'info') g.onStartupTreeInfo(JSON.stringify(info));
                else if (order[i] === 'config') { g.AutoBuildSetting.loadFrom({ autoBuild: setting }); A.noteConfigLoaded(); }
                else if (order[i] === 'presets') A.notePresetsApplied();
                if (i < order.length - 1) out['before' + i] = out.scans + out.events.length;
            }
            out.running = A.isRunning();
            A._resetForTest();
        });
        return out;
    },

    _testStartup: function(g) {
        var missing = { tree: 'missing', nodes: 0, loadOrder: 'aa' };
        var changed = { tree: 'present', nodes: 9, loadOrder: 'bb', builtLoadOrder: 'aa', noticedLoadOrder: '' };
        var same = { tree: 'present', nodes: 9, loadOrder: 'aa', builtLoadOrder: 'aa' };

        var r = this._start(g, 'whenMissing', missing, ['info', 'config', 'presets']);
        this.check(r.before0 === 0 && r.before1 === 0 && r.scans === 1 && r.running &&
            r.events.join(',') === 'decided,scan', 'no tree: the scan starts once tree info, config and presets are all in');
        r = this._start(g, 'whenMissing', missing, ['config', 'presets', 'info']);
        this.check(r.scans === 1, 'the same in another order');
        r = this._start(g, 'whenMissing', changed, ['info', 'config', 'presets']);
        this.check(r.scans === 0 && r.events.join(',') === 'decided,notice' && r.status.indexOf('status.loadOrderChanged') !== -1,
            'load order changed: no scan, the notice on the scan line and to C++');
        r = this._start(g, 'whenMissing', same, ['info', 'config', 'presets']);
        this.check(r.scans === 0 && r.events.join(',') === 'decided' && r.status.length === 0, 'same load order: nothing');
        r = this._start(g, 'off', missing, ['info', 'config', 'presets']);
        this.check(r.scans === 0 && r.events.join(',') === 'decided' && r.status.length === 0, 'setting off: nothing');
    },

    _testRun: function(g) {
        var self = this, A = g.AutoBuild, calls = [], events = [];
        var builtWith = null, appliedWith = null;
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: null },
            startScan: function() { calls.push('scan'); return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') events.push(JSON.parse(arg).event); },
            updateScanStatus: function() {},
            tOr: function(key, params, fallback) { return fallback; },
            TreePreviewHidden: { ensure: function() { calls.push('preview'); return { rootNodes: [], gridPoints: [] }; } },
            TreeGrowthClassic: {
                buildTree: function(opts) { calls.push('build'); builtWith = opts; return true; },
                applyTree: function(opts) { calls.push('apply'); appliedWith = opts; return true; }
            },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            A._resetForTest();
            g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            A.onSpellData();   // a scan with no spells yet: the run stops
            self.check(!A.isRunning() && events[events.length - 1] === 'failed', 'a scan without spells ends the run');

            A._resetForTest();
            events.length = 0; calls.length = 0;
            g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
            A.notePresetsApplied();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            g.state.lastSpellData = { spells: [{ formId: '0x01' }] };
            A.onSpellData();
            self.check(calls.join(',') === 'scan,preview,build' && builtWith && builtWith.auto === true,
                'the scan result builds with { auto } after the preview data is made');
            builtWith.onComplete('3 locks applied');
            self.check(calls[calls.length - 1] === 'apply' && appliedWith && appliedWith.auto === true,
                'the finished build is applied with { auto }');
            appliedWith.onSaved(12);
            self.check(!A.isRunning() && events.join(',') === 'decided,scan,build,apply,done',
                'saved: the run is done, every step reported');
            builtWith.onComplete('late');   // a late callback after the end does nothing
            self.check(calls.filter(function(c) { return c === 'apply'; }).length === 1, 'a late build callback is ignored');
            A._resetForTest();
        });
    },

    /**
     * spell_tree.json is one file for every save: a save load at any step does
     * not stop the run, and a later LoadSpellTree's info starts no second one.
     * Only the run's own end (done, failed, timed out) drops what comes back.
     */
    _testSaveLoadGoesOn: function(g) {
        var self = this, A = g.AutoBuild, calls, events, status, builtWith, appliedWith;
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] } },
            startScan: function() { calls.push('scan'); return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') events.push(JSON.parse(arg).event); },
            updateScanStatus: function(text, type, key) { status.push(key); },
            tOr: function(key, params, fallback) { return fallback; },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            TreeGrowthClassic: {
                buildTree: function(opts) { calls.push('build'); builtWith = opts; return true; },
                applyTree: function(opts) { calls.push('apply'); appliedWith = opts; return true; }
            },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            function begin(saveFirst) {
                calls = []; events = []; status = []; builtWith = null; appliedWith = null;
                A._resetForTest();
                if (saveFirst) A.onSaveGameLoaded();
                g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded();
                A.notePresetsApplied();
                g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            }
            begin(false);
            A.onSaveGameLoaded();   // during the scan
            A.onSpellData();
            A.onSaveGameLoaded();   // during the build
            builtWith.onComplete('ok');
            A.onSaveGameLoaded();   // while Apply declutters
            var current = appliedWith.isCurrent();
            appliedWith.onSaved(7);
            self.check(current === true && calls.join(',') === 'scan,build,apply' && !A.isRunning() &&
                events.join(',') === 'decided,scan,save loaded,build,save loaded,apply,save loaded,done' &&
                status.indexOf('status.autoBuildFailed') === -1,
                'save loads during scan, build and Apply: the run goes on and saves');

            begin(true);
            self.check(calls.join(',') === 'scan' && A.isRunning(), 'decided after a save load with no tree: build');

            begin(false);
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });   // LoadSpellTree again (Load Saved Tree, or a view reload)
            A.onSpellData();
            g.onStartupTreeInfo({ tree: 'present', nodes: 5, loadOrder: 'aa', builtLoadOrder: 'aa' });
            self.check(calls.join(',') === 'scan,build' && events.filter(function(e) { return e === 'decided'; }).length === 1,
                'a second startup info during a run: no second decision, no second build');
            builtWith.onComplete('ok');
            appliedWith.onSaved(3);
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            self.check(calls.join(',') === 'scan,build,apply', 'a startup info after the run: nothing new');

            begin(false);
            A.onSpellData();
            A._fail('not done after 600 s');   // what the run timer does
            builtWith.onComplete('late');
            self.check(!A.isRunning() && calls.join(',') === 'scan,build' && status.indexOf('status.autoBuildFailed') !== -1,
                'a timed out run: the late build result is dropped');
            A._resetForTest();
        });
    },

    /** Apply of the build at game start skips the save once the run has ended (timed out). */
    _testApplyStale: function(g) {
        var self = this, calls = [];
        var tgc = g.TreeGrowthClassic;
        var oldTree = tgc._treeData, oldLayout = tgc._layoutData;
        this._with(g, {
            callCpp: function(name) { calls.push(name); },
            t: function(key) { return key; },
            ClassicSettings: { setStatusText: function() {} },
            ClassicTreeOutput: { build: function() { return { output: self._tree(3), posCount: 3 }; } },
            LayoutDeclutter: { applyAsync: function(out, done) { done(); } },
            loadTreeData: function() { calls.push('load'); },
            TreePreview: undefined,
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            try {
                tgc._treeData = { schools: {} };
                var saved = 0;
                tgc.applyTree({ auto: true, isCurrent: function() { return false; }, onSaved: function() { saved++; } });
                self.check(calls.length === 0 && saved === 0, 'a stale automatic Apply neither saves nor loads the tree');
                tgc.applyTree({ auto: true, isCurrent: function() { return true; }, onSaved: function() { saved++; } });
                self.check(calls.join(',') === 'SaveBuiltSpellTree,load' && saved === 1, 'a current automatic Apply saves as a build and loads');
            } finally {
                tgc._treeData = oldTree;
                tgc._layoutData = oldLayout;
            }
        });
    },

    _testHiddenPreview: function(g) {
        var self = this;
        var schoolData = { Destruction: 40, Restoration: 30, Alteration: 25, Conjuration: 35, Illusion: 20 };
        var preview = Object.create(g.TreePreview);
        preview.modes = { sun: g.TreePreviewSun, flat: g.TreePreviewFlat };
        preview.schoolData = schoolData;
        preview._width = 0;
        preview._height = 0;
        var grids = ['naive', 'linear', 'equalArea', 'fibonacci', 'square'];
        var oldGridType = g.TreePreviewSun.settings.gridType;
        this._with(g, {
            settings: { selectedRoots: {} },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            try {
                for (var i = 0; i < grids.length; i++) {
                    g.TreePreviewSun._lastRenderData = null;
                    g.TreePreviewSun._gridPointCache = null;
                    g.TreePreviewSun.settings.gridType = grids[i];
                    preview.activeMode = 'sun';
                    var before = preview.getOutput();
                    var out = g.TreePreviewHidden.ensure(preview);
                    self.check(before === null && out && out.rootNodes.length === 5 && out.gridPoints.length > 0 &&
                        out.mode === 'sun', 'Sun ' + grids[i] + ': no output before, roots and grid points after, no render loop');
                }
                g.TreePreviewFlat._lastRenderData = null;
                g.TreePreviewFlat._gridPointCache = null;
                preview.activeMode = 'flat';
                var flat = g.TreePreviewHidden.ensure(preview);
                self.check(flat && flat.rootNodes.length >= 5 && flat.gridPoints.length > 0, 'Flat: output without the render loop');
            } finally {
                g.TreePreviewSun.settings.gridType = oldGridType;
                g.TreePreviewSun._lastRenderData = null;
                g.TreePreviewSun._gridPointCache = null;
                g.TreePreviewFlat._lastRenderData = null;
                g.TreePreviewFlat._gridPointCache = null;
            }
        });
    },

    /**
     * ensure() gives what the preview's own frame (TreePreview._render, the render
     * loop's step, pan/zoom/fit and overlays included) leaves for getOutput, at the
     * size the hidden panel has: a box measured at 400 x 400 before it was hidden,
     * and a box never laid out (TreePreviewUtils.fitCanvas leaves the size at 0 and
     * _render draws at its 400 x 400 fallback, ensure() at FALLBACK_SIZE).
     */
    _testHiddenMatchesVisible: function(g) {
        var self = this;
        var schoolData = { Destruction: 40, Restoration: 30, Alteration: 25, Conjuration: 35, Illusion: 20 };
        var modes = [['sun', g.TreePreviewSun, 'fibonacci'], ['sun', g.TreePreviewSun, 'naive'], ['flat', g.TreePreviewFlat, null]];
        var sides = [400, 0];   // the box's size: measured, not laid out
        var boxSide = 0;
        var box = { getBoundingClientRect: function() { return { width: boxSide, height: boxSide }; } };
        var oldGridType = g.TreePreviewSun.settings.gridType;
        function snapshot(out) {
            return out ? JSON.stringify({ roots: out.rootNodes, points: out.gridPoints, grid: out.grid, mode: out.mode }) : 'none';
        }
        function clear(mode) { mode._lastRenderData = null; mode._gridPointCache = null; }
        this._with(g, {
            settings: { selectedRoots: {} },
            document: { getElementById: function(id) { return id === 'treePreviewCanvasWrap' ? box : null; } },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            try {
                for (var n = 0; n < modes.length * sides.length; n++) {
                    var i = n % modes.length;
                    boxSide = sides[Math.floor(n / modes.length)];
                    var preview = Object.create(g.TreePreview);
                    preview.modes = { sun: g.TreePreviewSun, flat: g.TreePreviewFlat };
                    preview.schoolData = schoolData;
                    preview.activeMode = modes[i][0];
                    preview._width = boxSide;
                    preview._height = boxSide;
                    preview.panX = 0; preview.panY = 0; preview.zoom = 1;
                    if (modes[i][2]) g.TreePreviewSun.settings.gridType = modes[i][2];

                    clear(modes[i][1]);
                    var hidden = snapshot(g.TreePreviewHidden.ensure(preview));
                    clear(modes[i][1]);
                    preview.ctx = g.TreePreviewHidden.nullContext();
                    preview.canvas = { width: boxSide, height: boxSide, style: {} };
                    preview._render();
                    var visible = snapshot(preview.getOutput());
                    self.check(hidden !== 'none' && hidden === visible && preview._width === boxSide,
                        modes[i][0] + (modes[i][2] ? ' ' + modes[i][2] : '') + (boxSide ? ' (box measured)' : ' (box not laid out)') +
                        ': the hidden output equals a drawn frame\'s');
                }
            } finally {
                g.TreePreviewSun.settings.gridType = oldGridType;
                clear(g.TreePreviewSun);
                clear(g.TreePreviewFlat);
            }
        });
    }
};

if (typeof window !== 'undefined') window.AutoBuildTest = AutoBuildTest;
if (typeof global !== 'undefined') global.AutoBuildTest = AutoBuildTest;
