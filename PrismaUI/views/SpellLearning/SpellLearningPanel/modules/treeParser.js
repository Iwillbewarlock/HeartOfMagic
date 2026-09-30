/**
 * SpellLearning Tree Parser Module
 * 
 * Parses spell tree JSON data, detects cycles, fixes orphaned nodes.
 * Depends on: config.js (TREE_CONFIG), spellCache.js (SpellCache), state.js (settings)
 */

// =============================================================================
// LOGGING HELPER
// =============================================================================

function logTreeParser(message, isWarning) {
    var prefix = '[TreeParser] ';
    var fullMsg = prefix + message;
    
    if (isWarning) {
        console.warn(fullMsg);
    } else {
        console.log(fullMsg);
    }
    
    // Send to C++ for SKSE log
    if (window.callCpp) {
        window.callCpp('LogMessage', JSON.stringify({
            level: isWarning ? 'warn' : 'info',
            message: fullMsg
        }));
    }
}

// =============================================================================
// TREE PARSER
// =============================================================================

var TreeParser = {
    nodes: new Map(),
    edges: [],
    schools: {},

    /**
     * A spell is never its own child or prerequisite: the list without `id`, and
     * without anything that is not a formId string (a tree saved by the 6316b47
     * build has null links, which would make {to: null} edges). The same array
     * when there is nothing to drop; [] for a missing list.
     * Also used by the trusted fast path in treeViewerUI.js.
     */
    withoutId: function(list, id) {
        if (!list || !list.length) return list || [];
        var keep = function(x) { return typeof x === 'string' && x !== id; };
        for (var i = 0; i < list.length; i++) {
            if (!keep(list[i])) return list.filter(keep);
        }
        return list;
    },

    /** softNeeded no larger than the soft list, so dropping a self id cannot lock a spell */
    clampSoftNeeded: function(softNeeded, softPrereqs) {
        var n = softNeeded || 0;
        var len = softPrereqs ? softPrereqs.length : 0;
        return n > len ? len : n;
    },

    parse: function(data) {
        this.nodes.clear();
        this.edges = [];
        this.schools = {};

        if (typeof data === 'string') {
            try { data = JSON.parse(data); }
            catch (e) { return { success: false, error: e.message }; }
        }

        if (!data.schools) return { success: false, error: 'Missing schools' };

        var allFormIds = [];
        var self = this;
        this._maxChildrenPerNode = (data.settings && data.settings.maxChildrenPerNode) || 5;
        var trustPrereqs = data.trustPrereqs || false; // Skip prereq mutations if source is authoritative

        for (var schoolName in data.schools) {
            var schoolData = data.schools[schoolName];
            if (!schoolData.root || !schoolData.nodes) continue;
            
            // Extract layoutStyle from LLM response
            var layoutStyle = schoolData.layoutStyle || 'radial';
            if (!TREE_CONFIG.layoutStyles[layoutStyle]) {
                logTreeParser('Unknown layout style "' + layoutStyle + '" for ' + schoolName + ', using radial', true);
                layoutStyle = 'radial';
            }
            logTreeParser(schoolName + ' using layout style: ' + layoutStyle);
            
            this.schools[schoolName] = {
                root: schoolData.root,
                nodeIds: [],
                maxDepth: 0,
                maxWidth: 0,
                layoutStyle: layoutStyle,
                // Pass through sliceInfo for wheelRenderer to use exact sector angles
                sliceInfo: schoolData.sliceInfo,
                config: schoolData.config_used,
                // Pass through pre-baked sector angles from TreeGrowth
                spokeAngle: schoolData.spokeAngle,
                startAngle: schoolData.startAngle,
                endAngle: schoolData.endAngle,
                angleSpan: schoolData.arcSize !== undefined ? (schoolData.arcSize * 180 / Math.PI) : undefined,
                color: schoolData.color
            };

            // DIAGNOSTIC: Check first node's children in input
            if (schoolData.nodes.length > 0) {
                var firstInput = schoolData.nodes[0];
                logTreeParser('DIAGNOSTIC ' + schoolName + ' input: first node children=' +
                    (firstInput.children ? firstInput.children.length : 'undefined') +
                    ', prereqs=' + (firstInput.prerequisites ? firstInput.prerequisites.length : 'undefined'));
            }

            schoolData.nodes.forEach(function(nd) {
                var id = nd.formId || nd.spellId;
                if (!id) return;

                allFormIds.push(id);
                
                // Preserve pre-computed positions from visual-first builder or Tree Growth
                var hasPrecomputed = nd._fromVisualFirst || nd._fromLayoutEngine ||
                    (nd.x !== undefined && nd.y !== undefined && !(nd.x === 0 && nd.y === 0));
                // trustPrereqs data has authoritative positions — flag for renderer
                var fromEngine = nd._fromLayoutEngine || (trustPrereqs && hasPrecomputed);

                self.nodes.set(id, {
                    id: id,
                    formId: id,
                    name: nd.name || null,
                    school: schoolName,
                    level: null,
                    cost: null,
                    type: null,
                    effects: [],
                    desc: null,
                    // A spell is never its own child or prerequisite (all four lists)
                    children: self.withoutId(nd.children, id),
                    prerequisites: self.withoutId(nd.prerequisites, id),
                    tier: nd.tier || 0,
                    state: 'locked',
                    depth: nd.tier || nd.depth || 0,  // Use tier as initial depth, BFS will recalculate if tree structure is valid
                    // Preserve positions if pre-computed, otherwise default to 0
                    x: hasPrecomputed ? nd.x : 0,
                    y: hasPrecomputed ? nd.y : 0,
                    angle: nd.angle || 0,
                    radius: nd.radius || 0,
                    // Preserve visual-first flags
                    _fromVisualFirst: nd._fromVisualFirst || false,
                    _fromLayoutEngine: fromEngine,
                    isRoot: nd.isRoot || false,  // CRITICAL: Preserve root flag for origin lines
                    // Preserve hard/soft prerequisite data
                    hardPrereqs: self.withoutId(nd.hardPrereqs, id),
                    softPrereqs: self.withoutId(nd.softPrereqs, id),
                    softNeeded: self.clampSoftNeeded(nd.softNeeded, self.withoutId(nd.softPrereqs, id)),
                    // Lock prerequisites (Pre Req Master)
                    locks: nd.locks || [],
                    // Theme data baked from tree generator
                    theme: nd.theme || null,
                    themeColor: nd.themeColor || null,
                    skillLevel: nd.skillLevel || null,
                    // "Plugin.esp|0x000800": names the plugin when two spells share a name
                    persistentId: nd.persistentId || null
                });
                self.schools[schoolName].nodeIds.push(id);
            });
        }

        if (trustPrereqs) {
            // Authoritative data: build edges only, no prereq mutations
            this.nodes.forEach(function(node) {
                if (node.isRoot) {
                    node.prerequisites = [];
                    node.hardPrereqs = [];
                }
                node.children.forEach(function(childId) {
                    var child = self.nodes.get(childId);
                    if (child) {
                        self.edges.push({ from: node.id, to: childId });
                    }
                });
            });
        } else {
            // Multi-root cleanup: root nodes are independent starting points
            // Remove other root formIds from root children, and clear root prerequisites
            this.nodes.forEach(function(node) {
                if (node.isRoot) {
                    node.prerequisites = [];
                    node.children = node.children.filter(function(cid) {
                        var child = self.nodes.get(cid);
                        return !child || !child.isRoot;
                    });
                }
            });

            // Build edges from children (self ids were dropped when the nodes were made)
            this.nodes.forEach(function(node) {
                node.children.forEach(function(childId) {
                    var child = self.nodes.get(childId);
                    if (child) {
                        self.edges.push({ from: node.id, to: childId });
                        // Never add prerequisites to root nodes - they are independent starting points
                        if (!child.isRoot && child.prerequisites.indexOf(node.id) === -1) {
                            child.prerequisites.push(node.id);
                        }
                    }
                });
            });

            // Also build edges from prerequisites (handles LLM inconsistencies)
            this.nodes.forEach(function(node) {
                node.prerequisites.forEach(function(prereqId) {
                    var parent = self.nodes.get(prereqId);
                    if (parent) {
                        var edgeExists = self.edges.some(function(e) {
                            return e.from === prereqId && e.to === node.id;
                        });
                        if (!edgeExists) {
                            logTreeParser('Adding missing edge: ' + prereqId + ' -> ' + node.id);
                            self.edges.push({ from: prereqId, to: node.id });
                            if (parent.children.indexOf(node.id) === -1) {
                                parent.children.push(node.id);
                            }
                        }
                    }
                });
            });
        }

        // Detect unobtainable spells per school (no auto-fix - user must regenerate)
        for (var schoolName in this.schools) {
            var schoolData = this.schools[schoolName];
            var unobtainableCount = this.detectAndFixCycles(schoolName, schoolData.root);
            if (unobtainableCount > 0) {
                logTreeParser(schoolName + ' has ' + unobtainableCount + ' unobtainable spells - regenerate tree', true);
            }
        }

        // Calculate depths via BFS from root
        for (var sName in this.schools) {
            var sData = this.schools[sName];
            var root = this.nodes.get(sData.root);
            if (!root) {
                logTreeParser('WARNING: Root not found for ' + sName + ' (root=' + sData.root + '), using tier-based depths', true);
                continue;
            }
            logTreeParser('BFS traversal for ' + sName + ' starting from root: ' + root.id +
                         ', root has ' + (root.children ? root.children.length : 0) + ' children');

            var queue = [{ node: root, depth: 0 }];
            var visited = new Set();
            var depthCounts = {};
            
            while (queue.length) {
                var item = queue.shift();
                var node = item.node;
                var depth = item.depth;
                if (visited.has(node.id)) continue;
                visited.add(node.id);
                node.depth = depth;
                sData.maxDepth = Math.max(sData.maxDepth, depth);
                depthCounts[depth] = (depthCounts[depth] || 0) + 1;
                
                node.children.forEach(function(cid) {
                    var c = self.nodes.get(cid);
                    if (c) queue.push({ node: c, depth: depth + 1 });
                });
            }

            sData.maxWidth = Math.max.apply(null, Object.values(depthCounts).concat([1]));

            logTreeParser(sName + ' BFS complete: ' + visited.size + '/' + sData.nodeIds.length + ' nodes reached, maxDepth=' + sData.maxDepth);

            // Find additional roots (element roots in multi-root mode) and BFS from them
            var additionalRoots = [];
            sData.nodeIds.forEach(function(nodeId) {
                if (!visited.has(nodeId)) {
                    var node = self.nodes.get(nodeId);
                    if (node && node.isRoot) {
                        additionalRoots.push(node);
                    }
                }
            });

            // BFS from each additional root
            if (additionalRoots.length > 0) {
                logTreeParser('Processing ' + additionalRoots.length + ' additional element roots');
                additionalRoots.forEach(function(addRoot) {
                    logTreeParser('BFS from element root: ' + addRoot.id + ' (' + (addRoot.name || 'unnamed') + ')');
                    visited.add(addRoot.id);
                    addRoot.depth = 0;
                    addRoot.state = 'available';

                    var addQueue = [{ node: addRoot, depth: 0 }];
                    while (addQueue.length) {
                        var item = addQueue.shift();
                        var node = item.node;
                        var depth = item.depth;
                        if (node !== addRoot && visited.has(node.id)) continue;
                        if (node !== addRoot) visited.add(node.id);
                        node.depth = depth;
                        sData.maxDepth = Math.max(sData.maxDepth, depth);
                        depthCounts[depth] = (depthCounts[depth] || 0) + 1;

                        node.children.forEach(function(cid) {
                            var c = self.nodes.get(cid);
                            if (c && !visited.has(cid)) {
                                addQueue.push({ node: c, depth: depth + 1 });
                            }
                        });
                    }
                });
                logTreeParser(sName + ' after element roots: ' + visited.size + '/' + sData.nodeIds.length + ' nodes reached');
            }

            // Find and fix truly orphaned nodes (not element roots)
            // Skip for trustPrereqs data — source is authoritative
            if (!trustPrereqs) {
                var orphanedNodes = [];
                sData.nodeIds.forEach(function(nodeId) {
                    if (!visited.has(nodeId)) {
                        orphanedNodes.push(nodeId);
                    }
                });

                if (orphanedNodes.length > 0) {
                    logTreeParser('Found ' + orphanedNodes.length + ' orphaned nodes in ' + sName + ' - attempting to fix', true);
                    this._fixOrphanedNodes(orphanedNodes, sName, sData, root, visited);
                }
            }

            // Root nodes are AVAILABLE (learnable starting points), not auto-unlocked
            // Children stay locked until root is actually learned
            root.state = 'available';
        }

        return {
            success: true,
            nodes: Array.from(this.nodes.values()),
            edges: this.edges,
            schools: this.schools,
            allFormIds: allFormIds
        };
    },

    _fixOrphanedNodes: function(orphanedNodes, schoolName, schoolData, root, visited) {
        var self = this;

        orphanedNodes.forEach(function(orphanId) {
            var orphan = self.nodes.get(orphanId);
            if (!orphan) return;

            // Safety check: Never connect nodes marked as roots - they are intentional roots
            if (orphan.isRoot) {
                logTreeParser('SKIP orphan fix for root node: ' + orphanId + ' (' + (orphan.name || 'unnamed') + ')');
                visited.add(orphanId);
                orphan.depth = 0;
                orphan.state = 'available';
                return;
            }

            var orphanTier = orphan.tier || 0;
            var potentialParents = [];
            
            visited.forEach(function(connectedId) {
                var connected = self.nodes.get(connectedId);
                if (connected && connected.school === schoolName) {
                    var connectedTier = connected.tier || 0;
                    if (connectedTier <= orphanTier && connectedTier >= orphanTier - 1) {
                        var childCount = connected.children.length;
                        potentialParents.push({ node: connected, childCount: childCount, tierDiff: orphanTier - connectedTier });
                    }
                }
            });
            
            // Respect maxChildren limit
            var maxCh = self._maxChildrenPerNode || 5;
            var withRoom = potentialParents.filter(function(p) { return p.childCount < maxCh; });
            if (withRoom.length === 0) withRoom = potentialParents; // fallback if all full

            withRoom.sort(function(a, b) {
                if (a.tierDiff !== b.tierDiff) return a.tierDiff - b.tierDiff;
                return a.childCount - b.childCount;
            });

            var bestParent = withRoom.length > 0 ? withRoom[0].node : root;
            
            logTreeParser('Connecting orphan ' + orphanId + ' (tier ' + orphanTier + ') to ' + bestParent.id);
            
            if (bestParent.children.indexOf(orphanId) === -1) {
                bestParent.children.push(orphanId);
            }
            if (orphan.prerequisites.indexOf(bestParent.id) === -1) {
                orphan.prerequisites.push(bestParent.id);
            }
            self.edges.push({ from: bestParent.id, to: orphanId });
            
            orphan.depth = bestParent.depth + 1;
            schoolData.maxDepth = Math.max(schoolData.maxDepth, orphan.depth);
            visited.add(orphanId);
        });
        
        // Re-process children of newly connected nodes
        orphanedNodes.forEach(function(orphanId) {
            var orphan = self.nodes.get(orphanId);
            if (!orphan) return;
            
            var childQueue = [{ node: orphan, depth: orphan.depth }];
            while (childQueue.length > 0) {
                var item = childQueue.shift();
                item.node.children.forEach(function(cid) {
                    var child = self.nodes.get(cid);
                    if (child && !visited.has(cid)) {
                        visited.add(cid);
                        child.depth = item.depth + 1;
                        schoolData.maxDepth = Math.max(schoolData.maxDepth, child.depth);
                        childQueue.push({ node: child, depth: child.depth });
                    }
                });
            }
        });
        
        logTreeParser('Fixed ' + orphanedNodes.length + ' orphaned nodes in ' + schoolName);
    },

    detectAndFixCycles: function(schoolName, rootId) {
        var self = this;
        var fixesMade = 0;
        
        var rootNode = this.nodes.get(rootId);
        if (!rootNode) return 0;
        
        var schoolNodeIds = this.schools[schoolName].nodeIds;
        var totalNodes = schoolNodeIds.length;
        
        function simulateUnlocks() {
            var unlocked = new Set();
            unlocked.add(rootId);
            
            var changed = true;
            var iterations = 0;
            var maxIterations = totalNodes + 10;
            
            while (changed && iterations < maxIterations) {
                changed = false;
                iterations++;
                
                schoolNodeIds.forEach(function(nodeId) {
                    if (unlocked.has(nodeId)) return;
                    
                    var node = self.nodes.get(nodeId);
                    if (!node) return;
                    
                    var prereqs = node.prerequisites;
                    if (prereqs.length === 0) {
                        // Nodes with no prerequisites are inherently unlockable
                        // This includes additional root nodes in multi-root trees
                        unlocked.add(nodeId);
                        changed = true;
                        return;
                    }

                    var allPrereqsUnlocked = prereqs.every(function(prereqId) {
                        return unlocked.has(prereqId);
                    });

                    if (allPrereqsUnlocked) {
                        unlocked.add(nodeId);
                        changed = true;
                    }
                });
            }

            return unlocked;
        }

        var unlockable = simulateUnlocks();

        var unobtainable = [];
        schoolNodeIds.forEach(function(nodeId) {
            if (!unlockable.has(nodeId)) {
                unobtainable.push(nodeId);
            }
        });
        
        if (unobtainable.length === 0) {
            logTreeParser(schoolName + ': All ' + totalNodes + ' spells are obtainable');
            return 0;
        }
        
        // DISABLED: Gentle fix was causing more problems than it solved
        // Just log warning about unobtainable spells - user should regenerate tree
        logTreeParser(schoolName + ': ERROR - Found ' + unobtainable.length + ' unobtainable spells!', true);
        logTreeParser(schoolName + ': Tree has broken prerequisite chains. Please regenerate the tree.', true);

        // Log first few blocking prereqs for debugging
        var logged = 0;
        unobtainable.forEach(function(nodeId) {
            if (logged >= 5) return; // Only log first 5

            var node = self.nodes.get(nodeId);
            if (!node) return;

            var blockingPrereqs = node.prerequisites.filter(function(prereqId) {
                return !unlockable.has(prereqId);
            });

            if (blockingPrereqs.length > 0) {
                logTreeParser('  ' + (node.name || nodeId) + ' blocked by: ' + blockingPrereqs.slice(0, 3).join(', '), true);
                logged++;
            }
        });

        if (unobtainable.length > 5) {
            logTreeParser('  ... and ' + (unobtainable.length - 5) + ' more unobtainable spells', true);
        }

        // Return the count of problems (no fixes attempted)
        return unobtainable.length;
    },
    
    updateNodeFromCache: function(node) {
        var spellData = SpellCache.get(node.formId);
        if (spellData) {
            node.name = spellData.name || spellData.editorId || node.formId;
            var level = spellData.level || spellData.skillLevel || 'Unknown';
            // The level filter counts levels: a new one is counted again when next asked
            if (level !== node.level && typeof BridgeView !== 'undefined') BridgeView.invalidateCounts();
            node.level = level;
            // The game works the cost out as a float (307.70135...); the card shows whole
            // points, and a cost under half a point shows 1, not "?" (the card's "no cost")
            var cost = Number(spellData.cost || spellData.magickaCost) || 0;
            node.cost = cost > 0 ? Math.max(1, Math.round(cost)) : 0;
            node.type = spellData.type || spellData.castingType || 'Spell';
            node.effects = spellData.effects || spellData.effectNames || [];
            node.desc = spellData.description || '';
            node.chips = spellData.chips || [];
            node.iconKey = spellData.iconKey || '';
            node.schoolIconKey = spellData.schoolIconKey || '';
            if (spellData.school) node.school = spellData.school;
            // The name may have changed: the card's lists count shared names again
            if (typeof SpellNames !== 'undefined') SpellNames.invalidate();
        }
    }
};
