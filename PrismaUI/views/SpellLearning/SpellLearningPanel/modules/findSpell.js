/**
 * Find Spell (the magnifier button on the tree's tool bar): a dropdown under
 * the button with a search field and the matching spells, keyboard navigation,
 * and panning to the chosen spell. Split out of treeViewerUI.js (600-line
 * limit); loads after spellProgressView.js and spellSearch.js.
 *
 * The button shows the dropdown and hides it again; Escape in the field and
 * choosing a spell hide it too, and so do leaving the tree tab, closing the
 * panel and loading a tree (dismissFindSpell): a dropdown left open out of
 * sight took the next Escape. Which spells match and in what order is
 * SpellSearch (spellSearch.js): shown name, school (English or translated) or
 * a formId-like text, ignoring case and spaces, exact and prefix name matches
 * first. The highlight ignores spaces the same way.
 */

// =============================================================================
// FIND SPELL
// =============================================================================

var _findSpellSelectedIndex = -1;
var _findSpellFiltered = [];
// Typing waits this long before the list is made again (a tree has ~1,500 spells)
var FIND_SPELL_DEBOUNCE_MS = 80;
var _findSpellTimer = null;

function initializeFindSpell() {
    var searchInput = document.getElementById('find-spell-search');
    if (!searchInput) return;

    searchInput.addEventListener('input', function() {
        if (_findSpellTimer) clearTimeout(_findSpellTimer);
        _findSpellTimer = setTimeout(function() {
            _findSpellTimer = null;
            renderFindSpellList(searchInput.value);
        }, FIND_SPELL_DEBOUNCE_MS);
    });

    searchInput.addEventListener('keydown', function(e) {
        if (e.key === 'Escape') {
            // Used here: the panel's Escape handler sees defaultPrevented and lets it be
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
            if (_findSpellTimer) {
                clearTimeout(_findSpellTimer);
                _findSpellTimer = null;
                renderFindSpellList(searchInput.value);
            }
            confirmFindSpell();
        }
    });
}

/** Shown: not hidden, and on the tree tab (a dropdown out of sight is not open) */
function isFindSpellOpen() {
    var panel = document.getElementById('find-spell-panel');
    if (!panel || panel.classList.contains('hidden')) return false;
    return !(typeof state !== 'undefined' && state && state.currentTab && state.currentTab !== 'spellTree');
}

/**
 * Hide the dropdown if it is up, whatever tab is in front: the tab switch,
 * the panel closing (button, hotkey, Escape) and a tree load call this.
 */
function dismissFindSpell() {
    var panel = document.getElementById('find-spell-panel');
    if (panel && !panel.classList.contains('hidden')) closeFindSpell();
}

/** The magnifier button: show the dropdown, or hide it when it is showing */
function toggleFindSpell() {
    if (isFindSpellOpen()) closeFindSpell();
    else openFindSpell();
}

function openFindSpell() {
    var panel = document.getElementById('find-spell-panel');
    var searchInput = document.getElementById('find-spell-search');
    if (!panel) return;

    panel.classList.remove('hidden');
    if (typeof TreeNav !== 'undefined') TreeNav.placeUnder(panel, document.getElementById('find-spell-btn'));
    var btn = document.getElementById('find-spell-btn');
    if (btn) btn.classList.add('active');
    _findSpellSelectedIndex = -1;

    if (searchInput) {
        searchInput.value = '';
        searchInput.focus();
    }
    renderFindSpellList('');
}

function closeFindSpell() {
    var panel = document.getElementById('find-spell-panel');
    if (panel) panel.classList.add('hidden');
    var btn = document.getElementById('find-spell-btn');
    if (btn) btn.classList.remove('active');
    var searchInput = document.getElementById('find-spell-search');
    if (searchInput && document.activeElement === searchInput) searchInput.blur();
    if (_findSpellTimer) {
        clearTimeout(_findSpellTimer);
        _findSpellTimer = null;
    }
    _findSpellSelectedIndex = -1;
    _findSpellFiltered = [];
}

function renderFindSpellList(searchTerm) {
    var listEl = document.getElementById('find-spell-list');
    if (!listEl) return;

    var hits = SpellSearch.search(null, searchTerm);
    _findSpellFiltered = hits.map(function(h) { return h.node; });
    _findSpellSelectedIndex = hits.length > 0 ? 0 : -1;
    listEl.innerHTML = '';

    if (hits.length === 0) {
        var emptyEl = document.createElement('div');
        emptyEl.className = 'find-spell-empty';
        emptyEl.textContent = SpellSearch.fold(searchTerm)
            ? tOr('tree.findNoMatch', null, 'No matching spells on tree')
            : tOr('tree.findNoSpells', null, 'No spells on tree');
        listEl.appendChild(emptyEl);
        return;
    }

    // State chip reuses the footer legend labels (already translated)
    var stateKeys = {
        locked: 'footer.legendLocked',
        available: 'footer.legendAvailable',
        learning: 'footer.legendLearning',
        unlocked: 'footer.legendUnlocked'
    };

    hits.forEach(function(hit, idx) {
        var node = hit.node;
        var item = document.createElement('div');
        item.className = 'find-spell-item' + (idx === 0 ? ' selected' : '');
        item.dataset.index = idx;

        var color = typeof getOrAssignSchoolColor === 'function' ? getOrAssignSchoolColor(node.school) : '#888';

        var dot = document.createElement('div');
        dot.className = 'find-spell-dot';
        dot.style.background = color;
        item.appendChild(dot);

        var text = document.createElement('div');
        text.className = 'find-spell-text';
        var nameEl = document.createElement('div');
        nameEl.className = 'find-spell-name';
        appendHighlighted(nameEl, hit.name || '???', searchTerm);
        text.appendChild(nameEl);

        var info = document.createElement('div');
        info.className = 'find-spell-info';
        var schoolEl = document.createElement('span');
        schoolEl.className = 'find-spell-school';
        schoolEl.style.color = color;
        schoolEl.textContent = SpellSearch.schoolLabel(node.school);
        info.appendChild(schoolEl);
        text.appendChild(info);
        item.appendChild(text);

        if (node.level) {
            var tier = document.createElement('span');
            tier.className = 'find-spell-tier';
            tier.textContent = node.level;
            item.appendChild(tier);
        }
        if (stateKeys[node.state]) {
            var chip = document.createElement('span');
            chip.className = 'find-spell-state ' + node.state;
            chip.textContent = t(stateKeys[node.state]);
            item.appendChild(chip);
        }

        item.addEventListener('click', function() {
            _findSpellSelectedIndex = idx;
            confirmFindSpell();
        });
        listEl.appendChild(item);
    });
}

/** The text into el, the first place it holds term in <mark> (case and spaces ignored) */
function appendHighlighted(el, text, term) {
    var range = SpellSearch.matchRange(text, term);
    if (!range) {
        el.textContent = text;
        return;
    }
    el.appendChild(document.createTextNode(text.substring(0, range[0])));
    var mark = document.createElement('mark');
    mark.textContent = text.substring(range[0], range[1]);
    el.appendChild(mark);
    el.appendChild(document.createTextNode(text.substring(range[1])));
}

function navigateFindSpell(direction) {
    if (_findSpellFiltered.length === 0) return;

    var listEl = document.getElementById('find-spell-list');
    if (!listEl) return;

    var items = listEl.querySelectorAll('.find-spell-item');
    if (items[_findSpellSelectedIndex]) {
        items[_findSpellSelectedIndex].classList.remove('selected');
    }

    _findSpellSelectedIndex += direction;
    if (_findSpellSelectedIndex < 0) _findSpellSelectedIndex = _findSpellFiltered.length - 1;
    if (_findSpellSelectedIndex >= _findSpellFiltered.length) _findSpellSelectedIndex = 0;

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

/** The tree status line after a jump: "Found: <name>" in the panel's language */
function _findSpellFoundStatus(node) {
    if (typeof setTreeStatus !== 'function') return;
    setTreeStatusKey('tree.findFound', { name: node.name || '???' }, 'Found: {{name}}');
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
            onComplete: function() { _findSpellFoundStatus(node); }
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

    function easeOutCubic(x) {
        return 1 - Math.pow(1 - x, 3);
    }

    function animate(timestamp) {
        if (!startTime) startTime = timestamp;
        var elapsed = timestamp - startTime;
        var p = Math.min(elapsed / duration, 1);
        var eased = easeOutCubic(p);

        CanvasRenderer.panX = startPanX + (targetPanX - startPanX) * eased;
        CanvasRenderer.panY = startPanY + (targetPanY - startPanY) * eased;
        CanvasRenderer._needsRender = true;

        if (p < 1) {
            requestAnimationFrame(animate);
        } else {
            // Select the node after animation completes
            state.selectedNode = node;
            if (typeof showSpellDetails === 'function') {
                showSpellDetails(node);
            }
            _findSpellFoundStatus(node);
        }
    }

    requestAnimationFrame(animate);
}
