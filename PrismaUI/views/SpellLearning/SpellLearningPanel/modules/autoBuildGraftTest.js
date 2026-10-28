/**
 * AutoBuildGraftTest - node tests for what C++'s tree graft tells the build at game
 * start (run-tests.js; split out of autoBuildTest.js).
 *
 *   - onTreeGrafted after the startup info drops the load order notice line for that
 *     load order only, and nothing else; before it, no notice is decided; an
 *     unreadable message changes nothing;
 *   - with reload (a graft journal's recovery put the tree file back): a run building
 *     or applying ends without saving, a decision not yet made is dropped, and the
 *     tree is loaded again (LoadSpellTree), whose startup info decides afresh; the
 *     first info's config and presets waits go with it (the next info waits afresh).
 *
 * Depends on: autoBuildTest.js (loads autoBuild.js, autoBuildCpp.js and
 * autoBuildSetting.js; its _with swaps the globals stubbed here and puts them back).
 */

var AutoBuildGraftTest = {

    passed: 0,
    failed: 0,
    _print: (typeof console !== 'undefined') ? console.log.bind(console) : function() {},

    check: function(ok, name) {
        if (ok) { this.passed++; this._print('  [PASS] ' + name); }
        else { this.failed++; this._print('  [FAIL] ' + name); }
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        if (typeof g.AutoBuild === 'undefined' || typeof g.AutoBuildSetting === 'undefined' ||
                typeof g.onTreeGrafted !== 'function' || !g.AutoBuildTest) {
            this.check(false, 'AutoBuild, AutoBuildSetting and onTreeGrafted loaded (autoBuildTest.js)');
            return { passed: this.passed, failed: this.failed };
        }
        this._with = g.AutoBuildTest._with;
        this._testTreeGrafted(g);
        this._testTreeReplaced(g);
        this._testReloadClearsWaits(g);
        return { passed: this.passed, failed: this.failed };
    },

    /** A tree graft at the main menu (C++ onTreeGrafted) after or before the startup info. */
    _testTreeGrafted: function(g) {
        var self = this, A = g.AutoBuild;
        var changed = { tree: 'present', nodes: 9, loadOrder: 'bb', builtLoadOrder: 'aa', noticedLoadOrder: '' };
        var out = { events: [], status: [] }, shown = null;
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            startScan: function() { return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') out.events.push(JSON.parse(arg).event); },
            updateScanStatus: function(text, type, key) { out.status.push(key); shown = key; },
            scanStatusKey: function() { return shown; },
            t: function(key) { return key; },
            tOr: function(key, params, fallback) { return fallback; },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            var start = function(info) {
                g.AutoBuildSetting.loaded = false;
                g.onStartupTreeInfo(JSON.stringify(info));
                g.AutoBuildSetting.loadFrom({ autoBuild: 'whenMissing' });
                A.noteConfigLoaded();
                A.notePresetsApplied();
            };
            // After the notice: its scan line goes, for this load order only
            A._resetForTest();
            start(changed);
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'cc' }));
            var kept = shown === 'status.loadOrderChanged';
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'bb' }));
            self.check(kept && shown === 'scanner.readyToScan' && out.events.indexOf('grafted') !== -1,
                'a graft for this load order clears the load order notice line (not for another load order)');
            // A line that is not the notice stays
            shown = 'status.scannedSpellsSchools';
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'bb' }));
            self.check(shown === 'status.scannedSpellsSchools', 'a graft leaves any other scan line alone');
            // Before the startup info: no notice at all
            A._resetForTest();
            out.events = [];
            out.status = [];
            shown = null;
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'bb' }));
            start(changed);
            self.check(out.events.indexOf('notice') === -1 && out.status.indexOf('status.loadOrderChanged') === -1,
                'a graft before the startup info: no notice is decided');
            var before = { order: A._graftedLoadOrder, events: out.events.length, status: out.status.length, shown: shown };
            g.onTreeGrafted('not json');
            self.check(before.order === 'bb' && A._graftedLoadOrder === before.order && out.events.length === before.events &&
                out.status.length === before.status && shown === before.shown,
                'an unreadable graft message is passed over: the grafted load order, events and scan line as they were');
            A._resetForTest();
        });
    },

    /** A graft journal's recovery put the tree file back (onTreeGrafted reload) while a build at game start runs or waits. */
    _testTreeReplaced: function(g) {
        var self = this, A = g.AutoBuild, calls = [], events = [], builtWith = null, appliedWith = null;
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            state: { lastSpellData: { spells: [{ formId: '0x01' }] } },
            startScan: function() { calls.push('scan'); return true; },
            callCpp: function(name, arg) { if (name === 'AutoBuildEvent') events.push(JSON.parse(arg).event); else calls.push(name); },
            updateScanStatus: function() {},
            tOr: function(key, params, fallback) { return fallback; },
            TreePreviewHidden: { ensure: function() { return { rootNodes: [], gridPoints: [] }; } },
            TreeGrowthClassic: {
                buildTree: function(opts) { calls.push('build'); builtWith = opts; return true; },
                applyTree: function(opts) { calls.push('apply'); appliedWith = opts; return true; }
            },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            var ready = function() { g.AutoBuildSetting.loadFrom({}); A.noteConfigLoaded(); A.notePresetsApplied(); };
            var present = { tree: 'present', nodes: 5, loadOrder: 'aa', builtLoadOrder: 'aa' };
            // Building: the build's result comes after the notice - no Apply
            A._resetForTest();
            ready();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            A.onSpellData();
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'aa', reload: true }));
            builtWith.onComplete('late');
            self.check(!A.isRunning() && calls.join(',') === 'scan,build,LoadSpellTree' && events.indexOf('yielded') !== -1,
                'tree put back during the build: the run ends, its result is not applied, the tree is loaded again');
            g.onStartupTreeInfo(present);
            self.check(calls.indexOf('scan', 1) === -1 && events.filter(function(e) { return e === 'decided'; }).length === 2,
                'the reload\'s startup info decides afresh: the tree is there, nothing is built');
            // Applying: the Apply still decluttering does not save
            A._resetForTest();
            calls.length = 0;
            ready();
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            A.onSpellData();
            builtWith.onComplete('ok');
            g.onTreeGrafted({ loadOrder: '', reload: true });
            self.check(appliedWith && appliedWith.isCurrent() === false && calls[calls.length - 1] === 'LoadSpellTree',
                'tree put back while Apply declutters: the Apply is no longer current, so it does not save');
            // Not decided yet (no config): nothing is built
            A._resetForTest();
            calls.length = 0;
            g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
            g.onTreeGrafted(JSON.stringify({ loadOrder: '', reload: true }));
            ready();
            g.onStartupTreeInfo(present);
            self.check(calls.join(',') === 'LoadSpellTree' && !A.isRunning(),
                'tree put back before the decision: the old "missing" builds nothing, the reload\'s info decides');
            // The usual graft notice (reload false) loads nothing
            calls.length = 0;
            g.onTreeGrafted(JSON.stringify({ loadOrder: 'aa', reload: false }));
            self.check(calls.length === 0, 'a graft notice without reload: no tree load');
            A._resetForTest();
        });
    },

    /** A reload before the decision: the first startup info's config and presets waits are dropped, the next info waits afresh. */
    _testReloadClearsWaits: function(g) {
        var self = this, A = g.AutoBuild, timers = [];
        this._with(g, {
            settings: { autoBuild: 'whenMissing' },
            setTimeout: function(fn, ms) { timers.push({ fn: fn, ms: ms, live: true }); return timers.length; },
            clearTimeout: function(id) { if (timers[id - 1]) timers[id - 1].live = false; },
            callCpp: function() {},
            updateScanStatus: function() {},
            tOr: function(key, params, fallback) { return fallback; },
            console: { log: function() {}, warn: function() {}, error: function() {} }
        }, function() {
            var waits = function() { return timers.filter(function(t) { return t.live && t.ms === A.WAIT_MS; }).length; };
            try {
                A._resetForTest();
                g.onStartupTreeInfo({ tree: 'missing', loadOrder: 'aa' });
                var first = waits();
                g.onTreeGrafted({ loadOrder: '', reload: true });
                var afterReload = waits();
                g.onStartupTreeInfo({ tree: 'present', nodes: 5, loadOrder: 'aa', builtLoadOrder: 'aa' });
                self.check(first === 2 && afterReload === 0 && waits() === 2,
                    'tree put back before the decision: the first info\'s two waits are cleared, the reload\'s info starts its own');
            } finally {
                A._resetForTest();
            }
        });
    }
};

if (typeof window !== 'undefined') window.AutoBuildGraftTest = AutoBuildGraftTest;
if (typeof global !== 'undefined') global.AutoBuildGraftTest = AutoBuildGraftTest;
