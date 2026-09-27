/**
 * CanvasRenderer - one spell drawn by itself: the ones NodeBatch does not take
 * (selected, hovered, on the hover path, learning, with an XP ring), and the
 * ones HoverOverlay draws again over the tree. renderNode gives every state and
 * lock look; renderMysteryNode an undiscovered spell's dashed silhouette and "?".
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.renderNode() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, NodeBatch (rotationAt), TreeStyle (tokens,
 * drawHalo, strokeInnerLine)
 */

(function() {
    var methods = {
        /** An undiscovered spell: dashed silhouette, "?" kept upright. */
        renderMysteryNode: function(ctx, node) {
            var color = this._getSchoolColor(node.school);
            var dimmedColor = this.dimColor(color, 0.4);
            var size = this._minSize(this._mysterySize(node));
            var path = this._getShapePath(node.school, node);
            var contextFactor = this._contextFactor(node);

            ctx.save();
            ctx.translate(node.x, node.y);

            // Flat edge toward the centre (circles are not turned)

            var turn = NodeBatch.rotationAt(node.school, node.x, node.y);

            if (turn !== 0) ctx.rotate(turn);

            ctx.scale(size, size);

            ctx.fillStyle = TreeStyle.tokens.mysteryFill;
            ctx.strokeStyle = dimmedColor;
            ctx.lineWidth = 1 / size;
            ctx.globalAlpha = 0.6 * contextFactor;

            this._underlay(ctx, path);
            ctx.fillStyle = TreeStyle.tokens.mysteryFill;
            ctx.fill(path);
            ctx.setLineDash([0.5, 0.4]);   // Undiscovered: dashed silhouette
            ctx.stroke(path);
            ctx.setLineDash([]);

            ctx.restore();

            // Draw "?" - counter-rotate so it stays screen-aligned
            ctx.save();
            ctx.translate(node.x, node.y);

            // Counter-rotate to cancel out the wheel rotation
            var rotRad = this.rotation * Math.PI / 180;
            ctx.rotate(-rotRad);

            ctx.globalAlpha = 0.8 * contextFactor;
            ctx.fillStyle = dimmedColor;
            ctx.font = '10px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('?', 0, 0);

            ctx.restore();
            ctx.globalAlpha = 1.0;
        },

        /** A spell by itself, any state: halo, XP ring or learnable ring, lock look, shape, accents. */
        renderNode: function(ctx, node) {
            var schoolColor = node.themeColor ? TreeStyle.ink(node.themeColor) : this._getSchoolColor(node.school);
            var isSelected = this.selectedNode && this.selectedNode.id === node.id;
            var isHovered = this.hoveredNode && this.hoveredNode.id === node.id;
            var path = this._getShapePath(node.school, node);

            var size, fillColor, strokeColor, strokeWidth, alpha;
            var learningPathColor = this._learningColor();
            var ringColor = this._heartRing();  // Gold ring color for outlines
            var style = TreeStyle.tokens;
            // Check if on learning path
            var isOnLearningPath = (this._learningPathNodes instanceof Set) &&
                                   this._learningPathNodes.has(node.id);
            var isLearning = (node.state === 'learning' || node.state === 'Learning');

            // Check if this node has hard prereqs (locks) - needs gray outline treatment
            var hasLockPrereqs = node.hardPrereqs && node.hardPrereqs.length > 0;

            // During animation, hide learning path styling ONLY for nodes in the animating path
            // (other learning paths remain visible)
            var isBeingAnimated = this._animatingPathNodes && this._animatingPathNodes.has(node.id);
            if (isOnLearningPath && isBeingAnimated) {
                isOnLearningPath = false;  // Hide path styling - animation will draw this
            }

            // If this is the learning node but it's being animated, don't show learning styling yet
            // (show as 'available' until animation completes and reaches this node)
            var showLearningStyle = isLearning && !isBeingAnimated;

            if (node.state === 'unlocked') {
                size = 12;
                fillColor = style.unlockedFill || schoolColor;
                // Use ring color only if on learning path, else school color
                strokeColor = isOnLearningPath ? ringColor : (style.unlockedRim || schoolColor);
                strokeWidth = 1.5;
                alpha = 1.0;
            } else if (showLearningStyle) {
                // Learning state - cyan fill, ring color outline (only after animation completes)
                size = 12;  // Same as unlocked
                fillColor = learningPathColor;  // Cyan fill
                strokeColor = ringColor;  // Ring color outline for learning node
                strokeWidth = 1.5;
                alpha = 1.0;
            } else if (node.state === 'available' || (isLearning && isBeingAnimated)) {
                // Available nodes OR learning nodes still being animated (show as available temporarily)
                size = 9;
                fillColor = style.nodeFill;
                strokeColor = schoolColor;  // Use school/tree color for available nodes
                strokeWidth = 1;
                alpha = style.availableAlpha;
            } else {
                size = 7;
                fillColor = style.nodeFill;
                strokeColor = style.lockedStroke || schoolColor;  // Use school/tree color for locked nodes (dimmed by alpha)
                strokeWidth = 1;
                alpha = 0.4;
            }

            // Nodes on the hovered node's dependency path stand out a little
            if (this._hoverPathNodes && this._hoverPathNodes.has(node.id)) {
                alpha = Math.max(alpha, 0.85);
            }

            // Focus + context dimming and minimum on-screen size
            alpha *= this._contextFactor(node);
            size = this._minSize(size);

            if (isSelected || isHovered) {
                size += 1.5;  // Subtle hover expansion
                strokeColor = style.focusStroke;
                strokeWidth = 1.5;
                alpha = 1.0;
            }

            // Lock visual overrides for nodes with hardPrereqs
            // Not-unlocked: gray fill + small school-colored center hole
            // Unlocked: normal look + gray outline ring
            var lockGrayFill = false;
            var lockGrayOutline = false;
            if (hasLockPrereqs) {
                if (node.state === 'unlocked') {
                    lockGrayOutline = true;  // Unlocked but was locked: gray ring persists
                } else {
                    lockGrayFill = true;     // Not yet unlocked: gray body + school color hole
                }
            }

            ctx.save();
            ctx.translate(node.x, node.y);

            // Halo behind known spells and the one being learned (one sprite each)
            var glow = node.state === 'unlocked' ? style.nodeGlow : (showLearningStyle ? style.learningGlow : 0);
            // A known spell's halo is left out when small on screen (HALO_MIN_SCREEN_PX); the learning one stays
            if (node.state === 'unlocked' && size * 2.6 * this.zoom < this.HALO_MIN_SCREEN_PX) glow = 0;
            if (glow > 0) {
                TreeStyle.drawHalo(ctx, size * 2.6, showLearningStyle ? learningPathColor : schoolColor, glow * alpha);
            }

            // XP progress ring: learning nodes and partially-studied available nodes.
            // Drawn before the shape (unrotated) so the arc starts at the screen's top.
            // A learnable spell with no progress yet gets a thin ring of its own, so
            // the spells the player can go for next stand apart from the locked ones.
            if (node.state === 'learning' || node.state === 'available') {
                var ringPct = this._getNodeProgressPct(node);
                if (ringPct <= 0 && style.availableRing && node.state === 'available') {
                    ctx.lineWidth = 1.2;
                    ctx.globalAlpha = 0.6 * this._contextFactor(node);
                    ctx.strokeStyle = schoolColor;
                    ctx.beginPath();
                    ctx.arc(0, 0, size + 4, 0, Math.PI * 2);
                    ctx.stroke();
                }
                if (ringPct > 0) {
                    var ringRadius = size + 4;
                    var ringStart = -Math.PI / 2 - (this.rotation * Math.PI / 180);
                    ctx.lineWidth = 2;
                    ctx.globalAlpha = 0.22;
                    ctx.strokeStyle = style.ringTrack;
                    ctx.beginPath();
                    ctx.arc(0, 0, ringRadius, 0, Math.PI * 2);
                    ctx.stroke();

                    ctx.lineWidth = 2.5;
                    ctx.globalAlpha = 0.95;
                    ctx.strokeStyle = isLearning ? learningPathColor : schoolColor;
                    ctx.beginPath();
                    ctx.arc(0, 0, ringRadius, ringStart, ringStart + Math.PI * 2 * ringPct);
                    ctx.stroke();
                }
            }

            // Flat edge toward the centre (circles are not turned)

            var turn = NodeBatch.rotationAt(node.school, node.x, node.y);

            if (turn !== 0) ctx.rotate(turn);

            if (lockGrayFill) {
                // === LOCKED NODE WITH HARD PREREQS ===
                // Outer: gray filled shape (the "lock shell")
                var outerSize = size + 2;
                ctx.save();
                ctx.scale(outerSize, outerSize);
                this._underlay(ctx, path);
                ctx.globalAlpha = Math.min(alpha + 0.2, 0.75);
                ctx.fillStyle = this.LOCK_SHELL_FILL;
                ctx.strokeStyle = this.LOCK_SHELL_STROKE;
                ctx.lineWidth = 1.2 / outerSize;
                ctx.fill(path);
                ctx.stroke(path);
                ctx.restore();

                // Inner: small school-colored center hole
                var holeSize = Math.max(size * 0.45, 3);
                ctx.scale(holeSize, holeSize);
                ctx.globalAlpha = Math.min(alpha + 0.15, 0.65);
                ctx.fillStyle = schoolColor;
                ctx.fill(path);
            } else if (lockGrayOutline) {
                // === UNLOCKED NODE WITH HARD PREREQS ===
                // Gray outline ring behind the normal shape
                var ringSize = size + 3;
                ctx.save();
                ctx.scale(ringSize, ringSize);
                ctx.globalAlpha = 0.5;
                ctx.fillStyle = this.LOCK_RING_FILL;
                ctx.strokeStyle = this.LOCK_RING_STROKE;
                ctx.lineWidth = 1.5 / ringSize;
                ctx.fill(path);
                ctx.stroke(path);
                ctx.restore();

                // Normal unlocked node on top
                ctx.scale(size, size);
                ctx.globalAlpha = alpha;
                ctx.fillStyle = fillColor;
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = strokeWidth / size;
                ctx.fill(path);
                ctx.stroke(path);
                TreeStyle.strokeInnerLine(ctx, path, size, this._backdrop());

                // Inner accent
                ctx.scale(0.5, 0.5);
                ctx.fillStyle = style.unlockedCore || this.getInnerAccentColor(schoolColor);
                ctx.fill(path);
            } else {
                // === NORMAL NODE (no lock prereqs) ===
                ctx.scale(size, size);
                if (alpha < 1) this._underlay(ctx, path);
                ctx.globalAlpha = alpha;
                ctx.fillStyle = fillColor;
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = strokeWidth / size;
                ctx.fill(path);
                // State is encoded in the outline too (not only color): locked = dashed
                if (node.state === 'locked') ctx.setLineDash([0.5, 0.4]);
                ctx.stroke(path);
                ctx.setLineDash([]);

                // Draw inner accent for unlocked nodes
                if (node.state === 'unlocked') {
                    TreeStyle.strokeInnerLine(ctx, path, size, this._backdrop());
                    ctx.scale(0.5, 0.5);
                    ctx.fillStyle = style.unlockedCore || this.getInnerAccentColor(schoolColor);
                    ctx.fill(path);
                }

                // Draw white center for learning node
                if (isLearning) {
                    ctx.scale(0.4, 0.4);
                    ctx.fillStyle = '#ffffff';
                    ctx.fill(path);
                }
            }

            ctx.restore();
            ctx.globalAlpha = 1.0;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
