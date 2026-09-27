/**
 * CanvasRenderer - selection: selecting a spell and focusing the camera on it,
 * the dependency path sets that selection and hover light up, and turning the
 * wheel to a spell or a school.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.selectNodeAndFocus() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, TreeCamera (optional)
 */

(function() {
    var methods = {
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
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
