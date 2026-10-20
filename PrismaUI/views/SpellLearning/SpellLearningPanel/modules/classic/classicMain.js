/**
 * TreeGrowthClassic — Classic Growth Mode Orchestrator
 *
 * Wires together ClassicSettings, ClassicLayout, and ClassicRenderer to
 * provide the "CLASSIC" tab in the Tree Growth section. Manages tree build
 * state, delegates rendering to ClassicRenderer, and communicates with the
 * C++ backend via window.callCpp for tree building and saving.
 *
 * Self-registers with TreeGrowth via registerMode().
 *
 * Depends on:
 *   classicSettings.js  (ClassicSettings)
 *   classicLayout.js    (ClassicLayout)
 *   classicRenderer.js  (ClassicRenderer)
 *   classicBuildRequest.js (ClassicBuildRequest - the build request to C++)
 *   classicTreeOutput.js   (ClassicTreeOutput - the tree JSON to save)
 *   treePreviewUtils.js (TreePreviewUtils)
 *   treePreview.js      (TreePreview.getOutput)
 *   treeGrowth.js       (TreeGrowth — registers into it)
 */

var TreeGrowthClassic = {

    // =========================================================================
    // STATE
    // =========================================================================

    settings: {
        ghostOpacity: 35,
        nodeRadius: 5,
        spread: 50,
        radialBias: 50,
        centerMask: 3,
        spellMatching: 'layered',
        dynamicGridExpansion: true,  // Expand grid when schools run out of space
        tierZones: {
            Novice:     { min: 0,  max: 40 },
            Apprentice: { min: 10, max: 55 },
            Adept:      { min: 30, max: 75 },
            Expert:     { min: 50, max: 90 },
            Master:     { min: 65, max: 100 }
        }
    },

    _treeData: null,
    _layoutData: null,
    _hasSpells: false,

    // =========================================================================
    // TREE GROWTH MODE INTERFACE (required by TreeGrowth orchestrator)
    // =========================================================================

    /**
     * Build the settings panel HTML for Classic mode.
     * @returns {string} HTML string
     */
    buildSettingsHTML: function () {
        return ClassicSettings.buildHTML(this.settings);
    },

    /**
     * Bind DOM events for the settings panel.
     * Connects ClassicSettings callbacks to internal methods.
     */
    bindEvents: function () {
        var self = this;

        ClassicSettings.bindEvents({
            onBuild: function () {
                self.buildTree();
            },
            onApply: function () {
                self.applyTree();
            },
            onClear: function () {
                self.clearTree();
            },
            onSettingChanged: function (key, value) {
                self.settings[key] = value;
                // Layout-affecting settings: invalidate cached layout so it recomputes
                if (key === 'spread' || key === 'radialBias' || key === 'centerMask' || key === 'spellMatching') {
                    if (self._treeData) {
                        self._layoutData = null; // triggers lazy re-layout on next render
                    }
                }
                TreeGrowth._markDirty();
            },
            onTierZoneChanged: function (tier, min, max) {
                if (!self.settings.tierZones) self.settings.tierZones = {};
                self.settings.tierZones[tier] = { min: min, max: max };
                if (self._treeData) {
                    self._layoutData = null; // triggers re-layout
                }
                TreeGrowth._markDirty();
            }
        });
    },

    /**
     * Render the Classic Growth overlay onto the shared canvas.
     *
     * @param {CanvasRenderingContext2D} ctx
     * @param {number} w  - Canvas logical width
     * @param {number} h  - Canvas logical height
     * @param {Object|null} baseData - Output from TreePreview.getOutput()
     */
    render: function (ctx, w, h, baseData) {
        if (!baseData) {
            // No base data — show placeholder message
            ctx.save();
            ctx.font = '13px sans-serif';
            ctx.fillStyle = getPreviewTextColor();
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(t('preview.scanToPreview'), w / 2, h / 2);
            ctx.restore();
            return;
        }

        // 1. Render the base grid underneath
        baseData.renderGrid(ctx, w, h);

        // 1b. Draw globe mask ring (non-root exclusion zone around globe)
        if (this.settings.centerMask > 0 && baseData.grid) {
            var maskR = this.settings.centerMask * (baseData.grid.tierSpacing || 30);
            var globeOff = (typeof TreeCore !== 'undefined' && TreeCore.getOutput)
                ? TreeCore.getOutput() : { x: 0, y: 0 };
            var mcx = w / 2 + globeOff.x;
            var mcy = h / 2 + globeOff.y;
            ctx.save();
            ctx.beginPath();
            ctx.arc(mcx, mcy, maskR, 0, Math.PI * 2);
            ctx.strokeStyle = 'rgba(255, 60, 60, 0.25)';
            ctx.lineWidth = 1.5;
            ctx.setLineDash([4, 4]);
            ctx.stroke();
            ctx.fillStyle = 'rgba(255, 60, 60, 0.04)';
            ctx.fill();
            ctx.restore();
        }

        // 2. Lazy layout: if we have tree data but no layout yet, try now
        if (this._treeData && (!this._layoutData || !this._layoutData.schools)) {
            this._layoutData = ClassicLayout.layoutAllSchools(this._treeData, baseData, this.settings);
            if (this._layoutData && this._layoutData.schools) {
                var totalNodes = 0;
                var s = this._layoutData.schools;
                for (var sn in s) {
                    if (s.hasOwnProperty(sn)) totalNodes += s[sn].nodes ? s[sn].nodes.length : 0;
                }
                var lazyPool = 0;
                if (this._treeData && this._treeData.schools) {
                    for (var lpn in this._treeData.schools) {
                        if (this._treeData.schools.hasOwnProperty(lpn)) {
                            lazyPool += this._treeData.schools[lpn].nodes ? this._treeData.schools[lpn].nodes.length : 0;
                        }
                    }
                }
                console.log('[ClassicGrowth] Lazy layout: ' + totalNodes + '/' + lazyPool + ' nodes positioned');
                ClassicSettings.setTreeBuilt(true, totalNodes, lazyPool);
                this._zoomToFit();
            }
        }

        // 3. If still no layout data, just show the grid
        if (!this._layoutData || !this._layoutData.schools) return;

        var cx = w / 2;
        var cy = h / 2;
        var opacity = this.settings.ghostOpacity / 100;
        var nodeR = this.settings.nodeRadius;

        // 3. For each school, render edges then nodes
        var schools = this._layoutData.schools;
        for (var schoolName in schools) {
            if (!schools.hasOwnProperty(schoolName)) continue;
            var school = schools[schoolName];

            // Edges first (underneath)
            ClassicRenderer.renderEdges(ctx, cx, cy, school.nodes, school.color, opacity);

            // Nodes on top
            ClassicRenderer.renderNodes(ctx, cx, cy, school.nodes, school.color, opacity, nodeR);

            // Root markers
            for (var i = 0; i < school.nodes.length; i++) {
                if (school.nodes[i].isRoot) {
                    ClassicRenderer.renderRootMarker(
                        ctx,
                        cx + school.nodes[i].x,
                        cy + school.nodes[i].y,
                        school.color,
                        nodeR
                    );
                }
            }
        }

        // 4. Draw per-school tier zone arcs (visual debug overlay)
        // Each school has its own tree height, so zone rings differ per school.
        var zi = this._layoutData.zoneInfo;
        if (zi && this.settings.tierZones) {
            var tierVis = [
                { key: 'Novice',     color: 'rgba(100,180,100,' },
                { key: 'Apprentice', color: 'rgba(80,150,200,' },
                { key: 'Adept',      color: 'rgba(180,160,80,' },
                { key: 'Expert',     color: 'rgba(200,120,60,' },
                { key: 'Master',     color: 'rgba(180,60,60,' }
            ];

            ctx.save();
            for (var zSchoolName in schools) {
                if (!schools.hasOwnProperty(zSchoolName)) continue;
                var zSchool = schools[zSchoolName];
                var treeMaxR = zSchool.treeMaxRadius || 0;
                var growRange = treeMaxR - zi.ringRadius;
                if (growRange < 1) continue;

                // Determine angular span for this school from its root nodes
                var zNodes = zSchool.nodes || [];
                var avgAngle = 0;
                var rootCount = 0;
                for (var zni = 0; zni < zNodes.length; zni++) {
                    if (zNodes[zni].isRoot) {
                        avgAngle += Math.atan2(zNodes[zni].y, zNodes[zni].x);
                        rootCount++;
                    }
                }
                if (rootCount === 0) continue;
                avgAngle /= rootCount;

                // Draw zone arcs as 60-degree wedge centered on school direction
                var arcSpan = Math.PI / 3; // 60 degrees
                var arcStart = avgAngle - arcSpan / 2;
                var arcEnd = avgAngle + arcSpan / 2;

                for (var tvi = 0; tvi < tierVis.length; tvi++) {
                    var tv = tierVis[tvi];
                    var zone = this.settings.tierZones[tv.key];
                    if (!zone) continue;

                    var innerR = zi.ringRadius + (zone.min / 100) * growRange;
                    var outerR = zi.ringRadius + (zone.max / 100) * growRange;

                    // Outer boundary arc
                    ctx.beginPath();
                    ctx.arc(cx, cy, outerR, arcStart, arcEnd);
                    ctx.strokeStyle = tv.color + '0.3)';
                    ctx.lineWidth = 1;
                    ctx.setLineDash([3, 4]);
                    ctx.stroke();

                    // Inner boundary arc
                    ctx.beginPath();
                    ctx.arc(cx, cy, innerR, arcStart, arcEnd);
                    ctx.strokeStyle = tv.color + '0.2)';
                    ctx.lineWidth = 1;
                    ctx.stroke();

                    // Label at the midpoint of the outer arc
                    if (tvi === 0 || tvi === 4) { // Only label Novice and Master to avoid clutter
                        ctx.setLineDash([]);
                        var labelAngle = avgAngle;
                        var labelR = outerR + 6;
                        ctx.font = '8px sans-serif';
                        ctx.fillStyle = tv.color + '0.5)';
                        ctx.textAlign = 'center';
                        ctx.textBaseline = 'middle';
                        ctx.fillText(tv.key, cx + Math.cos(labelAngle) * labelR, cy + Math.sin(labelAngle) * labelR);
                    }
                }
            }
            ctx.setLineDash([]);
            ctx.restore();
        }
    },

    // =========================================================================
    // TREE BUILDING FLOW
    // =========================================================================

    /**
     * Build tree from scanned spells via C++ (ProceduralTreeGenerate). The
     * modal, status and pending flag are set here; ClassicBuildRequest puts the
     * request together, sends it and undoes them if it cannot be sent.
     *
     * @param {Object} [options] - { auto: true, onComplete(summary), onFail(error) }:
     *   the build at game start (AutoBuild) - the progress modal stays hidden and
     *   says when it is done (BuildProgress silent mode) through the two callbacks
     * @returns {boolean} whether a build started
     */
    buildTree: function (options) {
        var auto = !!(options && options.auto);
        var spellData = (typeof state !== 'undefined' && state.lastSpellData)
            ? state.lastSpellData
            : null;

        if (!spellData || !spellData.spells || spellData.spells.length === 0) {
            ClassicSettings.setStatusText(t('buildProgress.statusNoSpells'), 'error', 'buildProgress.statusNoSpells');
            return false;
        }
        // The build at game start never waits behind another build; the player's wins over it (autoBuild.js)
        if (auto && state._classicGrowthBuildPending) return false;
        if (!auto && typeof AutoBuild !== 'undefined') AutoBuild.yieldTo('build');

        // Reset _treeBuilt so setTreeBuilt(true) sees wasBuilt=false and PRM runs
        if (typeof TreeGrowth !== 'undefined') {
            TreeGrowth._treeBuilt = false;
        }

        // Show build progress modal (not for the build at game start)
        var hasPRM = typeof PreReqMaster !== 'undefined' && PreReqMaster.isEnabled && PreReqMaster.isEnabled();
        if (typeof BuildProgress !== 'undefined') {
            BuildProgress.start(hasPRM, options && options.auto ?
                { silent: true, onComplete: options.onComplete, onFail: options.onFail } : null);
        }

        ClassicSettings.setStatusText(t('buildProgress.statusBuilding'), 'working', 'buildProgress.statusBuilding');
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.setBuilding(true);

        // C++ still builds an earlier request (the one at game start, or a second click):
        // it would turn this one away as busy and its result would close this modal as
        // if it were this build's. This one goes out when that result comes back, and
        // that result is dropped (onProceduralTreeComplete).
        if (state._classicGrowthBuildPending) {
            state._classicGrowthBuildQueued = function () { ClassicBuildRequest.send(spellData); };
            console.log('[ClassicGrowth] A build is still running in C++ - this one goes out when it is done');
            return true;
        }

        // Set pending flag so onProceduralTreeComplete routes result here
        state._classicGrowthBuildPending = true;

        ClassicBuildRequest.send(spellData);
        return true;
    },

    /**
     * Receive tree data from the C++ backend callback.
     * Runs layout and triggers a re-render.
     *
     * @param {Object} data - Raw tree structure from the backend
     */
    loadTreeData: function (data) {
        this._treeData = data;
        console.log('[ClassicGrowth] loadTreeData called, schools:', data && data.schools ? Object.keys(data.schools) : 'none');

        // Attempt layout against the current base grid
        var baseData = null;
        if (typeof TreePreview !== 'undefined' && TreePreview.getOutput) {
            baseData = TreePreview.getOutput();
        }
        console.log('[ClassicGrowth] baseData:', baseData ? 'present' : 'null',
            baseData ? ('mode=' + baseData.mode + ' schools=' + (baseData.schools ? baseData.schools.length : 0) +
            ' rootNodes=' + (baseData.rootNodes ? baseData.rootNodes.length : 0)) : '');

        if (baseData) {
            this._layoutData = ClassicLayout.layoutAllSchools(data, baseData, this.settings);
            console.log('[ClassicGrowth] layoutData:', this._layoutData ? Object.keys(this._layoutData.schools || {}) : 'null');
        } else {
            console.warn('[ClassicGrowth] No baseData from TreePreview — layout skipped');
        }

        // Count positioned nodes vs total pool for status display
        var totalNodes = 0;
        var totalPool = 0;
        if (this._layoutData && this._layoutData.schools) {
            var schools = this._layoutData.schools;
            for (var name in schools) {
                if (schools.hasOwnProperty(name)) {
                    totalNodes += schools[name].nodes ? schools[name].nodes.length : 0;
                    console.log('[ClassicGrowth] School ' + name + ': ' + (schools[name].nodes ? schools[name].nodes.length : 0) + ' nodes');
                }
            }
        }
        if (data && data.schools) {
            for (var pn in data.schools) {
                if (data.schools.hasOwnProperty(pn)) {
                    totalPool += data.schools[pn].nodes ? data.schools[pn].nodes.length : 0;
                }
            }
        }
        console.log('[ClassicGrowth] Total positioned nodes: ' + totalNodes + '/' + totalPool);

        ClassicSettings.setTreeBuilt(true, totalNodes, totalPool);
        this._zoomToFit();

        TreeGrowth._markDirty();
    },

    /**
     * Save the current tree data to the game via C++ backend.
     * Bakes layout positions from _layoutData into the output JSON
     * so the spell tree viewer can render at exact positions.
     *
     * A tree with no spell placed is not saved (SpellTreeSave): the status line
     * says so and spell_tree.json stays as it was.
     *
     * @param {Object} [options] - { auto: true, onSaved(spellCount), isCurrent() }: the
     *   build at game start (AutoBuild) - no switch to the Spell Tree tab while the panel
     *   is open (with it closed, the tree tab is where a saved tree opens anyway); not
     *   saved when isCurrent() says the run ended (timed out) during the declutter pass
     * @returns {boolean} false when nothing was sent to be saved
     */
    applyTree: function (options) {
        var auto = !!(options && options.auto);
        if (!this._treeData) return false;
        if (!auto && this._refuseWhileBuilding()) return false;
        if (!auto && typeof AutoBuild !== 'undefined') AutoBuild.yieldTo('apply');

        // Get base data for mode and root directions
        var baseData = null;
        if (typeof TreePreview !== 'undefined' && TreePreview.getOutput) {
            baseData = TreePreview.getOutput();
        }

        var built = ClassicTreeOutput.build(this._treeData, this._layoutData, baseData);
        var output = built.output;
        var posCount = built.posCount;
        var spellCount = SpellTreeSave.countSpellNodes(output);
        if (spellCount === 0) {
            console.error('[ClassicGrowth] Apply: no spell was placed (layout ' + (this._layoutData ? 'empty' : 'missing') +
                ', preview grid ' + (baseData ? 'present' : 'missing') + ') - not saved');
            ClassicSettings.setStatusText(tOr('buildProgress.statusNothingPlaced', null, 'No spell could be placed, so the tree ' +
                'was not saved. Open the Scan tab and build again.'), 'error', 'buildProgress.statusNothingPlaced');
            return false;
        }

        // Cross school bridges: extra soft prerequisites, and the list itself for the viewer
        if (typeof SchoolBridges !== 'undefined') SchoolBridges.applyToOutput(output, this._treeData);

        // No spell under another or under a line (LayoutDeclutter): a piece at a
        // time between frames, then saved and shown
        LayoutDeclutter.applyAsync(output, function() {
            // The build at game start ended (timed out) while this decluttered: not saved (AutoBuild)
            if (auto && options.isCurrent && !options.isCurrent()) {
                console.log('[ClassicGrowth] Apply: the build at game start had ended - not saved');
                return;
            }
            if (!SpellTreeSave.send(output, auto ? 'auto build' : 'apply')) return;
            if (typeof ScanStage !== 'undefined') ScanStage.markApplied();

            // Load into the spell tree viewer so it displays immediately. The build at
            // game start with the panel closed leaves progress and known spells to the
            // panel's opening (onPanelShowing asks for both), as a save load does.
            if (typeof loadTreeData === 'function') {
                var hidden = window._panelVisible === false;
                var loadStarted = Date.now();
                loadTreeData(output, auto ? hidden : true, false, { skipProgressFetch: auto && hidden });
                // A timing line kept in SpellLearning.log whatever the developer mode (console.warn)
                if (auto) console.warn('[Timing] auto build loadTreeData: ' + spellCount + ' spells in ' + (Date.now() - loadStarted) + ' ms' + (hidden ? ' (panel closed)' : ''));
            }

            var appliedSchoolCount = Object.keys(output.schools).length;
            ClassicSettings.setStatusText(t('buildProgress.statusApplied', {count: posCount}), 'done', 'buildProgress.statusApplied', {count: posCount});
            if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeApplied', {schools: appliedSchoolCount}), 'success', 'status.treeApplied', {schools: appliedSchoolCount});

            if (auto) {
                if (options.onSaved) options.onSaved(spellCount);
                return;
            }
            // Switch to the Spell Tree tab after a brief delay
            if (typeof switchTab === 'function') {
                setTimeout(function() {
                    switchTab('spellTree');
                }, 300);
            }
        });
        return true;
    },

    /**
     * Return the current layout position map (formId → {x, y}) for preview rendering.
     * Returns null if no tree has been built yet.
     */
    getPositionMap: function () {
        if (!this._layoutData || !this._layoutData.schools) return null;
        var posMap = {};
        var schools = this._layoutData.schools;
        for (var name in schools) {
            if (!schools.hasOwnProperty(name)) continue;
            var nodes = schools[name].nodes || [];
            for (var i = 0; i < nodes.length; i++) {
                var n = nodes[i];
                if (n.formId) posMap[n.formId] = { x: n.x, y: n.y };
            }
        }
        return Object.keys(posMap).length > 0 ? posMap : null;
    },

    /**
     * Apply or Clear while a build still runs (its locks still to come): refused, as the
     * modal over the panel refuses them for the buttons' build - saved now, the tree would
     * have no locks, and they would land on whatever tree is there when they come back.
     * @returns {boolean} true when refused
     */
    _refuseWhileBuilding: function () {
        if (typeof BuildProgress === 'undefined' || !BuildProgress.isActive()) return false;
        ClassicSettings.setStatusText(tOr('buildProgress.statusStillBuilding', null, 'The tree is still being built (locks are still being applied) - wait until it is done.'),
            'error', 'buildProgress.statusStillBuilding');
        return true;
    },

    /**
     * Discard the current tree data and clear the preview.
     */
    clearTree: function () {
        if (this._refuseWhileBuilding()) return false;
        // Clearing a built tree ends the build at game start (nothing to clear: it goes on)
        if (this._treeData && typeof AutoBuild !== 'undefined') AutoBuild.yieldTo('clear');
        this._treeData = null;
        this._layoutData = null;

        ClassicSettings.setTreeBuilt(false);
        if (typeof updateScanStatus === 'function') updateScanStatus(t('status.treeCleared'), '', 'status.treeCleared');

        TreeGrowth._markDirty();
    },

    // =========================================================================
    // ZOOM TO FIT
    // =========================================================================

    /**
     * Auto-zoom the Growth canvas so the full tree is visible with padding.
     * Reads all positioned node coordinates and computes the bounding box.
     */
    _zoomToFit: function () {
        if (!this._layoutData || !this._layoutData.schools) return;
        if (typeof TreeGrowth === 'undefined') return;

        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        var schools = this._layoutData.schools;
        var nodeCount = 0;
        for (var name in schools) {
            if (!schools.hasOwnProperty(name)) continue;
            var nodes = schools[name].nodes || [];
            for (var i = 0; i < nodes.length; i++) {
                if (nodes[i].x < minX) minX = nodes[i].x;
                if (nodes[i].x > maxX) maxX = nodes[i].x;
                if (nodes[i].y < minY) minY = nodes[i].y;
                if (nodes[i].y > maxY) maxY = nodes[i].y;
                nodeCount++;
            }
        }
        if (nodeCount === 0) return;

        var treeW = maxX - minX;
        var treeH = maxY - minY;
        if (treeW < 1) treeW = 100;
        if (treeH < 1) treeH = 100;

        // Add 15% padding
        var padding = 1.15;
        var canvasW = TreeGrowth._width || 400;
        var canvasH = TreeGrowth._height || 400;
        var zoomX = canvasW / (treeW * padding);
        var zoomY = canvasH / (treeH * padding);
        var zoom = Math.min(zoomX, zoomY, 2.0); // cap at 2x
        zoom = Math.max(zoom, 0.1);

        // Center pan on the tree's center
        var centerX = (minX + maxX) / 2;
        var centerY = (minY + maxY) / 2;

        TreeGrowth.zoom = zoom;
        TreeGrowth.panX = -centerX * zoom;
        TreeGrowth.panY = -centerY * zoom;

        console.log('[ClassicGrowth] Zoom to fit: zoom=' + zoom.toFixed(2) +
            ' bounds=' + Math.round(treeW) + 'x' + Math.round(treeH) +
            ' center=' + Math.round(centerX) + ',' + Math.round(centerY));
    },

    // =========================================================================
    // EXTERNAL API
    // =========================================================================

    /**
     * Return a shallow copy of the current settings.
     * @returns {Object}
     */
    getSettings: function () {
        var copy = {};
        for (var key in this.settings) {
            if (this.settings.hasOwnProperty(key)) {
                copy[key] = this.settings[key];
            }
        }
        return copy;
    }
};

// =============================================================================
// SELF-REGISTRATION
// =============================================================================

if (typeof TreeGrowth !== 'undefined') {
    TreeGrowth.registerMode('classic', TreeGrowthClassic);
}
