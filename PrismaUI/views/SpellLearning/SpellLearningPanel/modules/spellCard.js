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
     * Show the icon an installed icon pack has for this spell, in front of the name.
     * The picture goes into an <img> as a data URI, so nothing inside a mod's SVG
     * can run as script.
     * @param {HTMLImageElement} img
     * @param {string} iconKey - node.iconKey from C++, '' when there is no icon
     * @param {boolean} revealed - icons follow the name: hidden while it is "???"
     */
    renderIcon: function(img, iconKey, revealed) {
        if (!img) return;
        img.dataset.iconKey = (revealed && iconKey) ? iconKey : '';

        if (!revealed || !iconKey || this._icons[iconKey] === '') {
            img.classList.add('hidden');
            img.removeAttribute('src');
            return;
        }

        if (this._icons[iconKey]) {
            img.src = this._icons[iconKey];
            img.classList.remove('hidden');
            return;
        }

        // Not fetched yet: stay hidden until updateSpellIcon comes back
        img.classList.add('hidden');
        if (!this._iconWaiting[iconKey] && window.callCpp) {
            this._iconWaiting[iconKey] = true;
            window.callCpp('GetSpellIcon', iconKey);
        }
    },

    /** C++ reply for GetSpellIcon: { key, svg } */
    onIconData: function(data) {
        if (!data || !data.key) return;
        delete this._iconWaiting[data.key];
        this._icons[data.key] = data.svg
            ? 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(data.svg)
            : '';

        // Only paint it if the card is still showing the spell that asked
        var img = document.getElementById('spell-icon');
        if (img && img.dataset.iconKey === data.key) {
            this.renderIcon(img, data.key, true);
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
