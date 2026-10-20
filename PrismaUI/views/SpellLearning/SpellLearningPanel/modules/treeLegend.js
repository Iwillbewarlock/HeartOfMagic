/**
 * TreeLegend - click-operated state key. Never owns or hides the status line:
 * TreeStatusLine retains its success timeout and persistent warning/error rules.
 * Dependencies: index.html's tree-legend / tree-legend-btn.
 */
var TreeLegend = {
    init: function() {
        var button = document.getElementById('tree-legend-btn');
        var legend = document.getElementById('tree-legend');
        if (!button || !legend || button._treeLegendBound) return;
        button._treeLegendBound = true;
        this.setOpen(false);
        var self = this;
        button.addEventListener('click', function() { self.setOpen(legend.hidden); });
    },

    setOpen: function(open) {
        var button = document.getElementById('tree-legend-btn');
        var legend = document.getElementById('tree-legend');
        if (!button || !legend) return;
        legend.hidden = !open;
        button.setAttribute('aria-expanded', open ? 'true' : 'false');
    }
};

window.TreeLegend = TreeLegend;
TreeLegend.init();
