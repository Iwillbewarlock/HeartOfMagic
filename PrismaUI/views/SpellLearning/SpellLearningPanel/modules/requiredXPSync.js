/**
 * RequiredXPSync - keeps C++'s required XP for the learning targets equal to the
 * panel's (getRequiredXPForNode: per-spell override, else tier XP, times the
 * known-higher-spell share).
 *
 * C++ gets the number when a spell becomes a target (SetLearningTarget), but it
 * can change afterwards: the co-save keeps only the percent, so after a load C++
 * starts from plain tier XP; and a known higher spell appearing, or a share
 * slider moving, changes the panel's number mid-session. Without this the card
 * says "ready" at 80 XP while C++ masters the spell at 200, or the other way round.
 *
 * What C++ last reported (onProgressData, onProgressUpdate) is compared with the
 * panel's number for each current target; a difference is sent once
 * (SetRequiredXP), and C++ answers with a progress update carrying the new value.
 *
 * Depends on: state, getRequiredXPForNode, getCanonicalFormId (optional), callCpp
 */

var RequiredXPSync = {

    _reported: {},   // canonical formId -> required XP C++ last reported
    _sent: {},       // formId -> value sent and not reported back yet

    /** A full progress dump from C++ (after a load or when the panel asks). */
    fromProgressData: function(spellProgress) {
        this._reported = {};
        this._sent = {};
        for (var id in spellProgress) {
            if (spellProgress.hasOwnProperty(id) && spellProgress[id]) {
                this.noteReported(id, spellProgress[id].required);
            }
        }
    },

    /** One spell's required XP as C++ reported it. */
    noteReported: function(formId, requiredXP) {
        if (typeof requiredXP !== 'number' || !(requiredXP > 0)) return;
        this._reported[this._canon(formId)] = requiredXP;
    },

    /** Send C++ the panel's number for every learning target that differs. */
    sync: function() {
        if (!window.callCpp || !state.treeData || !state.treeData.nodes || !state.learningTargets) return;
        if (typeof getRequiredXPForNode !== 'function') return;
        for (var school in state.learningTargets) {
            if (!state.learningTargets.hasOwnProperty(school)) continue;
            var formId = state.learningTargets[school];
            if (!formId) continue;
            var node = this._node(formId);
            if (!node || node.state === 'unlocked') continue;

            var want = getRequiredXPForNode(node);
            var have = this._reported[this._canon(node.formId)];
            if (typeof have === 'number' && Math.abs(have - want) < 0.5) {
                delete this._sent[node.formId];
                continue;
            }
            if (this._sent[node.formId] === want) continue;
            this._sent[node.formId] = want;
            window.callCpp('SetRequiredXP', JSON.stringify({ formId: node.formId, requiredXP: want }));
        }
    },

    _node: function(formId) {
        var nodes = state.treeData.nodes;
        var hit = nodes.find(function(n) { return n.formId === formId; });
        if (hit) return hit;
        // A duplicate or a load-order shifted id: match on the canonical one
        var canon = this._canon(formId);
        var self = this;
        return nodes.find(function(n) { return self._canon(n.formId) === canon; }) || null;
    },

    _canon: function(formId) {
        if (typeof resolveCanonicalId === 'function') return resolveCanonicalId(formId);
        return formId;
    }
};

window.RequiredXPSync = RequiredXPSync;
