/**
 * TreeLegend - the node states key behind a small "?" at the footer's right (2.0 review, item 9)
 *
 * The key (Locked, Available, Learning, Unlocked swatches) no longer takes a place in the footer's
 * row: a click on "?" opens it above the button and a second click, or a tab switch, closes it.
 * Opened by a click, never by hovering: Tab closes the panel, so nothing may hide behind hover.
 *
 * Depends on: index.html (#tree-legend-btn, #tree-legend); script.js switchTab calls close().
 */
var TreeLegend = {
    _initialized: false,

    _doc: function() { return document; },

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;
        var btn = this._doc().getElementById('tree-legend-btn');
        if (btn) btn.addEventListener('click', function() { self.toggle(); });
    },

    isOpen: function() {
        var legend = this._doc().getElementById('tree-legend');
        return !!legend && !legend.classList.contains('hidden');
    },

    /** @param {boolean} [open] - left out: the other way round */
    toggle: function(open) {
        var doc = this._doc();
        var legend = doc.getElementById('tree-legend');
        var btn = doc.getElementById('tree-legend-btn');
        if (!legend) return;
        var show = open === undefined ? legend.classList.contains('hidden') : !!open;
        legend.classList.toggle('hidden', !show);
        if (btn) {
            btn.setAttribute('aria-expanded', show ? 'true' : 'false');
            btn.classList.toggle('active', show);
        }
    },

    close: function() { this.toggle(false); }
};

window.TreeLegend = TreeLegend;
