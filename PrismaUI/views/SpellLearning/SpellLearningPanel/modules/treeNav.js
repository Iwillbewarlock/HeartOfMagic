/**
 * TreeNav Module - Spell tree navigation chrome
 *
 * - School jump tabs above the tree (one per school, colored, click = focus root)
 * - Home button (reset rotation / pan / zoom)
 * - Search button (shows and hides the Find Spell dropdown, findSpell.js)
 * - Details panel layout (bottom info bar vs. right sidebar, settings.detailsLayout)
 *
 * Depends on: settings (state.js), CanvasRenderer (canvasRendererV2.js),
 *             TreeCamera (treeCamera.js), toggleFindSpell (findSpell.js)
 */

var TreeNav = {
    HOME_ZOOM: 0.75,
    // The home view never opens further out than this: a tree too big to fit above it opens here centred on the
    // heart. 0.5 is CanvasRenderer.LABEL_MIN_ZOOM (no names below it) and inside the full level of detail
    // (_computeLODTier: 0.45 and up; under 0.25 the spokes go and spells are dots)
    HOME_MIN_ZOOM: 0.5,
    _initialized: false,

    init: function() {
        if (this._initialized) return;
        this._initialized = true;

        var self = this;
        var homeBtn = document.getElementById('home-view-btn');
        if (homeBtn) homeBtn.addEventListener('click', function() { self.goHome(); });

        var findBtn = document.getElementById('find-spell-btn');
        if (findBtn) findBtn.addEventListener('click', function() {
            if (typeof toggleFindSpell === 'function') toggleFindSpell();
        });

        window.addEventListener('nodeSelected', function(e) {
            self.setActiveSchool(e.detail && e.detail.school);
        });

        this.applyDetailsLayout();
        console.log('[TreeNav] Initialized');
    },

    /**
     * Put a popup (absolutely placed in the tree) just under a tool bar button,
     * kept inside the tree: the bar's height changes when it wraps on a narrow
     * panel. Used by Find Spell and the render settings popup.
     */
    placeUnder: function(panel, btn) {
        var host = panel && (panel.offsetParent || panel.parentElement);
        if (!btn || !host || !btn.getBoundingClientRect || !host.getBoundingClientRect) return;
        var b = btn.getBoundingClientRect();
        var h = host.getBoundingClientRect();
        // Below the whole bar, not only the button: a wrapped bar has a row under it
        var bar = typeof btn.closest === 'function' ? btn.closest('.zoom-controls') : null;
        var bottom = bar ? bar.getBoundingClientRect().bottom : b.bottom;
        var width = panel.offsetWidth || 0;
        panel.style.left = Math.max(8, Math.min(b.left - h.left, h.width - width - 8)) + 'px';
        panel.style.top = (bottom - h.top + 6) + 'px';
    },

    // =========================================================================
    // HOME
    // =========================================================================

    /** The whole tree in the part of the canvas no panel covers (TreeCamera.homeTarget) */
    goHome: function() {
        if (typeof TreeCamera !== 'undefined') {
            var renderer = TreeCamera._getRenderer();
            var target = renderer ? TreeCamera.homeTarget(renderer, this.HOME_ZOOM, this.HOME_MIN_ZOOM) : null;
            TreeCamera.animateTo(target || { rotation: 0, panX: 0, panY: 0, zoom: this.HOME_ZOOM });
        } else if (typeof SmartRenderer !== 'undefined') {
            SmartRenderer.centerView();
        }
    },

    // =========================================================================
    // SCHOOL TABS
    // =========================================================================

    /**
     * Rebuild the school tabs from the renderer's school map.
     * Tabs are ordered clockwise starting from the top of the wheel.
     */
    buildSchoolTabs: function() {
        var bar = document.getElementById('tree-school-tabs');
        if (!bar) return;
        bar.innerHTML = '';

        if (typeof CanvasRenderer === 'undefined' || !CanvasRenderer.schools) return;
        var schools = CanvasRenderer.schools;
        var names = Object.keys(schools).filter(function(name) {
            return !settings.schoolVisibility || settings.schoolVisibility[name] !== false;
        });
        if (names.length === 0) return;

        function clockwiseFromTop(name) {
            var a = schools[name].spokeAngle;
            if (typeof a !== 'number' || isNaN(a)) return 999;
            return ((a + 90) % 360 + 360) % 360;
        }
        names.sort(function(a, b) { return clockwiseFromTop(a) - clockwiseFromTop(b); });

        var self = this;
        names.forEach(function(name) {
            var tab = document.createElement('button');
            tab.className = 'school-tab';
            tab.setAttribute('data-school', name);
            var full = self._schoolLabel(name);
            tab.textContent = self._schoolShort(name, full);
            tab.setAttribute('aria-label', full);
            tab.title = full + ' - ' + ((typeof t === 'function') ? t('tree.schoolTabsHint') : 'Jump to school');
            tab.style.borderBottomColor = CanvasRenderer._getSchoolColor(name);
            tab.addEventListener('click', function() { self.focusSchool(name); });
            bar.appendChild(tab);
        });

        var current = (typeof state !== 'undefined' && state.selectedNode) ? state.selectedNode.school : null;
        this.setActiveSchool(current);
    },

    /** The school's name in the page's language (chips.school.*), else as the tree has it. */
    _schoolLabel: function(name) {
        var key = 'chips.school.' + String(name).toLowerCase();
        var v = (typeof t === 'function') ? t(key) : key;
        return (v && v !== key) ? v : name;
    },

    /**
     * A short label for the tab, about half the width of the full name: the
     * language's own short form (tree.schoolShort.*) when it has one, else the
     * name cut to five letters. The full name stays in the tooltip.
     */
    _schoolShort: function(name, full) {
        var key = 'tree.schoolShort.' + String(name).toLowerCase();
        var v = (typeof t === 'function') ? t(key) : key;
        if (v && v !== key) return v;
        return full.length > 6 ? full.substring(0, 5) + '.' : full;
    },

    /**
     * The tabs again in the panel's language after a switch (languageSetting.js),
     * when there are any: their names and tooltips are written by script.
     */
    relabelSchoolTabs: function() {
        var bar = document.getElementById('tree-school-tabs');
        if (bar && bar.children && bar.children.length) this.buildSchoolTabs();
    },

    clearSchoolTabs: function() {
        var bar = document.getElementById('tree-school-tabs');
        if (bar) bar.innerHTML = '';
    },

    setActiveSchool: function(schoolName) {
        var bar = document.getElementById('tree-school-tabs');
        if (!bar) return;
        var tabs = bar.querySelectorAll('.school-tab');
        for (var i = 0; i < tabs.length; i++) {
            var active = !!schoolName && tabs[i].getAttribute('data-school') === schoolName;
            tabs[i].classList.toggle('active', active);
            tabs[i].setAttribute('aria-pressed', active ? 'true' : 'false');
        }
    },

    /**
     * Select and center the root node of a school (falls back to its first node).
     */
    focusSchool: function(schoolName) {
        if (typeof CanvasRenderer === 'undefined' || !CanvasRenderer._nodeMap) return;
        var school = CanvasRenderer.schools[schoolName];
        if (!school) return;

        var root = school.root ? CanvasRenderer._nodeMap.get(school.root) : null;
        if (!root) {
            for (var i = 0; i < CanvasRenderer.nodes.length; i++) {
                var n = CanvasRenderer.nodes[i];
                if (n.school === schoolName && (n.isRoot || !root)) { root = n; if (n.isRoot) break; }
            }
        }
        if (!root) return;

        CanvasRenderer.selectNodeAndFocus(root);
    },

    // =========================================================================
    // DETAILS PANEL LAYOUT
    // =========================================================================

    /**
     * Apply settings.detailsLayout ('bottom' | 'side') to the DOM.
     * The same #details-panel element is used; CSS in patch-ui.css restyles it.
     */
    applyDetailsLayout: function() {
        var page = document.getElementById('contentSpellTree');
        if (!page) return;
        var bottom = !settings || settings.detailsLayout !== 'side';
        page.classList.toggle('details-bottom', bottom);
        page.classList.toggle('details-side', !bottom);

        // The focus offset depends on where the panel sits
        if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
    },

    isBottomLayout: function() {
        return !settings || settings.detailsLayout !== 'side';
    }
};

window.TreeNav = TreeNav;
TreeNav.init();
