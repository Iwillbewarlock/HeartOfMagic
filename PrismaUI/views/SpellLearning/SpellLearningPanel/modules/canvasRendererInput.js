/**
 * CanvasRenderer - input: the canvas's mouse and wheel events, screen to world,
 * finding the spell (or the heart) under the cursor, hover and the tooltip.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.onClick() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, state, TreeCamera, PerfMeter,
 * DetailsPeek, HoverOverlay, EditMode (all optional but CanvasRenderer)
 */

(function() {
    var methods = {
        // =========================================================================
        // SELF-CONTAINED TOOLTIP (no WheelRenderer dependency)
        // =========================================================================

        _showTooltip: function(node, event) {
            var tooltip = document.getElementById('tooltip');
            if (!tooltip) return;

            // Progressive reveal logic (same as WheelRenderer/details panel)
            var _tCanonId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
            var progress = (typeof state !== 'undefined' && state.spellProgress) ? (state.spellProgress[_tCanonId] || {}) : {};
            var progressPercent = progress.required > 0 ? (progress.xp / progress.required) * 100 : 0;
            var playerHasSpell = progress.unlocked || node.state === 'unlocked';

            var showFullInfo = playerHasSpell || (typeof settings !== 'undefined' && settings.cheatMode);
            var isRootWithReveal = node.isRoot && (typeof settings !== 'undefined' && settings.showRootSpellNames);
            var isLearning = node.state === 'learning';
            var isLocked = node.state === 'locked';
            var revealThreshold = (typeof settings !== 'undefined' && settings.revealName !== undefined) ? settings.revealName : 10;
            var showName = showFullInfo || isLearning || (!isLocked && progressPercent >= revealThreshold) || isRootWithReveal;
            var showDetails = node.state !== 'locked' || (typeof settings !== 'undefined' && settings.cheatMode);

            // Translated with English fallback (t() returns the key when missing)
            function tt(key, params, fallback) {
                if (typeof t !== 'function') return fallback;
                var s = t(key, params);
                return s === key ? fallback : s;
            }

            var nameText = showName ? (node.name || node.formId) : '???';
            var infoText;
            if (node.state === 'locked') {
                // Undiscovered: tell the player what it is (school/tier) and how to reveal it
                var tierText = node.level || node.skillLevel || '';
                infoText = node.school + (tierText ? ' \u2022 ' + tierText : '') + ' \u2022 ' +
                           tt('tooltip.lockedHint', null, 'Unlock a linked spell to reveal');
            } else if (showDetails) {
                infoText = node.school + ' \u2022 ' + (node.level || '?') + ' \u2022 ' + (node.cost || '?') + ' magicka';
            } else {
                infoText = node.school + ' \u2022 ' + tt('tooltip.progress', { pct: Math.round(progressPercent) }, 'Progress: ' + Math.round(progressPercent) + '%');
            }
            if (!showName && node.state !== 'locked') {
                nameText = '??? (' + tt('tooltip.revealAt', { pct: revealThreshold }, 'name at ' + revealThreshold + '%') + ')';
            }

            var nameEl = tooltip.querySelector('.tooltip-name');
            var infoEl = tooltip.querySelector('.tooltip-info');
            var stateEl = tooltip.querySelector('.tooltip-state');
            if (nameEl) nameEl.textContent = nameText;
            if (infoEl) infoEl.textContent = infoText;
            if (stateEl) {
                stateEl.textContent = (typeof spellStateLabel === 'function') ? spellStateLabel(node.state) : node.state;
                stateEl.className = 'tooltip-state ' + node.state;
            }

            tooltip.classList.remove('hidden');
            tooltip.style.left = (event.clientX + 15) + 'px';
            tooltip.style.top = (event.clientY + 15) + 'px';
        },

        _hideTooltip: function() {
            var tooltip = document.getElementById('tooltip');
            if (tooltip) tooltip.classList.add('hidden');
        },

        setupEvents: function() {
            var self = this;

            this.canvas.addEventListener('mousedown', function(e) {
                self.onMouseDown(e);
            });

            this.canvas.addEventListener('mouseenter', function() {
                self._rectCache = null;   // the panel may have moved since (see _canvasRect)
            });
            this.canvas.addEventListener('mousemove', function(e) {
                self.onMouseMove(e);
            });

            this.canvas.addEventListener('mouseup', function(e) {
                self.onMouseUp(e);
            });

            this.canvas.addEventListener('mouseleave', function(e) {
                self.onMouseUp(e);
                // No more mousemove will come: the cursor is on the details bar
                // or outside the panel, so the spell under it is no longer hovered
                self._setHoveredNode(null, e);
            });

            // A release that lands anywhere else (the details bar that just opened,
            // a button, outside the panel) must still let go of the tree. Heard in
            // the capture phase, so an element that stops the event cannot keep it.
            window.addEventListener('mouseup', function(e) {
                if (self.isPanning) self.onMouseUp(e);
            }, true);
            // Focus leaving the page (the game takes the mouse back) or Escape also
            // let go: the release may never come to the page at all then
            window.addEventListener('blur', function() {
                self.releasePointer('blur');
            });
            window.addEventListener('keydown', function(e) {
                if (e.key === 'Escape' || e.keyCode === 27) self.releasePointer('escape');
            });
            // When the player last did something (INPUT_QUIET_MS, IDLE_AFTER_MS),
            // anywhere in the panel; heard in the capture phase
            var pressed = function() { self._lastPressAt = self._lastInputAt = performance.now(); };
            var moved = function() { self._lastInputAt = performance.now(); };
            document.addEventListener('mousedown', pressed, true);
            document.addEventListener('mouseup', pressed, true);
            document.addEventListener('wheel', pressed, true);
            document.addEventListener('keydown', pressed, true);
            document.addEventListener('mousemove', moved, true);

            this.canvas.addEventListener('wheel', function(e) {
                e.preventDefault();
                self.onWheel(e);
            }, { passive: false });

            this.canvas.addEventListener('click', function(e) {
                self.onClick(e);
            });

            // Window resize handler
            window.addEventListener('resize', function() {
                self._rectCache = null;
                self.updateCanvasSize();
            });

            // ResizeObserver for container size changes (more reliable than window resize)
            // This catches cases where the panel resizes without the window changing
            if (typeof ResizeObserver !== 'undefined' && this.container) {
                this._resizeObserver = new ResizeObserver(function(entries) {
                    // Debounce resize updates
                    if (self._resizeTimeout) {
                        clearTimeout(self._resizeTimeout);
                    }
                    self._resizeTimeout = setTimeout(function() {
                        self.updateCanvasSize();
                    }, 50);
                });
                this._resizeObserver.observe(this.container);
            }
        },

        // =========================================================================
        // COORDINATE TRANSFORMS
        // =========================================================================

        screenToWorld: function(screenX, screenY) {
            var cx = this._width / 2;
            var cy = this._height / 2;

            var x = (screenX - cx - this.panX) / this.zoom;
            var y = (screenY - cy - this.panY) / this.zoom;

            // Undo rotation
            var rotRad = -this.rotation * Math.PI / 180;
            var cos = Math.cos(rotRad);
            var sin = Math.sin(rotRad);
            var worldX = x * cos - y * sin;
            var worldY = x * sin + y * cos;

            return { x: worldX, y: worldY };
        },

        /**
         * Find the node under a world-space point.
         * Picks the NEAREST node within its hit radius (not the first found), and
         * guarantees a minimum on-screen hit radius so small nodes stay clickable
         * when zoomed out.
         */
        findNodeAt: function(worldX, worldY) {
            if (!this._nodeGrid) return null;

            var zoom = this.zoom || 1;
            var minWorldRadius = this.MIN_HIT_RADIUS_PX / zoom;
            var maxRadius = Math.max(14, minWorldRadius);
            var cellRange = Math.max(1, Math.ceil(maxRadius / this._gridCellSize));

            var cellX = Math.floor(worldX / this._gridCellSize);
            var cellY = Math.floor(worldY / this._gridCellSize);

            var best = null;
            var bestDist = Infinity;

            // Undiscovered nodes are not drawn, so they must not be hoverable/clickable either
            var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
            var discovery = (this._discoveryVisibleIds && !isEditActive) ? this._discoveryVisibleIds : null;
            var schoolVis = (typeof settings !== 'undefined') ? settings.schoolVisibility : null;

            for (var dx = -cellRange; dx <= cellRange; dx++) {
                for (var dy = -cellRange; dy <= cellRange; dy++) {
                    var key = (cellX + dx) + ',' + (cellY + dy);
                    var cell = this._nodeGrid[key];
                    if (!cell) continue;

                    for (var i = 0; i < cell.length; i++) {
                        var node = cell[i];
                        if (discovery && !discovery.has(node.id) && !discovery.has(node.formId)) continue;
                        if (schoolVis && schoolVis[node.school] === false) continue;
                        var ddx = node.x - worldX;
                        var ddy = node.y - worldY;
                        var dist = Math.sqrt(ddx * ddx + ddy * ddy);
                        var hitRadius = Math.max(node.state === 'unlocked' ? 14 : 10, minWorldRadius);

                        if (dist <= hitRadius && dist < bestDist) {
                            best = node;
                            bestDist = dist;
                        }
                    }
                }
            }

            return best;
        },

        findGlobeAt: function(worldX, worldY) {
            var globe = (state.treeData && state.treeData.globe) || { x: 0, y: 0, radius: 45 };
            var dx = worldX - globe.x;
            var dy = worldY - globe.y;
            var dist = Math.sqrt(dx * dx + dy * dy);
            return dist <= globe.radius ? globe : null;
        },

        // =========================================================================
        // EVENT HANDLERS
        // =========================================================================

        onMouseDown: function(e) {
            if (e.button === 0 || e.button === 2) {
                // User takes control: stop any camera focus animation in flight
                if (typeof TreeCamera !== 'undefined') TreeCamera.cancel();

                this.isPanning = true;
                this._dragMoved = false;
                this._pressX = e.clientX;
                this._pressY = e.clientY;
                this.panStartX = e.clientX - this.panX;
                this.panStartY = e.clientY - this.panY;
                // Does this browser say which buttons are down? Only then can a
                // lost release be noticed later (see onMouseMove).
                this._buttonsReported = typeof e.buttons === 'number' && e.buttons > 0;
                // A frame for the cursor, not a tree change: nothing the layer draws
                // depends on a press (it used to repaint every spell twice per click)
                this.__needsRender = true;
                this._animationOnlyRender = false;
                if (!this._buttonsLogged) {
                    // Once per session: tells from the game log whether the lost-release
                    // check below can work in this browser
                    this._buttonsLogged = true;
                    console.log('[CanvasRenderer] Mouse press: buttons=' + e.buttons + ' which=' + e.which +
                        ' (lost-release check ' + (this._buttonsReported ? 'on' : 'off') + ')');
                }
                if (typeof PerfMeter !== 'undefined') PerfMeter.press(e);
            }
        },

        onMouseMove: function(e) {
            var self = this;
            // The release never arrived (it happens in the game's browser) and the
            // tree would follow the cursor until the next click. No button is down
            // any more, so let go now.
            if (this.isPanning && this._buttonsReported && e.buttons === 0) {
                console.log('[CanvasRenderer] Release never arrived - let go on mousemove');
                this.onMouseUp(e);
            }

            if (this.isPanning && typeof PerfMeter !== 'undefined') PerfMeter.move(e);

            if (this.isPanning) {
                // Until the press travels past the threshold it is a click in the
                // making, and a click must not move the tree: the hand always shakes
                // a pixel or two, and the spell under it would slide along with it.
                if (!this._dragMoved) {
                    var mdx = e.clientX - this._pressX;
                    var mdy = e.clientY - this._pressY;
                    if (mdx * mdx + mdy * mdy <= this.DRAG_THRESHOLD * this.DRAG_THRESHOLD) return;
                    this._dragMoved = true;
                    // The hover preview lets go: it would be drawn again every frame of the drag
                    this._setHoveredNode(null, e);
                    // Start the drag from here, or the tree would jump by the threshold
                    this.panStartX = e.clientX - this.panX;
                    this.panStartY = e.clientY - this.panY;
                    this.canvas.style.cursor = 'grabbing';
                }

                // Batch pan updates using RAF to prevent multiple renders per frame
                this._pendingPanX = e.clientX - this.panStartX;
                this._pendingPanY = e.clientY - this.panStartY;

                if (!this._panRafPending) {
                    this._panRafPending = true;
                    requestAnimationFrame(function() {
                        self._panRafPending = false;
                        self.panX = self._pendingPanX;
                        self.panY = self._pendingPanY;
                        // A frame, but not "the tree changed": _drawTree sees the
                        // pan moved and slides the layer it already has.
                        self.__needsRender = true;
                        self._animationOnlyRender = false;
                    });
                }
            } else {
                var rect = this._canvasRect();
                var world = this.screenToWorld(e.clientX - rect.left, e.clientY - rect.top);
                this._setHoveredNode(this.findNodeAt(world.x, world.y), e);
            }
        },

        /**
         * The spell under the cursor changed (or there is none). Path preview,
         * tooltip, and the details panel's hover peek all follow from here.
         * @param {Object|null} node
         * @param {MouseEvent} e
         */
        _setHoveredNode: function(node, e) {
            if (node === this.hoveredNode) return;
            this.hoveredNode = node;
            this.canvas.style.cursor = node ? 'pointer' : 'grab';
            // A frame, not "the tree changed": the hover is painted over the layer
            // (HoverOverlay), so the layer is pasted as it is. Not throttled like
            // an animation frame - the player is waiting for it. Without the layer
            // (it failed) the hover is part of the tree again and needs a redraw.
            if (this._treeLayer && !this._treeLayerFailed && typeof HoverOverlay !== 'undefined') {
                this.__needsRender = true;
                this._animationOnlyRender = false;
            } else {
                this._needsRender = true;
            }

            // Hover preview: light up the hovered node's dependency path before any click
            if (node && (!this.selectedNode || this.selectedNode.id !== node.id)) {
                var hoverSets = this._computePathSets(node);
                this._hoverPathEdges = hoverSets.edges;
                this._hoverPathNodes = hoverSets.nodes;
            } else {
                this._hoverPathEdges = null;
                this._hoverPathNodes = null;
            }

            if (node) {
                this._showTooltip(node, e);
            } else {
                this._hideTooltip();
            }

            if (typeof DetailsPeek !== 'undefined') DetailsPeek.hover(node);
        },

        /** Let go of the tree whatever the mouse did (panel hidden, focus lost, Escape). */
        releasePointer: function(reason) {
            if (!this.isPanning) return;
            console.log('[CanvasRenderer] Let go of the tree: ' + reason);
            this.isPanning = false;
            this._dragMoved = false;
            if (this.canvas) this.canvas.style.cursor = 'grab';
            this.__needsRender = true;
        },

        onMouseUp: function(e) {
            if (this.isPanning && typeof PerfMeter !== 'undefined') {
                PerfMeter.release(e, this._dragMoved);
                // A release that should become a click: say so in the log if the
                // browser never sends the click (developer mode, PerfMeter.input)
                if (!this._dragMoved && PerfMeter.isOn()) {
                    var self = this;
                    var pending = this._clickPendingAt = performance.now();
                    setTimeout(function() {
                        if (self._clickPendingAt === pending) {
                            self._clickPendingAt = 0;
                            PerfMeter.input('release without a click event (' + self.CLICK_WAIT_MS + ' ms)');
                        }
                    }, this.CLICK_WAIT_MS);
                }
            }
            this.isPanning = false;
            this.canvas.style.cursor = this.hoveredNode ? 'pointer' : 'grab';
            this.__needsRender = true;
            this._animationOnlyRender = false;
        },

        onWheel: function(e) {
            // User takes control: stop any camera focus animation in flight
            if (typeof TreeCamera !== 'undefined') TreeCamera.cancel();

            var zoomFactor = e.deltaY < 0 ? 1 + this.WHEEL_ZOOM_STEP : 1 / (1 + this.WHEEL_ZOOM_STEP);
            var newZoom = this.zoom * zoomFactor;
            newZoom = Math.max(0.1, Math.min(5, newZoom));

            var rect = this._canvasRect();
            var mouseX = e.clientX - rect.left - rect.width / 2;
            var mouseY = e.clientY - rect.top - rect.height / 2;

            this.panX = mouseX - (mouseX - this.panX) * (newZoom / this.zoom);
            this.panY = mouseY - (mouseY - this.panY) * (newZoom / this.zoom);
            this.zoom = newZoom;

            // A frame with the layer stretched (_drawTree); once the wheel has been
            // still for WHEEL_SETTLE_MS a last frame repaints it at the new zoom
            var self = this;
            this._wheelAt = performance.now();
            this.__needsRender = true;
            this._animationOnlyRender = false;
            if (this._wheelSettleTimer) clearTimeout(this._wheelSettleTimer);
            this._wheelSettleTimer = setTimeout(function() {
                self._wheelSettleTimer = null;
                self.__needsRender = true;
                self._animationOnlyRender = false;
            }, this.WHEEL_SETTLE_MS + this.WHEEL_SETTLE_SLACK_MS);

            this.showZoom(this.zoom);
        },

        onClick: function(e) {
            this._clickPendingAt = 0;
            // A press that turned into a drag must not select whatever is under the cursor on release
            if (this._dragMoved) {
                this._dragMoved = false;
                if (typeof PerfMeter !== 'undefined') PerfMeter.input('click ignored: the press was a drag');
                return;
            }

            var rect = this._canvasRect();
            var world = this.screenToWorld(e.clientX - rect.left, e.clientY - rect.top);
            var clickedNode = this.findNodeAt(world.x, world.y);
            if (typeof PerfMeter !== 'undefined') {
                PerfMeter.input('click -> ' + (clickedNode ? (clickedNode.name || clickedNode.id) : 'nothing') +
                    ', late ' + PerfMeter.lateMs(e) + ' ms');
            }

            if (clickedNode) {
                this.selectNodeAndFocus(clickedNode);
            } else {
                if (this.selectedNode) {
                    this.selectedNode = null;
                    this._selectedPathEdges = null;
                    this._selectedPathNodes = null;
                    this._needsRender = true;
                    // The details panel lets go of the spell too
                    window.dispatchEvent(new CustomEvent('nodeDeselected'));
                }
            }
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
