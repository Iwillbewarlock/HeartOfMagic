/**
 * Find Spell (F on the tree): the search window, its list and keyboard
 * navigation, and panning to the chosen spell. Split out of treeViewerUI.js
 * (600-line limit); loads after spellProgressView.js.
 */

// =============================================================================
// FIND SPELL (F key search on spell tree)
// =============================================================================

var _findSpellSelectedIndex = -1;
var _findSpellFiltered = [];

function initializeFindSpell() {
    var modal = document.getElementById('find-spell-modal');
    if (!modal) return;

    var searchInput = document.getElementById('find-spell-search');
    var closeBtn = document.getElementById('find-spell-close');
    var backdrop = modal.querySelector('.modal-backdrop');

    // F key to open
    document.addEventListener('keydown', function(e) {
        // Don't trigger if typing in an input/textarea or if a modal is already open
        var tag = (e.target.tagName || '').toLowerCase();
        if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
        if (e.ctrlKey || e.altKey || e.metaKey) return;

        if (e.key === 'f' || e.key === 'F') {
            // Only when on spell tree tab
            if (state.currentTab !== 'spellTree') return;
            // Don't open if another modal is visible
            var openModals = document.querySelectorAll('.modal:not(.hidden)');
            if (openModals.length > 0) return;

            e.preventDefault();
            openFindSpell();
        }
    });

    // Close handlers
    if (closeBtn) closeBtn.addEventListener('click', closeFindSpell);
    if (backdrop) backdrop.addEventListener('click', closeFindSpell);

    // Search input
    if (searchInput) {
        searchInput.addEventListener('input', function() {
            renderFindSpellList(searchInput.value);
        });

        searchInput.addEventListener('keydown', function(e) {
            if (e.key === 'Escape') {
                e.preventDefault();
                closeFindSpell();
            } else if (e.key === 'ArrowDown') {
                e.preventDefault();
                navigateFindSpell(1);
            } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                navigateFindSpell(-1);
            } else if (e.key === 'Enter') {
                e.preventDefault();
                confirmFindSpell();
            }
        });
    }
}

function openFindSpell() {
    var modal = document.getElementById('find-spell-modal');
    var searchInput = document.getElementById('find-spell-search');
    if (!modal) return;

    modal.classList.remove('hidden');
    _findSpellSelectedIndex = -1;

    if (searchInput) {
        searchInput.value = '';
        searchInput.focus();
    }

    renderFindSpellList('');
}

function closeFindSpell() {
    var modal = document.getElementById('find-spell-modal');
    if (modal) modal.classList.add('hidden');
    _findSpellSelectedIndex = -1;
    _findSpellFiltered = [];
}

function renderFindSpellList(searchTerm) {
    var listEl = document.getElementById('find-spell-list');
    if (!listEl) return;

    searchTerm = (searchTerm || '').toLowerCase().trim();

    // Get all nodes on tree
    var nodes = [];
    if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.nodes && CanvasRenderer.nodes.length > 0) {
        nodes = CanvasRenderer.nodes;
    } else if (state.treeData && state.treeData.rawData && state.treeData.rawData.schools) {
        // Fallback: pull from raw tree data
        var schools = state.treeData.rawData.schools;
        for (var schoolName in schools) {
            var school = schools[schoolName];
            if (school.nodes) {
                school.nodes.forEach(function(n) {
                    nodes.push({
                        formId: n.formId || n.id || n.spellId,
                        name: n.name || n.spellName || n.formId,
                        school: schoolName,
                        level: n.level || n.skillLevel || '',
                        x: n.x,
                        y: n.y
                    });
                });
            }
        }
    }

    // Discovery mode: undiscovered (locked) spells must not be searchable by name
    if (settings.discoveryMode !== false && !settings.cheatMode) {
        nodes = nodes.filter(function(node) { return node.state !== 'locked'; });
    }

    // Filter by search term
    var filtered;
    if (searchTerm) {
        filtered = nodes.filter(function(node) {
            var name = (node.name || '').toLowerCase();
            var school = (node.school || '').toLowerCase();
            var formId = (node.formId || '').toLowerCase();
            return name.indexOf(searchTerm) !== -1 ||
                   school.indexOf(searchTerm) !== -1 ||
                   formId.indexOf(searchTerm) !== -1;
        });
    } else {
        filtered = nodes.slice();
    }

    // Sort: alphabetical by name
    filtered.sort(function(a, b) {
        var na = (a.name || '').toLowerCase();
        var nb = (b.name || '').toLowerCase();
        return na < nb ? -1 : na > nb ? 1 : 0;
    });

    // Limit display
    filtered = filtered.slice(0, 80);
    _findSpellFiltered = filtered;
    _findSpellSelectedIndex = filtered.length > 0 ? 0 : -1;

    if (filtered.length === 0) {
        var emptyEl = document.createElement('div');
        emptyEl.className = 'find-spell-empty';
        emptyEl.textContent = searchTerm
            ? tOr('tree.findNoMatch', null, 'No matching spells on tree')
            : tOr('tree.findNoSpells', null, 'No spells on tree');
        listEl.innerHTML = '';
        listEl.appendChild(emptyEl);
        return;
    }

    listEl.innerHTML = '';
    filtered.forEach(function(node, idx) {
        var item = document.createElement('div');
        item.className = 'find-spell-item' + (idx === 0 ? ' selected' : '');
        item.dataset.index = idx;

        // School color dot
        var color = typeof getOrAssignSchoolColor === 'function'
            ? getOrAssignSchoolColor(node.school)
            : '#888';

        // Highlight matching text in name
        var displayName = node.name || node.formId || '???';
        if (searchTerm) {
            displayName = highlightMatch(displayName, searchTerm);
        }

        // State chip reuses the footer legend labels (already translated)
        var stateKeys = {
            locked: 'footer.legendLocked',
            available: 'footer.legendAvailable',
            learning: 'footer.legendLearning',
            unlocked: 'footer.legendUnlocked'
        };
        var stateLabel = stateKeys[node.state] ? t(stateKeys[node.state]) : '';

        item.innerHTML =
            '<div class="find-spell-dot" style="background:' + color + '"></div>' +
            '<div class="find-spell-text">' +
                '<div class="find-spell-name">' + displayName + '</div>' +
                '<div class="find-spell-info">' +
                    '<span class="find-spell-school" style="color:' + color + '">' + (node.school || '') + '</span>' +
                '</div>' +
            '</div>' +
            (node.level ? '<span class="find-spell-tier">' + node.level + '</span>' : '') +
            (stateLabel ? '<span class="find-spell-state ' + node.state + '">' + stateLabel + '</span>' : '');

        item.addEventListener('click', function() {
            _findSpellSelectedIndex = idx;
            confirmFindSpell();
        });

        listEl.appendChild(item);
    });
}

function highlightMatch(text, term) {
    var lower = text.toLowerCase();
    var idx = lower.indexOf(term);
    if (idx === -1) return text;
    var before = text.substring(0, idx);
    var match = text.substring(idx, idx + term.length);
    var after = text.substring(idx + term.length);
    return before + '<mark>' + match + '</mark>' + after;
}

function navigateFindSpell(direction) {
    if (_findSpellFiltered.length === 0) return;

    var listEl = document.getElementById('find-spell-list');
    if (!listEl) return;

    // Remove current selection
    var items = listEl.querySelectorAll('.find-spell-item');
    if (items[_findSpellSelectedIndex]) {
        items[_findSpellSelectedIndex].classList.remove('selected');
    }

    // Move index
    _findSpellSelectedIndex += direction;
    if (_findSpellSelectedIndex < 0) _findSpellSelectedIndex = _findSpellFiltered.length - 1;
    if (_findSpellSelectedIndex >= _findSpellFiltered.length) _findSpellSelectedIndex = 0;

    // Apply selection & scroll into view
    if (items[_findSpellSelectedIndex]) {
        items[_findSpellSelectedIndex].classList.add('selected');
        items[_findSpellSelectedIndex].scrollIntoView({ block: 'nearest' });
    }
}

function confirmFindSpell() {
    if (_findSpellSelectedIndex < 0 || _findSpellSelectedIndex >= _findSpellFiltered.length) return;

    var node = _findSpellFiltered[_findSpellSelectedIndex];
    closeFindSpell();
    smoothPanToNode(node);
}

/**
 * Smoothly pan the canvas to center on a node and select it.
 */
function smoothPanToNode(targetNode) {
    if (typeof CanvasRenderer === 'undefined') return;

    // Find the actual renderer node (may have more data than our filtered copy)
    var node = targetNode;
    if (CanvasRenderer._nodeMap) {
        var found = CanvasRenderer._nodeMap.get(targetNode.formId || targetNode.id);
        if (found) node = found;
    }

    // Preferred path: TreeCamera handles rotation-aware centering + selection
    if (typeof TreeCamera !== 'undefined' && CanvasRenderer.canvas) {
        CanvasRenderer.selectNodeAndFocus(node, {
            onComplete: function() {
                if (typeof setTreeStatus === 'function') {
                    setTreeStatus('Found: ' + (node.name || node.formId));
                }
            }
        });
        return;
    }

    // Legacy fallback (no TreeCamera): pan only, ignores wheel rotation
    var targetPanX = -node.x * CanvasRenderer.zoom;
    var targetPanY = -node.y * CanvasRenderer.zoom;

    var startPanX = CanvasRenderer.panX;
    var startPanY = CanvasRenderer.panY;
    var duration = 400; // ms
    var startTime = null;

    function easeOutCubic(t) {
        return 1 - Math.pow(1 - t, 3);
    }

    function animate(timestamp) {
        if (!startTime) startTime = timestamp;
        var elapsed = timestamp - startTime;
        var t = Math.min(elapsed / duration, 1);
        var eased = easeOutCubic(t);

        CanvasRenderer.panX = startPanX + (targetPanX - startPanX) * eased;
        CanvasRenderer.panY = startPanY + (targetPanY - startPanY) * eased;
        CanvasRenderer._needsRender = true;

        if (t < 1) {
            requestAnimationFrame(animate);
        } else {
            // Select the node after animation completes
            state.selectedNode = node;
            if (typeof showSpellDetails === 'function') {
                showSpellDetails(node);
            }
            if (typeof setTreeStatus === 'function') {
                setTreeStatus('Found: ' + (node.name || node.formId));
            }
        }
    }

    requestAnimationFrame(animate);
}
