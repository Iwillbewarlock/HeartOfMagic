/**
 * CanvasRenderer - a frame: render(), the background, and the tree layer - when
 * it is pasted, slid, stretched, shifted (LayerScroll), built over frames
 * (LayerBuild) or repainted whole (_drawTree) - and what goes into it
 * (_renderTreeInto: dividers, lines, spells, bridges, names, chapter titles).
 * See docs/DESIGN.md, "Performance: the tree layer and the developer read-out".
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.render() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, TreeStyle, Starfield, StaticBase, FxLayer,
 * LayerScroll, LayerBuild, HoverOverlay, BridgeView, EditMode, PerfMeter
 * (all optional but CanvasRenderer and TreeStyle)
 */

(function() {
    var methods = {
        /** A frame: background, the tree (its layer), the heart and the moving parts. */
        render: function() {
            if (!this.ctx || !this.canvas) return;

            var startTime = performance.now();
            this._frameStartAt = startTime;          // LayerBuild fills what is left of the frame
            var ctx = this.ctx;
            var width = this._width || 800;
            var height = this._height || 600;

            if (width === 0 || height === 0) return;

            var cx = width / 2;
            var cy = height / 2;
            var dpr = window.devicePixelRatio || 1;

            // Compute LOD tier based on zoom
            this._lodTier = this._computeLODTier();

            // DPR reduction in MINIMAL tier (halve pixel work on HiDPI)
            var effectiveDpr = dpr;
            if (this._lodTier === 'minimal' && dpr > 1) {
                effectiveDpr = 1;
            }
            // Only resize canvas buffer when effective DPR changes (avoids per-frame resize)
            if (this._activeDpr !== effectiveDpr) {
                this._activeDpr = effectiveDpr;
                this.canvas.width = Math.round(width * effectiveDpr);
                this._mainShownKey = null;
                this.canvas.height = Math.round(height * effectiveDpr);
                // CSS size stays the same — browser upscales
            }
            dpr = effectiveDpr;

            // Moving parts on their own canvases this frame (FxLayer)?
            this._fxThisFrame = this.USE_FX_LAYER && typeof FxLayer !== 'undefined' && FxLayer.ready(this.canvas);
            if (this._fxThisFrame) {
                FxLayer.begin(this.canvas);
            } else {
                this._mainShownKey = null;
                if (typeof FxLayer !== 'undefined') FxLayer.hideAll();
            }

            // The background: drawn here, or - when it is still - by the tree
            // pass, which may paste it together with the tree (StaticBase)
            var bgPending = typeof StaticBase !== 'undefined' && StaticBase.backgroundStill(this);
            if (bgPending) {
                ctx.setTransform(1, 0, 0, 1, 0, 0);
                ctx.globalAlpha = 1.0;
                ctx.globalCompositeOperation = 'source-over';
                ctx.scale(dpr, dpr);
            } else {
                this._drawBackground(ctx, dpr, width, height);
            }

            // Calculate rotation values
            var rotRad = this.rotation * Math.PI / 180;
            var cos = Math.cos(rotRad);
            var sin = Math.sin(rotRad);

            // Calculate view bounds in WORLD coordinates (accounting for pan)
            // The view center in world space is at (-panX/zoom, -panY/zoom) before rotation
            var viewCenterX = -this.panX / this.zoom;
            var viewCenterY = -this.panY / this.zoom;

            // Undo rotation to get world-space view center
            var worldCenterX = viewCenterX * cos + viewCenterY * sin;
            var worldCenterY = -viewCenterX * sin + viewCenterY * cos;

            // View extent in world units (add generous padding)
            var viewExtent = Math.max(cx, cy) / this.zoom + 500;
            var viewLeft = worldCenterX - viewExtent;
            var viewRight = worldCenterX + viewExtent;
            var viewTop = worldCenterY - viewExtent;
            var viewBottom = worldCenterY + viewExtent;

            // =====================================================================
            // THE TREE: drawn into its own layer when something changed, otherwise
            // the layer is pasted as it is (see _drawTree)
            // =====================================================================
            this._drawTree(ctx, dpr, {
                cx: cx, cy: cy, rotRad: rotRad, cos: cos, sin: sin,
                viewLeft: viewLeft, viewRight: viewRight, viewTop: viewTop, viewBottom: viewBottom
            }, bgPending);

            // =====================================================================
            // RENDER CENTER HUB ON TOP (does NOT rotate with wheel) - with heartbeat
            // =====================================================================
            this._renderHubAndFinish(ctx, cx, cy, startTime);
            if (this._fxThisFrame) FxLayer.end();
        },

        /**
         * Background colour, then the design's page or the starfield. Leaves ctx
         * DPR-scaled. ctx is the screen or StaticBase's picture (same size).
         */
        _drawBackground: function(ctx, dpr, width, height) {
            // FULL RESET - prevent ghosting
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.globalAlpha = 1.0;
            ctx.globalCompositeOperation = 'source-over';

            // Clear the ENTIRE canvas buffer (including offscreen areas)
            ctx.fillStyle = this._bgColor || '#000000';
            ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);

            // Scale for DPR
            ctx.scale(dpr, dpr);

            // =====================================================================
            // RENDER STARFIELD BACKGROUND (behind everything)
            // =====================================================================
            // A design preset with a page draws it here instead of the stars
            var pageDrawn = TreeStyle.renderPage(ctx, width, height);
            if (!pageDrawn && this._starfieldEnabled && typeof Starfield !== 'undefined') {
                // Apply settings
                Starfield.setColor(this._starfieldColor || '#ffffff');
                Starfield.maxSize = this._starfieldMaxSize || 2.5;
                // Compared with the defaults applied: an unset seed or density
                // used to count as changed on every frame and re-roll the stars
                var starSeed = this._starfieldSeed || 42;
                var starCount = this._starfieldDensity || 200;
                if (Starfield.seed !== starSeed) {
                    Starfield.seed = starSeed;
                    Starfield.stars = null;  // Force reinit with new seed
                }
                if (Starfield.starCount !== starCount) {
                    Starfield.starCount = starCount;
                    Starfield.stars = null;  // Force reinit
                }

                // Held still (twinkle off, or "still everything"): drawn, not moved
                Starfield.still = this._starsStill === true;

                // Render - either fixed to screen or world-space (seed-based)
                if (this._starfieldFixed) {
                    // Fixed mode: screen-space stars that drift
                    if (!Starfield.stars || Starfield.width !== this._width || Starfield.height !== this._height) {
                        Starfield.init(this._width, this._height);
                    }
                    Starfield.render(ctx);
                } else {
                    // World-space: deterministic tile-based stars from seed
                    Starfield.renderWorldSpace(ctx, this.panX, this.panY, this.zoom, this._width, this._height);
                }
                // Keep animation running (throttled) - not for stars held still
                if (!Starfield.still) this._requestAnimationOnlyFrame();
            }
        },

        /**
         * Everything that only changes when the player does something: dividers,
         * edges, nodes, bridges, labels. About 3,300 paint calls for 1440 spells
         * before the spells were batched (NodeBatch; ~800 since), and the heart in the middle used to make all of it be redrawn 20 times a
         * second just to beat. Now it is drawn once into a see-through layer and the
         * layer is pasted until `_treeDirty` says the tree changed. See-through,
         * because the starfield behind it keeps moving.
         */
        _drawTree: function(ctx, dpr, view, bgPending) {
            var self = this;
            var drawBackground = function(c) { self._drawBackground(c, dpr, self._width || 800, self._height || 600); };
            var layer = this.USE_TREE_LAYER ? this._ensureTreeLayer(dpr) : null;
            if (!layer) {
                if (bgPending) drawBackground(ctx);
                this._renderTreeInto(ctx, view);
                this._treeDirty = false;
                this._mainShownKey = null;
                this._drawMoving(ctx, view);
                return;
            }

            // The layer is drawn with a margin all round, so a drag can slide it
            // instead of redrawing all 1440 spells every frame. It is redrawn
            // only when the tree changed, when zoom or rotation moved, or when
            // the drag has gone past the margin and would show its bare edge.
            var margin = this.TREE_LAYER_MARGIN;
            var dx = this.panX - this._layerPanX;
            var dy = this.panY - this._layerPanY;
            var slid = Math.abs(dx) > margin || Math.abs(dy) > margin;
            var viewTurned = this._layerZoom !== this.zoom || this._layerRotation !== this.rotation;
            // While the camera glides, the wheel turns or the wheel rotates, zoom
            // and rotation change every frame. The layer is not repainted for each
            // of them (a click's focus zoom was ~27 repaints of every spell): it is
            // pasted stretched and turned to the new view, and repainted once the
            // motion stops (the frame after it asks for a repaint by itself).
            // A tree change during the motion waits for it too (it stays dirty): a
            // click selects a spell and starts the focus glide on the same frame,
            // and repainting the tree for the old view there was a whole repaint
            // thrown away when the glide ended. The selection shows as it stops.
            var stretch = (viewTurned || this._treeDirty) && !this._treeLayerStale &&
                this._layerZoom > 0 && this._viewInMotion();
            // A whole repaint in progress over several frames (LayerBuild): dropped
            // if the view it was for is gone (the camera moves on without a known
            // end, a resize); the change it carried is marked again
            var hasBuild = typeof LayerBuild !== 'undefined';
            var building = hasBuild && LayerBuild.active();
            if (building && (this._treeLayerStale || !LayerBuild.valid(this, dpr, layer) || (stretch && !LayerBuild.forGlide()))) {
                LayerBuild.abort(this);
                building = false;
            }
            // The tree keeps changing faster than a build ends (LayerBuild.MAX_RESTARTS
            // started again in a row): the build in hand goes and the tree is repainted
            // at once below
            if (building && this._treeDirty && !stretch && LayerBuild.overRestarts()) {
                LayerBuild.abort(this, true);        // the count stays: a repaint at once follows
                building = false;
            }
            // A camera glide says where it ends: the tree is built for that view while
            // it glides, so it is ready when the camera arrives (a click's highlight
            // included); a change during the glide starts it again
            var glide = this._glideTarget;
            if (stretch && glide && hasBuild && (!building || this._treeDirty) && LayerBuild.wanted(this, layer) &&
                    (this._treeDirty || glide.zoom !== this._layerZoom || glide.rotation !== this._layerRotation ||
                     Math.abs(glide.panX - this._layerPanX) > margin * LayerScroll.EARLY_SHARE ||
                     Math.abs(glide.panY - this._layerPanY) > margin * LayerScroll.EARLY_SHARE) &&
                    LayerBuild.start(this, dpr, margin, view, glide)) {
                this._treeDirty = false;
                building = true;
            }
            // A drag with nothing else changed: once it has used half the margin the
            // layer is shifted and the uncovered strips drawn - those on screen at
            // once, the others in the frame's time left (LayerScroll) - a few ms
            // instead of a 25-60 ms repaint of the whole tree in the middle of the drag
            var scrolled = !stretch && !building && !this._treeDirty && !this._treeLayerStale && !viewTurned &&
                !(typeof EditMode !== 'undefined' && EditMode.isActive) &&
                typeof LayerScroll !== 'undefined' && LayerScroll.step(this, dpr, margin, view);
            if (scrolled) {
                layer = this._treeLayer;
                dx = this.panX - this._layerPanX;
                dy = this.panY - this._layerPanY;
            } else if (!stretch && (!building || this._treeDirty) &&
                    (this._treeDirty || this._treeLayerStale || slid || viewTurned) &&
                    !this._treeLayerStale && hasBuild && LayerBuild.wanted(this, layer) &&
                    LayerBuild.start(this, dpr, margin, view)) {
                // Spread over the next frames (the old picture stays up meanwhile);
                // a new change restarts it with the latest tree
                this._treeDirty = false;
                building = true;
            } else if (!stretch && !building && (this._treeDirty || this._treeLayerStale || slid || viewTurned)) {
                if (typeof LayerScroll !== 'undefined') LayerScroll.reset();
                var syncStart = performance.now();
                var lctx = this._treeLayerCtx;
                lctx.setTransform(1, 0, 0, 1, 0, 0);
                lctx.globalAlpha = 1.0;
                lctx.globalCompositeOperation = 'source-over';
                lctx.clearRect(0, 0, layer.width, layer.height);
                lctx.scale(dpr, dpr);
                lctx.translate(margin, margin);
                // Whatever lies in the margin must be drawn too, not culled
                var extra = margin / this.zoom;
                var drawLayer = function() {
                    self._renderTreeInto(lctx, {
                        cx: view.cx, cy: view.cy, rotRad: view.rotRad, cos: view.cos, sin: view.sin,
                        viewLeft: view.viewLeft - extra, viewRight: view.viewRight + extra,
                        viewTop: view.viewTop - extra, viewBottom: view.viewBottom + extra,
                        labelMargin: margin
                    });
                };
                // The layer does not show the hover: HoverOverlay paints it on top,
                // so moving the cursor over the tree never repaints every spell
                if (typeof HoverOverlay !== 'undefined') HoverOverlay.withoutHover(this, drawLayer);
                else drawLayer();
                this._treeDirty = false;
                this._treeLayerStale = false;
                this._layerPanX = this.panX;
                this._layerPanY = this.panY;
                this._layerZoom = this.zoom;
                this._layerRotation = this.rotation;
                this._treeLayerDraws = (this._treeLayerDraws || 0) + 1;
                if (typeof LayerBuild !== 'undefined') LayerBuild.noteSync(performance.now() - syncStart);
                dx = 0;
                dy = 0;
            }
            // Held still past the old picture's margin (a jump without a glide, a
            // drag meanwhile) its bare edge shows: the build goes on urgently (the
            // pieces on screen first, a bigger share of the frame, swapped in as
            // soon as they are done; LayerBuild.step). A smaller old picture after
            // a zoom out keeps its bare border a few frames more, as it had during
            // the zoom; so does a glide's build that is not quite done as the camera
            // arrives (it showed so during the glide).
            var urgent = building && slid && !this._viewInMotion() && !LayerBuild.forGlide();
            // Pieces an urgent build swapped in without wait in LayerScroll's queue. A
            // frame that scrolls draws them (LayerScroll.step above); a stretched one (a
            // wheel zoom, a glide) draws them here, into the layer on screen. Not with a
            // build under way (its pieces have the frame, and it replaces that layer)
            // nor with the tree changed since (they would show the new tree in the old
            // picture): they wait, and a build's swap drops them
            if (!scrolled && !building && !this._treeDirty && !this._treeLayerStale && typeof LayerScroll !== 'undefined' &&
                    LayerScroll._pending.length && !(typeof EditMode !== 'undefined' && EditMode.isActive)) {
                LayerScroll.drawPendingAside(this, dpr, margin, view);
            }
            if (building) {
                if (LayerBuild.step(this, this._frameStartAt || performance.now(), urgent)) {
                    layer = this._treeLayer;
                    dx = this.panX - this._layerPanX;
                    dy = this.panY - this._layerPanY;
                    slid = Math.abs(dx) > margin || Math.abs(dy) > margin;
                    viewTurned = this._layerZoom !== this.zoom || this._layerRotation !== this.rotation;
                }
                // The layer mapped onto the view as during a glide (moved, stretched,
                // turned): the old picture while building, the new one if it was built
                // for a glide's end or the drag has gone on past its margin
                stretch = stretch || viewTurned || slid;
            }

            var w = this.canvas.width, h = this.canvas.height;
            // With a spell hovered: the preview on its own spot (FxLayer frame), else
            // the layer with the preview on it, cached until the hover or the layer
            // changes (HoverOverlay.composite)
            var hoverSpot = this._fxThisFrame && typeof HoverOverlay !== 'undefined' && HoverOverlay.drawSpot;
            // (not onto a stretched layer: its preview would be drawn for the wrong view)
            var src = (!hoverSpot && !stretch && typeof HoverOverlay !== 'undefined') ? HoverOverlay.composite(this, layer, dpr, margin, view) : layer;
            var offX = Math.round((margin - dx) * dpr), offY = Math.round((margin - dy) * dpr);
            var paste = function(c) {
                c = c || ctx;
                c.save();
                c.setTransform(1, 0, 0, 1, 0, 0);
                c.globalAlpha = 1.0;
                if (stretch) {
                    // Map the layer's view (zoom, rotation, pan it was drawn at) onto this one
                    var k = self.zoom / self._layerZoom;
                    c.translate(dpr * (view.cx + self.panX), dpr * (view.cy + self.panY));
                    c.rotate((self.rotation - self._layerRotation) * Math.PI / 180);
                    c.scale(k, k);
                    c.translate(-dpr * (margin + view.cx + self._layerPanX), -dpr * (margin + view.cy + self._layerPanY));
                    c.drawImage(src, 0, 0);
                } else {
                    c.drawImage(src, offX, offY, w, h, 0, 0, w, h);
                }
                c.restore();
            };
            // A still background and a still tree: one picture of both (StaticBase).
            // With the moving parts on their own canvases (FxLayer), a tree canvas
            // that already shows that picture is not touched at all.
            var pasted = false;
            var shownKey = null;
            if (bgPending && !stretch) {
                var layerKey = (src === layer ? 'layer' : 'hover|' + HoverOverlay._key) + '|' +
                    (this._treeLayerDraws || 0) + '|' + offX + ',' + offY;
                var mainKey = StaticBase.keyFor(this, layerKey);
                if (this._fxThisFrame && !this._learningPath && mainKey === this._mainShownKey) {
                    pasted = true;
                } else {
                    pasted = StaticBase.draw(ctx, this, layerKey, drawBackground, paste);
                }
                // The learning path is drawn over it this frame: not the plain picture
                if (pasted && this._fxThisFrame && !this._learningPath) shownKey = mainKey;
            }
            this._mainShownKey = shownKey;
            if (!pasted) {
                if (bgPending) drawBackground(ctx);
                paste();
            }

            if (hoverSpot) HoverOverlay.drawSpot(this, view, dpr);

            // Selection sigil, learning glow and particles move every frame, so
            // they sit on top of the layer (on their own canvases with FxLayer)
            this._drawMoving(ctx, view);
        },

        /** The camera, a wheel rotation or the mouse wheel is moving the view right now. */
        _viewInMotion: function() {
            return this.isAnimating || (performance.now() - (this._wheelAt || 0)) < this.WHEEL_SETTLE_MS;
        },

        /** The layer canvas: the visible one plus the margin all round. Null if it cannot be made. */
        _ensureTreeLayer: function(dpr) {
            if (this._treeLayerFailed) return null;
            try {
                if (!this._treeLayer) {
                    this._treeLayer = document.createElement('canvas');
                    this._treeLayerCtx = this._treeLayer.getContext('2d');
                    if (!this._treeLayerCtx) throw new Error('no 2d context');
                }
                var pad = Math.ceil(2 * this.TREE_LAYER_MARGIN * (dpr || 1));
                if (this._treeLayer.width !== this.canvas.width + pad || this._treeLayer.height !== this.canvas.height + pad) {
                    this._treeLayer.width = this.canvas.width + pad;
                    this._treeLayer.height = this.canvas.height + pad;
                    this._treeLayerStale = true;
                }
                return this._treeLayer;
            } catch (e) {
                console.warn('[CanvasRenderer] Tree layer unavailable, drawing directly: ' + e.message);
                this._treeLayerFailed = true;
                return null;
            }
        },

        _renderTreeInto: function(ctx, view) {
            var cx = view.cx, cy = view.cy, rotRad = view.rotRad, cos = view.cos, sin = view.sin;
            var viewLeft = view.viewLeft, viewRight = view.viewRight, viewTop = view.viewTop, viewBottom = view.viewBottom;

            // =====================================================================
            // RENDER ROTATING ELEMENTS FIRST (dividers, edges, nodes)
            // =====================================================================
            // Developer mode: how long each part of a repaint takes (PerfMeter.part)
            var pm = typeof PerfMeter !== 'undefined' && PerfMeter.isOn();
            this._partAt = pm ? performance.now() : 0;

            ctx.save();
            ctx.translate(cx + this.panX, cy + this.panY);
            ctx.rotate(rotRad);  // Apply wheel rotation
            ctx.scale(this.zoom, this.zoom);

            // Dividers, edges and nodes
            this._drawTreeShapes(ctx, viewLeft, viewRight, viewTop, viewBottom);

            // Cross school bridges and the trait filter, on top of the nodes
            if (typeof BridgeView !== 'undefined') {
                BridgeView.render(ctx, this, {
                    left: viewLeft, right: viewRight, top: viewTop, bottom: viewBottom
                });
            }
            if (pm) this._partAt = PerfMeter.part('bridges', this._partAt);

            // Edit mode overlay (pen line, eraser path)
            if (typeof EditMode !== 'undefined' && EditMode.isActive) {
                EditMode.renderOverlay(ctx);
            }

            ctx.restore();

            // =====================================================================
            // RENDER LABELS (screen-aligned, do NOT rotate with wheel)
            // Part of the layer: they only move when the tree does. They used to be
            // drawn after the hub; now the hub sits over any label that reaches it.
            // =====================================================================
            if (!view.noLabels) this.renderLabels(ctx, cx, cy, cos, sin, view.labelMargin || 0);
            if (pm) this._partAt = PerfMeter.part('labels', this._partAt);

            // Chapter titles: school names past each school's outer edge (design preset)
            if (!view.noChapters) TreeStyle.renderChapters(ctx, this, cx, cy, cos, sin, view.labelMargin || 0);
            if (pm) {
                this._partAt = PerfMeter.part('chapters', this._partAt);
                // The browser may only rasterize the calls when the canvas is read
                // or painted: a one-pixel read makes it do it here, to be timed
                try { ctx.getImageData(0, 0, 1, 1); } catch (e) { /* tainted or no support: untimed */ }
                PerfMeter.part('raster', this._partAt);
            }
        },

        /** School dividers, debug grid, edges and nodes (world coordinates, rotated context). */
        _drawTreeShapes: function(ctx, viewLeft, viewRight, viewTop, viewBottom) {
            var pm = this._partAt > 0;
            this.renderSchoolDividers(ctx);
            this.renderDebugGrid(ctx);          // behind edges and nodes
            if (pm) this._partAt = PerfMeter.part('dividers', this._partAt);
            // No drawn lines in edit mode: a dragged spell would change its hand-drawn shape every frame
            TreeStyle.beginInk(this._lodTier === 'full' && !(typeof EditMode !== 'undefined' && EditMode.isActive), this.zoom);
            this.renderEdges(ctx, viewLeft, viewRight, viewTop, viewBottom);
            if (pm) this._partAt = PerfMeter.part('edges', this._partAt);
            // The learning path animation and the detached particles move every
            // frame: they are drawn over the pasted layer (_drawLearningPath, _drawTree)
            this.renderNodes(ctx, viewLeft, viewRight, viewTop, viewBottom);
            if (pm) this._partAt = PerfMeter.part('nodes', this._partAt);
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
