/**
 * HomIconsTest - node tests for modules/homIcons.js: paths, painting slots,
 * fallback for a design without icons, and labels that keep the icon slot.
 * Depends on: homIcons.js (global HomIcons).
 */
var HomIconsTest = {
    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _slot: function(id) {
        var attrs = { 'data-icon': id };
        return { className: 'hom-icon', style: {}, getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; } };
    },

    _root: function(slots) {
        return { querySelectorAll: function(sel) { return sel === '.hom-icon' ? slots : []; } };
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('HomIcons');

        this.check(HomIcons.IDS.length === 19, '19 icon ids');
        this.check(HomIcons.IDS.indexOf('scan') !== -1 && HomIcons.IDS.indexOf('prereq') !== -1, 'ids include scan and prereq');

        var a = this._slot('scan'), b = this._slot('nope');
        HomIcons._set = HomIcons.DEFAULT_SET;
        HomIcons.paint(this._root([a, b]));
        this.check(a.style.backgroundImage === 'url("themes/icons-line/scan.png")', 'paint uses the default line set');
        this.check(!b.style.backgroundImage, 'unknown id is left alone');

        var savedDoc = typeof document !== 'undefined' ? document : undefined;
        var c = this._slot('save');
        global.document = this._root([c]);
        HomIcons.setPath('themes/arcane/icons/');
        this.check(c.style.backgroundImage === 'url("themes/arcane/icons/save.png")', 'setPath repaints with the design set');
        HomIcons.setPath(undefined);
        this.check(c.style.backgroundImage === 'url("themes/icons-line/save.png")', 'design without icons falls back to the line set');
        HomIcons.setPath('themes/x');
        this.check(HomIcons._set === HomIcons.DEFAULT_SET, 'path without trailing slash falls back');
        global.document = savedDoc;

        var html = HomIcons.slot('close');
        this.check(html.indexOf('class="hom-icon"') !== -1 && html.indexOf('data-icon="close"') !== -1, 'slot() markup');
        this.check(html.indexOf('icons-line/close.png') !== -1, 'slot() carries the current set');

        var label = { textContent: 'Scan Spells' };
        var btn = { textContent: '', querySelector: function(sel) { return sel === '.hom-label' ? label : null; } };
        HomIcons.setLabel(btn, 'Scanning...');
        this.check(label.textContent === 'Scanning...' && btn.textContent === '', 'setLabel writes the label span, not the button');
        var plain = { textContent: 'X', querySelector: function() { return null; } };
        HomIcons.setLabel(plain, 'Y');
        this.check(plain.textContent === 'Y', 'setLabel falls back to textContent');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.HomIconsTest = HomIconsTest;
