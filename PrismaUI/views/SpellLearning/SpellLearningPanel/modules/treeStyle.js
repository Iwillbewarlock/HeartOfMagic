/**
 * TreeStyle - how the spell tree looks, as a set of tokens a design preset can change.
 *
 * CanvasRenderer asks TreeStyle for colours, label font and which effects are on,
 * and calls the draw helpers below for the effects. Every effect is picked so it
 * adds almost nothing to a frame:
 * - halos are one drawImage of a sprite that is made once per colour;
 * - rings and edges only change style, and edges are stroked in batches;
 * - the moving parts (the selection sigil, the learning glow) are painted over the
 *   finished tree layer, a handful of paint calls, so the 1440 spells in the layer
 *   are never redrawn to animate them. They move only when the heart, globe or
 *   starfield already asks for frames; they never ask for frames of their own.
 *
 * DEFAULTS is the look the tree had before presets existed ("Classic"): with no
 * preset applied nothing changes. Presets live in designPresets.js; the book
 * effects (page, chapters) are added to TreeStyle by treeStyleBook.js. (The ink
 * reveal on opening is gone: a design's reveal tokens are ignored.)
 *
 * Depends on: nothing (CanvasRenderer reads TreeStyle.tokens if it is there)
 */

var TreeStyle = {

    DEFAULTS: {
        // Labels
        labelFont: 'sans-serif',
        labelMaxChars: 12,
        labelHalo: '',                              // '' = no outline behind names
        labelHaloWidth: 3,
        labelUnlocked: '#ffffff',
        labelAvailable: 'rgba(255, 255, 255, 0.7)',
        labelHidden: 'rgba(255, 255, 255, 0.35)',

        // Page - '' pageColor keeps the starfield; a colour draws a still page instead
        pageColor: '',
        pageGlow: '',                               // warm light in the middle ('' = none)
        pageGlowAlpha: 0.5,
        pageGlowRadius: 0.6,                        // share of the larger screen side
        pageEdge: '#000000',                        // darkened page edges
        pageEdgeAlpha: 0,
        pageGrain: 0,                               // 0..1 fibres and blotches in the page
        pageGrainColor: '#000000',

        // Nodes
        nodeFill: '#1a1a2e',                        // inside of learnable and locked spells
        unlockedFill: '',                           // '' = school colour
        unlockedRim: '',                            // '' = school colour
        unlockedCore: '',                           // '' = a dark shade of the school colour
        lockedStroke: '',                           // '' = school colour (dimmed by alpha)
        mysteryFill: 'rgba(20, 20, 30, 0.9)',
        focusStroke: '#ffffff',                     // outline of the selected and hovered spell
        ringTrack: '#ffffff',                       // empty part of the XP ring
        availableAlpha: 0.8,                        // spells that can be learned now
        availableRing: false,                       // thin school-coloured ring round them
        nodeGlow: 0,                                // halo strength behind unlocked spells (0 = off)
        learningGlow: 0,                            // halo strength behind the spell being learned
        hubFill: '',                                // '' = the heart background from settings
        hubRing: '',                                // '' = the heart ring colour from settings
        hubText: '',                                // '' = the heart text colour from settings
        globeColor: '',                             // '' = the globe particle colour from settings
        schoolInk: 0,                               // 0..1: school colours mixed toward schoolInkTone,
        schoolInkTone: '#000000',                   //   so the player's colours still read on a light page
        learningColor: '',                          // '' = the player's learning colour setting

        // Edges
        dimEdgeColor: '#222222',                    // off-path edges while a spell is selected
        unlockedEdgeColor: '',                      // '' = school colour
        lockedEdgeColor: '#333333',
        lockedEdgeAlpha: 0.15,
        unlockedEdgeAlpha: 0.5,
        unlockedEdgeWidth: 2,
        edgeGlow: 0,                                // soft wide stroke under unlocked edges (0 = off)
        frontierEdgeAlpha: 0,                       // unlocked -> learnable edges in school colour (0 = drawn as locked)
        selectedPathColor: '#555555',
        selectedPathAlpha: 0.5,
        selectedPathWidth: 2,
        hoverPathAlpha: 0.55,

        // Overlay and hub
        accent: '#e8c874',                          // sigil, hub runes, chapter titles, ornamented dividers
        selectionSigil: false,
        hubRunes: false,

        // Book
        chapterTitles: false,                       // school names round the outside, like chapter headings
        chapterSize: 15,                            // screen px, the same at every zoom
        dividerOrnament: false                      // dividers as thin double rules with an end mark
    },

    tokens: null,

    /**
     * The effects a player can turn off (Render settings > Design Effects), one
     * entry per effect: the token it lives in, the value that turns it off, and
     * whether a design's overrides use it. The one list both the switch controls
     * (designEffectsSetting.js) and set() read, so a new effect is added here only.
     */
    EFFECTS: [
        { key: 'page',   token: 'pageColor',      off: '',    uses: function(o) { return !!o.pageColor; } },
        { key: 'sigil',  token: 'selectionSigil', off: false, uses: function(o) { return o.selectionSigil === true; } },
        { key: 'glow',   token: 'learningGlow',   off: 0,     uses: function(o) { return o.learningGlow > 0; } },
        { key: 'runes',  token: 'hubRunes',       off: false, uses: function(o) { return o.hubRunes === true; } }
    ],
    _spriteCache: {},

    /**
     * Replace the tokens: DEFAULTS, then whatever the preset sets.
     * Unknown keys are ignored, so an add-on written for a later version still loads.
     * @param {Object} [overrides]
     */
    set: function(overrides) {
        var t = {};
        var d = this.DEFAULTS;
        for (var k in d) {
            if (!d.hasOwnProperty(k)) continue;
            var v = overrides ? overrides[k] : undefined;
            t[k] = (v !== undefined && v !== null && typeof v === typeof d[k]) ? v : d[k];
        }
        this.overrides = overrides || {};
        // Effects the player turned off (Render settings > Design Effects) stay off
        // in every design
        var off = this.effectsOff || {};
        for (var i = 0; i < this.EFFECTS.length; i++) {
            var fx = this.EFFECTS[i];
            if (off[fx.key]) t[fx.token] = fx.off;
        }
        this.tokens = t;
        this._spriteCache = {};
        this._inkCache = {};
        this._page = null;
        // Node buckets hold each spell's colour; they have to take the new ink
        if (typeof CanvasRenderer !== 'undefined' && CanvasRenderer.nodes && CanvasRenderer.nodes.length &&
            CanvasRenderer._buildNodeBuckets) {
            CanvasRenderer._buildNodeBuckets();
        }
        if (typeof CanvasRenderer !== 'undefined') CanvasRenderer._needsRender = true;
    },

    /**
     * Effects the player turned off, whatever the design sets: { page, sigil,
     * glow, runes } (true = off). Re-applies the current design's tokens.
     * @param {Object} off
     */
    setEffectsOff: function(off) {
        this.effectsOff = off || {};
        this.set(this.overrides);
    },

    // =========================================================================
    // SPRITES
    // =========================================================================

    /** A soft round glow in `color`, drawn once and reused. Null if no canvas can be made. */
    _glowSprite: function(color) {
        var sprite = this._spriteCache[color];
        if (sprite !== undefined) return sprite;
        sprite = null;
        try {
            var size = 64;
            var c = document.createElement('canvas');
            c.width = size;
            c.height = size;
            var g = c.getContext('2d');
            var grad = g.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
            grad.addColorStop(0, this._rgba(color, 0.9));
            grad.addColorStop(0.35, this._rgba(color, 0.35));
            grad.addColorStop(1, this._rgba(color, 0));
            g.fillStyle = grad;
            g.fillRect(0, 0, size, size);
            sprite = c;
        } catch (e) {
            sprite = null;
        }
        this._spriteCache[color] = sprite;
        return sprite;
    },

    _inkCache: {},

    /**
     * A school colour as this preset's ink: mixed toward schoolInkTone by schoolInk.
     * The player's own colour choice still decides the hue. Cached per colour.
     */
    ink: function(color) {
        var t = this.tokens;
        if (!(t.schoolInk > 0) || !color) return color;
        var hit = this._inkCache[color];
        if (hit) return hit;
        var out = color;
        if (typeof CanvasRenderer !== 'undefined') {
            var a = CanvasRenderer.parseColor(color), b = CanvasRenderer.parseColor(t.schoolInkTone);
            if (a && b) {
                var k = t.schoolInk;
                var hex = function(v) { v = Math.round(v); return (v < 16 ? '0' : '') + v.toString(16); };
                out = '#' + hex(a.r + (b.r - a.r) * k) + hex(a.g + (b.g - a.g) * k) + hex(a.b + (b.b - a.b) * k);
            }
        }
        this._inkCache[color] = out;
        return out;
    },

    _rgba: function(color, alpha) {
        if (typeof CanvasRenderer !== 'undefined') {
            var rgb = CanvasRenderer.parseColor(color);
            if (rgb) return 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + alpha + ')';
        }
        return color;
    },

    /**
     * Glow centred on the current origin.
     * @param {number} radius - world units
     * @param {number} alpha - 0..1
     */
    drawHalo: function(ctx, radius, color, alpha) {
        if (alpha <= 0) return;
        var sprite = this._glowSprite(color);
        if (!sprite) return;
        ctx.globalAlpha = alpha > 1 ? 1 : alpha;
        ctx.drawImage(sprite, -radius, -radius, radius * 2, radius * 2);
    },

    // =========================================================================
    // LABELS
    // =========================================================================

    /** Shorten a name to labelMaxChars, marking the cut. */
    clipLabel: function(text) {
        var max = this.tokens.labelMaxChars;
        if (!text || text.length <= max) return text;
        return text.substring(0, max - 1) + '…';
    },

    /** Prepare ctx for a run of labels (font, outline). */
    beginLabels: function(ctx, fontSize) {
        var t = this.tokens;
        ctx.font = fontSize + 'px ' + t.labelFont;
        if (t.labelHalo) {
            ctx.strokeStyle = t.labelHalo;
            ctx.lineWidth = t.labelHaloWidth;
            ctx.lineJoin = 'round';
        }
    },

    drawLabel: function(ctx, text, x, y, color) {
        if (this.tokens.labelHalo) ctx.strokeText(text, x, y);
        ctx.fillStyle = color;
        ctx.fillText(text, x, y);
    },

    // =========================================================================
    // OVERLAY - painted over the tree layer every frame
    // =========================================================================

    /**
     * The selection sigil and the learning glow. `view` is the one render() made.
     * With `only`, just that spell's glow and sigil (CanvasRenderer draws each
     * spell's on its own small canvas, FxLayer; overlayNodes says which).
     * @param {Object} r - CanvasRenderer
     * @param {Object} [only] - a node
     */
    renderOverlay: function(ctx, r, view, only) {
        var t = this.tokens;
        var sel = t.selectionSigil ? r.selectedNode : null;
        var learning = t.learningGlow > 0 && r._learningNodeIds instanceof Set && r._learningNodeIds.size > 0;
        if (!sel && !learning) return;

        var now = (typeof performance !== 'undefined' ? performance.now() : Date.now()) / 1000;
        ctx.save();
        ctx.translate(view.cx + r.panX, view.cy + r.panY);
        ctx.rotate(view.rotRad);
        ctx.scale(r.zoom, r.zoom);

        if (learning) {
            var breath = 0.55 + 0.45 * Math.sin(now * 2.4);
            var color = r._learningColor();
            r._learningNodeIds.forEach(function(id) {
                if (only && only.id !== id) return;
                var n = r._nodeMap ? r._nodeMap.get(id) : null;
                if (!n || n.state !== 'learning') return;
                if (settings.schoolVisibility && settings.schoolVisibility[n.school] === false) return;
                ctx.save();
                ctx.translate(n.x, n.y);
                TreeStyle.drawHalo(ctx, r._minSize(12) * 3.2, color, t.learningGlow * breath);
                ctx.restore();
            });
        }

        if (sel && (!only || only.id === sel.id)) this._drawSigil(ctx, r, sel, now);

        ctx.restore();
        ctx.globalAlpha = 1;
    },

    /** The spells renderOverlay would draw on right now: the selected one and the glowing learning ones. */
    overlayNodes: function(r) {
        var t = this.tokens;
        var out = [];
        var sel = t.selectionSigil ? r.selectedNode : null;
        if (t.learningGlow > 0 && r._learningNodeIds instanceof Set) {
            r._learningNodeIds.forEach(function(id) {
                var n = r._nodeMap ? r._nodeMap.get(id) : null;
                if (!n || n.state !== 'learning') return;
                if (settings.schoolVisibility && settings.schoolVisibility[n.school] === false) return;
                if (sel && sel.id === n.id) return;
                out.push(n);
            });
        }
        if (sel) out.push(sel);
        return out;
    },

    /** How far (world units) a spell's glow or sigil reaches from its centre. */
    overlayExtent: function(r) {
        var z = r.zoom || 1;
        var glow = r._minSize(12) * 3.2;
        var inner = Math.max(r._minSize(12) + 7, 14 / z);
        var sigil = inner + Math.max(5, 7 / z) + 4 / z;
        return Math.max(glow, sigil) + 2 / z;
    },

    /** Two counter-turning rune rings round the selected spell. About six paint calls. */
    _drawSigil: function(ctx, r, node, now) {
        var z = r.zoom || 1;
        var inner = Math.max(r._minSize(12) + 7, 14 / z);
        var outer = inner + Math.max(5, 7 / z);
        var line = 1.2 / z;

        ctx.save();
        ctx.translate(node.x, node.y);
        ctx.strokeStyle = this.tokens.accent;
        ctx.fillStyle = this.tokens.accent;

        // Inner ring, turning one way
        ctx.rotate(now * 0.6);
        ctx.globalAlpha = 0.9;
        ctx.lineWidth = line;
        ctx.beginPath();
        ctx.arc(0, 0, inner, 0, Math.PI * 2);
        ctx.stroke();

        // Three rune marks on it, one path
        ctx.beginPath();
        for (var i = 0; i < 3; i++) {
            var a = i * Math.PI * 2 / 3;
            var px = Math.cos(a) * inner, py = Math.sin(a) * inner, s = 2.6 / z;
            ctx.moveTo(px + Math.cos(a) * s, py + Math.sin(a) * s);
            ctx.lineTo(px - Math.sin(a) * s, py + Math.cos(a) * s);
            ctx.lineTo(px - Math.cos(a) * s, py - Math.sin(a) * s);
            ctx.lineTo(px + Math.sin(a) * s, py - Math.cos(a) * s);
            ctx.closePath();
        }
        ctx.fill();

        // Outer ring, dashed, turning the other way
        ctx.rotate(-now * 1.1);
        ctx.globalAlpha = 0.55;
        ctx.setLineDash([4 / z, 3 / z]);
        ctx.beginPath();
        ctx.arc(0, 0, outer, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);

        ctx.restore();
    },

    // =========================================================================
    // HUB
    // =========================================================================

    /**
     * Rune circle round the heart: a ring of ticks and a slowly turning dashed ring.
     * Three paint calls; drawn with the hub, which is drawn every frame anyway.
     */
    renderHubRunes: function(ctx, radius) {
        var t = this.tokens;
        if (!t.hubRunes) return;
        var now = (typeof performance !== 'undefined' ? performance.now() : Date.now()) / 1000;
        var tickIn = radius + 12, tickOut = radius + 17, ticks = 36;

        ctx.save();
        ctx.strokeStyle = t.accent;
        ctx.lineWidth = 1;
        ctx.globalAlpha = 0.45;
        ctx.beginPath();
        ctx.arc(0, 0, tickIn, 0, Math.PI * 2);
        for (var i = 0; i < ticks; i++) {
            var a = i * Math.PI * 2 / ticks;
            var len = (i % 3 === 0) ? tickOut + 3 : tickOut;
            ctx.moveTo(Math.cos(a) * tickIn, Math.sin(a) * tickIn);
            ctx.lineTo(Math.cos(a) * len, Math.sin(a) * len);
        }
        ctx.stroke();

        ctx.rotate(-now * 0.15);
        ctx.globalAlpha = 0.3;
        ctx.setLineDash([10, 6, 2, 6]);
        ctx.beginPath();
        ctx.arc(0, 0, tickOut + 8, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.restore();
    }
};

TreeStyle.set(null);

window.TreeStyle = TreeStyle;
