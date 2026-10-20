/**
 * TreeToolsScale - the size of the tree's tool bar (zoom, home, Find Spell,
 * render settings, edit), set by dragging the grip at its bottom-right corner.
 *
 * The bar sits at the tree's top left, so the grip is on the corner away from
 * that anchor: dragging it right/down makes the bar bigger, left/up smaller,
 * between 1x and 3x (default 1x). Double-clicking the grip puts it back to 1x.
 * The size is a transform on the bar (buttons, icons and text together); the
 * rows under it (school tabs, trait filter, in .tree-top-tools) are pushed down
 * by a margin of the same amount, and a bar too wide for the tree wraps.
 * The popups under the bar (Find Spell, render settings) are placed under its
 * scaled bottom (TreeNav.placeUnder).
 *
 * Saved as settings.treeToolsScale in the panel config; Reset to Defaults sets 1.
 * A design whose page has corner drawings moves the tools right past the top-left one,
 * at their usual top, and keeps them as far from the right edge (setCornerArt, called by
 * TreeStyle.renderPage with TreeStyle.hasCornerArt; CanvasRenderer.hide puts them back).
 * The grip takes its own mouse presses: the tree does not pan from them. A
 * release the page never hears (a move with no button held, the page losing
 * focus) ends the drag too, so the bar does not keep following the pointer.
 *
 * Depends on: settings, autoSaveSettings (optional)
 */

var TreeToolsScale = {

    MIN: 1,
    MAX: 3,
    DEFAULT: 1,
    // How far a drag must move for one step of the scale: a step of 0.05
    STEP: 0.05,

    _drag: null,
    EDGE_PX: 14,      // .tree-top-tools' top and left in patch-tree-tools.css
    // Beside corner drawings: the tools' left, and their room kept from the right edge. TreeStyle draws the
    // corners at most ORNAMENT_MAX (112) px; 16 px more keeps the tools clear of them (the GPT comparison build's
    // margin). The top stays EDGE_PX: past the drawing's width nothing is under them.
    CORNER_ART_PX: 128,
    _corners: false,

    /**
     * Beside a page's corner drawings (true) or at the stylesheet's own place. Called every frame: only a
     * change writes to the page; the bar's room is worked out again.
     */
    setCornerArt: function(on) {
        on = !!on;
        if (on === this._corners) return;
        this._corners = on;
        var tools = document.querySelector ? document.querySelector('.tree-top-tools') : null;
        if (tools && tools.style) {
            tools.style.left = on ? this.CORNER_ART_PX + 'px' : '';
            tools.style.maxWidth = on ? 'calc(100% - ' + (2 * this.CORNER_ART_PX) + 'px)' : '';
        }
        this.apply();
    },

    /** A saved or dragged value as a scale: a number in [MIN, MAX] on STEP, else DEFAULT */
    clamp: function(value) {
        var v = Number(value);
        if (typeof value === 'boolean' || value === null || value === '' || !isFinite(v)) return this.DEFAULT;
        v = Math.max(this.MIN, Math.min(this.MAX, v));
        return Math.round(Math.round(v / this.STEP) * this.STEP * 100) / 100;
    },

    /** The scale a drag gives: the bar grows by as much as the pointer moved, as a share of its size */
    scaleForDrag: function(startScale, startWidth, startHeight, dx, dy) {
        var w = Math.max(1, startWidth), h = Math.max(1, startHeight);
        var ratio = ((w + dx) / w + (h + dy) / h) / 2;
        return this.clamp(startScale * ratio);
    },

    current: function() {
        return this.clamp(typeof settings !== 'undefined' && settings ? settings.treeToolsScale : this.DEFAULT);
    },

    init: function() {
        var self = this;
        var grip = document.getElementById('tree-tools-grip');
        if (grip) {
            grip.addEventListener('mousedown', function(e) { self._startDrag(e); });
            grip.addEventListener('dblclick', function(e) {
                e.preventDefault();
                e.stopPropagation();
                self.set(self.DEFAULT, true);
            });
            // A click on the grip is not a click on the tree under it
            grip.addEventListener('click', function(e) { e.stopPropagation(); });
        }
        window.addEventListener('resize', function() { self.apply(); });
        this.apply();
    },

    /** Change the scale; save when asked (a finished drag, a double-click) */
    set: function(value, save) {
        if (typeof settings !== 'undefined' && settings) settings.treeToolsScale = this.clamp(value);
        this.apply();
        if (save && typeof autoSaveSettings === 'function') autoSaveSettings();
    },

    /** Put the scale on the bar and make room for it under and beside it */
    apply: function() {
        var bar = document.querySelector ? document.querySelector('.tree-top-tools .zoom-controls') : null;
        if (!bar || !bar.style) return;
        var s = this.current();
        var host = bar.parentElement && bar.parentElement.parentElement;
        // The bar's unscaled width: what the tree has room for at this scale
        var side = this._corners ? this.CORNER_ART_PX : this.EDGE_PX;
        var room = host && host.clientWidth ? Math.floor((host.clientWidth - 2 * side) / s) : 0;
        bar.style.maxWidth = room > 0 ? room + 'px' : '';
        bar.style.transformOrigin = 'top left';
        bar.style.transform = s === 1 ? '' : 'scale(' + s + ')';
        // A transform takes no room: push the rows under it down by what it grew
        bar.style.marginBottom = s === 1 ? '' : Math.round(bar.offsetHeight * (s - 1)) + 'px';
        bar.style.marginRight = s === 1 ? '' : Math.round(bar.offsetWidth * (s - 1)) + 'px';
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer) CanvasRenderer._needsRender = true;
    },

    loadFrom: function(data) {
        if (typeof settings !== 'undefined' && settings) settings.treeToolsScale = this.clamp(data ? data.treeToolsScale : undefined);
        this.apply();
    },

    saveTo: function(config) {
        config.treeToolsScale = this.current();
        return config;
    },

    reset: function() {
        this.set(this.DEFAULT, false);
    },

    _startDrag: function(e) {
        if (e.button !== undefined && e.button !== 0) return;
        // The tree must not pan or select from this press
        e.preventDefault();
        e.stopPropagation();
        var bar = document.querySelector('.tree-top-tools .zoom-controls');
        if (!bar) return;
        if (this._drag) this._endDrag(null);   // a drag whose end never came
        var self = this;
        this._drag = {
            x: e.clientX, y: e.clientY,
            scale: this.current(),
            // Does this engine say which buttons are down? Ultralight may send 0
            // on every event, so a move with buttons 0 means a lost release only
            // when the press itself reported its button (as canvasRendererInput)
            reported: typeof e.buttons === 'number' && e.buttons > 0,
            width: bar.offsetWidth * this.current(),
            height: bar.offsetHeight * this.current(),
            move: function(ev) { self._onDragMove(ev); },
            up: function(ev) { self._endDrag(ev); },
            blur: function() { self._endDrag(null); }
        };
        window.addEventListener('mousemove', this._drag.move, true);
        window.addEventListener('mouseup', this._drag.up, true);
        window.addEventListener('blur', this._drag.blur);
    },

    /**
     * The pointer moved during a drag. No button held means the release went
     * somewhere this page did not hear it (outside the view, the game took the
     * mouse): the drag ends here instead of following the pointer on. Only
     * when the press reported its button: an engine that always sends 0 would
     * otherwise end every drag on its first move.
     */
    _onDragMove: function(ev) {
        var d = this._drag;
        if (!d) return;
        if (d.reported && ev.buttons === 0) {
            this._endDrag(null);
            return;
        }
        ev.preventDefault();
        this.set(this.scaleForDrag(d.scale, d.width, d.height, ev.clientX - d.x, ev.clientY - d.y), false);
    },

    /** End the drag (released, lost release, or the page lost focus) and save the size */
    _endDrag: function(ev) {
        var d = this._drag;
        if (!d) return;
        window.removeEventListener('mousemove', d.move, true);
        window.removeEventListener('mouseup', d.up, true);
        window.removeEventListener('blur', d.blur);
        this._drag = null;
        if (ev && ev.stopPropagation) ev.stopPropagation();
        this.set(this.current(), true);
    }
};

window.TreeToolsScale = TreeToolsScale;
