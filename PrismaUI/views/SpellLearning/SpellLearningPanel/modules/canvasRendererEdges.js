/**
 * CanvasRenderer - the lines: from the heart to the known roots, then the lines
 * between spells in passes bottom to top (dimmed, locked, frontier, known, each
 * look stroked as one batch; the hover path; the selected path; learning
 * paths; the chains of a selected spell's hard prerequisites).
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.renderEdges() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, state (globe position), TreeStyle
 * (tokens, edgeBow, bowBulge, inkStipple, inkDash, edgeCutWidth), EditMode (optional)
 */

(function() {
    var methods = {
        /**
         * Draw an edge path between two points (straight or curved Bezier).
         * Call between ctx.beginPath() and ctx.stroke().
         */
        _drawEdgePath: function(ctx, x1, y1, x2, y2, curved) {
            ctx.moveTo(x1, y1);
            if (curved) {
                var cpx = (x1 + x2) / 2 + (y2 - y1) * 0.15;
                var cpy = (y1 + y2) / 2 - (x2 - x1) * 0.15;
                ctx.quadraticCurveTo(cpx, cpy, x2, y2);
            } else {
                // A design that draws by hand bows its straight lines a little (TreeStyle.edgeBow)
                var bow = TreeStyle.edgeBow(x1, y1, x2, y2);
                if (bow) ctx.quadraticCurveTo((x1 + x2) / 2 - (y2 - y1) * bow, (y1 + y2) / 2 + (x2 - x1) * bow, x2, y2);
                else ctx.lineTo(x2, y2);
            }
        },

        renderEdges: function(ctx, viewLeft, viewRight, viewTop, viewBottom) {
            var learningPathColor = this._learningColor();
            var hasLearningPaths = this._learningPathNodes instanceof Set && this._learningPathNodes.size > 0;
            var curved = settings.edgeStyle === 'curved';

            // Detect heartbeat for spawning traveling pulses
            var isHeartbeating = false;
            if (hasLearningPaths && this._heartAnimationEnabled) {
                var phasePerSecond = this._heartbeatSpeed * 60;
                var pulseDelay = (this._heartPulseDelay || 2.0) * phasePerSecond;
                var beatDuration = Math.PI;
                var cycleLength = beatDuration + pulseDelay;
                var cyclePos = this._heartbeatPhase % cycleLength;

                // Detect start of heartbeat (rising edge) for learning path particles
                var nowBeating = cyclePos < beatDuration && cyclePos < 0.5;
                // (particles to the learning path leave from _renderHubAndFinish,
                // which runs every frame; this only runs when the layer is repainted)
                this._lastHeartbeatPulse = nowBeating;
                isHeartbeating = cyclePos < beatDuration;
            }

            // =====================================================================
            // FIRST: Draw lines from CENTER to ROOT NODES
            // =====================================================================
            for (var i = 0; i < this.nodes.length; i++) {
                var node = this.nodes[i];

                // Check if this is a root node ONLY (not tier 1)
                if (!node.isRoot) continue;

                // Skip if hidden school
                if (settings.schoolVisibility && settings.schoolVisibility[node.school] === false) continue;

                // Check if this node is on a learning path
                var isOnLearningPath = this._learningPathNodes instanceof Set &&
                                       this._learningPathNodes.has(node.id);

                // During animation, hide ONLY the nodes in the currently animating path
                // (let animation draw that path progressively, but keep other learning paths visible)
                if (isOnLearningPath && this._animatingPathNodes && this._animatingPathNodes.has(node.id)) {
                    isOnLearningPath = false;  // Hide - animation will draw this
                }

                // Draw if unlocked OR if on a learning path (after animation)
                if (node.state !== 'unlocked' && !isOnLearningPath) continue;

                // Check if this root is on the selected path
                var isRootOnSelectedPath = this._selectedPathNodes && this._selectedPathNodes.has(node.id);
                var hasSelectedPath = this._selectedPathEdges && this._selectedPathEdges.size > 0;
                var showSelectionPathRoot = settings.showSelectionPath !== false;

                // Draw line from globe center to root node
                var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };
                ctx.beginPath();
                ctx.moveTo(gd.x, gd.y);
                ctx.lineTo(node.x, node.y);

                if (isOnLearningPath) {
                    // Glowing learning path style - static, pulses travel along it
                    ctx.strokeStyle = learningPathColor;
                    ctx.lineWidth = 3;
                    ctx.globalAlpha = 0.7;
                } else if (isRootOnSelectedPath && showSelectionPathRoot) {
                    // Root is on selected path - WHITE highlight (if enabled)
                    ctx.strokeStyle = '#ffffff';
                    ctx.lineWidth = 3;
                    ctx.globalAlpha = 0.45;
                } else {
                    // Normal unlocked root line - always visible regardless of selection
                    ctx.strokeStyle = this._getSchoolColor(node.school);
                    ctx.lineWidth = 2;
                    ctx.globalAlpha = 0.4;
                }
                ctx.stroke();
            }

            // =====================================================================
            // SECOND: Draw regular edges (3-pass for z-order)
            // Pass 1: Dim/normal edges (bottom)
            // Pass 2: Selected path edges (WHITE, middle)
            // Pass 3: Learning path edges (top)
            // =====================================================================
            var hasSelectedPath = this._selectedPathEdges && this._selectedPathEdges.size > 0;
            var self = this;

            // Helper to check visibility and culling
            // A curved or hand-bowed line bulges past its ends' box: culling allows for it
            var bend = (curved ? this.CURVE_BULGE : 0) + TreeStyle.bowBulge();
            function shouldDrawEdge(edge) {
                var fromNode = self._nodeMap.get(edge.from);
                var toNode = self._nodeMap.get(edge.to);
                if (!fromNode || !toNode) return null;

                // Discovery mode: skip if either node not visible
                if (self._discoveryVisibleIds && !(typeof EditMode !== 'undefined' && EditMode.isActive)) {
                    var fromVisible = self._discoveryVisibleIds.has(edge.from) || self._discoveryVisibleIds.has(fromNode.id);
                    var toVisible = self._discoveryVisibleIds.has(edge.to) || self._discoveryVisibleIds.has(toNode.id);
                    if (!fromVisible || !toVisible) return null;
                }

                // Viewport culling
                var minX = Math.min(fromNode.x, toNode.x);
                var maxX = Math.max(fromNode.x, toNode.x);
                var minY = Math.min(fromNode.y, toNode.y);
                var maxY = Math.max(fromNode.y, toNode.y);
                var ext = bend ? bend * (maxX - minX + maxY - minY) : 0;
                if (maxX + ext < viewLeft || minX - ext > viewRight || maxY + ext < viewTop || minY - ext > viewBottom) {
                    return null;
                }

                return { fromNode: fromNode, toNode: toNode };
            }

            var S = TreeStyle.tokens;

            // === PASS 1: Dim/normal edges (background) ===
            // LOD: Skip entirely in MINIMAL (biggest edge savings)
            if (this._lodTier !== 'minimal') {
            // Check if base connections should be shown (setting)
            var showBaseConnections = settings.showBaseConnections !== false;
            var lodSimple = this._lodTier === 'simple';
            // Only dim when selection path highlighting is enabled
            var dimAll = hasSelectedPath && settings.showSelectionPath !== false;
            var showFrontier = S.frontierEdgeAlpha > 0;

            // Edges that look alike are stroked together, one path per look, instead
            // of one beginPath/stroke per edge. Where two edges of a batch cross, the
            // crossing is no longer painted twice.
            var batches = { dim: [], locked: [] };
            var frontierKeys = [], unlockedKeys = [];

            for (var i = 0; i < this.edges.length; i++) {
                var edge = this.edges[i];
                var nodes = shouldDrawEdge(edge);
                if (!nodes) continue;

                var fromNode = nodes.fromNode;
                var toNode = nodes.toNode;
                var edgeKey = edge.from + '->' + edge.to;
                var isOnSelectedPath = this._selectedPathEdges && this._selectedPathEdges.has(edgeKey);
                var fromOnPath = hasLearningPaths && this._learningPathNodes.has(fromNode.id);
                var toOnPath = hasLearningPaths && this._learningPathNodes.has(toNode.id);
                var isLearningEdge = fromOnPath && toOnPath;

                // Skip selected and learning edges - they go in later passes
                if (isOnSelectedPath || isLearningEdge) continue;

                var bothUnlocked = fromNode.state === 'unlocked' && toNode.state === 'unlocked';
                // Frontier: from a known spell to one that can be learned now
                var isFrontier = showFrontier && !bothUnlocked && fromNode.state === 'unlocked' &&
                                 (toNode.state === 'available' || toNode.state === 'learning');

                // LOD SIMPLE: only edges that lead somewhere the player has been or can go
                if (lodSimple && !bothUnlocked && !isFrontier) continue;

                // Unlocked and frontier connections always show; base connections respect setting
                if (!bothUnlocked && !isFrontier && !showBaseConnections) continue;

                var bucket;
                if (dimAll) {
                    bucket = batches.dim;
                } else if (bothUnlocked || isFrontier) {
                    var key = bothUnlocked ? 'u|' + (S.unlockedEdgeColor || this._getSchoolColor(fromNode.school))
                                           : 'f|' + this._getSchoolColor(fromNode.school);
                    bucket = batches[key];
                    if (!bucket) {
                        bucket = batches[key] = [];
                        (bothUnlocked ? unlockedKeys : frontierKeys).push(key);
                    }
                } else {
                    bucket = batches.locked;
                }
                bucket.push(fromNode, toNode);
            }

            function strokeBatch(list) {
                ctx.beginPath();
                for (var b = 0; b < list.length; b += 2) {
                    self._drawEdgePath(ctx, list[b].x, list[b].y, list[b + 1].x, list[b + 1].y, curved);
                }
                ctx.stroke();
            }

            // Bottom to top: dimmed, locked, frontier, unlocked
            if (batches.dim.length) {
                // Node selected but these edges NOT on its path - dim heavily
                ctx.strokeStyle = S.dimEdgeColor;
                ctx.lineWidth = 1;
                ctx.globalAlpha = 0.08;
                strokeBatch(batches.dim);
            }
            if (batches.locked.length) {
                // Stippled in a design that asks for it (a dot pattern, TreeStyle.inkStipple)
                ctx.strokeStyle = TreeStyle.inkStipple(ctx, S.lockedEdgeColor) || S.lockedEdgeColor;
                ctx.lineWidth = 1;
                ctx.globalAlpha = S.lockedEdgeAlpha;
                strokeBatch(batches.locked);
            }
            // Breaks in the known and frontier lines (chalk skipping on the board)
            var breaks = TreeStyle.inkDash(S.edgeBreaks);
            if (breaks) ctx.setLineDash(breaks);
            for (var fk = 0; fk < frontierKeys.length; fk++) {
                ctx.strokeStyle = frontierKeys[fk].substring(2);
                ctx.lineWidth = 1.25;
                ctx.globalAlpha = S.frontierEdgeAlpha;
                strokeBatch(batches[frontierKeys[fk]]);
            }
            for (var uk = 0; uk < unlockedKeys.length; uk++) {
                var uList = batches[unlockedKeys[uk]];
                ctx.strokeStyle = unlockedKeys[uk].substring(2);
                if (S.edgeGlow > 0 && this.zoom >= this.EDGE_GLOW_MIN_ZOOM) {
                    // A wide faint stroke under the line: the channel glows (not broken)
                    if (breaks) ctx.setLineDash([]);
                    ctx.lineWidth = S.unlockedEdgeWidth * 3;
                    ctx.globalAlpha = S.edgeGlow;
                    strokeBatch(uList);
                    if (breaks) ctx.setLineDash(breaks);
                }
                // Unlocked connections always visible
                ctx.lineWidth = S.unlockedEdgeWidth;
                ctx.globalAlpha = S.unlockedEdgeAlpha;
                strokeBatch(uList);
                // Engraved: a stripe of the page down the middle turns the line into two
                var cut = TreeStyle.edgeCutWidth(S.unlockedEdgeWidth);
                if (cut) {
                    ctx.strokeStyle = this._backdrop();
                    ctx.lineWidth = cut;
                    ctx.globalAlpha = 1;
                    strokeBatch(uList);
                }
            }
            if (breaks) ctx.setLineDash([]);
            } // end LOD skip for MINIMAL

            // === PASS 1.5: Hover preview path (hovered node's school color) ===
            if (this._lodTier !== 'minimal' && this._hoverPathEdges && this._hoverPathEdges.size > 0 && this.hoveredNode) {
                ctx.strokeStyle = this._getSchoolColor(this.hoveredNode.school);
                ctx.lineWidth = 2;
                ctx.globalAlpha = S.hoverPathAlpha;
                for (var hi = 0; hi < this.edges.length; hi++) {
                    var hEdge = this.edges[hi];
                    if (!this._hoverPathEdges.has(hEdge.from + '->' + hEdge.to)) continue;
                    var hNodes = shouldDrawEdge(hEdge);
                    if (!hNodes) continue;
                    ctx.beginPath();
                    this._drawEdgePath(ctx, hNodes.fromNode.x, hNodes.fromNode.y, hNodes.toNode.x, hNodes.toNode.y, curved);
                    ctx.stroke();
                }
            }

            // === PASS 2: Selected path edges (WHITE, middle layer) ===
            // LOD: Skip in MINIMAL tier
            // Only draw if selection path highlighting is enabled
            var showSelectionPath = settings.showSelectionPath !== false;

            if (this._lodTier !== 'minimal' && hasSelectedPath && showSelectionPath) {
                for (var i = 0; i < this.edges.length; i++) {
                    var edge = this.edges[i];
                    var nodes = shouldDrawEdge(edge);
                    if (!nodes) continue;

                    var edgeKey = edge.from + '->' + edge.to;
                    if (!this._selectedPathEdges.has(edgeKey)) continue;

                    // Don't draw over learning edges - they get their own pass
                    var fromOnPath = hasLearningPaths && this._learningPathNodes.has(nodes.fromNode.id);
                    var toOnPath = hasLearningPaths && this._learningPathNodes.has(nodes.toNode.id);
                    if (fromOnPath && toOnPath) continue;

                    ctx.strokeStyle = S.selectedPathColor;
                    ctx.lineWidth = S.selectedPathWidth;
                    ctx.globalAlpha = S.selectedPathAlpha;

                    ctx.beginPath();
                    this._drawEdgePath(ctx, nodes.fromNode.x, nodes.fromNode.y, nodes.toNode.x, nodes.toNode.y, curved);
                    ctx.stroke();
                }
            }

            // === PASS 3: Learning path edges (top layer) ===
            if (hasLearningPaths) {
                for (var i = 0; i < this.edges.length; i++) {
                    var edge = this.edges[i];
                    var nodes = shouldDrawEdge(edge);
                    if (!nodes) continue;

                    var fromNode = nodes.fromNode;
                    var toNode = nodes.toNode;
                    var fromOnPath = this._learningPathNodes.has(fromNode.id);
                    var toOnPath = this._learningPathNodes.has(toNode.id);

                    if (!fromOnPath || !toOnPath) continue;

                    // Skip if animating
                    if (this._animatingPathNodes) {
                        if (this._animatingPathNodes.has(fromNode.id) && this._animatingPathNodes.has(toNode.id)) {
                            continue;
                        }
                    }

                    var toLearning = toNode.state === 'learning';
                    ctx.strokeStyle = learningPathColor;
                    ctx.lineWidth = toLearning ? 3 : 2;
                    ctx.globalAlpha = 0.7;

                    ctx.beginPath();
                    this._drawEdgePath(ctx, fromNode.x, fromNode.y, toNode.x, toNode.y, curved);
                    ctx.stroke();
                }
            }

            // === PASS 4: Chain edges for hardPrereqs on selected node ===
            // LOD: Skip in MINIMAL and SIMPLE tiers (chains are expensive + low visibility)
            if (this._lodTier === 'full' && this.selectedNode && this.selectedNode.hardPrereqs && this.selectedNode.hardPrereqs.length > 0) {
                var selNode = this.selectedNode;

                for (var li = 0; li < selNode.hardPrereqs.length; li++) {
                    var hpId = selNode.hardPrereqs[li];
                    var hpNode = this._nodeMap ? this._nodeMap.get(hpId) : null;
                    if (!hpNode) continue;

                    // Chain goes FROM hardPrereq TO selected node
                    var fromNode = hpNode;
                    var toNode = selNode;

                    // Viewport culling
                    if (fromNode.x < viewLeft && toNode.x < viewLeft) continue;
                    if (fromNode.x > viewRight && toNode.x > viewRight) continue;
                    if (fromNode.y < viewTop && toNode.y < viewTop) continue;
                    if (fromNode.y > viewBottom && toNode.y > viewBottom) continue;

                    var dx = toNode.x - fromNode.x;
                    var dy = toNode.y - fromNode.y;
                    var dist = Math.sqrt(dx * dx + dy * dy);
                    if (dist < 1) continue;

                    // Chain-link parameters
                    var linkW = 5;       // Link width (along chain)
                    var linkH = 3.2;     // Link height (perpendicular)
                    var linkSpacing = linkW * 1.15; // Center-to-center distance
                    var numLinks = Math.max(3, Math.round(dist / linkSpacing));
                    var angle = Math.atan2(dy, dx);

                    // Lazy-create chain link sprite (once, ~12x10px offscreen canvas)
                    if (!this._chainSprite) {
                        var linkThick = 1.8;
                        var pad = Math.ceil(linkThick) + 1;
                        var sprW = Math.ceil(linkW + pad * 2); if (sprW % 2 !== 0) sprW++;
                        var sprH = Math.ceil(linkH + pad * 2); if (sprH % 2 !== 0) sprH++;
                        var sc = document.createElement('canvas'); sc.width = sprW; sc.height = sprH;
                        var sctx = sc.getContext('2d');
                        var scx = sprW / 2, scy = sprH / 2;
                        var hw = linkW * 0.5, hh = linkH * 0.5, cr = Math.min(hw, hh) * 0.8;
                        sctx.beginPath();
                        sctx.moveTo(scx-hw+cr, scy-hh); sctx.lineTo(scx+hw-cr, scy-hh);
                        sctx.arcTo(scx+hw, scy-hh, scx+hw, scy-hh+cr, cr); sctx.lineTo(scx+hw, scy+hh-cr);
                        sctx.arcTo(scx+hw, scy+hh, scx+hw-cr, scy+hh, cr); sctx.lineTo(scx-hw+cr, scy+hh);
                        sctx.arcTo(scx-hw, scy+hh, scx-hw, scy+hh-cr, cr); sctx.lineTo(scx-hw, scy-hh+cr);
                        sctx.arcTo(scx-hw, scy-hh, scx-hw+cr, scy-hh, cr); sctx.closePath();
                        sctx.fillStyle = 'rgba(130, 130, 140, 0.6)';
                        sctx.strokeStyle = 'rgba(80, 80, 90, 0.9)';
                        sctx.lineWidth = linkThick; sctx.fill(); sctx.stroke();
                        var ihw = hw * 0.45, ihh = hh * 0.35, ir = Math.min(ihw, ihh) * 0.6;
                        sctx.beginPath();
                        sctx.moveTo(scx-ihw+ir, scy-ihh); sctx.lineTo(scx+ihw-ir, scy-ihh);
                        sctx.arcTo(scx+ihw, scy-ihh, scx+ihw, scy-ihh+ir, ir); sctx.lineTo(scx+ihw, scy+ihh-ir);
                        sctx.arcTo(scx+ihw, scy+ihh, scx+ihw-ir, scy+ihh, ir); sctx.lineTo(scx-ihw+ir, scy+ihh);
                        sctx.arcTo(scx-ihw, scy+ihh, scx-ihw, scy+ihh-ir, ir); sctx.lineTo(scx-ihw, scy-ihh+ir);
                        sctx.arcTo(scx-ihw, scy-ihh, scx-ihw+ir, scy-ihh, ir); sctx.closePath();
                        sctx.fillStyle = 'rgba(20, 20, 30, 0.7)'; sctx.fill();
                        this._chainSprite = sc;
                    }
                    var spr = this._chainSprite;
                    var sprHW = spr.width / 2, sprHH = spr.height / 2;

                    ctx.globalAlpha = 0.8;

                    // Draw chain links using pre-rendered sprite
                    for (var cl = 0; cl < numLinks; cl++) {
                        var t = (cl + 0.5) / numLinks;
                        var cx = fromNode.x + dx * t;
                        var cy = fromNode.y + dy * t;

                        ctx.save();
                        ctx.translate(cx, cy);
                        ctx.rotate(angle);
                        if (cl % 2 !== 0) ctx.rotate(Math.PI / 2);
                        ctx.drawImage(spr, -sprHW, -sprHH);
                        ctx.restore();
                    }
                }
            }

            ctx.globalAlpha = 1.0;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
