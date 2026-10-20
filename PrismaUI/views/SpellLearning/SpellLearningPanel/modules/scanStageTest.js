/* ScanStage tests in an isolated browser-like context; run by run-tests.js. */
var ScanStageTest = {
    passed: 0, failed: 0,
    check: function(ok, label) {
        if (ok) this.passed++;
        else this.failed++;
        console.log('  [' + (ok ? 'PASS' : 'FAIL') + '] ' + label);
    },

    context: function() {
        var vm = require('vm');
        var fs = require('fs');
        var path = require('path');
        var elements = {};
        var make = function(id) {
            var el = {
                id: id, style: {}, disabled: false, textContent: '', attrs: {}, children: [],
                classList: { add: function() {}, toggle: function() {} },
                setAttribute: function(key, value) { this.attrs[key] = value; },
                removeAttribute: function(key) { delete this.attrs[key]; },
                querySelector: function() { return this.label || null; },
                click: function() { this.clicks = (this.clicks || 0) + 1; }
            };
            elements[id] = el;
            return el;
        };
        [
            'scanStageBtn', 'scanSummary', 'scanExclusions', 'scanBuiltCount', 'scanDraftFilters', 'scanBtn',
            'scanClearDraftBtn', 'scanDetailsToggle', 'scanDetailCounts', 'scanPluginHint',
            'scanGenerationBtn', 'scanOverview', 'scanModBreakdown', 'blacklistBtn', 'whitelistBtn',
            'scannerComplexContent', 'tgBuildBtn', 'tgApplyBtn', 'tgClearBtn', 'easyBuildBtn'
        ].forEach(make);
        ['scanBtn', 'blacklistBtn', 'whitelistBtn'].forEach(function(id) { elements[id].label = make(id + '-label'); });
        elements.scannerComplexContent.style.display = 'none';
        var context = {
            console: console, state: { lastSpellData: null },
            settings: { pluginWhitelist: [], spellBlacklist: [] }, scannerPresets: {},
            TreeGrowth: { _treeBuilt: false, _nodeCount: 0 },
            foldPluginName: function(name) { return name.toLowerCase(); },
            document: {
                getElementById: function(id) { return elements[id] || null; },
                querySelector: function() { return null; }
            },
            t: function(key) { return key; },
            tOr: function(key, params, fallback) {
                var result = fallback;
                Object.keys(params || {}).forEach(function(k) { result = result.split('{{' + k + '}}').join(params[k]); });
                return result;
            }
        };
        context.window = context;
        vm.createContext(context);
        ['uiHelpers.js', 'proceduralTreeBuilder.js', 'scanStage.js'].forEach(function(file) {
            vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), context, { filename: file });
        });
        context.elements = elements;
        context.ScanStage._initialized = true;
        return context;
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        var c = this.context();
        var s = c.ScanStage;
        var e = c.elements;
        s.refresh();
        this.check(s.stage() === 'scan' && e.scanStageBtn.textContent === 'buttons.scanSpells', 'before scan: one Scan Spells action');
        s.run();
        this.check(e.scanBtn.clicks === 1, 'Scan action uses original handler');
        c.state.lastSpellData = { spells: [
            { formId: '1', plugin: 'a.esp', name: 'One', school: 'Destruction' },
            { formId: '2', plugin: 'a.esp', taughtByTome: false },
            { formId: '3', plugin: 'a.esp', duplicateOf: '1' },
            { formId: '4', plugin: 'a.esp', gate: { band: 'out' } },
            { formId: '5', plugin: 'b.esp' },
            { formId: '6', plugin: 'new.esp' }
        ] };
        c.settings.spellBlacklist = [{ formId: '1' }];
        c.settings.pluginWhitelist = [{ plugin: 'B.esp', enabled: false }, { plugin: 'a.esp', enabled: true }];
        s.refresh();
        var count = s.counts();
        this.check(count.found === 6 && count.eligible === 1, 'summary uses actual scanned and eligible counts');
        this.check(count.tomes === 3 && count.blacklist === 1 && count.plugins === 1, 'exclusions explain complete difference without double counting');
        this.check(e.scanExclusions.textContent.indexOf('3') >= 0, 'filter gap is visible without opening details');
        this.check(e.whitelistBtn.label.textContent === 'Plugins: 1 disabled ›', 'whitelist describes disabled plugins, not selected-only inclusion');
        this.check(e.blacklistBtn.label.textContent === 'Excluded spells: 1 ›', 'excluded spells compact filter count');
        this.check(s.stage() === 'build' && e.scanStageBtn.textContent === 'buttons.buildTree', 'after scan: Build Tree action');
        s.run();
        this.check(e.easyBuildBtn.clicks === 1, 'Easy Build relays original preset-aware button');
        e.scannerComplexContent.style.display = '';
        s.run();
        this.check(e.tgBuildBtn.clicks === 1, 'Complex Build relays original generation control');
        e.tgBuildBtn.disabled = true;
        s.refresh();
        this.check(e.scanStageBtn.disabled, 'staged action reflects pending build');
        c.TreeGrowth._treeBuilt = true;
        c.TreeGrowth._nodeCount = 1;
        s.markDraft(true);
        this.check(s.stage() === 'apply' && e.scanStageBtn.textContent === 'buttons.applyTree', 'after build: Apply is separate action');
        this.check(!e.tgApplyBtn.clicks, 'build never implicitly applies');
        s.run();
        this.check(e.tgApplyBtn.clicks === 1, 'Apply relays original save path');
        this.check(e.scanSummary.textContent.indexOf('not applied yet') !== -1, 'unapplied draft is explicit');
        s.markApplied();
        this.check(e.scanSummary.textContent.indexOf('not applied yet') === -1, 'successful Apply changes summary state');
        c.settings.spellBlacklist = [{ formId: '6' }];
        s.refresh();
        this.check(s.counts().eligible === 1 && e.scanDraftFilters.textContent.indexOf('Filters changed') === 0,
            'same-sized different eligible set warns that draft needs rebuilding');
        this.check(s._applied, 'changing filters preserves applied state of the existing draft');
        c.settings.spellBlacklist = [{ formId: '1' }];
        s.refresh();
        this.check(e.scanDraftFilters.textContent === '', 'restoring original eligibility clears draft mismatch note');
        s._detailsOpen = true;
        s.refresh();
        this.check(e.scanDetailsToggle.attrs['aria-expanded'] === 'true' && e.scanDetailCounts.style.display === '', 'details unfold on click state');
        this.check(e.scanModBreakdown.innerHTML.indexOf('a.esp') !== -1, 'details include counts per plugin');
        s._detailsOpen = false;
        s.refresh();
        this.check(e.scanDetailCounts.style.display === 'none', 'details fold without removing scan status');
        c.settings.pluginWhitelist = [];
        s.refresh();
        this.check(e.whitelistBtn.label.textContent === 'Plugins: all ›', 'empty plugin filter includes every plugin');
        this.check(s.counts().eligible === 2, 'new and unlisted plugins remain included');
        c.TreeGrowth._treeBuilt = false;
        s.markDraft();
        this.check(s.stage() === 'build' && e.scanClearDraftBtn.style.display === 'none', 'clear draft returns to Build and hides Clear');
        var fs = require('fs');
        var path = require('path');
        var css = fs.readFileSync(path.join(__dirname, '../scan-stage.css'), 'utf8');
        this.check(css.indexOf('[data-prm-tab="altpaths"] { display: none !important; }') !== -1, 'unfinished Alternate Pathways stays hidden');
        this.check(css.indexOf('#scanStatusBar { display: none') === -1, 'working and auto-build scan status remains visible');
        c.state.fields = {};
        c.updateStatus = function() {};
        c.getTreeRulesPrompt = function() { return ''; };
        c.HomIcons = { setLabel: function() {} };
        c.callCpp = function(method) { c.sent = method; };
        require('vm').runInContext(fs.readFileSync(path.join(__dirname, 'buttonHandlers.js'), 'utf8'), c);
        s.markApplied();
        c.startScan();
        this.check(!s._applied && c.sent === 'ScanSpells', 'real scan handler resets applied state before native scan');
        var harness = fs.readFileSync(path.join(__dirname, '../dev-harness-toolbar.js'), 'utf8');
        this.check(harness.indexOf('TreeGrowthClassic.loadTreeData(treeData)') !== -1,
            'harness Build populates the actual draft needed by Apply');
        this.check(c.getPreviewTextColor() === '#e8e4d9', 'preview text has opaque readable fallback outside browser');
        c.document.addEventListener = function() {};
        c.addEventListener = function() {};
        c.setTimeout = function(callback) { callback(); };
        c.onPreReqMasterComplete = function(result) { c.lockReply = result; };
        require('vm').runInContext(fs.readFileSync(path.join(__dirname, '../dev-harness-bridge.js'), 'utf8'), c);
        c.callCpp('PreReqMasterScore', '{}');
        this.check(c.lockReply && c.lockReply.success === false,
            'harness scoring answers through real fallback callback so Build can finish');
        return { passed: this.passed, failed: this.failed };
    }
};
if (typeof module !== 'undefined' && module.exports) global.ScanStageTest = ScanStageTest;
