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
 * Home view (homeTarget): the whole tree fitted into the part of the canvas
 * the details panel and How-to-Learn panel leave visible (visibleRect).
 *
 * Depends on: settings, state (state.js), CanvasRenderer (canvasRendererV2.js),
 *             TreeStyle.chapterBoxes (treeStyleBook.js, optional)
 */

var TreeCamera = {
    // Animation state
    _rafId: null,
    _anim: null,

    // Defaults
    DEFAULT_DURATION: 450,      // ms
    STILL_EPSILON: 0.5,         // pan (px) and rotation (deg) closer than this: already there (zoom: this / 1000)
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
     * land: the middle of the part of the tree the details panel (bar under the
     * tree or sidebar) and the open How-to-Learn panel leave visible.
     * @returns {{x:number, y:number}}
     */
    getPanelOffset: function() {
        var renderer = this._getRenderer();
        var rect = renderer ? this.visibleRect(renderer) : null;
        if (!rect) return { x: 0, y: 0 };
        return {
            x: rect.x + rect.w / 2 - renderer._width / 2,
            y: rect.y + rect.h / 2 - renderer._height / 2
        };
    },

    // =========================================================================
    // VISIBLE AREA AND HOME VIEW
    // =========================================================================

    HOME_PAD: 24,              // css px kept clear round the tree in the home view (spell names)
    WIDE_OVERLAY: 0.5,         // an overlay at least this share of the tree's width is a bar (top/bottom), else a side panel

    /** The panels drawn over the tree that are showing now. */
    _overlays: function() {
        var list = [];
        var details = document.getElementById('details-panel');
        if (details && !details.classList.contains('hidden')) list.push(details);
        var howto = document.getElementById('howto-panel');
        if (howto && !howto.classList.contains('hidden') && howto.classList.contains('open')) list.push(howto);
        return list;
    },

    /**
     * Take an overlay's part of the canvas off the side it sits on. o and c are
     * client rects; cut holds css px per side and only grows.
     */
    _addCut: function(cut, c, o) {
        var l = Math.max(c.left, o.left), r = Math.min(c.right, o.right);
        var t = Math.max(c.top, o.top), b = Math.min(c.bottom, o.bottom);
        if (r <= l || b <= t) return;
        if (r - l >= c.width * this.WIDE_OVERLAY) {
            if ((t + b) / 2 > (c.top + c.bottom) / 2) cut.bottom = Math.max(cut.bottom, c.bottom - t);
            else cut.top = Math.max(cut.top, b - c.top);
        } else if ((l + r) / 2 > (c.left + c.right) / 2) {
            cut.right = Math.max(cut.right, c.right - l);
        } else {
            cut.left = Math.max(cut.left, r - c.left);
        }
    },

    /**
     * The part of the tree canvas no panel covers, canvas-local css px
     * {x, y, w, h}. null while the canvas has no size (another tab in front):
     * the caller tries again on a later frame. With the canvas laid out, a
     * panel with no size covers nothing.
     */
    visibleRect: function(renderer) {
        var canvas = renderer && renderer.canvas;
        if (!canvas || !canvas.getBoundingClientRect || !renderer._width || !renderer._height) return null;
        var c = canvas.getBoundingClientRect();
        if (!c.width || !c.height) return null;
        var cut = { left: 0, right: 0, top: 0, bottom: 0 };
        var overlays = this._overlays();
        for (var i = 0; i < overlays.length; i++) {
            var o = overlays[i].getBoundingClientRect();
            if (o.width && o.height) this._addCut(cut, c, o);
        }
        var w = renderer._width, h = renderer._height;
        var x = Math.min(cut.left, w - 1), y = Math.min(cut.top, h - 1);
        return { x: x, y: y, w: Math.max(1, w - x - cut.right), h: Math.max(1, h - y - cut.bottom) };
    },

    FIT_STEPS: 30,             // halvings of the zoom range when screen-sized boxes (chapter titles) go with the tree

    /**
     * What a view at zoom z covers, css px from the world origin (no pan,
     * rotation 0): the world box scaled, and each of extrasAt(z)'s screen boxes.
     */
    _extent: function(bounds, z, extrasAt) {
        var e = { l: bounds.minX * z, r: bounds.maxX * z, t: bounds.minY * z, b: bounds.maxY * z };
        var boxes = extrasAt ? extrasAt(z) : [];
        for (var i = 0; i < boxes.length; i++) {
            var x = boxes[i];
            if (x.l < e.l) e.l = x.l;
            if (x.r > e.r) e.r = x.r;
            if (x.t < e.t) e.t = x.t;
            if (x.b > e.b) e.b = x.b;
        }
        return e;
    },

    /**
     * Pan and zoom that fit a world box - and, when extrasAt is given, the
     * screen-sized boxes that go with it at each zoom (the chapter titles, past
     * the outer spells along their spokes) - into rect (canvas-local css px)
     * with pad.x / pad.y css px round it, centred; the zoom no closer than
     * maxZoom. Width and height are fitted apart, so a side title takes room
     * across and a top title room down, each only its own box. Rotation 0.
     * @param {{minX:number, maxX:number, minY:number, maxY:number}} bounds world
     * @param {{x:number, y:number, w:number, h:number}} rect
     * @param {{w:number, h:number}} size the canvas, css px
     * @param {{x:number, y:number}} pad
     * @param {number} maxZoom
     * @param {function(number): Array<{l:number, r:number, t:number, b:number}>} [extrasAt]
     * @returns {{rotation:number, zoom:number, panX:number, panY:number}}
     */
    fitTarget: function(bounds, rect, size, pad, maxZoom, extrasAt) {
        var self = this;
        var fits = function(z) {
            var e = self._extent(bounds, z, extrasAt);
            return e.r - e.l <= rect.w - 2 * pad.x && e.b - e.t <= rect.h - 2 * pad.y;
        };
        var zoom;
        if (fits(maxZoom)) {
            zoom = maxZoom;
        } else if (!fits(this.MIN_ZOOM)) {
            zoom = this.MIN_ZOOM;
        } else {
            // Bisection: lo always fits, hi never does. The extents grow with the zoom
            // (a title's distance from the heart is its spells' reach times the zoom plus fixed px)
            var lo = this.MIN_ZOOM, hi = maxZoom;
            for (var i = 0; i < this.FIT_STEPS; i++) {
                var mid = (lo + hi) / 2;
                if (fits(mid)) lo = mid; else hi = mid;
            }
            zoom = lo;
        }
        var e = this._extent(bounds, zoom, extrasAt);
        return {
            rotation: 0,
            zoom: zoom,
            panX: rect.x + rect.w / 2 - size.w / 2 - (e.l + e.r) / 2,
            panY: rect.y + rect.h / 2 - size.h / 2 - (e.t + e.b) / 2
        };
    },

    /** World box of the shown schools' spells and the heart, null for no spells. */
    treeBounds: function(renderer) {
        var nodes = renderer.nodes || [];
        var hidden = (typeof settings !== 'undefined' && settings.schoolVisibility) || {};
        var gd = (typeof state !== 'undefined' && state.treeData && state.treeData.globe) || { x: 0, y: 0 };
        var b = { minX: gd.x, maxX: gd.x, minY: gd.y, maxY: gd.y };
        var any = false;
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            if (hidden[n.school] === false || typeof n.x !== 'number' || typeof n.y !== 'number') continue;
            any = true;
            if (n.x < b.minX) b.minX = n.x;
            if (n.x > b.maxX) b.maxX = n.x;
            if (n.y < b.minY) b.minY = n.y;
            if (n.y > b.maxY) b.maxY = n.y;
        }
        return any ? b : null;
    },

    /**
     * The home view: the whole tree fitted into the part no panel covers, no
     * closer than TreeNav.HOME_ZOOM; the design's chapter titles get their room
     * past the outer spells. null while that part cannot be measured.
     */
    homeTarget: function(renderer, maxZoom) {
        var rect = this.visibleRect(renderer);
        if (!rect) return null;
        var bounds = this.treeBounds(renderer);
        if (!bounds) {
            return { rotation: 0, zoom: maxZoom, panX: rect.x + rect.w / 2 - renderer._width / 2,
                     panY: rect.y + rect.h / 2 - renderer._height / 2 };
        }
        // The design's chapter titles where they would be drawn at each zoom tried (TreeStyle.chapterBoxes
        // with no pan, centre 0, rotation 0, nothing culled)
        var extrasAt = null;
        if (typeof TreeStyle !== 'undefined' && TreeStyle.chapterBoxes && renderer.ctx) {
            var view = { nodes: renderer.nodes, schools: renderer.schools, zoom: 1, panX: 0, panY: 0,
                         _width: renderer._width, _height: renderer._height,
                         MIN_NODE_SCREEN_RADIUS: renderer.MIN_NODE_SCREEN_RADIUS };
            extrasAt = function(z) {
                view.zoom = z;
                return TreeStyle.chapterBoxes(renderer.ctx, view, 0, 0, 1, 0, Infinity);
            };
        }
        var pad = { x: this.HOME_PAD, y: this.HOME_PAD };
        return this.fitTarget(bounds, rect, { w: renderer._width, h: renderer._height }, pad, maxZoom, extrasAt);
    },

    /**
     * Put the home view a new tree asked for (CanvasRenderer.centerView) in
     * place; called at the start of a frame, before anything reads the view.
     * @returns {boolean} false while the tree's visible part cannot be measured
     */
    settleHome: function(renderer) {
        var maxZoom = (typeof TreeNav !== 'undefined' && TreeNav.HOME_ZOOM) || renderer.zoom;
        var target = this.homeTarget(renderer, maxZoom);
        if (!target) return false;
        renderer.rotation = target.rotation;
        renderer.zoom = target.zoom;
        renderer.panX = target.panX;
        renderer.panY = target.panY;
        if (renderer.showZoom) renderer.showZoom(target.zoom);
        return true;
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
            renderer._homePending = false;
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
        renderer._homePending = false;   // a view asked for after the tree loaded wins over its home view

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

        // Already there (a click on a spell in view): no glide. The renderer
        // holds tree repaints while the view is in motion, so a glide that
        // goes nowhere would only delay the new selection's highlight.
        if (Math.abs(end.rotation - start.rotation) < this.STILL_EPSILON &&
            Math.abs(end.panX - start.panX) < this.STILL_EPSILON &&
            Math.abs(end.panY - start.panY) < this.STILL_EPSILON &&
            Math.abs(end.zoom - start.zoom) < this.STILL_EPSILON * 0.001) {
            this._apply(renderer, end);
            if (typeof onComplete === 'function') onComplete();
            return;
        }

        this._anim = { start: start, end: end, startTime: startTime, duration: dur, onComplete: onComplete };
        renderer.isAnimating = true;
        // Where the glide ends: the renderer draws the tree for that view while it
        // glides (LayerBuild), so it is ready when the camera arrives
        renderer._glideTarget = end;

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
                renderer._glideTarget = null;
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
        if (renderer._layerZoom !== undefined) {
            // The view moved, the tree did not. The canvas renderer notices
            // what moved by itself: it slides its tree layer when only the
            // pan changed and redraws it when zoom or rotation did.
            renderer.__needsRender = true;
            renderer._animationOnlyRender = false;
        } else {
            renderer._needsRender = true;
        }

        if (renderer.showZoom) renderer.showZoom(cam.zoom);
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
            if (renderer) { renderer.isAnimating = false; renderer._glideTarget = null; }
        }
    },

    isAnimating: function() {
        return !!this._anim;
    }
};

window.TreeCamera = TreeCamera;
