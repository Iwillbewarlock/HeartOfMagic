/**
 * TreeCamera Module - Camera focus animation for the Canvas 2D spell tree
 *
 * Provides vanilla-perk-menu style navigation: when a node is selected the
 * view rotates the node's school to the top AND pans so the node lands in the
 * middle of the visible area, optionally zooming in to a readable level.
 * Rotation, pan and zoom are interpolated together in one eased animation,
 * and a new focus request re-targets the running animation instead of being
 * dropped.
 *
 * Coordinate model (must match CanvasRenderer.render):
 *   screen = rotate(world, rotation) * zoom + pan + viewCenter
 * so to put a world point at screen offset `off` from the view center:
 *   pan = off - rotate(world, rotation) * zoom
 *
 * Depends on: settings (state.js), CanvasRenderer (canvasRendererV2.js)
 */

var TreeCamera = {
    // Animation state
    _rafId: null,
    _anim: null,

    // Defaults
    DEFAULT_DURATION: 450,      // ms
    DEFAULT_FOCUS_ZOOM: 1.0,    // Zoom level used when "zoom on click" is enabled
    MIN_ZOOM: 0.1,
    MAX_ZOOM: 5,

    /**
     * Get the renderer this camera drives (CanvasRenderer only).
     * @returns {Object|null}
     */
    _getRenderer: function() {
        if (typeof CanvasRenderer === 'undefined' || !CanvasRenderer.canvas) return null;
        return CanvasRenderer;
    },

    /**
     * Resolve a node (possibly a filtered copy) to the renderer's own node object
     * so we use the live x/y positions.
     */
    _resolveNode: function(renderer, node) {
        if (!node || !renderer._nodeMap) return node;
        var found = renderer._nodeMap.get(node.formId || node.id) || renderer._nodeMap.get(node.id);
        return found || node;
    },

    /**
     * Screen-space offset (from the view center) where a focused node should
     * land so it is not covered by the details sidebar (right) or the open
     * How-to-Learn panel (left).
     * @returns {{x:number, y:number}}
     */
    getPanelOffset: function() {
        var dx = 0;
        var dy = 0;
        var details = document.getElementById('details-panel');
        if (details && !details.classList.contains('hidden')) {
            var page = document.getElementById('contentSpellTree');
            var bottomBar = page && page.classList.contains('details-bottom');
            if (bottomBar) {
                dy -= (details.offsetHeight || 190) / 2;   // Bar covers the lower part of the tree
            } else {
                dx -= (details.offsetWidth || 240) / 2;    // Sidebar covers the right side
            }
        }
        var howto = document.getElementById('howto-panel');
        if (howto && !howto.classList.contains('hidden') && howto.classList.contains('open')) {
            dx += (howto.offsetWidth || 260) / 2;
        }
        return { x: dx, y: dy };
    },

    /**
     * Rotation (degrees) that puts the node's school spoke at the visual top.
     * Returns null when the school has no spoke angle.
     */
    getSchoolTopRotation: function(renderer, node) {
        if (!node || !node.school || !renderer.schools) return null;
        var school = renderer.schools[node.school];
        if (!school || school.spokeAngle === undefined) return null;
        return -90 - school.spokeAngle;
    },

    /**
     * Normalize an angle delta to the shortest direction (-180..180).
     */
    _shortestDelta: function(from, to) {
        var delta = to - from;
        while (delta > 180) delta -= 360;
        while (delta < -180) delta += 360;
        return delta;
    },

    /**
     * Compute the camera state that centers `node`.
     * @param {Object} renderer
     * @param {Object} node
     * @param {Object} [opts]
     * @param {boolean} [opts.rotate=true]   Rotate school to top
     * @param {number}  [opts.zoom]          Target zoom (undefined = keep current)
     * @param {{x:number,y:number}} [opts.offset] Screen offset from view center
     * @returns {{rotation:number, zoom:number, panX:number, panY:number}}
     */
    computeTarget: function(renderer, node, opts) {
        opts = opts || {};
        var rotation = renderer.rotation;
        // Turning the whole wheel on every school change is dizzying, so by
        // default the view only travels. Labels are drawn upright whatever the
        // wheel's angle, so nothing needs turning for the text's sake either.
        // A caller can still ask for it; the setting brings the old way back.
        var wantRotate = (typeof opts.rotate === 'boolean')
            ? opts.rotate
            : (typeof settings !== 'undefined' && settings.focusRotate === true);
        if (wantRotate && !renderer.noRotate) {
            var schoolRot = this.getSchoolTopRotation(renderer, node);
            if (schoolRot !== null) {
                rotation = renderer.rotation + this._shortestDelta(renderer.rotation, schoolRot);
            }
        }

        var zoom = (typeof opts.zoom === 'number') ? opts.zoom : renderer.zoom;
        zoom = Math.max(this.MIN_ZOOM, Math.min(this.MAX_ZOOM, zoom));

        var rad = rotation * Math.PI / 180;
        var cos = Math.cos(rad);
        var sin = Math.sin(rad);
        var rx = node.x * cos - node.y * sin;
        var ry = node.x * sin + node.y * cos;

        var off = opts.offset || this.getPanelOffset();
        return {
            rotation: rotation,
            zoom: zoom,
            panX: off.x - rx * zoom,
            panY: off.y - ry * zoom
        };
    },

    /**
     * Zoom to use when focusing, honoring user settings.
     * Never zooms OUT on the user: if they are already closer than the focus
     * zoom, the current zoom is kept.
     */
    _resolveFocusZoom: function(renderer, opts) {
        if (typeof opts.zoom === 'number') return opts.zoom;
        var s = (typeof settings !== 'undefined') ? settings : {};
        if (s.focusZoomOnClick === false) return renderer.zoom;
        var focusZoom = (typeof s.focusZoom === 'number' && s.focusZoom > 0) ? s.focusZoom : this.DEFAULT_FOCUS_ZOOM;
        return Math.max(renderer.zoom, focusZoom);
    },

    /**
     * Animate the camera so `node` is centered (vanilla perk style).
     * @param {Object} node
     * @param {Object} [opts]
     * @param {boolean}  [opts.rotate=true]
     * @param {number}   [opts.zoom]        Explicit target zoom
     * @param {number}   [opts.duration]
     * @param {boolean}  [opts.instant]     Jump without animation
     * @param {Function} [opts.onComplete]
     * @returns {boolean} true if a focus was started
     */
    focusNode: function(node, opts) {
        opts = opts || {};
        var renderer = this._getRenderer();
        if (!renderer || !node) return false;

        node = this._resolveNode(renderer, node);
        if (typeof node.x !== 'number' || typeof node.y !== 'number') return false;

        var target = this.computeTarget(renderer, node, {
            rotate: opts.rotate,
            zoom: this._resolveFocusZoom(renderer, opts),
            offset: opts.offset
        });

        if (opts.instant) {
            this.cancel();
            this._apply(renderer, target);
            if (typeof opts.onComplete === 'function') opts.onComplete();
            return true;
        }

        this.animateTo(target, opts.duration, opts.onComplete);
        return true;
    },

    /**
     * Animate the camera to an explicit state. Re-targets a running animation.
     * @param {{rotation?:number, zoom?:number, panX?:number, panY?:number}} target
     * @param {number} [duration]
     * @param {Function} [onComplete]
     */
    animateTo: function(target, duration, onComplete) {
        var renderer = this._getRenderer();
        if (!renderer) return;

        this.cancel();

        var self = this;
        var start = {
            rotation: renderer.rotation,
            zoom: renderer.zoom,
            panX: renderer.panX,
            panY: renderer.panY
        };
        var end = {
            rotation: (typeof target.rotation === 'number') ? target.rotation : start.rotation,
            zoom: (typeof target.zoom === 'number') ? target.zoom : start.zoom,
            panX: (typeof target.panX === 'number') ? target.panX : start.panX,
            panY: (typeof target.panY === 'number') ? target.panY : start.panY
        };
        // Always take the short way round
        end.rotation = start.rotation + this._shortestDelta(start.rotation, end.rotation);

        var dur = (typeof duration === 'number' && duration >= 0) ? duration : this.DEFAULT_DURATION;
        var startTime = performance.now();

        this._anim = { start: start, end: end, startTime: startTime, duration: dur, onComplete: onComplete };
        renderer.isAnimating = true;

        function easeOutCubic(t) { return 1 - Math.pow(1 - t, 3); }

        function step(now) {
            var anim = self._anim;
            if (!anim) return;

            var t = anim.duration > 0 ? Math.min((now - anim.startTime) / anim.duration, 1) : 1;
            var e = easeOutCubic(t);
            var s = anim.start, d = anim.end;

            self._apply(renderer, {
                rotation: s.rotation + (d.rotation - s.rotation) * e,
                zoom: s.zoom + (d.zoom - s.zoom) * e,
                panX: s.panX + (d.panX - s.panX) * e,
                panY: s.panY + (d.panY - s.panY) * e
            });

            if (t < 1) {
                self._rafId = requestAnimationFrame(step);
            } else {
                self._apply(renderer, d);
                self._rafId = null;
                self._anim = null;
                renderer.isAnimating = false;
                if (typeof anim.onComplete === 'function') anim.onComplete();
            }
        }

        this._rafId = requestAnimationFrame(step);
    },

    /**
     * Write a camera state to the renderer and refresh the zoom readout.
     */
    _apply: function(renderer, cam) {
        renderer.rotation = cam.rotation;
        renderer.zoom = cam.zoom;
        renderer.panX = cam.panX;
        renderer.panY = cam.panY;
        renderer._needsRender = true;

        var zoomEl = renderer._zoomLevelEl || document.getElementById('zoom-level');
        if (zoomEl) zoomEl.textContent = Math.round(cam.zoom * 100) + '%';
    },

    /**
     * Stop the running animation (used when the user starts dragging/zooming).
     */
    cancel: function() {
        if (this._rafId) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
        if (this._anim) {
            this._anim = null;
            var renderer = this._getRenderer();
            if (renderer) renderer.isAnimating = false;
        }
    },

    isAnimating: function() {
        return !!this._anim;
    }
};

window.TreeCamera = TreeCamera;
