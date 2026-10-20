/**
 * SpellCardNeeds - the spell card around "can I learn this now" (2.0 review, item 8)
 *
 * renderSpellCard (spellCardView.js) draws the card's top - name, school, level, Magicka, the
 * effect line - and calls render() last. This module draws:
 *  - the state line: the state word (#spell-state, the 1.6 "Weakened (N%)" included), the XP
 *    share ("62% learned"), "casts at N% power" while weakened, and a thin progress bar (the XP
 *    figures "45 / 100 XP" stay in the hidden progress section; cheat mode shows its XP boxes);
 *  - "Still needed:" - only what is not met yet, as chips: required spells, optional ones with
 *    "(need 2 of 3)" once after them, Pre Req Master locks as lock chips with the lock's school
 *    when it is another one. A lock is also one of the spell's required spells (classicTreeOutput.js
 *    bakes locks into hardPrereqs): it shows once, as the lock chip. Shown only for a locked spell:
 *    an open one (prerequisites met, or opened by a known higher spell) needs nothing;
 *  - "Met:" - the requirements already mastered, in one quiet line;
 *  - why Learn cannot be pressed: a locked spell's Learn button shows, off, with the reason
 *    beside it ("Master Frostbite first", "3 requirements left");
 *  - "Leads to N later spells" folding the Unlocks list (open or shut stays for the session).
 * Names follow the card's rules: a hidden spell is "???" and cannot be pressed (SpellNames.isShown),
 * a lock's name only once revealed. Edit mode keeps the old lists with their hard/soft switch, delete
 * button and "need N" box (.card-editing), and the effects list; the chips are for play.
 *
 * requirements() and whyText() take everything they read as arguments: tested under node
 * (spellCardNeedsTest.js).
 *
 * Depends on: renderSpellCard (spellCardView.js), state, settings, _findNodeById (treeViewerUI.js),
 * spellDisplayName / SpellNames (spellNames.js), PreReqMaster.getLocksForNode, schoolText
 * (uiHelpers.js), setI18n / tOr (i18n.js), EditMode, selectNodeById (spellProgressView.js).
 */
var SpellCardNeeds = {
    CHECK: '✓',

    _unlocksOpen: false,
    _initialized: false,

    _doc: function() { return document; },
    _el: function(id) { return this._doc().getElementById(id); },

    // =========================================================================
    // WHAT IS NEEDED (pure)
    // =========================================================================

    /**
     * Sort a spell's requirements into what is still needed and what is met.
     * @param {Object} node - hardPrereqs, softPrereqs, softNeeded, prerequisites, school
     * @param {{find:function(string):Object, isMet:function(string):boolean, locks:Array,
     *          shown:function(Object):boolean, name:function(string, Object, boolean):string}} ctx
     * @returns {{unmet:Array, met:Array, softNeeded:number, softTotal:number, softMet:number, left:number}}
     *   each chip { id, name, hidden, kind: 'req'|'opt'|'lock', school }; left = how many more must
     *   be mastered (every unmet required spell and lock, and the optional ones still short)
     */
    requirements: function(node, ctx) {
        var self = node || {};
        var notSelf = function(id) { return id !== self.id && id !== self.formId; };
        var hard = (self.hardPrereqs || []).filter(notSelf);
        var soft = (self.softPrereqs || []).filter(notSelf);
        var need = self.softNeeded || 0;
        var raw = (self.prerequisites || []).filter(notSelf);
        // A spell with no hard/soft lists yet: the same split recalculateNodeAvailability makes
        if (hard.length === 0 && soft.length === 0 && raw.length > 0) {
            if (raw.length === 1) hard = raw.slice();
            else { hard = [raw[0]]; soft = raw.slice(1); need = 1; }
        }
        var locks = ctx.locks || [];
        var lockOf = {};
        locks.forEach(function(l) { if (l && l.nodeId) lockOf[l.nodeId] = l; });
        var seen = {};
        var chip = function(id, kind) {
            var n = ctx.find(id);
            var lock = lockOf[id];
            // A lock's name shows once revealed (selecting either spell reveals it), as the Locks list did,
            // or once the player has mastered it (a spell they know has no secret name)
            var shown = lock ? (!!(lock.revealed && (lock.name || (n && n.name))) || (ctx.isMet(id) && ctx.shown(n))) : ctx.shown(n);
            var school = (kind === 'lock' && n && n.school && n.school !== self.school) ? n.school : '';
            // A lock on a spell the tree does not hold: its own name, as the Locks list wrote it
            var name = (lock && !n) ? (shown ? lock.name : '???') : ctx.name(id, n, shown);
            return { id: id, name: name, hidden: !shown, kind: kind, school: school };
        };
        var unmet = [], met = [];
        hard.forEach(function(id) {
            if (seen[id]) return;
            seen[id] = true;
            var c = chip(id, lockOf[id] ? 'lock' : 'req');
            (ctx.isMet(id) ? met : unmet).push(c);
        });
        // A lock the hard list does not carry (an old tree): still a requirement
        locks.forEach(function(l) {
            if (!l || !l.nodeId || !notSelf(l.nodeId) || seen[l.nodeId]) return;
            seen[l.nodeId] = true;
            var c = chip(l.nodeId, 'lock');
            (ctx.isMet(l.nodeId) ? met : unmet).push(c);
        });
        var softMet = 0, softOpen = [];
        soft.forEach(function(id) {
            if (seen[id]) return;
            seen[id] = true;
            var c = chip(id, 'opt');
            if (ctx.isMet(id)) { softMet++; met.push(c); } else softOpen.push(c);
        });
        // Counted once each (a spell listed twice, or also required or a lock, is not a second option),
        // and never more needed than there are
        var softTotal = softMet + softOpen.length;
        need = Math.min(need, softTotal);
        var short = Math.max(0, need - softMet);
        // Optional spells are needed only while too few are mastered
        if (short > 0) unmet = unmet.concat(softOpen);
        var hardLeft = unmet.filter(function(c) { return c.kind !== 'opt'; }).length;
        return { unmet: unmet, met: met, softNeeded: need, softTotal: softTotal, softMet: softMet, left: hardLeft + short };
    },

    /**
     * Why Learn cannot be pressed, as [key, params, English], or null when it can.
     * @param {string} state - the node's state
     * @param {Object} req - requirements()
     */
    whyText: function(state, req) {
        if (state !== 'locked') return null;
        if (req.left === 1 && req.unmet.length === 1 && !req.unmet[0].hidden) {
            return ['details.whyMasterFirst', { name: req.unmet[0].name }, 'Master {{name}} first'];
        }
        if (req.left === 1) return ['details.whyLeftOne', null, '1 requirement left'];
        if (req.left > 0) return ['details.whyLeft', { count: req.left }, '{{count}} requirements left'];
        return ['details.whyLocked', null, 'Its requirements are not met yet'];
    },

    // =========================================================================
    // DRAWING
    // =========================================================================

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;
        var toggle = this._el('spell-unlocks-toggle');
        if (toggle) toggle.addEventListener('click', function() {
            self._unlocksOpen = !self._unlocksOpen;
            self._drawUnlocksFold();
        });
    },

    /** The live context requirements() reads */
    _ctx: function(node) {
        var find = function(id) { return (typeof _findNodeById === 'function' && typeof state !== 'undefined' && state && state.treeData) ? _findNodeById(id) : null; };
        var locks = (typeof PreReqMaster !== 'undefined' && PreReqMaster.getLocksForNode) ? PreReqMaster.getLocksForNode(node.id) : [];
        return {
            find: find,
            // Mastered, as the tree's own availability counts it (cppCallbacks.js isPrereqMastered)
            isMet: function(id) { var n = find(id); return !!n && n.state === 'unlocked'; },
            locks: locks,
            shown: function(n) { return typeof SpellNames !== 'undefined' ? SpellNames.isShown(n) : !!n; },
            name: function(id, n, shown) { return typeof spellDisplayName === 'function' ? spellDisplayName(id, n, shown) : (shown && n ? n.name : '???'); }
        };
    },

    /**
     * @param {Object} node
     * @param {{preview?:boolean, progressPercent?:number, weakenedPower?:number|null}} info
     */
    render: function(node, info) {
        var panel = this._el('details-panel');
        if (!panel || !node) return;
        info = info || {};
        var editing = typeof EditMode !== 'undefined' && !!EditMode.isActive;
        var cheat = typeof settings !== 'undefined' && !!settings.cheatMode;
        panel.classList.toggle('card-editing', editing);
        var progress = this._el('progress-section');
        if (progress) progress.classList.toggle('cheat-xp', cheat);

        this._drawState(node, info);
        var req = this.requirements(node, this._ctx(node));
        this._drawNeeds(node, req, editing);
        this._drawWhy(node, req, info, cheat);
        this._drawUnlocksFold();
    },

    /**
     * The card's lower part again after a redraw of only the progress part (updateDetailsProgression:
     * an XP update, Learn, a progress reload, cheat unlock): the state line and bar, "Weakened (N%)",
     * "Still needed" / "Met" and the reason beside Learn follow, as a full render would draw them.
     * The XP share is read as updateDetailsProgression reads it (the canonical id's progress over
     * getRequiredXPForNode); renderSpellCard draws the card once more with its own figure after it.
     * @param {Object} node
     * @param {boolean} [preview] - a hover preview
     */
    refresh: function(node, preview) {
        if (!node || typeof state === 'undefined' || !state) return;
        var canon = (typeof getCanonicalFormId === 'function') ? getCanonicalFormId(node) : node.formId;
        var progress = (state.spellProgress && state.spellProgress[canon]) || null;
        var required = (typeof getRequiredXPForNode === 'function') ? getRequiredXPForNode(node) : 0;
        var percent = (progress && required > 0) ? ((progress.xp || 0) / required) * 100 : 0;
        var power = (typeof spellWeakenedPower === 'function') ? spellWeakenedPower(node) : null;
        var badge = this._el('spell-state');
        if (badge && power !== null) {
            badge.textContent = tOr('details.stateWeakened', { pct: power }, 'Weakened ({{pct}}%)');
            badge.className = 'state-badge weakened';
        }
        this.render(node, { preview: !!preview, progressPercent: percent, weakenedPower: power });
    },

    _drawState: function(node, info) {
        var detail = this._el('spell-state-detail');
        var fill = this._el('spell-state-fill');
        var bar = this._el('spell-state-bar');
        var pct = Math.max(0, Math.min(100, Math.round(info.progressPercent || 0)));
        var learning = node.state === 'available' || node.state === 'learning';
        if (detail) {
            detail.innerHTML = '';
            var parts = [];
            if (learning || (node.state === 'locked' && pct > 0)) parts.push(['details.statePct', { pct: pct }, '{{pct}}% learned']);
            if (info.weakenedPower !== null && info.weakenedPower !== undefined) {
                parts.push(['details.castsAtPower', { pct: info.weakenedPower }, 'casts at {{pct}}% power']);
            }
            for (var i = 0; i < parts.length; i++) {
                if (i > 0) detail.appendChild(this._doc().createTextNode(' · '));
                var span = this._doc().createElement('span');
                if (parts[i][0] === 'details.castsAtPower') span.className = 'spell-state-power';
                setI18n(span, parts[i][0], parts[i][1], parts[i][2]);
                detail.appendChild(span);
            }
        }
        if (fill) fill.style.width = pct + '%';
        if (bar) bar.classList.toggle('hidden', !(learning || pct > 0) || node.state === 'unlocked');
    },

    _chip: function(c, needText) {
        var doc = this._doc();
        var btn = doc.createElement(c.hidden ? 'span' : 'button');
        if (!c.hidden) btn.setAttribute('type', 'button');
        btn.className = 'spell-need-chip spell-need-' + c.kind + (c.hidden ? ' prereq-hidden' : '');
        btn.setAttribute('data-id', c.id);
        if (c.kind === 'lock') {
            var badge = doc.createElement('span');
            badge.className = 'spell-need-lockmark';
            setI18n(badge, 'details.lock', null, 'LOCK');
            btn.appendChild(badge);
        }
        var name = doc.createElement('span');
        name.textContent = c.name;
        btn.appendChild(name);
        if (c.school) {
            var school = doc.createElement('span');
            school.className = 'spell-need-school';
            school.textContent = '(' + (typeof schoolText === 'function' ? schoolText(c.school) : c.school) + ')';
            btn.appendChild(school);
        }
        if (!c.hidden) btn.addEventListener('click', function() { if (typeof selectNodeById === 'function') selectNodeById(c.id); });
        return btn;
    },

    _drawNeeds: function(node, req, editing) {
        var doc = this._doc();
        var needs = this._el('spell-needs');
        var chips = this._el('spell-needs-chips');
        var met = this._el('spell-met');
        var showNeeds = !editing && node.state === 'locked' && req.unmet.length > 0;
        if (chips) {
            chips.innerHTML = '';
            var self = this, optCount = 0;
            req.unmet.forEach(function(c) {
                chips.appendChild(self._chip(c));
                if (c.kind === 'opt') optCount++;
            });
            if (optCount > 0) {
                var count = doc.createElement('span');
                count.className = 'spell-need-count';
                count.textContent = '(' + tOr('details.softNeed', { need: req.softNeeded, total: req.softTotal }, 'need {{need}} of {{total}}') + ')';
                // The optional chips come last (requirements() puts them after the required ones and locks)
                chips.appendChild(count);
            }
        }
        if (needs) needs.classList.toggle('hidden', !showNeeds);
        if (met) {
            met.innerHTML = '';
            var showMet = !editing && req.met.length > 0;
            if (showMet) {
                var label = doc.createElement('span');
                label.className = 'spell-met-label';
                setI18n(label, 'details.met', null, 'Met:');
                met.appendChild(label);
                req.met.forEach(function(c, i) {
                    var item = doc.createElement('span');
                    item.className = 'spell-met-item';
                    item.textContent = (i > 0 ? ', ' : ' ') + c.name + ' ';
                    var tick = doc.createElement('span');
                    tick.className = 'spell-met-tick';
                    tick.textContent = SpellCardNeeds.CHECK;
                    item.appendChild(tick);
                    met.appendChild(item);
                });
            }
            met.classList.toggle('hidden', !showMet);
        }
    },

    /**
     * Why a locked spell's Learn button (shown, off: spellProgressView.js) cannot be pressed, beside it.
     * Not in cheat mode (Unlock (Cheat) takes any spell) nor on a hover preview (it says to click instead).
     */
    _drawWhy: function(node, req, info, cheat) {
        var why = this._el('learn-why');
        var text = (!cheat && !info.preview) ? this.whyText(node.state, req) : null;
        if (why) {
            if (text) setI18n(why, text[0], text[1], text[2]);
            else {
                // No key left behind: a language switch would write the last reason back in
                if (typeof clearI18n === 'function') clearI18n(why);
                why.textContent = '';
            }
            why.classList.toggle('hidden', !text);
        }
    },

    /** "Leads to N later spells" over the Unlocks list; the list as the player left it */
    _drawUnlocksFold: function() {
        var list = this._el('spell-unlocks');
        var toggle = this._el('spell-unlocks-toggle');
        var label = this._el('spell-unlocks-label');
        var arrow = this._el('spell-unlocks-arrow');
        var section = this._el('details-unlocks-section');
        if (!list) return;
        var count = list.children ? list.children.length : 0;
        if (section) section.classList.toggle('hidden', count === 0);
        if (label) {
            if (count === 1) setI18n(label, 'details.leadsToOne', null, 'Leads to 1 later spell');
            else setI18n(label, 'details.leadsTo', { count: count }, 'Leads to {{count}} later spells');
        }
        list.classList.toggle('hidden', !this._unlocksOpen);
        if (toggle) toggle.setAttribute('aria-expanded', this._unlocksOpen ? 'true' : 'false');
        if (arrow) arrow.textContent = this._unlocksOpen ? '▾' : '▸';
    }
};

window.SpellCardNeeds = SpellCardNeeds;
