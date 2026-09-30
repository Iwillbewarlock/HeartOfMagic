/**
 * The spell card: selecting a spell (showSpellDetails, selectSpell,
 * clearSpellSelection) and drawing the card (renderSpellCard). Split out of
 * treeViewerUI.js (600-line limit); loads after treeLoad.js.
 */

/**
 * Select a spell: make it the card's subject and the Learn/Unlock target.
 * Selecting is what reveals things and writes state; a hover preview
 * (DetailsPeek) only draws the card, so it goes through renderSpellCard alone.
 */
function showSpellDetails(node) {
    if (!node) return;
    // A refresh of the pinned spell while the cursor previews another one:
    // keep what the cursor is on, the pinned card comes back when it leaves
    if (typeof DetailsPeek !== 'undefined' && DetailsPeek.isPeeking() &&
        state.selectedNode === node && DetailsPeek.peekNode !== node) {
        selectSpell(node);
        DetailsPeek.refresh();
        return;
    }
    if (typeof DetailsPeek !== 'undefined') DetailsPeek.onSelected(node);
    selectSpell(node);
    renderSpellCard(node, { preview: false });
}

/** The side effects of picking a spell, kept apart from drawing its card */
function selectSpell(node) {
    // Reveal locks for this node (Pre Req Master)
    if (typeof PreReqMaster !== 'undefined' && PreReqMaster.revealLocksForNode) {
        PreReqMaster.revealLocksForNode(node.id);
    }
    // Store selected node for button handlers
    state.selectedNode = node;
}

/**
 * Drop the selection: the card empties (or hides when hover preview is off)
 * and the tree's highlight goes with it. Close button, Esc, click on nothing.
 */
function clearSpellSelection() {
    state.selectedNode = null;
    if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.selectedNode) {
        CanvasRenderer.selectedNode = null;
        CanvasRenderer._selectedPathEdges = null;
        CanvasRenderer._selectedPathNodes = null;
        CanvasRenderer._needsRender = true;
    }
    if (typeof TreeNav !== 'undefined') TreeNav.setActiveSchool(null);
    if (typeof DetailsPeek !== 'undefined') {
        DetailsPeek.onSelected(null);
        DetailsPeek.applyLayout();
    } else {
        var panel = document.getElementById('details-panel');
        if (panel) panel.classList.add('hidden');
        var treePage = document.getElementById('contentSpellTree');
        if (treePage) treePage.classList.remove('details-open');
    }
}

/**
 * A node state as the player reads it: the same words as the footer legend,
 * which every language already translates.
 * @param {string} nodeState - 'locked' | 'available' | 'learning' | 'unlocked'
 * @returns {string}
 */
function spellStateLabel(nodeState) {
    var s = String(nodeState || '');
    var fallback = s.charAt(0).toUpperCase() + s.slice(1);
    var keys = {
        locked: 'footer.legendLocked',
        available: 'footer.legendAvailable',
        learning: 'footer.legendLearning',
        unlocked: 'footer.legendUnlocked'
    };
    return keys[s] ? tOr(keys[s], null, fallback) : fallback;
}

/**
 * Fill the details panel with a spell's card. Draws only: nothing here
 * changes the selection, reveals a lock or talks to C++.
 * @param {Object} node
 * @param {{preview?: boolean}} [opts] preview = hover peek: Learn/Unlock are
 *        shown but cannot be pressed, and the card says to click to select
 */
function renderSpellCard(node, opts) {
    var preview = !!(opts && opts.preview);
    var panel = document.getElementById('details-panel');
    if (!panel) return;
    panel.classList.remove('hidden');
    panel.classList.remove('is-empty');
    panel.classList.toggle('peeking', preview);
    var treePage = document.getElementById('contentSpellTree');
    if (treePage) treePage.classList.add('details-open');

    // Get progress data for progressive reveal
    // Debug: show what keys are in spellProgress (a hover preview stays quiet)
    var devLog = !preview && typeof LogGate !== 'undefined' && LogGate.on();
    if (devLog) {
        console.log('[SpellLearning] showSpellDetails - Looking for:', node.formId);
        console.log('[SpellLearning] Available progress keys:', Object.keys(state.spellProgress).join(', '));
    }

    // Use canonical formId for duplicates, then try multiple formats
    var lookupId = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
    var formIdVariants = [
        lookupId,
        lookupId.toLowerCase(),
        lookupId.toUpperCase(),
        lookupId.replace(/^0x/i, ''),
        '0x' + lookupId.replace(/^0x/i, ''),
        '0x' + lookupId.replace(/^0x/i, '').toUpperCase(),
        '0x' + lookupId.replace(/^0x/i, '').toLowerCase()
    ];
    // Also check the node's own formId if different from canonical
    if (lookupId !== node.formId) {
        formIdVariants.push(node.formId);
    }
    
    var progress = null;
    var matchedKey = null;
    for (var i = 0; i < formIdVariants.length; i++) {
        if (state.spellProgress[formIdVariants[i]]) {
            progress = state.spellProgress[formIdVariants[i]];
            matchedKey = formIdVariants[i];
            if (!preview) console.log('[SpellLearning] Found progress with key:', matchedKey);
            break;
        }
    }

    if (!progress && !preview) {
        console.log('[SpellLearning] No progress found for any variant of', node.formId);
    }
    progress = progress || { xp: 0, required: 100, progress: 0 };
    
    // XP required from current settings, not a stale C++ value: override, else
    // tier, times the known-higher-spell share (the reveal thresholds read this too)
    var requiredXP = getRequiredXPForNode(node);
    
    // Calculate progress percent - use xp/required directly since progress.progress may not be set
    var progressPercent = requiredXP > 0 ? ((progress.xp || 0) / requiredXP) * 100 : 0;
    if (!preview) {
        console.log('[SpellLearning] Progress:', progress.xp, '/', requiredXP, '=', progressPercent.toFixed(1) + '%',
            '| revealName:', settings.revealName, '| showName should be:', progressPercent >= settings.revealName);
    }
    
    // Check if player has the spell (via early learning or other means)
    // Use canonical formId for duplicates
    var playerHasSpell = progress.unlocked ||
                         state.playerKnownSpells.has(lookupId) ||
                         state.playerKnownSpells.has(node.formId) ||
                         node.state === 'unlocked';
    
    // Determine what to show based on state and progress
    // Cheat mode shows ALL info (includes former debug mode features)
    // Edit mode reveals everything so the user can see what they're editing
    // Player having the spell (early learning) also reveals full info
    // Available (learnable) spells always show their name
    var isEditActive = typeof EditMode !== 'undefined' && EditMode.isActive;
    var showFullInfo = playerHasSpell || settings.cheatMode || isEditActive;
    var isLearning = node.state === 'learning';
    var isLocked = node.state === 'locked';
    // Locked/mystery nodes never reveal info via progress threshold - only via cheat/edit/hasSpell
    var showName = showFullInfo || isLearning || (!isLocked && progressPercent >= settings.revealName);
    // Card reveal order: name -> keyword chips -> description and figures.
    // The chips take over the threshold the effects list used to have; the effects,
    // cost and type now open together with the description.
    var showChips = showFullInfo || (!isLocked && progressPercent >= settings.revealEffects);
    var showDescription = showFullInfo || (!isLocked && progressPercent >= settings.revealDescription);
    var showLevel = !isLocked || settings.cheatMode || isEditActive;
    var showFigures = showDescription;
    
    // School badge always visible
    document.getElementById('spell-school').textContent = node.school;
    document.getElementById('spell-school').className = 'school-badge ' + node.school.toLowerCase();

    // Name - progressive reveal (cheat mode shows all)
    if (showName) {
        var nameDisplay = node.name || node.formId;
        if (settings.cheatMode && node.state === 'locked') {
            nameDisplay = (node.name || 'Unknown') + ' [LOCKED]';
        }
        document.getElementById('spell-name').textContent = nameDisplay;
    } else {
        document.getElementById('spell-name').textContent = '???';
    }
    
    // Icon in front of the name: pack picture, else the school emblem, else our glyph
    SpellCard.renderIcon(node, showName);

    // Keyword chips - second thing to open up, after the name
    SpellCard.renderChips(document.getElementById('spell-chips'), node.chips, showChips,
        isLocked ? '???' : '??? (' + settings.revealEffects + '%)');

    // Level is what the node's size already gives away; cost and type are figures
    var levelEl = document.getElementById('spell-level');
    levelEl.textContent = showLevel ? (node.level || '?') : '???';
    if (typeof BridgeView !== 'undefined') BridgeView.bindLevel(levelEl, node, showLevel);
    document.getElementById('spell-cost').textContent = showFigures ? (node.cost || '?') : '???';
    document.getElementById('spell-type').textContent = showFigures ? (node.type || '?') : '???';
    
    // The raw effect list is how the spell is wired, not something a player reads:
    // helper effects, duplicates, internal names. The card's description and chips say
    // what the spell does, so the list only shows while editing the tree.
    var effectsSection = document.getElementById('details-effects-section');
    if (effectsSection) effectsSection.style.display = isEditActive ? '' : 'none';

    // Effects - progressive reveal with weakened info
    var effectsList = document.getElementById('spell-effects');
    effectsList.innerHTML = '';
    
    // Check if spell is weakened (effectiveness < 100%)
    var isWeakened = node.isWeakened === true || (node.effectiveness && node.effectiveness < 100);
    var effectiveness = node.effectiveness || 100;
    
    if (showFigures) {
        // Show effectiveness warning if weakened
        if (isWeakened) {
            var weakenedLi = document.createElement('li');
            weakenedLi.className = 'weakened-warning';
            weakenedLi.textContent = '! ' + tOr('details.weakenedPower', { pct: effectiveness }, '{{pct}}% Power (practicing...)');
            weakenedLi.style.color = '#f59e0b';
            weakenedLi.style.fontWeight = 'bold';
            effectsList.appendChild(weakenedLi);
        }
        
        // Use scaledEffects if available (from C++ for weakened spells)
        var effectsToShow = (isWeakened && node.scaledEffects) ? node.scaledEffects : (Array.isArray(node.effects) ? node.effects : []);
        
        if (effectsToShow.length === 0) {
            var noEffLi = document.createElement('li');
            noEffLi.textContent = tOr('details.noEffects', null, 'No effects');
            effectsList.appendChild(noEffLi);
        } else {
            effectsToShow.forEach(function(e) {
                var li = document.createElement('li');
                if (typeof e === 'string') {
                    li.textContent = e;
                } else if (e.scaledMagnitude !== undefined) {
                    // Scaled effect from C++
                    var text = e.name || 'Effect';
                    if (e.scaledMagnitude > 0) {
                        text += ' (' + e.scaledMagnitude + ')';
                        if (e.originalMagnitude && e.originalMagnitude !== e.scaledMagnitude) {
                            li.title = 'Full power: ' + e.originalMagnitude;
                        }
                    }
                    if (e.duration > 0) {
                        text += ' for ' + e.duration + 's';
                    }
                    li.textContent = text;
                    if (isWeakened) li.style.color = '#fbbf24';
                } else {
                    // Plain effect from C++: name, then magnitude and duration when it has them
                    var plainText = e.name || JSON.stringify(e);
                    if (e.magnitude > 0) plainText += ' (' + Math.round(e.magnitude) + ')';
                    if (e.duration > 0) plainText += ' ' + e.duration + 's';
                    li.textContent = plainText;
                }
                effectsList.appendChild(li);
            });
        }
    } else {
        var hiddenEff = document.createElement('li');
        hiddenEff.className = 'hidden-info';
        hiddenEff.textContent = '??? (' + tOr('details.revealAtPct', { pct: settings.revealDescription }, '{{pct}}% to reveal') + ')';
        effectsList.appendChild(hiddenEff);
    }

    // Description - progressive reveal
    var descEl = document.getElementById('spell-description');
    if (showDescription) {
        descEl.textContent = node.desc || tOr('details.noDescription', null, 'No description.');
    } else if (node.state === 'locked') {
        descEl.textContent = tOr('details.descLocked', null, 'Unlock prerequisites to reveal.');
    } else {
        descEl.textContent = tOr('details.descRevealAt', { pct: settings.revealDescription }, 'Progress to {{pct}}% to reveal description...');
    }

    // Populate prerequisites with hard/soft distinction
    var prereqSummary = document.getElementById('prereq-summary');
    var hardPrereqsSection = document.getElementById('hard-prereqs-section');
    var softPrereqsSection = document.getElementById('soft-prereqs-section');
    var hardPrereqsList = document.getElementById('hard-prereqs-list');
    var softPrereqsList = document.getElementById('soft-prereqs-list');
    var softNeededCount = document.getElementById('soft-needed-count');
    var legacyPrereqList = document.getElementById('spell-prereqs');
    
    // Clear all lists
    if (hardPrereqsList) hardPrereqsList.innerHTML = '';
    if (softPrereqsList) softPrereqsList.innerHTML = '';
    if (legacyPrereqList) legacyPrereqList.innerHTML = '';
    
    // Get hard/soft prereqs from node
    var hardPrereqs = node.hardPrereqs || [];
    var softPrereqs = node.softPrereqs || [];
    var softNeeded = node.softNeeded || 0;
    var hasHardSoftData = hardPrereqs.length > 0 || softPrereqs.length > 0;
    
    // Helper to create prereq list item
    function createPrereqItem(id, isHard, isMet) {
        var n = state.treeData ? _findNodeById(id) : null;
        var li = document.createElement('li');
        var showPrereqName = SpellNames.isShown(n);

        // Check if edit mode is active
        var isEditMode = typeof EditMode !== 'undefined' && EditMode.isActive;

        if (isEditMode) {
            li.classList.add('prereq-edit-item');

            // Name span
            var nameSpan = document.createElement('span');
            nameSpan.className = 'prereq-name';
            nameSpan.textContent = spellDisplayName(id, n, showPrereqName);
            nameSpan.dataset.id = id;
            li.appendChild(nameSpan);

            // Edit controls container
            var controls = document.createElement('span');
            controls.className = 'prereq-edit-controls';

            // Toggle hard/soft button
            var toggleBtn = document.createElement('button');
            toggleBtn.className = 'prereq-toggle-btn ' + (isHard ? 'hard' : 'soft');
            toggleBtn.textContent = isHard ? 'H' : 'S';
            toggleBtn.title = isHard ? 'Hard (required) - click to make Soft' : 'Soft (optional) - click to make Hard';
            toggleBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                if (typeof EditMode !== 'undefined') {
                    EditMode.togglePrereqType(node, id);
                }
            });
            controls.appendChild(toggleBtn);

            // Delete button
            var deleteBtn = document.createElement('button');
            deleteBtn.className = 'prereq-delete-btn';
            deleteBtn.textContent = '×';
            deleteBtn.title = 'Remove prerequisite';
            deleteBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                if (typeof EditMode !== 'undefined') {
                    EditMode.deletePrerequisite(node, id);
                }
            });
            controls.appendChild(deleteBtn);

            li.appendChild(controls);
        } else {
            li.textContent = spellDisplayName(id, n, showPrereqName);
        }

        li.dataset.id = id;
        if (isMet) li.classList.add('prereq-met');
        if (!showPrereqName) li.classList.add('prereq-hidden');  // Not clickable
        return li;
    }
    
    // Check if a prereq is met (node is unlocked/mastered)
    function isPrereqMet(id) {
        var n = state.treeData ? _findNodeById(id) : null;
        return n && n.state === 'unlocked';
    }
    
    if (hasHardSoftData && hardPrereqsSection && softPrereqsSection) {
        // Show hard/soft distinction
        hardPrereqsSection.classList.toggle('hidden', hardPrereqs.length === 0);
        softPrereqsSection.classList.toggle('hidden', softPrereqs.length === 0);
        legacyPrereqList.classList.add('hidden');
        
        // Count met prereqs
        var hardMet = hardPrereqs.filter(isPrereqMet).length;
        var softMet = softPrereqs.filter(isPrereqMet).length;
        
        // Summary text
        if (prereqSummary) {
            var totalHard = hardPrereqs.length;
            var totalSoft = softPrereqs.length;
            if (totalHard === 0 && totalSoft === 0) {
                prereqSummary.textContent = tOr('details.noPrereqs', null, 'No prerequisites');
                prereqSummary.className = 'prereq-summary none';
            } else {
                var parts = [];
                if (totalHard > 0) parts.push(tOr('details.prereqRequiredCount', { met: hardMet, total: totalHard }, '{{met}}/{{total}} required'));
                if (totalSoft > 0) parts.push(tOr('details.prereqOptionalCount', { met: softMet, total: softNeeded }, '{{met}}/{{total}} optional'));
                prereqSummary.textContent = parts.join(' • ');
                prereqSummary.className = 'prereq-summary ' + 
                    (hardMet === totalHard && softMet >= softNeeded ? 'complete' : 'incomplete');
            }
        }
        
        // Populate hard prereqs
        hardPrereqs.forEach(function(id) {
            hardPrereqsList.appendChild(createPrereqItem(id, true, isPrereqMet(id)));
        });
        
        // Update soft needed label (editable in edit mode)
        if (softNeededCount) {
            var isEditMode = typeof EditMode !== 'undefined' && EditMode.isActive;

            if (isEditMode && softPrereqs.length > 0) {
                // Create editable input
                softNeededCount.innerHTML = '';
                var needLabel = document.createTextNode('(need ');
                softNeededCount.appendChild(needLabel);

                var input = document.createElement('input');
                input.type = 'number';
                input.className = 'soft-needed-input';
                input.min = '0';
                input.max = String(softPrereqs.length);
                input.value = String(softNeeded);
                input.addEventListener('change', function() {
                    var newVal = parseInt(this.value) || 0;
                    if (typeof EditMode !== 'undefined') {
                        EditMode.updateSoftNeeded(node, newVal);
                    }
                });
                input.addEventListener('click', function(e) {
                    e.stopPropagation();  // Prevent panel close
                });
                softNeededCount.appendChild(input);

                var ofLabel = document.createTextNode(' of ' + softPrereqs.length + ')');
                softNeededCount.appendChild(ofLabel);
            } else {
                softNeededCount.textContent = '(' + tOr('details.softNeed', { need: softNeeded, total: softPrereqs.length }, 'need {{need}} of {{total}}') + ')';
            }
        }
        
        // Populate soft prereqs
        softPrereqs.forEach(function(id) {
            softPrereqsList.appendChild(createPrereqItem(id, false, isPrereqMet(id)));
        });
    } else {
        // Fallback to legacy display
        hardPrereqsSection.classList.add('hidden');
        softPrereqsSection.classList.add('hidden');
        legacyPrereqList.classList.remove('hidden');
        
        if (prereqSummary) {
            prereqSummary.textContent = node.prerequisites.length > 0
                ? tOr('details.prereqCount', { n: node.prerequisites.length }, '{{n}} prerequisite(s)')
                : tOr('details.noPrereqs', null, 'No prerequisites');
            prereqSummary.className = 'prereq-summary';
        }
        
        node.prerequisites.forEach(function(id) {
            legacyPrereqList.appendChild(createPrereqItem(id, true, isPrereqMet(id)));
        });
    }

    var unlocksList = document.getElementById('spell-unlocks');
    unlocksList.innerHTML = '';
    node.children.forEach(function(id) {
        var n = state.treeData ? _findNodeById(id) : null;
        var li = document.createElement('li');
        // Cheat mode shows all names; a name another spell shares gets its plugin
        li.textContent = spellDisplayName(id, n);
        li.dataset.id = id;
        unlocksList.appendChild(li);
    });

    // Opens with the keyword chips: the paths name another spell and the
    // keywords it shares, which is more than this spell's own card shows yet
    if (typeof BridgeView !== 'undefined') BridgeView.renderCard(node, showChips);

    // === LOCKS (Pre Req Master) ===
    var locksSection = document.getElementById('locks-section');
    var locksList = document.getElementById('locks-list');
    if (locksSection && locksList) {
        locksList.innerHTML = '';
        var locks = (typeof PreReqMaster !== 'undefined' && PreReqMaster.getLocksForNode)
            ? PreReqMaster.getLocksForNode(node.id)
            : [];

        if (locks.length > 0) {
            locksSection.style.display = '';
            locks.forEach(function(lock) {
                var li = document.createElement('li');
                li.className = 'lock-prereq-item';
                var showName = !!(lock.revealed && lock.name);
                // Built as nodes, the name as text: a spell name is never read as HTML
                var lockNode = _findNodeById(lock.nodeId);
                var icon = document.createElement('span');
                icon.className = 'lock-icon';
                icon.textContent = '\u{1F517}';
                var label = document.createElement('span');
                label.className = 'lock-label';
                label.textContent = 'LOCK';
                li.appendChild(icon);
                li.appendChild(document.createTextNode(' ' +
                    (lockNode ? spellDisplayName(lock.nodeId, lockNode, showName) : (showName ? lock.name : '???')) + ' '));
                li.appendChild(label);
                li.dataset.id = lock.nodeId;
                if (lock.revealed && lock.name) {
                    li.addEventListener('click', function() { selectNodeById(lock.nodeId); });
                }
                locksList.appendChild(li);
            });
        } else {
            locksSection.style.display = 'none';
        }
    }

    var stateBadge = document.getElementById('spell-state');
    var stateText = spellStateLabel(node.state);
    var stateClass = node.state;

    // Show "Weakened" for early-learned spells
    if (isWeakened && node.state === 'unlocked') {
        stateText = tOr('details.stateWeakened', { pct: effectiveness }, 'Weakened ({{pct}}%)');
        stateClass = 'weakened';
    }
    
    stateBadge.textContent = stateText;
    stateBadge.className = 'state-badge ' + stateClass;

    // Update progression UI
    updateDetailsProgression(node, opts);

    // The buttons act on state.selectedNode, not on the spell a preview shows.
    // CSS stops the mouse; disabled also stops Enter/Space on a focused button.
    var learnBtnEl = document.getElementById('learn-btn');
    var unlockBtnEl = document.getElementById('unlock-btn');
    if (preview) {
        if (learnBtnEl) learnBtnEl.disabled = true;
        if (unlockBtnEl) unlockBtnEl.disabled = true;
    } else if (learnBtnEl) {
        learnBtnEl.disabled = false;
    }
}
