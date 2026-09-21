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

    // Stand-in drawn when no installed icon pack covers the spell: one plain glyph
    // per school. These are ours, so they go in as real SVG and take the school
    // colour from the theme through currentColor.
    _schoolGlyphs: {
        destruction: '<path d="M12 2.5c.8 3.6 5 5.6 5 10.3a5 5 0 0 1-10 0c0-2 .9-3.2 2-4.2 0 1.9.9 3 2 3 .2-3.6-1-5.7 1-9.1z"/>',
        restoration: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1M18.7 5.3l-2.1 2.1M7.4 16.6l-2.1 2.1"/>',
        alteration: '<path d="M12 2.5l8 5.5v8l-8 5.5-8-5.5v-8z"/><path d="M4 8l8 5 8-5M12 13v8.5"/>',
        conjuration: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="1" fill="currentColor"/>',
        illusion: '<path d="M2.5 12s3.8-6.5 9.5-6.5 9.5 6.5 9.5 6.5-3.8 6.5-9.5 6.5S2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>'
    },

    _schoolGlyphSvg: function(school) {
        var paths = this._schoolGlyphs[String(school || '').toLowerCase()];
        if (!paths) return '';
        return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6"' +
               ' stroke-linecap="round" stroke-linejoin="round">' + paths + '</svg>';
    },

    /**
     * Icon in front of the name. An installed icon pack's picture when there is one
     * and the name is revealed; otherwise the school glyph, so the slot is never
     * empty. The school is always visible on the card anyway, so the glyph gives
     * nothing away. Pack pictures go into an <img> as a data URI, so nothing inside
     * a mod's SVG can run as script.
     * @param {string} iconKey - node.iconKey from C++, '' when there is no pack icon
     * @param {string} school - node.school
     * @param {boolean} revealed - pack icons follow the name: held back while it is "???"
     */
    renderIcon: function(iconKey, school, revealed) {
        var img = document.getElementById('spell-icon');
        var glyph = document.getElementById('spell-school-glyph');
        if (!img || !glyph) return;

        var wantsPack = !!(revealed && iconKey);
        img.dataset.iconKey = wantsPack ? iconKey : '';

        var packReady = wantsPack && !!this._icons[iconKey];
        if (packReady) {
            img.src = this._icons[iconKey];
            img.classList.remove('hidden');
        } else {
            img.classList.add('hidden');
            img.removeAttribute('src');
        }

        // Glyph whenever the pack picture is not on screen (none, held back, or still loading)
        var glyphSvg = packReady ? '' : this._schoolGlyphSvg(school);
        glyph.innerHTML = glyphSvg;
        glyph.className = 'spell-icon school-glyph ' + String(school || '').toLowerCase() + (glyphSvg ? '' : ' hidden');
        glyph.dataset.school = school || '';

        if (wantsPack && this._icons[iconKey] === undefined && !this._iconWaiting[iconKey] && window.callCpp) {
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

        // Only repaint if the card is still showing the spell that asked
        var img = document.getElementById('spell-icon');
        var glyph = document.getElementById('spell-school-glyph');
        if (img && img.dataset.iconKey === data.key) {
            this.renderIcon(data.key, glyph ? glyph.dataset.school : '', true);
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
