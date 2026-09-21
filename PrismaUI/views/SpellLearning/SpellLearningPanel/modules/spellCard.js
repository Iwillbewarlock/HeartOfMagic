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

    // Last resort, when neither an icon pack nor Wheeler's standard emblems are
    // installed: our own drawing of each school's vanilla emblem - the tree, the
    // Oblivion sigil, the flame over an orb, the three rings, the phoenix - as the
    // flat single-colour silhouettes the packs use. They take the school colour
    // from the theme through currentColor; cut-outs rely on fill-rule evenodd.
    _schoolGlyphs: {
        alteration:
            '<path d="M10.4 22l1-4.6V11h1.6v6.4l1 4.6h-1.3l-.5-1.4-.5 1.4z"/>' +
            '<path d="M12.6 12V3.8a3 3 0 0 1 5.4 1.3 2.9 2.9 0 0 1 1.5 5.2 2.9 2.9 0 0 1-4.9 1.7z"/>' +
            '<path d="M11.6 12.2L6.8 9l-.5-3.4h1.1l.4 2.6 1.7 1.1V5.2h1.1v4.9l1 .7z"/>' +
            '<path d="M7.7 9.9L3.8 9.2l-.3-1.1 3.6.6z"/>',
        conjuration:
            '<path d="M7.2 1.8c-.5 5.2.3 9.3 2.1 13.3.9 2.1 1.8 4.5 2.7 7.1.9-2.6 1.8-5 2.7-7.1 1.8-4 2.6-8.1 2.1-13.3-.9 4.7-1.8 7.8-3 10.5-.6 1.4-1.2 2.8-1.8 4.4-.6-1.6-1.2-3-1.8-4.4-1.2-2.7-2.1-5.8-3-10.5z"/>' +
            '<path d="M12 6.8a2.5 2.5 0 1 1-2.5 2.5h1.3A1.2 1.2 0 1 0 12 8.1z"/>',
        destruction:
            '<path d="M12 1.5c.9 3 2.6 4.6 2.6 7.6 0 1.4-.5 2.5-1.2 3.4h-2.8c-.7-.9-1.2-2-1.2-3.4 0-3 1.7-4.6 2.6-7.6z"/>' +
            '<path d="M6.8 4.8c.3 2.5 1.6 3.8 1.6 6 0 .8-.2 1.5-.5 2.1l-1.9-.6C5.3 11.3 4.9 10.3 4.9 9.2c0-1.8 1.4-2.8 1.9-4.4z"/>' +
            '<path d="M17.2 4.8c-.3 2.5-1.6 3.8-1.6 6 0 .8.2 1.5.5 2.1l1.9-.6c.7-1 1.1-2 1.1-3.1 0-1.8-1.4-2.8-1.9-4.4z"/>' +
            '<path d="M6.2 12.2h11.6l-2.3 2.6H8.5z"/>' +
            '<path d="M12 13.8a4.1 4.1 0 1 0 0 8.2 4.1 4.1 0 0 0 0-8.2zM12 16.3a1.6 1.6 0 1 1 0 3.2 1.6 1.6 0 0 1 0-3.2z"/>',
        illusion:
            '<path d="M12 2.2a4.7 4.7 0 1 0 0 9.4 4.7 4.7 0 0 0 0-9.4zM12 3.8a3.1 3.1 0 1 1 0 6.2 3.1 3.1 0 0 1 0-6.2z"/>' +
            '<path d="M7.2 11.2a4.7 4.7 0 1 0 0 9.4 4.7 4.7 0 0 0 0-9.4zM7.2 12.8a3.1 3.1 0 1 1 0 6.2 3.1 3.1 0 0 1 0-6.2z"/>' +
            '<path d="M16.8 11.2a4.7 4.7 0 1 0 0 9.4 4.7 4.7 0 0 0 0-9.4zM16.8 12.8a3.1 3.1 0 1 1 0 6.2 3.1 3.1 0 0 1 0-6.2z"/>',
        restoration:
            '<path d="M12 9.2C9.9 6.6 6.3 5.6 1.6 6.1c1 1.3 2.3 1.9 3.7 2.1-1.1.3-2.1.3-3.2 0 1.1 1.9 2.8 2.9 5 3.1-.8.5-1.7.7-2.7.6 1.6 1.5 3.7 2.1 6.1 1.5L12 11.2z"/>' +
            '<path d="M12 9.2c2.1-2.6 5.7-3.6 10.4-3.1-1 1.3-2.3 1.9-3.7 2.1 1.1.3 2.1.3 3.2 0-1.1 1.9-2.8 2.9-5 3.1.8.5 1.7.7 2.7.6-1.6 1.5-3.7 2.1-6.1 1.5L12 11.2z"/>' +
            '<path d="M12 5.8a1.7 1.7 0 0 1 1.2 2.9l.9 4.4c.3 1.5-.3 3.1-1.2 4.6-.3 1.5-.6 2.8-.9 4.1-.3-1.3-.6-2.6-.9-4.1-.9-1.5-1.5-3.1-1.2-4.6l.9-4.4A1.7 1.7 0 0 1 12 5.8z"/>' +
            '<path d="M12.7 2.2l1.9 1.3-1.4.5.8 1.6-2.1-.9z"/>'
    },
    _schoolGlyphSvg: function(school) {
        var paths = this._schoolGlyphs[String(school || '').toLowerCase()];
        if (!paths) return '';
        return '<svg viewBox="0 0 24 24" fill="currentColor" fill-rule="evenodd" stroke="none">' + paths + '</svg>';
    },
    /**
     * Icon in front of the name, best source first:
     *   1. an installed icon pack's picture for this spell      (iconKey)
     *   2. the vanilla school emblem from Wheeler's standard set (schoolIconKey)
     *   3. our own school glyph, so the slot is never empty
     * While the name is still "???" only the plain school emblem or glyph shows:
     * the school is always visible on the card anyway, a pack icon or a fire/frost
     * variant would give the spell away. Pictures from disk go into an <img> as a
     * data URI, so nothing inside a mod's SVG can run as script.
     * @param {Object} node - uses iconKey, schoolIconKey, school
     * @param {boolean} revealed - true once the name is shown
     */
    renderIcon: function(node, revealed) {
        var img = document.getElementById('spell-icon');
        var glyph = document.getElementById('spell-school-glyph');
        if (!img || !glyph || !node) return;

        var key = (revealed && node.iconKey) ? node.iconKey : (node.schoolIconKey || '');
        this._shown = { node: node, revealed: revealed, key: key };

        var ready = !!(key && this._icons[key]);
        if (ready) {
            img.src = this._icons[key];
            img.classList.remove('hidden');
        } else {
            img.classList.add('hidden');
            img.removeAttribute('src');
        }

        // Glyph whenever no picture is on screen (none installed, or still loading)
        var school = String(node.school || '').toLowerCase();
        var glyphSvg = ready ? '' : this._schoolGlyphSvg(school);
        glyph.innerHTML = glyphSvg;
        glyph.className = 'spell-icon school-glyph ' + school + (glyphSvg ? '' : ' hidden');

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
