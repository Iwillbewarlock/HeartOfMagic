/**
 * ScanStage - the Scan tab's summary line and its one main button (2.0 review, item 7)
 *
 * One line says where the player is: "753 spells found · 681 go in the tree · not built
 * yet". A difference between found and in-the-tree is never hidden: a second line says
 * how many were left out and why, in the order the build filters them (getPrimedSpells:
 * the tome filter, the excluded list, the plugin list) and, once built, the ones the
 * builder did not place.
 *
 * The main button follows the stage: Scan Spells, Build Tree, Apply Tree (Build and Apply
 * stay two steps: the player looks at the preview before applying), then "Tree applied".
 * It is a relay, as the Easy page's buttons are (easyMode.js): it clicks the real button
 * (#scanBtn, the Easy or Complex Build, #tgApplyBtn), whose handlers, guards and the build
 * at game start (autoBuild.js) stay as they were. The other actions are small text
 * buttons: Scan again, Import, Clear draft tree (it clears the built draft, not anyone's
 * progress). "Scan details" folds the counts per school; the filters read as lines with
 * their state ("Excluded spells: 12", "Plugins: all").
 *
 * "Applied" is this module's own flag: set when Apply (or the build at game start) saved
 * the draft (TreeGrowthClassic.applyTree calls markApplied), dropped when the draft changes
 * (TreeGrowth.setTreeBuilt, Pre Req Master's lock changes and an Import call onDraftChanged) or a
 * new scan comes in (updateSpellData calls onScanned). A draft keeps the real Build off until it is
 * cleared (TreeGrowth.updateBuildButton), so while one exists the stage is Apply, and Clear draft
 * tree - shown whenever a draft exists - is the way back to Build.
 *
 * Depends on: state (lastSpellData), settings (spellBlacklist, pluginWhitelist),
 * isTaughtByTome / filterBlacklistedSpells / filterWhitelistedSpells / spellPluginName
 * (proceduralTreeBuilder.js), foldPluginName (constants.js), TreeGrowth, setI18n / tOr
 * (i18n.js), HomIcons (the relayed buttons' labels are left alone).
 */
var ScanStage = {
    SEPARATOR: ' · ',

    _applied: false,
    _detailsOpen: false,
    _initialized: false,

    /** The page's document (a stand-in under node tests) */
    _doc: function() { return document; },
    _el: function(id) { return this._doc().getElementById(id); },

    // =========================================================================
    // WHAT THE PAGE SAYS (pure: tested without a page)
    // =========================================================================

    /**
     * The spells at each filter, in the build's order (getPrimedSpells).
     * @param {Array} spells - the scan's spells
     * @returns {{found:number, tomeOut:number, excluded:number, pluginsOut:number, inTree:number}}
     */
    counts: function(spells) {
        var all = Array.isArray(spells) ? spells : [];
        var a = (typeof isTaughtByTome === 'function') ? all.filter(isTaughtByTome) : all.slice();
        var b = (typeof filterBlacklistedSpells === 'function') ? filterBlacklistedSpells(a) : a;
        var c = (typeof filterWhitelistedSpells === 'function') ? filterWhitelistedSpells(b) : b;
        return {
            found: all.length,
            tomeOut: all.length - a.length,
            excluded: a.length - b.length,
            pluginsOut: b.length - c.length,
            inTree: c.length
        };
    },

    /**
     * @param {{scanning:boolean, hasSpells:boolean, building:boolean, built:boolean, applied:boolean}} s
     * @returns {string} 'scanning' | 'scan' | 'building' | 'build' | 'apply' | 'applied'
     */
    stageOf: function(s) {
        if (s.scanning) return 'scanning';
        if (!s.hasSpells) return 'scan';
        if (s.building) return 'building';
        if (!s.built) return 'build';
        return s.applied ? 'applied' : 'apply';
    },

    /**
     * The summary as language keys: { parts, gap } - each item [key, params, English].
     * @param {string} stage
     * @param {Object} c - counts()
     * @param {number} placed - spells the built draft placed (TreeGrowth._nodeCount)
     */
    summary: function(stage, c, placed, treeCount) {
        if (stage === 'scan') {
            // A saved tree came in with the game, but no scan this session (the build at game start only
            // scans when there is no tree): say the tree is there, not that nothing is
            if (treeCount > 0) return { parts: [['scan.summaryTreeNoScan', { count: treeCount }, 'Your tree has {{count}} spells · scan to build a new one']], gap: [] };
            return { parts: [['scan.summaryNoScan', null, 'No scan yet: scan your spells to build a tree from them']], gap: [] };
        }
        if (stage === 'scanning') return { parts: [['status.scanningAllSpells', null, 'Scanning all spells...']], gap: [] };
        var built = stage === 'apply' || stage === 'applied';
        var inTree = built ? placed : c.inTree;
        var parts = [['scan.summaryFound', { count: c.found }, '{{count}} spells found']];
        if (stage === 'applied') parts.push(['scan.summaryInYourTree', { count: inTree }, '{{count}} in your tree']);
        else if (built) parts.push(['scan.summaryInNewTree', { count: inTree }, '{{count}} in the new tree']);
        else parts.push(['scan.summaryInTree', { count: inTree }, '{{count}} go in the tree']);
        var state = { build: ['scan.summaryNotBuilt', 'not built yet'], building: ['scan.summaryBuilding', 'being built'],
            apply: ['scan.summaryNotApplied', 'not applied yet'], applied: ['scan.summaryApplied', 'applied'] }[stage];
        if (state) parts.push([state[0], null, state[1]]);

        var gap = [];
        if (c.tomeOut > 0) gap.push(['scan.gapNotPlayer', { count: c.tomeOut }, '{{count}} not player spells (no tome, powers, copies)']);
        if (c.excluded > 0) gap.push(['scan.gapExcluded', { count: c.excluded }, '{{count}} on your excluded list']);
        if (c.pluginsOut > 0) gap.push(['scan.gapPlugins', { count: c.pluginsOut }, '{{count}} from plugins turned off']);
        var notPlaced = built ? c.inTree - placed : 0;
        if (notPlaced > 0) gap.push(['scan.gapNotPlaced', { count: notPlaced }, '{{count}} not placed by the builder']);
        var left = c.found - inTree;
        // "72 left out:" leads two or more reasons; one reason that is the whole gap says it alone
        if (!gap.length || left <= 0) gap = [];
        else if (gap.length > 1 || gap[0][1].count !== left) gap.unshift(['scan.gapLeftOut', { count: left }, '{{count}} left out:']);
        return { parts: parts, gap: gap };
    },

    /** The main button for a stage: [key, English, which button it clicks] */
    mainButton: function(stage) {
        switch (stage) {
            case 'scan': return ['buttons.scanSpells', 'Scan Spells', 'scan'];
            case 'scanning': return ['status.scanning', 'Scanning...', null];
            case 'build': case 'building': return ['buttons.buildTree', 'Build Tree', 'build'];
            case 'apply': return ['buttons.applyTree', 'Apply Tree', 'apply'];
            default: return ['scan.stageApplied', 'Tree applied', null];
        }
    },

    /**
     * The plugin list's state: plugins the scan found, and how many of them are turned off.
     * @param {Array} spells
     * @param {Array} whitelist - settings.pluginWhitelist ({plugin, enabled})
     * @returns {{total:number, off:number}}
     */
    pluginState: function(spells, whitelist) {
        var fold = (typeof foldPluginName === 'function') ? foldPluginName : function(s) { return String(s).toLowerCase(); };
        var nameOf = (typeof spellPluginName === 'function') ? spellPluginName : function(s) { return s.plugin || ''; };
        var off = {};
        (whitelist || []).forEach(function(e) { if (e && e.plugin && e.enabled === false) off[fold(e.plugin)] = true; });
        var seen = {}, total = 0, offCount = 0;
        (spells || []).forEach(function(s) {
            var name = nameOf(s);
            if (!name) return;
            var k = fold(name);
            if (seen[k]) return;
            seen[k] = true;
            total++;
            if (off[k]) offCount++;
        });
        return { total: total, off: offCount };
    },

    /** [key, params, English] for the plugin line */
    pluginLine: function(p) {
        if (p.off === 0) return ['scan.pluginsAll', null, 'Plugins: all'];
        if (p.off >= p.total) return ['scan.pluginsNone', null, 'Plugins: none (no spell goes in the tree)'];
        return ['scan.pluginsSome', { on: p.total - p.off, total: p.total }, 'Plugins: {{on}} of {{total}}'];
    },

    // =========================================================================
    // THE PAGE
    // =========================================================================

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;
        var bind = function(id, fn) { var el = self._el(id); if (el) el.addEventListener('click', fn); };
        bind('scanStageBtn', function() { self.onMainClick(); });
        bind('scanAgainBtn', function() { self._click('scanBtn'); });
        bind('scanClearDraftBtn', function() { self._click('tgClearBtn'); });
        bind('scanDetailsToggle', function() { self.toggleDetails(); });
        this.refresh();
    },

    _click: function(id) {
        var btn = this._el(id);
        if (btn && !btn.disabled) btn.click();
    },

    _isComplex: function() {
        var complex = this._el('scannerComplexContent');
        return !!complex && complex.style.display !== 'none';
    },

    /** Easy's Build applies the chosen preset first; Complex builds on the page's settings */
    _buildId: function() { return this._isComplex() ? 'tgBuildBtn' : 'easyBuildBtn'; },

    /** The live inputs of stageOf */
    _inputs: function() {
        var data = (typeof state !== 'undefined' && state) ? state.lastSpellData : null;
        var scanBtn = this._el('scanBtn');
        var growth = (typeof TreeGrowth !== 'undefined') ? TreeGrowth : null;
        return {
            scanning: !!(scanBtn && scanBtn.disabled),
            hasSpells: !!(data && data.spells && data.spells.length > 0),
            building: !!(growth && growth._building),
            built: !!(growth && growth._treeBuilt),
            applied: this._applied
        };
    },

    stage: function() { return this.stageOf(this._inputs()); },

    /** The main button: the real button of its stage, as the player would press it */
    onMainClick: function() {
        var target = this.mainButton(this.stage())[2];
        if (target === 'scan') this._click('scanBtn');
        else if (target === 'build') this._click(this._buildId());
        else if (target === 'apply') this._click('tgApplyBtn');
    },

    /** Apply (or the build at game start) saved the draft */
    markApplied: function() {
        this._applied = true;
        this.refresh();
    },

    /** A new draft, a cleared one, its locks changed, or an Import replaced the tree: what is shown is not applied */
    onDraftChanged: function() {
        this._applied = false;
        this.refresh();
    },

    /** A scan came in: nothing on the page is the applied tree any more (a draft from before it stays, to apply or clear) */
    onScanned: function() {
        this._applied = false;
        this.refresh();
    },

    toggleDetails: function() {
        this._detailsOpen = !this._detailsOpen;
        this._renderDetails();
    },

    _renderDetails: function() {
        var body = this._el('scanResultsBody');
        var toggle = this._el('scanDetailsToggle');
        var arrow = this._el('scanDetailsArrow');
        if (body) body.classList.toggle('hidden', !this._detailsOpen);
        if (toggle) toggle.setAttribute('aria-expanded', this._detailsOpen ? 'true' : 'false');
        if (arrow) arrow.textContent = this._detailsOpen ? '▾' : '▸';
    },

    /** Write a list of [key, params, English] as spans joined by sep */
    _writeParts: function(el, parts, sep) {
        if (!el) return;
        var doc = this._doc();
        el.innerHTML = '';
        for (var i = 0; i < parts.length; i++) {
            if (i > 0) el.appendChild(doc.createTextNode(sep));
            var span = doc.createElement('span');
            setI18n(span, parts[i][0], parts[i][1], parts[i][2]);
            el.appendChild(span);
        }
    },

    /** Everything on the page from the current state; cheap, called on every change */
    refresh: function() {
        var doc = this._doc();
        if (!doc || !this._el('scanStageBtn')) return;
        var inputs = this._inputs();
        var stage = this.stageOf(inputs);
        var data = (typeof state !== 'undefined' && state) ? state.lastSpellData : null;
        var spells = (data && data.spells) || [];
        var c = this.counts(spells);
        var placed = (typeof TreeGrowth !== 'undefined' && TreeGrowth._nodeCount) || 0;
        var tree = (typeof state !== 'undefined' && state && state.treeData && state.treeData.nodes) ? state.treeData.nodes.length : 0;
        var s = this.summary(stage, c, placed, tree);

        this._writeParts(this._el('scanSummary'), s.parts, this.SEPARATOR);
        var gapEl = this._el('scanSummaryGap');
        if (gapEl) {
            // "72 left out:" then the reasons, comma separated (or one reason alone)
            var lead = s.gap.length > 0 && s.gap[0][0] === 'scan.gapLeftOut';
            this._writeParts(gapEl, s.gap.slice(0, 1), '');
            if (lead && s.gap.length > 1) {
                gapEl.appendChild(doc.createTextNode(' '));
                var rest = doc.createElement('span');
                this._writeParts(rest, s.gap.slice(1), ', ');
                gapEl.appendChild(rest);
            }
            gapEl.classList.toggle('hidden', s.gap.length === 0);
        }

        var main = this.mainButton(stage);
        var mainBtn = this._el('scanStageBtn');
        var mainLabel = this._el('scanStageLabel');
        setI18n(mainLabel || mainBtn, main[0], null, main[1]);
        // Its state from the Complex buttons, which Easy's mirror (_syncEasyButtonStates, a MutationObserver
        // that runs after this): Easy's copy may not have caught up yet
        var target = main[2] === 'scan' ? 'scanBtn' : main[2] === 'build' ? 'tgBuildBtn' : main[2] === 'apply' ? 'tgApplyBtn' : null;
        var targetBtn = target ? this._el(target) : null;
        mainBtn.disabled = !targetBtn || !!targetBtn.disabled || stage === 'building';
        mainBtn.setAttribute('data-stage', stage);

        this._show('scanAgainBtn', inputs.hasSpells && !inputs.scanning);
        var clearBtn = this._el('tgClearBtn');
        // While a draft exists (also once applied): clearing it is the way back to Build
        this._show('scanClearDraftBtn', inputs.built && !!clearBtn && !clearBtn.disabled);
        // Nothing to unfold before a scan
        this._show('scanDetailsToggle', inputs.hasSpells);
        if (!inputs.hasSpells) this._detailsOpen = false;

        // The filter lines
        var excluded = (typeof settings !== 'undefined' && settings && settings.spellBlacklist) ? settings.spellBlacklist.length : 0;
        setI18n(this._el('blacklistLabel'), 'scan.excludedCount', { count: excluded }, 'Excluded spells: {{count}}');
        // Plugins with player spells only, as the build counts them (the tome filter comes first)
        var playerSpells = (typeof isTaughtByTome === 'function') ? spells.filter(isTaughtByTome) : spells;
        var plugins = this.pluginLine(this.pluginState(playerSpells, (typeof settings !== 'undefined' && settings) ? settings.pluginWhitelist : null));
        setI18n(this._el('whitelistLabel'), plugins[0], plugins[1], plugins[2]);
        this._renderDetails();
    },

    _show: function(id, on) {
        var el = this._el(id);
        if (el) el.classList.toggle('hidden', !on);
    }
};

window.ScanStage = ScanStage;
