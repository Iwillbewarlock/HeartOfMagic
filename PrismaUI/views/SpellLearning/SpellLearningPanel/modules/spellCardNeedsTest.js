/**
 * SpellCardNeedsTest - node tests for the spell card's "can I learn this now" parts (spellCardNeeds.js,
 * 2.0 review item 8; run-tests.js).
 *
 * requirements(): required spells, optional ones only while too few are mastered (with their "need N of M",
 * counted on what is left: how many more out of the optional ones not mastered yet),
 * a lock shown once as a lock chip with its school when it is another one, a lock the hard list lacks still
 * counted, met ones apart, hidden names "???" (a lock's name only once revealed), a spell listed as its own
 * prerequisite ignored, a tree with only `prerequisites` split as the tree's availability splits it.
 * whyText(): "Master X first" for one named requirement, a count otherwise, nothing for an open spell.
 * render(): the chips and the "(need 1 of 2)" after the optional ones, "Met:" with ticks ("Requirements met:"
 * once nothing is left on an open spell), the reason beside a
 * locked spell's Learn (not in cheat mode, not on a preview), edit mode's lists instead of chips, the state
 * line's XP share and "casts at N% power" with the 1.6 weakened power, the Unlocks fold and its memory.
 * Markup and sources: the ids other modules and tests use are kept, a locked spell's Learn shows off.
 *
 * Depends on: spellCardNeeds.js, run in its own sandbox with a fake document (PanelLayoutTest's helpers).
 */

var SpellCardNeedsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _file: function(rel) {
        return require('fs').readFileSync(require('path').join(__dirname, rel), 'utf8');
    },

    /** A small tree: Destruction spells a-f, a Restoration lock r, a hidden spell h */
    _tree: function() {
        var nodes = {
            a: { id: 'a', name: 'Sparks', school: 'Destruction', state: 'unlocked' },
            b: { id: 'b', name: 'Flames', school: 'Destruction', state: 'available' },
            c: { id: 'c', name: 'Frostbite', school: 'Destruction', state: 'unlocked' },
            d: { id: 'd', name: 'Ice Spike', school: 'Destruction', state: 'locked' },
            e: { id: 'e', name: 'Firebolt', school: 'Destruction', state: 'locked' },
            r: { id: 'r', name: 'Lesser Ward', school: 'Restoration', state: 'locked' },
            s: { id: 's', name: 'Fury', school: 'Destruction', state: 'locked' },
            h: { id: 'h', name: 'Secret', school: 'Destruction', state: 'locked', hidden: true }
        };
        return nodes;
    },

    _ctx: function(nodes, locks) {
        return {
            find: function(id) { return nodes[id] || null; },
            isMet: function(id) { return !!nodes[id] && nodes[id].state === 'unlocked'; },
            locks: locks || [],
            shown: function(n) { return !!n && !n.hidden; },
            name: function(id, n, shown) { return shown && n ? n.name : '???'; }
        };
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['requirements', this._requirements], ['render', this._render], ['markup', this._markup]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    },

    _box: function(extra) {
        // Its helpers (fake document, sandbox) live in panelLayoutTest.js: loaded here too when run alone
        var L = global.PanelLayoutTest || (require('./panelLayoutTest.js'), global.PanelLayoutTest);
        return L._sandbox(['spellCardNeeds.js'], extra || {});
    },

    _requirements: function() {
        var N = this._box().SpellCardNeeds, nodes = this._tree();
        var ids = function(list) { return list.map(function(c) { return c.id + ':' + c.kind; }).join(' '); };

        var spell = { id: 'x', school: 'Destruction', hardPrereqs: ['a', 'd'], softPrereqs: ['b', 'c', 'e'], softNeeded: 2 };
        var r = N.requirements(spell, this._ctx(nodes));
        this.check(ids(r.unmet) === 'd:req b:opt e:opt' && ids(r.met) === 'a:req c:opt',
            'Requirements: the unmastered required spell, the optional ones while too few are mastered; met ones apart');
        this.check(r.softNeeded === 2 && r.softTotal === 3 && r.softMet === 1 && r.left === 2,
            'Requirements: need 2 of the 3 optional spells, 1 met, so 2 more spells left (1 required, 1 optional)');
        this.check(r.softLeft === 1 && r.softOpen === 2,
            'Requirements: the card counts what is left - "need 1 of 2" (1 more, out of the 2 optional spells not mastered yet)');

        nodes.e.state = 'unlocked';
        r = N.requirements(spell, this._ctx(nodes));
        this.check(ids(r.unmet) === 'd:req' && r.left === 1, 'Requirements: enough optional spells mastered - none of the others is still needed');
        nodes.e.state = 'locked';

        var locked = { id: 'x', school: 'Destruction', hardPrereqs: ['d', 'r'], softPrereqs: [], softNeeded: 0 };
        r = N.requirements(locked, this._ctx(nodes, [{ nodeId: 'r', revealed: true, name: 'Lesser Ward' }]));
        this.check(ids(r.unmet) === 'd:req r:lock' && r.unmet[1].school === 'Restoration' && r.unmet[1].name === 'Lesser Ward',
            'Requirements: a lock (also in hardPrereqs) once, as a lock chip naming its other school');
        r = N.requirements(locked, this._ctx(nodes, [{ nodeId: 'r', revealed: false, name: 'Lesser Ward' }]));
        this.check(r.unmet[1].hidden && r.unmet[1].name === '???', 'Requirements: a lock not revealed yet reads "???" and cannot be pressed');
        r = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: [] }, this._ctx(nodes, [{ nodeId: 's', revealed: true, name: 'Fury' }]));
        this.check(ids(r.unmet) === 's:lock' && r.unmet[0].school === '' && r.left === 1,
            'Requirements: a lock the hard list lacks (an old tree) still counts; same school, no school shown');

        r = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: ['h', 'x'] }, this._ctx(nodes));
        this.check(ids(r.unmet) === 'h:req' && r.unmet[0].hidden && r.unmet[0].name === '???',
            'Requirements: a hidden spell is "???" (discovery mode); the spell listed as its own prerequisite is ignored');
        r = N.requirements({ id: 'x', school: 'Destruction', prerequisites: ['d', 'b', 'c'] }, this._ctx(nodes));
        this.check(ids(r.unmet) === 'd:req' && ids(r.met) === 'c:opt' && r.softNeeded === 1,
            'Requirements: only `prerequisites` - the first required, the rest "any 1" (as recalculateNodeAvailability)');
        r = N.requirements({ id: 'x', hardPrereqs: [], softPrereqs: ['b', 'd'], softNeeded: 0 }, this._ctx(nodes));
        this.check(r.unmet.length === 0 && r.left === 0, 'Requirements: optional spells with none needed (a repaired lock) need nothing');

        var why = function(state, req) { var w = N.whyText(state, req); return w ? w[0] + (w[1] ? JSON.stringify(w[1]) : '') : null; };
        this.check(why('locked', { left: 1, unmet: [{ name: 'Ice Spike', hidden: false }] }) === 'details.whyMasterFirst{"name":"Ice Spike"}',
            'Why: one named requirement - "Master Ice Spike first"');
        this.check(why('locked', { left: 1, unmet: [{ name: '???', hidden: true }] }) === 'details.whyLeftOne' &&
            why('locked', { left: 3, unmet: [{}, {}] }) === 'details.whyLeft{"count":3}', 'Why: a hidden name or several: the count ("1 requirement left" in its own key, "3 requirements left")');
        r = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: ['d'], softPrereqs: ['b', 'b', 'd', 'c'], softNeeded: 3 }, this._ctx(nodes));
        this.check(r.softTotal === 2 && r.softNeeded === 2 && r.softMet === 1 && r.left === 2 && r.softLeft === 1 && r.softOpen === 1,
            'Requirements: an optional spell listed twice or also required counts once, and no more are needed than there are (need 1 of 1 left)');
        r = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: [], softPrereqs: ['h', 'e', 'c', 'b'], softNeeded: 3 }, this._ctx(nodes));
        this.check(r.softLeft === 2 && r.softOpen === 3 && ids(r.unmet) === 'h:opt e:opt b:opt' && r.unmet[0].hidden && r.unmet[0].name === '???',
            'Requirements: a hidden optional spell is "???" and still counted (need 3 of 4, 1 mastered: "need 2 of 3")');
        var metLock = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: ['c'] }, this._ctx(nodes, [{ nodeId: 'c', revealed: false, name: 'Frostbite' }]));
        this.check(metLock.met.length === 1 && metLock.met[0].name === 'Frostbite' && !metLock.met[0].hidden,
            'Requirements: a mastered lock shows its name in "Met:" even before it is revealed');
        var gone = N.requirements({ id: 'x', school: 'Destruction', hardPrereqs: [] }, this._ctx(nodes, [{ nodeId: 'zz', revealed: true, name: 'Old Spell' }, { nodeId: 'x', revealed: true, name: 'Self' }]));
        this.check(gone.unmet.length === 1 && gone.unmet[0].name === 'Old Spell',
            'Requirements: a lock on a spell the tree lacks keeps its own name; a lock on the spell itself is ignored');
        this.check(why('available', { left: 2, unmet: [{}] }) === null && why('learning', { left: 0, unmet: [] }) === null &&
            why('locked', { left: 0, unmet: [] }) === 'details.whyLocked', 'Why: an open spell has no reason to give; a locked one with nothing listed says it is not met yet');
    },

    _render: function() {
        var L = global.PanelLayoutTest || (require('./panelLayoutTest.js'), global.PanelLayoutTest), self = this;
        var ids = ['details-panel', 'progress-section', 'spell-state-detail', 'spell-state-fill', 'spell-state-bar', 'spell-needs', 'spell-needs-chips',
            'spell-met', 'learn-why', 'learn-btn', 'spell-unlocks', 'spell-unlocks-toggle', 'spell-unlocks-label', 'spell-unlocks-arrow', 'details-unlocks-section'];
        var els = {};
        ids.forEach(function(id) { els[id] = L.el('div', { id: id }); });
        els['spell-unlocks'].appendChild(L.el('li'));
        els['spell-unlocks'].appendChild(L.el('li'));
        var nodes = this._tree(), selected = [];
        var box = this._box({
            state: { treeData: { nodes: [] } }, settings: { cheatMode: false }, EditMode: { isActive: false },
            _findNodeById: function(id) { return nodes[id] || null; },
            SpellNames: { isShown: function(n) { return !!n && !n.hidden; } },
            spellDisplayName: function(id, n, shown) { return shown && n ? n.name : '???'; },
            PreReqMaster: { getLocksForNode: function() { return [{ nodeId: 'r', revealed: true, name: 'Lesser Ward' }]; } },
            schoolText: function(s) { return s; },
            tOr: function(k, p, f) { var t = f; for (var q in (p || {})) t = t.split('{{' + q + '}}').join(p[q]); return t; },
            selectNodeById: function(id) { selected.push(id); }
        });
        var N = box.SpellCardNeeds, doc = L.doc(ids.map(function(id) { return els[id]; }));
        N._doc = function() { return doc; };
        N.init();
        var spell = { id: 'x', school: 'Destruction', state: 'locked', hardPrereqs: ['d', 'r'], softPrereqs: ['b', 'c', 'e'], softNeeded: 2 };
        N.render(spell, { preview: false, progressPercent: 0, weakenedPower: null });
        var chips = els['spell-needs-chips'].children;
        var names = chips.map(function(c) { return c.text(); });
        this.check(!els['spell-needs'].classList.contains('hidden') && names.join(' | ') === 'Ice Spike | LOCKLesser Ward(Restoration) | Flames | Firebolt | (need 1 of 2)',
            'Render: "Still needed" - required, the lock with its school, the optional ones and "(need 1 of 2)" after them (Frostbite mastered: 1 more of the 2 left)');
        this.check(/spell-need-lock/.test(chips[1].className) && chips[1].tagName === 'BUTTON', 'Render: the lock is its own kind of chip, pressable once revealed');
        chips[0].click();
        this.check(selected[0] === 'd', 'Render: a chip goes to its spell');
        this.check(!els['spell-met'].classList.contains('hidden') && els['spell-met'].text() === 'Met: Frostbite ✓',
            'Render: "Met:" lists what is mastered, with a tick');
        this.check(!els['learn-why'].classList.contains('hidden') && els['learn-why'].textContent === '3 requirements left',
            'Render: the reason beside the locked spell\'s Learn ("3 requirements left")');

        N.render(spell, { preview: true, progressPercent: 0, weakenedPower: null });
        this.check(els['learn-why'].classList.contains('hidden'), 'Render: no reason on a hover preview (the card says to click instead)');
        box.settings.cheatMode = true;
        N.render(spell, { preview: false });
        this.check(els['learn-why'].classList.contains('hidden') && els['progress-section'].classList.contains('cheat-xp'),
            'Render: cheat mode gives no reason (Unlock takes any spell) and keeps the XP boxes');
        box.settings.cheatMode = false;

        box.EditMode.isActive = true;
        N.render(spell, { preview: false });
        this.check(els['details-panel'].classList.contains('card-editing') && els['spell-needs'].classList.contains('hidden') &&
            els['spell-met'].classList.contains('hidden'), 'Render: edit mode shows the lists with their controls, not the chips');
        box.EditMode.isActive = false;

        var open = { id: 'y', school: 'Destruction', state: 'learning', hardPrereqs: ['a'] };
        N.render(open, { preview: false, progressPercent: 62.4, weakenedPower: 50 });
        this.check(els['spell-met'].text() === 'Met: Sparks ✓', 'Render: an open spell with a lock still unmastered keeps "Met:" (the lock is a requirement)');
        var withLocks = box.PreReqMaster;
        box.PreReqMaster = { getLocksForNode: function() { return []; } };
        N.render(open, { preview: false, progressPercent: 62.4, weakenedPower: 50 });
        this.check(!els['spell-met'].classList.contains('hidden') && els['spell-met'].text() === 'Requirements met: Sparks ✓' &&
            els['spell-met'].children[0].getAttribute('data-i18n') === 'details.requirementsMet',
            'Render: an open spell with nothing left says "Requirements met:" where "Still needed" stands, its own key');
        N.render({ id: 'v', school: 'Destruction', state: 'available', hardPrereqs: ['a', 'd'] }, { preview: false, progressPercent: 0, weakenedPower: null });
        this.check(els['spell-met'].text() === 'Met: Sparks ✓' && els['spell-needs'].classList.contains('hidden'),
            'Render: an open spell whose own requirements are not all mastered (opened by a known higher spell) keeps "Met:", not "Requirements met"');
        box.PreReqMaster = withLocks;
        N.render(open, { preview: false, progressPercent: 62.4, weakenedPower: 50 });
        this.check(els['spell-needs'].classList.contains('hidden') && els['spell-state-detail'].text() === ' · 62% learned · casts at 50% power' &&
            els['spell-state-fill'].style.width === '62%' && !els['spell-state-bar'].classList.contains('hidden'),
            'Render: an open, weakened spell - nothing still needed, the word then " · 62% learned · casts at 50% power" on one line, and its bar');
        box.spellWeakenedPower = function() { return 65; };
        box.tOr = function(k, p, f) { var t = f; for (var q in (p || {})) t = t.split('{{' + q + '}}').join(p[q]); return t; };
        var badge = L.el('span', { id: 'spell-state' });
        doc.add(badge);
        box.state.spellProgress = { '0xY': { xp: 71.2 } };
        box.getCanonicalFormId = function() { return '0xY'; };
        box.getRequiredXPForNode = function() { return 100; };
        N.refresh(open, false);
        this.check(els['spell-state-detail'].text() === ' · 71% learned · casts at 65% power' && els['spell-state-fill'].style.width === '71%' &&
            badge.textContent === 'Weakened', 'Progress-only redraw (refresh): the state line and its bar follow the XP, the badge says "Weakened" with no second figure');
        var wasLocked = { id: 'w', school: 'Destruction', state: 'locked', hardPrereqs: ['d'] };
        N.refresh(wasLocked, false);
        var whyBefore = !els['learn-why'].classList.contains('hidden');
        nodes.d.state = 'unlocked';
        wasLocked.state = 'available';
        N.refresh(wasLocked, false);
        this.check(whyBefore && els['learn-why'].classList.contains('hidden') && els['spell-needs'].classList.contains('hidden'),
            'Progress-only redraw: a spell opened since the card was drawn loses its reason and "Still needed"');
        nodes.d.state = 'locked';
        delete box.spellWeakenedPower;
        N.render({ id: 'z', state: 'unlocked' }, { progressPercent: 0, weakenedPower: null });
        this.check(els['spell-state-detail'].text() === ' · 100% learned' && els['spell-state-fill'].style.width === '100%' &&
            !els['spell-state-bar'].classList.contains('hidden'),
            'Render: a mastered spell - "100% learned" whatever its record holds, a full bar, no power');
        N.render({ id: 'q', state: 'locked', hardPrereqs: ['d'] }, { progressPercent: 0, weakenedPower: null });
        this.check(els['spell-state-detail'].text() === ' · 0% learned' && els['spell-state-fill'].style.width === '0%' &&
            !els['spell-state-bar'].classList.contains('hidden'),
            'Render: a locked spell reads the same way - "0% learned" after the word, the empty bar under it');
        var cardCss = this._file('../panel-card.css');
        this.check(/\.spell-state-line \{\s*display: block;/.test(cardCss) && /#spell-state \{[^}]*background: none;[^}]*border: 0;[^}]*text-transform: none;[^}]*color: var\(--text-primary/.test(cardCss) &&
            /#spell-state\.weakened::before \{ background: var\(--weakened-color/.test(cardCss),
            'CSS: the state line flows as one sentence; the state word on the card has no box or capitals, in the ink of the card, its colour on a dot (by id, over the stamps of the designs)');

        this.check(els['spell-unlocks'].classList.contains('hidden') && els['spell-unlocks-label'].textContent === 'Leads to 2 later spells' &&
            !els['details-unlocks-section'].classList.contains('hidden'), 'Unlocks: folded to "Leads to 2 later spells"');
        els['spell-unlocks-toggle'].click();
        this.check(!els['spell-unlocks'].classList.contains('hidden') && els['spell-unlocks-toggle'].getAttribute('aria-expanded') === 'true',
            'Unlocks: a click opens the list');
        N.render(open, { progressPercent: 0 });
        this.check(!els['spell-unlocks'].classList.contains('hidden'), 'Unlocks: open stays open for the next card (the player chose it)');
        els['spell-unlocks'].innerHTML = '';
        els['spell-unlocks'].appendChild(L.el('li'));
        N.render(open, { progressPercent: 0 });
        this.check(els['spell-unlocks-label'].textContent === 'Leads to 1 later spell' && els['spell-unlocks-label'].getAttribute('data-i18n') === 'details.leadsToOne',
            'Unlocks: one later spell in its own key ("Leads to 1 later spell")');
        els['spell-unlocks'].innerHTML = '';
        N.render(open, { progressPercent: 0 });
        this.check(els['details-unlocks-section'].classList.contains('hidden'), 'Unlocks: a spell that leads nowhere has no line');
    },

    _markup: function() {
        var html = this._file('../index.html');
        var card = html.substring(html.indexOf('id="details-panel"'), html.indexOf('id="howto-panel"'));
        var keep = ['spell-name', 'spell-school', 'spell-level', 'spell-cost', 'spell-type', 'spell-description', 'spell-chips', 'spell-effects',
            'details-effects-section', 'prereq-summary', 'hard-prereqs-list', 'soft-prereqs-list', 'soft-needed-count', 'spell-prereqs', 'spell-unlocks',
            'details-unlocks-section', 'spell-bridges', 'locks-section', 'locks-list', 'progress-section', 'learning-status-badge', 'learning-status-hint',
            'reverse-unlock-note', 'progress-text', 'xp-current-input', 'xp-required-input', 'progress-bar', 'spell-state', 'details-peek-hint',
            'learn-btn', 'unlock-btn', 'close-details', 'details-empty', 'spell-icon', 'spell-needs', 'spell-met', 'learn-why', 'spell-state-detail'];
        var missing = keep.filter(function(id) { return card.indexOf('id="' + id + '"') === -1; });
        this.check(missing.length === 0, 'Markup: every id the card\'s code and tests use is still on the card' + (missing.length ? ' (' + missing.join(', ') + ')' : ''));
        this.check(card.indexOf('id="spell-school"') < card.indexOf('id="spell-level"') && card.indexOf('id="spell-cost"') < card.indexOf('id="spell-description"') &&
            card.indexOf('id="spell-description"') < card.indexOf('id="spell-state"') && card.indexOf('id="spell-state"') < card.indexOf('id="spell-needs"') &&
            card.indexOf('id="spell-needs"') < card.indexOf('id="learn-btn"') && card.indexOf('id="learn-btn"') < card.indexOf('id="spell-unlocks-toggle"'),
            'Markup: top to bottom - school, level, Magicka, the effect line, the state, still needed, Learn, then Unlocks');
        var progress = this._file('spellProgressView.js');
        this.check(/if \(node\.state === 'locked'\) \{[\s\S]{0,200}learnBtn\.classList\.remove\('hidden'\);[\s\S]{0,200}learnBtn\.disabled = true;/.test(progress),
            'Sources: a locked spell\'s Learn shows, off (spellProgressView.js)');
        this.check(/xpCurrentInput\.onchange = function\(\) \{[\s\S]{0,1200}SpellCardNeeds\.refresh\(node, preview\);[\s\S]{0,1500}xpRequiredInput\.onchange = function\(\) \{[\s\S]{0,1200}SpellCardNeeds\.refresh\(node, preview\);/.test(progress),
            'Sources: the cheat XP inputs (current and required) redraw the state line and bar, not only the hidden old bar (Codex review)');
        var view = this._file('spellCardView.js');
        this.check(/learnBtnEl\.disabled = node\.state === 'locked';/.test(view) && /SpellCardNeeds\.render\(node, \{ preview: preview, progressPercent: progressPercent, weakenedPower: isWeakened \? effectiveness : null \}\)/.test(view),
            'Sources: renderSpellCard keeps a locked Learn off and hands the 1.6 weakened power to the state line');
        this.check(/SpellCardNeeds\.refresh\(node, preview\)/.test(progress) && /learnBtn\.disabled = preview;/.test(progress) &&
            /EditMode\] Activated/.test(this._file('editMode.js')) && /undoStack = \[\];[\s\S]{0,300}showSpellDetails\(state\.selectedNode\)[\s\S]{0,80}\[EditMode\] Activated/.test(this._file('editMode.js')),
            'Sources: every progress-only redraw (updateDetailsProgression) redraws the lower part of the card and turns Learn back on; entering edit mode redraws the open card');
        var css = this._file('../panel-card.css');
        this.check(/\.details-panel:not\(\.card-editing\) \.details-col-links \{ display: none; \}/.test(css) && /#contentSpellTree\.details-bottom/.test(css),
            'CSS: the edit-mode lists only while editing; the bar under the tree styled too');
    }
};

if (typeof window !== 'undefined') window.SpellCardNeedsTest = SpellCardNeedsTest;
if (typeof global !== 'undefined') global.SpellCardNeedsTest = SpellCardNeedsTest;
