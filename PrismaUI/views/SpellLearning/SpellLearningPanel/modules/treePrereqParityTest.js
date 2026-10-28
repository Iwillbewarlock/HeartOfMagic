/**
 * TreePrereqParityTest - node test: the panel's own tree load sends C++ the prerequisites
 * the plugin's game start applies (run-tests.js).
 *
 * include/TreePrereqEntries.h ports the panel's rules (cppCallbacks.js updateTreeData,
 * treeLoad.js loadTreeData, _loadTrustedTree and sendTreePrerequisites, TreeParser.withoutId,
 * clampSoftNeeded, PrereqEdit.repair) so game start can apply a tree with no panel. Its
 * cases (tools/tree-prereq-check.cpp, `tree-prereq-check --dump-cases`, kept as
 * test-data/treePrereqCases.json) run here through the real updateTreeData, in a context
 * of their own with the page's other modules stubbed, and SetTreePrerequisites is caught:
 *   Built        {clear: true}, then exactly the case's entries
 *   NoSchools    nothing sent (updateTreeData stops first)
 *   PanelThrows  nothing sent (the panel's code throws, updateTreeData catches it)
 *   SlowPath     the fast path (_loadTrustedTree) not taken
 *   NotModelled  not compared (the plugin refuses those trees, the panel does what it does)
 * A changed rule on either side fails it. When the tool is built (build/tools/Release),
 * its --dump-cases must also equal the copy here, so a case added there and not copied fails.
 *
 * Depends on: prereqEdit.js, treeParser.js, treeLoad.js, cppCallbacks.js (read from disk);
 * node's vm, fs, path, child_process.
 */

var TreePrereqParityTest = {

    passed: 0,
    failed: 0,

    FILES: ['prereqEdit.js', 'treeParser.js', 'treeLoad.js', 'cppCallbacks.js'],
    CASES_FILE: ['..', 'test-data', 'treePrereqCases.json'],
    TOOL: ['..', '..', '..', '..', '..', 'build', 'tools', 'Release', 'tree-prereq-check.exe'],
    TOOL_TIMEOUT_MS: 10000,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** The same value with object keys sorted, as text: the comparison does not hang on key order */
    _canonical: function(value) {
        var self = this;
        if (Array.isArray(value)) return '[' + value.map(function(v) { return self._canonical(v); }).join(',') + ']';
        if (value && typeof value === 'object') {
            return '{' + Object.keys(value).sort().map(function(k) {
                return JSON.stringify(k) + ':' + self._canonical(value[k]);
            }).join(',') + '}';
        }
        return JSON.stringify(value);
    },

    /** A context of its own: the four modules and stubs for what their tree load touches */
    _context: function(vm, fs, path) {
        var noop = function() {};
        var element = { classList: { add: noop, remove: noop, toggle: noop }, style: {}, textContent: '', setAttribute: noop };
        var sandbox = {
            console: { log: noop, info: noop, warn: noop, error: noop },
            setTimeout: noop,
            clearTimeout: noop,
            document: { getElementById: function() { return element; }, querySelector: function() { return null; } },
            state: {},
            settings: { schoolColors: {}, schoolVisibility: {}, schoolConfigs: {} },
            _logToSKSE: noop,
            switchTab: noop,
            setTreeStatusKey: noop,
            SpellCache: { requestBatch: noop },
            calls: [],
            trustedLoads: 0
        };
        sandbox.window = sandbox;
        sandbox.callCpp = function(name, text) { sandbox.calls.push({ name: name, text: text }); };
        var context = vm.createContext(sandbox);
        for (var i = 0; i < this.FILES.length; i++) {
            var file = path.join(__dirname, this.FILES[i]);
            vm.runInContext(fs.readFileSync(file, 'utf8'), context, { filename: file });
        }
        var trusted = sandbox._loadTrustedTree;
        sandbox._loadTrustedTree = function() {
            sandbox.trustedLoads++;
            return trusted.apply(this, arguments);
        };
        return sandbox;
    },

    _runCase: function(sandbox, c) {
        sandbox.calls.length = 0;
        sandbox.trustedLoads = 0;
        sandbox.state = {};
        sandbox.updateTreeData(c.tree);
        var sent = sandbox.calls.filter(function(call) { return call.name === 'SetTreePrerequisites'; });
        switch (c.outcome) {
            case 'Built':
                this.check(sent.length === 2 && sent[0].text === JSON.stringify({ clear: true }) &&
                    this._canonical(JSON.parse(sent[1].text)) === this._canonical(c.entries),
                    'built: ' + c.name + (sent.length === 2 ? '' : ' (' + sent.length + ' messages)'));
                if (sent.length === 2 && this._canonical(JSON.parse(sent[1].text)) !== this._canonical(c.entries)) {
                    console.log('        panel sent ' + sent[1].text);
                }
                return true;
            case 'NoSchools':
            case 'PanelThrows':
                this.check(sent.length === 0, c.outcome + ': ' + c.name + ' - nothing sent (' + sent.length + ' messages)');
                return true;
            case 'SlowPath':
                this.check(sandbox.trustedLoads === 0, 'slow path: ' + c.name + ' - the fast path not taken');
                return true;
            default:
                return false;   // NotModelled
        }
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var cases = null, sandbox = null;
        try {
            cases = JSON.parse(fs.readFileSync(path.join.apply(path, [__dirname].concat(this.CASES_FILE)), 'utf8'));
            sandbox = this._context(vm, fs, path);
        } catch (e) {
            this.check(false, 'cases and modules loaded: ' + e.message);
            return { passed: this.passed, failed: this.failed };
        }
        this.check(Array.isArray(cases) && cases.length > 0, 'the cases are there (' + (cases ? cases.length : 0) + ')');

        var tool = path.join.apply(path, [__dirname].concat(this.TOOL));
        if (fs.existsSync(tool)) {
            var dumped = null;
            try {
                dumped = JSON.parse(require('child_process').execFileSync(tool, ['--dump-cases'],
                    { encoding: 'utf8', timeout: this.TOOL_TIMEOUT_MS }));
            } catch (e) {
                dumped = null;
            }
            this.check(dumped !== null && this._canonical(dumped) === this._canonical(cases),
                'test-data/treePrereqCases.json is tree-prereq-check --dump-cases (regenerate it after a case changes)');
        } else {
            console.log('  (tree-prereq-check not built: the copy of its cases is not compared with it)');
        }

        var skipped = 0;
        for (var i = 0; i < cases.length; i++) {
            if (!this._runCase(sandbox, cases[i])) skipped++;
        }
        console.log('  (' + skipped + ' cases the plugin does not model, not compared)');
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TreePrereqParityTest = TreePrereqParityTest;
if (typeof global !== 'undefined') global.TreePrereqParityTest = TreePrereqParityTest;
