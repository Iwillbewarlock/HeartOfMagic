/**
 * IconSlotsTest - markup and translations carry no bracket glyphs: every icon
 * is an empty .hom-icon slot with a known id, no slot sits inside an element
 * that a translation rewrites, and no lang value holds an icon.
 * Depends on: homIcons.js, node fs.
 */
var IconSlotsTest = {
    passed: 0,
    failed: 0,
    GLYPH: /<span class="(btn-icon|settings-block-icon|prm-collapse-icon)[^"]*"[^>]*>[^<]+<\/span>|>\s*(\[[A-Za-z*~^+\-v ]\]|X|×)\s*</,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSlots');
        var fs = require('fs'), path = require('path');
        var panel = path.join(__dirname, '..');
        var html = fs.readFileSync(path.join(panel, 'index.html'), 'utf8');

        var m = html.match(this.GLYPH);
        this.check(!m, 'index.html has no bracket glyph' + (m ? ' (found: ' + m[0].slice(0, 60) + ')' : ''));

        var slots = html.match(/<span class="hom-icon[^"]*" data-icon="([^"]+)"/g) || [];
        var bad = [];
        for (var i = 0; i < slots.length; i++) {
            var id = slots[i].replace(/.*data-icon="([^"]+)".*/, '$1');
            if (HomIcons.IDS.indexOf(id) === -1) bad.push(id);
        }
        // 30 = 9 action buttons + 4 save/reset/import buttons + 9 settings heads + fullscreen + 7 closes (Steps 4-6)
        this.check(slots.length >= 30 && bad.length === 0, slots.length + ' slots, all known ids' + (bad.length ? ' (unknown: ' + bad.join(', ') + ')' : ''));

        // [\s\S] crosses the markup's line breaks; any closing tag ends the translated element's own text
        var nested = html.match(/<[^>]+data-i18n(-html)?="[^"]*"[^>]*>(?:(?!<\/[a-z0-9]+>)[\s\S])*hom-icon/);
        this.check(!nested, 'no slot inside a translated element');

        var langDir = path.join(panel, 'lang');
        var files = fs.readdirSync(langDir).filter(function(f) { return /\.json$/.test(f); });
        var dirty = [];
        for (var f = 0; f < files.length; f++) {
            var text = fs.readFileSync(path.join(langDir, files[f]), 'utf8');
            if (/btn-icon|hom-icon|"\[[A-Za-z*~^+\-v]\] |"\(!\) /.test(text)) dirty.push(files[f]);
            var js = files[f].replace(/\.json$/, '.js');
            this.check(fs.existsSync(path.join(langDir, js)), js + ' exists');
        }
        this.check(files.length === 13 && dirty.length === 0, files.length + ' lang files without icon glyphs' + (dirty.length ? ' (dirty: ' + dirty.join(', ') + ')' : ''));
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSlotsTest = IconSlotsTest;
