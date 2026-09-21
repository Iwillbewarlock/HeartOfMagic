/**
 * SpellLearning Spell Card Module
 *
 * The keyword line of the spell details card ("Fire - Projectile - Destruction").
 * C++ (SpellScannerChips.cpp) sends stable chip ids derived from the spell's
 * structure; this module only turns them into translated labels.
 *
 * Translation: lang files carry "chips.<id>" keys. A language that has not been
 * updated yet falls back to the English labels below instead of showing raw keys.
 */

var SpellCard = {
    _fallbackLabels: {
        'element.fire': 'Fire',
        'element.frost': 'Frost',
        'element.shock': 'Shock',
        'element.poison': 'Poison',
        'element.disease': 'Disease',
        'form.projectile': 'Projectile',
        'form.lobbed': 'Lobbed',
        'form.beam': 'Beam',
        'form.spray': 'Spray',
        'form.cone': 'Cone',
        'form.self': 'Self',
        'form.touch': 'Touch',
        'form.target': 'Target',
        'form.location': 'Location',
        'area.blast': 'Area',
        'area.hazard': 'Hazard',
        'kind.summon': 'Summon',
        'kind.bound': 'Bound Weapon',
        'kind.cloak': 'Cloak',
        'kind.rune': 'Rune',
        'kind.paralysis': 'Paralysis',
        'kind.invisibility': 'Invisibility',
        'kind.light': 'Light',
        'kind.nightEye': 'Night Eye',
        'kind.detect': 'Detect',
        'kind.telekinesis': 'Telekinesis',
        'kind.reanimate': 'Reanimate',
        'kind.soulTrap': 'Soul Trap',
        'kind.turnUndead': 'Turn Undead',
        'kind.calm': 'Calm',
        'kind.frenzy': 'Frenzy',
        'kind.fear': 'Fear',
        'kind.rally': 'Rally',
        'kind.absorb': 'Absorb',
        'kind.banish': 'Banish',
        'kind.ethereal': 'Ethereal',
        'kind.slowTime': 'Slow Time',
        'kind.lock': 'Lock',
        'kind.open': 'Open',
        'kind.command': 'Command',
        'kind.disarm': 'Disarm',
        'kind.stagger': 'Stagger',
        'kind.dispel': 'Dispel',
        'kind.guide': 'Guide',
        'kind.grab': 'Grab',
        'kind.cure': 'Cure',
        'kind.damage': 'Damage',
        'kind.heal': 'Heal',
        'kind.armor': 'Armor',
        'kind.ward': 'Ward',
        'cast.concentration': 'Concentration',
        'cast.twoHanded': 'Two-Handed',
        'school.alteration': 'Alteration',
        'school.conjuration': 'Conjuration',
        'school.destruction': 'Destruction',
        'school.illusion': 'Illusion',
        'school.restoration': 'Restoration'
    },

    /**
     * @param {string} chipId - e.g. "element.fire"
     * @returns {string} Translated label, English if the language lacks the key
     */
    label: function(chipId) {
        var key = 'chips.' + chipId;
        var translated = (typeof t === 'function') ? t(key) : key;
        if (translated && translated !== key) return translated;
        return this._fallbackLabels[chipId] || chipId;
    },

    // iconKey -> data URI, '' when C++ found nothing, undefined while unknown
    _icons: {},
    _iconWaiting: {},

    /**
     * Icon in front of the name. Icons are not drawn by this mod: they come from the
     * installed icon set (Kome's Inventory Tweaks is the requirement), picked in C++:
     *   iconKey       - the pack's own picture for this spell, else the icon rules
     *                   (card_icons.json: fire cloak, frost rune, summon ...)
     *   schoolIconKey - the plain school emblem
     * While the name is still "???" only the school emblem shows: the school is always
     * visible on the card anyway, anything more specific would give the spell away.
     * Without the icon set there is simply no icon. Pictures go into an <img> as a
     * data URI, so nothing inside a mod's SVG can run as script.
     * @param {Object} node - uses iconKey, schoolIconKey
     * @param {boolean} revealed - true once the name is shown
     */
    renderIcon: function(node, revealed) {
        var img = document.getElementById('spell-icon');
        if (!img || !node) return;

        var key = (revealed && node.iconKey) ? node.iconKey : (node.schoolIconKey || '');
        this._shown = { node: node, revealed: revealed, key: key };

        if (key && this._icons[key]) {
            img.src = this._icons[key];
            img.classList.remove('hidden');
        } else {
            img.classList.add('hidden');
            img.removeAttribute('src');
        }

        if (key && this._icons[key] === undefined && !this._iconWaiting[key] && window.callCpp) {
            this._iconWaiting[key] = true;
            window.callCpp('GetSpellIcon', key);
        }
    },
    /** C++ reply for GetSpellIcon: { key, svg } */
    onIconData: function(data) {
        if (!data || !data.key) return;
        delete this._iconWaiting[data.key];
        this._icons[data.key] = data.svg
            ? 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(data.svg)
            : '';

        // Only repaint if the card is still showing the spell that asked
        if (this._shown && this._shown.key === data.key) {
            this.renderIcon(this._shown.node, this._shown.revealed);
        }
    },
    /**
     * Fill the chip row.
     * @param {HTMLElement} container
     * @param {Array<string>} chips - Chip ids from C++ (node.chips)
     * @param {boolean} revealed - false shows a single "???" chip
     * @param {string} [hiddenText] - Text for the hidden state
     */
    renderChips: function(container, chips, revealed, hiddenText) {
        if (!container) return;
        container.innerHTML = '';

        if (!revealed) {
            var hidden = document.createElement('span');
            hidden.className = 'spell-chip hidden-info';
            hidden.textContent = hiddenText || '???';
            container.appendChild(hidden);
            return;
        }

        var list = Array.isArray(chips) ? chips : [];
        for (var i = 0; i < list.length; i++) {
            var chip = document.createElement('span');
            // "element.fire" -> classes "spell-chip chip-element chip-element-fire"
            var parts = String(list[i]).split('.');
            chip.className = 'spell-chip chip-' + parts[0] + ' chip-' + parts.join('-');
            chip.textContent = this.label(list[i]);
            container.appendChild(chip);
        }
    }
};
