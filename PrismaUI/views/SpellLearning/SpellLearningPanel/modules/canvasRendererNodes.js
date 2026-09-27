/**
 * CanvasRenderer - the spells, all of them: the pass per level of detail
 * (minimal dots, simple shapes, full), what goes into NodeBatch (plain locked,
 * learnable, known and undiscovered spells, lock looks included), the school
 * shapes, and the sizes and dimming every spell look shares. A spell with
 * something of its own is drawn by itself (canvasRendererSpell.js).
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.renderNodes() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, NodeBatch, TreeStyle, settings, state,
 * getCanonicalFormId and getRequiredXPForNode (optional), EditMode (optional)
 */

(function() {
    var methods = {
        /**
         * Initialize Path2D cache for all school shapes
         * Called once at startup - shapes are reused for all nodes
         */
        _initShapePaths: function() {
            // Diamond Destruction, circle Restoration, hexagon Alteration, pentagon
            // Conjuration, triangle Illusion (tip inward); the shapes live in NodeBatch
            this._shapePaths = {};
            var schools = ['Destruction', 'Restoration', 'Alteration', 'Conjuration', 'Illusion'];
            for (var i = 0; i < schools.length; i++) {
                this._shapePaths[schools[i]] = NodeBatch.unitPath(schools[i]);
            }
            this._shapePaths['default'] = this._shapePaths['Restoration'];
        },
        /** node: its hand-drawn variant when the design draws by hand (NodeBatch.handPath). */
        _getShapePath: function(school, node) {
            var hand = node && NodeBatch.handPath(school, node.x, node.y);
            return hand || this._shapePaths[school] || this._shapePaths['default'];
        },

        /**
         * Focus + context: while a node is selected, everything outside its
         * dependency path fades (other schools more than the selected school) so
         * the eye lands on the path and the next unlock candidates. Hovered nodes
         * and hover paths stay bright so the user can still explore.
         * @returns {number} alpha multiplier 0..1
         */
        _contextFactor: function(node) {
            if (!this.selectedNode || (typeof settings !== 'undefined' && settings.focusDimOthers === false)) return 1;
            if (this._selectedPathNodes && this._selectedPathNodes.has(node.id)) return 1;
            if (this._hoverPathNodes && this._hoverPathNodes.has(node.id)) return 1;
            if (this.hoveredNode && this.hoveredNode.id === node.id) return 1;
            return node.school === this.selectedNode.school ? this.DIM_SAME_SCHOOL : this.DIM_OTHER_SCHOOL;
        },

        /**
         * Clamp a world-space node radius so it never renders below
         * MIN_NODE_SCREEN_RADIUS pixels at the current zoom.
         */
        _minSize: function(size) {
            var minWorld = this.MIN_NODE_SCREEN_RADIUS / (this.zoom || 1);
            return size < minWorld ? minWorld : size;
        },

        /**
         * Mystery (undiscovered) node radius grows with tier so the silhouette
         * still tells the player roughly how advanced the hidden spell is.
         */
        _mysterySize: function(node) {
            var tierIndex = 0;
            var level = (node.level || node.skillLevel || '').toString().toLowerCase();
            var byLevel = { novice: 0, apprentice: 1, adept: 2, expert: 3, master: 4 };
            if (byLevel[level] !== undefined) {
                tierIndex = byLevel[level];
            } else if (typeof node.tier === 'number' && node.tier > 0) {
                tierIndex = Math.min(node.tier - 1, 4);
            }
            return this.LOCKED_SIZE + tierIndex;
        },

        /**
         * XP progress (0..1) for a node, using the same lookups as the details panel.
         * Returns 0 when there is no progress data.
         */
        _getNodeProgressPct: function(node) {
            if (typeof state === 'undefined' || !state.spellProgress) return 0;
            var canonId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
            var progress = state.spellProgress[canonId];
            if (!progress || !progress.xp) return 0;

            var required = (typeof getRequiredXPForNode === 'function') ? getRequiredXPForNode(node) : null;
            if (!required) required = progress.required || 100;
            return required > 0 ? Math.min(progress.xp / required, 1) : 0;
        },

        /**
         * Minimal node rendering - batched fillRect dots grouped by school+state.
         * ~15 style changes + N fillRect calls instead of 8-18 canvas API calls per node.
         */
        renderNodesMinimal: function(ctx, viewLeft, viewRight, viewTop, viewBottom) {
            if (!this._nodeBuckets) return;

            var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
            var hasDiscovery = this._discoveryVisibleIds && !isEditActive;
            var schoolVis = settings.schoolVisibility;

            var bucketKeys = Object.keys(this._nodeBuckets);
            for (var b = 0; b < bucketKeys.length; b++) {
                var key = bucketKeys[b];
                var parts = key.split('|');
                var bucketSchool = parts[0];
                var bucketState = parts[1];
                var bucket = this._nodeBuckets[key];
                if (bucket.length === 0) continue;

                // Skip hidden schools
                if (schoolVis && schoolVis[bucketSchool] === false) continue;

                // Determine dot size and alpha by state
                var dotSize, alpha;
                if (bucketState === 'unlocked') {
                    dotSize = 4; alpha = 1.0;
                } else if (bucketState === 'available' || bucketState === 'learning') {
                    dotSize = 3; alpha = TreeStyle.tokens.availableAlpha;
                } else {
                    dotSize = 2; alpha = 0.4;
                }

                // Focus + context at bucket granularity (per-node path checks are too costly here)
                if (this.selectedNode && settings.focusDimOthers !== false && bucketSchool !== this.selectedNode.school) {
                    alpha *= this.DIM_OTHER_SCHOOL;
                }

                // Set style once per bucket
                var color = bucket[0]._cachedSchoolColor || this._getSchoolColor(bucketSchool);
                ctx.fillStyle = color;
                ctx.globalAlpha = alpha;

                var halfDot = dotSize / 2;

                for (var i = 0; i < bucket.length; i++) {
                    var node = bucket[i];

                    // Viewport culling
                    if (node.x < viewLeft || node.x > viewRight || node.y < viewTop || node.y > viewBottom) continue;

                    // Discovery visibility
                    if (hasDiscovery) {
                        if (!this._discoveryVisibleIds.has(node.id) && !this._discoveryVisibleIds.has(node.formId)) continue;
                    }

                    ctx.fillRect(node.x - halfDot, node.y - halfDot, dotSize, dotSize);
                }
            }

            // Render selected/hovered node as larger highlighted dot on top
            var highlight = this.selectedNode || this.hoveredNode;
            if (highlight) {
                var hColor = highlight._cachedSchoolColor || this._getSchoolColor(highlight.school);
                ctx.fillStyle = '#ffffff';
                ctx.globalAlpha = 1.0;
                ctx.fillRect(highlight.x - 5, highlight.y - 5, 10, 10);
                ctx.fillStyle = hColor;
                ctx.fillRect(highlight.x - 3, highlight.y - 3, 6, 6);
            }

            ctx.globalAlpha = 1.0;
        },

        /**
         * Simple node rendering - shapes without rotation, lock overlays, or inner accents.
         * Still uses Path2D + save/translate/scale/fill/stroke/restore but skips atan2/rotate.
         */
        renderNodesSimple: function(ctx, viewLeft, viewRight, viewTop, viewBottom) {
            var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
            var hasDiscovery = this._discoveryVisibleIds && !isEditActive;
            var learningPathColor = this._learningColor();

            // Every spell but the selected and hovered one into NodeBatch, unturned
            // (a few paint calls for all of them instead of nine each); those two after
            var own = [];
            NodeBatch.begin(true);
            // The spells near the box only (_nodesInBox), in node order; the box test below stays
            var nodes = this.nodes, count = this._nodesInBox(viewLeft, viewRight, viewTop, viewBottom);
            var pick = count < 0 ? null : this._cull.nodePick;
            if (count < 0) count = nodes.length;
            for (var ni = 0; ni < count; ni++) {
                var node = nodes[pick ? pick[ni] : ni];

                // Viewport culling
                if (node.x < viewLeft || node.x > viewRight || node.y < viewTop || node.y > viewBottom) continue;

                // Skip hidden schools
                if (settings.schoolVisibility && settings.schoolVisibility[node.school] === false) continue;

                // Discovery mode visibility
                if (hasDiscovery) {
                    if (!this._discoveryVisibleIds.has(node.id) && !this._discoveryVisibleIds.has(node.formId)) continue;
                    if (node.state === 'locked') {
                        // Simplified mystery node - just a dim dot
                        var dimColor = node._cachedSchoolColor || this._getSchoolColor(node.school);
                        ctx.globalAlpha = 0.3;
                        ctx.fillStyle = dimColor;
                        ctx.fillRect(node.x - 3, node.y - 3, 6, 6);
                        continue;
                    }
                }

                if ((this.selectedNode && this.selectedNode.id === node.id) ||
                        (this.hoveredNode && this.hoveredNode.id === node.id)) {
                    own.push(node);
                } else {
                    this._renderNodeSimple(ctx, node, learningPathColor, true);
                }
            }
            NodeBatch.flush(ctx, this.rotation, this._backdrop());
            for (var k = 0; k < own.length; k++) this._renderNodeSimple(ctx, own[k], learningPathColor);
            ctx.globalAlpha = 1.0;
        },

        /**
         * One spell at the simple level of detail (renderNodesSimple; HoverOverlay
         * redraws the hovered one). batch: into NodeBatch instead of drawn.
         */
        _renderNodeSimple: function(ctx, node, learningPathColor, batch) {
            var schoolColor = node._cachedSchoolColor || this._getSchoolColor(node.school);
            var isSelected = this.selectedNode && this.selectedNode.id === node.id;
            var isHovered = this.hoveredNode && this.hoveredNode.id === node.id;
            var path = this._getShapePath(node.school, node);
            var isLearning = node.state === 'learning';

            var size, fillColor, strokeColor, strokeWidth, alpha;

            var style = TreeStyle.tokens;
            if (node.state === 'unlocked') {
                size = this.KNOWN_SIZE; fillColor = style.unlockedFill || schoolColor; strokeColor = style.unlockedRim || schoolColor;
                strokeWidth = 1.5; alpha = 1.0;
            } else if (isLearning) {
                size = this.KNOWN_SIZE; fillColor = learningPathColor; strokeColor = learningPathColor;
                strokeWidth = 1.5; alpha = 1.0;
            } else if (node.state === 'available') {
                size = this.LEARNABLE_SIZE; fillColor = style.nodeFill; strokeColor = schoolColor;
                strokeWidth = 1; alpha = style.availableAlpha;
            } else {
                size = this.LOCKED_SIZE; fillColor = style.nodeFill; strokeColor = style.lockedStroke || schoolColor;
                strokeWidth = 1; alpha = 0.4;
            }

            alpha *= this._contextFactor(node);
            size = this._minSize(size);

            if (isSelected || isHovered) {
                size += this.FOCUS_GROW; strokeColor = style.focusStroke; strokeWidth = 1.5; alpha = 1.0;
            }

            if (batch) {
                NodeBatch.addShape(node.school, node.x, node.y, size, fillColor, strokeColor, alpha, false, strokeWidth);
                return;
            }

            ctx.save();
            ctx.translate(node.x, node.y);
            // NO rotation — skip atan2 + rotate (main LOD saving)
            ctx.scale(size, size);
            if (alpha < 1) this._underlay(ctx, path);
            ctx.globalAlpha = alpha;
            ctx.fillStyle = fillColor;
            ctx.strokeStyle = strokeColor;
            ctx.lineWidth = strokeWidth / size;
            ctx.fill(path);
            ctx.stroke(path);
            ctx.restore();
        },

        renderNodes: function(ctx, viewLeft, viewRight, viewTop, viewBottom) {
            // LOD dispatch
            if (this._lodTier === 'minimal') {
                this.renderNodesMinimal(ctx, viewLeft, viewRight, viewTop, viewBottom);
                return;
            }
            if (this._lodTier === 'simple') {
                this.renderNodesSimple(ctx, viewLeft, viewRight, viewTop, viewBottom);
                return;
            }

            // Locked, known (lock look too) and undiscovered spells go into NodeBatch
            // (a few paint calls for all of them); the rest are drawn one by one on top
            var discovery = this._discoveryVisibleIds && !(typeof EditMode !== 'undefined' && EditMode.isActive);
            var special = [];
            NodeBatch.begin();
            // The spells near the box only (_nodesInBox), in node order; the box test below stays
            var nodes = this.nodes, count = this._nodesInBox(viewLeft, viewRight, viewTop, viewBottom);
            var pick = count < 0 ? null : this._cull.nodePick;
            if (count < 0) count = nodes.length;
            for (var ni = 0; ni < count; ni++) {
                var node = nodes[pick ? pick[ni] : ni];

                // Viewport culling
                if (node.x < viewLeft || node.x > viewRight || node.y < viewTop || node.y > viewBottom) {
                    continue;
                }

                // Skip hidden schools
                if (settings.schoolVisibility && settings.schoolVisibility[node.school] === false) {
                    continue;
                }

                // Discovery mode visibility (disabled in edit mode - show everything)
                if (discovery) {
                    if (!this._discoveryVisibleIds.has(node.id) && !this._discoveryVisibleIds.has(node.formId)) {
                        continue;
                    }

                    // Show locked nodes as mystery
                    if (node.state === 'locked') {
                        this._batchMysteryNode(node);
                        continue;
                    }
                }

                if (!this._batchPlainNode(node)) special.push(node);
            }
            NodeBatch.flush(ctx, this.rotation, this._backdrop());
            for (var k = 0; k < special.length; k++) this.renderNode(ctx, special[k]);
        },

        /**
         * A locked, learnable or known spell with nothing of its own (not selected,
         * hovered or on the hover path, no XP ring) goes into NodeBatch, with the
         * look renderNode would give it - the lock look too (hard prerequisites: a
         * grey shell with a school-coloured hole until known, a grey ring once
         * known). Returns false for the others. Learnable spells used to be drawn one
         * by one, some nine paint calls each: a tree has hundreds of them.
         */
        _batchPlainNode: function(node) {
            var locked = node.state === 'locked';
            var available = node.state === 'available';
            if (!locked && !available && node.state !== 'unlocked') return false;
            if (available && this._getNodeProgressPct(node) > 0) return false;
            if (this.selectedNode && this.selectedNode.id === node.id) return false;
            if (this.hoveredNode && this.hoveredNode.id === node.id) return false;
            if (this._hoverPathNodes && this._hoverPathNodes.has(node.id)) return false;
            var style = TreeStyle.tokens;
            var schoolColor = node.themeColor ? TreeStyle.ink(node.themeColor) : this._getSchoolColor(node.school);
            var cf = this._contextFactor(node);
            var lock = node.hardPrereqs && node.hardPrereqs.length > 0;
            if (locked || available) {
                var lsize = this._minSize(locked ? this.LOCKED_SIZE : this.LEARNABLE_SIZE), alpha = (locked ? 0.4 : style.availableAlpha) * cf;
                // A learnable spell's own thin ring (renderNode draws it before the shape, unturned)
                if (available && style.availableRing) {
                    NodeBatch.addShape(NodeBatch.RING, node.x, node.y, lsize + this.RING_GAP, null, schoolColor,
                        this.RING_ALPHA * cf, false, this.RING_WIDTH, 0, true);
                }
                if (lock) {
                    NodeBatch.addShape(node.school, node.x, node.y, lsize + 2, this.LOCK_SHELL_FILL,
                        this.LOCK_SHELL_STROKE, Math.min(alpha + 0.2, 0.75), false, 1.2, 0);
                    NodeBatch.addShape(node.school, node.x, node.y, Math.max(lsize * 0.45, 3), schoolColor,
                        null, Math.min(alpha + 0.15, 0.65), false, 1, 2);
                    return true;
                }
                NodeBatch.addShape(node.school, node.x, node.y, lsize, style.nodeFill,
                    locked ? (style.lockedStroke || schoolColor) : schoolColor, alpha, locked);
                return true;
            }
            var size = this._minSize(this.KNOWN_SIZE);
            var onPath = (this._learningPathNodes instanceof Set) && this._learningPathNodes.has(node.id) &&
                         !(this._animatingPathNodes && this._animatingPathNodes.has(node.id));
            if (style.nodeGlow > 0 && size * this.HALO_SCALE * this.zoom >= this.HALO_MIN_SCREEN_PX) {
                NodeBatch.addHalo(node.x, node.y, size * this.HALO_SCALE, schoolColor, style.nodeGlow * cf);
            }
            if (lock) {
                NodeBatch.addShape(node.school, node.x, node.y, size + 3, this.LOCK_RING_FILL,
                    this.LOCK_RING_STROKE, 0.5, false, 1.5, 0, true);
            }
            // The lock ring's spell is not underlaid (renderNode draws it straight over the ring)
            NodeBatch.addShape(node.school, node.x, node.y, size, style.unlockedFill || schoolColor,
                onPath ? this._heartRing() : (style.unlockedRim || schoolColor), cf, false, 1.5, 1, lock);
            NodeBatch.addShape(node.school, node.x, node.y, size * 0.5,
                style.unlockedCore || this.getInnerAccentColor(schoolColor), null, cf, false, 1, 2);
            var inner = TreeStyle.innerLineAt(size);
            if (inner) {
                NodeBatch.addShape(node.school, node.x, node.y, inner.size, null,
                    inner.color || this._backdrop(), cf, false, inner.width, 2);
            }
            return true;
        },

        /**
         * What is behind the tree: the design's page colour, else the background
         * colour. A see-through spell is filled with it first, so the lines drawn
         * before the spells do not show through them.
         */
        _backdrop: function() {
            return (TreeStyle.tokens && TreeStyle.tokens.pageColor) || this._bgColor || '#000000';
        },

        /** Fill `path` with the backdrop, opaque (the spell's own fill goes on top). */
        _underlay: function(ctx, path) {
            var a = ctx.globalAlpha;
            ctx.globalAlpha = 1;
            ctx.fillStyle = this._backdrop();
            ctx.fill(path);
            ctx.globalAlpha = a;
        },

        /** An undiscovered spell (renderMysteryNode's look) goes into NodeBatch. */
        _batchMysteryNode: function(node) {
            var dimmed = this.dimColor(this._getSchoolColor(node.school), 0.4);
            var cf = this._contextFactor(node);
            NodeBatch.addShape(node.school, node.x, node.y, this._minSize(this._mysterySize(node)),
                TreeStyle.tokens.mysteryFill, dimmed, 0.6 * cf, true);
            NodeBatch.addMark(node.x, node.y, dimmed, 0.8 * cf);
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
