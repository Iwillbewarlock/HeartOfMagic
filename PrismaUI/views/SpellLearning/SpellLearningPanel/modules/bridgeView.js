/**
 * BridgeView - how the tree viewer shows the cross school bridges
 *
 * The saved tree carries `bridges` (SchoolBridges.applyToOutput). Drawn all at
 * once they are 180 lines across the whole wheel, so nothing is drawn until it
 * is asked for:
 *
 *  - a spell that has a bridge wears a thin ring
 *  - selecting or hovering it draws its bridges, dashed, in the colour of what
 *    the two spells share
 *  - the spell card lists them, and a click travels there
 *  - the trait filter veils the tree and lights every spell of one trait,
 *    whatever its school
 *
 * Depends on: state, settings, CanvasRenderer, SpellCard (labels), t()
 */
var BridgeView = {

    // Line and ring colour by the first trait the two spells share
    TRAIT_COLORS: {
        'element.fire': '#e8683a',
        'element.frost': '#6cc3e8',
        'element.shock': '#b48cf0',
        'element.poison': '#7fc25a',
        'element.sun': '#f0c850',
        'kind.bound': '#c8a0e0',
        'kind.summon': '#a080e0'
    },
    DEFAULT_COLOR: '#b8a878',

    MARKER_RADIUS: 10,
    VEIL_ALPHA: 0.62,
    VEIL_EXTENT: 20000,        // larger than any tree, in tree units
    FILTER_DOT: 5,
    MIN_SPELLS_PER_FILTER: 10, // kinds rarer than this do not get a button
    MAX_KIND_FILTERS: 8,       // the bar has to fit on one or two rows
    TOO_BROAD: 'kind.damage',  // every attack spell has it

    _byNode: {},
    _filterTrait: null,

    // =========================================================================
    // DATA
    // =========================================================================

    /** Called whenever a tree has been loaded into state.treeData. */
    setTree: function (treeData) {
        this._byNode = {};
        var raw = treeData && treeData.rawData;
        var bridges = (raw && raw.bridges) || [];
        for (var i = 0; i < bridges.length; i++) {
            var b = bridges[i];
            this._add(b.from, b.to, b, true);
            this._add(b.to, b.from, b, false);
        }
        this._filterTrait = null;
        this.buildFilterBar();
    },

    _add: function (ownId, otherId, bridge, isSource) {
        var list = this._byNode[ownId] || (this._byNode[ownId] = []);
        list.push({
            other: otherId,
            otherSchool: isSource ? bridge.toSchool : bridge.fromSchool,
            shared: bridge.shared || [],
            // Which way it opens: out of this spell, into it, or both
            opensOut: isSource || !!bridge.twoWay,
            opensIn: !isSource || !!bridge.twoWay
        });
    },

    forNode: function (node) {
        if (!node) return [];
        return this._byNode[node.formId] || this._byNode[node.id] || [];
    },

    colorOf: function (shared) {
        for (var i = 0; i < shared.length; i++) {
            if (this.TRAIT_COLORS[shared[i]]) return this.TRAIT_COLORS[shared[i]];
        }
        return this.DEFAULT_COLOR;
    },

    _labelOf: function (keyword) {
        if (keyword.indexOf('word.') === 0) return keyword.substring(5);
        return (typeof SpellCard !== 'undefined') ? SpellCard.label(keyword) : keyword;
    },

    // =========================================================================
    // CANVAS - called by CanvasRenderer inside the rotated tree transform
    // =========================================================================

    render: function (ctx, renderer) {
        if (!renderer || !renderer._nodeMap) return;
        if (this._filterTrait) this._renderFilter(ctx, renderer);
        this._renderMarkers(ctx, renderer);

        var shown = renderer.selectedNode;
        if (shown) this._renderBridgesOf(ctx, renderer, shown, 0.95);
        var hovered = renderer.hoveredNode;
        if (hovered && hovered !== shown) this._renderBridgesOf(ctx, renderer, hovered, 0.6);
    },

    _renderMarkers: function (ctx, renderer) {
        if (renderer._lodTier === 'minimal') return;
        ctx.save();
        ctx.lineWidth = 1;
        ctx.globalAlpha = 0.55;
        for (var id in this._byNode) {
            if (!this._byNode.hasOwnProperty(id)) continue;
            var node = renderer._nodeMap.get(id);
            if (!node) continue;
            // A spell the player has not reached yet keeps its secrets
            if (node.state === 'locked' && !settings.cheatMode) continue;
            ctx.strokeStyle = this.colorOf(this._byNode[id][0].shared);
            ctx.beginPath();
            ctx.arc(node.x, node.y, this.MARKER_RADIUS, 0, Math.PI * 2);
            ctx.stroke();
        }
        ctx.restore();
    },

    _renderBridgesOf: function (ctx, renderer, node, alpha) {
        var list = this.forNode(node);
        if (list.length === 0) return;
        ctx.save();
        ctx.lineWidth = 2;
        ctx.globalAlpha = alpha;
        if (ctx.setLineDash) ctx.setLineDash([8, 6]);
        for (var i = 0; i < list.length; i++) {
            var other = renderer._nodeMap.get(list[i].other);
            if (!other) continue;
            var color = this.colorOf(list[i].shared);
            ctx.strokeStyle = color;
            // Bowed toward the centre so the line does not hide the branches it passes
            ctx.beginPath();
            ctx.moveTo(node.x, node.y);
            ctx.quadraticCurveTo((node.x + other.x) * 0.25, (node.y + other.y) * 0.25, other.x, other.y);
            ctx.stroke();

            ctx.fillStyle = color;
            ctx.beginPath();
            ctx.arc(other.x, other.y, this.FILTER_DOT + 1, 0, Math.PI * 2);
            ctx.fill();
        }
        if (ctx.setLineDash) ctx.setLineDash([]);
        ctx.restore();
    },

    _renderFilter: function (ctx, renderer) {
        var trait = this._filterTrait;
        var color = this.TRAIT_COLORS[trait] || this.DEFAULT_COLOR;
        var e = this.VEIL_EXTENT;
        ctx.save();
        ctx.globalAlpha = this.VEIL_ALPHA;
        ctx.fillStyle = '#000000';
        ctx.fillRect(-e, -e, e * 2, e * 2);

        ctx.globalAlpha = 1;
        var nodes = renderer.nodes || [];
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            if (!node.traits || node.traits.indexOf(trait) < 0) continue;
            ctx.fillStyle = renderer._getSchoolColor(node.school);
            ctx.beginPath();
            ctx.arc(node.x, node.y, this.FILTER_DOT, 0, Math.PI * 2);
            ctx.fill();
            ctx.strokeStyle = color;
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.arc(node.x, node.y, this.FILTER_DOT + 3, 0, Math.PI * 2);
            ctx.stroke();
        }
        ctx.restore();
    },

    _redraw: function () {
        if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
    },

    // =========================================================================
    // SPELL CARD
    // =========================================================================

    renderCard: function (node) {
        var section = document.getElementById('details-bridges-section');
        var listEl = document.getElementById('spell-bridges');
        if (!section || !listEl) return;
        listEl.innerHTML = '';

        var list = this.forNode(node);
        if (list.length === 0) { section.classList.add('hidden'); return; }
        section.classList.remove('hidden');

        var self = this;
        list.forEach(function (entry) {
            var other = (typeof _findNodeById === 'function' && state.treeData) ? _findNodeById(entry.other) : null;
            var showName = settings.cheatMode || (other && other.state !== 'locked');
            var arrow = entry.opensOut && entry.opensIn ? '↔ ' : (entry.opensOut ? '→ ' : '← ');
            var name = showName ? (other ? (other.name || other.formId) : entry.other) : '???';
            var what = entry.shared.map(function (k) { return self._labelOf(k); }).join(', ');

            var li = document.createElement('li');
            li.dataset.id = entry.other;
            li.className = 'bridge-item';
            // Reachable and usable without a mouse
            li.tabIndex = 0;
            li.setAttribute('role', 'button');
            li.addEventListener('keydown', function (e) {
                if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); li.click(); }
            });
            li.style.borderLeftColor = self.colorOf(entry.shared);
            // Plain text: the list's click handler only reacts to the <li> itself
            li.textContent = arrow + name + '  ·  ' + entry.otherSchool + (what ? ' · ' + what : '');
            listEl.appendChild(li);
        });
    },

    // =========================================================================
    // TRAIT FILTER
    // =========================================================================

    /** One button per element, and per kind that enough spells carry. */
    buildFilterBar: function () {
        var bar = document.getElementById('tree-trait-filter');
        if (!bar) return;
        bar.innerHTML = '';

        var nodes = (state.treeData && state.treeData.nodes) || [];
        var counts = {};
        for (var i = 0; i < nodes.length; i++) {
            var traits = nodes[i].traits || [];
            for (var k = 0; k < traits.length; k++) {
                var trait = traits[k];
                if (trait === this.TOO_BROAD) continue;
                if (trait.indexOf('element.') !== 0 && trait.indexOf('kind.') !== 0) continue;
                counts[trait] = (counts[trait] || 0) + 1;
            }
        }
        var self = this;
        var shown = Object.keys(counts).filter(function (trait) {
            return trait.indexOf('element.') === 0 || counts[trait] >= self.MIN_SPELLS_PER_FILTER;
        });
        shown.sort(function (a, b) {
            var ea = a.indexOf('element.') === 0 ? 0 : 1, eb = b.indexOf('element.') === 0 ? 0 : 1;
            if (ea !== eb) return ea - eb;
            if (counts[b] !== counts[a]) return counts[b] - counts[a];
            return a < b ? -1 : 1;
        });
        if (shown.length === 0) return;
        var elements = shown.filter(function (trait) { return trait.indexOf('element.') === 0; });
        shown = shown.slice(0, elements.length + this.MAX_KIND_FILTERS);

        shown.forEach(function (trait) {
            var btn = document.createElement('button');
            btn.className = 'trait-filter-btn';
            btn.setAttribute('data-trait', trait);
            btn.setAttribute('type', 'button');
            btn.setAttribute('aria-pressed', 'false');
            btn.textContent = self._labelOf(trait) + ' ' + counts[trait];
            btn.style.borderBottomColor = self.TRAIT_COLORS[trait] || self.DEFAULT_COLOR;
            btn.addEventListener('click', function () { self.toggleFilter(trait); });
            bar.appendChild(btn);
        });
    },

    toggleFilter: function (trait) {
        this._filterTrait = (this._filterTrait === trait) ? null : trait;
        var bar = document.getElementById('tree-trait-filter');
        if (bar) {
            var buttons = bar.querySelectorAll('.trait-filter-btn');
            for (var i = 0; i < buttons.length; i++) {
                var on = buttons[i].getAttribute('data-trait') === this._filterTrait;
                if (on) buttons[i].classList.add('active'); else buttons[i].classList.remove('active');
                buttons[i].setAttribute('aria-pressed', on ? 'true' : 'false');
            }
        }
        this._redraw();
    }
};
