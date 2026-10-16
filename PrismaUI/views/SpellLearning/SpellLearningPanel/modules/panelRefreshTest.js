/**
 * PanelRefreshTest - node tests for what the panel shows and sends after a change
 * the player makes in it (run-tests.js)
 *
 * From the 2026-10-17 check:
 *  - Unlock / cheat unlock: the tree was not repainted (onSpellUnlocked only told
 *    WheelRenderer, which holds no nodes while the canvas tree is in use, and
 *    updateSpellState skips its own refresh once the state is already 'unlocked').
 *  - Classic's tier zone chart kept its own copy of the zones: a preset did not show
 *    on it, and a drag started from the stale copy and wrote it over the preset.
 *  - Edit mode's prerequisite changes never reached C++ (a tome was judged by the
 *    old links until the next tree load), and a spell added in edit mode had no
 *    persistentId, so a load order change could not move it; it was also put into
 *    the tree's node list twice.
 *
 * Depends on: progressionUI.js, progressUpdates.js, classic/classicSettings.js,
 * prereqEdit.js, editMode.js, treeLoad.js - under node this file runs them into the
 * global scope the way the page's script tags do. Swaps `state`, `settings`,
 * `callCpp`, `document`, CanvasRenderer, SmartRenderer, WheelRenderer and the page
 * functions they call for its own while it runs.
 */

var PanelRefreshTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return false;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        // progressionUI.js may be in already as a CommonJS module (openRefreshGateTest), where
        // onUnlockClick is not a global: it is run again as a script
        var files = [['progressUpdates.js', 'ProgressUpdates'], ['progressionUI.js', 'onUnlockClick'],
                     ['classic/classicSettings.js', 'ClassicSettings'], ['prereqEdit.js', 'PrereqEdit'],
                     ['editMode.js', 'EditMode'], ['treeLoad.js', 'sendTreePrerequisites']];
        var hadListener = typeof g.addEventListener === 'function';
        if (!hadListener) g.addEventListener = function() {};   // editMode.js waits for the page's load event
        try {
            for (var i = 0; i < files.length; i++) {
                if (typeof g[files[i][1]] !== 'undefined') continue;
                var file = path.join(__dirname, files[i][0]);
                vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
            }
        } finally {
            if (!hadListener) delete g.addEventListener;
        }
        return typeof g.onUnlockClick === 'function' && typeof g.ProgressUpdates !== 'undefined' &&
            typeof g.EditMode !== 'undefined' && typeof g.sendTreePrerequisites === 'function';
    },

    /** Runs fn(env) with the listed globals replaced, then puts them back */
    _with: function(g, over, fn) {
        var saved = {}, had = {}, oldLog = console.log;
        Object.keys(over).forEach(function(k) { had[k] = Object.prototype.hasOwnProperty.call(g, k); saved[k] = g[k]; g[k] = over[k]; });
        console.log = function(first) {
            if (typeof first === 'string' && /^\[(SpellLearning|EditMode)\]/.test(first)) return;
            oldLog.apply(console, arguments);
        };
        try {
            fn.call(this);
        } finally {
            console.log = oldLog;
            Object.keys(over).forEach(function(k) { if (had[k]) g[k] = saved[k]; else delete g[k]; });
        }
    },

    _noDocument: function() {
        return { getElementById: function() { return null; }, querySelector: function() { return null; },
                 querySelectorAll: function() { return []; }, addEventListener: function() {},
                 removeEventListener: function() {} };
    },

    // =========================================================================
    // UNLOCK REPAINT
    // =========================================================================

    /** A stand-in tree: A (available) opens B (locked), and the renderers counting their calls */
    _unlockEnv: function(env) {
        var a = { id: 'A', formId: 'A', name: 'Alpha', state: 'available', school: 'Destruction', prerequisites: [] };
        var b = { id: 'B', formId: 'B', name: 'Beta', state: 'locked', school: 'Destruction', prerequisites: ['A'] };
        env.a = a; env.b = b;
        env.refreshes = 0; env.recalcs = 0; env.sent = [];
        var canvas = { _needsRender: false };
        env.canvas = canvas;
        return {
            state: { treeData: { nodes: [a, b] }, spellProgress: {}, learningTargets: {}, selectedNode: null },
            settings: { discoveryMode: true, cheatMode: false },
            document: this._noDocument(),
            callCpp: function(m, d) { env.sent.push([m, d]); },
            _panelVisible: true,
            SmartRenderer: { activeRenderer: 'canvas', refresh: function() { env.refreshes++; } },
            CanvasRenderer: canvas,
            WheelRenderer: { updateNodeStates: function() {}, render: function() {} },
            recalculateNodeAvailability: function() {
                env.recalcs++;
                if (a.state === 'unlocked' && b.state === 'locked') { b.state = 'available'; return 1; }
                return 0;
            },
            syncDuplicateState: function() {},
            setTreeStatusKey: function() {},
            updateUnlockedCount: function() {},
            updateDetailsProgression: function() {},
            showSpellDetails: function() {},
            getCanonicalFormId: undefined,
            resolveCanonicalId: undefined,
            t: function(k) { return k; }
        };
    },

    _testUnlockRepaint: function(g) {
        var env = {};
        this._with(g, this._unlockEnv(env), function() {
            g.onSpellUnlocked(JSON.stringify({ success: true, formId: 'A' }));
            this.check(env.a.state === 'unlocked' && env.b.state === 'available' && env.refreshes === 1 &&
                env.canvas._needsRender === true, 'Unlock: the canvas tree repaints with the spell mastered and its child open');
        });
        env = {};
        this._with(g, this._unlockEnv(env), function() {
            env.a.state = 'unlocked';   // the cheat button set it before C++ answered
            g.onSpellUnlocked(JSON.stringify({ success: true, formId: 'A' }));
            this.check(env.refreshes === 1 && env.canvas._needsRender === true,
                'Unlock answer for a spell already shown unlocked: still repainted');
        });
        env = {};
        this._with(g, this._unlockEnv(env), function() {
            g._panelVisible = false;
            g.onSpellUnlocked(JSON.stringify({ success: true, formId: 'A' }));
            this.check(env.a.state === 'unlocked' && env.refreshes === 0, 'Unlock with the panel closed: state kept, no repaint');
        });
        env = {};
        this._with(g, this._unlockEnv(env), function() {
            g.onSpellUnlocked(JSON.stringify({ success: false, formId: 'A' }));
            this.check(env.a.state === 'available' && env.refreshes === 0 && env.recalcs === 0,
                'Unlock refused by C++: nothing changes, nothing repaints');
        });
        env = {};
        this._with(g, this._unlockEnv(env), function() {
            g.settings.cheatMode = true;
            g.state.selectedNode = env.a;
            g.onUnlockClick();
            this.check(env.sent.length === 1 && env.sent[0][0] === 'CheatUnlockSpell' && env.a.state === 'unlocked' &&
                env.b.state === 'available' && env.recalcs === 1 && env.refreshes === 1 && env.canvas._needsRender === true,
                'Cheat unlock: sent, its child opens and the tree repaints at once');
            env.refreshes = 0;
            g.onUnlockClick();   // now Relock
            this.check(env.sent[1][0] === 'RelockSpell' && env.a.state === 'available' && env.refreshes === 1,
                'Cheat relock: the tree repaints at once');
        });
    },

    // =========================================================================
    // TIER ZONES
    // =========================================================================

    /** A tier chart: one column per tier with its fill, handles and track (100 px tall) */
    _chartDoc: function(env) {
        var cols = [], handles = [];
        ['Novice', 'Apprentice', 'Adept', 'Expert', 'Master'].forEach(function(tier) {
            var pct = { textContent: '' };
            var fill = { style: {}, querySelector: function() { return pct; } };
            var track = { getBoundingClientRect: function() { return { bottom: 100, height: 100 }; },
                          querySelector: function() { return fill; } };
            var col = { getAttribute: function() { return tier; },
                        querySelector: function(sel) { return sel === '.classic-tier-fill' ? fill : track; } };
            ['min', 'max'].forEach(function(kind) {
                var h = { listeners: {}, tier: tier,
                          addEventListener: function(ev, fn) { this.listeners[ev] = fn; },
                          getAttribute: function() { return kind; },
                          closest: function() { return col; } };
                handles.push(h);
            });
            col.fill = fill;
            cols.push(col);
        });
        var chart = { querySelectorAll: function(sel) { return sel === '.classic-tier-col' ? cols : handles; } };
        env.cols = cols; env.handles = handles; env.docListeners = {};
        return {
            getElementById: function(id) { return id === 'tgClassicTierChart' ? chart : null; },
            querySelector: function() { return null; }, querySelectorAll: function() { return []; },
            addEventListener: function(ev, fn) { (env.docListeners[ev] = env.docListeners[ev] || []).push(fn); }
        };
    },

    _fill: function(env, tier) {
        for (var i = 0; i < env.cols.length; i++) if (env.cols[i].getAttribute() === tier) return env.cols[i].fill;
        return null;
    },

    _drag: function(env, tier, kind, clientY) {
        var h = env.handles.filter(function(x) { return x.tier === tier && x.getAttribute() === kind; })[0];
        h.listeners.mousedown({ preventDefault: function() {}, stopPropagation: function() {} });
        env.docListeners.mousemove.forEach(function(fn) { fn({ clientY: clientY }); });
        env.docListeners.mouseup.forEach(function(fn) { fn({}); });
    },

    _testTierZones: function(g) {
        var CS = g.ClassicSettings, env = {};
        var over = {
            document: this._chartDoc(env),
            TreePreviewUtils: { settingHTML: function() { return ''; }, bindInput: function() {} },
            TreeBuilderSetting: undefined,
            t: function(k) { return k; },
            tOr: function(k, p, f) { return f || k; }
        };
        var oldSource = CS._zoneSource;
        this._with(g, over, function() {
            try {
                var settings = { tierZones: { Novice: { min: 5, max: 30 } } };
                var changed = [];
                var html = CS.buildHTML(settings);
                CS.bindEvents({ onTierZoneChanged: function(tier, min, max) {
                    changed.push([tier, min, max]);
                    settings.tierZones[tier] = { min: min, max: max };   // as classicMain.js does
                } });
                this.check(/bottom:\s*5%/.test(html) && this._fill(env, 'Novice').style.bottom === '5%' &&
                    this._fill(env, 'Apprentice').style.bottom === '10%',
                    'Tier zones: chart drawn from the settings, a tier they lack from the defaults');

                // A preset writes the settings in place (scannerPresets.js _deepCopy), then refreshes the chart
                settings.tierZones.Novice.min = 20;
                settings.tierZones.Novice.max = 60;
                CS.refreshTierChart();
                this.check(this._fill(env, 'Novice').style.bottom === '20%' && this._fill(env, 'Novice').style.height === '40%',
                    'Tier zones: the chart shows a preset\'s zones');

                this._drag(env, 'Novice', 'max', 10);   // top handle to 90%
                this.check(settings.tierZones.Novice.min === 20 && settings.tierZones.Novice.max === 90 &&
                    changed.length === 1 && changed[0].join() === 'Novice,20,90',
                    'Tier zones: a drag starts from the current zones (the preset\'s min stays)');

                this._drag(env, 'Novice', 'min', 0);    // bottom handle past the top
                this.check(settings.tierZones.Novice.min === 85 && settings.tierZones.Novice.max === 90,
                    'Tier zones: the bottom handle stops 5 below the top');

                this.check(CS.getTierZones().Novice.min === 85 && CS.getTierZones() !== settings.tierZones,
                    'Tier zones: getTierZones reads the settings, as a copy');

                var other = {};
                CS.buildHTML(other);
                this.check(other.tierZones && other.tierZones.Master.min === CS.DEFAULT_TIER_ZONES.Master.min &&
                    other.tierZones.Master !== CS.DEFAULT_TIER_ZONES.Master,
                    'Tier zones: settings without zones get a copy of the defaults (the defaults are never changed)');
            } finally {
                CS._zoneSource = oldSource;
            }
        });
        var src = require('fs').readFileSync(require('path').join(__dirname, 'scannerPresets.js'), 'utf8');
        var at = src.indexOf('_deepCopy(s.classicSettings, TreeGrowthClassic.settings)');
        this.check(at !== -1 && src.indexOf('ClassicSettings.refreshTierChart()', at) !== -1,
            'Tier zones: applying a scanner preset refreshes the chart');
    },

    // =========================================================================
    // EDIT MODE: PREREQUISITES TO C++, SPAWNED SPELLS
    // =========================================================================

    /** P (root) -> S; X not linked; one array for the tree and the canvas, as a loaded tree has */
    _editEnv: function(env) {
        var p = { id: 'P', formId: 'P', school: 'A', state: 'available', children: ['S'], prerequisites: [], hardPrereqs: [], softPrereqs: [], softNeeded: 0, isRoot: true };
        var x = { id: 'X', formId: 'X', school: 'A', state: 'available', children: [], prerequisites: [], hardPrereqs: [], softPrereqs: [], softNeeded: 0 };
        var s = { id: 'S', formId: 'S', school: 'A', state: 'locked', children: [], prerequisites: ['P'], hardPrereqs: ['P'], softPrereqs: [], softNeeded: 0 };
        var nodes = [p, x, s];
        var raw = { schools: { A: { root: 'P', nodes: nodes.map(function(n) {
            return { formId: n.formId, children: n.children.slice(), prerequisites: n.prerequisites.slice(),
                     hardPrereqs: n.hardPrereqs.slice(), softPrereqs: [], softNeeded: 0 };
        }) } } };
        env.nodes = nodes; env.p = p; env.x = x; env.s = s; env.raw = raw;
        env.sent = []; env.recalcs = 0; env.saves = 0;
        env.canvas = { nodes: nodes, edges: [{ from: 'P', to: 'S' }], _nodeMap: new Map(nodes.map(function(n) { return [n.formId, n]; })),
                       _needsRender: false, showDebugGrid: true, buildSpatialIndex: function() {}, _buildDiscoveryVisibility: function() {} };
        return {
            state: { treeData: { nodes: nodes, rawData: raw }, selectedNode: null, spellProgress: {} },
            document: this._noDocument(),
            callCpp: function(m, d) { env.sent.push([m, d]); },
            CanvasRenderer: env.canvas,
            saveTreeToFile: function() { env.saves++; return true; },
            recalculateNodeAvailability: function() { env.recalcs++; return 0; },
            setTreeStatus: function() {}, setTreeStatusKey: function() {},
            invalidateNodeLookup: function() {}, SpellNames: undefined,
            showSpellDetails: function() {}, TreeToolsScale: undefined,
            t: function(k) { return k; }
        };
    },

    _prereqsSent: function(env) {
        var calls = env.sent.filter(function(c) { return c[0] === 'SetTreePrerequisites'; });
        return calls.length === 2 && JSON.parse(calls[0][1]).clear === true ? JSON.parse(calls[1][1]) : null;
    },

    _activate: function() {
        EditMode.isActive = true;
        EditMode.undoStack = [];
        EditMode._prereqsChanged = false;
    },

    _testEditModePrereqs: function(g) {
        var env = {};
        this._with(g, this._editEnv(env), function() {
            this._activate();
            EditMode.addPrerequisite(env.x, env.s);
            this.check(this._prereqsSent(env) === null, 'Edit mode: a link is not sent while editing');
            EditMode.deactivate();
            var sent = this._prereqsSent(env);
            var sNode = sent && sent.filter(function(n) { return n.formId === 'S'; })[0];
            this.check(sNode && sNode.hardPrereqs.join() === 'P,X' && sent.length === 3,
                'Edit mode ends: the whole tree\'s prerequisites go to C++ (cleared first), with the new link');
            this.check(env.recalcs === 1 && env.canvas._needsRender === true, 'Edit mode ends: available and locked spells recounted');
            env.sent.length = 0;
            this._activate();
            EditMode.deactivate();
            this.check(env.sent.length === 0 && env.recalcs === 1, 'Edit mode with no prerequisite change: nothing sent');
        });
        env = {};
        this._with(g, this._editEnv(env), function() {
            this._activate();
            EditMode.deletePrerequisite(env.s, 'P');
            // The panel closes with edit mode still on (cppCallbacks.js onPanelHiding)
            this.check(EditMode.commitPrerequisites() === true && this._prereqsSent(env) &&
                this._prereqsSent(env).filter(function(n) { return n.formId === 'S'; })[0].hardPrereqs.length === 0,
                'Panel hidden in edit mode: a deleted link reaches C++');
            env.sent.length = 0;
            EditMode.deactivate();
            this.check(env.sent.length === 0, 'and is not sent a second time when edit mode ends');
        });
        env = {};
        this._with(g, this._editEnv(env), function() {
            this._activate();
            g.state.treeData = null;   // no tree (cleared while editing)
            EditMode._prereqsChanged = true;
            this.check(EditMode.commitPrerequisites() === false && env.sent.length === 0, 'No tree: nothing sent, no throw');
            EditMode.isActive = false;
        });
        var cb = require('fs').readFileSync(require('path').join(__dirname, 'cppCallbacks.js'), 'utf8');
        var hide = cb.slice(cb.indexOf('window.onPanelHiding = function('));
        hide = hide.slice(0, hide.indexOf('\n};'));
        this.check(/EditMode\.commitPrerequisites\(\)/.test(hide), 'onPanelHiding sends edit mode\'s changes');
    },

    _testSpawnPersistentId: function(g) {
        var env = {};
        this._with(g, this._editEnv(env), function() {
            this._activate();
            EditMode.spawnPosition = { x: 10, y: 20, school: 'A', tier: 1 };
            EditMode.hideSpawnMenu = function() {};
            EditMode.spawnSpell({ formId: '0xFE000801', name: 'Gamma', school: 'A', level: 'Novice',
                                  persistentId: 'Gamma.esl|0x000801' });
            var node = env.canvas._nodeMap.get('0xFE000801');
            var raw = env.raw.schools.A.nodes.filter(function(n) { return n.formId === '0xFE000801'; })[0];
            this.check(node && node.persistentId === 'Gamma.esl|0x000801' && raw && raw.persistentId === 'Gamma.esl|0x000801',
                'Spawn: the node and the saved tree keep the spell\'s persistentId');
            this.check(env.nodes.filter(function(n) { return n === node; }).length === 1,
                'Spawn: the spell is in the tree\'s node list once (the list is the canvas\'s)');
            this.check(EditMode._prereqsChanged === true, 'Spawn: C++ hears of the new spell when edit mode ends');

            EditMode.spawnPosition = { x: 30, y: 20, school: 'A', tier: 1 };
            EditMode.spawnSpell({ formId: '0xFE000801', name: 'Gamma', school: 'A', level: 'Novice',
                                  persistentId: 'Gamma.esl|0x000801' });
            var copy = env.nodes.filter(function(n) { return n.originalFormId === '0xFE000801'; })[0];
            this.check(copy && !copy.persistentId,
                'Spawn of a spell already on the tree: the copy has no persistentId (it would become the original\'s formId)');

            EditMode.spawnPosition = { x: 50, y: 20, school: 'A', tier: 1 };
            EditMode.spawnSpell({ formId: '0x00012FCD', name: 'Flames', school: 'A', level: 'Novice' });
            var plain = env.canvas._nodeMap.get('0x00012FCD');
            this.check(plain && !plain.persistentId, 'Spawn of a spell the scan gave no persistentId: none made up');
            EditMode.isActive = false;
            delete EditMode.hideSpawnMenu;
        });
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        console.log('[PanelRefreshTest]');
        var g = (typeof global !== 'undefined') ? global : window;
        var loaded = false;
        try {
            loaded = this._load(g);
        } catch (e) {
            console.log('  load: ' + e.message);
        }
        if (!loaded) {
            this.check(false, 'progressionUI.js, progressUpdates.js, classicSettings.js, editMode.js, treeLoad.js load');
            return { passed: this.passed, failed: this.failed };
        }
        this._testUnlockRepaint(g);
        this._testTierZones(g);
        this._testEditModePrereqs(g);
        this._testSpawnPersistentId(g);
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.PanelRefreshTest = PanelRefreshTest;
if (typeof module !== 'undefined') module.exports = PanelRefreshTest;
