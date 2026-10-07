/**
 * StarCadenceTest - node tests for the stars' own pace and the still tree canvas
 * (run-tests.js), with stand-in canvases (no drawing):
 * - Starfield: advance() moves the stars (world-space phase, fixed mode drift)
 *   by AnimClock's steps, at most every STAR_FRAME_MS, never while held still;
 *   drawing does not move them; `moves` counts the moves.
 * - A move changes a star's opacity by under one OPACITY_STEP (the step it is
 *   drawn in), also at the most steps a move can take.
 * - CanvasRenderer._syncStarfield: a move only when due, frames asked for while
 *   the stars move, none held still or under a design page.
 * - CanvasRenderer._drawTree with StaticBase: a key's first frame draws straight
 *   and the next frames with that key leave the tree canvas alone (FxLayer on);
 *   a star move draws once; the learning path or no FxLayer: drawn each frame.
 *
 * Depends on: Starfield, StaticBase, AnimClock, CanvasRenderer (canvasRendererV2.js,
 * canvasRendererFrame.js)
 */

var StarCadenceTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var SF = g.Starfield, SB = g.StaticBase, AC = g.AnimClock, CR = g.CanvasRenderer;
        if (!SF || !SB || !AC || !CR || typeof CR._syncStarfield !== 'function') {
            this.check(false, 'Starfield, StaticBase, AnimClock and CanvasRenderer loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var saved = { perf: g.performance, ts: g.TreeStyle, still: SF.still, phase: SF._twinklePhase, moves: SF.moves,
                      at: SF._advancedAt, stars: SF.stars, last: AC._last, carry: AC._carry };
        try {
            this._starfieldTests(g, SF, AC, CR);
            this._syncTests(g, SF, AC, CR);
            this._shownKeyTests(g, SF, SB, CR);
        } finally {
            g.performance = saved.perf; g.TreeStyle = saved.ts; SF.still = saved.still; SF._twinklePhase = saved.phase;
            SF.moves = saved.moves; SF._advancedAt = saved.at; SF.stars = saved.stars; AC._last = saved.last;
            AC._carry = saved.carry; SB.reset();
        }
        return { passed: this.passed, failed: this.failed };
    },

    _clock: function(g) {
        var t = { now: 1000 };
        g.performance = { now: function() { return t.now; } };
        return t;
    },

    _starfieldTests: function(g, SF, AC, CR) {
        var t = this._clock(g);
        AC._last = {}; AC._carry = {};
        SF.still = false; SF._twinklePhase = 0; SF.moves = 0; SF._advancedAt = -1e9;
        this.check(SF.due(t.now), 'a first move is due');
        SF.advance(false, t.now);
        var phase1 = SF._twinklePhase, moves1 = SF.moves;
        this.check(phase1 === SF.twinkleSpeed && moves1 === 1, 'a first move: one step of the phase, counted');
        t.now += SF.STAR_FRAME_MS - 1;
        this.check(!SF.due(t.now), 'not due before STAR_FRAME_MS');
        t.now += 1;
        this.check(SF.due(t.now), 'due at STAR_FRAME_MS');
        SF.advance(false, t.now);
        var steps = Math.floor(SF.STAR_FRAME_MS / AC.STEP_MS);
        this.check(Math.abs(SF._twinklePhase - phase1 - steps * SF.twinkleSpeed) < 1e-9 && SF.moves === 2,
            'the phase moves by the steps due since the last move (the speed stays)');
        var before = SF._twinklePhase;
        SF._bucketBegin = function() {}; SF._bucketFlush = function() {};
        SF.renderWorldSpace({}, 0, 0, 1, 10, 10);
        delete SF._bucketBegin; delete SF._bucketFlush;
        this.check(SF._twinklePhase === before, 'drawing does not move the stars');
        SF.still = true;
        t.now += 10000;
        this.check(!SF.due(t.now), 'held still: never due');
        SF.advance(false, t.now);
        this.check(SF._twinklePhase === before && SF.moves === 2, 'held still: advance moves nothing');
        SF.still = false;

        // Fixed mode drifts each star
        SF.stars = [{ x: 5, y: 5, size: 1, phase: 0, twinkleRate: 1, baseOpacity: 0.5, dx: 0.5, dy: 0 }];
        SF.width = 100; SF.height = 100;
        t.now += 1000;
        SF.advance(true, t.now);
        this.check(SF.stars[0].x > 5 && SF.stars[0].phase > 0 && SF.moves === 3, 'fixed mode: the stars drift and twinkle');

        // A move changes a star's opacity by under one drawn step. Moves come every
        // second animation frame: the time since the last move, plus AnimClock's carry
        // under one step, in steps
        this.check(SF.STAR_FRAME_MS <= 2 * CR.ANIMATION_FRAME_MS && SF.STAR_FRAME_MS > CR.ANIMATION_FRAME_MS,
            'the stars move every second animation frame');
        this.check(SF.STAR_FRAME_MS <= CR.IDLE_FRAME_MS, 'never further apart than the idle frames already moved them');
        var gap = 2 * CR.ANIMATION_FRAME_MS;
        var maxSteps = Math.floor((gap + AC.STEP_MS - 1) / AC.STEP_MS);
        var maxOpacity = 0;
        for (var i = 0; i < SF._layers.length; i++) maxOpacity = Math.max(maxOpacity, SF._layers[i].opacityMax);
        var maxRate = 2.0;     // twinkleRate 0.5 .. 2 (_tileStars)
        var change = maxOpacity * 0.5 * maxRate * SF.twinkleSpeed * maxSteps;
        this.check(change <= SF.OPACITY_STEP, 'a world-space star changes by at most one drawn opacity step a move (' +
            change.toFixed(3) + ' <= ' + SF.OPACITY_STEP + ')');
    },

    _syncTests: function(g, SF, AC, CR) {
        var t = this._clock(g);
        g.TreeStyle = { tokens: {} };
        AC._last = {}; AC._carry = {};       // a new clock: AnimClock starts over
        var asked = 0;
        var r = Object.create(CR);
        r._starfieldEnabled = true; r._starfieldFixed = false; r._starsStill = false;
        r._width = 800; r._height = 600;
        r._requestAnimationOnlyFrame = function() { asked++; };
        SF.still = false; SF._advancedAt = -1e9; SF.moves = 0;
        r._syncStarfield(t.now);
        this.check(SF.moves === 1 && asked === 1, 'a move due: the stars move, the next frame is asked for');
        t.now += CR.ANIMATION_FRAME_MS;
        r._syncStarfield(t.now);
        this.check(SF.moves === 1 && asked === 2, 'one animation frame later: no move, but frames still asked for');
        t.now += CR.ANIMATION_FRAME_MS;
        r._syncStarfield(t.now);
        this.check(SF.moves === 2, 'the second animation frame: the next move');
        r._starsStill = true; asked = 0;
        t.now += 1000;
        r._syncStarfield(t.now);
        this.check(SF.moves === 2 && asked === 0 && SF.still === true, 'held still: no move, no frames asked for');
        r._starsStill = false;
        g.TreeStyle = { tokens: { pageColor: '#e6d6b0' } };
        r._syncStarfield(t.now + 1000);
        this.check(SF.moves === 2 && asked === 0, 'a design page: the stars are not touched');
    },

    _shownKeyTests: function(g, SF, SB, CR) {
        var t = this._clock(g);
        g.TreeStyle = { tokens: {}, renderChapters: function() {} };
        var calls = [];
        var ctx = {};
        ['setTransform', 'save', 'restore', 'scale', 'translate', 'rotate'].forEach(function(n) { ctx[n] = function() {}; });
        ctx.drawImage = function(img) { calls.push('paste'); };
        var r = Object.create(CR);
        r.canvas = { width: 800, height: 600 }; r.ctx = ctx; r._width = 800; r._height = 600;
        r.zoom = 1; r.rotation = 0; r.panX = 0; r.panY = 0;
        r._layerZoom = 1; r._layerRotation = 0; r._layerPanX = 0; r._layerPanY = 0;
        r._treeLayer = { id: 'layer', width: 1056, height: 856 }; r._treeLayerStale = false;
        r._ensureTreeLayer = function() { return this._treeLayer; };
        r._drawBackground = function() { calls.push('bg'); };
        r._drawMoving = function() {};
        r.isAnimating = false; r._wheelAt = -1e9; r._glideTarget = null; r._fxThisFrame = true;
        r._treeDirty = false; r._learningPath = null; r._treeLayerDraws = 7; r._mainShownKey = null;
        r._starfieldEnabled = true; r._starfieldFixed = false; r._starsStill = false; r._bgColor = '#000';
        var saved = { scroll: g.LayerScroll, build: g.LayerBuild, hover: g.HoverOverlay };
        g.LayerScroll = undefined; g.LayerBuild = undefined; g.HoverOverlay = undefined;
        var view = { cx: 400, cy: 300, rotRad: 0, cos: 1, sin: 0, viewLeft: -900, viewRight: 900, viewTop: -800, viewBottom: 800 };
        var frame = function() { calls = []; r._drawTree(ctx, 1, view, true); return calls.join(' '); };
        try {
            SB.reset(); SF.moves = 10;
            this.check(frame() === 'bg paste', "a key's first frame: drawn straight");
            this.check(frame() === '', 'the next frame with that key: the tree canvas is left alone');
            this.check(frame() === '', 'and the one after');
            SF.moves++;
            this.check(frame() === 'bg paste', 'a star move: drawn once');
            this.check(frame() === '', 'then left alone again (no second repaint a frame later)');
            r._learningPath = {};
            this.check(frame() !== '' && frame() !== '', 'the learning path drawn over it: the canvas is drawn each frame');
            r._learningPath = null;
            var built = 0, bump = function() { built++; };
            SB.draw(ctx, r, 'k', bump, bump); SB.draw(ctx, r, 'k', bump, bump);
            this.check(built === 0, 'moving stars: no picture is kept for them (it would last a frame or two)');
            frame();
            r._fxThisFrame = false;
            this.check(frame() !== '', 'no FxLayer (moving parts on the tree canvas): drawn each frame');
        } finally {
            g.LayerScroll = saved.scroll; g.LayerBuild = saved.build; g.HoverOverlay = saved.hover;
        }
    }
};

if (typeof window !== 'undefined') window.StarCadenceTest = StarCadenceTest;
if (typeof module !== 'undefined') module.exports = StarCadenceTest;
