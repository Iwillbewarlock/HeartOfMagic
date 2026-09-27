/**
 * CanvasRenderer - spell names, screen-aligned (they do not turn with the
 * wheel): the candidates in view and in the layer's margin, their boxes, and
 * placing them by priority without overlaps. Shared with LayerScroll, which
 * places names in the strips a drag uncovers against the ones the layer keeps.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.renderLabels() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, TreeStyle (tokens, clipLabel, beginLabels,
 * drawLabel), settings, state, BridgeView and EditMode (optional)
 */

(function() {
    var methods = {
        /**
         * Label priority for collision resolution (higher wins):
         * 5 selected, 4 hovered / hover path, 3 learning, 2 available, 1 unlocked.
         */
        _labelPriority: function(node) {
            if (this.selectedNode && this.selectedNode.id === node.id) return 5;
            if (this.hoveredNode && this.hoveredNode.id === node.id) return 4;
            if (this._hoverPathNodes && this._hoverPathNodes.has(node.id)) return 4;
            if (node.state === 'learning') return 3;
            if (node.state === 'available') return 2;
            return 1;
        },

        /**
         * The names that could be drawn at the current pan, in the order they are
         * placed (in view first, then priority). Null when names are off or the
         * tree is zoomed out too far for them. Shared by renderLabels and
         * LayerScroll (the names in a strip uncovered by a drag).
         * @returns {{candidates: Array, fontSize: number, maxLabels: number}|null}
         */
        _labelCandidates: function(cx, cy, cos, sin, margin) {
            if (this.zoom < this.LABEL_MIN_ZOOM) return null;
            margin = margin || 0;
            if (settings.showNodeNames === false) return null;

            var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
            var fontSize = settings.nodeFontSize || 10;
            var style = TreeStyle.tokens;

            var focusOnly = this.zoom < this.LABEL_FOCUS_ZOOM;
            var learningColor = this._learningColor();
            var candidates = [];

            // Labels are drawn after the tree transform is undone, so the trait
            // filter's veil cannot dim them: they have to drop out themselves.
            var filtering = typeof BridgeView !== 'undefined' && BridgeView.hasFilter();

            var edgeX0 = -50 - margin, edgeX1 = this._width + 50 + margin;
            var edgeY0 = -50 - margin, edgeY1 = this._height + 50 + margin;
            for (var i = 0; i < this.nodes.length; i++) {
                var node = this.nodes[i];
                // In edit mode: show ALL labels. Otherwise: only unlocked/learning/available
                if (!isEditActive && node.state !== 'unlocked' && node.state !== 'learning' && node.state !== 'available') continue;
                if (!node.name && !isEditActive) continue;

                // Transform node position WITH rotation, but text stays screen-aligned;
                // off the canvas (and its margin) is ruled out before the lookups below
                var rotatedX = node.x * cos - node.y * sin;
                var rotatedY = node.x * sin + node.y * cos;
                var screenX = rotatedX * this.zoom + this.panX + cx;
                var screenY = rotatedY * this.zoom + this.panY + cy;
                if (screenX < edgeX0 || screenX > edgeX1 || screenY < edgeY0 || screenY > edgeY1) continue;

                if (filtering && !BridgeView.matchesFilter(node)) continue;
                if (settings.schoolVisibility && settings.schoolVisibility[node.school] === false) continue;

                // Check if name should be revealed based on XP progress
                var labelText = node.name || node.formId;
                if (!isEditActive && node.state !== 'unlocked' && settings.cheatMode !== true) {
                    var _canonId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
                    var _prog = state.spellProgress ? state.spellProgress[_canonId] : null;
                    var _pct = _prog && _prog.required > 0 ? (_prog.xp / _prog.required) * 100 : 0;
                    var _threshold = settings.revealName !== undefined ? settings.revealName : 10;
                    if (_pct < _threshold && node.state !== 'learning') {
                        labelText = '???';
                    }
                }

                var priority = labelText === '???' ? 0 : this._labelPriority(node);
                // Zoomed out: keep only selected / hovered / learning / available names
                if (focusOnly && priority < 2) continue;

                // In view, as before; in the margin only
                var inView = !(screenX < -50 || screenX > this._width + 50 || screenY < -50 || screenY > this._height + 50);

                // Color by state
                var color;
                if (node.state === 'unlocked') {
                    color = style.labelUnlocked;
                } else if (node.state === 'learning') {
                    color = learningColor;
                } else if (labelText === '???') {
                    color = style.labelHidden;
                } else {
                    color = style.labelAvailable;
                }

                // The shortened name, kept on the spell while its name and the limit stay
                if (node._labelSrc !== labelText || node._labelMax !== style.labelMaxChars) {
                    node._labelSrc = labelText;
                    node._labelMax = style.labelMaxChars;
                    node._labelText = TreeStyle.clipLabel(labelText);
                }
                candidates.push({
                    node: node,
                    text: node._labelText,
                    inView: inView,
                    priority: priority,
                    x: screenX,
                    y: screenY + (fontSize + 4) * this.zoom,
                    color: color
                });
            }

            // In view before the margin, then high priority first; stable on index so
            // results don't flicker between frames
            candidates.sort(this._byLabelOrder);
            return { candidates: candidates, fontSize: fontSize, maxLabels: this.MAX_LABELS };
        },

        /**
         * The box a candidate name takes (css px, the canvas's own coordinates); ctx
         * has the label font and _labelFontFrom has seen it. Its top and bottom are
         * cand.y - LABEL_PAD and cand.y + fontSize + LABEL_PAD (LayerScroll tests
         * them before asking for the box).
         */
        _labelRect: function(ctx, cand, fontSize) {
            var pad = this.LABEL_PAD;
            var halfW = this._labelWidth(ctx, cand.text) / 2 + pad;
            return { l: cand.x - halfW, r: cand.x + halfW, t: cand.y - pad, b: cand.y + fontSize + pad };
        },

        /** A placed name as the layer keeps it (_layerLabels): its box, what and how it was drawn. */
        _keepLabel: function(cand, rect) {
            return { node: cand.node, text: cand.text, x: cand.x, y: cand.y, color: cand.color,
                     alpha: this._contextFactor(cand.node), l: rect.l, r: rect.r, t: rect.t, b: rect.b };
        },

        /**
         * Render labels - SCREEN ALIGNED (don't rotate with wheel).
         * Labels are placed by priority (selected > hovered > learning > available >
         * unlocked) with screen-space collision rejection, so overlapping names no
         * longer pile up. Zoomed out, only the important labels remain.
         * margin: CSS px drawn round the canvas (the tree layer's): the names there
         * are drawn too, so a drag that slides the layer does not show spells
         * without them - after the ones in view, which come out as they would
         * without it. At most MAX_LABELS.
         */
        renderLabels: function(ctx, cx, cy, cos, sin, margin) {
            // The names on the layer, kept for LayerScroll (a drag moves them with the picture)
            var placed = this._layerLabels = [];
            var found = this._labelCandidates(cx, cy, cos, sin, margin);
            if (!found) return;
            var candidates = found.candidates, fontSize = found.fontSize;
            TreeStyle.beginLabels(ctx, fontSize);
            this._labelFontFrom(ctx);
            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';

            for (var c = 0; c < candidates.length && placed.length < found.maxLabels; c++) {
                var cand = candidates[c];
                var rect = this._labelRect(ctx, cand, fontSize);

                // Collision rejection: the selected node's label always wins
                var collides = false;
                if (cand.priority < 5) {
                    for (var p = 0; p < placed.length; p++) {
                        var o = placed[p];
                        if (rect.l < o.r && rect.r > o.l && rect.t < o.b && rect.b > o.t) { collides = true; break; }
                    }
                }
                if (collides) continue;

                var lab = this._keepLabel(cand, rect);
                placed.push(lab);
                ctx.globalAlpha = lab.alpha;
                TreeStyle.drawLabel(ctx, cand.text, cand.x, cand.y, cand.color);
            }

            ctx.globalAlpha = 1.0;
        },

        _byLabelOrder: function(a, b) {
            if (a.inView !== b.inView) return a.inView ? -1 : 1;
            return b.priority - a.priority;
        },

        /**
         * Before a run of names (renderLabels, LayerScroll._labels), once: the text
         * widths kept are for ctx's font (read here, not per name - reading
         * ctx.font builds a string each time); another font starts them afresh.
         */
        _labelFontFrom: function(ctx) {
            var font = ctx.font;
            if (this._labelWidthFont !== font || !this._labelWidths) {
                this._labelWidthFont = font;
                this._labelWidths = new Map();
            }
        },

        /** measureText's width, kept per text for the font _labelFontFrom saw (names do not change between repaints). */
        _labelWidth: function(ctx, text) {
            var w = this._labelWidths.get(text);
            if (w === undefined) {
                w = ctx.measureText(text).width;
                this._labelWidths.set(text, w);
            }
            return w;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
