/** Grouping and interaction tests, using the production card modules. */
var SpellCardLayoutTest = {
    passed: 0,
    failed: 0,
    check: function(ok, label) {
        this[ok ? 'passed' : 'failed']++;
        console.log('  [' + (ok ? 'PASS' : 'FAIL') + '] ' + label);
    },
    run: function() {
        this.passed = 0;
        this.failed = 0;
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var elements = {}, nodes = {};
        function element() {
            var classes = {};
            var el = {
                children: [], attributes: {}, style: {}, textContent: '', disabled: false,
                classList: {
                    add: function(name) { classes[name] = true; },
                    remove: function(name) { delete classes[name]; },
                    contains: function(name) { return !!classes[name]; },
                    toggle: function(name, value) {
                        if (value === undefined) value = !classes[name];
                        if (value) classes[name] = true; else delete classes[name];
                    }
                },
                setAttribute: function(key, value) { this.attributes[key] = value; },
                appendChild: function(child) { this.children.push(child); return child; },
                insertBefore: function(child) { this.children.unshift(child); return child; }
            };
            Object.defineProperty(el, 'id', {
                get: function() { return this._id; },
                set: function(value) { this._id = value; elements[value] = this; }
            });
            return el;
        }
        var box = {
            console: console, window: {},
            document: {
                getElementById: function(id) { return elements[id] || null; },
                createElement: element
            },
            state: { spellProgress: {} }, settings: { cheatMode: false },
            _findNodeById: function(id) { return nodes[id]; },
            SpellNames: { isShown: function(node) { return node && node.state !== 'locked'; } },
            spellDisplayName: function(id, node, shown) { return shown ? node.name : '???'; },
            tOr: function(key, params, fallback) {
                return fallback.replace(/\{\{(\w+)\}\}/g, function(_, name) { return params[name]; });
            },
            spellStateLabel: function(value) { return value; },
            spellWeakenedPower: function() { return null; },
            getRequiredXPForNode: function() { return 100; },
            selectNodeById: function(id) { box.selected = id; },
            renderSpellPrereqEditor: function() { box.edited = true; }
        };
        vm.createContext(box);
        ['spellRequirements.js', 'spellCardLayout.js'].forEach(function(file) {
            vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), box);
        });
        nodes.a = { name: 'A', state: 'unlocked' };
        nodes.b = { name: 'Secret B', state: 'locked' };
        nodes.c = { name: 'C', state: 'available' };
        nodes.d = { name: 'D', state: 'available' };
        nodes.e = { name: 'E', state: 'available' };
        var subject = { id: 'subject', formId: 'subject', state: 'locked',
            hardPrereqs: ['a', 'b'], softPrereqs: ['c', 'd', 'e'], softNeeded: 2 };
        var req = box.SpellRequirements, group = req.group(subject, []);
        this.check(group.required.length === 1 && group.required[0].id === 'b', 'only unmet required spells in Still needed');
        this.check(group.met.length === 1 && group.met[0].id === 'a', 'satisfied prerequisite shown separately');
        this.check(group.need === 2 && group.optional.length === 3, 'optional quota: need 2 of 3');
        this.check(group.required[0].name === '???' && !group.required[0].shown, 'discovery mode hides unmet names');
        var hidden = req.chip(group.required[0]);
        this.check(hidden.disabled && hidden.textContent === '???' && !hidden.onclick, 'hidden chip does not reveal by clicking');
        var shown = req.chip(group.optional[0]);
        shown.onclick();
        this.check(box.selected === 'c', 'known chip selects its spell');
        nodes.c.state = 'unlocked';
        group = req.group(subject, []);
        this.check(group.need === 1 && group.optional.length === 2 && group.met.length === 2, 'optional quota uses remaining 1 of 2');
        nodes.d.state = 'unlocked';
        group = req.group(subject, []);
        this.check(group.need === 0 && group.optional.length === 0, 'fulfilled optional quota leaves no unmet choices');
        subject.softNeeded = 0;
        this.check(req.group(subject, []).optional.length === 0, 'repaired optional quota of zero stays fulfilled');
        group = req.group(subject, [{ nodeId: 'b', revealed: false }]);
        this.check(group.locks.length === 1 && group.required.length === 0, 'cross-school lock appears once, distinct from required');
        var lockChip = req.chip(group.locks[0]);
        this.check(lockChip.className.indexOf('requirement-lock') !== -1 &&
            lockChip.children[0].textContent.indexOf('LOCK') !== -1, 'cross-school chip has its lock badge');
        this.check(group.locks[0].name === '???', 'unrevealed lock keeps its name hidden');
        nodes.b.state = 'unlocked';
        group = req.group(subject, [{ nodeId: 'b', revealed: true }]);
        this.check(group.locks.length === 0 && group.met.some(function(entry) { return entry.id === 'b'; }),
            'mastered lock moves to Met');
        group = req.group({ prerequisites: ['a', 'e'] }, []);
        this.check(group.met.length === 1 && group.required[0].id === 'e', 'legacy prerequisites use the same grouping');
        group = req.group({ prerequisites: ['e'], openedByKnownChild: true, state: 'available' }, []);
        this.check(group.required.length === 0 && group.met.length === 0, 'reverse unlock waives gates without claiming mastery');
        box.settings.reverseUnlock = false;
        group = req.group({ prerequisites: ['e'], openedByKnownChild: true, state: 'available' }, []);
        this.check(group.required.length === 1, 'turning reverse unlock off restores the unmet requirement');
        box.settings.reverseUnlock = true;
        elements['card-requirements'] = element();
        req.render(subject, true);
        this.check(box.edited && elements['card-requirements'].classList.contains('hidden'), 'edit mode retains original prerequisite editor');

        ['card-unlocks-toggle', 'spell-unlocks', 'details-unlocks-section', 'card-state-line',
            'progress-section', 'progress-bar', 'learn-btn', 'unlock-btn', 'card-disabled-reason'].forEach(function(id) {
            elements[id] = element();
        });
        var layout = box.SpellCardLayout;
        layout.foldUnlocks(4);
        this.check(elements['spell-unlocks'].classList.contains('hidden') &&
            elements['card-unlocks-toggle'].attributes['aria-expanded'] === 'false', 'later spells folded by default');
        this.check(elements['card-unlocks-toggle'].textContent.indexOf('4 later spells') !== -1, 'folded later spells show real count');
        layout._unlocksOpen = true;
        layout.foldUnlocks(1);
        this.check(!elements['spell-unlocks'].classList.contains('hidden') &&
            elements['card-unlocks-toggle'].textContent.indexOf('1 later spell') !== -1, 'expanded single unlock uses singular');
        layout.foldUnlocks(0);
        this.check(elements['details-unlocks-section'].classList.contains('hidden'), 'no later spells hides empty disclosure');
        layout.syncProgress(subject);
        this.check(elements['learn-btn'].disabled && elements['card-disabled-reason'].textContent.length > 0,
            'locked spell has a disabled Learn and visible reason');
        subject.state = 'available';
        box.state.spellProgress.subject = { xp: 62 };
        box.spellWeakenedPower = function() { return 40; };
        layout.syncProgress(subject);
        this.check(!elements['learn-btn'].disabled && elements['card-disabled-reason'].textContent === '', 'available spell enables Learn and clears old reason');
        this.check(elements['card-state-line'].textContent === 'available · 62% XP · casts at 40% power',
            'state line says XP and current weakened power once');
        this.check(elements['progress-bar'].style.width === '62%', 'thin progress bar uses same XP fraction');
        layout.syncProgress(subject, { preview: true });
        this.check(elements['learn-btn'].disabled && elements['unlock-btn'].disabled, 'hover preview cannot execute either action');
        subject.state = 'unlocked';
        box.spellWeakenedPower = function() { return null; };
        layout.syncProgress(subject);
        this.check(elements['unlock-btn'].disabled && elements['card-disabled-reason'].textContent.indexOf('mastered') !== -1,
            'mastered spell exposes disabled Unlock reason');
        this.check(elements['progress-bar'].style.width === '100%', 'mastered spell progress is full');

        // Exercise the real disclosure binding, including repeated initialization.
        delete elements['card-requirements'];
        var parts = {};
        elements['details-panel'] = element();
        elements['details-panel'].querySelector = function(selector) {
            if (!parts[selector]) parts[selector] = element();
            return parts[selector];
        };
        elements['details-desc-section'] = element();
        elements['details-unlocks-section'].querySelector = function() { return element(); };
        layout.init();
        layout._unlocksOpen = false;
        layout.foldUnlocks(3);
        var toggle = elements['card-unlocks-toggle'];
        toggle.onclick();
        this.check(!elements['spell-unlocks'].classList.contains('hidden') &&
            toggle.attributes['aria-expanded'] === 'true', 'click opens the later spells disclosure');
        layout.init();
        toggle.onclick();
        this.check(elements['spell-unlocks'].classList.contains('hidden'), 'second click closes it after repeat initialization');

        var english = JSON.parse(fs.readFileSync(path.join(__dirname, '../lang/en.json'), 'utf8'));
        var newKeys = Object.keys(english).filter(function(key) {
            return /^(card\.|scanStage\.|settings\.categories\.|footer\.legendTitle$)/.test(key);
        });
        this.check(newKeys.length === 29, 'all 29 new UI language keys are registered');
        ['en', 'ko', 'de', 'es', 'fr', 'it', 'ja', 'pl', 'pt-br', 'ru', 'tr', 'zh-cn', 'zh-tw'].forEach(function(lang) {
            var data = JSON.parse(fs.readFileSync(path.join(__dirname, '../lang/' + lang + '.json'), 'utf8'));
            var valid = newKeys.every(function(key) {
                return typeof data[key] === 'string' && data[key].length > 0 &&
                    JSON.stringify((data[key].match(/\{\{\w+\}\}/g) || []).sort()) ===
                    JSON.stringify((english[key].match(/\{\{\w+\}\}/g) || []).sort());
            });
            SpellCardLayoutTest.check(valid, lang + ': every new key and substitution is translated');
        });
        return { passed: this.passed, failed: this.failed };
    }
};
if (typeof global !== 'undefined') global.SpellCardLayoutTest = SpellCardLayoutTest;
if (typeof window !== 'undefined') window.SpellCardLayoutTest = SpellCardLayoutTest;
