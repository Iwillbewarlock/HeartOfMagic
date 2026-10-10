/**
 * IconSetsTest - each icon folder in SETS has a PNG for each of the 19 ids.
 * SETS holds the two line folders (neutral and gold); later tasks add the
 * folders the shipped designs name.
 * Depends on: homIcons.js, node fs.
 */
var IconSetsTest = {
    passed: 0,
    failed: 0,
    SETS: ['themes/icons-line/', 'themes/icons-line-gold/', 'themes/arcane/icons/', 'themes/nightgrimoire/icons/'],

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSets');
        var fs = require('fs'), path = require('path');
        var panel = path.join(__dirname, '..');
        for (var s = 0; s < this.SETS.length; s++) {
            var missing = [];
            for (var i = 0; i < HomIcons.IDS.length; i++) {
                if (!fs.existsSync(path.join(panel, this.SETS[s], HomIcons.IDS[i] + '.png'))) missing.push(HomIcons.IDS[i]);
            }
            this.check(missing.length === 0, this.SETS[s] + ' has all 19' + (missing.length ? ' (missing: ' + missing.join(', ') + ')' : ''));
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSetsTest = IconSetsTest;
