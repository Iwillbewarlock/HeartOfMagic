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

    // Stand-in drawn when no installed icon pack covers the spell: one glyph per
    // school, in the style the packs use - a flat single-colour silhouette, no
    // outlines, no gradients. These are ours, so they go in as real SVG and take
    // the school colour from the theme through currentColor. Cut-outs rely on
    // fill-rule evenodd.
    _schoolGlyphs: {
        // flame with a hollow core
        destruction: '<path d="M12 1.5c1.2 4.2 6.5 6.6 6.5 12.2a6.5 6.5 0 0 1-13 0c0-2.6 1.2-4.4 2.8-5.8.1 2 .9 3.3 2.2 3.6.4-3.6-.9-6.4 1.5-10zM12 13.6c.6 1.6 2.3 2.4 2.3 4.2a2.3 2.3 0 0 1-4.6 0c0-1.8 1.7-2.6 2.3-4.2z"/>',
        // sun: disc and eight rays
        restoration: '<circle cx="12" cy="12" r="4.3"/>' +
            '<path d="M12 1.2l1.5 4.1h-3z"/><path d="M12 22.8l-1.5-4.1h3z"/>' +
            '<path d="M1.2 12l4.1-1.5v3z"/><path d="M22.8 12l-4.1 1.5v-3z"/>' +
            '<path d="M4.4 4.4l4 1.8-2.2 2.2z"/><path d="M19.6 19.6l-4-1.8 2.2-2.2z"/>' +
            '<path d="M19.6 4.4l-1.8 4-2.2-2.2z"/><path d="M4.4 19.6l1.8-4 2.2 2.2z"/>',
        // cut gem: three facets with a gap between them
        alteration: '<path d="M12 2l7.6 4.6L12 11.2 4.4 6.6z"/>' +
            '<path d="M3.6 8.1l7.6 4.6v9.1l-7.6-4.6z"/>' +
            '<path d="M20.4 8.1l-7.6 4.6v9.1l7.6-4.6z"/>',
        // portal: ring around a four-point star
        conjuration: '<path d="M12 1.5a10.5 10.5 0 1 0 0 21 10.5 10.5 0 0 0 0-21zM12 4.3a7.7 7.7 0 1 1 0 15.4 7.7 7.7 0 0 1 0-15.4z"/>' +
            '<path d="M12 6.6l1.6 3.8 3.8 1.6-3.8 1.6-1.6 3.8-1.6-3.8-3.8-1.6 3.8-1.6z"/>',
        // eye: lid shape, hollow iris, solid pupil
        illusion: '<path d="M1.2 12s4.1-7.2 10.8-7.2S22.8 12 22.8 12s-4.1 7.2-10.8 7.2S1.2 12 1.2 12zM12 7.9a4.1 4.1 0 1 0 0 8.2 4.1 4.1 0 0 0 0-8.2z"/>' +
            '<circle cx="12" cy="12" r="2"/>'
    },

    _schoolGlyphSvg: function(school) {
        var paths = this._schoolGlyphs[String(school || '').toLowerCase()];
        if (!paths) return '';
        return '<svg viewBox="0 0 24 24" fill="currentColor" fill-rule="evenodd" stroke="none">' + paths + '</svg>';
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
