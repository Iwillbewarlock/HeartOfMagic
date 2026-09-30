/**
 * SpellLearning Orphan Repair Module
 *
 * Finds spells the loaded tree cannot reach and prerequisite links to spells it
 * does not have, and repairs them (the orphan repair button). Split from
 * treeParser.js, 2026-09-30.
 * Depends on: state.js (state.treeData)
 */

// =============================================================================
// ORPHAN ANALYSIS & REPAIR
// Works on state.treeData directly (fast path compatible — no TreeParser needed)
// =============================================================================

/**
 * Last orphan analysis result. Updated by analyzeOrphans().
 * UI reads this to show/hide the orphan repair button.
 */
var lastOrphanStats = null;

/**
 * Analyze the currently loaded tree for orphans and missing prerequisite refs.
 * Read-only — does NOT modify the tree.
 *
 * Returns { totalOrphans, totalMissingPrereqs, totalNodes, schools: { ... } }
 */
function analyzeOrphans() {
    if (!state.treeData || !state.treeData.nodes) {
        lastOrphanStats = { totalOrphans: 0, totalMissingPrereqs: 0, totalNodes: 0, schools: {} };
        return lastOrphanStats;
    }

    var nodes = state.treeData.nodes;
    var schools = state.treeData.schools;

    // Build lookup
    var nodeById = {};
    nodes.forEach(function(n) { nodeById[n.id] = n; });

    var stats = { totalOrphans: 0, totalMissingPrereqs: 0, totalNodes: nodes.length, schools: {} };

    for (var schoolName in schools) {
        var sData = schools[schoolName];
        var rootId = sData.root;
        if (!nodeById[rootId]) continue;

        var schoolNodes = nodes.filter(function(n) { return n.school === schoolName; });
        var schoolIds = schoolNodes.map(function(n) { return n.id; });
        var schoolIdSet = {};
        schoolIds.forEach(function(id) { schoolIdSet[id] = true; });

        var schoolStats = { orphans: 0, missingPrereqs: 0, missingPrereqIds: [], subtrees: [] };
        var missingIdSet = {};

        // Count missing prereq references
        schoolNodes.forEach(function(node) {
            (node.prerequisites || []).forEach(function(prereqId) {
                if (!nodeById[prereqId]) {
                    schoolStats.missingPrereqs++;
                    stats.totalMissingPrereqs++;
                    if (!missingIdSet[prereqId]) {
                        missingIdSet[prereqId] = true;
                        schoolStats.missingPrereqIds.push(prereqId);
                    }
                }
            });
        });

        // BFS from root(s) to find reachable nodes
        var reachable = {};
        var queue = [rootId];
        reachable[rootId] = true;

        while (queue.length > 0) {
            var cId = queue.shift();
            var cNode = nodeById[cId];
            if (!cNode) continue;
            (cNode.children || []).forEach(function(childId) {
                if (!reachable[childId] && nodeById[childId]) {
                    reachable[childId] = true;
                    queue.push(childId);
                }
            });
        }

        // Additional element roots
        schoolNodes.forEach(function(node) {
            if (!reachable[node.id] && node.isRoot) {
                reachable[node.id] = true;
                var q = [node.id];
                while (q.length > 0) {
                    var id = q.shift();
                    var n = nodeById[id];
                    if (!n) continue;
                    (n.children || []).forEach(function(childId) {
                        if (!reachable[childId] && nodeById[childId]) {
                            reachable[childId] = true;
                            q.push(childId);
                        }
                    });
                }
            }
        });

        // Count orphans
        var orphanIds = schoolIds.filter(function(id) { return !reachable[id]; });
        schoolStats.orphans = orphanIds.length;
        stats.totalOrphans += orphanIds.length;

        // Group orphans into connected subtrees
        var visited = {};
        orphanIds.forEach(function(orphanId) {
            if (visited[orphanId]) return;
            var component = [];
            var compQ = [orphanId];
            visited[orphanId] = true;
            while (compQ.length > 0) {
                var id = compQ.shift();
                component.push(id);
                var node = nodeById[id];
                if (!node) continue;
                (node.children || []).forEach(function(childId) {
                    if (!visited[childId] && !reachable[childId] && schoolIdSet[childId]) {
                        visited[childId] = true;
                        compQ.push(childId);
                    }
                });
                // Reverse: find orphans whose children include this id
                orphanIds.forEach(function(otherId) {
                    if (visited[otherId]) return;
                    var otherNode = nodeById[otherId];
                    if (otherNode && (otherNode.children || []).indexOf(id) !== -1) {
                        visited[otherId] = true;
                        compQ.push(otherId);
                    }
                });
            }
            schoolStats.subtrees.push({ size: component.length });
        });

        stats.schools[schoolName] = schoolStats;
    }

    lastOrphanStats = stats;
    return stats;
}

/**
 * Repair orphans in the currently loaded tree (state.treeData).
 * 1. Strip prerequisite references to non-existent nodes
 * 2. Strip non-existent children references
 * 3. Identify orphan subtrees per school
 * 4. Reconnect each subtree root to an appropriate same-school parent
 * 5. Recalculate depths
 * 6. Sync changes to rawData and save
 *
 * Returns { removedPrereqs, reconnectedSubtrees, nodesRecovered }
 */
function repairOrphans() {
    if (!state.treeData || !state.treeData.nodes) {
        console.log('[OrphanRepair] No tree data');
        return { removedPrereqs: 0, reconnectedSubtrees: 0, nodesRecovered: 0 };
    }

    var nodes = state.treeData.nodes;
    var edges = state.treeData.edges || [];
    var schools = state.treeData.schools;
    var repairStats = { removedPrereqs: 0, reconnectedSubtrees: 0, nodesRecovered: 0 };

    // Build lookup
    var nodeById = {};
    nodes.forEach(function(n) { nodeById[n.id] = n; });

    // Step 1: Strip non-existent prereqs and children from ALL nodes
    nodes.forEach(function(node) {
        var origPrereqLen = (node.prerequisites || []).length;
        node.prerequisites = (node.prerequisites || []).filter(function(pid) {
            return !!nodeById[pid];
        });
        var removed = origPrereqLen - node.prerequisites.length;
        if (removed > 0) {
            repairStats.removedPrereqs += removed;
            console.log('[OrphanRepair] Stripped ' + removed + ' missing prereqs from ' + (node.name || node.id));
        }

        node.children = (node.children || []).filter(function(cid) {
            return !!nodeById[cid];
        });

        // Also strip non-existent locks
        if (node.locks) {
            node.locks = node.locks.filter(function(lid) {
                return !!nodeById[lid];
            });
        }
        if (node.hardPrereqs) {
            node.hardPrereqs = node.hardPrereqs.filter(function(pid) {
                return !!nodeById[pid];
            });
        }
        if (node.softPrereqs) {
            node.softPrereqs = node.softPrereqs.filter(function(pid) {
                return !!nodeById[pid];
            });
        }
    });

    // Clean edges too
    state.treeData.edges = edges.filter(function(e) {
        return nodeById[e.from] && nodeById[e.to];
    });
    edges = state.treeData.edges;

    // Step 2: Per-school orphan subtree repair
    for (var schoolName in schools) {
        var sData = schools[schoolName];
        var rootId = sData.root;
        var rootNode = nodeById[rootId];
        if (!rootNode) continue;

        var schoolNodes = nodes.filter(function(n) { return n.school === schoolName; });
        var schoolIds = schoolNodes.map(function(n) { return n.id; });
        var schoolIdSet = {};
        schoolIds.forEach(function(id) { schoolIdSet[id] = true; });

        // BFS from root(s)
        var reachable = {};
        var depthMap = {};
        var queue = [{ id: rootId, depth: 0 }];
        reachable[rootId] = true;
        depthMap[rootId] = 0;

        while (queue.length > 0) {
            var item = queue.shift();
            var cNode = nodeById[item.id];
            if (!cNode) continue;
            (cNode.children || []).forEach(function(childId) {
                if (!reachable[childId] && nodeById[childId]) {
                    reachable[childId] = true;
                    depthMap[childId] = item.depth + 1;
                    queue.push({ id: childId, depth: item.depth + 1 });
                }
            });
        }

        // Additional element roots
        schoolNodes.forEach(function(node) {
            if (!reachable[node.id] && node.isRoot) {
                reachable[node.id] = true;
                depthMap[node.id] = 0;
                var q = [{ id: node.id, depth: 0 }];
                while (q.length > 0) {
                    var it = q.shift();
                    var n = nodeById[it.id];
                    if (!n) continue;
                    (n.children || []).forEach(function(childId) {
                        if (!reachable[childId] && nodeById[childId]) {
                            reachable[childId] = true;
                            depthMap[childId] = it.depth + 1;
                            q.push({ id: childId, depth: it.depth + 1 });
                        }
                    });
                }
            }
        });

        // Find orphans
        var orphanIds = schoolIds.filter(function(id) { return !reachable[id]; });
        if (orphanIds.length === 0) continue;

        // Group into connected subtrees
        var visited = {};
        var subtrees = [];

        orphanIds.forEach(function(orphanId) {
            if (visited[orphanId]) return;
            var component = [];
            var compQ = [orphanId];
            visited[orphanId] = true;
            while (compQ.length > 0) {
                var id = compQ.shift();
                component.push(id);
                var node = nodeById[id];
                if (!node) continue;
                (node.children || []).forEach(function(childId) {
                    if (!visited[childId] && !reachable[childId] && schoolIdSet[childId]) {
                        visited[childId] = true;
                        compQ.push(childId);
                    }
                });
                orphanIds.forEach(function(otherId) {
                    if (visited[otherId]) return;
                    var otherNode = nodeById[otherId];
                    if (otherNode && (otherNode.children || []).indexOf(id) !== -1) {
                        visited[otherId] = true;
                        compQ.push(otherId);
                    }
                });
            }

            // Find subtree root: node with no parent in this component, lowest tier
            var stRoot = null;
            var lowestTier = Infinity;
            component.forEach(function(cId) {
                var cNode = nodeById[cId];
                if (!cNode) return;
                var hasParent = component.some(function(otherId) {
                    if (otherId === cId) return false;
                    var oNode = nodeById[otherId];
                    return oNode && (oNode.children || []).indexOf(cId) !== -1;
                });
                if (!hasParent && (cNode.tier || 0) < lowestTier) {
                    lowestTier = cNode.tier || 0;
                    stRoot = cId;
                }
            });
            if (!stRoot) stRoot = component[0];
            subtrees.push({ root: stRoot, members: component });
        });

        // Reconnect each subtree
        var maxCh = 5;
        subtrees.forEach(function(subtree) {
            var stRootNode = nodeById[subtree.root];
            if (!stRootNode) return;
            var stTier = stRootNode.tier || 0;

            // Find best parent in reachable set
            var candidates = [];
            for (var connId in reachable) {
                var conn = nodeById[connId];
                if (!conn || conn.school !== schoolName) continue;
                var connTier = conn.tier || 0;
                if (connTier > stTier) continue;
                if (connTier < stTier - 1) continue;
                candidates.push({ node: conn, childCount: (conn.children || []).length, tierDiff: stTier - connTier });
            }

            var withRoom = candidates.filter(function(c) { return c.childCount < maxCh; });
            if (withRoom.length === 0) withRoom = candidates;
            if (withRoom.length === 0) withRoom = [{ node: rootNode, childCount: (rootNode.children || []).length, tierDiff: stTier }];

            withRoom.sort(function(a, b) {
                if (a.tierDiff !== b.tierDiff) return a.tierDiff - b.tierDiff;
                return a.childCount - b.childCount;
            });

            var bestParent = withRoom[0].node;
            console.log('[OrphanRepair] Reconnecting subtree (' + subtree.members.length + ' nodes) ' +
                (stRootNode.name || subtree.root) + ' (tier ' + stTier + ') -> ' +
                (bestParent.name || bestParent.id) + ' (tier ' + (bestParent.tier || 0) + ')');

            // Link
            if (!bestParent.children) bestParent.children = [];
            if (bestParent.children.indexOf(subtree.root) === -1) {
                bestParent.children.push(subtree.root);
            }
            if (!stRootNode.prerequisites) stRootNode.prerequisites = [];
            if (stRootNode.prerequisites.indexOf(bestParent.id) === -1) {
                stRootNode.prerequisites.push(bestParent.id);
            }
            edges.push({ from: bestParent.id, to: subtree.root });

            // BFS to recover subtree depths
            var parentDepth = depthMap[bestParent.id] || bestParent.depth || 0;
            reachable[subtree.root] = true;
            depthMap[subtree.root] = parentDepth + 1;
            stRootNode.depth = parentDepth + 1;

            var recQ = [{ id: subtree.root, depth: parentDepth + 1 }];
            var recovered = 0;
            while (recQ.length > 0) {
                var rItem = recQ.shift();
                var rNode = nodeById[rItem.id];
                if (!rNode) continue;
                recovered++;
                (rNode.children || []).forEach(function(childId) {
                    if (!reachable[childId] && nodeById[childId]) {
                        reachable[childId] = true;
                        var cd = rItem.depth + 1;
                        depthMap[childId] = cd;
                        nodeById[childId].depth = cd;
                        recQ.push({ id: childId, depth: cd });
                    }
                });
            }
            repairStats.reconnectedSubtrees++;
            repairStats.nodesRecovered += recovered;
        });

        // Update school maxDepth
        var maxDepth = 0;
        schoolNodes.forEach(function(n) { maxDepth = Math.max(maxDepth, n.depth || 0); });
        sData.maxDepth = maxDepth;
    }

    console.log('[OrphanRepair] Complete: removed ' + repairStats.removedPrereqs +
        ' missing prereqs, reconnected ' + repairStats.reconnectedSubtrees +
        ' subtrees, recovered ' + repairStats.nodesRecovered + ' nodes');

    // Sync to rawData for persistence
    if (state.treeData.rawData && state.treeData.rawData.schools) {
        nodes.forEach(function(node) {
            var rawSchool = state.treeData.rawData.schools[node.school];
            if (!rawSchool || !rawSchool.nodes) return;
            for (var i = 0; i < rawSchool.nodes.length; i++) {
                var rawNode = rawSchool.nodes[i];
                if ((rawNode.formId || rawNode.spellId) === node.id) {
                    rawNode.prerequisites = (node.prerequisites || []).slice();
                    rawNode.children = (node.children || []).slice();
                    rawNode.depth = node.depth;
                    if (node.hardPrereqs) rawNode.hardPrereqs = node.hardPrereqs.slice();
                    if (node.softPrereqs) rawNode.softPrereqs = node.softPrereqs.slice();
                    if (node.locks) rawNode.locks = node.locks.slice();
                    break;
                }
            }
        });
    }

    // Save repaired tree
    if (window.callCpp && state.treeData.rawData) {
        var treeJson = JSON.stringify(state.treeData.rawData);
        window.callCpp('SaveSpellTree', treeJson);
        console.log('[OrphanRepair] Saved repaired tree');
    }

    // Re-analyze
    analyzeOrphans();

    // Re-render
    if (typeof WheelRenderer !== 'undefined' && WheelRenderer.nodes && WheelRenderer.nodes.length > 0) {
        WheelRenderer.render();
    }

    return repairStats;
}

/**
 * Update the orphan repair button visibility and text.
 * Called after tree loads and after repair.
 */
function updateOrphanRepairButton() {
    var btn = document.getElementById('orphanRepairBtn');
    if (!btn) return;

    var stats = lastOrphanStats || analyzeOrphans();
    if (stats.totalOrphans > 0 || stats.totalMissingPrereqs > 0) {
        var total = stats.totalOrphans + stats.totalMissingPrereqs;
        btn.textContent = total + ' orphan' + (total !== 1 ? 's' : '') + ' - repair';
        btn.style.display = '';
        btn.title = stats.totalOrphans + ' unreachable nodes, ' + stats.totalMissingPrereqs + ' missing prereq refs';
    } else {
        btn.style.display = 'none';
    }
}
