/**
 * Starfield Module - Parallax twinkling star background
 *
 * Supports two modes:
 * - Fixed: stars are screen-space (drift + wrap around edges)
 * - World-space: seed-based stars that stay in place as user pans,
 *   with multiple parallax layers for depth effect
 *
 * Parallax layers:
 *   Layer 0 (far):  tiny dim stars,  move slowly  (depth 0.15)
 *   Layer 1 (mid):  medium stars,    move moderate (depth 0.40)
 *   Layer 2 (near): larger stars,    move fast     (depth 0.75)
 *
 * Drawing: stars are gathered into one path per opacity step (OPACITY_STEP)
 * and each path is filled once, instead of one fill per star - the game's
 * view draws on the CPU and pays per fill. The world-space tiles are
 * generated once and kept (_tileCache) instead of re-rolled every frame.
 */

var Starfield = {
    // Star data (fixed mode only)
    stars: null,

    // Configuration
    enabled: true,
    starCount: 200,
    maxSize: 2.5,
    minSize: 0.5,
    twinkleSpeed: 0.02,
    driftSpeed: 0.05,
    color: { r: 255, g: 255, b: 255 },
    seed: 42,

    // Canvas dimensions (set by init)
    width: 0,
    height: 0,

    // Twinkle phase accumulator
    _twinklePhase: 0,

    OPACITY_STEP: 0.04,        // stars are drawn in opacity steps this big, one path each
    TILE_SIZE: 500,            // world-space tile edge, screen pixels
    TILE_CACHE_MAX: 256,       // tiles kept before the cache starts over
    _tileCache: {},
    _tileCacheSize: 0,
    _buckets: null,            // opacity step -> Path2D, while a frame is drawn
    still: false,          // set by the renderer: draw without moving (render settings)

    // Parallax layer definitions
    // depth: 0 = fixed to screen, 1 = fixed to world
    _layers: [
        { depth: 0.12, sizeMin: 0.2,  sizeMax: 0.5,  opacityMin: 0.10, opacityMax: 0.30, densityMul: 0.6,  seedOffset: 0 },
        { depth: 0.35, sizeMin: 0.3,  sizeMax: 0.9,  opacityMin: 0.20, opacityMax: 0.45, densityMul: 0.8,  seedOffset: 7919 },
        { depth: 0.70, sizeMin: 0.5,  sizeMax: 1.0,  opacityMin: 0.30, opacityMax: 0.60, densityMul: 1.0,  seedOffset: 16381 }
    ],

    /**
     * Seeded pseudo-random number generator (mulberry32)
     * Returns a function that produces deterministic floats [0, 1)
     */
    _seededRng: function(seed) {
        var s = seed | 0;
        return function() {
            s = (s + 0x6D2B79F5) | 0;
            var t = Math.imul(s ^ (s >>> 15), 1 | s);
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        };
    },

    /**
     * Initialize starfield with seeded random star positions (fixed mode)
     */
    init: function(width, height) {
        this.width = width || 800;
        this.height = height || 600;
        this.stars = [];

        var rng = this._seededRng(this.seed);

        for (var i = 0; i < this.starCount; i++) {
            this.stars.push({
                x: rng() * this.width,
                y: rng() * this.height,
                size: this.minSize + rng() * (this.maxSize - this.minSize),
                phase: rng() * Math.PI * 2,
                twinkleRate: 0.5 + rng() * 1.5,
                baseOpacity: 0.3 + rng() * 0.5,
                dx: (rng() - 0.5) * this.driftSpeed,
                dy: (rng() - 0.5) * this.driftSpeed
            });
        }

        console.log('[Starfield] Initialized with', this.starCount, 'stars, seed:', this.seed);
    },

    /**
     * Update canvas dimensions (call on resize)
     */
    resize: function(width, height) {
        var oldWidth = this.width;
        var oldHeight = this.height;
        this.width = width;
        this.height = height;

        if (this.stars && oldWidth > 0 && oldHeight > 0) {
            var scaleX = width / oldWidth;
            var scaleY = height / oldHeight;
            for (var i = 0; i < this.stars.length; i++) {
                this.stars[i].x *= scaleX;
                this.stars[i].y *= scaleY;
            }
        }
    },

    /** Steps due for `key` (AnimClock: the speed does not follow the frame rate). */
    _steps: function(key) {
        return typeof AnimClock !== 'undefined' ? AnimClock.steps('stars-' + key) : 1;
    },

    /**
     * Update star positions and twinkle (fixed mode only)
     */
    update: function() {
        if (!this.stars) return;

        for (var i = 0; i < this.stars.length; i++) {
            var star = this.stars[i];
            star.phase += this.twinkleSpeed * star.twinkleRate;
            star.x += star.dx;
            star.y += star.dy;
            if (star.x < 0) star.x = this.width;
            if (star.x > this.width) star.x = 0;
            if (star.y < 0) star.y = this.height;
            if (star.y > this.height) star.y = 0;
        }
    },

    /**
     * Render stars (fixed to screen mode)
     */
    render: function(ctx) {
        if (!this.enabled || !this.stars) return;

        // still: drawn where they are, no drift or twinkle (render settings)
        if (!this.still) {
            for (var n = this._steps('drift'); n > 0; n--) this.update();
        }

        this._bucketBegin();
        for (var i = 0; i < this.stars.length; i++) {
            var star = this.stars[i];
            var twinkle = 0.5 + 0.5 * Math.sin(star.phase);
            this._bucketAdd(star.x, star.y, star.size, star.baseOpacity * twinkle);
        }
        this._bucketFlush(ctx);
    },

    // =========================================================================
    // OPACITY BUCKETS
    // =========================================================================

    _bucketBegin: function() {
        this._buckets = [];
    },

    _bucketAdd: function(x, y, size, opacity) {
        var step = Math.round(opacity / this.OPACITY_STEP);
        if (step <= 0) return;
        var path = this._buckets[step] || (this._buckets[step] = new Path2D());
        path.moveTo(x + size, y);
        path.arc(x, y, size, 0, Math.PI * 2);
    },

    _bucketFlush: function(ctx) {
        var rgb = this.color;
        ctx.save();
        ctx.fillStyle = 'rgb(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ')';
        for (var step = 1; step < this._buckets.length; step++) {
            if (!this._buckets[step]) continue;
            var a = step * this.OPACITY_STEP;
            ctx.globalAlpha = a > 1 ? 1 : a;
            ctx.fill(this._buckets[step]);
        }
        ctx.restore();
        this._buckets = null;
    },

    /**
     * One tile's stars, generated from its seed once and kept:
     * [x, y, size, baseOpacity, twinkleRate, phaseOffset] per star, tile-space.
     */
    _tileStars: function(li, layer, tx, ty, layerStars, sizeScale) {
        // Seed, size and density are in the key: the renderer sets them directly
        var key = this.seed + ',' + sizeScale + ',' + layerStars + ',' + li + ',' + tx + ',' + ty;
        var cached = this._tileCache[key];
        if (cached) return cached;
        if (this._tileCacheSize >= this.TILE_CACHE_MAX) {
            this._tileCache = {};
            this._tileCacheSize = 0;
        }
        var tileSize = this.TILE_SIZE;
        // Unique deterministic seed per tile per layer
        var tileSeed = (this.seed + layer.seedOffset) * 73856093 + tx * 19349663 + ty * 83492791;
        var rng = this._seededRng(tileSeed);
        var stars = [];
        for (var si = 0; si < layerStars; si++) {
            // Always all six rng() calls per star, in this order: the tile's
            // stars must come out the same every time
            stars.push(tx * tileSize + rng() * tileSize,
                       ty * tileSize + rng() * tileSize,
                       (layer.sizeMin + rng() * (layer.sizeMax - layer.sizeMin)) * sizeScale,
                       layer.opacityMin + rng() * (layer.opacityMax - layer.opacityMin),
                       0.5 + rng() * 1.5,
                       rng() * Math.PI * 2);
        }
        this._tileCache[key] = stars;
        this._tileCacheSize++;
        return stars;
    },

    /** Forget the generated tiles (seed, density or size changed). */
    _clearTiles: function() {
        this._tileCache = {};
        this._tileCacheSize = 0;
    },

    /**
     * Render a single parallax layer as a SCREEN-SPACE tile grid.
     *
     * Stars are generated in screen-space tiles that scroll with parallax.
     * Zoom has NO effect on star positions, sizes, or density — stars are
     * infinitely far away. Only panning shifts them, and each layer shifts
     * at a different rate (depth) for the parallax depth illusion.
     *
     * @param {CanvasRenderingContext2D} ctx - Screen-space context (DPR-scaled only)
     * @param {number} camX - Camera world-space X position (-panX/zoom)
     * @param {number} camY - Camera world-space Y position (-panY/zoom)
     * @param {number} canvasW - Canvas logical width
     * @param {number} canvasH - Canvas logical height
     * @param {object} layer - Layer definition { depth, sizeMin, sizeMax, ... }
     * @param {number} starsPerTile - Base stars per tile
     */
    _renderLayer: function(ctx, camX, camY, canvasW, canvasH, layer, starsPerTile, li) {
        var tileSize = this.TILE_SIZE;
        var layerStars = Math.max(1, Math.round(starsPerTile * layer.densityMul));

        // Scroll offset for this layer (in screen-space tile units).
        // Camera world position × depth gives zoom-independent parallax.
        var scrollX = camX * layer.depth;
        var scrollY = camY * layer.depth;

        // The visible screen [0, canvasW] maps to tile-space [scrollX, scrollX + canvasW]
        var tileMinX = Math.floor(scrollX / tileSize);
        var tileMaxX = Math.floor((scrollX + canvasW) / tileSize);
        var tileMinY = Math.floor(scrollY / tileSize);
        var tileMaxY = Math.floor((scrollY + canvasH) / tileSize);

        // Cap to prevent explosion (shouldn't happen since tile count is canvasW/500 ≈ 4)
        var tileCount = (tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1);
        if (tileCount > 200) return;

        // Scale star sizes with user's maxSize setting
        var sizeScale = this.maxSize / 2.5;
        var phase = this._twinklePhase;

        for (var tx = tileMinX; tx <= tileMaxX; tx++) {
            for (var ty = tileMinY; ty <= tileMaxY; ty++) {
                var stars = this._tileStars(li, layer, tx, ty, layerStars, sizeScale);
                for (var i = 0; i < stars.length; i += 6) {
                    // Convert tile-space → screen by subtracting the scroll offset
                    var screenX = stars[i] - scrollX;
                    var screenY = stars[i + 1] - scrollY;
                    if (screenX < -5 || screenX > canvasW + 5 ||
                        screenY < -5 || screenY > canvasH + 5) continue;

                    // Twinkle animation
                    var twinkle = 0.5 + 0.5 * Math.sin(phase * stars[i + 4] + stars[i + 5]);
                    this._bucketAdd(screenX, screenY, stars[i + 2], stars[i + 3] * twinkle);
                }
            }
        }
    },

    /**
     * Render parallax starfield layers.
     *
     * Stars live in screen-space tiles, completely independent of zoom.
     * Each layer scrolls at a different rate based on the camera's world
     * position, creating a depth illusion. Zooming changes nothing — stars
     * are at infinity.
     *
     * @param {CanvasRenderingContext2D} ctx - Screen-space context (DPR-scaled only)
     * @param {number} panX - Current pan X offset (screen pixels)
     * @param {number} panY - Current pan Y offset (screen pixels)
     * @param {number} zoom - Current zoom level
     * @param {number} canvasW - Canvas logical width
     * @param {number} canvasH - Canvas logical height
     */
    renderWorldSpace: function(ctx, panX, panY, zoom, canvasW, canvasH) {
        if (!this.enabled) return;

        if (!this.still) this._twinklePhase += this.twinkleSpeed * this._steps('twinkle');

        // Derive camera world position from pan/zoom.
        // This is approximately zoom-independent: pure zooming barely
        // changes the world center, so stars stay put.
        var camX = -panX / zoom;
        var camY = -panY / zoom;

        // Base stars per tile (scale with density setting)
        var starsPerTile = Math.max(2, Math.round(this.starCount / 10));

        // Every parallax layer into the same opacity paths
        this._bucketBegin();
        for (var li = 0; li < this._layers.length; li++) {
            this._renderLayer(ctx, camX, camY, canvasW, canvasH, this._layers[li], starsPerTile, li);
        }
        this._bucketFlush(ctx);
    },

    /**
     * Set star color from hex
     */
    setColor: function(hex) {
        if (!hex || hex === this._hex) return;
        this._hex = hex;
        var result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
        if (result) {
            this.color = {
                r: parseInt(result[1], 16),
                g: parseInt(result[2], 16),
                b: parseInt(result[3], 16)
            };
        }
    },

    /**
     * Configure starfield
     */
    configure: function(options) {
        if (!options) return;

        var needsReinit = false;

        if (options.enabled !== undefined) this.enabled = options.enabled;
        if (options.starCount !== undefined && options.starCount !== this.starCount) {
            this.starCount = options.starCount;
            needsReinit = true;
        }
        if (options.seed !== undefined && options.seed !== this.seed) {
            this.seed = options.seed;
            needsReinit = true;
        }
        if (options.maxSize !== undefined && options.maxSize !== this.maxSize) {
            this.maxSize = options.maxSize;
            this._clearTiles();
        }
        if (options.minSize !== undefined) this.minSize = options.minSize;
        if (options.twinkleSpeed !== undefined) this.twinkleSpeed = options.twinkleSpeed;
        if (options.driftSpeed !== undefined) this.driftSpeed = options.driftSpeed;
        if (options.color) this.setColor(options.color);

        if (needsReinit) {
            this._clearTiles();
            this.init(this.width, this.height);
        }
    }
};

window.Starfield = Starfield;
