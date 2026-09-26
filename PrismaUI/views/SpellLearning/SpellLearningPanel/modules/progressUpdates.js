/**
 * Progress updates from C++ (onProgressUpdate) - one per XP gain while the panel
 * is open, so the cheap path matters.
 *
 * It used to cost, per gain: a scan of all ~1,400 spells to find the one,
 * two JSON dumps to the log, a repaint of every spell in the tree layer, a
 * rebuild of the discovery set, the learning paths (which also dropped the
 * particles in flight) and the node buckets, and - for the selected spell - the
 * whole spell card rebuilt. Now:
 *   - the spell is found through a map (rebuilt when the tree changes);
 *   - the tree is repainted only when what it shows changed: the spell's state,
 *     its XP ring by at least RING_STEP, or a reveal threshold crossed (the
 *     name on the tree); the full refresh only when the state changed;
 *   - the selected card is rebuilt only for a state change or a reveal
 *     threshold, otherwise only its progress bar and buttons are updated.
 *
 * Depends on: state, settings, getCanonicalFormId, resolveCanonicalId,
 *             getRequiredXPForNode, isSpellMastered, syncDuplicateState,
 *             recalculateNodeAvailability, autoAdvanceLearningTarget,
 *             showSpellDetails, updateDetailsProgression, RequiredXPSync,
 *             CanvasRenderer, SmartRenderer, WheelRenderer (all optional)
 */

var ProgressUpdates = {

    RING_STEP: 0.01,       // the XP ring is repainted for a change this big (1%)
    AUTO_ADVANCE_DELAY_MS: 100,

    _painted: {},          // canonical formId -> progress the tree layer last showed
    _byCanon: null,        // canonical formId -> [nodes] (duplicates share one id)
    _forTree: null,        // the tree the map was built for

    _canon: function(n) {
        return (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(n) : n.formId;
    },

    /** Every node with this canonical formId; the map is rebuilt when the tree object changes. */
    nodesFor: function(canonId) {
        if (!state.treeData || !state.treeData.nodes) return [];
        if (this._forTree !== state.treeData || !this._byCanon || this._forLength !== state.treeData.nodes.length) {
            var map = {};
            var self = this;
            state.treeData.nodes.forEach(function(n) {
                var k = self._canon(n);
                (map[k] = map[k] || []).push(n);
            });
            this._byCanon = map;
            this._forTree = state.treeData;
            this._forLength = state.treeData.nodes.length;   // nodes added or removed in edit mode
        }
        return this._byCanon[canonId] || [];
    },

    /** A node was added or removed without a new tree object (edit mode): rebuild the map next time. */
    invalidate: function() {
        this._byCanon = null;
    },

    /** Did the progress cross one of the reveal thresholds (name, effects, description)? */
    crossedReveal: function(before, after) {
        var lo = Math.min(before, after) * 100, hi = Math.max(before, after) * 100;
        var t = [settings.revealName, settings.revealEffects, settings.revealDescription];
        for (var i = 0; i < t.length; i++) {
            if (typeof t[i] === 'number' && lo < t[i] && t[i] <= hi) return true;
        }
        return false;
    },

    handle: function(dataStr) {
        var data = typeof dataStr === 'string' ? JSON.parse(dataStr) : dataStr;
        var canonId = (typeof resolveCanonicalId === 'function') ? resolveCanonicalId(data.formId) : data.formId;
        if (typeof RequiredXPSync !== 'undefined') RequiredXPSync.noteReported(data.formId, data.requiredXP);

        // The panel's own required XP (tier settings, overrides, known-higher-spell share)
        var nodes = this.nodesFor(canonId);
        var node = nodes[0] || null;
        var req = node && typeof getRequiredXPForNode === 'function' ? getRequiredXPForNode(node) : data.requiredXP;

        var before = state.spellProgress[canonId];
        var beforePct = before && typeof before.progress === 'number' ? before.progress : 0;
        // What the tree last showed, not the last update: gains smaller than
        // RING_STEP add up until the ring is worth repainting
        var paintedPct = this._painted.hasOwnProperty(canonId) ? this._painted[canonId] : beforePct;
        var afterPct = req > 0 ? data.currentXP / req : 0;
        state.spellProgress[canonId] = {
            xp: data.currentXP,
            required: req,
            progress: afterPct,
            unlocked: data.unlocked || false,
            ready: data.ready || (data.currentXP >= req)
        };

        // Mastered: the spell's state changes, and with it what its children may do
        var isMastered = typeof isSpellMastered === 'function' && isSpellMastered(canonId);
        var stateChanged = false;
        if (isMastered) {
            nodes.forEach(function(n) {
                if (n.state === 'unlocked') return;
                n.state = 'unlocked';
                stateChanged = true;
                if (typeof syncDuplicateState === 'function') syncDuplicateState(n);
            });
        } else if (data.unlocked) {
            console.warn('[SpellLearning] C++ says unlocked but not mastered yet: ' + canonId);
        }
        if (stateChanged) {
            console.log('[SpellLearning] Mastered: ' + (node && node.name ? node.name : canonId));
            if (typeof recalculateNodeAvailability === 'function') recalculateNodeAvailability();
            if (settings.autoAdvanceLearning && node && typeof autoAdvanceLearningTarget === 'function') {
                var school = node.school;
                setTimeout(function() { autoAdvanceLearningTarget(canonId, school); }, this.AUTO_ADVANCE_DELAY_MS);
            }
        }

        var revealCrossed = this.crossedReveal(beforePct, afterPct);
        var repaint = stateChanged || revealCrossed || Math.abs(afterPct - paintedPct) >= this.RING_STEP;

        // The selected spell's card
        var sel = state.selectedNode;
        if (sel && this._canon(sel) === canonId) {
            if (isMastered) sel.state = 'unlocked';
            if ((stateChanged || revealCrossed) && typeof showSpellDetails === 'function') showSpellDetails(sel);
            else if (typeof updateDetailsProgression === 'function') updateDetailsProgression(sel);
        }

        // The tree
        if (state.treeData) {
            if (stateChanged) {
                if (typeof WheelRenderer !== 'undefined' && WheelRenderer.updateNodeStates) WheelRenderer.updateNodeStates();
                if (typeof SmartRenderer !== 'undefined' && SmartRenderer.refresh) SmartRenderer.refresh();
                if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
            } else if (repaint) {
                if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
            }
        }
        this._painted[canonId] = repaint ? afterPct : paintedPct;

        // C++ takes the panel's number if it has another one (RequiredXPSync)
        if (typeof RequiredXPSync !== 'undefined') RequiredXPSync.sync();
    }
};

window.onProgressUpdate = function(dataStr) {
    try {
        ProgressUpdates.handle(dataStr);
    } catch (e) {
        console.error('[SpellLearning] Failed to parse progress update:', e);
    }
};

window.ProgressUpdates = ProgressUpdates;
