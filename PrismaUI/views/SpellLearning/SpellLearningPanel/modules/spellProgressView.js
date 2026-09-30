/**
 * The card's learning part: the progress bar and the Learn/Unlock buttons
 * (updateDetailsProgression), and selecting a spell by id (selectNodeById).
 * Split out of treeViewerUI.js (600-line limit); loads after spellCardView.js.
 */

/**
 * The progress bar and the Learn/Unlock buttons for the spell on the card.
 * @param {Object} node
 * @param {{preview?: boolean}} [opts] preview = hover peek, see renderSpellCard
 */
function updateDetailsProgression(node, opts) {
    var preview = !!(opts && opts.preview);
    // A refresh of the selected spell while another one is previewed would put
    // its progress under the wrong card; the selected card is redrawn whole
    // when the cursor leaves
    if (!preview && typeof DetailsPeek !== 'undefined' && DetailsPeek.isPeeking() && node === state.selectedNode) return;
    var progressSection = document.getElementById('progress-section');
    var learnBtn = document.getElementById('learn-btn');
    var unlockBtn = document.getElementById('unlock-btn');
    var progressBar = document.getElementById('progress-bar');
    var progressText = document.getElementById('progress-text');
    var progressEdit = document.getElementById('progress-edit');
    var xpCurrentInput = document.getElementById('xp-current-input');
    var xpRequiredInput = document.getElementById('xp-required-input');
    
    // Get progress data for this spell (use canonical ID for duplicates)
    var canonId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
    var progress = state.spellProgress[canonId] || { xp: 0, required: 100, unlocked: false, ready: false };
    var isLearningTarget = state.learningTargets[node.school] === canonId || state.learningTargets[node.school] === node.formId;

    // Debug: log learning target check to file via C++ (developer mode, not for every hover)
    if (!preview && typeof LogGate !== 'undefined' && LogGate.on()) {
        var debugMsg = '[SELECT] ' + (node.name || node.formId) +
                       ' | school:' + node.school +
                       ' | formId:' + node.formId +
                       ' | targets:' + JSON.stringify(state.learningTargets) +
                       ' | isTarget:' + isLearningTarget +
                       ' | state:' + node.state +
                       ' | isRoot:' + (node.isRoot || false);
        console.log(debugMsg);
        if (window.callCpp) {
            window.callCpp('LogMessage', JSON.stringify({ level: 'info', message: debugMsg }));
        }
    }
    
    // Update learning status badge
    updateLearningStatusBadge(node, progress);
    
    // Calculate required XP - use override if exists, otherwise tier-based
    var tierXP = getXPForTier(node.level);
    var requiredXP = getRequiredXPForNode(node);
    progress.required = requiredXP;

    // Opened by a spell the player already knows: say so, it explains both the
    // open lock and the smaller XP bar
    var reverseNote = document.getElementById('reverse-unlock-note');
    if (reverseNote) {
        var showReverse = node.openedByKnownChild && settings.reverseUnlock !== false && node.state !== 'unlocked';
        reverseNote.classList.toggle('hidden', !showReverse);
        if (showReverse) {
            var reversePct = Math.round(getReverseUnlockXPShare(node.level) * 100);
            reverseNote.textContent = tOr('details.openedByKnownChild', { pct: reversePct },
                'You already know a spell this one leads to: open, at ' + reversePct + '% of its XP.');
        }
    }
    
    // Hide all buttons by default
    learnBtn.classList.add('hidden');
    unlockBtn.classList.add('hidden');
    progressSection.classList.add('hidden');
    progressText.classList.remove('hidden');
    if (progressEdit) progressEdit.classList.add('hidden');
    
    // CHEAT MODE: Allow unlocking/relocking any node + editable XP
    if (settings.cheatMode) {
        progressSection.classList.remove('hidden');
        unlockBtn.classList.remove('hidden');
        unlockBtn.disabled = false;
        
        // Show editable XP inputs instead of text
        progressText.classList.add('hidden');
        if (progressEdit) {
            progressEdit.classList.remove('hidden');
            if (xpCurrentInput) {
                xpCurrentInput.value = Math.floor(progress.xp || 0);
                // Remove old listeners to avoid duplicates
                xpCurrentInput.onchange = function() {
                    var newXP = Math.max(0, parseInt(this.value) || 0);
                    this.value = newXP;
                    progress.xp = newXP;
                    state.spellProgress[canonId] = progress;
                    // Update progress bar
                    var percent = requiredXP > 0 ? (newXP / requiredXP) * 100 : 0;
                    progressBar.style.width = Math.min(percent, 100) + '%';
                    progressBar.classList.toggle('ready', newXP >= requiredXP);
                    // Tell C++ about the XP change (send canonical ID)
                    if (window.callCpp) {
                        window.callCpp('SetSpellXP', JSON.stringify({ formId: canonId, xp: newXP }));
                    }
                };
            }
            if (xpRequiredInput) {
                xpRequiredInput.value = Math.floor(requiredXP);
                // Show if this is an override
                var hasOverride = xpOverrides[node.formId] !== undefined;
                xpRequiredInput.classList.toggle('has-override', hasOverride);
                xpRequiredInput.title = hasOverride ? 'Custom override (tier default: ' + tierXP + ')' : 'Tier default';
                
                xpRequiredInput.onchange = function() {
                    var newRequired = Math.max(1, parseInt(this.value) || tierXP);
                    this.value = newRequired;
                    // Store as override
                    xpOverrides[node.formId] = newRequired;
                    this.classList.add('has-override');
                    this.title = 'Custom override (tier default: ' + tierXP + ')';
                    progress.required = newRequired;
                    // Update progress bar
                    var percent = newRequired > 0 ? (progress.xp / newRequired) * 100 : 0;
                    progressBar.style.width = Math.min(percent, 100) + '%';
                    progressBar.classList.toggle('ready', progress.xp >= newRequired);
                    // Save overrides
                    autoSaveSettings();
                };
            }
        }
        
        // Update progress bar
        var cheatPercent = requiredXP > 0 ? (progress.xp / requiredXP) * 100 : 0;
        progressBar.style.width = Math.min(cheatPercent, 100) + '%';
        
        if (node.state === 'unlocked') {
            // Actually unlocked (node state is the source of truth) - show relock option
            unlockBtn.textContent = tOr('details.relockSpell', null, 'Relock Spell');
            unlockBtn.style.background = '#ef4444';  // Red for relock
            progressBar.classList.add('ready');
            learnBtn.classList.add('hidden');
        } else {
            // Not unlocked - show cheat unlock button (even if progress.unlocked is stale)
            unlockBtn.textContent = tOr('details.unlockCheat', null, 'Unlock (Cheat)');
            unlockBtn.style.background = '';  // Default color
            progressBar.classList.toggle('ready', progress.xp >= requiredXP);

            // Also show learn button in cheat mode (for normal learning path)
            if (node.state === 'available' || node.state === 'learning') {
                learnBtn.classList.remove('hidden');
                var isLearningTarget = state.learningTargets && state.learningTargets[node.school] === node.formId;
                if (isLearningTarget) {
                    learnBtn.textContent = tOr('details.learning', null, 'Learning...');
                    learnBtn.classList.add('active');
                } else {
                    learnBtn.textContent = tOr('details.learnThis', null, 'Learn This');
                    learnBtn.classList.remove('active');
                }
            }
        }
        return;
    }
    
    // NORMAL MODE
    if (node.state === 'locked') {
        // Locked - can't do anything
        return;
    }
    
    if (node.state === 'unlocked') {
        // Actually unlocked - nothing to show
        return;
    }
    
    // Node is available - show progression options
    progressSection.classList.remove('hidden');
    
    // Update progress bar
    var percent = progress.required > 0 ? (progress.xp / progress.required) * 100 : 0;
    progressBar.style.width = Math.min(percent, 100) + '%';
    progressText.textContent = Math.floor(progress.xp) + ' / ' + Math.floor(progress.required) + ' ' + tOr('details.xpUnit', null, 'XP');
    
    if (progress.ready || progress.xp >= progress.required) {
        // Ready to unlock - show unlock button
        progressBar.classList.add('ready');
        unlockBtn.classList.remove('hidden');
        unlockBtn.disabled = false;
        unlockBtn.textContent = tOr('details.unlockSpell', null, 'Unlock Spell');
        unlockBtn.style.background = '';  // Default color
        learnBtn.classList.add('hidden');
    } else {
        // Not ready - show learn button
        progressBar.classList.remove('ready');
        learnBtn.classList.remove('hidden');
        
        if (isLearningTarget) {
            learnBtn.textContent = tOr('details.learning', null, 'Learning...');
            learnBtn.classList.add('active');
        } else {
            learnBtn.textContent = tOr('details.learnThis', null, 'Learn This');
            learnBtn.classList.remove('active');
        }
    }
}

function selectNodeById(id) {
    if (!state.treeData) return;
    var node = _findNodeById(id);
    if (!node) return;

    // Canvas renderer: select + center the node (same path as a click)
    if (SmartRenderer.activeRenderer === 'canvas' && typeof CanvasRenderer !== 'undefined' && CanvasRenderer.canvas) {
        var canvasNode = CanvasRenderer._nodeMap ? (CanvasRenderer._nodeMap.get(node.id) || CanvasRenderer._nodeMap.get(node.formId)) : null;
        CanvasRenderer.selectNodeAndFocus(canvasNode || node);
        return;
    }

    WheelRenderer.selectNode(node);
    WheelRenderer.rotateSchoolToTop(node.school);
}
