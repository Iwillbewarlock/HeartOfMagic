/**
 * Tree Viewer UI Module
 * Handles spell tree visualization and node selection/interaction
 * 
 * Depends on:
 * - modules/state.js (state, settings)
 * - modules/wheelRenderer.js (WheelRenderer)
 * - modules/treeParser.js (TreeParser)
 * - modules/colorUtils.js (getOrAssignSchoolColor)
 * - modules/uiHelpers.js (updateStatus)
 * 
 * Exports (global): initializeTreeViewer(), the node lookup (_findNodeById,
 * invalidateNodeLookup), SmartRenderer, the Import dialog and mergeTreeData.
 * The rest of the tree page is in treeLoad.js (loadTreeData), spellCardView.js
 * (showSpellDetails, renderSpellCard), spellProgressView.js
 * (updateDetailsProgression, selectNodeById) and findSpell.js, loaded in
 * that order right after this file (split 2026-09-30, 600-line limit).
 */

// =============================================================================
// NODE LOOKUP CACHE — O(1) node-by-id lookup instead of O(n) Array.find()
// =============================================================================

/**
 * Cached map of { formId/id: node } built once per tree load.
 * Rebuilt when state.treeData is a different object (a new tree was loaded).
 */
var _nodeLookupMap = {};
var _nodeLookupTreeRef = null;

/**
 * Make the node lookup map again at its next use: edit mode added or removed a
 * node in the same tree object, which the reference check below cannot see.
 * Costs nothing until the next lookup.
 */
function invalidateNodeLookup() {
    _nodeLookupTreeRef = null;
}

/**
 * Returns a node lookup map { id: node } for the current tree data.
 * Rebuilds the cache only when the tree data reference changes
 * (or after invalidateNodeLookup).
 */
function _getNodeLookupMap() {
    var treeData = state.treeData;
    if (!treeData || !treeData.nodes) {
        return {};
    }
    // Rebuild if the tree data reference changed (new tree loaded)
    if (treeData !== _nodeLookupTreeRef) {
        _nodeLookupMap = {};
        var nodes = treeData.nodes;
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            if (n.id) _nodeLookupMap[n.id] = n;
            // Also index by formId if different from id
            if (n.formId && n.formId !== n.id) _nodeLookupMap[n.formId] = n;
        }
        _nodeLookupTreeRef = treeData;
        console.log('[NodeLookup] Rebuilt cache: ' + Object.keys(_nodeLookupMap).length + ' entries');
    }
    return _nodeLookupMap;
}

/**
 * Look up a node by its id/formId in O(1).
 * Falls back to null if not found.
 */
function _findNodeById(id) {
    var map = _getNodeLookupMap();
    return map[id] || null;
}

// =============================================================================
// TREE VIEWER
// =============================================================================

// Smart renderer - auto-selects best renderer based on node count
// Tiers: SVG (<200), Canvas 2D (200+)
var SmartRenderer = {
    activeRenderer: 'canvas',  // 'svg' or 'canvas' — matches forceRenderer default

    // Thresholds for renderer selection
    svgThreshold: 200,      // Use SVG below this, Canvas 2D above

    // Force a specific renderer (set to null for auto)
    forceRenderer: 'canvas',    // FORCED: Always use Canvas 2D for best performance

    /**
     * Select the best renderer for given node count
     * @param {number} nodeCount
     * @returns {string} 'svg' or 'canvas'
     */
    selectRenderer: function(nodeCount) {
        if (this.forceRenderer) {
            if (this.forceRenderer === 'canvas' && typeof CanvasRenderer !== 'undefined') {
                return 'canvas';
            } else if (this.forceRenderer === 'svg') {
                return 'svg';
            }
        }

        if (nodeCount > this.svgThreshold && typeof CanvasRenderer !== 'undefined') {
            return 'canvas';
        }

        return 'svg';
    },

    setData: function(nodes, edges, schools) {
        var nodeCount = nodes ? nodes.length : 0;
        var targetRenderer = this.selectRenderer(nodeCount);

        console.log('[SmartRenderer] setData: ' + nodeCount + ' nodes -> ' + targetRenderer + ' mode');

        var container = document.getElementById('tree-container');
        if (!container) {
            console.error('[SmartRenderer] tree-container not found!');
            return;
        }

        this.hideAll();

        if (targetRenderer === 'canvas' && typeof CanvasRenderer !== 'undefined') {
            if (!CanvasRenderer.container || CanvasRenderer.container !== container) {
                CanvasRenderer.init(container);
            }

            this.activeRenderer = 'canvas';
            CanvasRenderer.clear();
            CanvasRenderer.setData(nodes, edges, schools);
            CanvasRenderer.show();
            CanvasRenderer.centerView();
            CanvasRenderer.forceRender();
            this.updateRendererBadge();
            console.log('[SmartRenderer] Canvas render triggered with ' + nodeCount + ' nodes');
            return;
        }

        this.activeRenderer = 'svg';
        WheelRenderer.setData(nodes, edges, schools);
        this.updateRendererBadge();
        console.log('[SmartRenderer] SVG render triggered with ' + nodeCount + ' nodes');
    },

    hideAll: function() {
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.canvas) {
            CanvasRenderer.hide();
        }
        WheelRenderer.clear();
        if (typeof TreeNav !== 'undefined') TreeNav.clearSchoolTabs();
    },

    render: function() {
        if (this.activeRenderer === 'canvas') {
            if (typeof CanvasRenderer !== 'undefined') CanvasRenderer.forceRender();
        } else {
            WheelRenderer.render();
        }
    },

    clear: function() {
        this.hideAll();
        this.activeRenderer = 'svg';
    },

    setZoom: function(z) {
        if (this.activeRenderer === 'canvas') {
            if (typeof CanvasRenderer !== 'undefined') CanvasRenderer.setZoom(z);
        } else {
            WheelRenderer.setZoom(z);
        }
    },

    centerView: function() {
        if (this.activeRenderer === 'canvas') {
            if (typeof CanvasRenderer !== 'undefined') CanvasRenderer.centerView();
        } else {
            WheelRenderer.centerView();
        }
    },

    getZoom: function() {
        if (this.activeRenderer === 'canvas') {
            if (typeof CanvasRenderer !== 'undefined') return CanvasRenderer.zoom;
        }
        return WheelRenderer.zoom;
    },

    /**
     * Get current renderer info for debugging
     */
    getInfo: function() {
        return {
            active: this.activeRenderer,
            forced: this.forceRenderer,
            canvasAvailable: typeof CanvasRenderer !== 'undefined',
            rendererName: 'CanvasRenderer'
        };
    },

    /**
     * Refresh the active renderer when node states change
     * Call this after spell unlock/progression changes
     */
    refresh: function() {
        if (this.activeRenderer === 'canvas') {
            if (typeof CanvasRenderer !== 'undefined') CanvasRenderer.refresh();
        } else {
            WheelRenderer.render();
        }
    },

    /**
     * Update the renderer badge UI to show current renderer
     */
    updateRendererBadge: function() {
        var badge = document.getElementById('renderer-badge');
        if (!badge) return;

        badge.classList.remove('svg', 'canvas');
        var renderer = this.activeRenderer.toUpperCase();
        badge.textContent = renderer;
        badge.classList.add(this.activeRenderer);
        badge.title = 'Renderer: ' + renderer +
            (this.activeRenderer === 'canvas' ? ' (Canvas 2D)' : ' (SVG DOM)');
    }
};

// Export SmartRenderer
window.SmartRenderer = SmartRenderer;

function initializeTreeViewer() {
    var svg = document.getElementById('tree-svg');
    if (!svg) return;

    WheelRenderer.init(svg);

    // Initialize canvas renderer
    var container = document.getElementById('tree-container');
    if (container && typeof CanvasRenderer !== 'undefined') {
        CanvasRenderer.init(container);
        console.log('[TreeViewer] CanvasRenderer ready');
    }
    
    state.treeInitialized = true;

    // Update renderer badge to reflect actual renderer (defaults to canvas)
    SmartRenderer.updateRendererBadge();

    // Zoom controls - use SmartRenderer for auto renderer selection
    document.getElementById('zoom-in').addEventListener('click', function() {
        SmartRenderer.setZoom(SmartRenderer.getZoom() + TREE_CONFIG.zoom.step);
    });
    document.getElementById('zoom-out').addEventListener('click', function() {
        SmartRenderer.setZoom(SmartRenderer.getZoom() - TREE_CONFIG.zoom.step);
    });
    
    // Import buttons
    var importBtn = document.getElementById('import-btn');

    var goToScannerBtn = document.getElementById('go-to-scanner-btn');
    if (goToScannerBtn) goToScannerBtn.addEventListener('click', function() { switchTab('spellScan'); });
    if (importBtn) importBtn.addEventListener('click', showImportModal);
    
    // Modal controls
    var modalCloseBtn = document.getElementById('modal-close-btn');
    var importCancel = document.getElementById('import-cancel');
    var importConfirm = document.getElementById('import-confirm');
    var pasteTreeBtn = document.getElementById('paste-tree-btn');
    var modalBackdrop = document.querySelector('.modal-backdrop');
    
    if (modalCloseBtn) modalCloseBtn.addEventListener('click', hideImportModal);
    if (importCancel) importCancel.addEventListener('click', hideImportModal);
    if (pasteTreeBtn) pasteTreeBtn.addEventListener('click', onPasteTreeClick);
    if (importConfirm) importConfirm.addEventListener('click', importTreeFromModal);
    if (modalBackdrop) modalBackdrop.addEventListener('click', hideImportModal);
    
    // Details panel
    window.addEventListener('nodeSelected', function(e) { showSpellDetails(e.detail); });
    // A click on nothing (canvas) drops the selection
    window.addEventListener('nodeDeselected', function() { clearSpellSelection(); });

    var closeDetails = document.getElementById('close-details');
    if (closeDetails) closeDetails.addEventListener('click', clearSpellSelection);
    
    // How-to-Learn panel
    initializeHowToPanel();
    
    // Learn button - set learning target
    var learnBtn = document.getElementById('learn-btn');
    if (learnBtn) learnBtn.addEventListener('click', onLearnClick);
    
    // Unlock button - unlock spell when XP is ready
    var unlockBtn = document.getElementById('unlock-btn');
    if (unlockBtn) unlockBtn.addEventListener('click', onUnlockClick);
    
    // Clickable prereqs/unlocks
    var prereqList = document.getElementById('spell-prereqs');
    var unlocksList = document.getElementById('spell-unlocks');
    var hardPrereqsList = document.getElementById('hard-prereqs-list');
    var softPrereqsList = document.getElementById('soft-prereqs-list');
    
    // Handler that only selects if spell name is revealed (not ???)
    function handlePrereqClick(e) {
        if (e.target.tagName === 'LI') {
            var id = e.target.dataset.id;
            var text = e.target.textContent.replace(/^[✓✗]\s*/, '').trim();
            // Only navigate if the name is revealed (not hidden as ???)
            if (id && text !== '???' && text.indexOf('???') === -1) {
                selectNodeById(id);
            }
        }
    }
    
    if (prereqList) prereqList.addEventListener('click', handlePrereqClick);
    if (unlocksList) unlocksList.addEventListener('click', handlePrereqClick);
    if (hardPrereqsList) hardPrereqsList.addEventListener('click', handlePrereqClick);
    if (softPrereqsList) softPrereqsList.addEventListener('click', handlePrereqClick);
    var bridgesList = document.getElementById('spell-bridges');
    if (bridgesList) bridgesList.addEventListener('click', handlePrereqClick);

    // Find Spell (F key)
    initializeFindSpell();
}

function showImportModal() {
    var modal = document.getElementById('import-modal');
    if (modal) modal.classList.remove('hidden');
    var errorBox = document.getElementById('import-error');
    if (errorBox) errorBox.classList.add('hidden');

    // Pre-fill with current tree JSON
    var textarea = document.getElementById('import-textarea');
    if (textarea && state.treeData && state.treeData.rawData) {
        textarea.value = JSON.stringify(state.treeData.rawData, null, 2);
    }
}

function hideImportModal() {
    var modal = document.getElementById('import-modal');
    if (modal) modal.classList.add('hidden');
}

function loadSavedTree() {
    if (window.callCpp) {
        window.callCpp('LoadSpellTree', '');
        setTreeStatus(t('status.loadingSavedTree'));
    } else {
        setTreeStatus(t('status.noSavedTree'));
    }
}

function importTreeFromModal() {
    var textarea = document.getElementById('import-textarea');
    var text = textarea ? textarea.value.trim() : '';
    if (!text) {
        showImportError(t('status.noJsonToSave'));
        return;
    }

    // Step 1: Parse JSON
    var data;
    try {
        data = JSON.parse(text);
    } catch (e) {
        showImportError(t('status.invalidJson', {error: e.message}));
        return;
    }

    // Step 2: Validate through TreeParser
    var parsed = TreeParser.parse(data);
    if (!parsed.success) {
        showImportError(t('status.treeValidationFailed', {error: parsed.error || 'Unknown error'}));
        return;
    }

    // Step 3: Load the validated tree
    loadTreeData(data, true, true);
    hideImportModal();
    setTreeStatus(t('status.treeSavedCount', {count: parsed.nodes.length}));

    // Save to file via C++
    if (window.callCpp) {
        window.callCpp('SaveSpellTree', JSON.stringify(data));
    }
}

/**
 * Simple deep copy for plain data objects (no functions, no circular refs).
 * Avoids the overhead of JSON.parse(JSON.stringify()) serialization round-trip.
 */
function _deepCopyPlain(obj) {
    if (obj === null || typeof obj !== 'object') return obj;
    if (Array.isArray(obj)) {
        var arrCopy = [];
        for (var i = 0; i < obj.length; i++) {
            arrCopy[i] = _deepCopyPlain(obj[i]);
        }
        return arrCopy;
    }
    var objCopy = {};
    for (var key in obj) {
        if (obj.hasOwnProperty(key)) {
            objCopy[key] = _deepCopyPlain(obj[key]);
        }
    }
    return objCopy;
}

function mergeTreeData(existing, newData) {
    // Create a deep copy of existing data
    var merged = _deepCopyPlain(existing);

    if (!merged.schools) merged.schools = {};
    
    // Merge each school from new data
    for (var schoolName in newData.schools) {
        var newSchool = newData.schools[schoolName];
        
        if (!merged.schools[schoolName]) {
            // School doesn't exist, add it entirely
            merged.schools[schoolName] = newSchool;
            console.log('[Merge] Added new school: ' + schoolName + ' (layout: ' + (newSchool.layoutStyle || 'radial') + ')');
        } else {
            // School exists, merge nodes
            var existingSchool = merged.schools[schoolName];
            
            // Update layoutStyle if new data provides one
            if (newSchool.layoutStyle && !existingSchool.layoutStyle) {
                existingSchool.layoutStyle = newSchool.layoutStyle;
                console.log('[Merge] Updated ' + schoolName + ' layout style: ' + newSchool.layoutStyle);
            }
            var existingNodeIds = {};
            var existingNodeMap = {};
            for (var ei = 0; ei < existingSchool.nodes.length; ei++) {
                var eNodeId = existingSchool.nodes[ei].formId || existingSchool.nodes[ei].spellId;
                existingNodeIds[eNodeId] = true;
                existingNodeMap[eNodeId] = existingSchool.nodes[ei];
            }

            var addedCount = 0;
            newSchool.nodes.forEach(function(newNode) {
                var nodeId = newNode.formId || newNode.spellId;
                if (!existingNodeIds[nodeId]) {
                    existingSchool.nodes.push(newNode);
                    addedCount++;
                } else {
                    // Node exists - update children/prerequisites if new ones exist
                    var existingNode = existingNodeMap[nodeId];
                    if (existingNode && newNode.children) {
                        if (!existingNode.children) existingNode.children = [];
                        var childrenSeen = {};
                        for (var ci = 0; ci < existingNode.children.length; ci++) {
                            childrenSeen[existingNode.children[ci]] = true;
                        }
                        newNode.children.forEach(function(childId) {
                            if (!childrenSeen[childId]) {
                                childrenSeen[childId] = true;
                                existingNode.children.push(childId);
                            }
                        });
                    }
                    if (existingNode && newNode.prerequisites) {
                        if (!existingNode.prerequisites) existingNode.prerequisites = [];
                        var prereqsSeen = {};
                        for (var pri = 0; pri < existingNode.prerequisites.length; pri++) {
                            prereqsSeen[existingNode.prerequisites[pri]] = true;
                        }
                        newNode.prerequisites.forEach(function(prereqId) {
                            if (!prereqsSeen[prereqId]) {
                                prereqsSeen[prereqId] = true;
                                existingNode.prerequisites.push(prereqId);
                            }
                        });
                    }
                }
            });
            console.log('[Merge] ' + schoolName + ': added ' + addedCount + ' new nodes');
        }
    }
    
    // Update timestamp
    merged.generatedAt = new Date().toISOString();
    merged.version = merged.version || '1.0';
    
    return merged;
}

function showImportError(msg) {
    var errorBox = document.getElementById('import-error');
    if (errorBox) {
        errorBox.textContent = msg;
        errorBox.classList.remove('hidden');
    }
}

/** Log to both console and spelllearning.log via C++. */
function _logToSKSE(msg) {
    if (typeof LogGate !== 'undefined' && !LogGate.on()) return;
    console.log(msg);
    if (window.callCpp) {
        try {
            window.callCpp('LogMessage', JSON.stringify({ message: msg, level: 'info' }));
        } catch (e) { /* ignore */ }
    }
}
