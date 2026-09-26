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
    EXPERIMENT_TURN_MS: 16,       // PerfExperiment 'timer': an idle loop turn waits this long
    TREE_LAYER_MARGIN: 128,       // css px drawn beyond each edge, so a drag slides the layer (was 256:
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
    // SELF-CONTAINED SCHOOL COLORS (no TREE_CONFIG dependency)
    // =========================================================================

    _defaultSchoolColors: {
        'Destruction': '#ef4444',
        'Restoration': '#facc15',
        'Alteration': '#22c55e',
        'Conjuration': '#a855f7',
        'Illusion': '#38bdf8'
    },

    /**
     * Get school color - checks settings first, then defaults
     * Replaces TREE_CONFIG.getSchoolColor()
     */
    _getSchoolColor: function(school) {
        var color;
        if (typeof settings !== 'undefined' && settings.schoolColors && settings.schoolColors[school]) {
            color = settings.schoolColors[school];
        } else if (typeof getOrAssignSchoolColor === 'function') {
            color = getOrAssignSchoolColor(school);
        } else {
            color = this._defaultSchoolColors[school] || '#888888';
        }
        // As the design preset's ink (unchanged unless the preset asks)
        return TreeStyle.ink(color);
    },

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

    // =========================================================================
    // PATH2D CACHE - Pre-computed shapes for performance
    // =========================================================================
    
    _shapePaths: null,
    
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
    _getShapePath: function(school) {
        return this._shapePaths[school] || this._shapePaths['default'];
    },

    // =========================================================================
    // LOD (Level of Detail) SYSTEM
    // =========================================================================

    /**
     * Compute LOD tier based on current zoom level.
     * Returns 'full', 'simple', or 'minimal'.
     * Forces 'full' when EditMode is active.
     */
    _computeLODTier: function() {
        // EditMode always needs full detail for accurate editing
        if (typeof EditMode !== 'undefined' && EditMode.isActive) {
            return 'full';
        }
        if (this.zoom >= 0.45) return 'full';
        if (this.zoom >= 0.25) return 'simple';
        return 'minimal';
    },

    /**
     * Build node buckets grouped by 'school|state' for batched minimal rendering.
     * Pre-caches school color on each node to avoid per-node lookups.
     */
    _buildNodeBuckets: function() {
        this._nodeBuckets = {};
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];
            var key = (node.school || 'unknown') + '|' + (node.state || 'locked');
            if (!this._nodeBuckets[key]) {
                this._nodeBuckets[key] = [];
            }
            this._nodeBuckets[key].push(node);
            // Pre-cache school color on node
            node._cachedSchoolColor = node.themeColor ? TreeStyle.ink(node.themeColor) : this._getSchoolColor(node.school);
        }
    },

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
    // DATA MANAGEMENT
    // =========================================================================
    
    setData: function(nodes, edges, schools) {
        this.selectedNode = null;
        this.hoveredNode = null;
        this.rotation = 0;

        this.nodes = nodes || [];
        this.edges = edges || [];
        this.schools = schools || {};

        // Check if nodes need position calculation (all at center = no positions)
        var nodesNeedLayout = false;
        if (this.nodes.length > 0) {
            var nodesWithPositions = this.nodes.filter(function(n) {
                return (n.x !== 0 || n.y !== 0) || n._fromVisualFirst || n._fromLayoutEngine;
            }).length;
            nodesNeedLayout = nodesWithPositions < this.nodes.length * 0.1; // <10% have positions
            console.log('[CanvasRenderer] Position check: ' + nodesWithPositions + '/' + this.nodes.length + ' have positions, needLayout=' + nodesNeedLayout);
        }

        // If nodes don't have positions, use simple spiral layout as fallback
        if (nodesNeedLayout) {
            console.log('[CanvasRenderer] FALLBACK: Applying spiral layout for ' + this.nodes.length + ' nodes');
            this._applySpiralLayout();
        }

        // Build lookup maps
        this._nodeMap = new Map();
        this._nodeByFormId = new Map();
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];
            this._nodeMap.set(node.id, node);
            if (node.formId) {
                this._nodeByFormId.set(node.formId, node);
                this._nodeMap.set(node.formId, node);
            }
        }

        this.buildSpatialIndex();
        this._computeSchoolAngles();
        this._buildDiscoveryVisibility();
        this._buildLearningPaths();
        this._buildNodeBuckets();

        this._needsRender = true;
        this._logNextRender = true;

        // Navigation chrome follows the loaded schools (all load paths end up here)
        if (typeof TreeNav !== 'undefined') TreeNav.buildSchoolTabs();

        console.log('[CanvasRenderer] Data set:', this.nodes.length, 'nodes,', this.edges.length, 'edges');
    },
    
    _computeSchoolAngles: function() {
        var schoolNames = Object.keys(this.schools);
        if (schoolNames.length === 0) return;

        var self = this;
        var numSchools = schoolNames.length;
        var sliceAngle = 360 / numSchools;

        // Build a lookup map { schoolName: [nodes] } once — O(n) instead of
        // O(n * numSchools) from repeated .filter() calls per school
        var schoolNodeMap = {};
        var i;
        for (i = 0; i < schoolNames.length; i++) {
            schoolNodeMap[schoolNames[i]] = [];
        }
        for (i = 0; i < this.nodes.length; i++) {
            var n = this.nodes[i];
            if (n.school && schoolNodeMap[n.school]) {
                schoolNodeMap[n.school].push(n);
            }
        }

        // DATA-DRIVEN: Derive sector centers from actual root node positions
        // This ensures dividers always align with nodes regardless of generation formula
        for (i = 0; i < schoolNames.length; i++) {
            var name = schoolNames[i];
            var school = self.schools[name];

            // Skip if already set from external data
            if (school.spokeAngle !== undefined && school.startAngle !== undefined) {
                continue;
            }

            var allSchoolNodes = schoolNodeMap[name];

            // Find root node(s) for this school from pre-built lookup
            var rootNodes = [];
            var j;
            for (j = 0; j < allSchoolNodes.length; j++) {
                if (allSchoolNodes[j].isRoot) {
                    rootNodes.push(allSchoolNodes[j]);
                }
            }

            // Fallback: find node closest to center
            if (rootNodes.length === 0 && allSchoolNodes.length > 0) {
                var closest = allSchoolNodes[0];
                var closestDist = closest.x * closest.x + closest.y * closest.y;
                for (j = 1; j < allSchoolNodes.length; j++) {
                    var dist = allSchoolNodes[j].x * allSchoolNodes[j].x + allSchoolNodes[j].y * allSchoolNodes[j].y;
                    if (dist < closestDist) {
                        closest = allSchoolNodes[j];
                        closestDist = dist;
                    }
                }
                rootNodes = [closest];
            }

            if (rootNodes.length > 0) {
                // Average angle of all root nodes = sector center
                var sumSin = 0, sumCos = 0;
                for (j = 0; j < rootNodes.length; j++) {
                    var a = Math.atan2(rootNodes[j].y, rootNodes[j].x);
                    sumSin += Math.sin(a);
                    sumCos += Math.cos(a);
                }
                var avgAngle = Math.atan2(sumSin, sumCos) * 180 / Math.PI;

                school.spokeAngle = avgAngle;
                school.startAngle = avgAngle - sliceAngle / 2;
                school.endAngle = avgAngle + sliceAngle / 2;
                school.angleSpan = sliceAngle;
            } else {
                // No nodes at all — fallback to even distribution
                var startAngle = i * sliceAngle - 90;
                school.startAngle = startAngle;
                school.endAngle = startAngle + sliceAngle;
                school.angleSpan = sliceAngle;
                school.spokeAngle = startAngle + sliceAngle / 2;
            }
        }
    },
    
    /**
     * Simple spiral fallback layout for nodes without pre-baked positions.
     * Groups nodes by school, places roots around the center, spirals children outward.
     */
    _applySpiralLayout: function() {
        var schoolNames = Object.keys(this.schools);
        if (schoolNames.length === 0) {
            // No schools: simple spiral for all nodes
            var spacing = 55;
            for (var i = 0; i < this.nodes.length; i++) {
                var angle = i * 2.4;  // golden angle approximation
                var radius = 100 + i * spacing * 0.15;
                this.nodes[i].x = Math.cos(angle) * radius;
                this.nodes[i].y = Math.sin(angle) * radius;
            }
            return;
        }

        var numSchools = schoolNames.length;
        var sliceAngle = (2 * Math.PI) / numSchools;
        var self = this;

        // Build parent lookup from edges
        var childrenOf = {};
        for (var e = 0; e < this.edges.length; e++) {
            var edge = this.edges[e];
            if (!childrenOf[edge.from]) childrenOf[edge.from] = [];
            childrenOf[edge.from].push(edge.to);
        }

        schoolNames.forEach(function(name, schoolIdx) {
            var school = self.schools[name];
            var schoolNodes = self.nodes.filter(function(n) { return n.school === name; });
            if (schoolNodes.length === 0) return;

            var baseAngle = schoolIdx * sliceAngle - Math.PI / 2;
            var baseRadius = 100;
            var tierSpacing = 55;
            var arcSpread = sliceAngle * 0.7;

            // BFS from root(s)
            var rootId = school.root;
            var rootNode = rootId ? schoolNodes.find(function(n) { return n.id === rootId || n.formId === rootId; }) : null;
            if (!rootNode) rootNode = schoolNodes[0];

            // Place root
            rootNode.x = Math.cos(baseAngle) * baseRadius;
            rootNode.y = Math.sin(baseAngle) * baseRadius;

            var placed = new Set([rootNode.id]);
            var queue = [{ node: rootNode, depth: 0 }];
            var depthCounters = {};

            while (queue.length > 0) {
                var item = queue.shift();
                var parentNode = item.node;
                var depth = item.depth + 1;
                var radius = baseRadius + depth * tierSpacing;

                var kids = childrenOf[parentNode.id] || [];
                if (kids.length === 0 && parentNode.formId) {
                    kids = childrenOf[parentNode.formId] || [];
                }

                if (!depthCounters[depth]) depthCounters[depth] = 0;

                for (var k = 0; k < kids.length; k++) {
                    var childId = kids[k];
                    if (placed.has(childId)) continue;

                    var childNode = schoolNodes.find(function(n) { return n.id === childId || n.formId === childId; });
                    if (!childNode) continue;

                    var idx = depthCounters[depth]++;
                    var totalAtDepth = Math.max(kids.length, 3);
                    var angleOffset = (idx - (totalAtDepth - 1) / 2) * (arcSpread / Math.max(totalAtDepth - 1, 1));
                    childNode.x = Math.cos(baseAngle + angleOffset) * radius;
                    childNode.y = Math.sin(baseAngle + angleOffset) * radius;

                    placed.add(childNode.id);
                    queue.push({ node: childNode, depth: depth });
                }
            }

            // Place any unplaced nodes (orphans) in a spiral within the school sector
            var orphanIdx = 0;
            schoolNodes.forEach(function(n) {
                if (!placed.has(n.id)) {
                    var oAngle = baseAngle + (orphanIdx * 0.5 - arcSpread / 2);
                    var oRadius = baseRadius + 200 + orphanIdx * 30;
                    n.x = Math.cos(oAngle) * oRadius;
                    n.y = Math.sin(oAngle) * oRadius;
                    orphanIdx++;
                }
            });
        });

        console.log('[CanvasRenderer] Spiral layout applied to ' + this.nodes.length + ' nodes');
    },

    /**
     * Build discovery mode visibility set
     * Shows: unlocked, available, and locked nodes ONE STEP from available/unlocked
     */
    _buildDiscoveryVisibility: function() {
        if (!settings.discoveryMode || settings.cheatMode) {
            this._discoveryVisibleIds = null;
            return;
        }
        
        var visible = new Set();
        var availableOrUnlockedIds = new Set();
        
        // First pass: collect unlocked, learning, and available nodes
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];
            if (node.state === 'unlocked' || node.state === 'learning' || node.state === 'available') {
                visible.add(node.id);
                if (node.formId) visible.add(node.formId);
                availableOrUnlockedIds.add(node.id);
                if (node.formId) availableOrUnlockedIds.add(node.formId);
            }
        }
        
        // Second pass: find locked nodes ONE STEP away from visible
        for (var i = 0; i < this.edges.length; i++) {
            var edge = this.edges[i];
            var fromVisible = availableOrUnlockedIds.has(edge.from);
            var toVisible = availableOrUnlockedIds.has(edge.to);
            
            if (fromVisible && !toVisible) {
                visible.add(edge.to);
            }
            if (toVisible && !fromVisible) {
                visible.add(edge.from);
            }
        }
        
        this._discoveryVisibleIds = visible;
    },
    
    buildSpatialIndex: function() {
        this._nodeGrid = {};
        
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];
            var cellX = Math.floor(node.x / this._gridCellSize);
            var cellY = Math.floor(node.y / this._gridCellSize);
            var key = cellX + ',' + cellY;
            
            if (!this._nodeGrid[key]) {
                this._nodeGrid[key] = [];
            }
            this._nodeGrid[key].push(node);
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
    },

    /**
     * Select a node, open its details, and bring it to the center of the view
     * (vanilla perk-menu style). Used by click, Find Spell, and prereq links.
     * @param {Object} node
     * @param {Object} [focusOpts] Options forwarded to TreeCamera.focusNode
     */
    selectNodeAndFocus: function(node, focusOpts) {
        if (!node) return;

        this.selectedNode = node;
        this._buildSelectedPathToRoot(node);
        this._needsRender = true;

        console.log('[CanvasRenderer] Node selected:', node.name || node.id);

        // Dispatch nodeSelected FIRST so the details panel is open when the
        // camera computes its side-panel offset
        window.dispatchEvent(new CustomEvent('nodeSelected', { detail: node }));

        var focusEnabled = (typeof settings === 'undefined') || settings.focusOnClick !== false;
        if (focusEnabled && typeof TreeCamera !== 'undefined') {
            TreeCamera.focusNode(node, focusOpts);
        } else if (focusEnabled || (typeof settings !== 'undefined' && settings.focusRotate === true)) {
            // No camera module: the wheel turning to the school is all that is
            // left, so do it rather than leave the selection off screen. When
            // the player turned focus off, only an explicit focusRotate asks.
            this.rotateSchoolToTop(node.school);
        }
    },

    /**
     * Build the complete path from selected node in BOTH directions:
     * - Back to root (ancestors via prerequisites)
     * - Forward to leaves (descendants via children)
     * Stores edges in _selectedPathEdges for highlighting.
     */
    _buildSelectedPathToRoot: function(node) {
        var sets = this._computePathSets(node);
        this._selectedPathEdges = sets.edges;
        this._selectedPathNodes = sets.nodes;

        // A selected node no longer needs its hover preview
        this._hoverPathEdges = null;
        this._hoverPathNodes = null;

        console.log('[CanvasRenderer] Selected path (bidirectional): ' + sets.nodes.size + ' nodes, ' + sets.edges.size + ' edges');
    },

    /**
     * Compute the bidirectional dependency path sets for a node
     * (ancestors via prerequisites, descendants via children).
     * Shared by selection highlighting and hover preview.
     * @param {Object} node
     * @returns {{edges: Set, nodes: Set}} edge keys are 'from->to'
     */
    _computePathSets: function(node) {
        var edges = new Set();
        var nodes = new Set();
        if (!node || !this._nodeMap) return { edges: edges, nodes: nodes };

        nodes.add(node.id);

        // Walk one direction: getLinks(node) returns ids, makeKey(id, currentId) builds the edge key
        var self = this;
        function walk(getLinks, makeKey) {
            var visited = new Set();
            var queue = [node.id];
            while (queue.length > 0) {
                var currentId = queue.shift();
                if (visited.has(currentId)) continue;
                visited.add(currentId);

                var currentNode = self._nodeMap.get(currentId);
                if (!currentNode) continue;

                nodes.add(currentId);

                var links = getLinks(currentNode) || [];
                for (var i = 0; i < links.length; i++) {
                    var linkId = links[i];
                    edges.add(makeKey(linkId, currentId));
                    if (!visited.has(linkId)) queue.push(linkId);
                }
            }
        }

        // Back to root (via prerequisites): edge is prereq -> current
        walk(function(n) { return n.prerequisites; }, function(linkId, currentId) { return linkId + '->' + currentId; });
        // Forward to leaves (via children): edge is current -> child
        walk(function(n) { return n.children; }, function(linkId, currentId) { return currentId + '->' + linkId; });

        return { edges: edges, nodes: nodes };
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
        return 7 + tierIndex;
    },

    /**
     * XP progress (0..1) for a node, using the same lookups as the details panel.
     * Returns 0 when there is no progress data.
     */
    /**
     * Colours both a design and the player set (learning path, heart, globe):
     * the player's once they changed it from a shipped default, else the design's
     * token, else the player's. A design like Arcane re-colours them for its page,
     * but a colour the player picked is kept.
     */
    PLAYER_COLOR_DEFAULTS: {
        learningPathColor: ['#00ffff'],
        heartRingColor: ['#b8a878'],
        heartBgColor: ['#000000', '#0a0a14'],   // panel default, C++ default config
        magicTextColor: ['#ffecb3', '#b8a878'],  // state default, swatch reset
        globeColor: ['#b8a878']
    },

    _designOrPlayer: function(tokenName, settingName, playerValue) {
        var token = TreeStyle.tokens[tokenName];
        if (!token) return playerValue;
        var defaults = this.PLAYER_COLOR_DEFAULTS[settingName] || [];
        var own = playerValue ? String(playerValue).toLowerCase() : '';
        if (own && defaults.indexOf(own) < 0) return playerValue;
        return token;
    },

    /** Learning colour: see _designOrPlayer. */
    _learningColor: function() {
        return this._designOrPlayer('learningColor', 'learningPathColor', this._learningPathColor) || '#00ffff';
    },

    _heartRing: function() {
        return this._designOrPlayer('hubRing', 'heartRingColor', this._heartRingColor) || '#b8a878';
    },

    _getNodeProgressPct: function(node) {
        if (typeof state === 'undefined' || !state.spellProgress) return 0;
        var canonId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
        var progress = state.spellProgress[canonId];
        if (!progress || !progress.xp) return 0;

        var required = (typeof getRequiredXPForNode === 'function') ? getRequiredXPForNode(node) : null;
        if (!required) required = progress.required || 100;
        return required > 0 ? Math.min(progress.xp / required, 1) : 0;
    },
    
    // =========================================================================
    // RENDERING
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
                // PerfExperiment 'timer' (developer mode): a turn with nothing
                // drawn books the next with a timer, not an animation frame
                self._rafIsTimer = !shouldRender && typeof PerfExperiment !== 'undefined' && PerfExperiment.timerLoop();
                self._rafId = self._rafIsTimer
                    ? setTimeout(function() { loop(performance.now()); }, self.EXPERIMENT_TURN_MS)
                    : requestAnimationFrame(loop);
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
            if (this._rafIsTimer) clearTimeout(this._rafId);
            else cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    },
    
    forceRender: function() {
        this._needsRender = true;
        this.render();
    },
    
    render: function() {
        if (!this.ctx || !this.canvas) return;
        
        var startTime = performance.now();
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
        if (!stretch && (this._treeDirty || this._treeLayerStale || slid || viewTurned)) {
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
            dx = 0;
            dy = 0;
        }

        var w = this.canvas.width, h = this.canvas.height;
        // With a spell hovered: the preview on its own spot (FxLayer frame), else
        // the layer with the preview on it, cached until the hover or the layer
        // changes (HoverOverlay.composite)
        var hoverSpot = this._fxThisFrame && typeof HoverOverlay !== 'undefined' && HoverOverlay.drawSpot;
        var src = (!hoverSpot && typeof HoverOverlay !== 'undefined') ? HoverOverlay.composite(this, layer, dpr, margin, view) : layer;
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

    /** The camera, a wheel rotation or the mouse wheel is moving the view right now. */
    _viewInMotion: function() {
        return this.isAnimating || (performance.now() - (this._wheelAt || 0)) < this.WHEEL_SETTLE_MS;
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
        this.renderLabels(ctx, cx, cy, cos, sin, view.labelMargin || 0);
        if (pm) this._partAt = PerfMeter.part('labels', this._partAt);

        // Chapter titles: school names past each school's outer edge (design preset)
        TreeStyle.renderChapters(ctx, this, cx, cy, cos, sin);
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
        this.renderEdges(ctx, viewLeft, viewRight, viewTop, viewBottom);
        if (pm) this._partAt = PerfMeter.part('edges', this._partAt);
        // The learning path animation and the detached particles move every
        // frame: they are drawn over the pasted layer (_drawLearningPath, _drawTree)
        this.renderNodes(ctx, viewLeft, viewRight, viewTop, viewBottom);
        if (pm) this._partAt = PerfMeter.part('nodes', this._partAt);
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
    
    renderSchoolDividers: function(ctx) {
        // Check if dividers are enabled
        if (!settings.showSchoolDividers) return;

        // LOD: Skip dividers entirely in MINIMAL tier
        if (this._lodTier === 'minimal') return;

        var schoolNames = Object.keys(this.schools);
        if (schoolNames.length < 2) return;

        var length = settings.dividerLength !== undefined ? settings.dividerLength : 800;
        var fade = (settings.dividerFade !== undefined ? settings.dividerFade : 50) / 100;  // Convert to 0-1
        var lineWidth = settings.dividerSpacing !== undefined ? settings.dividerSpacing : 3;
        var colorMode = settings.dividerColorMode || 'school';
        var customColor = settings.dividerCustomColor || '#ffffff';
        var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };

        // A design preset can draw them as book rules instead
        if (TreeStyle.renderOrnamentDividers(ctx, this, length, gd)) return;

        // Build cache key from all settings that affect gradients
        var cacheKey = length + '|' + fade + '|' + lineWidth + '|' + colorMode + '|' + customColor + '|' + gd.x + '|' + gd.y + '|' + schoolNames.length;
        for (var ci = 0; ci < schoolNames.length; ci++) {
            var sch = this.schools[schoolNames[ci]];
            cacheKey += '|' + (sch.startAngle || 0);
            if (colorMode === 'school') cacheKey += '|' + this._getSchoolColor(schoolNames[ci]);
        }

        // Rebuild gradient cache if settings changed
        if (this._dividerCacheKey !== cacheKey || !this._cachedDividerGradients) {
            this._dividerCacheKey = cacheKey;
            this._cachedDividerGradients = [];
            var startAlpha = 0.8;
            var endAlpha = startAlpha * (1 - fade);

            for (var i = 0; i < schoolNames.length; i++) {
                var schoolName = schoolNames[i];
                var school = this.schools[schoolName];
                var angle = school.startAngle !== undefined ? school.startAngle : (i * (360 / schoolNames.length) - 90);
                var rad = angle * Math.PI / 180;

                var color;
                if (colorMode === 'custom') {
                    color = customColor;
                } else {
                    color = this._getSchoolColor(schoolName) || '#ffffff';
                }

                var endX = gd.x + Math.cos(rad) * length;
                var endY = gd.y + Math.sin(rad) * length;
                var gradient = ctx.createLinearGradient(gd.x, gd.y, endX, endY);

                var rgb = this._hexToRgb(color);
                gradient.addColorStop(0, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + startAlpha + ')');
                gradient.addColorStop(0.5, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + (startAlpha * 0.7 + endAlpha * 0.3) + ')');
                gradient.addColorStop(1, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + endAlpha + ')');

                this._cachedDividerGradients.push({
                    gradient: gradient,
                    startX: gd.x, startY: gd.y,
                    endX: endX, endY: endY
                });
            }
        }

        ctx.lineWidth = lineWidth;
        ctx.lineCap = 'round';

        // Draw using cached gradients
        for (var i = 0; i < this._cachedDividerGradients.length; i++) {
            var cached = this._cachedDividerGradients[i];
            ctx.strokeStyle = cached.gradient;
            ctx.beginPath();
            ctx.moveTo(cached.startX, cached.startY);
            ctx.lineTo(cached.endX, cached.endY);
            ctx.stroke();
        }
    },
    
    _hexToRgb: function(hex) {
        hex = hex.replace('#', '');
        return {
            r: parseInt(hex.substring(0, 2), 16),
            g: parseInt(hex.substring(2, 4), 16),
            b: parseInt(hex.substring(4, 6), 16)
        };
    },
    
    _hexToRgba: function(hex, alpha) { return hexToRgba(hex, alpha); },

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
            ctx.lineTo(x2, y2);
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
            if (maxX < viewLeft || minX > viewRight || maxY < viewTop || minY > viewBottom) {
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
            ctx.strokeStyle = S.lockedEdgeColor;
            ctx.lineWidth = 1;
            ctx.globalAlpha = S.lockedEdgeAlpha;
            strokeBatch(batches.locked);
        }
        for (var fk = 0; fk < frontierKeys.length; fk++) {
            ctx.strokeStyle = frontierKeys[fk].substring(2);
            ctx.lineWidth = 1.25;
            ctx.globalAlpha = S.frontierEdgeAlpha;
            strokeBatch(batches[frontierKeys[fk]]);
        }
        for (var uk = 0; uk < unlockedKeys.length; uk++) {
            var uList = batches[unlockedKeys[uk]];
            ctx.strokeStyle = unlockedKeys[uk].substring(2);
            if (S.edgeGlow > 0) {
                // A wide faint stroke under the line: the channel glows
                ctx.lineWidth = S.unlockedEdgeWidth * 3;
                ctx.globalAlpha = S.edgeGlow;
                strokeBatch(uList);
            }
            // Unlocked connections always visible
            ctx.lineWidth = S.unlockedEdgeWidth;
            ctx.globalAlpha = S.unlockedEdgeAlpha;
            strokeBatch(uList);
        }
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
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];

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
        var path = this._getShapePath(node.school);
        var isLearning = node.state === 'learning';

        var size, fillColor, strokeColor, strokeWidth, alpha;

        var style = TreeStyle.tokens;
        if (node.state === 'unlocked') {
            size = 12; fillColor = style.unlockedFill || schoolColor; strokeColor = style.unlockedRim || schoolColor;
            strokeWidth = 1.5; alpha = 1.0;
        } else if (isLearning) {
            size = 12; fillColor = learningPathColor; strokeColor = learningPathColor;
            strokeWidth = 1.5; alpha = 1.0;
        } else if (node.state === 'available') {
            size = 9; fillColor = style.nodeFill; strokeColor = schoolColor;
            strokeWidth = 1; alpha = style.availableAlpha;
        } else {
            size = 7; fillColor = style.nodeFill; strokeColor = style.lockedStroke || schoolColor;
            strokeWidth = 1; alpha = 0.4;
        }

        alpha *= this._contextFactor(node);
        size = this._minSize(size);

        if (isSelected || isHovered) {
            size += 1.5; strokeColor = style.focusStroke; strokeWidth = 1.5; alpha = 1.0;
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
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];

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
     * A locked or known spell with nothing of its own (not selected, hovered or
     * on the hover path) goes into NodeBatch, with the look renderNode would
     * give it - the lock look too (hard prerequisites: a grey shell with a
     * school-coloured hole while locked, a grey ring once known). Returns false
     * for the others.
     */
    _batchPlainNode: function(node) {
        var locked = node.state === 'locked';
        if (!locked && node.state !== 'unlocked') return false;
        if (this.selectedNode && this.selectedNode.id === node.id) return false;
        if (this.hoveredNode && this.hoveredNode.id === node.id) return false;
        if (this._hoverPathNodes && this._hoverPathNodes.has(node.id)) return false;
        var style = TreeStyle.tokens;
        var schoolColor = node.themeColor ? TreeStyle.ink(node.themeColor) : this._getSchoolColor(node.school);
        var cf = this._contextFactor(node);
        var lock = node.hardPrereqs && node.hardPrereqs.length > 0;
        if (locked) {
            var lsize = this._minSize(7), alpha = 0.4 * cf;
            if (lock) {
                NodeBatch.addShape(node.school, node.x, node.y, lsize + 2, this.LOCK_SHELL_FILL,
                    this.LOCK_SHELL_STROKE, Math.min(alpha + 0.2, 0.75), false, 1.2, 0);
                NodeBatch.addShape(node.school, node.x, node.y, Math.max(lsize * 0.45, 3), schoolColor,
                    null, Math.min(alpha + 0.15, 0.65), false, 1, 2);
                return true;
            }
            NodeBatch.addShape(node.school, node.x, node.y, lsize, style.nodeFill,
                style.lockedStroke || schoolColor, alpha, true);
            return true;
        }
        var size = this._minSize(12);
        var onPath = (this._learningPathNodes instanceof Set) && this._learningPathNodes.has(node.id) &&
                     !(this._animatingPathNodes && this._animatingPathNodes.has(node.id));
        if (style.nodeGlow > 0) NodeBatch.addHalo(node.x, node.y, size * 2.6, schoolColor, style.nodeGlow * cf);
        if (lock) {
            NodeBatch.addShape(node.school, node.x, node.y, size + 3, this.LOCK_RING_FILL,
                this.LOCK_RING_STROKE, 0.5, false, 1.5, 0, true);
        }
        // The lock ring's spell is not underlaid (renderNode draws it straight over the ring)
        NodeBatch.addShape(node.school, node.x, node.y, size, style.unlockedFill || schoolColor,
            onPath ? this._heartRing() : (style.unlockedRim || schoolColor), cf, false, 1.5, 1, lock);
        NodeBatch.addShape(node.school, node.x, node.y, size * 0.5,
            style.unlockedCore || this.getInnerAccentColor(schoolColor), null, cf, false, 1, 2);
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
    },
    
    renderMysteryNode: function(ctx, node) {
        var color = this._getSchoolColor(node.school);
        var dimmedColor = this.dimColor(color, 0.4);
        var size = this._minSize(this._mysterySize(node));
        var path = this._getShapePath(node.school);
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
    
    renderNode: function(ctx, node) {
        var schoolColor = node.themeColor ? TreeStyle.ink(node.themeColor) : this._getSchoolColor(node.school);
        var isSelected = this.selectedNode && this.selectedNode.id === node.id;
        var isHovered = this.hoveredNode && this.hoveredNode.id === node.id;
        var path = this._getShapePath(node.school);
        
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
    },
    
    /**
     * Render debug grid showing all candidate positions
     * Called within the rotated context
     * Uses ACTUAL school data for alignment
     */
    renderDebugGrid: function(ctx) {
        if (!this.showDebugGrid) return;

        var spacing = 50;
        var extent = 1300;

        ctx.fillStyle = 'rgba(184, 168, 120, 0.35)';
        for (var gx = -extent; gx <= extent; gx += spacing) {
            for (var gy = -extent; gy <= extent; gy += spacing) {
                ctx.beginPath();
                ctx.arc(gx, gy, 3, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    },
    
    /**
     * Toggle debug grid visibility
     */
    toggleDebugGrid: function() {
        this.showDebugGrid = !this.showDebugGrid;
        this._needsRender = true;
        console.log('[CanvasRenderer] Debug grid:', this.showDebugGrid ? 'ON' : 'OFF');
        return this.showDebugGrid;
    },
    
    // =========================================================================
    // LEARNING PATH ANIMATION
    // =========================================================================
    
    /**
     * Build the path from center (0,0) to a node, following prerequisite edges
     * Returns array of {x, y} points
     */
    _buildPathToNode: function(nodeId) {
        var path = [];
        var visited = new Set();
        var node = this._nodeMap.get(nodeId);
        
        if (!node) return path;
        
        // Build path backwards from target to root, then reverse
        var current = node;
        var safety = 100;  // Prevent infinite loops
        
        while (current && safety-- > 0) {
            path.unshift({ x: current.x, y: current.y, node: current });
            visited.add(current.id);
            
            // Find prerequisite (parent node that connects to this one)
            var prereq = null;
            for (var i = 0; i < this.edges.length; i++) {
                var edge = this.edges[i];
                if (edge.to === current.id && !visited.has(edge.from)) {
                    prereq = this._nodeMap.get(edge.from);
                    break;
                }
            }
            
            if (!prereq) break;
            current = prereq;
        }
        
        // Add globe position at start (instead of hardcoded origin)
        var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };
        path.unshift({ x: gd.x, y: gd.y, node: null });
        
        return path;
    },
    
    /**
     * Trigger learning animation for a node
     * Call this when a spell is learned/unlocked
     */
    triggerLearningAnimation: function(nodeId) {
        var node = this._nodeMap ? this._nodeMap.get(nodeId) : null;
        if (!node) {
            console.warn('[CanvasRenderer] triggerLearningAnimation: node not found:', nodeId);
            return;
        }
        
        var path = this._buildPathToNode(nodeId);
        if (path.length < 2) {
            console.warn('[CanvasRenderer] triggerLearningAnimation: path too short');
            return;
        }
        
        var color = this._learningColor();

        // Pre-compute segment lengths for animation (avoids sqrt per frame)
        var segmentLengths = [];
        var totalLength = 0;
        for (var si = 1; si < path.length; si++) {
            var sdx = path[si].x - path[si-1].x;
            var sdy = path[si].y - path[si-1].y;
            var slen = Math.sqrt(sdx * sdx + sdy * sdy);
            segmentLengths.push(slen);
            totalLength += slen;
        }

        this._learningPath = {
            nodeId: nodeId,
            path: path,
            progress: 0,
            startTime: performance.now(),
            color: color,
            segmentLengths: segmentLengths,
            totalLength: totalLength
        };

        // Store which nodes are in THIS specific animating path
        // (so we can hide only these during animation, not other learning paths)
        this._animatingPathNodes = new Set();
        for (var i = 0; i < path.length; i++) {
            if (path[i].node && path[i].node.id) {
                this._animatingPathNodes.add(path[i].node.id);
            }
        }

        // Don't show static learning path until animation completes
        this._learningPathAnimationComplete = false;

        console.log('[CanvasRenderer] Learning animation started for:', node.name, 'path length:', path.length, 'animating nodes:', this._animatingPathNodes.size);
        this._needsRender = true;
    },
    
    /**
     * Render the learning path animation (glowing line from center to spell)
     * Called within rotated context
     */
    renderLearningPath: function(ctx) {
        if (!this._learningPath) return;
        
        var lp = this._learningPath;
        var elapsed = performance.now() - lp.startTime;
        var progress = Math.min(elapsed / this._learningPathDuration, 1);
        
        // Ease-out for smooth arrival
        var easedProgress = 1 - Math.pow(1 - progress, 2);
        
        var path = lp.path;
        if (path.length < 2) return;

        // Use pre-computed segment lengths (cached in triggerLearningAnimation)
        var segmentLengths = lp.segmentLengths;
        var totalLength = lp.totalLength;

        // How far along the path we are
        var targetLength = totalLength * easedProgress;
        
        // Draw the glowing path up to the current progress point
        ctx.save();
        
        // Outer glow
        ctx.beginPath();
        ctx.moveTo(path[0].x, path[0].y);
        
        var drawnLength = 0;
        var lastPoint = path[0];
        
        for (var i = 1; i < path.length; i++) {
            var segLen = segmentLengths[i-1];
            
            if (drawnLength + segLen <= targetLength) {
                // Full segment
                ctx.lineTo(path[i].x, path[i].y);
                lastPoint = path[i];
                drawnLength += segLen;
            } else {
                // Partial segment - interpolate
                var remaining = targetLength - drawnLength;
                var t = remaining / segLen;
                var interpX = path[i-1].x + (path[i].x - path[i-1].x) * t;
                var interpY = path[i-1].y + (path[i].y - path[i-1].y) * t;
                ctx.lineTo(interpX, interpY);
                lastPoint = { x: interpX, y: interpY };
                break;
            }
        }
        
        // Draw line - same style as static learning path
        var learningPathColor = this._learningColor();
        ctx.strokeStyle = learningPathColor;
        ctx.lineWidth = 3;
        ctx.globalAlpha = 0.7;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.stroke();
        
        ctx.restore();
        
        // Continue animation or end it
        if (progress >= 1) {
            if (!this._learningPathAnimationComplete) {
                // Done: the layer is repainted once, now with the whole path in it
                this._learningPathAnimationComplete = true;
                this._animatingPathNodes = null;
                this._needsRender = true;
            }

            // Clear the animation object shortly after completion
            if (elapsed > this._learningPathDuration + 200) {
                this._learningPath = null;
            }
        }

        // Keep the frames coming while it runs, without touching the layer
        if (this._learningPath) this._requestAnimationOnlyFrame();
    },
    
    /**
     * Render labels - SCREEN ALIGNED (don't rotate with wheel).
     * Labels are placed by priority (selected > hovered > learning > available >
     * unlocked) with screen-space collision rejection, so overlapping names no
     * longer pile up. Zoomed out, only the important labels remain.
     */
    /**
     * Spell names, screen-aligned. margin: CSS px drawn round the canvas (the
     * tree layer's): the names there are drawn too, so a drag that slides the
     * layer does not show spells without them - after the ones in view, which
     * come out as they would without it.
     */
    renderLabels: function(ctx, cx, cy, cos, sin, margin) {
        if (this.zoom < this.LABEL_MIN_ZOOM) return;
        margin = margin || 0;
        if (settings.showNodeNames === false) return;

        var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
        var fontSize = settings.nodeFontSize || 10;
        var style = TreeStyle.tokens;
        TreeStyle.beginLabels(ctx, fontSize);
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';

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

        var maxLabels = 150;
        var pad = 2;
        var placed = [];
        var drawn = 0;

        for (var c = 0; c < candidates.length && drawn < maxLabels; c++) {
            var cand = candidates[c];
            var halfW = this._labelWidth(ctx, cand.text) / 2 + pad;
            var rect = { l: cand.x - halfW, r: cand.x + halfW, t: cand.y - pad, b: cand.y + fontSize + pad };

            // Collision rejection: the selected node's label always wins
            var collides = false;
            if (cand.priority < 5) {
                for (var p = 0; p < placed.length; p++) {
                    var o = placed[p];
                    if (rect.l < o.r && rect.r > o.l && rect.t < o.b && rect.b > o.t) { collides = true; break; }
                }
            }
            if (collides) continue;

            placed.push(rect);
            ctx.globalAlpha = this._contextFactor(cand.node);
            TreeStyle.drawLabel(ctx, cand.text, cand.x, cand.y, cand.color);
            drawn++;
        }

        ctx.globalAlpha = 1.0;
    },
    
    _byLabelOrder: function(a, b) {
        if (a.inView !== b.inView) return a.inView ? -1 : 1;
        return b.priority - a.priority;
    },

    /** measureText's width, kept per font and text (the names do not change between repaints). */
    _labelWidth: function(ctx, text) {
        if (this._labelWidthFont !== ctx.font) {
            this._labelWidthFont = ctx.font;
            this._labelWidths = {};
        }
        var key = '#' + text;          // never an Object.prototype name
        var w = this._labelWidths[key];
        if (w === undefined) w = this._labelWidths[key] = ctx.measureText(text).width;
        return w;
    },

    // =========================================================================
    // COLOR UTILITIES
    // =========================================================================
    
    dimColor: function(color, factor) {
        var rgb = this.parseColor(color);
        if (!rgb) return color;
        return 'rgb(' + Math.round(rgb.r * factor) + ',' + 
                        Math.round(rgb.g * factor) + ',' + 
                        Math.round(rgb.b * factor) + ')';
    },
    
    brightenColor: function(color, factor) {
        var rgb = this.parseColor(color);
        if (!rgb) return color;
        return 'rgb(' + Math.min(255, Math.round(rgb.r + (255 - rgb.r) * (factor - 1))) + ',' + 
                        Math.min(255, Math.round(rgb.g + (255 - rgb.g) * (factor - 1))) + ',' + 
                        Math.min(255, Math.round(rgb.b + (255 - rgb.b) * (factor - 1))) + ')';
    },
    
    blendColors: function(color1, color2, t) {
        var rgb1 = this.parseColor(color1);
        var rgb2 = this.parseColor(color2);
        if (!rgb1 || !rgb2) return color1;
        return 'rgb(' + Math.round(rgb1.r + (rgb2.r - rgb1.r) * t) + ',' + 
                        Math.round(rgb1.g + (rgb2.g - rgb1.g) * t) + ',' + 
                        Math.round(rgb1.b + (rgb2.b - rgb1.b) * t) + ')';
    },
    
    getInnerAccentColor: function(color) {
        var cache = this._innerAccentCache || (this._innerAccentCache = {});
        if (cache.hasOwnProperty(color)) return cache[color];
        var rgb = this.parseColor(color);
        var accent = !rgb ? '#1a1a2e' : 'rgb(' + Math.round(rgb.r * 0.35) + ',' +
                                                 Math.round(rgb.g * 0.3) + ',' +
                                                 Math.round(rgb.b * 0.4) + ')';
        cache[color] = accent;
        return accent;
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
    },

    parseColor: function(color) {
        if (!color) return null;
        if (color.startsWith('#')) {
            return {
                r: parseInt(color.slice(1, 3), 16),
                g: parseInt(color.slice(3, 5), 16),
                b: parseInt(color.slice(5, 7), 16)
            };
        }
        var match = color.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/);
        if (match) {
            return {
                r: parseInt(match[1]),
                g: parseInt(match[2]),
                b: parseInt(match[3])
            };
        }
        return null;
    },
    
    // =========================================================================
    // ROTATION
    // =========================================================================
    
    rotateToNode: function(node) {
        if (this.noRotate) return;
        if (!node || typeof node.angle === 'undefined') return;
        var targetRotation = -node.angle;
        var delta = targetRotation - this.rotation;
        while (delta > 180) delta -= 360;
        while (delta < -180) delta += 360;
        this.animateRotation(this.rotation + delta);
    },
    
    rotateSchoolToTop: function(schoolName) {
        if (this.noRotate) return;
        var schoolConfig = this.schools[schoolName];
        if (!schoolConfig || schoolConfig.spokeAngle === undefined) return;
        
        // Formula: rotate so spokeAngle ends up at visual TOP (-90 degrees)
        // targetRotation = -90 - spokeAngle
        var targetRotation = -90 - schoolConfig.spokeAngle;
        var delta = targetRotation - this.rotation;
        while (delta > 180) delta -= 360;
        while (delta < -180) delta += 360;
        this.animateRotation(this.rotation + delta);
    },
    
    animateRotation: function(target) {
        var self = this;
        var start = this.rotation;
        var duration = 300;
        var startTime = performance.now();

        // Re-target instead of dropping the request when already animating
        if (this._rotationRafId) {
            cancelAnimationFrame(this._rotationRafId);
            this._rotationRafId = null;
        }
        this.isAnimating = true;

        function animate() {
            var elapsed = performance.now() - startTime;
            var progress = Math.min(elapsed / duration, 1);
            var eased = 1 - Math.pow(1 - progress, 3);

            self.rotation = start + (target - start) * eased;
            // The layer is stretched and turned while this runs (_drawTree) and
            // repainted on the frame after it ends
            self.__needsRender = true;
            self._animationOnlyRender = false;

            if (progress < 1) {
                self._rotationRafId = requestAnimationFrame(animate);
            } else {
                self.rotation = target;
                self._rotationRafId = null;
                self.isAnimating = false;
            }
        }

        animate();
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
    },
    
    /**
     * Build persistent learning paths - tracks nodes in learning state
     * and the path from center to each learning node
     */
    _buildLearningPaths: function() {
        var log = window.debugOutput || console.log;
        log('[CANVAS] _buildLearningPaths called');
        
        // Clear any traveling particles from previous learning target
        if (typeof Globe3D !== 'undefined' && Globe3D.clearDetachedParticles) {
            Globe3D.clearDetachedParticles();
        }
        
        // Initialize sets (can't use new Set() in property definition for compatibility)
        this._learningNodeIds = new Set();
        this._learningPathNodes = new Set();
        this._learningPathSegments = [];  // Clear cached segments
        this._learningPulses = [];  // Clear active pulses
        
        if (!this.nodes || this.nodes.length === 0) {
            log('[CANVAS] No nodes available');
            return;
        }
        
        log('[CANVAS] Checking ' + this.nodes.length + ' nodes for learning state...');
        
        // Debug: log all unique states
        var statesFound = {};
        for (var s = 0; s < this.nodes.length; s++) {
            var st = this.nodes[s].state || 'undefined';
            statesFound[st] = (statesFound[st] || 0) + 1;
        }
        log('[CANVAS] Node states: ' + JSON.stringify(statesFound));
        
        // Find all nodes in learning state
        var learningFound = [];
        for (var i = 0; i < this.nodes.length; i++) {
            var node = this.nodes[i];
            var nodeState = (node.state || '').toLowerCase();
            
            // Check various possible learning state values
            if (nodeState === 'learning') {
                log('[CANVAS] FOUND learning: ' + node.name + ' (id:' + node.id + ')');
                this._learningNodeIds.add(node.id);
                learningFound.push(node.name || node.id);
                
                // Build path from this node back to root
                var pathNodes = this._getPathToRoot(node.id);
                log('[CANVAS] Path for ' + node.name + ': ' + pathNodes.length + ' nodes');
                for (var j = 0; j < pathNodes.length; j++) {
                    this._learningPathNodes.add(pathNodes[j]);
                }
            }
        }
        
        log('[CANVAS] RESULT: ' + this._learningNodeIds.size + ' learning, ' + this._learningPathNodes.size + ' path nodes');
        if (learningFound.length > 0) {
            log('[CANVAS] Learning: ' + learningFound.join(', '));
        }
    },
    
    /**
     * Get all node IDs from a node back to root (or center)
     */
    _getPathToRoot: function(nodeId) {
        var log = window.debugOutput || console.log;
        var path = [nodeId];
        var visited = new Set([nodeId]);
        var current = nodeId;
        var safety = 100;
        
        // Get the starting node to check if it's already a root
        var startNode = this._nodeMap ? this._nodeMap.get(nodeId) : null;
        if (startNode && (startNode.isRoot || startNode.tier === 1)) {
            log('[CANVAS] Node ' + (startNode.name || nodeId) + ' is root, path = [self]');
            return path;
        }
        
        while (safety-- > 0) {
            // Find prerequisite (parent) node
            var prereqId = null;
            for (var i = 0; i < this.edges.length; i++) {
                var edge = this.edges[i];
                // Try both exact match and formId match
                var formIdNode = this._nodeByFormId ? this._nodeByFormId.get(current) : null;
                var altId = formIdNode ? formIdNode.id : null;
                if ((edge.to === current || (altId && edge.to === altId)) && !visited.has(edge.from)) {
                    prereqId = edge.from;
                    break;
                }
            }
            
            if (!prereqId) break;
            
            path.push(prereqId);
            visited.add(prereqId);
            current = prereqId;
            
            // Check if we reached root
            var currentNode = this._nodeMap ? this._nodeMap.get(current) : null;
            if (currentNode && (currentNode.isRoot || currentNode.tier === 1)) {
                log('[CANVAS] Reached root: ' + (currentNode.name || current));
                break;
            }
        }
        
        log('[CANVAS] Path built: ' + path.length + ' nodes -> ' + path.join(' -> '));
        return path;
    },
    
    /**
     * Build path segments for pulse animation
     * Returns array of {from: {x,y}, to: {x,y}} segments from center to learning nodes
     */
    _buildLearningPathSegments: function() {
        this._learningPathSegments = [];
        
        if (!this._learningNodeIds || this._learningNodeIds.size === 0) return;
        if (!this._nodeMap) return;
        
        var self = this;
        
        // For each learning node, build segments from center through path
        this._learningNodeIds.forEach(function(learningId) {
            var pathNodeIds = self._getPathToRoot(learningId);
            if (pathNodeIds.length === 0) return;
            
            // Reverse so we go from root to learning node
            pathNodeIds.reverse();
            
            var segments = [];
            
            // First segment: globe center to root node
            var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };
            var rootNode = self._nodeMap.get(pathNodeIds[0]);
            if (rootNode) {
                var sdx = rootNode.x - gd.x, sdy = rootNode.y - gd.y;
                segments.push({
                    from: { x: gd.x, y: gd.y },
                    to: { x: rootNode.x, y: rootNode.y },
                    length: Math.sqrt(sdx * sdx + sdy * sdy)
                });
            }

            // Subsequent segments: node to node
            for (var i = 0; i < pathNodeIds.length - 1; i++) {
                var fromNode = self._nodeMap.get(pathNodeIds[i]);
                var toNode = self._nodeMap.get(pathNodeIds[i + 1]);
                if (fromNode && toNode) {
                    var sdx2 = toNode.x - fromNode.x, sdy2 = toNode.y - fromNode.y;
                    segments.push({
                        from: { x: fromNode.x, y: fromNode.y },
                        to: { x: toNode.x, y: toNode.y },
                        length: Math.sqrt(sdx2 * sdx2 + sdy2 * sdy2)
                    });
                }
            }

            if (segments.length > 0) {
                // Pre-compute segmentLengths array and totalLength for pulse animation
                var segLengths = [];
                var totalLen = 0;
                for (var si = 0; si < segments.length; si++) {
                    segLengths.push(segments[si].length);
                    totalLen += segments[si].length;
                }
                self._learningPathSegments.push({
                    learningNodeId: learningId,
                    segments: segments,
                    segmentLengths: segLengths,
                    totalLength: totalLen
                });
            }
        });
    },
    
    /**
     * Calculate total length of path segments
     */
    _calculatePathLength: function(segments) {
        var total = 0;
        for (var i = 0; i < segments.length; i++) {
            var seg = segments[i];
            var dx = seg.to.x - seg.from.x;
            var dy = seg.to.y - seg.from.y;
            total += Math.sqrt(dx * dx + dy * dy);
        }
        return total;
    },
    
    /**
     * Detach a globe particle to travel along learning paths (called on heartbeat)
     */
    _detachGlobeParticleToLearningPath: function() {
        if (typeof Globe3D === 'undefined' || !Globe3D.enabled) return;
        
        if (!this._learningPathSegments || this._learningPathSegments.length === 0) {
            this._buildLearningPathSegments();
        }
        
        if (!this._learningPathSegments || this._learningPathSegments.length === 0) return;
        
        // For each learning path, detach a globe particle with learning color
        var learningColor = this._learningColor();
        for (var i = 0; i < this._learningPathSegments.length; i++) {
            var pathData = this._learningPathSegments[i];
            Globe3D.detachParticleToPath(pathData.segments, this._learningPulseSpeed, learningColor);
        }
    },
    
    /**
     * Update traveling pulses
     */
    _updateLearningPulses: function() {
        if (!this._learningPulses || this._learningPulses.length === 0) return;
        
        // Update each pulse, by the steps due (AnimClock: not faster or slower with the frame rate)
        var steps = typeof AnimClock !== 'undefined' ? AnimClock.steps('pulses') : 1;
        for (var i = this._learningPulses.length - 1; i >= 0; i--) {
            var pulse = this._learningPulses[i];
            pulse.progress += pulse.speed * steps;
            
            // Fade out near the end
            if (pulse.progress > 0.8) {
                pulse.alpha = Math.max(0, 1 - (pulse.progress - 0.8) / 0.2);
            }
            
            // Remove completed pulses
            if (pulse.progress >= 1.0) {
                this._learningPulses.splice(i, 1);
            }
        }
    },
    
    /**
     * Render traveling pulses
     */
    _renderLearningPulses: function(ctx) {
        if (!this._learningPulses || this._learningPulses.length === 0) return;
        if (!this._learningPathSegments || this._learningPathSegments.length === 0) return;
        
        var learningPathColor = this._learningColor();
        
        for (var i = 0; i < this._learningPulses.length; i++) {
            var pulse = this._learningPulses[i];
            var pathData = this._learningPathSegments[pulse.pathIndex];
            
            if (!pathData) continue;
            
            // Find position along path (use cached segment lengths)
            var pos = this._getPositionAlongPath(pathData.segments, pulse.progress, pathData.segmentLengths, pathData.totalLength);
            if (!pos) continue;
            
            // Draw glowing pulse
            ctx.save();
            ctx.globalAlpha = pulse.alpha * 0.9;
            
            // Outer glow
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, pulse.size * 2, 0, Math.PI * 2);
            ctx.fillStyle = learningPathColor;
            ctx.globalAlpha = pulse.alpha * 0.3;
            ctx.fill();
            
            // Inner bright core
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, pulse.size, 0, Math.PI * 2);
            ctx.fillStyle = '#ffffff';
            ctx.globalAlpha = pulse.alpha * 0.9;
            ctx.fill();
            
            ctx.restore();
        }
    },
    
    /**
     * Get x,y position along a path given progress (0-1)
     */
    _getPositionAlongPath: function(segments, progress, cachedSegLengths, cachedTotalLength) {
        if (!segments || segments.length === 0) return null;

        // Use pre-cached lengths if available, else compute
        var totalLength, segmentLengths;
        if (cachedSegLengths && cachedTotalLength > 0) {
            segmentLengths = cachedSegLengths;
            totalLength = cachedTotalLength;
        } else {
            totalLength = 0;
            segmentLengths = [];
            for (var i = 0; i < segments.length; i++) {
                var seg = segments[i];
                var len = seg.length !== undefined ? seg.length : Math.sqrt(
                    (seg.to.x - seg.from.x) * (seg.to.x - seg.from.x) +
                    (seg.to.y - seg.from.y) * (seg.to.y - seg.from.y)
                );
                segmentLengths.push(len);
                totalLength += len;
            }
        }

        // Find position
        var targetDist = progress * totalLength;
        var distSoFar = 0;

        for (var i = 0; i < segments.length; i++) {
            var segLen = segmentLengths[i];

            if (distSoFar + segLen >= targetDist) {
                var segProgress = (targetDist - distSoFar) / segLen;
                var seg = segments[i];
                return {
                    x: seg.from.x + (seg.to.x - seg.from.x) * segProgress,
                    y: seg.from.y + (seg.to.y - seg.from.y) * segProgress
                };
            }

            distSoFar += segLen;
        }

        // Return end position
        var lastSeg = segments[segments.length - 1];
        return { x: lastSeg.to.x, y: lastSeg.to.y };
    }
};

// Dozens of places, in this file and in other modules, say "something about the
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
