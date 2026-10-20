/**
 * ScanStage: a single staged action above the existing scanner controls.
 * Relays the original buttons, preserving presets, native builds and AutoBuild.
 * Loaded before easyMode.js; initializeEasyMode calls init after its relays bind.
 */
var ScanStage = {
    _initialized: false,
    _applied: false,
    _detailsOpen: false,
    _draftFilters: null,

    text: function(key, params, fallback) {
        return tOr('scanStage.' + key, params || null, fallback);
    },

    stage: function() {
        if (typeof TreeGrowth !== 'undefined' && TreeGrowth._treeBuilt) return 'apply';
        return state.lastSpellData && state.lastSpellData.spells ? 'build' : 'scan';
    },

    counts: function() {
        var all = state.lastSpellData && state.lastSpellData.spells || [];
        var tomes = typeof filterTomeSpells === 'function' ? filterTomeSpells(all) : all;
        var blacklist = typeof filterBlacklistedSpells === 'function' ? filterBlacklistedSpells(tomes) : tomes;
        var eligible = typeof filterWhitelistedSpells === 'function' ? filterWhitelistedSpells(blacklist) : blacklist;
        var disabled = {};
        (settings.pluginWhitelist || []).forEach(function(entry) {
            if (entry.enabled === false && entry.plugin) disabled[foldPluginName(entry.plugin)] = true;
        });
        return {
            found: all.length, eligible: eligible.length,
            tomes: all.length - tomes.length,
            blacklist: tomes.length - blacklist.length,
            plugins: blacklist.length - eligible.length,
            disabledPlugins: Object.keys(disabled).length,
            excludedSpells: (settings.spellBlacklist || []).length
        };
    },

    element: function(tag, id, className) {
        var el = document.createElement(tag);
        if (id) el.id = id;
        if (className) el.className = className;
        return el;
    },

    init: function() {
        var page = document.getElementById('contentSpellScan');
        var actions = page && page.querySelector('.scan-actions-sticky');
        var results = document.getElementById('scanResultsSection');
        if (this._initialized || !actions || !results) return;
        this._initialized = true;
        var self = this;
        page.classList.add('scan-stage-page');
        var overview = this.element('div', 'scanOverview', 'scan-overview');
        var main = this.element('div', 'scanOverviewMain', 'scan-overview-main');
        var preview = this.element('div', 'scanOverviewPreview', 'scan-overview-preview');
        page.insertBefore(overview, actions);
        overview.appendChild(main);
        overview.appendChild(preview);
        var summary = this.element('p', 'scanSummary', 'scan-summary');
        summary.setAttribute('role', 'status');
        main.appendChild(summary);
        var exclusions = this.element('p', 'scanExclusions', 'scan-exclusions');
        main.appendChild(exclusions);
        var built = this.element('p', 'scanBuiltCount', 'scan-exclusions');
        main.appendChild(built);
        main.appendChild(this.element('p', 'scanDraftFilters', 'scan-exclusions'));
        main.appendChild(actions);
        var stage = this.element('button', 'scanStageBtn', 'btn btn-primary');
        stage.addEventListener('click', function() { self.run(); });
        actions.insertBefore(stage, actions.firstChild);
        var scan = document.getElementById('scanBtn');
        if (scan) scan.classList.add('scan-text-action');
        var clear = this.element('button', 'scanClearDraftBtn', 'btn scan-text-action');
        clear.addEventListener('click', function() { self.relay('tgClearBtn'); });
        actions.appendChild(clear);
        main.appendChild(results);
        var status = page.querySelector('.easy-status-wrap');
        if (status) {
            status.classList.add('scan-builder-status');
            main.insertBefore(status, results);
        }

        var stats = results.querySelector('.scan-split-left');
        var toggle = this.element('button', 'scanDetailsToggle', 'scan-row-action');
        toggle.setAttribute('aria-controls', 'scanDetailCounts');
        toggle.addEventListener('click', function() {
            self._detailsOpen = !self._detailsOpen;
            self.refresh();
        });
        stats.parentNode.insertBefore(toggle, stats);
        stats.id = 'scanDetailCounts';
        var mods = this.element('div', 'scanModBreakdown', 'scan-mod-breakdown');
        stats.appendChild(mods);
        var filters = results.querySelector('.scan-split-right');
        var hint = this.element('p', 'scanPluginHint', 'scan-filter-hint');
        filters.appendChild(hint);
        var generation = this.element('button', 'scanGenerationBtn', 'scan-row-action');
        generation.addEventListener('click', function() {
            switchScannerMode('complex');
            var section = document.getElementById('treeGrowthSection');
            if (section && section.style.display !== 'none') section.scrollIntoView(true);
        });
        main.appendChild(generation);
        var presets = page.querySelector('.easy-presets-section');
        if (presets) main.appendChild(presets);
        var placeholder = document.getElementById('easyPreviewPlaceholder');
        if (placeholder) preview.appendChild(placeholder);
        var replay = document.getElementById('easyReplayBtn');
        if (replay) {
            replay.classList.add('scan-text-action');
            preview.appendChild(replay);
        }
        // Buttons remain bound as the single source of truth for AutoBuild and presets.
        // Attribute observation also covers native callbacks without a periodic poll.
        if (typeof MutationObserver !== 'undefined') {
            ['scanBtn', 'tgBuildBtn', 'tgApplyBtn', 'tgClearBtn'].forEach(function(id) {
                var button = document.getElementById(id);
                if (!button) return;
                var observer = new MutationObserver(function() { self.refresh(); });
                observer.observe(button, { attributes: true, attributeFilter: ['disabled'] });
            });
        }
        this.refresh();
    },

    relay: function(id) {
        var button = document.getElementById(id);
        if (button && !button.disabled) button.click();
    },

    run: function() {
        var stage = this.stage();
        var complex = document.getElementById('scannerComplexContent');
        var easy = !complex || complex.style.display === 'none';
        this.relay(stage === 'scan' ? 'scanBtn' :
            stage === 'build' ? (easy ? 'easyBuildBtn' : 'tgBuildBtn') : 'tgApplyBtn');
    },

    markApplied: function() {
        this._applied = true;
        this.refresh();
    },

    filterSignature: function() {
        return getPrimedSpells().map(function(spell) {
            return spell.persistentId || spell.formId || spell.id || '';
        }).sort().join('\n');
    },

    markDraft: function(built) {
        this._applied = false;
        if (built === true) this._draftFilters = this.filterSignature();
        else if (built === false) this._draftFilters = null;
        this.refresh();
    },

    label: function(id, value) {
        var element = document.getElementById(id);
        if (element && element.textContent !== value) element.textContent = value;
    },

    refresh: function() {
        if (!this._initialized) return;
        var count = this.counts();
        var stage = this.stage();
        var source = document.getElementById(stage === 'scan' ? 'scanBtn' : stage === 'build' ? 'tgBuildBtn' : 'tgApplyBtn');
        var button = document.getElementById('scanStageBtn');
        button.disabled = !source || source.disabled;
        this.label('scanStageBtn', t(stage === 'scan' ? 'buttons.scanSpells' :
            stage === 'build' ? 'buttons.buildTree' : 'buttons.applyTree'));
        count.state = this.text(this._applied ? 'applied' : 'pending', null,
            this._applied ? 'applied' : 'not applied yet');
        this.label('scanSummary', stage === 'scan' ? this.text('ready', null, 'Scan to find spells for your tree') :
            this.text('summary', count, '{{found}} spells found · {{eligible}} go in the tree · {{state}}'));
        this.label('scanExclusions', count.found === count.eligible ? '' : this.text('excluded', count,
            'Left out: {{tomes}} unavailable tomes / voice spells / duplicates / spell gate; {{blacklist}} excluded spells; {{plugins}} spells from disabled plugins.'));
        var growth = typeof TreeGrowth !== 'undefined' ? TreeGrowth : {};
        this.label('scanBuiltCount', growth._treeBuilt && growth._nodeCount !== count.eligible ?
            this.text('builtCount', { placed: growth._nodeCount, eligible: count.eligible },
                'Draft: {{placed}} of {{eligible}} eligible spells placed.') : '');
        this.label('scanDraftFilters', growth._treeBuilt && this._draftFilters !== null &&
            this._draftFilters !== this.filterSignature() ? this.text('filtersChanged', null,
                'Filters changed; clear the draft and build again to use them.') : '');
        var scan = document.getElementById('scanBtn');
        scan.style.display = stage === 'scan' ? 'none' : '';
        var scanLabel = scan.querySelector('.hom-label');
        if (scanLabel) {
            scanLabel.removeAttribute('data-i18n-html');
            scanLabel.textContent = this.text('scanAgain', null, 'Scan again');
        }
        var clear = document.getElementById('scanClearDraftBtn');
        var clearSource = document.getElementById('tgClearBtn');
        clear.disabled = !clearSource || clearSource.disabled;
        clear.style.display = stage === 'apply' ? '' : 'none';
        this.label('scanClearDraftBtn', this.text('clearDraft', null, 'Clear draft tree'));
        this.label('scanDetailsToggle', this.text('details', null, 'Scan details') + (this._detailsOpen ? ' ▾' : ' ▸'));
        document.getElementById('scanDetailsToggle').setAttribute('aria-expanded', String(this._detailsOpen));
        document.getElementById('scanDetailCounts').style.display = this._detailsOpen ? '' : 'none';
        this.filterLabel('blacklistBtn', this.text('excludedSpells', { count: count.excludedSpells }, 'Excluded spells: {{count}}') + ' ›');
        this.filterLabel('whitelistBtn', count.disabledPlugins ?
            this.text('pluginsDisabled', { count: count.disabledPlugins }, 'Plugins: {{count}} disabled') + ' ›' :
            this.text('pluginsAll', null, 'Plugins: all') + ' ›');
        this.label('scanPluginHint', this.text('pluginsHint', null, 'Only disabled plugins are excluded. New plugins are included.'));
        this.label('scanGenerationBtn', this.text('generation', null, 'Tree generation') + ' ›');
        var complex = document.getElementById('scannerComplexContent');
        var isComplex = complex && complex.style.display !== 'none';
        document.getElementById('scanOverview').classList.toggle('scan-complex-overview', !!isComplex);
        var replay = document.getElementById('easyReplayBtn');
        var replayParent = document.getElementById(isComplex ? 'prmPreviewWrap' : 'scanOverviewPreview');
        if (replay && replayParent && replay.parentNode !== replayParent) replayParent.appendChild(replay);
        var presets = document.querySelector('#scanOverview .easy-presets-section');
        if (presets) presets.style.display = isComplex || !Object.keys(scannerPresets).length ? 'none' : '';
        if (this._detailsOpen) this.renderMods();
    },

    filterLabel: function(id, text) {
        var button = document.getElementById(id);
        var label = button && button.querySelector('.hom-label');
        if (label) {
            label.removeAttribute('data-i18n-html');
            label.textContent = text;
        }
    },

    renderMods: function() {
        var counts = {};
        var spells = state.lastSpellData && state.lastSpellData.spells || [];
        spells.forEach(function(spell) {
            var plugin = spellPluginName(spell) || schoolText('Unknown');
            counts[plugin] = (counts[plugin] || 0) + 1;
        });
        var html = '';
        Object.keys(counts).sort().forEach(function(plugin) {
            html += '<div class="scan-school-row"><span class="scan-school-name">' +
                escapeHtml(plugin) + '</span><span class="scan-school-count">' + counts[plugin] + '</span></div>';
        });
        document.getElementById('scanModBreakdown').innerHTML = html;
    }
};

if (typeof module !== 'undefined' && module.exports) module.exports = ScanStage;
