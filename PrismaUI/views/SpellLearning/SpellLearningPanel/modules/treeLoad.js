/**
 * Tree loading: a saved or built tree into state.treeData and the renderers -
 * the trusted fast path (_loadTrustedTree) and loadTreeData. Split out of
 * treeViewerUI.js (600-line limit); loads right after it.
 */

/**
 * Fast path for trustPrereqs data — builds nodes/edges/schools directly
 * from JSON and sends to TrustedRenderer. No TreeParser, no WheelRenderer,
 * no CanvasRenderer, no layout logic, no prereq mutations.
 */
function _loadTrustedTree(data, switchToTreeTab) {
    _logToSKSE('[_loadTrustedTree] === ENTRY ===');
    _logToSKSE('[_loadTrustedTree] trustPrereqs=' + data.trustPrereqs +
               ', schools=' + Object.keys(data.schools || {}).join(','));

    var nodes = [];
    var edges = [];
    var schools = {};
    var allFormIds = [];
    var rootIds = {};
    var nodesWithXY = 0;
    var nodesWithoutXY = 0;

    for (var schoolName in data.schools) {
        if (!data.schools.hasOwnProperty(schoolName)) continue;
        var sd = data.schools[schoolName];
        var schoolNodes = sd.nodes || [];

        _logToSKSE('[_loadTrustedTree] School "' + schoolName + '": ' +
                   schoolNodes.length + ' nodes, root=' + sd.root +
                   ', color=' + sd.color + ', spokeAngle=' + sd.spokeAngle);

        // Filter out NaN angles — let CanvasRendererV2 compute from node positions instead
        var validSpoke = (sd.spokeAngle !== undefined && !isNaN(sd.spokeAngle));
        var validStart = (sd.startAngle !== undefined && !isNaN(sd.startAngle));
        var validEnd = (sd.endAngle !== undefined && !isNaN(sd.endAngle));

        schools[schoolName] = {
            root: sd.root,
            nodeIds: [],
            spokeAngle: validSpoke ? sd.spokeAngle : undefined,
            startAngle: validStart ? sd.startAngle : undefined,
            endAngle: validEnd ? sd.endAngle : undefined,
            angleSpan: (validEnd && validStart) ? sd.endAngle - sd.startAngle : undefined,
            color: sd.color,
            layoutStyle: sd.layoutStyle || 'tree'
        };

        rootIds[sd.root] = true;

        for (var i = 0; i < schoolNodes.length; i++) {
            var nd = schoolNodes[i];
            var id = nd.formId;
            allFormIds.push(id);
            schools[schoolName].nodeIds.push(id);

            // Use explicit undefined check — nd.x || 0 would lose x=0 values
            var nx = (nd.x !== undefined && nd.x !== null) ? nd.x : 0;
            var ny = (nd.y !== undefined && nd.y !== null) ? nd.y : 0;

            // Track position stats
            if (nd.x !== undefined && nd.x !== null) {
                nodesWithXY++;
            } else {
                nodesWithoutXY++;
            }

            // Log first 3 nodes per school
            if (i < 3) {
                _logToSKSE('[_loadTrustedTree]   node[' + i + ']: formId=' + id +
                           ', raw x=' + nd.x + ', raw y=' + nd.y +
                           ', used x=' + nx + ', used y=' + ny +
                           ', name=' + (nd.name || '?') + ', tier=' + nd.tier);
            }

            // Compute hard/soft prereqs using same fallback as loadTreeData.
            // A spell is never its own prerequisite: self ids out of all three lists first.
            var _prereqs = TreeParser.withoutId(nd.prerequisites, id);
            var _hardPrereqs = TreeParser.withoutId(nd.hardPrereqs, id);
            var _softPrereqs = TreeParser.withoutId(nd.softPrereqs, id);
            var _softNeeded = TreeParser.clampSoftNeeded(nd.softNeeded, _softPrereqs);
            if (_hardPrereqs.length === 0 && _softPrereqs.length === 0) {
                if (_prereqs.length === 1) {
                    _hardPrereqs = _prereqs.slice();
                } else if (_prereqs.length > 1) {
                    _hardPrereqs = [_prereqs[0]];
                    _softPrereqs = _prereqs.slice(1);
                    _softNeeded = 1;
                }
            }

            // Root nodes must never have prerequisites — they are school entry points.
            // Old trees (pre-parentMap fix) had NLP prereqs on roots, causing
            // entire schools to be unreachable orphan chains.
            if (nd.isRoot || id === sd.root) {
                _hardPrereqs = [];
                _softPrereqs = [];
                _softNeeded = 0;
            }

            var isRootNode = nd.isRoot || id === sd.root;
            var node = {
                id: id,
                formId: id,
                name: nd.name || null,
                // "Plugin.esp|0x000800": names the plugin when two spells share a name
                persistentId: nd.persistentId || null,
                school: schoolName,
                // A spell is never its own child or prerequisite
                children: TreeParser.withoutId(nd.children, id),
                prerequisites: isRootNode ? [] : _prereqs,
                hardPrereqs: _hardPrereqs,
                softPrereqs: _softPrereqs,
                softNeeded: _softNeeded,
                tier: nd.tier || 0,
                depth: nd.tier || 0,
                x: nx,
                y: ny,
                isRoot: isRootNode,
                _fromLayoutEngine: true,
                state: 'locked',
                // Theme data baked from tree generator
                theme: nd.theme || null,
                themeColor: nd.themeColor || null,
                skillLevel: nd.skillLevel || null,
                traits: nd.traits || null
            };

            // Roots and prereq-less nodes are available
            if (isRootNode || !node.prerequisites || node.prerequisites.length === 0) {
                node.state = 'available';
            }

            nodes.push(node);

            // Build edges from children
            var ch = node.children;
            for (var c = 0; c < ch.length; c++) {
                edges.push({ from: id, to: ch[c] });
            }
        }
    }

    _logToSKSE('[_loadTrustedTree] TOTALS: ' + nodes.length + ' nodes, ' +
               edges.length + ' edges, ' + Object.keys(schools).length + ' schools');
    _logToSKSE('[_loadTrustedTree] POSITIONS: withXY=' + nodesWithXY +
               ', withoutXY=' + nodesWithoutXY);

    // Store for state tracking
    state.treeData = {
        nodes: nodes,
        edges: edges,
        schools: schools,
        allFormIds: allFormIds,
        rawData: data,
        globe: data.globe
            ? { x: data.globe.x || 0, y: data.globe.y || 0, radius: data.globe.radius || 45 }
            : { x: 0, y: 0, radius: 45 }
    };

    // Use CanvasRendererV2 — nodes already have _fromLayoutEngine flag so spiral fallback won't trigger
    if (typeof CanvasRenderer !== 'undefined') {
        _logToSKSE('[_loadTrustedTree] Using CanvasRendererV2 with baked positions, noRotate=' + !!data.noRotate);
        var container = document.getElementById('tree-container');
        if (container) {
            // Hide TrustedRenderer if it was previously shown
            if (typeof TrustedRenderer !== 'undefined' && TrustedRenderer.canvas) {
                TrustedRenderer.hide();
            }
            if (!CanvasRenderer.container || CanvasRenderer.container !== container) {
                CanvasRenderer.init(container);
            }
            CanvasRenderer.clear();
            var isFlat = !!data.noRotate || (data.config && data.config.layoutMode === 'flat') || data.layoutMode === 'flat';
            CanvasRenderer.noRotate = isFlat;
            CanvasRenderer.setData(nodes, edges, schools);
            CanvasRenderer.show();
            CanvasRenderer.centerView();
            CanvasRenderer.forceRender();
        }
    } else {
        _logToSKSE('[_loadTrustedTree] FALLBACK: No CanvasRenderer, using TrustedRenderer');
        var container2 = document.getElementById('tree-container');
        if (container2 && typeof TrustedRenderer !== 'undefined') {
            TrustedRenderer.init(container2);
            TrustedRenderer.setData(nodes, edges, schools);
            TrustedRenderer.show();
        }
    }

    // Update UI
    var emptyState = document.getElementById('empty-state');
    if (emptyState) emptyState.classList.add('hidden');
    if (typeof DetailsPeek !== 'undefined') DetailsPeek.applyLayout();
    document.getElementById('total-count').textContent = nodes.length;
    document.getElementById('unlocked-count').textContent = '0';

    if (switchToTreeTab !== false) {
        switchTab('spellTree');
    }

    // Send prereqs to C++ (already baked, no splitting needed)
    if (typeof BridgeView !== 'undefined') BridgeView.setTree(state.treeData);

    if (window.callCpp) {
        var prereqData = [];
        for (var pi = 0; pi < nodes.length; pi++) {
            prereqData.push({
                formId: nodes[pi].formId,
                hardPrereqs: nodes[pi].hardPrereqs,
                softPrereqs: nodes[pi].softPrereqs,
                softNeeded: nodes[pi].softNeeded
            });
        }
        window.callCpp('SetTreePrerequisites', JSON.stringify({ clear: true }));
        window.callCpp('SetTreePrerequisites', JSON.stringify(prereqData));
    }

    // Request spell names for display
    SpellCache.requestBatch(allFormIds, function() {
        for (var si = 0; si < nodes.length; si++) {
            TreeParser.updateNodeFromCache(nodes[si]);
        }
        if (typeof CanvasRenderer !== 'undefined') {
            CanvasRenderer._needsRender = true;
        }
    });

    // Sync progress
    if (window.callCpp) {
        window.callCpp('GetProgress', '');
        window.callCpp('GetPlayerKnownSpells', '');
    }

    // Analyze orphans and show repair button if needed
    if (typeof analyzeOrphans === 'function') {
        setTimeout(function() {
            analyzeOrphans();
            if (typeof updateOrphanRepairButton === 'function') {
                updateOrphanRepairButton();
            }
        }, 300);
    }

    _logToSKSE('[_loadTrustedTree] === DONE ===');
    setTreeStatus(t('status.loadedTrustedTree', {count: nodes.length}));
}

function loadTreeData(jsonData, switchToTreeTab, isManualImport) {
    var data = typeof jsonData === 'string' ? JSON.parse(jsonData) : jsonData;

    _logToSKSE('[loadTreeData] === ENTRY === typeof=' + (typeof jsonData) +
               ', trustPrereqs=' + data.trustPrereqs +
               ', isManualImport=' + isManualImport +
               ', keys=' + Object.keys(data).join(','));

    // =====================================================================
    // FAST PATH: trustPrereqs data bypasses TreeParser entirely.
    // Positions, prereqs, and edges are authoritative — no mutations.
    // Also detect if nodes have x/y even without the flag (old saves).
    // =====================================================================
    var hasTrustFlag = !!data.trustPrereqs;
    var hasNodePositions = false;
    if (!hasTrustFlag && data.schools) {
        // Check first school's first node for x/y
        for (var _sk in data.schools) {
            if (data.schools.hasOwnProperty(_sk)) {
                var _sn = data.schools[_sk].nodes;
                if (_sn && _sn.length > 0 && _sn[0].x !== undefined) {
                    hasNodePositions = true;
                    _logToSKSE('[loadTreeData] No trustPrereqs flag but nodes have x/y — using fast path');
                }
                break;
            }
        }
    }

    if (hasTrustFlag || hasNodePositions) {
        _logToSKSE('[loadTreeData] FAST PATH — bypassing TreeParser (trust=' + hasTrustFlag + ', hasPos=' + hasNodePositions + ')');
        _loadTrustedTree(data, switchToTreeTab);
        return;
    }

    _logToSKSE('[loadTreeData] SLOW PATH — going through TreeParser (no positions in data)');

    var result = TreeParser.parse(jsonData);
    if (!result.success) {
        showImportError(result.error);
        return;
    }

    // DIAGNOSTIC: Check nodes after TreeParser
    console.log('[loadTreeData] === DIAGNOSTIC: After TreeParser.parse ===');
    var nodesArray = Array.from(result.nodes.values());
    var nodesWithVisualFirst = nodesArray.filter(function(n) { return n._fromVisualFirst; });
    var nodesWithXY = nodesArray.filter(function(n) { return n.x !== 0 || n.y !== 0; });
    var nodesWithChildren = nodesArray.filter(function(n) { return n.children && n.children.length > 0; });

    // Check depth distribution
    var depthCounts = {};
    nodesArray.forEach(function(n) {
        var d = n.depth || 0;
        depthCounts[d] = (depthCounts[d] || 0) + 1;
    });

    console.log('[loadTreeData] Total nodes:', nodesArray.length);
    console.log('[loadTreeData] Nodes with _fromVisualFirst:', nodesWithVisualFirst.length);
    console.log('[loadTreeData] Nodes with non-zero x/y:', nodesWithXY.length);
    console.log('[loadTreeData] Nodes with children:', nodesWithChildren.length);
    console.log('[loadTreeData] Depth distribution:', JSON.stringify(depthCounts));

    if (nodesArray.length > 0) {
        var sample = nodesArray[0];
        console.log('[loadTreeData] Sample node: x=' + sample.x + ', y=' + sample.y +
                    ', depth=' + sample.depth + ', tier=' + sample.tier +
                    ', children=' + (sample.children ? sample.children.length : 0) +
                    ', _fromVisualFirst=' + sample._fromVisualFirst);

        // Find root node and log its children
        var rootNode = nodesArray.find(function(n) { return n.isRoot; });
        if (rootNode) {
            console.log('[loadTreeData] ROOT: id=' + rootNode.id +
                        ', depth=' + rootNode.depth +
                        ', children=' + (rootNode.children ? rootNode.children.length : 0));
        }
    }

    // Store raw data for future merges
    result.rawData = typeof jsonData === 'string' ? JSON.parse(jsonData) : jsonData;
    var rawGlobe = result.rawData ? result.rawData.globe : null;
    result.globe = rawGlobe
        ? { x: rawGlobe.x || 0, y: rawGlobe.y || 0, radius: rawGlobe.radius || 45 }
        : { x: 0, y: 0, radius: 45 };
    state.treeData = result;
    
    // AGGRESSIVE PATH VALIDATION for manual imports
    // Runs path check (no AI self-correction, no procedural injection)
    if (isManualImport && settings.aggressivePathValidation) {
        console.log('[SpellLearning] Running path validation on imported tree...');
        var totalProblems = 0;

        for (var schoolName in TreeParser.schools) {
            var schoolData = TreeParser.schools[schoolName];
            if (schoolData && schoolData.root) {
                var problems = TreeParser.detectAndFixCycles(schoolName, schoolData.root);
                if (problems > 0) {
                    totalProblems += problems;
                    console.log('[SpellLearning] ' + schoolName + ': ' + problems + ' unreachable spells');
                }
            }
        }

        if (totalProblems > 0) {
            console.log('[SpellLearning] Validation found ' + totalProblems + ' unreachable spells - regenerate tree');
            setTreeStatus(t('status.warningUnreachable', {count: totalProblems}));
        }
    }
    
    // Clean up self-references in prerequisites (LLM sometimes generates these incorrectly)
    var selfRefCount = 0;
    result.nodes.forEach(function(node) {
        if (node.prerequisites && node.prerequisites.length > 0) {
            var originalLen = node.prerequisites.length;
            node.prerequisites = node.prerequisites.filter(function(prereqId) {
                // Remove if prereq is this node itself
                return prereqId !== node.id && prereqId !== node.formId;
            });
            if (node.prerequisites.length < originalLen) {
                selfRefCount++;
                console.warn('[SpellLearning] Removed self-reference prerequisite from ' + (node.name || node.id));
            }
        }
    });
    if (selfRefCount > 0) {
        console.log('[SpellLearning] Fixed ' + selfRefCount + ' nodes with self-referencing prerequisites');
    }
    
    // IMPORTANT: Reset all node states to locked/available on load
    // Don't use saved states from file - those are stale
    // States will be updated after player loads into a save game
    
    // Get root nodes for each school
    var rootIds = new Set();
    for (var schoolName in result.schools) {
        var schoolData = result.schools[schoolName];
        if (schoolData.root) {
            rootIds.add(schoolData.root);
        }
    }
    
    result.nodes.forEach(function(node) {
        // Root nodes are always AVAILABLE (learnable starting points, not auto-unlocked)
        // They need to be learned like any other spell, but are always accessible
        if (rootIds.has(node.id) || rootIds.has(node.formId)) {
            node.state = 'available';
            console.log('[SpellLearning] Root node marked available: ' + (node.name || node.id));
        }
        // Nodes with no prerequisites are available (shouldn't happen except for roots)
        else if (!node.prerequisites || node.prerequisites.length === 0) {
            node.state = 'available';
        }
        // Everything else starts locked (children of roots remain locked until root is learned)
        else {
            node.state = 'locked';
        }
    });
    
    // NOTE: Children of root nodes stay LOCKED until root is actually learned (unlocked)
    // The onPlayerKnownSpells callback will mark nodes as 'unlocked' when player has them,
    // and recalculateNodeAvailability will then unlock their children
    
    var stateCount = { unlocked: 0, available: 0, locked: 0 };
    result.nodes.forEach(function(n) { stateCount[n.state] = (stateCount[n.state] || 0) + 1; });
    console.log('[SpellLearning] Tree loaded - states: unlocked=' + stateCount.unlocked + 
                ', available=' + stateCount.available + ', locked=' + stateCount.locked);
    
    // Pass school configs and LLM groups to renderer for visual styling
    var rawData = result.rawData || jsonData;
    if (typeof rawData === 'string') rawData = JSON.parse(rawData);
    
    if (rawData.school_configs) {
        WheelRenderer.setSchoolConfigs(rawData.school_configs);
    }
    if (rawData.llm_groups) {
        WheelRenderer.setLLMGroups(rawData.llm_groups);
    }
    
    // Request spell data for all formIds
    SpellCache.requestBatch(result.allFormIds, function() {
        result.nodes.forEach(function(node) {
            TreeParser.updateNodeFromCache(node);
        });
        SmartRenderer.setData(result.nodes, result.edges, result.schools);
        setTreeStatus('Loaded ' + result.nodes.length + ' spells' + 
            (SmartRenderer.activeRenderer === 'canvas' ? ' (Canvas mode)' : ''));
    });

    // Initial render - use SmartRenderer to auto-switch based on node count
    SmartRenderer.setData(result.nodes, result.edges, result.schools);

    var emptyState = document.getElementById('empty-state');
    if (emptyState) emptyState.classList.add('hidden');
    if (typeof DetailsPeek !== 'undefined') DetailsPeek.applyLayout();

    document.getElementById('total-count').textContent = result.nodes.length;
    document.getElementById('unlocked-count').textContent = '0';  // Always 0 on load - will be updated after save loads
    
    // Switch to Spell Tree tab if requested (e.g., on startup with saved tree)
    if (switchToTreeTab !== false) {
        switchTab('spellTree');
        console.log('[SpellLearning] Tree loaded - switched to Spell Tree tab');
    }
    
    // Compute hard/soft prereqs on each node (source of truth for all systems)
    result.nodes.forEach(function(node) {
        // Use new unified hard/soft prereq system if available
        var hardPrereqs = node.hardPrereqs || [];
        var softPrereqs = node.softPrereqs || [];
        var softNeeded = node.softNeeded || 0;

        // A spell is never its own prerequisite (TreeParser already dropped it; a
        // node edited since may not have been through it)
        hardPrereqs = TreeParser.withoutId(hardPrereqs, node.id);
        softPrereqs = TreeParser.withoutId(softPrereqs, node.id);
        softNeeded = TreeParser.clampSoftNeeded(softNeeded, softPrereqs);

        // Fallback to old system: treat all prereqs as hard if no hard/soft data
        if (hardPrereqs.length === 0 && softPrereqs.length === 0) {
            if (node.prerequisites && node.prerequisites.length > 0) {
                // Single prereq = hard, multiple = first is hard, rest are soft with softNeeded=1
                if (node.prerequisites.length === 1) {
                    hardPrereqs = node.prerequisites.slice();
                } else {
                    hardPrereqs = [node.prerequisites[0]];
                    softPrereqs = node.prerequisites.slice(1);
                    softNeeded = 1;
                }
            }
        }

        // Store computed hard/soft data back on the node so ALL systems
        // (detail panel, recalculateNodeAvailability, C++ bridge) use the
        // same source of truth. This prevents the "hidden prerequisites" bug
        // where the detail panel says "complete" but availability disagrees.
        node.hardPrereqs = hardPrereqs;
        node.softPrereqs = softPrereqs;
        node.softNeeded = softNeeded;
    });

    // Also on this path, or the bridges of the previous tree would stay on screen
    if (typeof BridgeView !== 'undefined') BridgeView.setTree(state.treeData);

    // Send tree prerequisites (hard/soft system) to C++ for tome learning validation
    if (window.callCpp) {
        var prereqData = [];
        var hardCount = 0;
        var softCount = 0;

        for (var pi = 0; pi < result.nodes.length; pi++) {
            var pNode = result.nodes[pi];
            hardCount += (pNode.hardPrereqs ? pNode.hardPrereqs.length : 0);
            softCount += (pNode.softPrereqs ? pNode.softPrereqs.length : 0);
            prereqData.push({
                formId: pNode.formId || pNode.id,
                hardPrereqs: pNode.hardPrereqs || [],
                softPrereqs: pNode.softPrereqs || [],
                softNeeded: pNode.softNeeded || 0
            });
        }

        // First clear existing prerequisites, then set new ones
        console.log('[SpellLearning] Sending ' + prereqData.length + ' spell prereqs (' +
                    hardCount + ' hard, ' + softCount + ' soft) to C++');
        window.callCpp('SetTreePrerequisites', JSON.stringify({ clear: true }));
        window.callCpp('SetTreePrerequisites', JSON.stringify(prereqData));
    }
    
    // After tree is loaded, sync with player's known spells and progression data
    if (window.callCpp) {
        console.log('[SpellLearning] Tree loaded - syncing progress and player known spells...');
        window.callCpp('GetProgress', '');  // Reload progress data
        window.callCpp('GetPlayerKnownSpells', '');  // Sync known spells
    }

    // Analyze orphans and show repair button if needed
    if (typeof analyzeOrphans === 'function') {
        setTimeout(function() {
            analyzeOrphans();
            if (typeof updateOrphanRepairButton === 'function') {
                updateOrphanRepairButton();
            }
        }, 300);
    }
}
