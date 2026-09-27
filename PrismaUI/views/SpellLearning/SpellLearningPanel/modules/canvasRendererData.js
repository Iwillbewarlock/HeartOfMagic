/**
 * CanvasRenderer - the tree's data: setData and what it builds from the spells
 * (lookup maps, school angles, the fallback spiral layout, the discovery set,
 * the spatial index), and the level of detail with its node buckets.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.setData() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, TreeStyle (ink), EditMode and TreeNav
 * (optional)
 */

(function() {
    var methods = {
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
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
