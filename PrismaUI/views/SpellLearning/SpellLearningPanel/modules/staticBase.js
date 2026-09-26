/**
 * StaticBase - the background and the tree, kept as one picture while neither moves.
 *
 * An animation frame (heart beat, globe, sigil) used to repaint the whole
 * canvas three times before drawing what moved: the background colour, the
 * page (or the stars) and the pasted tree layer. The game's view draws on the
 * CPU, so each of those full-screen passes costs. When the background is
 * still - a design page, no stars, or stars held still - the three are drawn
 * once into this canvas, and later frames paste it with one pass.
 *
 * The picture is rebuilt when its key changes (the tree layer was redrawn,
 * the hover preview changed, the view moved, the background changed). A key
 * is only built after it has been the same for two frames in a row, so a
 * drag or glide, whose key changes every frame, is drawn straight as before
 * and never pays for a picture it throws away.
 *
 * Depends on: TreeStyle (tokens, _page), Starfield (optional)
 */

var StaticBase = {

    _canvas: null,
    _ctx: null,
    _key: '',              // what the picture shows
    _seen: '',             // last frame's key (a key is built on its second frame)
    _failed: false,

    /** Is the background still this frame: a design page, no stars, or stars held still? */
    backgroundStill: function(r) {
        if (TreeStyle.tokens.pageColor) return true;
        if (!r._starfieldEnabled || typeof Starfield === 'undefined') return true;
        return r._starsStill === true;
    },

    /** What the background looks like, as a string (see backgroundStill). */
    _backgroundKey: function(r) {
        var t = TreeStyle.tokens;
        if (t.pageColor) return 'page|' + t.pageColor + '|' + (TreeStyle._pageKey || '') + '|' + (TreeStyle._pageBuilds || 0);
        var k = 'bg|' + (r._bgColor || '');
        if (r._starfieldEnabled && typeof Starfield !== 'undefined') {
            k += '|stars|' + r._starfieldFixed + '|' + Starfield.seed + '|' + Starfield.starCount + '|' +
                 Starfield.maxSize + '|' + (Starfield._hex || '') + '|' + Starfield._twinklePhase;
            // World-space stars follow the camera
            if (!r._starfieldFixed) k += '|' + r.panX + '|' + r.panY + '|' + r.zoom;
        }
        return k;
    },

    /**
     * Paste the background and the tree layer. Returns false when the picture
     * cannot be used this frame (the caller then draws both itself).
     * @param {CanvasRenderingContext2D} ctx - the screen
     * @param {Object} r - CanvasRenderer
     * @param {string} layerKey - the tree layer as pasted: source, draws, offset
     * @param {function(CanvasRenderingContext2D)} drawBackground - fills a context with the background
     * @param {function(CanvasRenderingContext2D)} paste - pastes the tree layer into a context
     */
    draw: function(ctx, r, layerKey, drawBackground, paste) {
        if (this._failed) return false;
        var w = r.canvas.width, h = r.canvas.height;
        var key = this.keyFor(r, layerKey);
        if (key !== this._key) {
            var seenBefore = key === this._seen;
            this._seen = key;
            if (!seenBefore) return false;
            if (!this._build(w, h, drawBackground, paste)) return false;
            this._key = key;
        }
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.globalAlpha = 1.0;
        ctx.globalCompositeOperation = 'source-over';
        ctx.drawImage(this._canvas, 0, 0);
        ctx.restore();
        return true;
    },

    /** The key of the picture for this layer and background (what the tree canvas shows once drawn). */
    keyFor: function(r, layerKey) {
        return r.canvas.width + 'x' + r.canvas.height + '|' + layerKey + '|' + this._backgroundKey(r);
    },

    _build: function(w, h, drawBackground, paste) {
        try {
            if (!this._canvas) {
                this._canvas = document.createElement('canvas');
                this._ctx = this._canvas.getContext('2d');
                if (!this._ctx) throw new Error('no 2d context');
            }
            if (this._canvas.width !== w || this._canvas.height !== h) {
                this._canvas.width = w;
                this._canvas.height = h;
            }
            drawBackground(this._ctx);
            paste(this._ctx);
            return true;
        } catch (e) {
            this._failed = true;
            this._canvas = this._ctx = null;
            return false;
        }
    },

    /** Forget the picture (the panel was hidden, the canvas resized). */
    reset: function() {
        this._key = '';
        this._seen = '';
    }
};

window.StaticBase = StaticBase;
