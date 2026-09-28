/**
 * TreeStyleBook - the open-spellbook effects of TreeStyle: the page behind the
 * tree, chapter titles and ornamented dividers. (The ink reveal on opening was
 * removed: it cost full-rate frames on every open in the game's CPU-drawn view.)
 *
 * Split out of treeStyle.js to keep both under the file size limit; the methods
 * are added to TreeStyle, so callers still write TreeStyle.renderPage() and so on.
 * Every effect here is off unless a design preset turns it on (see the Book and
 * Page tokens in TreeStyle.DEFAULTS).
 *
 * Depends on: TreeStyle, state (globe position), settings, t() (optional),
 * CanvasRenderer (optional: its size and frames, for painting the page)
 */

(function() {
    var Book = {
        // =========================================================================
        // PAGE - drawn once into a texture, pasted every frame in place of the stars
        // =========================================================================
        //
        // The texture costs 25-30 ms to paint on a CPU canvas (the grain's 40 soft
        // blotches, the light in the middle and the dark edges, each a gradient over
        // much of the screen) - one long frame on a design switch, and again when the
        // corner drawing arrived. It is painted over several frames instead
        // (stepPage): the same calls in the same order, the blotches one by one and
        // the two full-page gradients in bands, so the finished texture is the same
        // to the pixel. Meanwhile the page shows the texture it had if it is of the
        // same colour (a resize, the corner drawing arriving), else the plain colour.
        // While the tree is not drawn (the design picked on the settings tab, the
        // panel hidden) a timer paints it in small slices, so it is usually ready
        // when the tree is shown again.

        PAGE_TARGET_FRAME_MS: 8,  // a frame's steps fill it up to this from its start, after the tree (one step at least)
        PAGE_BAND_PX: 128,        // the full-page gradients are painted in bands this tall (css px)
        PAGE_IDLE_STEP_MS: 4,     // the timer's slice while the tree is not drawn
        PAGE_IDLE_TICK_MS: 16,    // ...one a tick
        PAGE_IDLE_AFTER_MS: 100,  // ...counted as not drawn when no frame came for this long
        PAGE_FRAME_DEADLINE_MS: 14, // a frame's step is skipped when it would end past this (the frame's
                                    // later work - hub, paste - still has to fit under 16.7)
        PAGE_MAX_SKIPS: 8,        // frames in a row with no time left for a step: then one step anyway
        PAGE_BLOTCHES: 40,
        PAGE_SEED: 1234567,       // the grain is seeded, so the page looks the same every time it is painted

        _page: null,              // the finished texture
        _pageKey: '',             // its size, "w x h"
        _pageLook: '',            // the tokens it was painted with (_pageLookKey)
        _pageColor: '',           // its colour (what may stand in while another is painted)
        _pageJob: null,           // the texture being painted: { key, look, canvas, g, w, h, steps, next, rnd }
        _pageTimer: 0,

        /** What the texture depends on besides its size: the page tokens and whether the corner drawing is in. */
        _pageLookKey: function() {
            var t = this.tokens;
            return [t.pageColor, t.pageGrain, t.pageGrainColor, t.pageGlow, t.pageGlowAlpha, t.pageGlowRadius,
                    t.pageEdge, t.pageEdgeAlpha, t.pageOrnament, this._image(t.pageOrnament) ? 1 : 0].join('|');
        },

        /** Is the finished texture the one this size and these tokens want? */
        _pageReady: function(w, h) {
            return !!this._page && this._pageKey === w + 'x' + h && this._pageLook === this._pageLookKey();
        },

        /**
         * Paint the page behind the tree. Returns false when the preset has no page,
         * so the caller draws the starfield as before. One drawImage a frame, and no
         * frames of its own: a still page needs none, where the starfield asks for 20.
         * The texture is painted by stepPage; until it is done the old one of the
         * same colour stands in (stretched to the size), or the plain colour.
         */
        renderPage: function(ctx, w, h) {
            var t = this.tokens;
            if (!t.pageColor) return false;
            // (the same colour only: an add-on design sharing another's colour shows
            // that one's texture for the few frames its own is painted)
            if (this._page && this._pageColor === t.pageColor) {
                ctx.drawImage(this._page, 0, 0, w, h);
            } else {
                ctx.fillStyle = t.pageColor;
                ctx.fillRect(0, 0, w, h);
            }
            return true;
        },

        /**
         * Paint the texture on for w x h: steps until `budget` ms past `since` (one
         * at least). Returns 'none' (no page, or it is ready), 'pending' or 'done'
         * (swapped in just now). Called by a frame after its tree (CanvasRenderer.render)
         * and by the idle timer; either way the next frame is asked for.
         * inFrame: a frame's call - no step at all when the next one, at the last
         * step's cost, would end past PAGE_FRAME_DEADLINE_MS (a step is 1-1.5 ms at
         * 1640x1160 and grows with the screen: after an urgent build's 11 ms it could
         * push the frame past 16.7), except after PAGE_MAX_SKIPS such frames in a
         * row, so the page still comes. (Not the 8 ms fill target: frames that take
         * 8-12 ms anyway, a big screen, would then paint a step one frame in nine.)
         */
        stepPage: function(w, h, since, budget, inFrame) {
            if (!this.tokens.pageColor || !(w > 0 && h > 0)) { this._pageJob = null; return 'none'; }
            if (this._pageReady(w, h)) { this._pageJob = null; return 'none'; }
            var key = w + 'x' + h, look = this._pageLookKey();
            var job = this._pageJob;
            if (!job || job.key !== key || job.look !== look) {
                job = this._pageJob = this._startPage(w, h, key, look);
                if (!job) return 'none';            // no canvas: renderPage keeps the plain colour
            }
            var now = function() { return (typeof performance !== 'undefined') ? performance.now() : Date.now(); };
            var skip = inFrame && now() - since + (job.stepMs || 0) > this.PAGE_FRAME_DEADLINE_MS &&
                (job.skips || 0) < this.PAGE_MAX_SKIPS;
            job.skips = skip ? (job.skips || 0) + 1 : 0;
            try {
                if (!skip) {
                    do {
                        var s0 = now();
                        this._pageStep(job, job.steps[job.next++]);
                        job.stepMs = now() - s0;
                    } while (job.next < job.steps.length && now() - since < budget);
                }
            } catch (e) {
                this._pageJob = null;
                this._pageFailed = true;
                return 'none';
            }
            // The next frame, to go on or to show the texture, without marking the
            // tree changed (the tree layer does not show the page)
            if (typeof CanvasRenderer !== 'undefined') {
                CanvasRenderer.__needsRender = true;
                CanvasRenderer._animationOnlyRender = false;
            }
            if (job.next < job.steps.length) {
                // ...and the idle timer, should the frames stop (the panel hidden right after
                // the tree was loaded and drawn once, another tab): it leaves it to them while they come
                this._pageIdleLater();
                return 'pending';
            }
            this._page = job.canvas;
            this._pageKey = key;
            this._pageLook = look;
            this._pageColor = this.tokens.pageColor;
            this._pageBuilds = (this._pageBuilds || 0) + 1;   // StaticBase notices a new page
            this._pageJob = null;
            return 'done';
        },

        /** A texture to paint: its canvas and its steps, in the order the page is drawn. Null if no canvas can be made. */
        _startPage: function(w, h, key, look) {
            if (this._pageFailed) return null;
            var t = this.tokens, c, g;
            try {
                c = document.createElement('canvas');
                c.width = Math.max(1, Math.round(w));
                c.height = Math.max(1, Math.round(h));
                g = c.getContext('2d');
                if (!g) throw new Error('no 2d context');
            } catch (e) {
                this._pageFailed = true;
                return null;
            }
            var steps = [['fill']], i, y;
            if (t.pageGrain > 0) {
                for (i = 0; i < this.PAGE_BLOTCHES; i++) steps.push(['blotch']);
                steps.push(['fibres']);
            }
            // Bands with whole-pixel edges inside the page and its own edges outside:
            // a gradient is a function of the pixel, so the bands add up to the one rect
            var band = this.PAGE_BAND_PX;
            if (t.pageGlow) for (y = 0; y < h; y += band) steps.push(['glow', y, Math.min(y + band, h)]);
            steps.push(['ornaments']);
            if (t.pageEdgeAlpha > 0) for (y = 0; y < h; y += band) steps.push(['edge', y, Math.min(y + band, h)]);
            var seed = this.PAGE_SEED;
            return {
                key: key, look: look, canvas: c, g: g, w: w, h: h, steps: steps, next: 0,
                rnd: function() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
            };
        },

        /** One step of the texture: the page's drawing calls, as they were made in one go. */
        _pageStep: function(job, step) {
            var t = this.tokens, g = job.g, w = job.w, h = job.h, big = Math.max(w, h), rnd = job.rnd;
            switch (step[0]) {
                case 'fill':
                    g.fillStyle = t.pageColor;
                    g.fillRect(0, 0, w, h);
                    break;
                case 'blotch':
                    // Uneven ink absorption (the seeded numbers are taken in the same order)
                    var bx = rnd() * w, by = rnd() * h, br = big * (0.04 + rnd() * 0.12);
                    var bg = g.createRadialGradient(bx, by, 0, bx, by, br);
                    bg.addColorStop(0, this._rgba(t.pageGrainColor, 0.07 * t.pageGrain));
                    bg.addColorStop(1, this._rgba(t.pageGrainColor, 0));
                    g.fillStyle = bg;
                    g.fillRect(bx - br, by - br, br * 2, br * 2);
                    break;
                case 'fibres':
                    // One path
                    g.strokeStyle = this._rgba(t.pageGrainColor, 0.1 * t.pageGrain);
                    g.lineWidth = 0.6;
                    g.beginPath();
                    var fibres = Math.round(w * h / 600);
                    for (var i = 0; i < fibres; i++) {
                        var fx = rnd() * w, fy = rnd() * h, fa = rnd() * Math.PI, fl = 2 + rnd() * 7;
                        g.moveTo(fx, fy);
                        g.lineTo(fx + Math.cos(fa) * fl, fy + Math.sin(fa) * fl);
                    }
                    g.stroke();
                    break;
                case 'glow':
                    if (!job.glow) {
                        job.glow = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, big * t.pageGlowRadius);
                        job.glow.addColorStop(0, this._rgba(t.pageGlow, t.pageGlowAlpha));
                        job.glow.addColorStop(0.55, this._rgba(t.pageGlow, t.pageGlowAlpha * 0.35));
                        job.glow.addColorStop(1, this._rgba(t.pageGlow, 0));
                    }
                    g.fillStyle = job.glow;
                    g.fillRect(0, step[1], w, step[2] - step[1]);
                    break;
                case 'ornaments':
                    this._drawOrnaments(g, w, h);
                    break;
                case 'edge':
                    if (!job.edge) {
                        job.edge = g.createRadialGradient(w / 2, h / 2, big * 0.3, w / 2, h / 2, big * 0.75);
                        job.edge.addColorStop(0, this._rgba(t.pageEdge, 0));
                        job.edge.addColorStop(1, this._rgba(t.pageEdge, t.pageEdgeAlpha));
                    }
                    g.fillStyle = job.edge;
                    g.fillRect(0, step[1], w, step[2] - step[1]);
                    break;
            }
        },

        /**
         * While the tree is not drawn (settings tab, hidden panel) the texture is
         * painted by a timer, a slice a tick; while frames come, they paint it.
         */
        _pageIdleLater: function() {
            if (this._pageTimer || typeof setTimeout !== 'function') return;
            // Not while the panel is hidden: the timer would paint behind the game
            // (cppCallbacks.js onPanelShowing starts it again)
            if (typeof window !== 'undefined' && window._panelVisible === false) return;
            var self = this;
            this._pageTimer = setTimeout(function() { self._pageTimer = 0; self._pageIdleTick(); }, this.PAGE_IDLE_TICK_MS);
        },

        _pageIdleTick: function() {
            if (typeof window !== 'undefined' && window._panelVisible === false) return;   // hidden: stops
            var r = typeof CanvasRenderer !== 'undefined' ? CanvasRenderer : null;
            if (!r || !this.tokens.pageColor || this._pageFailed) return;
            var w = r._width, h = r._height;
            if (!(w > 0 && h > 0) || this._pageReady(w, h)) return;
            var now = (typeof performance !== 'undefined') ? performance.now() : Date.now();
            if (r._frameStartAt && now - r._frameStartAt < this.PAGE_IDLE_AFTER_MS) {
                this._pageIdleLater();              // frames are coming: they paint it
                return;
            }
            if (this.stepPage(w, h, now, this.PAGE_IDLE_STEP_MS) === 'pending') this._pageIdleLater();
        },

        // =========================================================================
        // ILLUSTRATIONS - a design's drawings (themes/<design>/*.png)
        // =========================================================================
        //
        // Loaded once; until an image arrives nothing is drawn in its place, and when it
        // does the page is painted again over the next frames (stepPage) and a frame is
        // asked for (the heart's emblem shows on it). Both drawings are paid
        // for once: the corners go into the page texture, the heart emblem into a sprite
        // already at its on-screen size, so a frame costs one small blit at most.

        _images: {},

        /** The loaded image for a design token's path, or null (and start loading it). */
        _image: function(src) {
            if (!src) return null;
            var entry = this._images[src];
            if (!entry) {
                var self = this, img = new Image();
                entry = this._images[src] = { img: img, ok: false };
                img.onload = function() {
                    entry.ok = true;
                    // The page is painted again with the drawing over the next frames (its
                    // look changed; the old texture stays up meanwhile), the heart's emblem
                    // is drawn with the hub. A frame is asked for without marking the tree
                    // changed (the tree layer shows neither): drawing one here, at once, was
                    // a whole repaint of the page in the middle of a glide - a long frame -
                    // and marking the tree restarted the glide's build
                    self._emblemSprite = null;
                    if (typeof CanvasRenderer !== 'undefined') {
                        CanvasRenderer.__needsRender = true;
                        CanvasRenderer._animationOnlyRender = false;
                    }
                    self._pageIdleLater();
                };
                img.src = src;
            }
            return entry.ok ? entry.img : null;
        },

        /** The page's corner drawing, mirrored into all four corners of the page texture. */
        _drawOrnaments: function(g, w, h) {
            var img = this._image(this.tokens.pageOrnament);
            if (!img) return;
            var size = Math.round(Math.max(96, Math.min(200, Math.min(w, h) * 0.24)));
            var corners = [[1, 1, 0, 0], [-1, 1, w, 0], [1, -1, 0, h], [-1, -1, w, h]];
            for (var i = 0; i < corners.length; i++) {
                var c = corners[i];
                g.save();
                g.translate(c[2], c[3]);
                g.scale(c[0], c[1]);
                g.drawImage(img, 0, 0, size, size);
                g.restore();
            }
        },

        _emblemSprite: null,
        _emblemKey: '',

        /**
         * The heart's emblem, in place of its text. Returns false when the design has
         * none (or it is still loading), so the caller draws the text as before.
         * @param {number} radius - the heart's radius in tree units
         * @param {number} zoom - the view's zoom, to size the sprite in screen pixels
         */
        renderHubEmblem: function(ctx, radius, zoom) {
            var img = this._image(this.tokens.hubEmblem);
            if (!img) return false;
            var d = radius * 1.75;                            // the emblem's size in tree units: just inside the inner ring
            // the sprite is drawn at the on-screen size in device pixels, rounded so zooming
            // does not rebuild it every frame
            var dpr = (typeof window !== 'undefined' && window.devicePixelRatio) || 1;
            var px = Math.max(16, Math.min(img.width || 512, Math.round(d * (zoom || 1) * dpr / 8) * 8));
            var key = this.tokens.hubEmblem + '|' + px;
            if (!this._emblemSprite || this._emblemKey !== key) {
                try {
                    // halve step by step down to the size: one big bilinear shrink turns the
                    // stipple's dots to mush, halving keeps them as a clean tone
                    var src = img, sw = img.width, sh = img.height;
                    while (sw / 2 >= px) {
                        var half = document.createElement('canvas');
                        half.width = Math.round(sw / 2); half.height = Math.round(sh / 2);
                        half.getContext('2d').drawImage(src, 0, 0, half.width, half.height);
                        src = half; sw = half.width; sh = half.height;
                    }
                    var c = document.createElement('canvas');
                    c.width = px; c.height = px;
                    c.getContext('2d').drawImage(src, 0, 0, px, px);
                    this._emblemSprite = c;
                    this._emblemKey = key;
                } catch (e) {
                    return false;
                }
            }
            ctx.drawImage(this._emblemSprite, -d / 2, -d / 2, d, d);
            return true;
        },

        // =========================================================================
        // CHAPTERS AND DIVIDERS
        // =========================================================================

        _chapterRadii: null,
        _chapterNodes: null,

        /** How far out each school reaches from the heart, worked out once per tree. */
        _schoolReach: function(r, gx, gy) {
            if (this._chapterNodes === r.nodes && this._chapterRadii) return this._chapterRadii;
            var reach = {};
            for (var i = 0; i < r.nodes.length; i++) {
                var n = r.nodes[i];
                var dx = n.x - gx, dy = n.y - gy;
                var d = Math.sqrt(dx * dx + dy * dy);
                if (!reach[n.school] || d > reach[n.school]) reach[n.school] = d;
            }
            this._chapterNodes = r.nodes;
            this._chapterRadii = reach;
            return reach;
        },

        /**
         * School names past the outer edge of each school, upright and the same size
         * at every zoom, so a zoomed-out view still says which part is which.
         * Screen space; called after the tree transform is undone, like the labels.
         * margin: css px drawn past the canvas edge (the tree layer's margin), so a
         * title that a drag later brings into view is already on the layer.
         */
        renderChapters: function(ctx, r, cx, cy, cos, sin, margin) {
            margin = margin || 0;
            var t = this.tokens;
            if (!t.chapterTitles) return;
            var names = Object.keys(r.schools || {});
            if (names.length < 2) return;
            var gd = (typeof state !== 'undefined' && state.treeData && state.treeData.globe) || { x: 0, y: 0 };
            var reach = this._schoolReach(r, gd.x, gd.y);
            var gap = 40 / (r.zoom || 1);

            ctx.save();
            ctx.font = t.chapterSize + 'px ' + this.labelFamily();
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            if (t.labelHalo) {
                ctx.strokeStyle = t.labelHalo;
                ctx.lineWidth = t.labelHaloWidth + 1;
                ctx.lineJoin = 'round';
            }
            ctx.fillStyle = t.accent;
            ctx.globalAlpha = 0.9;
            for (var i = 0; i < names.length; i++) {
                var name = names[i];
                var school = r.schools[name];
                if (!school || school.spokeAngle === undefined || !reach[name]) continue;
                if (typeof settings !== 'undefined' && settings.schoolVisibility && settings.schoolVisibility[name] === false) continue;
                var a = school.spokeAngle * Math.PI / 180;
                var wx = gd.x + Math.cos(a) * (reach[name] + gap);
                var wy = gd.y + Math.sin(a) * (reach[name] + gap);
                var sx = (wx * cos - wy * sin) * r.zoom + r.panX + cx;
                var sy = (wx * sin + wy * cos) * r.zoom + r.panY + cy;
                if (sx < -200 - margin || sx > r._width + 200 + margin ||
                    sy < -50 - margin || sy > r._height + 50 + margin) continue;
                var text = '—  ' + this._schoolName(name) + '  —';
                if (t.labelHalo) ctx.strokeText(text, sx, sy);
                ctx.fillText(text, sx, sy);
            }
            ctx.restore();
        },

        _schoolName: function(name) {
            if (typeof t !== 'function') return name;
            var key = 'chips.school.' + String(name).toLowerCase();
            var s = t(key);
            return s === key ? name : s;
        },

        /**
         * Dividers as thin double rules ending in a small diamond, in the accent colour.
         * Returns false when the preset keeps the plain dividers. World space.
         */
        renderOrnamentDividers: function(ctx, r, length, gd) {
            var t = this.tokens;
            if (!t.dividerOrnament) return false;
            var names = Object.keys(r.schools);
            var off = 2.2, mark = 7;
            ctx.save();
            ctx.strokeStyle = t.accent;
            ctx.fillStyle = t.accent;
            ctx.lineWidth = 1;
            ctx.globalAlpha = 0.35;
            ctx.beginPath();
            var ends = [];
            for (var i = 0; i < names.length; i++) {
                var s = r.schools[names[i]];
                var ang = (s.startAngle !== undefined ? s.startAngle : (i * (360 / names.length) - 90)) * Math.PI / 180;
                var ux = Math.cos(ang), uy = Math.sin(ang), px = -uy * off, py = ux * off;
                var sx = gd.x + ux * 60, sy = gd.y + uy * 60;
                var ex = gd.x + ux * length, ey = gd.y + uy * length;
                ctx.moveTo(sx + px, sy + py); ctx.lineTo(ex + px, ey + py);
                ctx.moveTo(sx - px, sy - py); ctx.lineTo(ex - px, ey - py);
                ends.push(ex + ux * mark, ey + uy * mark, ux, uy);
            }
            ctx.stroke();
            ctx.globalAlpha = 0.6;
            ctx.beginPath();
            for (var k = 0; k < ends.length; k += 4) {
                var mx = ends[k], my = ends[k + 1], vx = ends[k + 2], vy = ends[k + 3];
                ctx.moveTo(mx + vx * mark, my + vy * mark);
                ctx.lineTo(mx - vy * mark * 0.5, my + vx * mark * 0.5);
                ctx.lineTo(mx - vx * mark, my - vy * mark);
                ctx.lineTo(mx + vy * mark * 0.5, my - vx * mark * 0.5);
                ctx.closePath();
            }
            ctx.fill();
            ctx.restore();
            return true;
        }
    };

    for (var k in Book) {
        if (Book.hasOwnProperty(k)) TreeStyle[k] = Book[k];
    }
})();
