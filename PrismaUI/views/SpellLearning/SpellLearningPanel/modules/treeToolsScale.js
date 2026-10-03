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
 * The grip takes its own mouse presses: the tree does not pan from them.
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
        var room = host && host.clientWidth ? Math.floor((host.clientWidth - 28) / s) : 0;
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
        var self = this;
        this._drag = {
            x: e.clientX, y: e.clientY,
            scale: this.current(),
            width: bar.offsetWidth * this.current(),
            height: bar.offsetHeight * this.current()
        };
        var move = function(ev) {
            if (!self._drag) return;
            ev.preventDefault();
            var d = self._drag;
            self.set(self.scaleForDrag(d.scale, d.width, d.height, ev.clientX - d.x, ev.clientY - d.y), false);
        };
        var up = function(ev) {
            window.removeEventListener('mousemove', move, true);
            window.removeEventListener('mouseup', up, true);
            if (!self._drag) return;
            ev.stopPropagation();
            self._drag = null;
            self.set(self.current(), true);
        };
        window.addEventListener('mousemove', move, true);
        window.addEventListener('mouseup', up, true);
    }
};

window.TreeToolsScale = TreeToolsScale;
