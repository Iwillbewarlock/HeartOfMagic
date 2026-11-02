/**
 * SpellResearchMemorySetting - Settings > Spell Tome Learning, under the "Spell
 * Research" line: practice and memory between Heart of Magic and Spell Research.
 *
 * Practice becomes memory: every 15% of a learning target's progress a cast
 * crosses (and 100%) pays a short research session ("Memory per 15% of practice",
 * research minutes) into the spell's Spell Research archetypes. Memory guides
 * practice: casting XP rises by up to "Most practice bonus from memory" (%) with
 * the share of the spell's archetypes remembered at its rank
 * (LibrarianSpellResearchMemory.cpp; docs/librarian/SPELL_RESEARCH.md).
 *
 * Saved as config.spellResearch.memoryExchange = { practiceToMemory, memoryMinutes,
 * memoryToPractice, maxPracticeBoost }. Only that object is written: the C++ save
 * merges objects key by key, so spellResearch.enabled (set by hand) stays. Shown
 * only with Spell Research installed: spellResearchStatus.js shows and hides the
 * rows with its own line. Not in settings presets.
 *
 * Depends on: settings, autoSaveSettings (optional)
 */

var SpellResearchMemorySetting = {

    DEFAULTS: {
        practiceToMemory: true,
        memoryMinutes: 25,
        memoryToPractice: true,
        maxPracticeBoost: 25
    },
    MAX_MINUTES: 240,
    MAX_BOOST: 100,
    ROW_IDS: ['srPracticeToMemoryRow', 'srMemoryMinutesRow', 'srMemoryToPracticeRow', 'srMaxPracticeBoostRow'],

    /** settings.spellResearchMemory, made with the defaults when missing. */
    _mem: function() {
        if (!settings.spellResearchMemory || typeof settings.spellResearchMemory !== 'object') {
            settings.spellResearchMemory = {};
        }
        var m = settings.spellResearchMemory;
        for (var k in this.DEFAULTS) {
            if (this.DEFAULTS.hasOwnProperty(k) && m[k] === undefined) m[k] = this.DEFAULTS[k];
        }
        return m;
    },

    /** A number clamped to 0..max, or the fallback for anything that is not a number. */
    _number: function(value, fallback, max) {
        var v = (typeof value === 'string' && value !== '') ? Number(value) : value;
        if (typeof v !== 'number' || !isFinite(v)) return fallback;
        if (v < 0) return 0;
        return v > max ? max : v;
    },

    init: function() {
        var self = this;
        var toMemory = document.getElementById('srPracticeToMemoryToggle');
        var minutes = document.getElementById('srMemoryMinutesInput');
        var toPractice = document.getElementById('srMemoryToPracticeToggle');
        var boost = document.getElementById('srMaxPracticeBoostInput');

        if (toMemory) {
            toMemory.addEventListener('change', function() {
                self._mem().practiceToMemory = this.checked;
                self.sync();
                self._save();
            });
        }
        if (toPractice) {
            toPractice.addEventListener('change', function() {
                self._mem().memoryToPractice = this.checked;
                self.sync();
                self._save();
            });
        }
        if (minutes) {
            minutes.addEventListener('change', function() {
                self._mem().memoryMinutes = self._number(this.value, self.DEFAULTS.memoryMinutes, self.MAX_MINUTES);
                self.sync();
                self._save();
            });
        }
        if (boost) {
            boost.addEventListener('change', function() {
                self._mem().maxPracticeBoost = self._number(this.value, self.DEFAULTS.maxPracticeBoost, self.MAX_BOOST);
                self.sync();
                self._save();
            });
        }
        this.sync();
    },

    /** Put the saved values on the controls; a number is greyed out while its switch is off. */
    sync: function() {
        var m = this._mem();
        var toMemory = document.getElementById('srPracticeToMemoryToggle');
        var minutes = document.getElementById('srMemoryMinutesInput');
        var minutesRow = document.getElementById('srMemoryMinutesRow');
        var toPractice = document.getElementById('srMemoryToPracticeToggle');
        var boost = document.getElementById('srMaxPracticeBoostInput');
        var boostRow = document.getElementById('srMaxPracticeBoostRow');

        if (toMemory) toMemory.checked = m.practiceToMemory !== false;
        if (toPractice) toPractice.checked = m.memoryToPractice !== false;
        if (minutes) {
            minutes.value = m.memoryMinutes;
            minutes.disabled = m.practiceToMemory === false;
        }
        if (boost) {
            boost.value = m.maxPracticeBoost;
            boost.disabled = m.memoryToPractice === false;
        }
        if (minutesRow && minutesRow.classList) minutesRow.classList.toggle('disabled-controls', m.practiceToMemory === false);
        if (boostRow && boostRow.classList) boostRow.classList.toggle('disabled-controls', m.memoryToPractice === false);
    },

    /** Show or hide the rows (with the Spell Research line: shown only when the plugin reports SR). */
    show: function(visible) {
        for (var i = 0; i < this.ROW_IDS.length; i++) {
            var row = document.getElementById(this.ROW_IDS[i]);
            if (row) row.style.display = visible ? '' : 'none';
        }
    },

    /** Read the saved settings from a config, keeping the defaults for what is missing or wrong. */
    loadFrom: function(data) {
        var d = this.DEFAULTS;
        var section = (data && data.spellResearch && typeof data.spellResearch === 'object') ? data.spellResearch : {};
        var src = (section.memoryExchange && typeof section.memoryExchange === 'object') ? section.memoryExchange : {};
        settings.spellResearchMemory = {
            practiceToMemory: typeof src.practiceToMemory === 'boolean' ? src.practiceToMemory : d.practiceToMemory,
            memoryMinutes: this._number(typeof src.memoryMinutes === 'number' ? src.memoryMinutes : null,
                d.memoryMinutes, this.MAX_MINUTES),
            memoryToPractice: typeof src.memoryToPractice === 'boolean' ? src.memoryToPractice : d.memoryToPractice,
            maxPracticeBoost: this._number(typeof src.maxPracticeBoost === 'number' ? src.maxPracticeBoost : null,
                d.maxPracticeBoost, this.MAX_BOOST)
        };
        this.sync();
    },

    /** Write the settings into a config: spellResearch.memoryExchange only, never spellResearch as a whole. */
    saveTo: function(config) {
        var m = this._mem();
        if (!config.spellResearch || typeof config.spellResearch !== 'object') config.spellResearch = {};
        config.spellResearch.memoryExchange = {
            practiceToMemory: m.practiceToMemory !== false,
            memoryMinutes: this._number(m.memoryMinutes, this.DEFAULTS.memoryMinutes, this.MAX_MINUTES),
            memoryToPractice: m.memoryToPractice !== false,
            maxPracticeBoost: this._number(m.maxPracticeBoost, this.DEFAULTS.maxPracticeBoost, this.MAX_BOOST)
        };
        return config;
    },

    reset: function() {
        settings.spellResearchMemory = JSON.parse(JSON.stringify(this.DEFAULTS));
        this.sync();
    },

    _save: function() {
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
    }
};

window.SpellResearchMemorySetting = SpellResearchMemorySetting;
