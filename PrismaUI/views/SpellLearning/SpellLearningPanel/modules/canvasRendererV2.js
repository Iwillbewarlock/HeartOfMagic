/**
 * CanvasRenderer V2 - Adapted for universal coordinate data from the new scan system
 *
 * Changes from V1:
 * - Self-contained school color lookup (no TREE_CONFIG dependency)
 * - Self-contained tooltip (no WheelRenderer dependency)
 * - Direct event dispatch for node clicks (no WheelRenderer.onNodeClick)
 * - Simple spiral fallback layout (no WheelRenderer.layoutRadial)
 * - Debug grid works without GRID_CONFIG
 * - Reads pre-baked x,y positions from scan system data
 *
 * Depends on: settings, state, TreeStyle (no TREE_CONFIG, no WheelRenderer, no GRID_CONFIG)
 *
 * CanvasRenderer is one object spread over several files. This one holds its
 * state and constants, start-up, the canvas size, the render loop and the public
 * calls; the files below add their methods to it (each an IIFE copying a table
 * of functions onto CanvasRenderer, as treeStyleBook.js does for TreeStyle), and
 * index.html loads them right after this one:
 *   canvasRendererColors.js    school colours, design-or-player colours, colour helpers
 *   canvasRendererInput.js     mouse and wheel, hit testing, hover, tooltip
 *   canvasRendererSelect.js    selecting a spell, its path sets, turning the wheel
 *   canvasRendererData.js      setData, school angles, fallback layout, discovery set,
 *                              spatial index, level of detail and node buckets
 *   canvasRendererFrame.js     render(), background, the tree layer (_drawTree) and
 *                              what goes into it (_renderTreeInto)
 *   canvasRendererMoving.js    what moves every frame: heart, globe, particles, sigil spots
 *   canvasRendererDividers.js  school dividers, debug grid
 *   canvasRendererEdges.js     the lines between spells
 *   canvasRendererNodes.js     all spells: levels of detail, batching (NodeBatch)
 *   canvasRendererSpell.js     one spell drawn by itself (renderNode, renderMysteryNode)
 *   canvasRendererLabels.js    spell names
 *   canvasRendererLearning.js  learning paths, their animation and pulses
 * The _needsRender accessor is defined at the end of this file. A later file
 * cannot replace it by accident: assigning CanvasRenderer._needsRender runs the
 * setter, and none of the tables has that name.
 */

var CanvasRenderer = {
    canvas: null,
    ctx: null,
    container: null,
    
    // Data
    nodes: [],
    edges: [],
    schools: {},
    
    // Transform state
    zoom: 1,
    panX: 0,
    panY: 0,
    rotation: 0,
    isAnimating: false,
    noRotate: false,
    
    // Interaction state
    isPanning: false,
    panStartX: 0,
    panStartY: 0,
    selectedNode: null,
    hoveredNode: null,
    _pressX: 0,               // Screen position where the mouse button went down
    _pressY: 0,
    _dragMoved: false,        // True once a press moved past DRAG_THRESHOLD (suppresses click)
    // Frames asked for by animation alone (heart, globe, stars, sigil) come at
    // most this often: ~12 a second (was 15, and 20 before). Each is an upload
    // of the whole panel in the game's browser; the moving parts keep their
    // speed (AnimClock), they only move in slightly bigger steps
    ANIMATION_FRAME_MS: 83,
    WHEEL_SETTLE_MS: 150,        // the wheel counts as still after this long without a notch
    WHEEL_SETTLE_SLACK_MS: 20,   // the repaint timer fires just after that
    HEARTBEAT_FRAME_MS: 50,      // the frame the heartbeat speed setting was tuned at
    HEARTBEAT_MAX_STEP_MS: 250,  // a longer gap (panel shut, a stall) is not caught up
    DRAG_THRESHOLD: 5,
    CLICK_WAIT_MS: 500,       // a release with no click event after this long is logged (PerfMeter.input)        // px of movement before a press counts as a drag
    WHEEL_ZOOM_STEP: 0.15,    // zoom change per wheel notch (was 0.10; out is the exact inverse of in)
    MIN_HIT_RADIUS_PX: 12,    // Minimum on-screen hit radius for node picking
    MIN_NODE_SCREEN_RADIUS: 4, // px: node shapes never render smaller than this on screen
    LABEL_MIN_ZOOM: 0.5,      // Below this zoom no labels are drawn
    LABEL_FOCUS_ZOOM: 0.8,    // Below this zoom only important labels (selected/learning/available) are drawn
    MAX_LABELS: 150,          // names placed on screen at most (renderLabels, LayerScroll)
    DIM_OTHER_SCHOOL: 0.3,    // Focus+context: alpha factor for nodes of other schools while a node is selected
    DIM_SAME_SCHOOL: 0.6,     // Focus+context: alpha factor for off-path nodes of the selected school
    
    // Spatial index for hit detection
    _nodeGrid: null,
    _gridCellSize: 50,

    // LOD (Level of Detail) - zoom-based rendering tiers
    _lodTier: 'full',        // 'full' | 'simple' | 'minimal'
    _activeDpr: 0,           // Current effective DPR (for tier-transition resize)
    _nodeBuckets: null,      // { 'school|state': [node, ...] } for batched minimal rendering
    _cachedDividerGradients: null,  // Cached CanvasGradient objects for school dividers
    _dividerCacheKey: '',    // String key to detect when divider settings change

    // Performance
    _rafId: null,
    _fxThisFrame: false,          // this frame draws the moving parts on FxLayer spots
    _mainShownKey: null,          // StaticBase key of what the tree canvas shows untouched by moving parts
    _lastPressAt: 0,
    _lastInputAt: 0,
    _needsRender: true,           // replaced by an accessor at the end of this file
    __needsRender: true,
    _treeDirty: true,             // the tree layer must be redrawn before it is pasted
    USE_TREE_LAYER: true,         // off = draw the tree straight onto the canvas every frame, as before
    // What moves every frame (heart, globe, sigil, learning glow, particles) is
    // drawn on small canvases over the tree (FxLayer), and the tree canvas is
    // left alone when nothing on it changed: the game's browser repaints only
    // what changed. Off = everything on the tree canvas every frame, as before.
    USE_FX_LAYER: true,
    INPUT_QUIET_MS: 150,          // no animation frame while a button is held or this soon after a press, wheel or key
    IDLE_AFTER_MS: 15000,         // no input this long: idle
    IDLE_FRAME_MS: 250,           // idle: animation frames come this far apart at most
    IDLE_STOP_MS: 45000,          // no input this long: no animation frames at all (the next mouse move or key brings them back)
    // A design's glows are left out where they would barely show and cost most:
    // a known spell's halo smaller than this on screen (css px radius; zoomed
    // out, the whole tree is hundreds of halo sprites per repaint)...
    HALO_MIN_SCREEN_PX: 20,
    EDGE_GLOW_MIN_ZOOM: 0.8,      // ...and the wide stroke under known lines below this zoom
    CURVE_BULGE: 0.08,            // a curved line (control point 0.15 of its length off) strays ~0.075 of it
    TREE_LAYER_MARGIN: 128,       // css px drawn beyond each edge, so a drag slides the layer; past it the
                                  // layer is shifted and the uncovered strips drawn (LayerScroll). (Was 256:
                                  // the layer was about 1.6 times the pixels, cleared and copied each repaint)
    // The lock look (hard prerequisites): renderNode and _batchPlainNode
    LOCK_SHELL_FILL: 'rgba(90, 90, 100, 0.7)',     // a locked spell's grey shell...
    LOCK_SHELL_STROKE: 'rgba(140, 140, 155, 0.8)',
    LOCK_RING_FILL: 'rgba(90, 90, 100, 0.25)',     // ...and the grey ring round a known one
    LOCK_RING_STROKE: 'rgba(150, 150, 160, 0.7)',
    _layerPanX: 0,                // where the view was when the layer was last drawn
    _layerPanY: 0,
    _layerZoom: null,
    _layerRotation: null,
    _animationOnlyRender: false,  // True when only animations need update (can be throttled)
    _lastRenderTime: 0,
    _logNextRender: false,
    _pendingPanX: 0,
    _pendingPanY: 0,
    _panRafPending: false,
    
    // Node lookup
    _nodeMap: null,
    _nodeByFormId: null,
    
    // Discovery mode visibility
    _discoveryVisibleIds: null,
    
    // Dimensions
    _width: 0,
    _height: 0,
    
    // Cached DOM elements
    _zoomLevelEl: null,

    // Debug grid
    showDebugGrid: false,
    
    // Heartbeat animation for central hub (configurable via settings)
    _heartbeatPhase: 0,
    _heartbeatSpeed: 0.2,   // Radians per frame (default 0.2)
    _heartPulseDelay: 5.0,  // Time (in seconds) between pulse groups (default 5s)
    _heartAnimationEnabled: true,
    _heartBgOpacity: 1.0,
    _heartBgColor: '#000000',
    _bgColor: '#000000',
    _heartRingColor: '#b8a878',
    _learningPathColor: '#00ffff',
    _globeBgFill: true,
    
    // Learning path animation (glowing line from center to newly learned spell)
    _learningPath: null,       // { nodeId, path: [{x,y}...], progress: 0-1, startTime, color }
    _learningPathDuration: 1200,  // ms for the path to animate
    _learningPathAnimationComplete: true,  // Static path only shows after animation
    _animatingPathNodes: null,    // Set of node IDs in the CURRENTLY ANIMATING path only

    // Persistent learning state - tracks which nodes are being learned
    _learningNodeIds: null,       // Set of node IDs currently in learning state
    _learningPathNodes: null,     // Set of all node IDs along paths to learning nodes
    
    // Traveling pulse particles along learning paths
    _learningPulses: [],          // Array of {x, y, progress, pathIndex, speed}
    _lastHeartbeatPulse: false,   // Track heartbeat state for pulse spawning
    _learningPathSegments: [],    // Cached path segments [{from:{x,y}, to:{x,y}, nodeId}...]
    _learningPulseSpeed: 0.015,   // Default pulse travel speed (configurable)
    _learningPulseSize: 4,        // Default pulse size (configurable)
    
    // 3D Globe (uses Globe3D module)
    _globeEnabled: true,
    
    // Starfield background (uses Starfield module)
    _starfieldEnabled: true,
    _starfieldFixed: false,  // true = fixed to screen, false = moves with world
    _starfieldColor: '#ffffff',
    _starfieldDensity: 200,
    _starfieldMaxSize: 2.5,
    _starfieldSeed: 42,

    // =========================================================================
    // SELF-CONTAINED SCHOOL COLORS (no TREE_CONFIG dependency; _getSchoolColor)
    // =========================================================================

    _defaultSchoolColors: {
        'Destruction': '#ef4444',
        'Restoration': '#facc15',
        'Alteration': '#22c55e',
        'Conjuration': '#a855f7',
        'Illusion': '#38bdf8'
    },

    // The shipped defaults of the colours a design may re-colour (_designOrPlayer)
    PLAYER_COLOR_DEFAULTS: {
        learningPathColor: ['#00ffff'],
        heartRingColor: ['#b8a878'],
        heartBgColor: ['#000000', '#0a0a14'],   // panel default, C++ default config
        magicTextColor: ['#ffecb3', '#b8a878'],  // state default, swatch reset
        globeColor: ['#b8a878']
    },

    // =========================================================================
    // PATH2D CACHE - Pre-computed shapes for performance (_initShapePaths)
    // =========================================================================

    _shapePaths: null,

    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    
    init: function(container) {
        this.container = container;

        // Initialize Path2D cache
        if (!this._shapePaths) {
            this._initShapePaths();
        }

        // Cache frequently-accessed DOM elements
        this._zoomLevelEl = document.getElementById('zoom-level');

        // Create canvas element if not already created
        if (!this.canvas) {
            this.canvas = document.createElement('canvas');
            this.canvas.id = 'tree-canvas';
            this.canvas.style.cssText = 'position: absolute; top: 0; left: 0; width: 100%; height: 100%; display: block; z-index: 1;';
            this.ctx = this.canvas.getContext('2d');

            this.setupEvents();
        }

        console.log('[CanvasRenderer] Initialized');
        return this;
    },
    
    /**
     * Where the canvas is on the page, measured once and kept: reading it on
     * every mouse move made the browser lay the page out again each time.
     * Measured again when the pointer comes onto the canvas, and dropped when
     * the canvas or the window is resized.
     */
    _canvasRect: function() {
        if (!this._rectCache) this._rectCache = this.canvas.getBoundingClientRect();
        return this._rectCache;
    },

    updateCanvasSize: function() {
        if (!this.container || !this.canvas) return;
        this._rectCache = null;
        
        var rect = this.container.getBoundingClientRect();
        // Another tab in front (the tree's is display:none): keep the canvas as
        // it is. Resizing it here, and back on return, cleared it both times and
        // repainted the whole tree.
        if (!rect.width || !rect.height) return;
        // Whole pixels: a fractional CSS size (the panel is centred and sized in
        // % and vh) left the backing store a fraction off it, and the view then
        // rescaled the whole canvas each time it painted it
        var width = Math.floor(rect.width);
        var height = Math.floor(rect.height);
        var dpr = window.devicePixelRatio || 1;
        // Same size: nothing to do (setting canvas.width clears the canvas). The
        // buffer's scale is the one render() last chose (lower when zoomed far out).
        var active = this._activeDpr || dpr;
        if (width === this._width && height === this._height &&
                this.canvas.width === Math.round(width * active) && this.canvas.height === Math.round(height * active)) return;
        
        this.canvas.width = Math.round(width * dpr);
        this.canvas.height = Math.round(height * dpr);
        this._activeDpr = dpr;       // what the buffer is now; render() lowers it again if it needs to
        this._mainShownKey = null;   // resizing clears the canvas
        this.canvas.style.width = width + 'px';
        this.canvas.style.height = height + 'px';
        
        this.ctx.setTransform(1, 0, 0, 1, 0, 0);
        this.ctx.scale(dpr, dpr);
        
        this._width = width;
        this._height = height;
        this._needsRender = true;
    },

    // =========================================================================
    // RENDER LOOP (a frame itself: render() in canvasRendererFrame.js)
    // =========================================================================
    
    startRenderLoop: function() {
        if (this._rafId) return;
        // Hidden panel (onPrismaReady / onPanelHiding): onPanelShowing starts it
        if (window._panelVisible === false) return;
        // Another tab in front (settings, scan): switchTab starts it on the way back
        if (typeof state !== 'undefined' && state.currentTab && state.currentTab !== 'spellTree') return;
        
        var self = this;
        console.log('[CanvasRenderer] Starting render loop');
        // Opening the panel or coming back to the tree counts as input: not idle
        this._lastInputAt = performance.now();
        
        // Throttle animation renders to reduce CPU load
        var lastAnimationRender = 0;
        var animationThrottleMs = self.ANIMATION_FRAME_MS;
        
        function loop(timestamp) {
            if (typeof PerfMeter !== 'undefined') PerfMeter.tick();
            var shouldRender = self._needsRender;
            
            // For animation-only updates, throttle to save CPU. Setting
            // _needsRender clears the flag, so a frame the player asked for
            // (pan, zoom, hover, selection) is never held back; an animation
            // sets it again afterwards and keeps the throttle - including the
            // learning path, which redraws the whole tree layer per frame.
            if (shouldRender && self._animationOnlyRender) {
                // A click or drag gets the browser first: no animation frame
                // while a button is held or right after a press, wheel or key.
                // Idle, a frame that repaints the whole tree canvas (moving
                // stars) comes less often.
                // Nobody touching the panel: slower, then none at all. Every
                // frame, however small what changed, is a repaint and a texture
                // upload of the whole panel in the game's browser.
                var nowMs = performance.now();
                var idleFor = nowMs - self._lastInputAt;
                var throttle = idleFor > self.IDLE_AFTER_MS ? self.IDLE_FRAME_MS : animationThrottleMs;
                if (idleFor > self.IDLE_STOP_MS || self.isPanning || nowMs - self._lastPressAt < self.INPUT_QUIET_MS ||
                        timestamp - lastAnimationRender < throttle) {
                    shouldRender = false;
                } else {
                    lastAnimationRender = timestamp;
                }
            }
            
            // The next frame is booked in `finally`. Booking it after render()
            // meant one throw ended the loop for good: _rafId still held the id
            // of the frame that had already fired, so startRenderLoop's guard
            // treated the dead loop as running and the tree never came back.
            try {
                if (shouldRender) {
                    self._needsRender = false;
                    self._animationOnlyRender = false;
                    self.render();
                }
            } catch (e) {
                // Once per session: a frame that throws usually throws every frame
                if (!self._renderErrorLogged) {
                    self._renderErrorLogged = true;
                    console.error('[CanvasRenderer] Frame failed, loop continues: ' + (e && e.message ? e.message : e));
                }
            } finally {
                self._rafId = requestAnimationFrame(loop);
            }
        }
        
        loop(performance.now());
    },
    
    /** A frame for the heart, the globe or the stars: the tree layer is pasted, not redrawn. */
    _requestAnimationOnlyFrame: function() {
        this.__needsRender = true;
        this._animationOnlyRender = true;
    },

    /** No mouse or key input for IDLE_AFTER_MS. */
    _isIdle: function() {
        return performance.now() - this._lastInputAt > this.IDLE_AFTER_MS;
    },

    stopRenderLoop: function() {
        if (typeof PerfMeter !== 'undefined') PerfMeter.pause();
        if (this._rafId) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    },
    
    forceRender: function() {
        this._needsRender = true;
        this.render();
    },

    // =========================================================================
    // PUBLIC API
    // =========================================================================
    
    show: function() {
        if (!this.canvas || !this.container) {
            console.error('[CanvasRenderer] Cannot show - not initialized');
            return;
        }
        
        var svg = document.getElementById('tree-svg');
        if (svg) svg.style.display = 'none';
        
        if (!this.canvas.parentNode) {
            this.container.appendChild(this.canvas);
        }
        // The container's own background and inner shadow lie under the opaque
        // canvas: not painted while it is there (patch-ui.css, design CSS)
        this.container.classList.add('canvas-shown');
        // The moving parts' canvases go back over it (FxLayer)
        if (typeof FxLayer !== 'undefined') FxLayer.reattach(this.canvas);

        // Update canvas size immediately
        this.updateCanvasSize();
        this.startRenderLoop();
        this.forceRender();
        
        // Also update after a brief delay to catch layout changes
        var self = this;
        setTimeout(function() {
            self.updateCanvasSize();
        }, 100);
        
        console.log('[CanvasRenderer] Shown with', this.nodes.length, 'nodes');
    },
    
    hide: function() {
        this.stopRenderLoop();
        if (typeof FxLayer !== 'undefined') FxLayer.hideAll();
        
        // Clear any pending resize timeout
        if (this._resizeTimeout) {
            clearTimeout(this._resizeTimeout);
            this._resizeTimeout = null;
        }
        
        if (this.canvas && this.canvas.parentNode) {
            this.canvas.parentNode.removeChild(this.canvas);
        }
        if (this.container) this.container.classList.remove('canvas-shown');
        
        var svg = document.getElementById('tree-svg');
        if (svg) svg.style.display = 'block';
    },
    
    centerView: function() {
        this.panX = 0;
        this.panY = 0;
        this.zoom = 0.75;
        this.rotation = 0;
        this._needsRender = true;
        
        this.showZoom(this.zoom);
    },
    
    /**
     * The zoom read-out; the text is only written when the shown number changes
     * (compared with the element, which the other tree views write too).
     */
    showZoom: function(zoom) {
        var zoomEl = this._zoomLevelEl || document.getElementById('zoom-level');
        if (!zoomEl) return;
        var text = Math.round(zoom * 100) + '%';
        if (zoomEl.textContent !== text) zoomEl.textContent = text;
    },

    setZoom: function(z) {
        this.zoom = Math.max(0.1, Math.min(5, z));
        this._needsRender = true;
        
        this.showZoom(this.zoom);
    },
    
    clear: function() {
        this.nodes = [];
        this.edges = [];
        this.schools = {};
        this._nodeMap = new Map();
        this._nodeByFormId = new Map();
        this._nodeGrid = {};
        this._discoveryVisibleIds = null;
        this._nodeBuckets = null;
        this._cachedDividerGradients = null;
        this._dividerCacheKey = '';
        this._activeDpr = 0;
        this.selectedNode = null;
        this.hoveredNode = null;

        if (this.ctx && this.canvas) {
            this.ctx.setTransform(1, 0, 0, 1, 0, 0);
            this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        }
        
        this._needsRender = true;
    },
    
    /**
     * Refresh renderer when node states change (e.g., spell unlocked)
     */
    refresh: function() {
        // Rebuild discovery visibility if in discovery mode
        this._buildDiscoveryVisibility();
        this._buildLearningPaths();
        this._buildNodeBuckets();
        this._needsRender = true;
    }
};

// Dozens of places, in the renderer's files and in other modules, say "something about the
// tree changed" by setting _needsRender. Catching that one assignment is what
// lets the tree layer know when it is out of date without touching any of them.
// Frames asked for by animation alone go through _requestAnimationOnlyFrame,
// which writes the backing field directly and so leaves the layer alone.
Object.defineProperty(CanvasRenderer, '_needsRender', {
    get: function() { return this.__needsRender; },
    set: function(value) {
        this.__needsRender = value;
        if (value) {
            this._treeDirty = true;
            // This is the player asking, so the frame is not throttleable.
            // The flag was left over from the previous frame's heartbeat, and
            // leaving it set is what used to hold a drag to 20 frames a second.
            // An animation that wants the throttle sets it again right after.
            this._animationOnlyRender = false;
        }
    },
    enumerable: true,
    configurable: true
});
CanvasRenderer._needsRender = true;

// Export
window.CanvasRenderer = CanvasRenderer;
