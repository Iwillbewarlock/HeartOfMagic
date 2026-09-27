/**
 * CanvasRenderer - what moves every frame, over the tree layer: the learning
 * path drawing itself in, the particles, the sigil and learning glow (on
 * FxLayer spots when it is on), and the heart - its beat, the globe, the rune
 * circle and the particle core.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer._renderHub() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, state (globe position), TreeStyle, FxLayer,
 * Globe3D, PerfMeter (optional but CanvasRenderer and TreeStyle)
 */

(function() {
    var methods = {
        /** The learning path drawing itself in, the particles, the sigil and the learning glow. */
        _drawMoving: function(ctx, view) {
            this._drawLearningPath(ctx, view);
            if (!this._fxThisFrame) {
                this._drawDetachedParticles(ctx, view);
                TreeStyle.renderOverlay(ctx, this, view);
                return;
            }
            var self = this;
            var dpr = this._activeDpr || window.devicePixelRatio || 1;

            // Particles: one canvas round all their trails
            var parts = typeof Globe3D !== 'undefined' ? Globe3D.detachedParticles : null;
            if (parts && parts.length) {
                var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity, pad = 0;
                for (var i = 0; i < parts.length; i++) {
                    var dp = parts[i];
                    if ((dp.size || 4) * 1.5 > pad) pad = (dp.size || 4) * 1.5;
                    for (var j = 0; j < dp.trail.length; j++) {
                        var s = this._worldToScreen(dp.trail[j].x, dp.trail[j].y, view);
                        if (s[0] < minX) minX = s[0];
                        if (s[0] > maxX) maxX = s[0];
                        if (s[1] < minY) minY = s[1];
                        if (s[1] > maxY) maxY = s[1];
                    }
                }
                if (minX <= maxX) {
                    var pp = pad * this.zoom + 3;
                    this._fxDraw('particles', minX - pp, minY - pp, maxX - minX + 2 * pp, maxY - minY + 2 * pp, dpr,
                        function(c) { self._drawDetachedParticles(c, view); });
                }
            }

            // Sigil and learning glow: one small canvas per spell
            var marks = TreeStyle.overlayNodes(this);
            if (marks.length) {
                var reach = TreeStyle.overlayExtent(this) * this.zoom + 3;
                marks.forEach(function(node) {
                    var p = self._worldToScreen(node.x, node.y, view);
                    self._fxDraw('mark:' + node.id, p[0] - reach, p[1] - reach, 2 * reach, 2 * reach, dpr,
                        function(c) { TreeStyle.renderOverlay(c, self, view, node); });
                });
            }
        },

        /**
         * FxLayer.draw; when no spot can be had, drawn on the tree canvas instead
         * (which then no longer shows the plain picture). Returns whether drawFn ran.
         */
        _fxDraw: function(key, x, y, w, h, dpr, drawFn) {
            if (FxLayer.draw(key, x, y, w, h, dpr, drawFn)) return FxLayer.lastDrawn;
            this._mainShownKey = null;
            drawFn(this.ctx);
            return true;
        },

        /** Where a point of the (turning) tree is on the canvas, in CSS px. */
        _worldToScreen: function(x, y, view) {
            var z = this.zoom;
            return [view.cx + this.panX + z * (x * view.cos - y * view.sin),
                    view.cy + this.panY + z * (x * view.sin + y * view.cos)];
        },

        /** How far (world units) the heart, its runes and the globe's scattering dots reach from its centre. */
        _hubExtent: function() {
            var globeData = (state.treeData && state.treeData.globe) || { radius: 45 };
            var base = (typeof renderValue === 'function' ? renderValue('globeSize', 0) : 0) || globeData.radius || 45;
            var globe = (this._globeEnabled && typeof Globe3D !== 'undefined') ? (Globe3D.radius || 0) : 0;
            return (Math.max(base, globe) + 45) * 1.1;
        },

        /**
         * The path growing towards a newly set learning target. It moves every
         * frame, so it is painted over the pasted layer: drawn into the layer it
         * cost a repaint of every spell per frame - and the flag it set for that
         * was cleared by the repaint itself, so the animation stood still on its
         * first frame. The layer leaves the animating path out until it ends
         * (_animatingPathNodes), then is repainted once with the whole path.
         */
        _drawLearningPath: function(ctx, view) {
            if (!this._learningPath) return;
            ctx.save();
            ctx.translate(view.cx + this.panX, view.cy + this.panY);
            ctx.rotate(view.rotRad);
            ctx.scale(this.zoom, this.zoom);
            this.renderLearningPath(ctx);
            ctx.restore();
        },

        /**
         * The particles a learning animation sends out from the globe. They move
         * every frame, so they are painted over the finished tree rather than into
         * it - inside the layer, anything moving costs a full redraw of all 1440
         * spells every frame. The price is that they now sit over the nodes
         * instead of under them. Drawn on both paths, layer or no layer.
         */
        _drawDetachedParticles: function(ctx, view) {
            if (typeof Globe3D === 'undefined' || !Globe3D.detachedParticles || Globe3D.detachedParticles.length === 0) return;
            ctx.save();
            ctx.translate(view.cx + this.panX, view.cy + this.panY);
            ctx.rotate(view.rotRad);
            ctx.scale(this.zoom, this.zoom);
            Globe3D._renderDetachedParticles(ctx);
            ctx.restore();
        },

        _renderHubAndFinish: function(ctx, cx, cy, startTime) {
            if (this._fxThisFrame) {
                var self = this;
                var g = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };
                var half = this._hubExtent() * this.zoom + 2;
                var hx = cx + this.panX + (g.x || 0) * this.zoom, hy = cy + this.panY + (g.y || 0) * this.zoom;
                var drawn = this._fxDraw('hub', hx - half, hy - half, 2 * half, 2 * half, this._activeDpr || window.devicePixelRatio || 1,
                    function(c) { self._renderHub(c, cx, cy); });
                // Off screen it is not drawn, but it still beats: the beat sends
                // the particles out and asks for the frames everything else moves in
                if (!drawn) this._hubOffscreen();
            } else {
                this._renderHub(ctx, cx, cy);
            }

            var elapsed = performance.now() - startTime;
            if (typeof PerfMeter !== 'undefined') PerfMeter.frame(elapsed);
            // No per-frame log here. It used to fire on every frame over 16 ms,
            // and in the game's browser a console call crosses into native code -
            // so a tree that was already too slow logged itself slower still.
            // PerfMeter above is the read-out; _logNextRender asks for one line.
            if (this._logNextRender) {
                console.log('[CanvasRenderer] Render:', Math.round(elapsed) + 'ms,', this.nodes.length, 'nodes');
                this._logNextRender = false;
            }
        },

        /**
         * Advance the heartbeat (by time), fire the beat's effects (globe scatter,
         * particle core flash, a particle for each learning path) and ask for the
         * next animation frame. Returns the pulse (0 .. ~0.08). Runs whether or not
         * the heart is drawn (off screen, FxLayer).
         */
        _heartBeat: function() {
            // Heartbeat animation - pulsing scale with configurable delay between pulse groups
            var pulse = 0;
            if (this._heartAnimationEnabled) {
                // Advanced by time, not per frame: a drag or the camera asking for
                // full-rate frames used to make the heart race. HEARTBEAT_FRAME_MS is
                // the frame the speed setting was tuned at (the old 20-a-second rate).
                var beatNow = performance.now();
                var beatDt = this._lastBeatAt ? Math.min(beatNow - this._lastBeatAt, this.HEARTBEAT_MAX_STEP_MS) : this.HEARTBEAT_FRAME_MS;
                this._lastBeatAt = beatNow;
                this._heartbeatPhase += this._heartbeatSpeed * beatDt / this.HEARTBEAT_FRAME_MS;

                // Heartbeat animation: double beat (systole-diastole) then delay
                // pulse_speed controls how fast each beat is
                // pulse_delay controls the pause between heartbeat groups

                // Convert delay from seconds to "phase units" at 60fps
                // At speed 0.06 and 60fps, one second = 0.06 * 60 = 3.6 phase units
                var phasePerSecond = this._heartbeatSpeed * 60;
                var pulseDelay = (this._heartPulseDelay || 2.0) * phasePerSecond;
                var beatDuration = Math.PI;  // The double-beat takes PI radians
                var cycleLength = beatDuration + pulseDelay;
                var cyclePos = this._heartbeatPhase % cycleLength;

                // Only pulse during the "beat" part of the cycle (first PI radians)
                if (cyclePos < beatDuration) {
                    // Double-beat pattern: quick pulse, pause, quick pulse
                    var beat1 = Math.max(0, Math.sin(cyclePos * 2));
                    var beat2 = Math.max(0, Math.sin(cyclePos * 2 - 0.8));
                    pulse = (beat1 + beat2 * 0.6) * 0.08;  // Max ~8% scale change
                }

                // Global rising-edge detection — fires once per beat start
                var nowBeatingGlobal = cyclePos < beatDuration && cyclePos < 0.5;
                if (nowBeatingGlobal && !this._lastHeartbeatGlobal) {
                    // Scatter globe particles on every heartbeat
                    if (typeof Globe3D !== 'undefined' && Globe3D.onHeartbeat) {
                        Globe3D.onHeartbeat(1.0);
                    }
                    // Boost particle core flash on heartbeat
                    this._coreFlashBoost = 1.0;
                    // A particle leaves for each learning path. This fired from
                    // renderEdges, which only runs when the tree layer is repainted,
                    // so with the tree still hardly any particle ever left.
                    // Not while idle: nobody is looking, and they cost frames.
                    if (!this._isIdle()) this._detachGlobeParticleToLearningPath();
                }
                this._lastHeartbeatGlobal = nowBeatingGlobal;
            }

            // Keep animation running for heartbeat or globe (throttled to reduce CPU)
            var globeEnabled = this._globeEnabled && (typeof Globe3D !== 'undefined') && Globe3D.enabled;
            if (typeof Globe3D !== 'undefined') Globe3D.still = this._globeStill === true;
            if (this._heartAnimationEnabled || (globeEnabled && !this._globeStill)) {
                this._requestAnimationOnlyFrame();  // throttleable, and the tree layer stays as it is
            }
            return pulse;
        },

        /** The heart off screen: the beat, the globe's movement and the frames, without drawing. */
        _hubOffscreen: function() {
            this._heartBeat();
            if (this._globeEnabled && typeof Globe3D !== 'undefined' && Globe3D.enabled && !Globe3D.still && Globe3D.advance) {
                Globe3D.advance();
            }
        },

        /** The heart: glow, rings, runes, text or particle core, and the globe; with the heartbeat. */
        _renderHub: function(ctx, cx, cy) {
            var globeData = (state.treeData && state.treeData.globe) || { x: 0, y: 0, radius: 45 };
            ctx.save();
            ctx.translate(cx + this.panX, cy + this.panY);
            ctx.scale(this.zoom, this.zoom);
            ctx.translate(globeData.x, globeData.y);
            // No rotation applied to hub!

            // Heartbeat: the pulse, the beat's side effects, the next frame
            var pulse = this._heartBeat();
            var scale = 1 + pulse;

            ctx.scale(scale, scale);

            // Core Size: use settings.globeSize if available, else fall back to globeData
            var baseRadius = (typeof renderValue === 'function' ? renderValue('globeSize', 0) : (typeof settings !== 'undefined' && settings.globeSize)) || globeData.radius || 45;
            var ringColor = this._heartRing();
            var bgColor = this._designOrPlayer('hubFill', 'heartBgColor', this._heartBgColor) || '#000000';

            // Parse ring color for glow
            var ringRgb = this.parseColor(ringColor);
            var glowColor = ringRgb ? 'rgba(' + ringRgb.r + ',' + ringRgb.g + ',' + ringRgb.b + ',' : 'rgba(184, 168, 120, ';

            // Outer glow ring (pulses with heartbeat)
            var glowAlpha = 0.15 + pulse * 1.5;
            ctx.beginPath();
            ctx.arc(0, 0, baseRadius + 8, 0, Math.PI * 2);
            ctx.fillStyle = glowColor + glowAlpha + ')';
            ctx.fill();

            // Background circle (dark center) - toggleable on/off
            if (this._globeBgFill) {
                ctx.beginPath();
                ctx.arc(0, 0, baseRadius, 0, Math.PI * 2);
                ctx.fillStyle = bgColor;
                ctx.fill();
            }

            // Inner decorative ring
            ctx.beginPath();
            ctx.arc(0, 0, baseRadius - 5, 0, Math.PI * 2);
            ctx.strokeStyle = glowColor + '0.3)';
            ctx.lineWidth = 1;
            ctx.stroke();

            // Outer border ring
            ctx.beginPath();
            ctx.arc(0, 0, baseRadius, 0, Math.PI * 2);
            ctx.strokeStyle = ringColor;
            ctx.lineWidth = 2.5;
            ctx.stroke();

            // Rune circle round the heart (design preset)
            TreeStyle.renderHubRunes(ctx, baseRadius);

            // Center content: particle core OR text
            if (this._particleCoreEnabled) {
                this._renderParticleCore(ctx, pulse);
            } else if (TreeStyle.renderHubEmblem(ctx, baseRadius, this.zoom)) {
                // the design's own drawing of the heart stands in for the text
            } else {
                // Globe text - use separate text color if set, supports \n for line breaks
                var textColor = this._designOrPlayer('hubText', 'magicTextColor', this._magicTextColor) || ringColor;
                var fontSize = this._globeTextSize || 16;
                var globeText = this._globeText || 'HoM';
                ctx.fillStyle = textColor;
                ctx.font = 'bold ' + fontSize + 'px sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';

                // Split by \n for multi-line support
                var lines = globeText.replace(/\\n/g, '\n').split('\n');
                var lineHeight = fontSize * 1.2;
                var totalHeight = (lines.length - 1) * lineHeight;
                var startY = -totalHeight / 2;

                for (var i = 0; i < lines.length; i++) {
                    ctx.fillText(lines[i], 0, startY + i * lineHeight);
                }
            }

            // 3D Globe particle effect (uses Globe3D module)
            if (this._globeEnabled && typeof Globe3D !== 'undefined') {
                // Use globe color if set, otherwise ring color
                var globeColor = this._designOrPlayer('globeColor', 'globeColor', this._globeColor) || ringColor;
                Globe3D.setColor(globeColor);
                Globe3D.render(ctx);
            }

            ctx.restore();
        },

        // =========================================================================
        // PARTICLE CORE (replaces center text when enabled)
        // =========================================================================

        _initParticleCore: function() {
            this._coreParticles = [];
            var count = 35;
            for (var i = 0; i < count; i++) {
                var r = Math.random() * 8;
                var angle = Math.random() * Math.PI * 2;
                this._coreParticles.push({
                    baseX: Math.cos(angle) * r,
                    baseY: Math.sin(angle) * r,
                    size: 1 + Math.random() * 1.5,
                    flashPhase: Math.random() * Math.PI * 2,
                    flashSpeed: 0.08 + Math.random() * 0.15,
                    jitterAmount: 1.5 + Math.random() * 3
                });
            }
            this._coreFrame = 0;
            this._coreFlashBoost = 0;
        },

        _renderParticleCore: function(ctx, pulse) {
            if (!this._coreParticles) this._initParticleCore();

            this._coreFrame++;

            // Decay heartbeat boost
            if (this._coreFlashBoost > 0.01) {
                this._coreFlashBoost *= 0.9;
            } else {
                this._coreFlashBoost = 0;
            }

            var boost = this._coreFlashBoost || 0;
            var jitterMult = 1 + boost * 3;    // Heartbeat amplifies jitter
            var speedMult = 1 + boost * 2;     // Heartbeat speeds up flash
            var frame = this._coreFrame;

            for (var i = 0; i < this._coreParticles.length; i++) {
                var p = this._coreParticles[i];

                // Vibrate position
                var jx = p.jitterAmount * jitterMult * (Math.random() - 0.5);
                var jy = p.jitterAmount * jitterMult * (Math.random() - 0.5);
                var x = p.baseX + jx;
                var y = p.baseY + jy;

                // Flash between black and white
                var flash = Math.sin(frame * p.flashSpeed * speedMult + p.flashPhase);
                var brightness = Math.round((flash * 0.5 + 0.5) * 255);
                var alpha = 0.6 + Math.abs(flash) * 0.4;

                // Slight size variation
                var size = p.size * (0.8 + Math.random() * 0.4);

                ctx.beginPath();
                ctx.arc(x, y, size, 0, Math.PI * 2);
                ctx.fillStyle = 'rgba(' + brightness + ',' + brightness + ',' + brightness + ',' + alpha.toFixed(2) + ')';
                ctx.fill();
            }
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
