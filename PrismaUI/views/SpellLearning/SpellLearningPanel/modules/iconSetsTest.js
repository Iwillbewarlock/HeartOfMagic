/**
 * IconSetsTest - each icon folder in SETS has a PNG for each of the 19 ids, and
 * each folder in PARTIAL_SETS has the ids listed for it.
 * SETS holds the line folders (neutral and gold) and each drawn design's set;
 * PARTIAL_SETS holds folders a single CSS rule takes a few tiles from
 * (design-arcane.css: the ink close on parchment, arcane/icons-ink/close.png).
 * Depends on: homIcons.js, node fs.
 */
var IconSetsTest = {
    passed: 0,
    failed: 0,
    SETS: ['themes/icons-line/', 'themes/icons-line-gold/', 'themes/arcane/icons/', 'themes/nightgrimoire/icons/', 'themes/chalkboard/icons/'],
    PARTIAL_SETS: { 'themes/arcane/icons-ink/': ['close'] },

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _missing: function(fs, path, dir, ids) {
        var missing = [];
        for (var i = 0; i < ids.length; i++) {
            if (HomIcons.IDS.indexOf(ids[i]) === -1 || !fs.existsSync(path.join(dir, ids[i] + '.png'))) missing.push(ids[i]);
        }
        return missing.length ? ' (missing: ' + missing.join(', ') + ')' : '';
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSets');
        var fs = require('fs'), path = require('path');
        var panel = path.join(__dirname, '..');
        var missing;
        for (var s = 0; s < this.SETS.length; s++) {
            missing = this._missing(fs, path, path.join(panel, this.SETS[s]), HomIcons.IDS);
            this.check(missing === '', this.SETS[s] + ' has all 19' + missing);
        }
        for (var dir in this.PARTIAL_SETS) {
            missing = this._missing(fs, path, path.join(panel, dir), this.PARTIAL_SETS[dir]);
            this.check(missing === '', dir + ' has ' + this.PARTIAL_SETS[dir].join(', ') + missing);
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSetsTest = IconSetsTest;
