/**
 * TreeNav Module - Spell tree navigation chrome
 *
 * - School jump tabs above the tree (one per school, colored, click = focus root)
 * - Home button (reset rotation / pan / zoom)
 * - Search button (opens the Find Spell modal, same as the F key)
 * - Details panel layout (bottom info bar vs. right sidebar, settings.detailsLayout)
 *
 * Depends on: settings (state.js), CanvasRenderer (canvasRendererV2.js),
 *             TreeCamera (treeCamera.js), openFindSpell (treeViewerUI.js)
 */

var TreeNav = {
    HOME_ZOOM: 0.75,
    _initialized: false,

    init: function() {
        if (this._initialized) return;
        this._initialized = true;

        var self = this;
        var homeBtn = document.getElementById('home-view-btn');
        if (homeBtn) homeBtn.addEventListener('click', function() { self.goHome(); });

        var findBtn = document.getElementById('find-spell-btn');
        if (findBtn) findBtn.addEventListener('click', function() {
            if (typeof openFindSpell === 'function') openFindSpell();
        });

        window.addEventListener('nodeSelected', function(e) {
            self.setActiveSchool(e.detail && e.detail.school);
        });

        this.applyDetailsLayout();
        console.log('[TreeNav] Initialized');
    },

    // =========================================================================
    // HOME
    // =========================================================================

    goHome: function() {
        if (typeof TreeCamera !== 'undefined') {
            TreeCamera.animateTo({ rotation: 0, panX: 0, panY: 0, zoom: this.HOME_ZOOM });
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
            tab.textContent = name;
            tab.title = (typeof t === 'function') ? t('tree.schoolTabsHint') : 'Jump to school';
            tab.style.borderBottomColor = CanvasRenderer._getSchoolColor(name);
            tab.addEventListener('click', function() { self.focusSchool(name); });
            bar.appendChild(tab);
        });

        var current = (typeof state !== 'undefined' && state.selectedNode) ? state.selectedNode.school : null;
        this.setActiveSchool(current);
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
            tabs[i].classList.toggle('active', !!schoolName && tabs[i].getAttribute('data-school') === schoolName);
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
