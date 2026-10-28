/**
 * DesignPresetsTest - node tests for the Design choice (designPresets.js, run-tests.js)
 * and the add-on designs' source trees.
 *
 * The base mod ships one design, Skald: it must be the one built-in entry and the
 * default, since apply(DEFAULT_ID) runs at load before any file arrives. A saved
 * design that is not installed shows Skald and keeps its id, so it comes back
 * once its add-on is in. Classic, Modern Dark, Arcane, Chalkboard and Night
 * Grimoire are add-ons (addons/designs/<folder>/, the game's Data layout inside,
 * one archive each): each folder holds one design file that parses, with an id of
 * its own, and every file it names - its stylesheet and the files that sheet
 * names, its icon folder, its tree drawings, the url()s in its inline CSS - is in
 * that add-on, in the base panel or in a shared tree (addons/designs/_shared/).
 * No add-on file lies where a base file or another add-on's file does.
 *
 * Depends on: designPresets.js (run in a sandbox of its own with stubs), node fs.
 */

var DesignPresetsTest = {

    passed: 0,
    failed: 0,

    PANEL_DIR: 'PrismaUI/views/SpellLearning/SpellLearningPanel',
    DESIGN_DIR: 'SKSE/Plugins/SpellLearning/presets/design',
    SHARED: '_shared',

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** designPresets.js in a sandbox of its own; out.tree and out.icons record what the last apply set */
    _load: function(out) {
        var vm = require('vm'), fs = require('fs'), path = require('path'), noop = function() {};
        var box = {
            window: {}, settings: {}, console: { log: noop },
            TreeStyle: { set: function(tree) { out.tree = tree; }, fontsChanged: noop },
            HomIcons: { setPath: function(p) { out.icons = p; } },
            document: { getElementById: function() { return null; }, head: { appendChild: noop },
                createElement: function() { return { setAttribute: noop, getAttribute: function() { return null; } }; } }
        };
        vm.createContext(box);
        vm.runInContext(fs.readFileSync(path.join(__dirname, 'designPresets.js'), 'utf8'), box);
        return box;
    },

    _builtIn: function() {
        var out = {}, box = this._load(out), D = box.DesignPresets;
        var ids = Object.keys(D.BUILT_IN);
        this.check(D.DEFAULT_ID === 'skald' && !!D.BUILT_IN[D.DEFAULT_ID], 'the default is skald, and it is built in');
        this.check(ids.length === 1, 'one built-in design (' + ids.join(', ') + ')');
        this.check(D._appliedId === 'skald' && out.tree === D.BUILT_IN.skald.tree && out.icons === 'themes/icons-line/',
            'at load the tree and the icons are Skald\'s');

        var threw = null;
        box.settings.designPreset = 'arcane';
        try { D.apply('no-such-design'); D.onLoaded([]); } catch (e) { threw = e; }
        this.check(!threw && D._appliedId === 'skald' && box.settings.uiTheme === 'skyrim',
            'an unknown or not installed design applies Skald without throwing' + (threw ? ' (threw ' + threw.message + ')' : ''));
        this.check(box.settings.designPreset === 'arcane', 'the saved id stays while its design is not installed');
        D.onLoaded([{ key: 'Arcane', data: { id: 'arcane', name: 'Arcane', tree: { pageColor: '#e6d6b0' } } }]);
        this.check(D._appliedId === 'arcane', 'it comes back once its file arrives');
        D.onLoaded([{ key: 'Skald', data: { id: 'skald', name: 'Not Skald', tree: {} } }]);
        this.check(D.all().skald === D.BUILT_IN.skald, 'a file cannot replace the built-in Skald');

        this.check(D.savedChoice({}) === 'skald', 'a config with no design takes the default');
        this.check(D.savedChoice({ designPreset: 'classic', uiTheme: 'default' }) === 'modern' &&
            D.savedChoice({ uiTheme: 'default' }) === 'modern', 'the old modern UI theme still means Modern Dark');

        box.UI_THEMES = { skyrim: { name: 'Skyrim Edge' } };
        D.onThemesLoaded();
        this.check(Object.keys(D._fromThemes).length === 0, 'the Skyrim theme makes no design of its own (Skald is built on it)');

        var fs = require('fs'), path = require('path'), panel = path.join(__dirname, '..');
        var refs = this._refs(D.BUILT_IN.skald), missing = refs.filter(function(r) { return !fs.existsSync(path.join(panel, r)); });
        this.check(refs.length > 0 && missing.length === 0, 'every file Skald names is in the base panel (' + refs.length + ')' +
            (missing.length ? ' - missing: ' + missing.join(', ') : ''));
    },

    /** Every file under dir, as paths relative to it with "/" */
    _files: function(fs, path, dir, rel, out) {
        if (!fs.existsSync(dir)) return out;
        fs.readdirSync(dir).forEach(function(name) {
            var full = path.join(dir, name), r = rel ? rel + '/' + name : name;
            if (fs.statSync(full).isDirectory()) this._files(fs, path, full, r, out);
            else out.push(r);
        }, this);
        return out;
    },

    /** Panel-relative paths a design names: stylesheet, icon folder, tree drawings, url()s in its inline CSS */
    _refs: function(d) {
        var refs = [], css = d.css, m, re = /url\(\s*["']?([^"')]+)["']?\s*\)/g;
        if (typeof d.cssFile === 'string' && d.cssFile) refs.push(d.cssFile);
        if (typeof d.icons === 'string' && d.icons) refs.push(d.icons);
        if (d.tree) ['hubEmblem', 'pageOrnament'].forEach(function(k) { if (typeof d.tree[k] === 'string' && d.tree[k]) refs.push(d.tree[k]); });
        if (Object.prototype.toString.call(css) === '[object Array]') css = css.join('\n');
        while (typeof css === 'string' && (m = re.exec(css))) if (!/^data:/.test(m[1])) refs.push(m[1]);
        return refs;
    },

    _addons: function() {
        var fs = require('fs'), path = require('path'), self = this;
        var root = path.join(__dirname, '../../../../..'), designs = path.join(root, 'addons/designs');
        var panels = function(name) { return path.join(designs, name, self.PANEL_DIR); };
        var shared = fs.readdirSync(path.join(designs, this.SHARED)).map(function(n) { return path.join(this.SHARED, n); }, this);
        // _shared (trees several add-ons take) and _package (the archives' README and notices parts) are no add-ons
        var names = fs.readdirSync(designs).filter(function(n) { return n.charAt(0) !== '_' && fs.statSync(path.join(designs, n)).isDirectory(); });
        this.check(names.sort().join(',') === 'Arcane,Chalkboard,Classic,ModernDark,NightGrimoire', 'five add-ons: ' + names.join(', '));

        // Where a panel-relative path may be found for an add-on: its own tree, the base panel, a shared tree
        var found = function(name, rel) {
            var places = [panels(name), path.join(root, self.PANEL_DIR)].concat(shared.map(panels));
            return places.some(function(p) { return fs.existsSync(path.join(p, rel)); });
        };
        var ids = {}, owner = {};
        names.forEach(function(name) {
            var jsons = this._files(fs, path, path.join(designs, name, this.DESIGN_DIR), '', []);
            this.check(jsons.length === 1 && jsons[0] === name + '.json', name + ': one design file, ' + name + '.json');
            var d = null;
            try { d = JSON.parse(fs.readFileSync(path.join(designs, name, this.DESIGN_DIR, name + '.json'), 'utf8')); } catch (e) { d = null; }
            this.check(!!(d && typeof d.id === 'string' && d.id && d.name), name + ': the file parses, with an id and a name');
            if (!d) return;
            var id = d.id.toLowerCase();
            this.check(!ids[id] && id !== 'skald', name + ': its id "' + id + '" is its own' + (ids[id] ? ' (also ' + ids[id] + ')' : ''));
            ids[id] = name;
            var refs = this._refs(d), missing = [];
            // The stylesheet's own url()s are relative to the sheet
            if (d.cssFile && fs.existsSync(path.join(panels(name), d.cssFile))) {
                var sheet = fs.readFileSync(path.join(panels(name), d.cssFile), 'utf8'), dir = path.posix.dirname(d.cssFile);
                this._refs({ css: sheet }).forEach(function(u) { refs.push(path.posix.join(dir, u)); });
            }
            refs.forEach(function(r) { if (!found(name, r)) missing.push(r); });
            this.check(refs.length > 0 && missing.length === 0, name + ': every file it names is there (' + refs.length + ')' +
                (missing.length ? ' - missing: ' + missing.join(', ') : ''));
        }, this);

        // No add-on file over a base file or another add-on's (installing one would replace it)
        var clash = [];
        names.concat(shared).forEach(function(name) {
            this._files(fs, path, path.join(designs, name), '', []).forEach(function(rel) {
                if (!/^(SKSE|PrismaUI)\//.test(rel)) return;
                if (fs.existsSync(path.join(root, rel))) clash.push(name + ': ' + rel + ' (base)');
                else if (owner[rel]) clash.push(name + ': ' + rel + ' (' + owner[rel] + ')');
                owner[rel] = name;
            });
        }, this);
        this.check(clash.length === 0, 'no add-on file where the base or another add-on has one' + (clash.length ? ': ' + clash.slice(0, 5).join('; ') : ''));
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        console.log('DesignPresets');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['Built-in', this._builtIn], ['Add-ons', this._addons]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.DesignPresetsTest = DesignPresetsTest;
