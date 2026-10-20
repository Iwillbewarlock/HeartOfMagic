/** Compact card presentation; the existing handlers remain the action authority. */
var SpellCardLayout = {
    MAX_PERCENT: 100,
    _nodeId: null,
    _unlocksOpen: false,

    init: function() {
        var panel = document.getElementById('details-panel');
        if (!panel || document.getElementById('card-requirements')) return;
        panel.classList.add('compact-card');
        var header = panel.querySelector('.details-header');
        var meta = document.createElement('div');
        meta.className = 'card-meta';
        header.appendChild(meta);
        ['spell-school', 'spell-level', 'spell-cost'].forEach(function(id) {
            var el = document.getElementById(id);
            if (!el) return;
            if (id === 'spell-school') meta.appendChild(el);
            else meta.appendChild(el.parentNode);
        });
        var description = document.getElementById('details-desc-section');
        header.appendChild(description);
        var progress = document.getElementById('progress-section');
        var line = document.createElement('div');
        line.id = 'card-state-line';
        line.className = 'card-state-line';
        progress.insertBefore(line, progress.firstChild);
        panel.insertBefore(progress, panel.querySelector('.details-body'));
        var requirements = document.createElement('div');
        requirements.id = 'card-requirements';
        requirements.className = 'card-requirements';
        panel.insertBefore(requirements, panel.querySelector('.details-body'));
        var reason = document.createElement('span');
        reason.id = 'card-disabled-reason';
        reason.className = 'card-disabled-reason';
        panel.querySelector('.details-footer').appendChild(reason);
        var section = document.getElementById('details-unlocks-section');
        var oldLabel = section.querySelector('.detail-label');
        if (oldLabel) oldLabel.style.display = 'none';
        var toggle = document.createElement('button');
        toggle.type = 'button';
        toggle.id = 'card-unlocks-toggle';
        toggle.className = 'card-unlocks-toggle';
        toggle.setAttribute('aria-controls', 'spell-unlocks');
        toggle.onclick = function() {
            SpellCardLayout._unlocksOpen = !SpellCardLayout._unlocksOpen;
            SpellCardLayout.foldUnlocks(SpellCardLayout._childCount);
        };
        section.insertBefore(toggle, section.firstChild);
        panel.querySelector('.details-footer').appendChild(section);
    },

    foldUnlocks: function(count) {
        var button = document.getElementById('card-unlocks-toggle');
        var list = document.getElementById('spell-unlocks');
        if (!button || !list) return;
        this._childCount = count;
        button.textContent = tOr(count === 1 ? 'card.laterOne' : 'card.laterMany', { count: count },
            count === 1 ? 'Leads to 1 later spell' : 'Leads to {{count}} later spells') +
            (this._unlocksOpen ? ' \u25BE' : ' \u25B8');
        button.setAttribute('aria-expanded', String(this._unlocksOpen));
        list.classList.toggle('hidden', !this._unlocksOpen);
        document.getElementById('details-unlocks-section').classList.toggle('hidden', !count);
    },

    render: function(node, editing, preview) {
        this.init();
        var panel = document.getElementById('details-panel');
        if (!panel) return;
        panel.classList.toggle('card-editing', !!editing);
        panel.classList.toggle('card-cheating', !!settings.cheatMode);
        if (this._nodeId !== node.id) this._unlocksOpen = false;
        this._nodeId = node.id;
        this.foldUnlocks((node.children || []).length);
        // All placements use the same grouping; edit mode keeps the original editor.
        SpellRequirements.render(node, editing);
        this.syncProgress(node, { preview: preview });
    },

    syncProgress: function(node, opts) {
        var line = document.getElementById('card-state-line');
        if (!line) return;
        var preview = !!(opts && opts.preview);
        if (!preview && typeof DetailsPeek !== 'undefined' && DetailsPeek.isPeeking() && node === state.selectedNode) return;
        var id = typeof getCanonicalFormId === 'function' ? getCanonicalFormId(node) : node.formId;
        var progress = state.spellProgress[id] || { xp: 0 };
        var required = getRequiredXPForNode(node);
        var percent = node.state === 'unlocked' ? this.MAX_PERCENT :
            Math.min(this.MAX_PERCENT, Math.max(0, required > 0 ? (progress.xp || 0) / required * this.MAX_PERCENT : 0));
        var power = spellWeakenedPower(node);
        line.textContent = spellStateLabel(node.state) + ' \u00B7 ' + Math.round(percent) + '% ' +
            tOr('details.xpUnit', null, 'XP') + (power === null ? '' : ' \u00B7 ' +
                tOr('card.castsPower', { pct: power }, 'casts at {{pct}}% power'));
        line.classList.toggle('weakened', power !== null);
        document.getElementById('progress-section').classList.remove('hidden');
        document.getElementById('progress-bar').style.width = percent + '%';
        var learn = document.getElementById('learn-btn');
        var unlock = document.getElementById('unlock-btn');
        var reason = document.getElementById('card-disabled-reason');
        reason.textContent = '';
        learn.disabled = preview;
        if (preview) {
            unlock.disabled = true;
            reason.textContent = tOr('details.peekHint', null, 'Click the spell to select it');
        } else if (!settings.cheatMode && node.state === 'locked') {
            learn.classList.remove('hidden');
            learn.disabled = true;
            learn.textContent = tOr('details.learnThis', null, 'Learn This');
            reason.textContent = tOr('card.finishRequirements', null, 'Meet the requirements above to learn this spell.');
        } else if (!settings.cheatMode && node.state === 'unlocked') {
            unlock.classList.remove('hidden');
            unlock.disabled = true;
            unlock.textContent = tOr('details.unlockSpell', null, 'Unlock Spell');
            reason.textContent = tOr('card.mastered', null, 'This spell is already mastered.');
        }
    }
};
