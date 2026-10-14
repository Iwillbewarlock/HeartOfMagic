/**
 * Edit Mode Module
 * Provides tree editing functionality:
 * - Move Tool: Drag nodes to reposition them
 * - Pen Tool: Draw lines between nodes to create prerequisites
 * - Eraser Tool: Draw to erase edges/prerequisites
 *
 * Depends on:
 * - modules/canvasRendererV2.js (CanvasRenderer)
 * - modules/config.js (GRID_CONFIG)
 * - modules/state.js (state)
 *
 * Exports (global):
 * - EditMode
 */

var EditMode = {
    // State
    isActive: false,
    currentTool: 'move', // 'move', 'pen', 'eraser'

    // Move tool state
    isDragging: false,
    draggedNode: null,
    draggedGlobe: null,
    dragStartX: 0,
    dragStartY: 0,
    originalNodeX: 0,
    originalNodeY: 0,
    originalGlobeX: 0,
    originalGlobeY: 0,

    // Pen tool state
    penStartNode: null,
    penCurrentX: 0,
    penCurrentY: 0,
    isPenDrawing: false,

    // Eraser tool state
    isErasing: false,
    eraserPath: [], // Array of {x, y} points

    // Cached grid positions for snapping
    gridPositions: [],

    // Undo stack (max 5 entries)
    undoStack: [],
    maxUndoSize: 5,

    // References to original handlers
    _originalOnMouseDown: null,
    _originalOnMouseMove: null,
    _originalOnMouseUp: null,
    _originalOnClick: null,
    _keydownHandler: null,

    /**
     * Initialize edit mode (call once on page load)
     */
    init: function() {
        var btn = document.getElementById('edit-tree-btn');
        if (!btn) {
            console.warn('[EditMode] Button not found');
            return;
        }

        var self = this;
        btn.addEventListener('click', function(e) {
            e.stopPropagation();
            self.toggle();
        });

        // Tool button handlers
        var moveBtn = document.getElementById('edit-tool-move');
        var penBtn = document.getElementById('edit-tool-pen');
        var eraserBtn = document.getElementById('edit-tool-eraser');

        if (moveBtn) {
            moveBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                self.selectTool('move');
            });
        }
        if (penBtn) {
            penBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                self.selectTool('pen');
            });
        }
        if (eraserBtn) {
            eraserBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                self.selectTool('eraser');
            });
        }

        console.log('[EditMode] Initialized');
    },

    /**
     * Select a tool
     */
    selectTool: function(tool) {
        this.currentTool = tool;

        // Update button states
        var buttons = document.querySelectorAll('.edit-tool');
        buttons.forEach(function(btn) {
            btn.classList.remove('active');
        });

        var activeBtn = document.getElementById('edit-tool-' + tool);
        if (activeBtn) {
            activeBtn.classList.add('active');
        }

        // Update cursor
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.canvas) {
            if (tool === 'move') {
                CanvasRenderer.canvas.style.cursor = 'grab';
            } else if (tool === 'pen') {
                CanvasRenderer.canvas.style.cursor = 'crosshair';
            } else if (tool === 'eraser') {
                CanvasRenderer.canvas.style.cursor = 'not-allowed';
            }
        }

        console.log('[EditMode] Tool selected:', tool);
    },

    /**
     * Toggle edit mode on/off
     */
    toggle: function() {
        if (this.isActive) {
            this.deactivate();
        } else {
            this.activate();
        }
        return this.isActive;
    },

    /**
     * Activate edit mode
     */
    activate: function() {
        if (this.isActive) return;
        this.isActive = true;

        // Update button appearance
        var btn = document.getElementById('edit-tree-btn');
        if (btn) {
            btn.textContent = '✓';
            btn.title = t('editMode.exitEditMode');
            btn.classList.add('active');
        }

        // Show edit tools
        var editTools = document.getElementById('edit-tools');
        if (editTools) {
            editTools.style.display = 'inline-flex';
        }
        // The bar got wider: at a bigger size it may wrap differently (treeToolsScale.js)
        if (typeof TreeToolsScale !== 'undefined') TreeToolsScale.apply();

        // Select move tool by default
        this.selectTool('move');

        // Show debug grid
        if (typeof CanvasRenderer !== 'undefined') {
            CanvasRenderer.showDebugGrid = true;
            CanvasRenderer._needsRender = true;
        }

        // Build grid positions for snapping
        this.buildGridPositions();

        // Override canvas event handlers
        this.installEventHandlers();

        // Install keyboard handler for Ctrl+Z undo
        this.installKeyboardHandler();

        // Clear undo stack on fresh activation
        this.undoStack = [];

        console.log('[EditMode] Activated');
    },

    /**
     * Deactivate edit mode
     */
    deactivate: function() {
        if (!this.isActive) return;
        this.isActive = false;

        // Update button appearance
        var btn = document.getElementById('edit-tree-btn');
        if (btn) {
            btn.textContent = '✎';
            btn.title = t('editMode.editTree');
            btn.classList.remove('active');
        }

        // Hide edit tools
        var editTools = document.getElementById('edit-tools');
        if (editTools) {
            editTools.style.display = 'none';
        }
        if (typeof TreeToolsScale !== 'undefined') TreeToolsScale.apply();

        // Hide debug grid and rebuild discovery visibility
        if (typeof CanvasRenderer !== 'undefined') {
            CanvasRenderer.showDebugGrid = false;
            CanvasRenderer._buildDiscoveryVisibility();
            CanvasRenderer._needsRender = true;
        }

        // Restore original event handlers
        this.uninstallEventHandlers();

        // Remove keyboard handler
        this.uninstallKeyboardHandler();

        // Clear all tool states
        this.isDragging = false;
        this.draggedNode = null;
        this.isPenDrawing = false;
        this.penStartNode = null;
        this.isErasing = false;
        this.eraserPath = [];

        // Refresh details panel to re-apply reveal rules
        if (state.selectedNode && typeof showSpellDetails === 'function') {
            showSpellDetails(state.selectedNode);
        }

        console.log('[EditMode] Deactivated');
    },

    /**
     * Build array of all valid grid positions for snapping
     */
    buildGridPositions: function() {
        this.gridPositions = [];

        var spacing = 50;
        var extent = 1300;

        for (var gx = -extent; gx <= extent; gx += spacing) {
            for (var gy = -extent; gy <= extent; gy += spacing) {
                this.gridPositions.push({ x: gx, y: gy });
            }
        }

        console.log('[EditMode] Built ' + this.gridPositions.length + ' square grid positions');
    },

    /**
     * Find the nearest empty grid position to a given point
     */
    findNearestEmptyGridPosition: function(x, y, excludeNode) {
        var minDist = Infinity;
        var nearest = null;

        var occupiedPositions = new Set();
        if (CanvasRenderer.nodes) {
            for (var i = 0; i < CanvasRenderer.nodes.length; i++) {
                var node = CanvasRenderer.nodes[i];
                if (node !== excludeNode) {
                    var key = Math.round(node.x) + ',' + Math.round(node.y);
                    occupiedPositions.add(key);
                }
            }
        }

        for (var i = 0; i < this.gridPositions.length; i++) {
            var pos = this.gridPositions[i];
            var key = Math.round(pos.x) + ',' + Math.round(pos.y);

            if (occupiedPositions.has(key)) continue;

            var dx = pos.x - x;
            var dy = pos.y - y;
            var dist = dx * dx + dy * dy;

            if (dist < minDist) {
                minDist = dist;
                nearest = pos;
            }
        }

        return nearest;
    },

    /**
     * Find the nearest grid position to a given point (regardless of occupancy)
     */
    findNearestGridPosition: function(x, y) {
        var minDist = Infinity;
        var nearest = null;

        for (var i = 0; i < this.gridPositions.length; i++) {
            var pos = this.gridPositions[i];
            var dx = pos.x - x;
            var dy = pos.y - y;
            var dist = dx * dx + dy * dy;

            if (dist < minDist) {
                minDist = dist;
                nearest = pos;
            }
        }

        return nearest;
    },

    /**
     * Install custom event handlers
     */
    installEventHandlers: function() {
        if (typeof CanvasRenderer === 'undefined' || !CanvasRenderer.canvas) return;

        var self = this;

        // Store original handlers
        this._originalOnMouseDown = CanvasRenderer.onMouseDown.bind(CanvasRenderer);
        this._originalOnMouseMove = CanvasRenderer.onMouseMove.bind(CanvasRenderer);
        this._originalOnMouseUp = CanvasRenderer.onMouseUp.bind(CanvasRenderer);
        this._originalOnClick = CanvasRenderer.onClick.bind(CanvasRenderer);

        // Override with edit mode handlers
        CanvasRenderer.onMouseDown = function(e) { self.handleMouseDown(e); };
        CanvasRenderer.onMouseMove = function(e) { self.handleMouseMove(e); };
        CanvasRenderer.onMouseUp = function(e) { self.handleMouseUp(e); };
        CanvasRenderer.onClick = function(e) { self.handleClick(e); };
    },

    /**
     * Restore original event handlers
     */
    uninstallEventHandlers: function() {
        if (typeof CanvasRenderer === 'undefined') return;

        if (this._originalOnMouseDown) CanvasRenderer.onMouseDown = this._originalOnMouseDown;
        if (this._originalOnMouseMove) CanvasRenderer.onMouseMove = this._originalOnMouseMove;
        if (this._originalOnMouseUp) CanvasRenderer.onMouseUp = this._originalOnMouseUp;
        if (this._originalOnClick) CanvasRenderer.onClick = this._originalOnClick;

        this._originalOnMouseDown = null;
        this._originalOnMouseMove = null;
        this._originalOnMouseUp = null;
        this._originalOnClick = null;
    },

    /**
     * Install keyboard handler for Ctrl+Z undo
     */
    installKeyboardHandler: function() {
        var self = this;
        this._keydownHandler = function(e) {
            if (e.ctrlKey && e.key === 'z' && self.isActive) {
                e.preventDefault();
                self.undo();
            }
        };
        document.addEventListener('keydown', this._keydownHandler);
    },

    /**
     * Remove keyboard handler
     */
    uninstallKeyboardHandler: function() {
        if (this._keydownHandler) {
            document.removeEventListener('keydown', this._keydownHandler);
            this._keydownHandler = null;
        }
    },

    /**
     * Push an action to the undo stack
     * @param {Object} action - { type, data }
     */
    pushUndo: function(action) {
        this.undoStack.push(action);
        if (this.undoStack.length > this.maxUndoSize) {
            this.undoStack.shift(); // Remove oldest
        }
        console.log('[EditMode] Undo stack size:', this.undoStack.length);
    },

    /**
     * Undo the last action
     */
    undo: function() {
        if (this.undoStack.length === 0) {
            console.log('[EditMode] Nothing to undo');
            if (typeof setTreeStatus === 'function') {
                setTreeStatusKey('editMode.nothingToUndo');
            }
            return;
        }

        var action = this.undoStack.pop();
        console.log('[EditMode] Undoing:', action.type);

        switch (action.type) {
            case 'move':
                this._undoMove(action.data);
                break;
            case 'moveGlobe':
                this._undoMoveGlobe(action.data);
                break;
            case 'addPrereq':
                this._undoAddPrereq(action.data);
                break;
            case 'removePrereq':
                this._undoRemovePrereq(action.data);
                break;
            case 'togglePrereqType':
                this._undoTogglePrereqType(action.data);
                break;
            case 'updateSoftNeeded':
                this._undoUpdateSoftNeeded(action.data);
                break;
        }

        if (typeof setTreeStatus === 'function') {
            setTreeStatusKey('editMode.undoAction', {action: action.type});
        }
    },

    _undoMove: function(data) {
        var node = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(data.nodeId) : null;
        if (node) {
            node.x = data.oldX;
            node.y = data.oldY;
            this.syncNodePositionToRawData(node);
            this.saveTree();
            CanvasRenderer._needsRender = true;
            CanvasRenderer.buildSpatialIndex();
        }
    },

    _undoMoveGlobe: function(data) {
        var globe = (state.treeData && state.treeData.globe);
        if (globe) {
            globe.x = data.oldX;
            globe.y = data.oldY;
            this.syncGlobePositionToRawData();
            this.saveTree();
            CanvasRenderer._needsRender = true;
        }
    },

    _undoAddPrereq: function(data) {
        // Remove the prereq that was added (from every list: PrereqEdit.remove)
        if (!this._bothExist(data.fromId, data.toId)) return;
        var toNode = CanvasRenderer._nodeMap.get(data.toId);
        PrereqEdit.remove(toNode, data.fromId);
        this._unlinkChild(data.fromId, data.toId);
        this._afterPrereqEdit(toNode);
    },

    _undoRemovePrereq: function(data) {
        // Re-add the prereq the way it was (an entry without `was`: drawn, hard or soft by wasHard).
        // Never a link to or from a deleted spell: nothing is changed or saved then
        if (!this._bothExist(data.fromId, data.toId)) return;
        var toNode = CanvasRenderer._nodeMap.get(data.toId);
        var was = data.was || { type: data.wasHard ? 'hard' : 'soft', linked: true, softNeeded: toNode.softNeeded || 0 };
        PrereqEdit.restore(toNode, data.fromId, was);
        if (was.linked) this._linkChild(data.fromId, data.toId);
        this._afterPrereqEdit(toNode);
    },

    _undoTogglePrereqType: function(data) {
        if (!this._bothExist(data.prereqId, data.nodeId)) return;
        var node = CanvasRenderer._nodeMap.get(data.nodeId);

        // Back to the type it had (none, when the toggle added it) and the old softNeeded
        var wasType = data.wasType !== undefined ? data.wasType : (data.wasHard ? 'hard' : 'soft');
        var linked = !!(node.prerequisites && node.prerequisites.indexOf(data.prereqId) !== -1);
        var softNeeded = data.softNeeded !== undefined ? data.softNeeded : (node.softNeeded || 0);
        PrereqEdit.restore(node, data.prereqId, { type: wasType, linked: linked, softNeeded: softNeeded });
        this._afterPrereqEdit(node);
    },

    /** Both spells still in the tree (an undo entry may name one deleted since) */
    _bothExist: function(a, b) {
        var map = CanvasRenderer._nodeMap;
        return !!(map && map.get(a) && map.get(b));
    },

    /** fromId's child entry and the drawn line to toId, once each */
    _linkChild: function(fromId, toId) {
        var fromNode = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(fromId) : null;
        if (fromNode) {
            if (!fromNode.children) fromNode.children = [];
            if (fromNode.children.indexOf(toId) === -1) fromNode.children.push(toId);
        }
        if (CanvasRenderer.edges) {
            var exists = CanvasRenderer.edges.some(function(e) { return e.from === fromId && e.to === toId; });
            if (!exists) CanvasRenderer.edges.push({ from: fromId, to: toId });
        }
    },

    /** fromId's child entry and the drawn line to toId out */
    _unlinkChild: function(fromId, toId) {
        var fromNode = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(fromId) : null;
        if (fromNode && fromNode.children) {
            var idx = fromNode.children.indexOf(toId);
            if (idx !== -1) fromNode.children.splice(idx, 1);
        }
        if (CanvasRenderer.edges) {
            for (var i = CanvasRenderer.edges.length - 1; i >= 0; i--) {
                var e = CanvasRenderer.edges[i];
                if (e.from === fromId && e.to === toId) CanvasRenderer.edges.splice(i, 1);
            }
        }
    },

    /** After a prerequisite change: rawData, save, repaint, the card if it shows `node` */
    _afterPrereqEdit: function(node) {
        this.syncPrerequisitesToRawData();
        this.saveTree();
        CanvasRenderer._needsRender = true;
        if (node && state.selectedNode && (state.selectedNode.formId === node.formId || state.selectedNode.id === node.id)) {
            if (typeof showSpellDetails === 'function') showSpellDetails(node);
        }
    },

    _undoUpdateSoftNeeded: function(data) {
        var node = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(data.nodeId) : null;
        if (!node) return;

        node.softNeeded = data.oldValue;

        this.syncPrerequisitesToRawData();
        this.saveTree();

        // Refresh details panel
        if (state.selectedNode && (state.selectedNode.formId === node.formId || state.selectedNode.id === node.id)) {
            if (typeof showSpellDetails === 'function') showSpellDetails(node);
        }
    },

    /**
     * Handle mouse down
     */
    handleMouseDown: function(e) {
        var rect = CanvasRenderer.canvas.getBoundingClientRect();
        var world = CanvasRenderer.screenToWorld(e.clientX - rect.left, e.clientY - rect.top);

        if (this.currentTool === 'move') {
            this.handleMoveMouseDown(e, world);
        } else if (this.currentTool === 'pen') {
            this.handlePenMouseDown(e, world);
        } else if (this.currentTool === 'eraser') {
            this.handleEraserMouseDown(e, world);
        }
    },

    /**
     * Handle mouse move
     */
    handleMouseMove: function(e) {
        var rect = CanvasRenderer.canvas.getBoundingClientRect();
        var world = CanvasRenderer.screenToWorld(e.clientX - rect.left, e.clientY - rect.top);

        if (this.currentTool === 'move') {
            this.handleMoveMouseMove(e, world);
        } else if (this.currentTool === 'pen') {
            this.handlePenMouseMove(e, world);
        } else if (this.currentTool === 'eraser') {
            this.handleEraserMouseMove(e, world);
        }
    },

    /**
     * Handle mouse up
     */
    handleMouseUp: function(e) {
        var rect = CanvasRenderer.canvas.getBoundingClientRect();
        var world = CanvasRenderer.screenToWorld(e.clientX - rect.left, e.clientY - rect.top);

        if (this.currentTool === 'move') {
            this.handleMoveMouseUp(e, world);
        } else if (this.currentTool === 'pen') {
            this.handlePenMouseUp(e, world);
        } else if (this.currentTool === 'eraser') {
            this.handleEraserMouseUp(e, world);
        }
    },

    /**
     * Handle click
     */
    handleClick: function(e) {
        // Prevent default click during active tool operations
        if (this.isDragging || this.isPenDrawing || this.isErasing) {
            return;
        }
        this._originalOnClick(e);
    },

    // =========================================================================
    // MOVE TOOL
    // =========================================================================

    handleMoveMouseDown: function(e, world) {
        // Check globe first (it's on top visually)
        var globe = CanvasRenderer.findGlobeAt(world.x, world.y);
        if (globe && e.button === 0) {
            this.isDragging = true;
            this.draggedGlobe = globe;
            this.draggedNode = null;
            this.dragStartX = world.x;
            this.dragStartY = world.y;
            this.originalGlobeX = globe.x;
            this.originalGlobeY = globe.y;
            CanvasRenderer.canvas.style.cursor = 'grabbing';
            console.log('[EditMode] Started dragging globe');
            return;
        }

        var node = CanvasRenderer.findNodeAt(world.x, world.y);

        if (node && e.button === 0) {
            this.isDragging = true;
            this.draggedNode = node;
            this.draggedGlobe = null;
            this.dragStartX = world.x;
            this.dragStartY = world.y;
            this.originalNodeX = node.x;
            this.originalNodeY = node.y;
            CanvasRenderer.canvas.style.cursor = 'grabbing';
            console.log('[EditMode] Started dragging:', node.name || node.id);
        } else {
            this._originalOnMouseDown(e);
        }
    },

    handleMoveMouseMove: function(e, world) {
        if (this.isDragging && this.draggedGlobe) {
            this.draggedGlobe.x = this.originalGlobeX + (world.x - this.dragStartX);
            this.draggedGlobe.y = this.originalGlobeY + (world.y - this.dragStartY);
            CanvasRenderer._needsRender = true;
            return;
        }
        if (this.isDragging && this.draggedNode) {
            this.draggedNode.x = this.originalNodeX + (world.x - this.dragStartX);
            this.draggedNode.y = this.originalNodeY + (world.y - this.dragStartY);
            CanvasRenderer._needsRender = true;
        } else {
            this._originalOnMouseMove(e);
        }
    },

    handleMoveMouseUp: function(e, world) {
        if (this.isDragging && this.draggedGlobe) {
            // Snap globe to grid if Shift is held
            if (e.shiftKey) {
                var snapPos = this.findNearestEmptyGridPosition(
                    this.draggedGlobe.x,
                    this.draggedGlobe.y,
                    null
                );
                if (snapPos) {
                    this.draggedGlobe.x = snapPos.x;
                    this.draggedGlobe.y = snapPos.y;
                    console.log('[EditMode] Snapped globe to:', Math.round(snapPos.x), Math.round(snapPos.y));
                }
            }

            // Round to 2 decimal places
            this.draggedGlobe.x = Math.round(this.draggedGlobe.x * 100) / 100;
            this.draggedGlobe.y = Math.round(this.draggedGlobe.y * 100) / 100;

            console.log('[EditMode] Placed globe at:', this.draggedGlobe.x, this.draggedGlobe.y);

            // Push to undo stack
            this.pushUndo({
                type: 'moveGlobe',
                data: {
                    oldX: this.originalGlobeX,
                    oldY: this.originalGlobeY
                }
            });

            // Sync to rawData and save
            this.syncGlobePositionToRawData();
            this.saveTree();

            this.isDragging = false;
            this.draggedGlobe = null;
            CanvasRenderer.canvas.style.cursor = 'grab';
            CanvasRenderer._needsRender = true;
            return;
        }

        if (this.isDragging && this.draggedNode) {
            // Only snap to grid if Shift is held
            if (e.shiftKey) {
                var snapPos = this.findNearestEmptyGridPosition(
                    this.draggedNode.x,
                    this.draggedNode.y,
                    this.draggedNode
                );

                if (snapPos) {
                    this.draggedNode.x = snapPos.x;
                    this.draggedNode.y = snapPos.y;
                    console.log('[EditMode] Snapped', this.draggedNode.name || this.draggedNode.id,
                        'to:', Math.round(snapPos.x), Math.round(snapPos.y));
                } else {
                    console.log('[EditMode] No empty grid position found');
                }
            } else {
                console.log('[EditMode] Placed', this.draggedNode.name || this.draggedNode.id,
                    'at:', Math.round(this.draggedNode.x), Math.round(this.draggedNode.y));
            }

            // Push to undo stack before saving
            this.pushUndo({
                type: 'move',
                data: {
                    nodeId: this.draggedNode.formId || this.draggedNode.id,
                    oldX: this.originalNodeX,
                    oldY: this.originalNodeY
                }
            });

            // Always save the new position
            this.syncNodePositionToRawData(this.draggedNode);
            this.saveTree();

            this.isDragging = false;
            this.draggedNode = null;
            CanvasRenderer.canvas.style.cursor = 'grab';
            CanvasRenderer._needsRender = true;
            CanvasRenderer.buildSpatialIndex();
        } else {
            this._originalOnMouseUp(e);
        }
    },

    // =========================================================================
    // PEN TOOL - Draw prerequisites
    // =========================================================================

    handlePenMouseDown: function(e, world) {
        var node = CanvasRenderer.findNodeAt(world.x, world.y);

        if (node && e.button === 0) {
            // Start drawing prerequisite line from this node
            this.isPenDrawing = true;
            this.penStartNode = node;
            this.penCurrentX = world.x;
            this.penCurrentY = world.y;
            console.log('[EditMode] Pen started from:', node.name || node.id);
        } else if (e.button === 0) {
            // Clicked on empty space - find nearest grid position and show spawn menu
            console.log('[EditMode] Pen clicked empty space at:', world.x, world.y);
            console.log('[EditMode] Grid positions count:', this.gridPositions.length);

            // Try to find nearest grid position (empty preferred, but allow any)
            var gridPos = this.findNearestGridPosition(world.x, world.y);
            if (gridPos) {
                console.log('[EditMode] Found grid position:', gridPos);
                this.showSpawnMenu(gridPos);
            } else {
                // Fallback: use click position directly
                console.log('[EditMode] No grid position found, using click position');
                this.showSpawnMenu({
                    x: world.x,
                    y: world.y,
                    school: 'Unknown',
                    tier: 0
                });
            }
        }
    },

    handlePenMouseMove: function(e, world) {
        if (this.isPenDrawing) {
            this.penCurrentX = world.x;
            this.penCurrentY = world.y;
            CanvasRenderer._needsRender = true;
        }
    },

    handlePenMouseUp: function(e, world) {
        if (this.isPenDrawing && this.penStartNode) {
            var endNode = CanvasRenderer.findNodeAt(world.x, world.y);

            if (endNode && endNode !== this.penStartNode) {
                // Create prerequisite: penStartNode is prereq of endNode
                this.addPrerequisite(this.penStartNode, endNode);
            } else {
                console.log('[EditMode] Pen cancelled - no target node');
            }

            this.isPenDrawing = false;
            this.penStartNode = null;
            CanvasRenderer._needsRender = true;
        }
    },

    /**
     * Add a prerequisite relationship
     */
    addPrerequisite: function(fromNode, toNode) {
        var fromId = fromNode.formId || fromNode.id;
        var toId = toNode.formId || toNode.id;

        // Drawn and Required, once (PrereqEdit.add refuses one that is already there)
        if (!PrereqEdit.add(toNode, fromId, false)) {
            console.log('[EditMode] Prerequisite already exists');
            return false;
        }
        this._linkChild(fromId, toId);

        console.log('[EditMode] Added prerequisite:', fromNode.name || fromId, '->', toNode.name || toId);

        this.pushUndo({
            type: 'addPrereq',
            data: {
                fromId: fromId,
                toId: toId
            }
        });

        this._afterPrereqEdit(toNode);
        return true;
    },

    // =========================================================================
    // ERASER TOOL - Remove edges
    // =========================================================================

    handleEraserMouseDown: function(e, world) {
        if (e.button === 0) {
            // Check if clicking on a node - delete it (shift+click also blacklists)
            var node = CanvasRenderer.findNodeAt(world.x, world.y);
            if (node) {
                // A school's root is refused before Shift blacklists it (deleteNode says why)
                if (this._isSchoolRoot(node)) {
                    this.deleteNode(node);
                    return;
                }
                if (e.shiftKey && typeof addToBlacklist === 'function') {
                    addToBlacklist({
                        formId: node.formId || node.id,
                        name: node.name || node.formId || node.id,
                        school: node.school || 'Unknown'
                    });
                    console.log('[EditMode] Shift+Eraser: blacklisted + deleted', node.name || node.formId);
                }
                this.deleteNode(node);
                return;
            }

            // Otherwise start erasing edges
            this.isErasing = true;
            this.eraserPath = [{ x: world.x, y: world.y }];
        }
    },

    handleEraserMouseMove: function(e, world) {
        if (this.isErasing) {
            this.eraserPath.push({ x: world.x, y: world.y });
            CanvasRenderer._needsRender = true;
        }
    },

    handleEraserMouseUp: function(e, world) {
        if (this.isErasing) {
            // Check which edges intersect with eraser path
            var edgesToRemove = this.findIntersectingEdges();

            if (edgesToRemove.length > 0) {
                console.log('[EditMode] Erasing', edgesToRemove.length, 'edges');
                for (var i = 0; i < edgesToRemove.length; i++) {
                    this.removeEdge(edgesToRemove[i]);
                }
                this.syncPrerequisitesToRawData();
                this.saveTree();
            }

            this.isErasing = false;
            this.eraserPath = [];
            CanvasRenderer._needsRender = true;
        }
    },

    /**
     * Find edges that intersect with the eraser path
     */
    findIntersectingEdges: function() {
        var intersecting = [];

        if (!CanvasRenderer.edges || this.eraserPath.length < 2) return intersecting;

        for (var i = 0; i < CanvasRenderer.edges.length; i++) {
            var edge = CanvasRenderer.edges[i];
            var fromNode = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(edge.from) : null;
            var toNode = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(edge.to) : null;

            if (!fromNode || !toNode) continue;

            // Check if any eraser path segment intersects this edge
            for (var j = 0; j < this.eraserPath.length - 1; j++) {
                var p1 = this.eraserPath[j];
                var p2 = this.eraserPath[j + 1];

                if (this.lineSegmentsIntersect(
                    p1.x, p1.y, p2.x, p2.y,
                    fromNode.x, fromNode.y, toNode.x, toNode.y
                )) {
                    intersecting.push(edge);
                    break; // Only add edge once
                }
            }
        }

        return intersecting;
    },

    /**
     * Check if two line segments intersect
     */
    lineSegmentsIntersect: function(x1, y1, x2, y2, x3, y3, x4, y4) {
        var denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
        if (Math.abs(denom) < 0.0001) return false; // Parallel

        var t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
        var u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;

        return t >= 0 && t <= 1 && u >= 0 && u <= 1;
    },

    /**
     * Remove an edge/prerequisite
     */
    removeEdge: function(edge, skipUndo) {
        var fromId = edge.from;
        var toId = edge.to;
        var toNode = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(toId) : null;

        // Out of every list; what it was goes on the undo stack
        var was = toNode ? PrereqEdit.remove(toNode, fromId) : { type: null, linked: true, softNeeded: 0 };
        if (!skipUndo) {
            this.pushUndo({
                type: 'removePrereq',
                data: {
                    fromId: fromId,
                    toId: toId,
                    wasHard: was.type === 'hard',
                    was: was
                }
            });
        }
        this._unlinkChild(fromId, toId);

        console.log('[EditMode] Removed edge:', fromId, '->', toId);

        // Refresh details panel if toNode is selected
        if (toNode && state.selectedNode && (state.selectedNode.formId === toNode.formId || state.selectedNode.id === toNode.id)) {
            if (typeof showSpellDetails === 'function') {
                showSpellDetails(toNode);
            }
        }
    },

    // =========================================================================
    // RENDERING (called from CanvasRenderer)
    // =========================================================================

    /**
     * Render edit mode overlays (pen line, eraser path)
     * Call this from CanvasRenderer.render() when edit mode is active
     */
    renderOverlay: function(ctx) {
        if (!this.isActive) return;

        // Render pen line being drawn (with arrow showing direction)
        if (this.isPenDrawing && this.penStartNode) {
            var startX = this.penStartNode.x;
            var startY = this.penStartNode.y;
            var endX = this.penCurrentX;
            var endY = this.penCurrentY;

            // Draw the dashed line
            ctx.beginPath();
            ctx.moveTo(startX, startY);
            ctx.lineTo(endX, endY);
            ctx.strokeStyle = '#00ff00';
            ctx.lineWidth = 3;
            ctx.setLineDash([5, 5]);
            ctx.stroke();
            ctx.setLineDash([]);

            // Draw arrowhead at the end
            var angle = Math.atan2(endY - startY, endX - startX);
            var arrowLength = 15;
            var arrowAngle = Math.PI / 6; // 30 degrees

            ctx.beginPath();
            ctx.moveTo(endX, endY);
            ctx.lineTo(
                endX - arrowLength * Math.cos(angle - arrowAngle),
                endY - arrowLength * Math.sin(angle - arrowAngle)
            );
            ctx.moveTo(endX, endY);
            ctx.lineTo(
                endX - arrowLength * Math.cos(angle + arrowAngle),
                endY - arrowLength * Math.sin(angle + arrowAngle)
            );
            ctx.strokeStyle = '#00ff00';
            ctx.lineWidth = 3;
            ctx.stroke();
        }

        // Render eraser path
        if (this.isErasing && this.eraserPath.length > 1) {
            ctx.beginPath();
            ctx.moveTo(this.eraserPath[0].x, this.eraserPath[0].y);
            for (var i = 1; i < this.eraserPath.length; i++) {
                ctx.lineTo(this.eraserPath[i].x, this.eraserPath[i].y);
            }
            ctx.strokeStyle = '#ff0000';
            ctx.lineWidth = 4;
            ctx.globalAlpha = 0.7;
            ctx.stroke();
            ctx.globalAlpha = 1.0;
        }
    },

    // =========================================================================
    // SAVE/SYNC
    // =========================================================================

    /**
     * Sync a node's position back to rawData
     */
    syncNodePositionToRawData: function(node) {
        if (!state.treeData || !state.treeData.rawData) return false;

        var rawData = state.treeData.rawData;
        var nodeId = node.formId || node.id;
        var schoolName = node.school;

        if (rawData.schools && rawData.schools[schoolName] && rawData.schools[schoolName].nodes) {
            var rawNodes = rawData.schools[schoolName].nodes;
            for (var i = 0; i < rawNodes.length; i++) {
                var rawNode = rawNodes[i];
                var rawId = rawNode.formId || rawNode.id || rawNode.spellId;
                if (rawId === nodeId) {
                    rawNode.x = node.x;
                    rawNode.y = node.y;
                    console.log('[EditMode] Synced position for', node.name || nodeId);
                    return true;
                }
            }
        }
        return false;
    },

    /**
     * Sync globe position to rawData for serialization
     */
    syncGlobePositionToRawData: function() {
        if (!state.treeData || !state.treeData.rawData) return;
        var globe = state.treeData.globe;
        if (!globe) return;
        state.treeData.rawData.globe = {
            x: globe.x,
            y: globe.y,
            radius: globe.radius
        };
        console.log('[EditMode] Synced globe position to rawData:', globe.x, globe.y);
    },

    /**
     * Sync all prerequisites back to rawData
     */
    syncPrerequisitesToRawData: function() {
        if (!state.treeData || !state.treeData.rawData || !CanvasRenderer.nodes) return;

        var rawData = state.treeData.rawData;

        // Build maps of nodeId -> various prereq data
        var prereqMap = {};
        var hardPrereqMap = {};
        var softPrereqMap = {};
        var softNeededMap = {};
        var childrenMap = {};

        for (var i = 0; i < CanvasRenderer.nodes.length; i++) {
            var node = CanvasRenderer.nodes[i];
            var nodeId = node.formId || node.id;
            // Copies: rawData never shares an array with a node or another list
            prereqMap[nodeId] = (node.prerequisites || []).slice();
            hardPrereqMap[nodeId] = (node.hardPrereqs || []).slice();
            softPrereqMap[nodeId] = (node.softPrereqs || []).slice();
            softNeededMap[nodeId] = node.softNeeded || 0;
            childrenMap[nodeId] = (node.children || []).slice();
        }

        // Update rawData
        for (var schoolName in rawData.schools) {
            var school = rawData.schools[schoolName];
            if (!school.nodes) continue;

            for (var i = 0; i < school.nodes.length; i++) {
                var rawNode = school.nodes[i];
                var rawId = rawNode.formId || rawNode.id || rawNode.spellId;

                if (prereqMap[rawId] !== undefined) {
                    rawNode.prerequisites = prereqMap[rawId];
                }
                if (hardPrereqMap[rawId] !== undefined) {
                    rawNode.hardPrereqs = hardPrereqMap[rawId];
                }
                if (softPrereqMap[rawId] !== undefined) {
                    rawNode.softPrereqs = softPrereqMap[rawId];
                }
                if (softNeededMap[rawId] !== undefined) {
                    rawNode.softNeeded = softNeededMap[rawId];
                }
                if (childrenMap[rawId] !== undefined) {
                    rawNode.children = childrenMap[rawId];
                }
            }
        }

        console.log('[EditMode] Synced prerequisites to rawData');
    },

    /**
     * Save tree to file
     */
    saveTree: function() {
        if (typeof saveTreeToFile === 'function') {
            var saved = saveTreeToFile();
            if (saved) {
                console.log('[EditMode] Tree saved');
                if (typeof setTreeStatus === 'function') {
                    setTreeStatusKey('status.changesSaved', null, 'Changes saved');
                }
            }
            return saved;
        }
        return false;
    },

    // =========================================================================
    // PREREQUISITE EDITING (hard/soft toggle, softNeeded count)
    // =========================================================================

    /**
     * Toggle a prerequisite between hard and soft
     * @param {Object} node - The node that has this prerequisite
     * @param {string} prereqId - The ID of the prerequisite to toggle
     * @returns {string} - The new type ('hard' or 'soft')
     */
    togglePrereqType: function(node, prereqId) {
        if (!node) return null;

        // Moves it between the lists, never into both (PrereqEdit.toggle)
        var r = PrereqEdit.toggle(node, prereqId);
        console.log('[EditMode] Changed prereq', prereqId, 'to', r.type.toUpperCase());

        this.pushUndo({
            type: 'togglePrereqType',
            data: {
                nodeId: node.formId || node.id,
                prereqId: prereqId,
                wasHard: r.wasType === 'hard',
                wasType: r.wasType,
                softNeeded: r.softNeeded
            }
        });

        this._afterPrereqEdit(node);
        return r.type;
    },

    /**
     * Update the softNeeded count for a node
     * @param {Object} node - The node to update
     * @param {number} count - The new softNeeded value
     */
    updateSoftNeeded: function(node, count) {
        if (!node) return;

        var oldValue = node.softNeeded || 0;
        var maxSoft = (node.softPrereqs || []).length;
        node.softNeeded = Math.max(0, Math.min(count, maxSoft));

        // Only push undo if value actually changed
        if (node.softNeeded !== oldValue) {
            this.pushUndo({
                type: 'updateSoftNeeded',
                data: {
                    nodeId: node.formId || node.id,
                    oldValue: oldValue
                }
            });
        }

        console.log('[EditMode] Updated softNeeded to', node.softNeeded, 'for', node.name || node.formId);

        // Sync and save
        this.syncPrerequisitesToRawData();
        this.saveTree();

        // Refresh details panel
        if (state.selectedNode && (state.selectedNode.formId === node.formId || state.selectedNode.id === node.id)) {
            if (typeof showSpellDetails === 'function') {
                showSpellDetails(node);
            }
        }
    },

    /**
     * Delete a specific prerequisite from a node
     * @param {Object} node - The node that has this prerequisite
     * @param {string} prereqId - The ID of the prerequisite to delete
     */
    deletePrerequisite: function(node, prereqId) {
        if (!node) return;

        var nodeId = node.formId || node.id;

        // Out of every list (it is in one of hard and soft); what it was goes on the undo stack
        var was = PrereqEdit.remove(node, prereqId);
        this.pushUndo({
            type: 'removePrereq',
            data: {
                fromId: prereqId,
                toId: nodeId,
                wasHard: was.type === 'hard',
                was: was
            }
        });
        this._unlinkChild(prereqId, nodeId);

        console.log('[EditMode] Deleted prereq', prereqId, 'from', node.name || nodeId);

        this._afterPrereqEdit(node);
    },

    // =========================================================================
    // NODE DELETION (Eraser click on node)
    // =========================================================================

    /**
     * Delete a node and all its connections
     */
    deleteNode: function(node) {
        if (!node) return false;

        var nodeId = node.formId || node.id;

        // A school's root stays: the saved tree's `root` names it, its children would be left
        // without a prerequisite, and a reload would make another spell the root (treeLoad,
        // canvasRendererData and the C++ tree check fall back to the school's first node)
        if (this._isSchoolRoot(node)) {
            var label = node.name || nodeId;
            console.log('[EditMode] Not deleting the school root:', label);
            if (typeof setTreeStatus === 'function') {
                setTreeStatusKey('editMode.rootNotDeleted', { name: label }, 'Not deleted: {{name}} is the root of its school');
            }
            return false;
        }
        console.log('[EditMode] Deleting node:', node.name || nodeId);

        // Every other spell that names it - a child, a parent, a bridge target
        // (soft, not a child), a PreReq Master lock (hard) - lets it go, each list
        // as a new array: the saved tree has its own arrays, so the saved tree is
        // cleaned below (forgetInTree) and from these lists (sync), not by sharing
        (CanvasRenderer.nodes || []).forEach(function(other) {
            if (other !== node) PrereqEdit.forget(other, nodeId);
        });

        // Remove all edges involving this node
        if (CanvasRenderer.edges) {
            for (var i = CanvasRenderer.edges.length - 1; i >= 0; i--) {
                var e = CanvasRenderer.edges[i];
                if (e.from === nodeId || e.to === nodeId) {
                    CanvasRenderer.edges.splice(i, 1);
                }
            }
        }

        // Remove from CanvasRenderer.nodes array
        if (CanvasRenderer.nodes) {
            var idx = CanvasRenderer.nodes.indexOf(node);
            if (idx !== -1) CanvasRenderer.nodes.splice(idx, 1);
        }

        // Remove from nodeMap
        if (CanvasRenderer._nodeMap) {
            CanvasRenderer._nodeMap.delete(nodeId);
        }

        // Out of rawData with every reference to it, then the other spells' new lists in
        this.removeNodeFromRawData(node);
        if (state.treeData && state.treeData.rawData) PrereqEdit.forgetInTree(state.treeData.rawData, nodeId);
        this.syncPrerequisitesToRawData();

        // Undo entries that name it go: an undo would link a spell to one that is gone
        this.undoStack = this.undoStack.filter(function(a) {
            var d = a.data || {};
            return d.nodeId !== nodeId && d.fromId !== nodeId && d.toId !== nodeId && d.prereqId !== nodeId;
        });

        // Clear selection if deleted node was selected
        if (state.selectedNode && (state.selectedNode.formId === nodeId || state.selectedNode.id === nodeId)) {
            state.selectedNode = null;
        }

        // Save and refresh
        this.saveTree();
        CanvasRenderer.buildSpatialIndex();
        CanvasRenderer._needsRender = true;

        if (typeof setTreeStatus === 'function') {
            setTreeStatusKey('editMode.deleted', { name: node.name || nodeId }, 'Deleted: {{name}}');
        }
        return true;
    },

    /** The node is its school's root (`isRoot`, or the saved tree's `schools[school].root`) */
    _isSchoolRoot: function(node) {
        if (!node) return false;
        if (node.isRoot) return true;
        var raw = state.treeData && state.treeData.rawData;
        var school = raw && raw.schools && raw.schools[node.school];
        return !!(school && school.root && school.root === (node.formId || node.id));
    },

    /**
     * Remove a node from rawData
     */
    removeNodeFromRawData: function(node) {
        if (!state.treeData || !state.treeData.rawData) return;

        var rawData = state.treeData.rawData;
        var nodeId = node.formId || node.id;
        var schoolName = node.school;

        if (rawData.schools && rawData.schools[schoolName] && rawData.schools[schoolName].nodes) {
            var nodes = rawData.schools[schoolName].nodes;
            for (var i = nodes.length - 1; i >= 0; i--) {
                var rawNode = nodes[i];
                var rawId = rawNode.formId || rawNode.id || rawNode.spellId;
                if (rawId === nodeId) {
                    nodes.splice(i, 1);
                    console.log('[EditMode] Removed node from rawData:', nodeId);
                    break;
                }
            }
        }

        // Also remove from state.treeData.nodes if it exists
        if (state.treeData.nodes) {
            for (var i = state.treeData.nodes.length - 1; i >= 0; i--) {
                var n = state.treeData.nodes[i];
                if ((n.formId || n.id) === nodeId) {
                    state.treeData.nodes.splice(i, 1);
                    // Node lookups and the card's shared-name counts are made again at next use
                    if (typeof invalidateNodeLookup === 'function') invalidateNodeLookup();
                    if (typeof SpellNames !== 'undefined') SpellNames.invalidate();
                    break;
                }
            }
        }
    },

    // =========================================================================
    // SPELL SPAWN MENU
    // =========================================================================

    // Spawn menu state
    spawnPosition: null,
    allSpells: [],
    spellsLoaded: false,

    /**
     * Show the spawn spell menu at a grid position
     */
    showSpawnMenu: function(gridPos) {
        console.log('[EditMode] showSpawnMenu called with:', gridPos);
        this.spawnPosition = gridPos;

        var modal = document.getElementById('spawn-spell-modal');
        var searchInput = document.getElementById('spawn-search');
        var spellList = document.getElementById('spawn-spell-list');

        console.log('[EditMode] Modal element:', modal);
        if (!modal) {
            console.error('[EditMode] spawn-spell-modal not found!');
            return;
        }

        // Show modal
        modal.classList.remove('hidden');
        console.log('[EditMode] Modal shown');

        // Focus search input
        if (searchInput) {
            searchInput.value = '';
            searchInput.focus();
        }

        // Load spells if not loaded
        if (!this.spellsLoaded) {
            this.loadAllSpells();
        } else {
            this.renderSpellList('');
        }

        // Set up event listeners
        this.setupSpawnMenuListeners();
    },

    /**
     * Hide the spawn spell menu
     */
    hideSpawnMenu: function() {
        var modal = document.getElementById('spawn-spell-modal');
        if (modal) {
            modal.classList.add('hidden');
        }
        this.spawnPosition = null;
    },

    /**
     * Load all available spells from every source we can find
     */
    loadAllSpells: function() {
        var self = this;
        var spellList = document.getElementById('spawn-spell-list');

        if (spellList) {
            spellList.innerHTML = '<div class="spawn-loading"></div>';
            setI18n(spellList.firstChild, 'modals.spawnSpell.loading', null, 'Loading spells...');
        }

        this.allSpells = [];
        var seenIds = {};

        // Helper to resolve a spell name from all available sources
        function resolveName(formId, rawName) {
            // Use provided name if valid
            if (rawName && rawName !== 'null' && rawName !== 'undefined') return rawName;
            // Try SpellCache
            if (typeof SpellCache !== 'undefined') {
                var cached = SpellCache.get(formId);
                if (cached && cached.name) return cached.name;
            }
            // Try CanvasRenderer node map
            if (CanvasRenderer._nodeMap) {
                var node = CanvasRenderer._nodeMap.get(formId);
                if (node && node.name) return node.name;
            }
            // Fallback to formId
            return formId;
        }

        function addSpell(formId, name, school, level) {
            if (!formId || seenIds[formId]) return;
            seenIds[formId] = true;
            self.allSpells.push({
                formId: formId,
                name: resolveName(formId, name),
                school: school || 'Unknown',
                level: level || 'Unknown'
            });
        }

        // Ensure updateSpellData hook is installed
        if (typeof _installUpdateSpellDataHook === 'function') {
            _installUpdateSpellDataHook();
        }

        // Source 1: Scanned spell data (state.lastSpellData from Spell Scan tab)
        // This has the most complete data with real spell names
        if (state.lastSpellData && state.lastSpellData.spells && state.lastSpellData.spells.length > 0) {
            console.log('[EditMode] Source: lastSpellData -', state.lastSpellData.spells.length, 'spells');
            state.lastSpellData.spells.forEach(function(spell) {
                // Another plugin's copy of a spell (duplicateOf, SpellScannerDuplicates.cpp)
                // is not offered: spawning it would put one spell on the tree twice. A copy
                // already on the tree still comes in from the sources below.
                if (spell.duplicateOf) return;
                addSpell(
                    spell.formId || spell.id,
                    spell.name || spell.spellName,
                    spell.school,
                    spell.skillLevel || spell.level
                );
            });
        }

        // Source 2: Live CanvasRenderer nodes (have resolved names from SpellCache)
        if (CanvasRenderer.nodes && CanvasRenderer.nodes.length > 0) {
            console.log('[EditMode] Source: CanvasRenderer -', CanvasRenderer.nodes.length, 'nodes');
            CanvasRenderer.nodes.forEach(function(node) {
                addSpell(
                    node.formId || node.id,
                    node.name,
                    node.school,
                    node.level
                );
            });
        }

        // Source 3: Tree rawData (spells in the tree JSON - may lack names)
        if (state.treeData && state.treeData.rawData && state.treeData.rawData.schools) {
            var rawSchools = state.treeData.rawData.schools;
            for (var schoolName in rawSchools) {
                var school = rawSchools[schoolName];
                if (school.nodes) {
                    school.nodes.forEach(function(node) {
                        addSpell(
                            node.formId || node.id || node.spellId,
                            node.name || node.spellName,
                            schoolName,
                            node.level || node.skillLevel
                        );
                    });
                }
            }
        }

        console.log('[EditMode] Total spells loaded:', this.allSpells.length);

        if (this.allSpells.length > 0) {
            this.spellsLoaded = true;
            this.renderSpellList('');
        } else {
            // No data from any source - try requesting a scan from C++
            console.log('[EditMode] No spell data from any source, requesting scan...');
            if (spellList) {
                spellList.innerHTML = '<div class="spawn-loading"></div>';
                setI18n(spellList.firstChild, 'modals.spawnSpell.scanning', null, 'Scanning spells... (open the Spell Scan tab first if this stays)');
            }
            this.spellsLoaded = false;
            if (window.callCpp) {
                // Its result must not feed the build at game start (autoBuild.js)
                if (typeof AutoBuild !== 'undefined') AutoBuild.yieldTo('edit mode scan');
                window.callCpp('ScanSpells', JSON.stringify({
                    fields: state.fields || {},
                    scanMode: 'all'
                }));
            }
        }
    },

    /**
     * Render the spell list with optional search filter
     */
    renderSpellList: function(searchTerm) {
        var spellList = document.getElementById('spawn-spell-list');
        if (!spellList) return;

        var self = this;
        searchTerm = (searchTerm || '').toLowerCase();

        // Build set of existing node formIds
        var existingIds = new Set();
        if (CanvasRenderer.nodes) {
            CanvasRenderer.nodes.forEach(function(n) {
                existingIds.add(n.formId || n.id);
            });
        }

        // Filter spells by search term (show ALL spells, including ones on tree)
        var filtered = this.allSpells.filter(function(spell) {
            if (searchTerm) {
                var nameMatch = (spell.name || '').toLowerCase().indexOf(searchTerm) !== -1;
                var schoolMatch = (spell.school || '').toLowerCase().indexOf(searchTerm) !== -1;
                var idMatch = (spell.formId || '').toLowerCase().indexOf(searchTerm) !== -1;
                return nameMatch || schoolMatch || idMatch;
            }
            return true;
        });

        // Sort: new spells first, then existing ones
        filtered.sort(function(a, b) {
            var aExists = existingIds.has(a.formId) ? 1 : 0;
            var bExists = existingIds.has(b.formId) ? 1 : 0;
            if (aExists !== bExists) return aExists - bExists;
            return (a.name || '').localeCompare(b.name || '');
        });

        // Limit to top 50 (increased since we show existing too)
        filtered = filtered.slice(0, 50);

        // Render
        if (filtered.length === 0) {
            spellList.innerHTML = '<div class="spawn-no-results"></div>';
            setI18n(spellList.firstChild, 'search.noMatchingSpells', null, 'No matching spells');
            return;
        }

        spellList.innerHTML = '';
        filtered.forEach(function(spell) {
            var isOnTree = existingIds.has(spell.formId);
            var item = document.createElement('div');
            item.className = 'spawn-spell-item' + (isOnTree ? ' on-tree' : '');
            item.dataset.formId = spell.formId;

            var badge = isOnTree ? '<span class="spawn-on-tree-badge">' +
                escapeHtml(tOr('modals.spawnSpell.onTree', null, 'On tree')) + '</span>' : '';
            item.innerHTML =
                '<div class="spawn-spell-name">' + escapeHtml(spell.name || spell.formId) + badge + '</div>' +
                '<div class="spawn-spell-info">' +
                    '<span>' + escapeHtml(schoolText(spell.school)) + '</span>' +
                    '<span>' + escapeHtml(levelText(spell.level)) + '</span>' +
                '</div>';

            item.addEventListener('click', function() {
                self.spawnSpell(spell);
            });

            spellList.appendChild(item);
        });
    },

    /**
     * Pan the canvas view to center on a spell node
     */
    panToSpell: function(formId) {
        var node = CanvasRenderer._nodeMap ? CanvasRenderer._nodeMap.get(formId) : null;
        if (!node) {
            console.log('[EditMode] Node not found for panTo:', formId);
            return;
        }

        // Center the view on this node (rotation-aware when TreeCamera is available)
        if (typeof TreeCamera !== 'undefined') {
            TreeCamera.focusNode(node, { rotate: false, zoom: CanvasRenderer.zoom, instant: true });
        } else {
            CanvasRenderer.panX = -node.x * CanvasRenderer.zoom;
            CanvasRenderer.panY = -node.y * CanvasRenderer.zoom;
            CanvasRenderer._needsRender = true;
        }

        // Select the node
        state.selectedNode = node;
        if (typeof showSpellDetails === 'function') {
            showSpellDetails(node);
        }

        console.log('[EditMode] Panned to:', node.name || formId);

        if (typeof setTreeStatus === 'function') {
            setTreeStatusKey('editMode.focused', { name: node.name || formId }, 'Focused: {{name}}');
        }

        this.hideSpawnMenu();
    },

    /**
     * Set up spawn menu event listeners
     */
    setupSpawnMenuListeners: function() {
        var self = this;
        var modal = document.getElementById('spawn-spell-modal');
        var searchInput = document.getElementById('spawn-search');
        var closeBtn = document.getElementById('spawn-modal-close');
        var cancelBtn = document.getElementById('spawn-cancel');
        var backdrop = modal ? modal.querySelector('.modal-backdrop') : null;

        // Remove old listeners by cloning elements (simple approach)
        if (searchInput) {
            var newSearch = searchInput.cloneNode(true);
            searchInput.parentNode.replaceChild(newSearch, searchInput);
            searchInput = newSearch;

            searchInput.addEventListener('input', function() {
                self.renderSpellList(this.value);
            });
        }

        if (closeBtn) {
            closeBtn.onclick = function() { self.hideSpawnMenu(); };
        }
        if (cancelBtn) {
            cancelBtn.onclick = function() { self.hideSpawnMenu(); };
        }
        if (backdrop) {
            backdrop.onclick = function() { self.hideSpawnMenu(); };
        }
    },

    /**
     * Spawn a spell at the selected grid position
     */
    spawnSpell: function(spell) {
        if (!this.spawnPosition || !spell) {
            this.hideSpawnMenu();
            return;
        }

        var pos = this.spawnPosition;
        console.log('[EditMode] Spawning spell:', spell.name, 'at', Math.round(pos.x), Math.round(pos.y));

        // Generate unique ID if this formId already exists on the tree
        var nodeId = spell.formId;
        var isDuplicate = false;
        if (CanvasRenderer._nodeMap && CanvasRenderer._nodeMap.has(nodeId)) {
            nodeId = spell.formId + '_dup_' + Date.now();
            isDuplicate = true;
            console.log('[EditMode] Duplicate spawn, using unique ID:', nodeId);
        }

        // Get the state from the original node if this is a duplicate
        var inheritedState = 'available';
        if (isDuplicate) {
            var originalNode = CanvasRenderer._nodeMap.get(spell.formId);
            if (originalNode) {
                inheritedState = originalNode.state || 'available';
            }
        }

        // Create new node
        var newNode = {
            id: nodeId,
            formId: nodeId,
            originalFormId: isDuplicate ? spell.formId : null,  // Track original for shared state
            name: spell.name,
            school: spell.school || pos.school || 'Unknown',
            level: spell.level || 'Novice',
            x: pos.x,
            y: pos.y,
            tier: pos.tier || 0,
            state: inheritedState,
            prerequisites: [],
            hardPrereqs: [],
            softPrereqs: [],
            softNeeded: 0,
            children: [],
            isRoot: false
        };

        // Add to CanvasRenderer
        if (CanvasRenderer.nodes) {
            CanvasRenderer.nodes.push(newNode);
        }
        if (CanvasRenderer._nodeMap) {
            CanvasRenderer._nodeMap.set(newNode.formId, newNode);
        }

        // Add to rawData
        this.addNodeToRawData(newNode);

        // Add to state.treeData.nodes
        if (state.treeData && state.treeData.nodes) {
            state.treeData.nodes.push(newNode);
            // Node lookups and the card's shared-name counts are made again at next use
            if (typeof invalidateNodeLookup === 'function') invalidateNodeLookup();
            if (typeof SpellNames !== 'undefined') SpellNames.invalidate();
        }

        // Save and refresh
        this.saveTree();
        CanvasRenderer.buildSpatialIndex();
        CanvasRenderer._needsRender = true;

        if (typeof setTreeStatus === 'function') {
            setTreeStatusKey('editMode.spawned', { name: spell.name }, 'Added: {{name}}');
        }

        this.hideSpawnMenu();
    },

    /**
     * Add a node to rawData
     */
    addNodeToRawData: function(node) {
        if (!state.treeData || !state.treeData.rawData) return;

        var rawData = state.treeData.rawData;
        var schoolName = node.school;

        // Ensure school exists
        if (!rawData.schools) rawData.schools = {};
        if (!rawData.schools[schoolName]) {
            rawData.schools[schoolName] = { nodes: [] };
        }
        if (!rawData.schools[schoolName].nodes) {
            rawData.schools[schoolName].nodes = [];
        }

        // Add node
        rawData.schools[schoolName].nodes.push({
            formId: node.formId,
            id: node.id,
            name: node.name,
            school: node.school,
            level: node.level,
            x: node.x,
            y: node.y,
            tier: node.tier,
            // Copies: rawData never shares an array with a node
            prerequisites: (node.prerequisites || []).slice(),
            hardPrereqs: (node.hardPrereqs || []).slice(),
            softPrereqs: (node.softPrereqs || []).slice(),
            softNeeded: node.softNeeded || 0,
            children: (node.children || []).slice()
        });

        console.log('[EditMode] Added node to rawData:', node.formId);
    }
};

// Hook into updateSpellData AFTER all scripts have loaded
// (editMode.js loads before cppCallbacks.js, so we defer the hook)
function _installUpdateSpellDataHook() {
    if (window._editModeHookInstalled) return;

    var originalUpdateSpellData = window.updateSpellData;
    if (originalUpdateSpellData) {
        window._editModeHookInstalled = true;
        window.updateSpellData = function(jsonStr) {
            // Call original function first
            originalUpdateSpellData.call(this, jsonStr);

            // Also update EditMode spell list
            try {
                var data = JSON.parse(jsonStr);
                if (data.spells && Array.isArray(data.spells)) {
                    console.log('[EditMode] Intercepted spell scan data:', data.spells.length, 'spells');
                    EditMode._processSpellData(data.spells);
                }
            } catch (e) {
                // Ignore parse errors - original handler will deal with them
            }
        };
        console.log('[EditMode] updateSpellData hook installed');
    }
}

// Defer hook installation until DOM is ready and all scripts have loaded
if (document.readyState === 'complete') {
    setTimeout(_installUpdateSpellDataHook, 0);
} else {
    window.addEventListener('load', _installUpdateSpellDataHook);
}

// Helper to process spell data from any source
EditMode._processSpellData = function(spells) {
    if (!spells || !Array.isArray(spells)) return;

    // Merge with existing spells (avoid duplicates)
    var existingIds = new Set(this.allSpells.map(function(s) { return s.formId; }));
    var self = this;
    var addedCount = 0;

    spells.forEach(function(spell) {
        var id = spell.formId || spell.id;
        if (id && !existingIds.has(id)) {
            self.allSpells.push({
                formId: id,
                name: spell.name || spell.spellName || 'Unknown',
                school: spell.school || 'Unknown',
                level: spell.skillLevel || spell.level || 'Unknown'
            });
            existingIds.add(id);
            addedCount++;
        }
    });

    console.log('[EditMode] Added', addedCount, 'new spells, total:', this.allSpells.length);

    // Mark as loaded now that we have real data
    if (this.allSpells.length > 0) {
        this.spellsLoaded = true;
    }

    // Re-render if spawn menu is open
    var modal = document.getElementById('spawn-spell-modal');
    if (modal && !modal.classList.contains('hidden')) {
        var searchInput = document.getElementById('spawn-search');
        this.renderSpellList(searchInput ? searchInput.value : '');
    }
};

// Initialize on DOM ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        EditMode.init();
    });
} else {
    setTimeout(function() {
        EditMode.init();
    }, 0);
}
