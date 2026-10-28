/**
 * ClassicZoomFit — fit the Growth canvas to a Classic layout
 *
 * Auto-zooms the Tree Growth canvas so the whole tree is visible with padding:
 * the bounding box of every positioned node, centred, the zoom capped. Called
 * by TreeGrowthClassic._zoomToFit() after a build and a tree load.
 *
 * Usage:
 *   ClassicZoomFit.fit(layoutData);   // layoutData.schools[name].nodes[i].x / .y
 *
 * Depends on: treeGrowth.js (TreeGrowth.zoom, panX, panY, _width, _height)
 */

var ClassicZoomFit = {

    /** Room round the tree: the box is this much bigger than the tree. */
    PADDING: 1.15,

    /** Closest and furthest zoom the fit gives. */
    MAX_ZOOM: 2.0,
    MIN_ZOOM: 0.1,

    /** A tree flatter than 1 unit counts as this wide (or tall). */
    MIN_TREE_SIZE: 100,

    /** Canvas size used while TreeGrowth has not measured its canvas. */
    DEFAULT_CANVAS: 400,

    /**
     * Zoom and pan TreeGrowth so every node of the layout is in view.
     * Nothing happens without TreeGrowth, a layout or a positioned node.
     * @param {Object} layoutData - ClassicLayout's result: { schools: { name: { nodes: [{x, y}] } } }
     */
    fit: function (layoutData) {
        if (!layoutData || !layoutData.schools) return;
        if (typeof TreeGrowth === 'undefined') return;

        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        var schools = layoutData.schools;
        var nodeCount = 0;
        for (var name in schools) {
            if (!schools.hasOwnProperty(name)) continue;
            var nodes = schools[name].nodes || [];
            for (var i = 0; i < nodes.length; i++) {
                if (nodes[i].x < minX) minX = nodes[i].x;
                if (nodes[i].x > maxX) maxX = nodes[i].x;
                if (nodes[i].y < minY) minY = nodes[i].y;
                if (nodes[i].y > maxY) maxY = nodes[i].y;
                nodeCount++;
            }
        }
        if (nodeCount === 0) return;

        var treeW = maxX - minX;
        var treeH = maxY - minY;
        if (treeW < 1) treeW = this.MIN_TREE_SIZE;
        if (treeH < 1) treeH = this.MIN_TREE_SIZE;

        var canvasW = TreeGrowth._width || this.DEFAULT_CANVAS;
        var canvasH = TreeGrowth._height || this.DEFAULT_CANVAS;
        var zoomX = canvasW / (treeW * this.PADDING);
        var zoomY = canvasH / (treeH * this.PADDING);
        var zoom = Math.min(zoomX, zoomY, this.MAX_ZOOM);
        zoom = Math.max(zoom, this.MIN_ZOOM);

        // Center pan on the tree's center
        var centerX = (minX + maxX) / 2;
        var centerY = (minY + maxY) / 2;

        TreeGrowth.zoom = zoom;
        TreeGrowth.panX = -centerX * zoom;
        TreeGrowth.panY = -centerY * zoom;

        console.log('[ClassicGrowth] Zoom to fit: zoom=' + zoom.toFixed(2) +
            ' bounds=' + Math.round(treeW) + 'x' + Math.round(treeH) +
            ' center=' + Math.round(centerX) + ',' + Math.round(centerY));
    }
};
