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
 * Depends on: TreeStyle, state (globe position), settings, t() (optional)
 */

(function() {
    var Book = {
        // =========================================================================
        // PAGE - drawn once into a texture, pasted every frame in place of the stars
        // =========================================================================

        _page: null,
        _pageKey: '',

        /**
         * Paint the page behind the tree. Returns false when the preset has no page,
         * so the caller draws the starfield as before. One drawImage a frame, and no
         * frames of its own: a still page needs none, where the starfield asks for 20.
         */
        renderPage: function(ctx, w, h) {
            var t = this.tokens;
            if (!t.pageColor) return false;
            var key = w + 'x' + h;
            if (!this._page || this._pageKey !== key) {
                this._page = this._buildPage(w, h);
                this._pageKey = key;
                this._pageBuilds = (this._pageBuilds || 0) + 1;   // StaticBase notices a new page
            }
            if (!this._page) {
                ctx.fillStyle = t.pageColor;
                ctx.fillRect(0, 0, w, h);
                return true;
            }
            ctx.drawImage(this._page, 0, 0, w, h);
            return true;
        },

        _buildPage: function(w, h) {
            var t = this.tokens;
            try {
                var c = document.createElement('canvas');
                c.width = Math.max(1, Math.round(w));
                c.height = Math.max(1, Math.round(h));
                var g = c.getContext('2d');
                var big = Math.max(w, h);
                g.fillStyle = t.pageColor;
                g.fillRect(0, 0, w, h);

                if (t.pageGrain > 0) {
                    // Seeded, so the page looks the same every time it is rebuilt
                    var seed = 1234567;
                    var rnd = function() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
                    var i;
                    // Blotches: uneven ink absorption
                    for (i = 0; i < 40; i++) {
                        var bx = rnd() * w, by = rnd() * h, br = big * (0.04 + rnd() * 0.12);
                        var bg = g.createRadialGradient(bx, by, 0, bx, by, br);
                        bg.addColorStop(0, this._rgba(t.pageGrainColor, 0.07 * t.pageGrain));
                        bg.addColorStop(1, this._rgba(t.pageGrainColor, 0));
                        g.fillStyle = bg;
                        g.fillRect(bx - br, by - br, br * 2, br * 2);
                    }
                    // Fibres, one path
                    g.strokeStyle = this._rgba(t.pageGrainColor, 0.1 * t.pageGrain);
                    g.lineWidth = 0.6;
                    g.beginPath();
                    var fibres = Math.round(w * h / 600);
                    for (i = 0; i < fibres; i++) {
                        var fx = rnd() * w, fy = rnd() * h, fa = rnd() * Math.PI, fl = 2 + rnd() * 7;
                        g.moveTo(fx, fy);
                        g.lineTo(fx + Math.cos(fa) * fl, fy + Math.sin(fa) * fl);
                    }
                    g.stroke();
                }

                if (t.pageGlow) {
                    var cg = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, big * t.pageGlowRadius);
                    cg.addColorStop(0, this._rgba(t.pageGlow, t.pageGlowAlpha));
                    cg.addColorStop(0.55, this._rgba(t.pageGlow, t.pageGlowAlpha * 0.35));
                    cg.addColorStop(1, this._rgba(t.pageGlow, 0));
                    g.fillStyle = cg;
                    g.fillRect(0, 0, w, h);
                }

                if (t.pageEdgeAlpha > 0) {
                    var vg = g.createRadialGradient(w / 2, h / 2, big * 0.3, w / 2, h / 2, big * 0.75);
                    vg.addColorStop(0, this._rgba(t.pageEdge, 0));
                    vg.addColorStop(1, this._rgba(t.pageEdge, t.pageEdgeAlpha));
                    g.fillStyle = vg;
                    g.fillRect(0, 0, w, h);
                }
                return c;
            } catch (e) {
                return null;
            }
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
         */
        renderChapters: function(ctx, r, cx, cy, cos, sin) {
            var t = this.tokens;
            if (!t.chapterTitles) return;
            var names = Object.keys(r.schools || {});
            if (names.length < 2) return;
            var gd = (typeof state !== 'undefined' && state.treeData && state.treeData.globe) || { x: 0, y: 0 };
            var reach = this._schoolReach(r, gd.x, gd.y);
            var gap = 40 / (r.zoom || 1);

            ctx.save();
            ctx.font = t.chapterSize + 'px ' + t.labelFont;
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
                if (sx < -200 || sx > r._width + 200 || sy < -50 || sy > r._height + 50) continue;
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
