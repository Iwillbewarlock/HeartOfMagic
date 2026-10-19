/**
 * SpellResearchStatus - Settings > Spell Tome Learning > "Spell Research": what
 * Heart of Magic last did with Spell Research's spell lists.
 *
 * Spell Research (SR) lists only the spells its import quests name (the base
 * game, Dawnguard, Dragonborn). Heart of Magic adds the rest of the tome spells
 * to SR's lists itself when SR's Import ends and at every save load
 * (LibrarianSpellResearchPatch.cpp), so they never show in SR's own import
 * messages. This line says that it happened. The plugin sends its state when
 * the panel opens and whenever a run changes it (window.onSpellResearchStatus,
 * UIManager::NotifySpellResearchStatus); without SR the row stays hidden.
 *
 * Depends on: t() (i18n.js); languageSetting.js redraws it after a language switch
 */

var SpellResearchStatus = {

    // state -> [language key, English text]
    TEXTS: {
        notImported: ['settings.tomeLearning.srNotImported',
            'Not imported yet. Press Import Spells in the Spell Research MCM: the spells of your other mods go in right after it.'],
        off: ['settings.tomeLearning.srOff', 'Turned off in config.json (spellResearch).'],
        noCatalog: ['settings.tomeLearning.srNoCatalog', 'Waiting for a full scan: run one and the spells go in.'],
        upToDate: ['settings.tomeLearning.srUpToDate',
            'All tome spells are in Spell Research ({{total}} spells, checked at {{time}}).'],
        added: ['settings.tomeLearning.srAdded',
            '{{added}} spells added at {{time}} ({{total}} spells in Spell Research).'],
        // One spell added: its own line ("1 spell"), picked by text()
        addedOne: ['settings.tomeLearning.srAddedOne',
            '1 spell added at {{time}} ({{total}} spells in Spell Research).']
    },

    _last: null,

    /** The plugin's state, as JSON text or an object. */
    receive: function(payload) {
        var data = payload;
        if (typeof payload === 'string') {
            try { data = JSON.parse(payload); } catch (e) { data = null; }
        }
        this._last = (data && typeof data === 'object' && this.TEXTS[data.state]) ? data : null;
        this.render();
    },

    /**
     * The line's text for a state, in the panel's language. One spell added has its
     * own line; a language without it gets its plural line before English.
     */
    text: function(data) {
        var entry = (data.state === 'added' && data.added === 1) ? this.TEXTS.addedOne : this.TEXTS[data.state];
        var params = { added: data.added || 0, total: data.srSpells || 0, time: data.time || '' };
        var text = (typeof t === 'function') ? t(entry[0], params) : entry[0];
        if (text === entry[0] && entry === this.TEXTS.addedOne && typeof t === 'function') {
            var plural = t(this.TEXTS.added[0], params);
            if (plural !== this.TEXTS.added[0]) return plural;
        }
        if (text === entry[0]) {
            text = entry[1];
            for (var name in params) {
                if (params.hasOwnProperty(name)) text = text.split('{{' + name + '}}').join(String(params[name]));
            }
        }
        return text;
    },

    render: function() {
        var row = document.getElementById('spellResearchStatusRow');
        var out = document.getElementById('spellResearchStatusText');
        if (!row || !out) return;
        if (!this._last) {
            row.style.display = 'none';
            return;
        }
        out.textContent = this.text(this._last);
        row.style.display = '';
    }
};

window.SpellResearchStatus = SpellResearchStatus;
window.onSpellResearchStatus = function(payload) { SpellResearchStatus.receive(payload); };
