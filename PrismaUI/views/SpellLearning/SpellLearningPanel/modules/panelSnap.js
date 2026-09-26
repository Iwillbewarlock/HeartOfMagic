/**
 * PanelSnap - keep the panel on whole pixels.
 *
 * The panel is centred with left/top 50% and translate(-50%, -50%), and sized
 * in px capped by vw/vh: with an odd size or an odd screen it lands half a
 * pixel off, and a drag placed it wherever the mouse left it. The game's
 * browser paints on the CPU; everything in a panel on a fractional position -
 * the tree canvas above all - is resampled each time it is painted, instead
 * of copied. apply() nudges the panel with a margin of under a pixel so its
 * corner is on a whole pixel; the drag (script.js) rounds its positions.
 *
 * Depends on: nothing (#spellPanel)
 */

var PanelSnap = {

    apply: function() {
        var panel = document.getElementById('spellPanel');
        if (!panel) return;
        panel.style.marginLeft = '';
        panel.style.marginTop = '';
        var r = panel.getBoundingClientRect();
        var dx = Math.round(r.left) - r.left, dy = Math.round(r.top) - r.top;
        if (Math.abs(dx) > 0.01) panel.style.marginLeft = dx.toFixed(3) + 'px';
        if (Math.abs(dy) > 0.01) panel.style.marginTop = dy.toFixed(3) + 'px';
    },

    LATE_MS: [300, 1500],  // again after these: the design's CSS is added after load and can resize the panel

    init: function() {
        var self = this;
        var again = function() { self.apply(); };
        window.addEventListener('resize', again);
        // The panel's own size changes (design CSS, vh caps) do not fire resize
        var panel = document.getElementById('spellPanel');
        if (panel && typeof ResizeObserver !== 'undefined') {
            try { new ResizeObserver(again).observe(panel); } catch (e) { /* timers below */ }
        }
        for (var i = 0; i < this.LATE_MS.length; i++) setTimeout(again, this.LATE_MS[i]);
        this.apply();
    }
};

window.PanelSnap = PanelSnap;
