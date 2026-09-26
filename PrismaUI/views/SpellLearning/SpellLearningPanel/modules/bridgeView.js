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
    FILTER_DOT_SCREEN_PX: 4,   // a lit spell never shrinks below this on screen
    TOO_BROAD: 'kind.damage',  // says only "this hurts"; too many spells share it

    _byNode: {},
    _filterTrait: null,
    _counts: {},

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
        this.countTraits();
        this._renderActiveChip();
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

    render: function (ctx, renderer, bounds) {
        if (!renderer || !renderer._nodeMap) return;
        if (this._filterTrait) this._renderFilter(ctx, renderer, bounds);
        this._renderMarkers(ctx, renderer, bounds);

        var shown = renderer.selectedNode;
        if (shown) this._renderBridgesOf(ctx, renderer, shown, 0.95);
        var hovered = renderer.hoveredNode;
        if (hovered && hovered !== shown) this._renderBridgesOf(ctx, renderer, hovered, 0.6);
    },

    /**
     * The same three tests renderNodes makes. Without them this module would
     * draw a ring or a dot where the renderer is deliberately drawing nothing -
     * in discovery mode that would give away an undiscovered spell's place.
     */
    isHidden: function (renderer, node) {
        if (!node) return true;
        if (settings.schoolVisibility && settings.schoolVisibility[node.school] === false) return true;
        var editing = typeof EditMode !== 'undefined' && EditMode.isActive;
        if (renderer._discoveryVisibleIds && !editing) {
            if (!renderer._discoveryVisibleIds.has(node.id) &&
                !renderer._discoveryVisibleIds.has(node.formId)) return true;
        }
        return false;
    },

    _outsideView: function (node, bounds) {
        if (!bounds) return false;
        return node.x < bounds.left || node.x > bounds.right ||
               node.y < bounds.top || node.y > bounds.bottom;
    },

    /** Does this spell carry the trait the filter is on? */
    matchesFilter: function (node) {
        if (!this._filterTrait) return true;
        return !!node && !!node.traits && node.traits.indexOf(this._filterTrait) >= 0;
    },

    hasFilter: function () {
        return !!this._filterTrait;
    },

    isFilter: function (trait) {
        return this._filterTrait === trait;
    },

    countOf: function (trait) {
        return this._counts[trait] || 0;
    },

    /**
     * Can this keyword light up the tree? The school is left out - the school
     * tabs do that - and so is the one kind every attack spell carries.
     */
    isFilterable: function (trait) {
        if (!trait || trait === this.TOO_BROAD) return false;
        return String(trait).indexOf('school.') !== 0;
    },

    _renderMarkers: function (ctx, renderer, bounds) {
        if (renderer._lodTier === 'minimal') return;
        ctx.save();
        ctx.lineWidth = 1;
        ctx.globalAlpha = 0.55;
        for (var id in this._byNode) {
            if (!this._byNode.hasOwnProperty(id)) continue;
            var node = renderer._nodeMap.get(id);
            if (!node || this.isHidden(renderer, node) || this._outsideView(node, bounds)) continue;
            // A spell the player has not reached yet keeps its secrets
            if (node.state === 'locked' && !settings.cheatMode) continue;
            if (!this.matchesFilter(node)) continue;
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
            // Not culled by the viewport: a line running off the edge still
            // tells the player which way the other spell lies.
            if (!other || this.isHidden(renderer, other)) continue;
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

    _renderFilter: function (ctx, renderer, bounds) {
        var trait = this._filterTrait;
        var color = this.TRAIT_COLORS[trait] || this.DEFAULT_COLOR;
        ctx.save();
        ctx.globalAlpha = this.VEIL_ALPHA;
        // Drawn into the see-through tree layer (the page and stars are under it,
        // on the tree canvas): what the tree drew so far is faded out of the
        // layer instead of painted over, so the page shows as it is. A black
        // veil turned a dark design's page (Candlelit Tome) all but black, and
        // one in the page's colour flattened its light and texture. Drawn
        // straight onto the tree canvas (no layer), the page is under the veil:
        // the colour behind the tree, so the rest fades into it.
        var intoLayer = ctx !== renderer.ctx;
        if (intoLayer) {
            ctx.globalCompositeOperation = 'destination-out';
            ctx.fillStyle = '#000000';
        } else {
            ctx.fillStyle = (renderer._backdrop && renderer._backdrop()) || '#000000';
        }
        if (bounds) {
            // The bounds are axis-aligned in tree space while the wheel is turned,
            // so a rect of exactly that size would leave the corners bare. Double it.
            var w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
            ctx.fillRect(bounds.left - w / 2, bounds.top - h / 2, w * 2, h * 2);
        } else {
            ctx.fillRect(-this.VEIL_EXTENT, -this.VEIL_EXTENT, this.VEIL_EXTENT * 2, this.VEIL_EXTENT * 2);
        }

        ctx.globalAlpha = 1;
        ctx.globalCompositeOperation = 'source-over';
        // A filter is most useful looking at the whole wheel, and there a dot of
        // a fixed size in tree units is a pinprick. Keep it the same on screen.
        var zoom = renderer.zoom || 1;
        var dot = Math.max(this.FILTER_DOT, this.FILTER_DOT_SCREEN_PX / zoom);
        var halo = dot + Math.max(3, 3 / zoom);
        ctx.lineWidth = Math.max(1.5, 1.5 / zoom);

        var nodes = renderer.nodes || [];
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            if (!node.traits || node.traits.indexOf(trait) < 0) continue;
            if (this.isHidden(renderer, node) || this._outsideView(node, bounds)) continue;
            ctx.fillStyle = renderer._getSchoolColor(node.school);
            ctx.beginPath();
            ctx.arc(node.x, node.y, dot, 0, Math.PI * 2);
            ctx.fill();
            ctx.strokeStyle = color;
            ctx.beginPath();
            ctx.arc(node.x, node.y, halo, 0, Math.PI * 2);
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

    renderCard: function (node, revealed) {
        var section = document.getElementById('details-bridges-section');
        var listEl = document.getElementById('spell-bridges');
        if (!section || !listEl) return;
        listEl.innerHTML = '';

        var list = (revealed === false) ? [] : this.forNode(node);
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
            // School names are translated under the same keys the spell card's chips use
            var school = self._labelOf('school.' + String(entry.otherSchool).toLowerCase());
            if (school.indexOf('school.') === 0) school = entry.otherSchool;
            li.textContent = arrow + name + '  ·  ' + school + (what ? ' · ' + what : '');
            listEl.appendChild(li);
        });
    },

    // =========================================================================
    // TRAIT FILTER
    // =========================================================================

    /**
     * Counts every keyword the tree carries, so the spell card knows which of
     * its chips can be pressed. There is no bar of keyword buttons: the card's
     * own chips are the filter, and a bar of 13 more was just clutter.
     */
    countTraits: function () {
        var nodes = (state.treeData && state.treeData.nodes) || [];
        var counts = {};
        for (var i = 0; i < nodes.length; i++) {
            var traits = nodes[i].traits || [];
            for (var k = 0; k < traits.length; k++) {
                counts[traits[k]] = (counts[traits[k]] || 0) + 1;
            }
        }
        this._counts = counts;
    },

    /**
     * While a filter is on, one pill above the tree says which keyword it is
     * and turns it off again. Without it a filter set from a spell card could
     * only be cleared from that same card.
     */
    _renderActiveChip: function () {
        var bar = document.getElementById('tree-trait-filter');
        if (!bar) return;
        bar.innerHTML = '';
        if (!this._filterTrait) return;

        var self = this;
        var btn = document.createElement('button');
        btn.className = 'trait-filter-active';
        btn.setAttribute('type', 'button');
        btn.textContent = this._labelOf(this._filterTrait) + ' ' + this.countOf(this._filterTrait) + '  \u00d7';
        btn.title = (typeof t === 'function') ? t('tree.clearTraitFilter') : 'Clear filter';
        btn.style.borderBottomColor = this.TRAIT_COLORS[this._filterTrait] || this.DEFAULT_COLOR;
        btn.addEventListener('click', function () { self.toggleFilter(self._filterTrait); });
        bar.appendChild(btn);
    },
    toggleFilter: function (trait) {
        if (!this.isFilterable(trait)) return;
        this._filterTrait = (this._filterTrait === trait) ? null : trait;
        this._markPressed(document.querySelectorAll('.spell-chip-filter'));
        this._renderActiveChip();
        this._redraw();
    },

    _markPressed: function (elements) {
        for (var i = 0; i < elements.length; i++) {
            var on = elements[i].getAttribute('data-trait') === this._filterTrait;
            if (on) elements[i].classList.add('active'); else elements[i].classList.remove('active');
            elements[i].setAttribute('aria-pressed', on ? 'true' : 'false');
        }
    }
};
