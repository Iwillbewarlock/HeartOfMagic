/** Existing prerequisite editor, separated from the player card. */
function renderSpellPrereqEditor(node) {
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
            toggleBtn.textContent = isHard ? tOr('details.prereqHardShort', null, 'H') : tOr('details.prereqSoftShort', null, 'S');
            if (isHard) setI18n(toggleBtn, 'details.prereqHardTitle', null, 'Required - click to make it optional', 'title');
            else setI18n(toggleBtn, 'details.prereqSoftTitle', null, 'Optional - click to make it required', 'title');
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
            setI18n(deleteBtn, 'details.removePrereq', null, 'Remove prerequisite', 'title');
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
                // A soft list with none needed (a repaired lock) is met already, in C++ and
                // cppCallbacks.js alike: no count that reads as "0 of N"
                if (totalSoft > 0 && softNeeded > 0) parts.push(tOr('details.prereqOptionalCount', { met: softMet, total: softNeeded }, '{{met}}/{{total}} optional'));
                else if (totalSoft > 0) parts.push(tOr('details.prereqOptionalNone', null, 'optional, none needed'));
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
                // "need {{need}} of {{total}}" in the panel's language, the number box where {{need}} goes
                var needParts = tOr('details.softNeed', { total: softPrereqs.length }, 'need {{need}} of {{total}}')
                    .split('{{need}}');
                softNeededCount.appendChild(document.createTextNode('(' + needParts[0]));

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

                softNeededCount.appendChild(document.createTextNode((needParts[1] || '') + ')'));
            } else {
                softNeededCount.textContent = '(' + (softNeeded > 0 ?
                    tOr('details.softNeed', { need: softNeeded, total: softPrereqs.length }, 'need {{need}} of {{total}}') :
                    tOr('details.softNoneNeeded', null, 'none needed')) + ')';
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

}
