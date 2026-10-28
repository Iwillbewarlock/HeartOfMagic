/**
 * IconSetsTest - each icon folder in SETS has a PNG for each of the 19 ids, and
 * each folder in PARTIAL_SETS has the ids listed for it.
 * SETS holds the line folders (neutral in the base panel, gold in Modern Dark)
 * and each drawn design's set; PARTIAL_SETS holds folders a single CSS rule takes
 * a few tiles from (design-arcane.css: the ink close on parchment,
 * arcane/icons-ink/close.png). A folder an add-on design ships is read from its
 * source tree, addons/designs/<add-on>/ (the game's Data layout inside).
 * Depends on: homIcons.js, node fs.
 */
var IconSetsTest = {
    passed: 0,
    failed: 0,
    // [add-on folder ('' = the base panel), folder relative to the panel]
    SETS: [['', 'themes/icons-line/'], ['ModernDark', 'themes/icons-line-gold/'], ['Arcane', 'themes/arcane/icons/'],
           ['NightGrimoire', 'themes/nightgrimoire/icons/'], ['Chalkboard', 'themes/chalkboard/icons/']],
    // [add-on folder, folder relative to the panel, ids]
    PARTIAL_SETS: [['Arcane', 'themes/arcane/icons-ink/', ['close']]],
    ADDON_PANEL: 'PrismaUI/views/SpellLearning/SpellLearningPanel',

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** The folder on disk: the base panel's, or the add-on source tree's (the repository root is five up) */
    _dir: function(path, addon, rel) {
        return addon ? path.join(__dirname, '../../../../../addons/designs', addon, this.ADDON_PANEL, rel) : path.join(__dirname, '..', rel);
    },

    _missing: function(fs, path, dir, ids) {
        var missing = [];
        for (var i = 0; i < ids.length; i++) {
            if (HomIcons.IDS.indexOf(ids[i]) === -1 || !fs.existsSync(path.join(dir, ids[i] + '.png'))) missing.push(ids[i]);
        }
        return missing.length ? ' (missing: ' + missing.join(', ') + ')' : '';
    },

    _label: function(addon, rel) { return (addon ? addon + ': ' : '') + rel; },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSets');
        var fs = require('fs'), path = require('path');
        var missing, i, s;
        for (i = 0; i < this.SETS.length; i++) {
            s = this.SETS[i];
            missing = this._missing(fs, path, this._dir(path, s[0], s[1]), HomIcons.IDS);
            this.check(missing === '', this._label(s[0], s[1]) + ' has all 19' + missing);
        }
        for (i = 0; i < this.PARTIAL_SETS.length; i++) {
            s = this.PARTIAL_SETS[i];
            missing = this._missing(fs, path, this._dir(path, s[0], s[1]), s[2]);
            this.check(missing === '', this._label(s[0], s[1]) + ' has ' + s[2].join(', ') + missing);
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSetsTest = IconSetsTest;
