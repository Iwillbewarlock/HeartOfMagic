/**
 * NewSpells - what changed while the panel was closed, and where to go next.
 *
 * 1. New spells. When the panel closes, the spells the tree shows as unlocked
 *    are noted. When it opens again (onPanelShowing and the two replies that
 *    follow it, GetProgress and GetPlayerKnownSpells), every spell unlocked now
 *    that was not then is new: mastered through XP, or learned from a tome or a
 *    vendor. A new spell gets a ring that spreads once (BURST_MS) and then a
 *    small mark that stays until the player looks at it - its card shown, by a
 *    click or the hover preview (markSeen, from renderSpellCard). The footer
 *    has a chip with the count; pressing it goes to the first one.
 *    Only from one opening to the next: the first opening after a save is
 *    loaded has nothing to compare with, so it shows nothing.
 *
 * 2. Next goals. The footer lists up to GOAL_COUNT spells to work on: the
 *    learning targets first, then the learnable spells with the most XP.
 *    Pressing one selects it and moves the camera there, as a click does.
 *
 * Cost. The ring and the marks are FxLayer spots (a canvas the size of the
 * mark over the tree canvas), so the tree layer is never drawn again for them:
 * a mark appearing or going is one small canvas shown or hidden. At most
 * MAX_MARKS are drawn (FxLayer has 16 spots, and the hub, particles, hover and
 * the learning glows use some); the footer counts them all. The ring runs on
 * animation frames, the ones the heart already gets. The footer is rebuilt only
 * when what it shows changed (a key string).
 *
 * The ring is left out (the mark stays) when the render popup's "still
 * everything" switch is on (DesignEffectsSetting._master), as the other
 * moving effects are.
 *
 * Depends on: state, settings, CanvasRenderer, FxLayer, spellDisplayName
 * (spellNames.js), DesignEffectsSetting (all optional but state)
 */

var NewSpells = {

    BURST_MS: 900,          // the ring spreading once
    BURST_DELAY_MS: 350,    // after the last reply: not on the frame the tree is repainted
    BURST_STAGGER_MS: 140,  // one ring after the other
    BURST_REACH_PX: 26,     // how far past the spell the ring goes (CSS px)
    MAX_MARKS: 4,           // marks drawn on the tree; the footer counts the rest
    OPEN_WINDOW_MS: 3000,   // replies later than this after opening are not compared
    GOAL_COUNT: 3,

    _known: null,           // formId -> true: unlocked when the panel last closed (null: never closed)
    _openUntil: 0,          // compare replies until this time (performance.now)
    _new: [],               // nodes new since the last opening, oldest first
    _bursts: [],            // { node, start }
    _burstTimer: null,
    _footerKey: null,
    _color: null,
    _colorDesign: null,     // the design _color was read for

    // =========================================================================
    // NEW SPELLS
    // =========================================================================

    _nodes: function() {
        return (typeof state !== 'undefined' && state.treeData && state.treeData.nodes) || [];
    },

    _now: function() {
        return typeof performance !== 'undefined' ? performance.now() : Date.now();
    },

    /** formId -> true for every unlocked spell in the tree */
    _unlockedSet: function() {
        var out = {};
        var nodes = this._nodes();
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].state === 'unlocked' && !nodes[i].originalFormId) out[nodes[i].formId] = true;
        }
        return out;
    },

    /** A new tree (load, import, build): nothing to compare with until it is closed once. */
    reset: function() {
        this._known = null;
        this._openUntil = 0;
        this._new = [];
        this._bursts = [];
        this._pendingBursts = [];
        this._footerKey = null;
        if (this._burstTimer) { clearTimeout(this._burstTimer); this._burstTimer = null; }
        this.refreshFooter();
    },

    /** onPanelHiding: note what is unlocked. */
    onHide: function() {
        if (!this._nodes().length) return;
        this._known = this._unlockedSet();
    },

    /** onPanelShowing: compare now (updates that came while hidden) and for a while after. */
    onShow: function() {
        this._openUntil = this._now() + this.OPEN_WINDOW_MS;
        this.compare();
    },

    /**
     * After a reply has updated the states. Outside an opening it does nothing:
     * a spell the player unlocks with the panel open is not news.
     * @returns {Array} the nodes found new by this call
     */
    compare: function() {
        if (!this._known || this._now() > this._openUntil) return [];
        var found = [];
        var nodes = this._nodes();
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            if (n.state !== 'unlocked' || n.originalFormId || this._known[n.formId]) continue;
            this._known[n.formId] = true;
            this._new.push(n);
            found.push(n);
        }
        if (found.length) {
            this._queueBursts(found);
            this.refreshFooter();
        }
        return found;
    },

    isNew: function(node) {
        return !!node && this._new.indexOf(node) !== -1;
    },

    /** The player has seen this spell's card. */
    markSeen: function(node) {
        var i = node ? this._new.indexOf(node) : -1;
        if (i === -1) return;
        this._new.splice(i, 1);
        this._frame();
        this.refreshFooter();
    },

    clearAll: function() {
        if (!this._new.length) return;
        this._new = [];
        this._frame();
        this.refreshFooter();
    },

    /** The new spells still marked, oldest first. */
    list: function() {
        return this._new.slice();
    },

    _ringWanted: function() {
        return !(typeof DesignEffectsSetting !== 'undefined' && DesignEffectsSetting._master === true);
    },

    _queueBursts: function(nodes) {
        if (!this._ringWanted()) { this._frame(); return; }
        var self = this;
        // Replies come one after the other: start once they have settled
        if (this._burstTimer) clearTimeout(this._burstTimer);
        this._pendingBursts = (this._pendingBursts || []).concat(nodes);
        this._burstTimer = setTimeout(function() {
            self._burstTimer = null;
            var start = self._now();
            var queued = self._pendingBursts || [];
            self._pendingBursts = [];
            for (var i = 0; i < queued.length && i < self.MAX_MARKS; i++) {
                self._bursts.push({ node: queued[i], start: start + i * self.BURST_STAGGER_MS });
            }
            self._frame();
        }, this.BURST_DELAY_MS);
    },

    /** One animation frame (the tree layer is pasted, not drawn). */
    _frame: function() {
        if (typeof CanvasRenderer === 'undefined') return;
        if (CanvasRenderer._requestAnimationOnlyFrame) CanvasRenderer._requestAnimationOnlyFrame();
        else CanvasRenderer._needsRender = true;
    },

    /** The mark's colour: the design's gold, read again when the design changes. */
    _markColor: function() {
        var design = typeof DesignPresets !== 'undefined' ? DesignPresets._appliedId : null;
        if (design !== this._colorDesign) { this._color = null; this._colorDesign = design; }
        if (!this._color && typeof getComputedStyle === 'function' && document.documentElement) {
            var c = getComputedStyle(document.documentElement).getPropertyValue('--accent-gold');
            this._color = (c && c.trim()) || '#e9c46a';
        }
        return this._color || '#e9c46a';
    },

    /**
     * Draw the rings and marks over the tree. Called by CanvasRenderer._drawMoving
     * every frame it draws; with FxLayer each goes on its own spot, otherwise on ctx.
     * @param {Object} r - CanvasRenderer
     * @param {Object} view
     * @param {number} dpr
     * @param {CanvasRenderingContext2D} [ctx] - no FxLayer this frame: draw here
     */
    drawSpots: function(r, view, dpr, ctx) {
        if (!this._new.length && !this._bursts.length) return;
        var self = this;
        var now = this._now();
        var color = this._markColor();
        var base = r._minSize ? r._minSize(r.KNOWN_SIZE || 8) : (r.KNOWN_SIZE || 8);
        var nodeR = Math.max(3, base * r.zoom);
        var running = false;

        for (var b = this._bursts.length - 1; b >= 0; b--) {
            var burst = this._bursts[b];
            var t = (now - burst.start) / this.BURST_MS;
            if (t >= 1) { this._bursts.splice(b, 1); continue; }
            running = true;
            if (t < 0) continue;
            var bp = r._worldToScreen(burst.node.x, burst.node.y, view);
            var reach = nodeR + this.BURST_REACH_PX + 4;
            (function(p, tt) {
                var paint = function(c) { self._paintBurst(c, p[0], p[1], nodeR, tt, color); };
                if (ctx) paint(ctx);
                else r._fxDraw('newring:' + b, p[0] - reach, p[1] - reach, 2 * reach, 2 * reach, dpr, paint);
            })(bp, t);
        }

        var markR = Math.max(3, Math.min(5, nodeR * 0.45));
        for (var i = 0; i < this._new.length && i < this.MAX_MARKS; i++) {
            var n = this._new[i];
            if (settings.schoolVisibility && settings.schoolVisibility[n.school] === false) continue;
            var p = r._worldToScreen(n.x, n.y, view);
            var mx = p[0] + nodeR * 0.75, my = p[1] - nodeR * 0.75;
            var box = markR + 3;
            (function(x, y) {
                var paint = function(c) { self._paintMark(c, x, y, markR, color); };
                if (ctx) { paint(ctx); return; }
                // Kept as it is while it stays put (version): a still mark costs nothing
                if (FxLayer.draw('newmark:' + i, x - box, y - box, 2 * box, 2 * box, dpr,
                        paint, { version: color + '|' + markR })) return;
                r._mainShownKey = null;
                paint(r.ctx);
            })(mx, my);
        }

        if (running) this._frame();
    },

    _paintBurst: function(c, x, y, r0, t, color) {
        var e = 1 - Math.pow(1 - t, 3);
        c.save();
        c.globalAlpha = 1 - t;
        c.strokeStyle = color;
        c.lineWidth = 1 + 2.5 * (1 - t);
        c.beginPath();
        c.arc(x, y, r0 + 2 + e * this.BURST_REACH_PX, 0, Math.PI * 2);
        c.stroke();
        c.restore();
    },

    _paintMark: function(c, x, y, rad, color) {
        c.save();
        c.fillStyle = color;
        c.strokeStyle = 'rgba(0, 0, 0, 0.75)';
        c.lineWidth = 1.5;
        c.beginPath();
        c.arc(x, y, rad, 0, Math.PI * 2);
        c.fill();
        c.stroke();
        c.restore();
    },

    // =========================================================================
    // NEXT GOALS
    // =========================================================================

    /** 0..1 progress of a spell, as its ring shows it */
    _pct: function(node) {
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer._getNodeProgressPct) {
            return CanvasRenderer._getNodeProgressPct(node) || 0;
        }
        return 0;
    },

    /**
     * The spells to work on: learning targets first, then learnable spells
     * with XP, most first. Hidden schools and edit-mode copies left out.
     * @returns {Array<{node: Object, pct: number}>}
     */
    goals: function() {
        var nodes = this._nodes();
        var learning = [], started = [];
        for (var i = 0; i < nodes.length; i++) {
            var n = nodes[i];
            if (n.originalFormId) continue;
            if (n.state !== 'learning' && n.state !== 'available') continue;
            if (typeof settings !== 'undefined' && settings.schoolVisibility && settings.schoolVisibility[n.school] === false) continue;
            var pct = this._pct(n);
            if (pct >= 1) continue;
            if (n.state === 'learning') learning.push({ node: n, pct: pct });
            else if (pct > 0) started.push({ node: n, pct: pct });
        }
        var byPct = function(a, b) { return b.pct - a.pct; };
        learning.sort(byPct);
        started.sort(byPct);
        return learning.concat(started).slice(0, this.GOAL_COUNT);
    },

    /** The name a goal chip shows: what the card would show (discovery mode). */
    goalName: function(node, pct) {
        var cheat = typeof settings !== 'undefined' && settings.cheatMode;
        var threshold = (typeof settings !== 'undefined' && settings.revealName !== undefined) ? settings.revealName : 10;
        var shown = cheat || node.state === 'learning' || pct * 100 >= threshold;
        if (typeof spellDisplayName === 'function') return spellDisplayName(node.formId, node, shown);
        return shown ? (node.name || node.formId) : '???';
    },

    // =========================================================================
    // FOOTER
    // =========================================================================

    /** Rebuild the footer chips when what they show changed. */
    refreshFooter: function() {
        if (typeof document === 'undefined') return;
        var box = document.getElementById('footer-goals');
        var newBtn = document.getElementById('footer-new');
        if (!box && !newBtn) return;
        var goals = this.goals();
        var self = this;
        var items = goals.map(function(g) {
            return { node: g.node, name: self.goalName(g.node, g.pct), pct: Math.floor(g.pct * 100) };
        });
        var key = this._new.length + '|' + items.map(function(g) { return g.node.formId + ':' + g.name + ':' + g.pct; }).join(',');
        if (key === this._footerKey) return;
        this._footerKey = key;

        if (newBtn) {
            newBtn.classList.toggle('hidden', !this._new.length);
            var cnt = document.getElementById('footer-new-count');
            if (cnt) cnt.textContent = this._new.length;
        }
        var wrap = document.getElementById('footer-goals-wrap');
        if (wrap) wrap.classList.toggle('hidden', !items.length);
        if (!box) return;
        while (box.firstChild) box.removeChild(box.firstChild);
        items.forEach(function(g) {
            var b = document.createElement('button');
            b.type = 'button';
            b.className = 'footer-chip' + (g.node.state === 'learning' ? ' is-learning' : '');
            var nm = document.createElement('span');
            nm.className = 'footer-chip-name';
            nm.textContent = g.name;
            var pc = document.createElement('span');
            pc.className = 'footer-chip-pct';
            pc.textContent = g.pct + '%';
            b.appendChild(nm);
            b.appendChild(pc);
            b.addEventListener('click', function() { self.goTo(g.node); });
            box.appendChild(b);
        });
    },

    /** Select a spell and bring it into view, as a click on it does. */
    goTo: function(node) {
        if (!node) return;
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.selectNodeAndFocus) {
            CanvasRenderer.selectNodeAndFocus(node);
        } else if (typeof showSpellDetails === 'function') {
            showSpellDetails(node);
        }
        this.markSeen(node);
    },

    /** The footer's "new" chip: the oldest new spell. */
    goToFirstNew: function() {
        if (this._new.length) this.goTo(this._new[0]);
    },

    init: function() {
        var self = this;
        var btn = typeof document !== 'undefined' ? document.getElementById('footer-new') : null;
        if (btn) btn.addEventListener('click', function() { self.goToFirstNew(); });
    }
};

if (typeof window !== 'undefined') {
    window.NewSpells = NewSpells;
}
