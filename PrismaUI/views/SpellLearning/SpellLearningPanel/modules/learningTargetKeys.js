/**
 * LearningTargetKeys - which school key of state.learningTargets holds a spell.
 *
 * The panel keys a learning target by the tree node's school (its first
 * effect), and so does the plugin (ProgressionManager::TargetSchoolOf). A save
 * from before could hold a spell under another key (a tome used the costliest
 * effect's school), so the spell card and the Learn button look the spell up
 * by id instead of trusting node.school, and clearing sends the formId too:
 * the plugin then clears it under whatever key holds it.
 *
 * window.onLearningTargetDropped (from UIManager::NotifyLearningTargetDropped):
 * a school key no longer holds the spell; the panel's copy forgets that key
 * and redraws the learning paths, the wheel's marks and the open spell card.
 *
 * Pure apart from the callback (learningTargetKeysTest.js covers it).
 * Depends on (read when called, optional): state, resolveCanonicalId,
 * CanvasRenderer, WheelRenderer, showSpellDetails.
 * Exports (global): LearningTargetKeys, onLearningTargetDropped
 */

var LearningTargetKeys = {

    /**
     * The school key holding the spell (formId or its canonical id), else null.
     * @param {Object} targets - school -> formId (state.learningTargets)
     * @param {string} formId
     * @param {string} [canonId]
     * @param {string} [preferSchool] - checked first (the node's own school)
     */
    schoolOf: function(targets, formId, canonId, preferSchool) {
        if (!targets || !formId) return null;
        var holds = function(id) { return !!id && (id === formId || (!!canonId && id === canonId)); };
        if (preferSchool && targets.hasOwnProperty(preferSchool) && holds(targets[preferSchool])) return preferSchool;
        for (var school in targets) {
            if (targets.hasOwnProperty(school) && holds(targets[school])) return school;
        }
        return null;
    },

    /** Forget a key the plugin dropped, if it still holds that spell; true when it did */
    drop: function(targets, school, formId) {
        if (!targets || !school || !targets.hasOwnProperty(school)) return false;
        var held = targets[school];
        var canon = (typeof resolveCanonicalId === 'function') ? resolveCanonicalId(formId) : formId;
        if (held !== formId && held !== canon) return false;
        delete targets[school];
        return true;
    },

    /**
     * Redraw what shows the targets after a key changed: the tree's learning
     * paths, the wheel's learning marks and the open spell card (its Learn button)
     */
    refreshViews: function() {
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer._nodeMap) {
            CanvasRenderer._buildLearningPaths();
            CanvasRenderer._needsRender = true;
        }
        if (typeof WheelRenderer !== 'undefined' && typeof WheelRenderer.updateNodeStates === 'function') {
            WheelRenderer.updateNodeStates();
        }
        if (typeof state !== 'undefined' && state.selectedNode && typeof showSpellDetails === 'function') {
            showSpellDetails(state.selectedNode);
        }
    }
};

window.LearningTargetKeys = LearningTargetKeys;

window.onLearningTargetDropped = function(dataStr) {
    try {
        var data = typeof dataStr === 'string' ? JSON.parse(dataStr) : dataStr;
        if (!data || typeof state === 'undefined' || !state.learningTargets) return;
        if (LearningTargetKeys.drop(state.learningTargets, data.school, data.formId)) {
            console.log('[SpellLearning] Learning target key dropped: ' + data.school + ' (' + data.formId + ')');
            LearningTargetKeys.refreshViews();
        }
    } catch (e) {
        console.error('[SpellLearning] Failed to parse dropped learning target:', e);
    }
};
