/**
 * DetailsPeek - the spell card follows the cursor
 *
 * Resting the cursor on a spell shows its card in the details panel; a click
 * still selects. The two are kept apart because selecting has consequences
 * (Learn/Unlock aim at the selected spell, locks get revealed, C++ is told),
 * so a preview only ever calls renderSpellCard, never showSpellDetails.
 *
 *  - the card appears after the cursor has rested DWELL_MS on a spell, and
 *    goes back to the selected spell GRACE_MS after it left (moving onto the
 *    panel itself keeps the preview, so its links can be pressed)
 *  - while previewing, Learn/Unlock cannot be pressed and the card says so
 *  - with the preview on the panel never closes: with nothing selected it
 *    shows a hint instead, so the bar under the tree cannot suddenly cover
 *    the spell the cursor is on (the canvas gets no mousemove once it does)
 *  - the close button, a click on nothing, or Esc drop the selection. Esc is
 *    two-step (script.js): the first press drops it, the next closes the panel
 *
 * Depends on: settings, state (state.js), renderSpellCard /
 *             clearSpellSelection (treeViewerUI.js), CanvasRenderer hover
 *             (canvasRendererV2.js calls DetailsPeek.hover), EditMode
 */
var DetailsPeek = {
    DWELL_MS: 150,     // how long the cursor rests on a spell before its card shows
    GRACE_MS: 250,     // how long after the cursor leaves before the selected card returns
    SLOW_CARD_MS: 20,  // a preview card built slower than this is logged (developer mode)

    peekNode: null,    // the spell being previewed, null when the card shows the selection
    _dwellTimer: null,
    _graceTimer: null,
    _initialized: false,

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;

        var panel = document.getElementById('details-panel');
        if (panel) {
            // Reading the previewed card, or pressing a link on it, is not leaving it
            panel.addEventListener('mouseenter', function() { self._clearGrace(); });
            panel.addEventListener('mouseleave', function() {
                if (self.peekNode) self._startGrace();
            });
        }

        console.log('[DetailsPeek] Initialized');
    },

    /** Is the preview on? Off in edit mode: there the card belongs to editing. */
    enabled: function() {
        if (typeof settings === 'undefined' || settings.detailsOnHover === false) return false;
        if (typeof EditMode !== 'undefined' && EditMode.isActive) return false;
        return true;
    },

    isPeeking: function() {
        return !!this.peekNode;
    },

    // =========================================================================
    // HOVER (called by CanvasRenderer whenever the spell under the cursor changes)
    // =========================================================================

    /**
     * @param {Object|null} node - the spell under the cursor, null for none
     */
    hover: function(node) {
        // Turned off mid-preview (edit mode switched on): give the card back
        if (!this.enabled()) {
            if (this.peekNode) this.reset();
            return;
        }
        // Typing an XP value on the card: a preview would redraw the inputs
        // under the cursor and bind them to another spell
        if (this._typingInPanel()) return;
        var self = this;
        this._clearDwell();

        if (!node) {
            if (this.peekNode) this._startGrace();
            return;
        }

        this._clearGrace();
        // The selected spell is already on the card
        if (state.selectedNode && this._sameSpell(node, state.selectedNode)) {
            if (this.peekNode) this._restore();
            return;
        }
        if (this.peekNode && this._sameSpell(node, this.peekNode)) return;

        this._dwellTimer = setTimeout(function() {
            self._dwellTimer = null;
            self._show(node);
        }, this.DWELL_MS);
    },

    /** A spell was selected (or the selection dropped): no preview is pending */
    onSelected: function(node) {
        this._clearDwell();
        this._clearGrace();
        this.peekNode = null;
        var panel = document.getElementById('details-panel');
        if (panel) panel.classList.remove('peeking');
    },

    /**
     * Forget the preview and show the selected card (or the empty panel).
     * The panel is hiding, edit mode came on, or the setting changed.
     */
    reset: function() {
        this.onSelected(null);
        this.applyLayout();
    },

    /** Redraw whatever the card shows now (progress arrived from C++ ...) */
    refresh: function() {
        if (this.peekNode) {
            this._show(this.peekNode);
        } else if (state.selectedNode) {
            renderSpellCard(state.selectedNode, { preview: false });
        } else {
            this.applyLayout();
        }
    },

    // =========================================================================
    // PANEL VISIBILITY
    // =========================================================================

    /**
     * Preview on: the panel is always open, empty when nothing is selected.
     * Preview off: it opens on a click and closes with the selection, as before.
     * Called when the setting changes, a tree loads, or the selection drops.
     */
    applyLayout: function() {
        var panel = document.getElementById('details-panel');
        var page = document.getElementById('contentSpellTree');
        if (!panel) return;

        var hasTree = typeof state !== 'undefined' && !!state.treeData;
        var showSelected = !!state.selectedNode;
        var keepOpen = this.enabled() && hasTree;

        // Whatever was being previewed, the layout decides afresh (a stale
        // peekNode would also stop the selected card's buttons from updating)
        this._clearDwell();
        this._clearGrace();
        this.peekNode = null;
        panel.classList.remove('peeking');

        if (showSelected) {
            renderSpellCard(state.selectedNode, { preview: false });
            return;
        }
        panel.classList.toggle('is-empty', keepOpen);
        panel.classList.toggle('hidden', !keepOpen);
        if (page) page.classList.toggle('details-open', keepOpen);
        // A frame for the new layout; nothing the tree layer draws changed
        if (typeof CanvasRenderer !== 'undefined') CanvasRenderer.__needsRender = true;
    },

    // =========================================================================
    // INTERNAL
    // =========================================================================

    _show: function(node) {
        this.peekNode = node;
        // The card is DOM the game's browser lays out and paints: a slow one
        // holds every click behind it (developer mode logs it)
        var t0 = (typeof performance !== 'undefined') ? performance.now() : 0;
        renderSpellCard(node, { preview: true });
        if (typeof PerfMeter !== 'undefined' && t0) {
            var cardMs = Math.round(performance.now() - t0);
            if (cardMs >= this.SLOW_CARD_MS) PerfMeter.input('card preview built in ' + cardMs + ' ms');
        }
    },

    _restore: function() {
        this.peekNode = null;
        if (state.selectedNode) {
            renderSpellCard(state.selectedNode, { preview: false });
        } else {
            this.applyLayout();
        }
    },

    _startGrace: function() {
        var self = this;
        this._clearGrace();
        this._graceTimer = setTimeout(function() {
            self._graceTimer = null;
            self._restore();
        }, this.GRACE_MS);
    },

    _clearGrace: function() {
        if (this._graceTimer) { clearTimeout(this._graceTimer); this._graceTimer = null; }
    },

    _clearDwell: function() {
        if (this._dwellTimer) { clearTimeout(this._dwellTimer); this._dwellTimer = null; }
    },

    _typingInPanel: function() {
        var el = document.activeElement;
        if (!el) return false;
        var tag = (el.tagName || '').toLowerCase();
        if (tag !== 'input' && tag !== 'textarea' && tag !== 'select') return false;
        var panel = document.getElementById('details-panel');
        return !!panel && panel.contains(el);
    },

    _sameSpell: function(a, b) {
        if (!a || !b) return false;
        return a === b || a.id === b.id || (a.formId && a.formId === b.formId);
    }
};

window.DetailsPeek = DetailsPeek;
DetailsPeek.init();
