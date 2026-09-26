/**
 * HoverOverlay - what the spell under the cursor adds to the tree, painted over
 * the pasted tree layer instead of into it.
 *
 * The hover preview (the path down to the root in the school's colour, the
 * brighter nodes on it, the hovered spell's focus ring, its bridges) used to be
 * drawn into the tree layer. Every time the cursor crossed onto another spell
 * the layer was thrown away and all ~1,400 spells repainted - about 3,300 paint
 * calls for one mouse move, in a browser that draws on the CPU. Now the layer
 * is drawn with the hover state hidden (withoutHover) and kept.
 *
 * The preview itself is cached too (composite): the layer copied once and the
 * preview painted onto the copy, only when the hovered spell or the layer
 * changes. A root's preview reaches its whole subtree (hundreds of spells), so
 * painting it on every animation frame while the cursor rests would cost more
 * than it saves; pasted from the copy, a frame costs what it did without hover.
 *
 * With the moving parts on their own canvases (FxLayer), the preview gets one
 * too (drawSpot): a spot over the box the preview covers, under the other
 * spots, drawn again only when the hover or the view changes. The tree canvas
 * is then not touched at all when the cursor moves onto another spell - with
 * the copy, each hover change repainted the whole canvas (twice: once pasted,
 * once more for its kept picture, StaticBase), in a view the game's browser
 * paints on the CPU.
 *
 * Differences from before, all small: the hover path and nodes sit on top of
 * the labels rather than under them, and the hovered spell's label no longer
 * wins label collisions (the hover card shows its name anyway). At the simple
 * level of detail (zoomed out) only the hovered spell is redrawn, so with a
 * spell selected the rest of its path stays dimmed there.
 *
 * Depends on: CanvasRenderer internals (_nodeMap, edges, _drawEdgePath,
 *             renderNode, _renderNodeSimple, _getSchoolColor, _learningColor),
 *             TreeStyle.tokens, BridgeView (optional), settings
 */

var HoverOverlay = {

    /** Size of the dot the minimal level of detail marks a spell with (renderNodesMinimal). */
    MINIMAL_DOT: 10,
    MINIMAL_CORE: 6,

    /**
     * Run fn with the renderer's hover state hidden, so whatever fn draws (the
     * tree layer) does not depend on the cursor.
     */
    withoutHover: function(r, fn) {
        var hovered = r.hoveredNode, edges = r._hoverPathEdges, nodes = r._hoverPathNodes;
        r.hoveredNode = null;
        r._hoverPathEdges = null;
        r._hoverPathNodes = null;
        try {
            fn();
        } finally {
            r.hoveredNode = hovered;
            r._hoverPathEdges = edges;
            r._hoverPathNodes = nodes;
        }
    },

    _hidden: function(r, node) {
        if (!node) return true;
        if (typeof BridgeView !== 'undefined' && BridgeView.isHidden) return BridgeView.isHidden(r, node);
        return !!(settings.schoolVisibility && settings.schoolVisibility[node.school] === false);
    },

    _canvas: null,
    _ctx: null,
    _key: '',

    /**
     * The canvas to paste this frame: the tree layer itself when nothing is
     * hovered, else a copy of it with the hover preview painted on, kept until
     * the hovered spell or the layer changes.
     * @param {Object} r - CanvasRenderer
     * @param {HTMLCanvasElement} layer - the tree layer (margin included)
     * @param {number} dpr
     * @param {number} margin - the layer's margin, CSS pixels
     * @param {Object} view - { cx, cy, rotRad }
     * @returns {HTMLCanvasElement}
     */
    composite: function(r, layer, dpr, margin, view) {
        var hovered = r.hoveredNode;
        if (!hovered || !r._nodeMap || this._hidden(r, hovered)) return layer;
        var key = hovered.id + '|' + (r._treeLayerDraws || 0) + '|' + r._lodTier + '|' + (r.selectedNode ? r.selectedNode.id : '');
        if (key === this._key && this._canvas && this._canvas.width === layer.width && this._canvas.height === layer.height) {
            return this._canvas;
        }
        try {
            if (!this._canvas) {
                this._canvas = document.createElement('canvas');
                this._ctx = this._canvas.getContext('2d');
                if (!this._ctx) throw new Error('no 2d context');
            }
            if (this._canvas.width !== layer.width || this._canvas.height !== layer.height) {
                this._canvas.width = layer.width;
                this._canvas.height = layer.height;
            }
            var c = this._ctx;
            c.setTransform(1, 0, 0, 1, 0, 0);
            c.globalAlpha = 1.0;
            c.globalCompositeOperation = 'source-over';
            c.clearRect(0, 0, this._canvas.width, this._canvas.height);
            c.drawImage(layer, 0, 0);
            // The layer's own space: DPR, margin, and the pan it was drawn at
            c.scale(dpr, dpr);
            c.translate(margin, margin);
            this.render(c, r, view, r._layerPanX, r._layerPanY);
            this._key = key;
            return this._canvas;
        } catch (e) {
            this._key = '';
            return layer;
        }
    },

    SPOT_PAD: 40,          // world units round the preview's spells (glow, ring, focus): its spot's margin
    SPOT_PAD_PX: 8,        // and screen px on top

    /**
     * The preview on its own FxLayer spot (see the header); nothing hovered:
     * no spot drawn, and FxLayer.end hides it. Call once per frame, with the
     * FxLayer frame open.
     */
    drawSpot: function(r, view, dpr) {
        var hovered = r.hoveredNode;
        if (!hovered || !r._nodeMap || this._hidden(r, hovered)) return;
        var box = this._spotBox(r, hovered, view);
        var version = hovered.id + '|' + (r._treeLayerDraws || 0) + '|' + r._lodTier + '|' +
            (r.selectedNode ? r.selectedNode.id : '') + '|' + r.panX + ',' + r.panY + '|' + r.zoom + '|' + r.rotation;
        var self = this;
        FxLayer.draw('hover', box[0], box[1], box[2] - box[0], box[3] - box[1], dpr,
            function(ctx) { self.render(ctx, r, view); }, { version: version, under: true });
    },

    /** The screen box (CSS px) the preview covers: its spells, and the ends of the hovered one's bridges. */
    _spotBox: function(r, hovered, view) {
        var pts = [hovered];
        if (r._hoverPathNodes) {
            r._hoverPathNodes.forEach(function(id) {
                var n = r._nodeMap.get(id);
                if (n) pts.push(n);
            });
        }
        if (typeof BridgeView !== 'undefined' && BridgeView.forNode && hovered !== r.selectedNode) {
            var list = BridgeView.forNode(hovered);
            for (var b = 0; b < list.length; b++) {
                var other = r._nodeMap.get(list[b].other);
                if (other) pts.push(other);
            }
        }
        var x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (var i = 0; i < pts.length; i++) {
            var p = r._worldToScreen(pts[i].x, pts[i].y, view);
            if (p[0] < x0) x0 = p[0];
            if (p[0] > x1) x1 = p[0];
            if (p[1] < y0) y0 = p[1];
            if (p[1] > y1) y1 = p[1];
        }
        var pad = this.SPOT_PAD * r.zoom + this.SPOT_PAD_PX;
        return [x0 - pad, y0 - pad, x1 + pad, y1 + pad];
    },

    /**
     * Paint the hover preview in the tree's own space.
     * @param {CanvasRenderingContext2D} ctx - DPR-scaled
     * @param {Object} r - CanvasRenderer
     * @param {Object} view - { cx, cy, rotRad }
     * @param {number} [panX] - the pan to draw at (the layer's; default the current one)
     * @param {number} [panY]
     */
    render: function(ctx, r, view, panX, panY) {
        var hovered = r.hoveredNode;
        if (!hovered || !r._nodeMap || this._hidden(r, hovered)) return;
        if (typeof panX !== 'number') panX = r.panX;
        if (typeof panY !== 'number') panY = r.panY;

        ctx.save();
        ctx.translate(view.cx + panX, view.cy + panY);
        ctx.rotate(view.rotRad);
        ctx.scale(r.zoom, r.zoom);

        this._renderPathEdges(ctx, r, hovered);
        ctx.globalAlpha = 1.0;
        this._renderNodes(ctx, r, hovered);
        ctx.globalAlpha = 1.0;

        // Its bridges (links to other schools), when something else is selected
        if (typeof BridgeView !== 'undefined' && BridgeView._renderBridgesOf && hovered !== r.selectedNode) {
            BridgeView._renderBridgesOf(ctx, r, hovered, 0.6);
        }
        ctx.restore();
    },

    /** The hovered spell's path to the root, one batched stroke (renderEdges pass 1.5 did this per edge). */
    _renderPathEdges: function(ctx, r, hovered) {
        var set = r._hoverPathEdges;
        if (r._lodTier === 'minimal' || !set || set.size === 0 || !r.edges) return;
        var curved = settings.edgeStyle === 'curved';
        var S = TreeStyle.tokens;
        ctx.strokeStyle = r._getSchoolColor(hovered.school);
        ctx.lineWidth = 2;
        ctx.globalAlpha = S.hoverPathAlpha;
        ctx.beginPath();
        for (var i = 0; i < r.edges.length; i++) {
            var edge = r.edges[i];
            if (!set.has(edge.from + '->' + edge.to)) continue;
            var a = r._nodeMap.get(edge.from), b = r._nodeMap.get(edge.to);
            if (!a || !b || this._hidden(r, a) || this._hidden(r, b)) continue;
            r._drawEdgePath(ctx, a.x, a.y, b.x, b.y, curved);
        }
        ctx.stroke();
    },

    /** The nodes on the path (brighter) and the hovered one (focus ring), drawn as the layer would. */
    _renderNodes: function(ctx, r, hovered) {
        if (r._lodTier === 'minimal') {
            // The minimal level marks the hovered spell with a dot, unless the selected one has it
            if (r.selectedNode) return;
            var color = hovered._cachedSchoolColor || r._getSchoolColor(hovered.school);
            var d = this.MINIMAL_DOT, c = this.MINIMAL_CORE;
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(hovered.x - d / 2, hovered.y - d / 2, d, d);
            ctx.fillStyle = color;
            ctx.fillRect(hovered.x - c / 2, hovered.y - c / 2, c, c);
            return;
        }
        var mystery = this._mysteryShown(r);
        if (r._lodTier === 'simple') {
            // An undiscovered spell stays the plain dot the layer drew
            if (!(mystery && hovered.state === 'locked')) r._renderNodeSimple(ctx, hovered, r._learningColor());
            return;
        }
        var self = this;
        if (r._hoverPathNodes) {
            r._hoverPathNodes.forEach(function(id) {
                var node = r._nodeMap.get(id);
                if (node && node !== hovered && !self._hidden(r, node)) self._renderFull(ctx, r, node, mystery);
            });
        }
        this._renderFull(ctx, r, hovered, mystery);
    },

    /** Discovery mode shows undiscovered (locked) spells as mystery nodes, except in edit mode (renderNodes). */
    _mysteryShown: function(r) {
        return !!r._discoveryVisibleIds && !(typeof EditMode !== 'undefined' && EditMode.isActive);
    },

    _renderFull: function(ctx, r, node, mystery) {
        if (mystery && node.state === 'locked') r.renderMysteryNode(ctx, node);
        else r.renderNode(ctx, node);
    }
};

window.HoverOverlay = HoverOverlay;
