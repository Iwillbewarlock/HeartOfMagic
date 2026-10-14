/**
 * PreviewFitTest - node tests for the Scan page's tree previews (treePreviewUtils.js,
 * treePreviewSun.js):
 * - boxSize / fitCanvas: a box that is not laid out (its page hidden) leaves the
 *   canvas as it is, instead of the made-up 300 px it used to take and keep; a box
 *   that has a size gets it once (setting the size again would clear the canvas)
 * - contentExtent / fitScale: the mode's extent when it gives one, else the root
 *   ring plus decorations; the content fits the box with FIT_MARGIN round it
 * - schoolLabelPlace and TreePreviewSun.render: every school's name is past its
 *   roots and their arrows (no name over a root), and the extent the fit reads
 *   holds every name
 *
 * Depends on: colorUtils.js, treePreviewUtils.js, treePreviewSun.js - run in a sandbox of their
 * own with a stand-in 2d context that records what is drawn.
 */

var PreviewFitTest = {

    passed: 0,
    failed: 0,
    CHAR_PX: 7,   // the stand-in context's width of one letter

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _sandbox: function() {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var quiet = function() {};
        var box = { console: { log: quiet, warn: quiet, error: quiet }, document: { getElementById: function() { return null; } } };
        box.window = box;
        ['colorUtils.js', 'treePreviewUtils.js', 'treePreviewSun.js'].forEach(function(f) {
            vm.runInNewContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), box, { filename: f });
        });
        return box;
    },

    _box: function(w, h) {
        return { getBoundingClientRect: function() { return { width: w, height: h }; } };
    },

    _canvas: function() {
        return { width: 300, height: 150, style: {} };
    },

    /** A 2d context that draws nothing and records arcs (roots) and texts. */
    _ctx: function(rec) {
        var self = this;
        var state = { textAlign: 'start', textBaseline: 'alphabetic' };
        var ctx = {
            measureText: function(t) { return { width: String(t).length * self.CHAR_PX }; },
            arc: function(x, y, r) { rec.arcs.push({ x: x, y: y, r: r }); },
            fillText: function(t, x, y) { rec.texts.push({ t: t, x: x, y: y, align: ctx.textAlign, base: ctx.textBaseline }); },
            save: function() {}, restore: function() {}
        };
        ['beginPath', 'moveTo', 'lineTo', 'closePath', 'fill', 'stroke', 'fillRect', 'strokeRect', 'translate',
         'rotate', 'scale', 'setTransform', 'setLineDash', 'drawImage', 'clearRect', 'rect'].forEach(function(n) {
            ctx[n] = function() {};
        });
        ctx.textAlign = state.textAlign;
        ctx.textBaseline = state.textBaseline;
        return ctx;
    },

    /** A text's box from where it was drawn and how it was aligned. */
    _textBox: function(t, h) {
        var w = t.t.length * this.CHAR_PX;
        var l = t.align === 'left' ? t.x : (t.align === 'right' ? t.x - w : t.x - w / 2);
        var top = t.base === 'top' ? t.y : (t.base === 'bottom' ? t.y - h : t.y - h / 2);
        return { l: l, r: l + w, t: top, b: top + h };
    },

    _circleHitsBox: function(c, b) {
        var nx = Math.max(b.l, Math.min(c.x, b.r)), ny = Math.max(b.t, Math.min(c.y, b.b));
        var dx = c.x - nx, dy = c.y - ny;
        return dx * dx + dy * dy < c.r * c.r;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('PreviewFitTest');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var self = this;
        var box = this._sandbox();
        var U = box.TreePreviewUtils;
        var ctx2d = { setTransform: function() {}, scale: function() {} };

        // ---- boxSize / fitCanvas ----
        this.check(U.boxSize(this._box(0, 0)) === null && U.boxSize(null) === null, 'boxSize: a box not laid out has no size');
        var s = U.boxSize(this._box(1207.6, 500.4));
        this.check(s && s.w === 1207 && s.h === 500, 'boxSize: whole pixels');

        var canvas = this._canvas();
        this.check(U.fitCanvas(canvas, ctx2d, this._box(0, 0), 0, 0) === null && canvas.width === 300,
            'fitCanvas: measured with its page hidden, the canvas is left as it is (no 300 px stand-in)');
        s = U.fitCanvas(canvas, ctx2d, this._box(1207, 500), 0, 0);
        this.check(s && s.w === 1207 && canvas.width === 1207 && canvas.height === 500 && canvas.style.width === '1207px',
            'fitCanvas: shown, it takes its box\'s size');
        var writes = 0, watched = { style: {} };
        Object.defineProperty(watched, 'width', { get: function() { return 1207; }, set: function() { writes++; } });
        Object.defineProperty(watched, 'height', { get: function() { return 500; }, set: function() { writes++; } });
        this.check(U.fitCanvas(watched, ctx2d, this._box(1207, 500), 1207, 500) === null && writes === 0,
            'fitCanvas: the same size again writes nothing (that would clear the canvas)');
        this.check(U.fitCanvas(canvas, ctx2d, this._box(0, 0), 1207, 500) === null && canvas.width === 1207,
            'fitCanvas: hidden again later, the last size stays');

        // ---- contentExtent / fitScale ----
        var roots = { rootNodes: [{ x: 0, y: -120 }, { x: 100, y: 0 }] };
        var ext = U.contentExtent(roots);
        this.check(ext && ext.x === 120 + U.FIT_DECOR && ext.y === ext.x, 'contentExtent: no extent given - the root ring plus decorations');
        ext = U.contentExtent({ rootNodes: roots.rootNodes, extent: { x: 230, y: 175 } });
        this.check(ext.x === 230 && ext.y === 175, 'contentExtent: the mode\'s own extent wins');
        this.check(U.contentExtent({ rootNodes: [] }) === null && U.fitScale(null, 400, 400) === 1, 'contentExtent: nothing to fit - scale 1');
        this.check(U.fitScale({ x: 230, y: 175 }, 1207, 500) === 1, 'fitScale: never zooms in past 1');
        var sc = U.fitScale({ x: 230, y: 175 }, 1207, 300);
        this.check(Math.abs(175 * sc - (150 - U.FIT_MARGIN)) < 1e-9, 'fitScale: a short box - the height decides, FIT_MARGIN kept');

        // ---- school names past their roots ----
        var place = U.schoolLabelPlace(0, 100, 70, 12);
        this.check(place.align === 'left' && place.baseline === 'middle' && place.reachX === 170, 'schoolLabelPlace: on the right it starts at the point');
        place = U.schoolLabelPlace(-Math.PI / 2, 100, 70, 12);
        this.check(place.align === 'center' && place.baseline === 'bottom' && Math.abs(place.reachY - 112) < 1e-9,
            'schoolLabelPlace: at the top it sits above the point');
        place = U.schoolLabelPlace(Math.PI * 0.8, 100, 70, 12);
        this.check(place.align === 'right' && place.baseline === 'top', 'schoolLabelPlace: lower left hangs left and down');

        var Sun = box.TreePreviewSun;
        var w = 1207, h = 400;
        var names = { Alteration: 115, Conjuration: 169, Destruction: 229, Illusion: 103, Restoration: 137 };
        [false, true].forEach(function(invert) {
            var rec = { arcs: [], texts: [] };
            Sun.settings.invertGrowth = invert;
            Sun.render(self._ctx(rec), w, h, names);
            var labels = rec.texts.filter(function(t) { return names.hasOwnProperty(t.t); });
            var rootR = Sun.settings.nodeSize + 3;   // the glow
            var circles = rec.arcs.filter(function(a) { return a.r === rootR; });
            var clear = labels.length === 5 && circles.length >= 5;
            var extent = Sun._lastRenderData.extent;
            var inside = !!extent;
            labels.forEach(function(t) {
                var b = self._textBox(t, Sun.LABEL_HEIGHT);
                circles.forEach(function(c) { if (self._circleHitsBox(c, b)) clear = false; });
                if (extent && (Math.abs(b.l - w / 2) > extent.x + 1e-9 || Math.abs(b.r - w / 2) > extent.x + 1e-9 ||
                               Math.abs(b.t - h / 2) > extent.y + 1e-9 || Math.abs(b.b - h / 2) > extent.y + 1e-9)) inside = false;
            });
            var tag = invert ? ' (arrows inward)' : '';
            self.check(clear, 'TreePreviewSun: no school name lies over a root' + tag);
            self.check(inside, 'TreePreviewSun: its extent holds every name' + tag);
            var fit = U.fitScale(U.contentExtent(Sun._lastRenderData), w, h);
            self.check(fit > 0 && extent.y * fit <= h / 2 - U.FIT_MARGIN + 1e-9, 'TreePreviewSun: fitted, the names stay in the box' + tag);
        });
        Sun.settings.invertGrowth = false;

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.PreviewFitTest = PreviewFitTest;
if (typeof global !== 'undefined') global.PreviewFitTest = PreviewFitTest;
