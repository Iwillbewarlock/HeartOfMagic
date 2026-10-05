/**
 * GateReview - the spells the spell gate is not sure about, in the blacklist window
 * (docs/librarian/ToDo.md M9, SpellScannerGate.cpp).
 *
 * The scan marks a tome spell gate.band "review" with its reasons: a horse summon,
 * a named unique summon, a cheap script-only spell, a review rule. Those stay in
 * the tree; this list shows them with the reasons so the player can leave one out
 * (it goes to the spell blacklist, which the builder and Spell Research both read)
 * or keep it (it leaves the list: settings.gateReviewed, plugin:localFormId keys).
 * "out" spells never reach the tree, so they are not listed.
 *
 * Depends on: state.lastSpellData, settings (spellBlacklist, gateReviewed),
 *   isTaughtByTome (proceduralTreeBuilder.js), getLocalFormId, addToBlacklist,
 *   autoSaveSettings (settingsPanel.js), tOr (i18n.js)
 */

var GateReview = {

    REASON_KEYS: {
        horseSummon: ['modals.gateReview.reason.horseSummon', 'summons a horse'],
        uniqueSummon: ['modals.gateReview.reason.uniqueSummon', 'summons one named character'],
        cheapScript: ['modals.gateReview.reason.cheapScript', 'a cheap spell that only runs a script'],
        rule: ['modals.gateReview.reason.rule', 'matches a review rule']
    },

    /** "plugin.esp:localid", lowercase plugin: the key blacklist entries use too */
    key: function(spell) {
        var local = typeof getLocalFormId === 'function' ? getLocalFormId(spell.formId) : spell.formId;
        return String(spell.plugin || '').toLowerCase() + ':' + local;
    },

    _blacklisted: function(spell, list) {
        var key = this.key(spell);
        for (var i = 0; i < list.length; i++) {
            var e = list[i];
            if (e.plugin && e.localFormId && (String(e.plugin).toLowerCase() + ':' + e.localFormId) === key) return true;
            if (e.formId === spell.formId) return true;
        }
        return false;
    },

    /** The spells to list: in the tree's reach, marked review, not blacklisted, not kept already */
    pending: function(spells, blacklist, reviewed) {
        var kept = {};
        for (var r = 0; r < (reviewed || []).length; r++) kept[reviewed[r]] = true;
        var out = [];
        for (var i = 0; i < (spells || []).length; i++) {
            var s = spells[i];
            if (!s || !s.gate || s.gate.band !== 'review') continue;
            if (typeof isTaughtByTome === 'function' && !isTaughtByTome(s)) continue;
            if (kept[this.key(s)] || this._blacklisted(s, blacklist || [])) continue;
            out.push(s);
        }
        return out;
    },

    _t: function(key, fallback) {
        return typeof tOr === 'function' ? tOr(key, null, fallback) : fallback;
    },

    /** One reason in the panel's language */
    reasonText: function(reason) {
        var name = String(reason).indexOf('rule:') === 0 ? 'rule' : reason;
        var entry = this.REASON_KEYS[name];
        return entry ? this._t(entry[0], entry[1]) : String(reason);
    },

    render: function() {
        var box = document.getElementById('gate-review-entries');
        var count = document.getElementById('gate-review-count');
        if (!box) return;
        var spells = (state.lastSpellData && state.lastSpellData.spells) || [];
        var list = this.pending(spells, settings.spellBlacklist, settings.gateReviewed);
        if (count) count.textContent = list.length;
        box.innerHTML = '';
        if (list.length === 0) {
            var empty = document.createElement('div');
            empty.className = 'blacklist-empty';
            empty.textContent = this._t('modals.gateReview.empty', 'Nothing to look over');
            box.appendChild(empty);
            return;
        }
        var self = this;
        list.forEach(function(spell) {
            var row = document.createElement('div');
            row.className = 'blacklist-entry';
            var info = document.createElement('div');
            info.className = 'blacklist-entry-info';
            var name = document.createElement('div');
            name.className = 'blacklist-entry-name';
            name.textContent = spell.name || '???';
            var why = document.createElement('div');
            why.className = 'blacklist-entry-school';
            why.textContent = (spell.plugin || '') + ' - ' +
                (spell.gate.reasons || []).map(function(r) { return self.reasonText(r); }).join(', ');
            info.appendChild(name);
            info.appendChild(why);
            var keep = document.createElement('button');
            keep.className = 'btn btn-secondary btn-sm';
            keep.textContent = self._t('modals.gateReview.keep', 'Keep');
            keep.addEventListener('click', function() { self.keep(spell); });
            var drop = document.createElement('button');
            drop.className = 'btn btn-secondary btn-sm';
            drop.textContent = self._t('modals.gateReview.exclude', 'Leave out');
            drop.addEventListener('click', function() { self.exclude(spell); });
            row.appendChild(info);
            row.appendChild(keep);
            row.appendChild(drop);
            box.appendChild(row);
        });
    },

    keep: function(spell) {
        if (!settings.gateReviewed) settings.gateReviewed = [];
        var key = this.key(spell);
        if (settings.gateReviewed.indexOf(key) === -1) settings.gateReviewed.push(key);
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
        this.render();
    },

    /** addToBlacklist repaints both lists (renderBlacklistEntries) */
    exclude: function(spell) {
        if (typeof addToBlacklist === 'function') addToBlacklist(spell);
    }
};

window.GateReview = GateReview;
