/**
 * HomeViewTest - node tests for the tree's home view (TreeCamera, treeCamera.js):
 * - fitTarget: the whole world box inside the given part of the canvas with its
 *   padding, centred on that part, never closer than the cap
 * - visibleRect: the canvas less the details bar (bottom), the sidebar (right)
 *   and the open How-to-Learn panel (left); a hidden panel covers nothing; null
 *   while the canvas has no size (another tab in front)
 * - getPanelOffset: the middle of that part, as the old bar/sidebar offsets
 * - homeTarget / settleHome: hidden schools left out, the design's chapter
 *   titles given room, the pending home view kept until the tree can be measured,
 *   a view asked for after the load (animateTo) wins over it
 *
 * Depends on: treeCamera.js - run in a sandbox of its own with stand-in
 * elements, so the page's globals are not touched.
 */

var HomeViewTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _rect: function(left, top, w, h) {
        return { left: left, top: top, right: left + w, bottom: top + h, width: w, height: h };
    },

    /** An element with a client rect and a class list. */
    _el: function(rect, classes) {
        var cls = classes || [];
        return {
            getBoundingClientRect: function() { return rect; },
            classList: { contains: function(c) { return cls.indexOf(c) !== -1; } }
        };
    },

    /** TreeCamera in a sandbox; els: id -> element. */
    _camera: function(els, extra) {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var box = {
            console: { log: function() {}, warn: function() {}, error: function() {} },
            document: { getElementById: function(id) { return els[id] || null; } },
            settings: { schoolVisibility: {} },
            state: { treeData: null },
            performance: { now: function() { return 0; } },
            requestAnimationFrame: function() { return 1; },
            cancelAnimationFrame: function() {}
        };
        for (var k in extra || {}) if (extra.hasOwnProperty(k)) box[k] = extra[k];
        box.window = box;
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'treeCamera.js'), 'utf8'), box, { filename: 'treeCamera.js' });
        return box;
    },

    /** A renderer on a 1000 x 800 canvas at the page's top left. */
    _renderer: function(canvasRect, nodes) {
        var self = this;
        return {
            canvas: self._el(canvasRect || self._rect(0, 0, 1000, 800)),
            _width: 1000, _height: 800,
            zoom: 0.75, panX: 0, panY: 0, rotation: 0,
            nodes: nodes || [], schools: {},
            showZoom: function() {}
        };
    },

    /** Does the world box, seen through cam, sit inside rect with pad round it? */
    _inside: function(bounds, cam, rect, pad, size) {
        var l = bounds.minX * cam.zoom + cam.panX + size.w / 2, r = bounds.maxX * cam.zoom + cam.panX + size.w / 2;
        var t = bounds.minY * cam.zoom + cam.panY + size.h / 2, b = bounds.maxY * cam.zoom + cam.panY + size.h / 2;
        var eps = 0.01;
        return l >= rect.x + pad.x - eps && r <= rect.x + rect.w - pad.x + eps &&
               t >= rect.y + pad.y - eps && b <= rect.y + rect.h - pad.y + eps;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('HomeViewTest');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var self = this;
        var size = { w: 1000, h: 800 };
        var near = function(a, b) { return Math.abs(a - b) < 0.01; };

        // ---- fitTarget ----
        var TC = this._camera({}).TreeCamera;
        var small = { minX: -100, maxX: 100, minY: -100, maxY: 100 };
        var full = { x: 0, y: 0, w: 1000, h: 800 };
        var cam = TC.fitTarget(small, full, size, { x: 24, y: 24 }, 0.75);
        this.check(cam.zoom === 0.75 && near(cam.panX, 0) && near(cam.panY, 0) && cam.rotation === 0,
            'fitTarget: a small tree stays at the cap, centred on the whole canvas');

        var bar = { x: 0, y: 0, w: 1000, h: 560 };   // a 240px bar over the bottom
        cam = TC.fitTarget(small, bar, size, { x: 24, y: 24 }, 0.75);
        this.check(near(cam.panY, -120) && near(cam.panX, 0), 'fitTarget: centred on the part above the bar (heart 120px up)');

        var big = { minX: -400, maxX: 400, minY: -380, maxY: 380 };
        var pad = { x: 24, y: 60 };
        cam = TC.fitTarget(big, bar, size, pad, 0.75);
        this.check(cam.zoom < 0.75 && this._inside(big, cam, bar, pad, size),
            'fitTarget: a big tree is zoomed out until it and its padding fit above the bar');
        this.check(near(cam.zoom, (560 - 120) / 760), 'fitTarget: the tighter side decides the zoom');

        var offset = { minX: 0, maxX: 400, minY: -100, maxY: 300 };
        cam = TC.fitTarget(offset, full, size, { x: 24, y: 24 }, 0.75);
        var midX = 200 * cam.zoom + cam.panX + 500, midY = 100 * cam.zoom + cam.panY + 400;
        this.check(near(midX, 500) && near(midY, 400), 'fitTarget: the box\'s middle, not the heart, goes in the middle');

        var huge = { minX: -1e6, maxX: 1e6, minY: -1e6, maxY: 1e6 };
        this.check(TC.fitTarget(huge, full, size, { x: 24, y: 24 }, 0.75).zoom === TC.MIN_ZOOM,
            'fitTarget: never below MIN_ZOOM');
        this.check(TC.fitTarget(small, { x: 0, y: 0, w: 30, h: 30 }, size, { x: 24, y: 24 }, 0.75).zoom === TC.MIN_ZOOM,
            'fitTarget: a part smaller than its padding gets MIN_ZOOM, not a negative zoom');

        // ---- visibleRect / getPanelOffset ----
        var canvasRect = this._rect(20, 100, 1000, 800);
        var bottomBar = this._el(this._rect(30, 100 + 800 - 206, 980, 206));
        var box = this._camera({ 'details-panel': bottomBar });
        var r = this._renderer(canvasRect);
        box.CanvasRenderer = r;
        var vr = box.TreeCamera.visibleRect(r);
        this.check(vr && vr.x === 0 && vr.y === 0 && vr.w === 1000 && vr.h === 594, 'visibleRect: the bottom bar is cut off the bottom');
        var off = box.TreeCamera.getPanelOffset();
        this.check(off.x === 0 && off.y === -103, 'getPanelOffset: half the bar up, as before');

        var side = this._el(this._rect(20 + 1000 - 280, 110, 270, 780));
        box = this._camera({ 'details-panel': side });
        box.CanvasRenderer = r;
        vr = box.TreeCamera.visibleRect(r);
        this.check(vr && vr.x === 0 && vr.w === 720 && vr.h === 800, 'visibleRect: the sidebar is cut off the right');
        this.check(box.TreeCamera.getPanelOffset().x === -140, 'getPanelOffset: half the sidebar to the left');

        var howto = this._el(this._rect(20, 150, 260, 600), ['open']);
        box = this._camera({ 'details-panel': bottomBar, 'howto-panel': howto });
        vr = box.TreeCamera.visibleRect(r);
        this.check(vr && vr.x === 260 && vr.w === 740 && vr.h === 594, 'visibleRect: the open How-to-Learn panel is cut off the left');
        box = this._camera({ 'details-panel': this._el(this._rect(30, 694, 980, 206), ['hidden']),
                             'howto-panel': this._el(this._rect(20, 150, 30, 600)) });
        vr = box.TreeCamera.visibleRect(r);
        this.check(vr && vr.w === 1000 && vr.h === 800, 'visibleRect: a hidden bar and a closed How-to tab cover nothing');
        box = this._camera({ 'details-panel': this._el(this._rect(0, 0, 0, 0)) });
        vr = box.TreeCamera.visibleRect(r);
        this.check(vr && vr.h === 800, 'visibleRect: a showing panel with no size covers nothing');

        var hiddenTab = this._renderer(this._rect(0, 0, 0, 0));
        box = this._camera({ 'details-panel': bottomBar });
        this.check(box.TreeCamera.visibleRect(hiddenTab) === null, 'visibleRect: null while the tree tab is hidden');

        // ---- homeTarget / settleHome ----
        var nodes = [];
        for (var i = 0; i < 72; i++) {
            var a = i * 5 * Math.PI / 180;
            nodes.push({ school: 'ABCDE'.charAt(Math.round(((i * 5 + 90) % 360) / 72) % 5), x: Math.cos(a) * 380, y: Math.sin(a) * 380 });
        }
        nodes.push({ school: 'Hidden', x: 3000, y: 3000 });
        var bar200 = this._el(this._rect(0, 600, 1000, 200));
        box = this._camera({ 'details-panel': bar200 }, { settings: { schoolVisibility: { Hidden: false } } });
        r = this._renderer(this._rect(0, 0, 1000, 800), nodes);
        box.CanvasRenderer = r;
        var home = box.TreeCamera.homeTarget(r, 0.75);
        var ring = { minX: -380, maxX: 380, minY: -380, maxY: 380 };
        this.check(home && this._inside(ring, home, { x: 0, y: 0, w: 1000, h: 600 }, { x: 24, y: 24 }, size),
            'homeTarget: the tree fits above the bar, a hidden school left out');

        // Titles as screen boxes past the ring along their spokes (TreeStyle.chapterBoxes at zoom z, no pan)
        r.ctx = {};
        var above = { x: 0, y: 0, w: 1000, h: 600 };
        var sides = function(z) {
            return [{ l: 380 * z + 20, r: 380 * z + 170, t: -11, b: 11 }, { l: -380 * z - 170, r: -380 * z - 20, t: -11, b: 11 }];
        };
        var tops = function(z) {
            return [{ l: -60, r: 60, t: -380 * z - 60, b: -380 * z - 38 }, { l: -60, r: 60, t: 380 * z + 38, b: 380 * z + 60 }];
        };
        var boxesInside = function(cam, boxes) {
            return boxes(cam.zoom).every(function(b) {
                return b.l + cam.panX + 500 >= 24 - 0.01 && b.r + cam.panX + 500 <= 976 + 0.01 &&
                       b.t + cam.panY + 400 >= 24 - 0.01 && b.b + cam.panY + 400 <= 576 + 0.01;
            });
        };
        box.TreeStyle = { chapterBoxes: function(ctx, view) { return sides(view.zoom); } };
        var titled = box.TreeCamera.homeTarget(r, 0.75);
        this.check(Math.abs(titled.zoom - home.zoom) < 1e-6 && boxesInside(titled, sides) &&
                   this._inside(ring, titled, above, { x: 24, y: 24 }, size),
            'homeTarget: side titles take room across only - with room there, the zoom is the one without titles');
        box.TreeStyle = { chapterBoxes: function(ctx, view) { return tops(view.zoom); } };
        titled = box.TreeCamera.homeTarget(r, 0.75);
        this.check(titled.zoom < home.zoom && Math.abs(titled.zoom - (276 - 60) / 380) < 1e-4 && boxesInside(titled, tops),
            'homeTarget: top and bottom titles take room down, just their own height');

        // The real titles (treeStyle.js + treeStyleBook.js) on five spokes round the ring, as the five vanilla schools
        var fs = require('fs'), path = require('path'), vm = require('vm');
        ['treeStyle.js', 'treeStyleBook.js'].forEach(function(f) {
            vm.runInNewContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), box, { filename: f });
        });
        var TS = box.TreeStyle;
        TS.tokens.chapterTitles = true; TS.tokens.chapterSize = 15; TS.tokens.labelHalo = null;
        r.ctx = { font: '', save: function() {}, restore: function() {}, measureText: function(t) { return { width: String(t).length * 8 }; } };
        r.schools = { A: { spokeAngle: -90 }, B: { spokeAngle: -18 }, C: { spokeAngle: 54 }, D: { spokeAngle: 126 }, E: { spokeAngle: 198 } };
        r.MIN_NODE_SCREEN_RADIUS = 3;
        var real = box.TreeCamera.homeTarget(r, 0.75);
        var view = { nodes: r.nodes, schools: r.schools, zoom: real.zoom, panX: real.panX, panY: real.panY, _width: 1000, _height: 800, MIN_NODE_SCREEN_RADIUS: 3 };
        var drawn = TS.chapterBoxes(r.ctx, view, 500, 400, 1, 0, 0);
        var allIn = drawn.length === 5 && drawn.every(function(b) { return b.l >= 23.99 && b.r <= 976.01 && b.t >= 23.99 && b.b <= 576.01; });
        var widest = String('—  A  —').length * 8, lead = 12 * 0.75 + TS.CHAPTER_GAP;
        var uniform = Math.min((1000 - 2 * (24 + lead + widest)) / 760, (600 - 2 * (24 + lead + 2 * 15 * TS.CHAPTER_HALF_HEIGHT)) / 760);
        this.check(allIn && this._inside(ring, real, above, { x: 24, y: 24 }, size),
            'homeTarget: with the real titles, tree and every title in the part above the bar');
        this.check(real.zoom > uniform + 0.01, 'homeTarget: closer than with the widest title\'s room kept on every side (' +
            real.zoom.toFixed(3) + ' > ' + uniform.toFixed(3) + ')');

        // A real-size tree (1,419 spells over about +-2,300 world units): fitting it whole needs ~0.12, under the
        // simpler levels of detail; it opens at the floor with the heart in the middle of the part above the bar
        var big2300 = [];
        for (var k = 0; k < 360; k += 3) {
            var ba = k * Math.PI / 180;
            big2300.push({ school: 'A', x: Math.cos(ba) * 2300, y: Math.sin(ba) * 2300 }, { school: 'B', x: Math.cos(ba) * 300, y: Math.sin(ba) * 300 });
        }
        var bigBox = this._camera({ 'details-panel': bar200 }, { state: { treeData: { globe: { x: 40, y: -20 } } } });
        var bigR = this._renderer(this._rect(0, 0, 1000, 800), big2300);
        bigBox.CanvasRenderer = bigR;
        var unfloored = bigBox.TreeCamera.homeTarget(bigR, 0.75);
        var floored = bigBox.TreeCamera.homeTarget(bigR, 0.75, 0.5);
        this.check(unfloored.zoom < 0.2, 'a real-size tree fits whole only far out (' + unfloored.zoom.toFixed(3) + ')');
        this.check(floored.zoom === 0.5 && near(40 * 0.5 + floored.panX + 500, 500) && near(-20 * 0.5 + floored.panY + 400, 300),
            'homeTarget: below the floor it opens at the floor, the heart in the middle of the part above the bar');
        var smallFloored = box.TreeCamera.homeTarget(r, 0.75, 0.5);
        this.check(Math.abs(smallFloored.zoom - real.zoom) < 1e-9 && smallFloored.zoom >= 0.5, 'homeTarget: a tree that fits above the floor still fits whole');
        bigBox.TreeNav = { HOME_ZOOM: 0.75, HOME_MIN_ZOOM: 0.5 };
        this.check(bigBox.TreeCamera.settleHome(bigR) && bigR.zoom === 0.5, 'settleHome: the first view uses TreeNav\'s floor');

        var pendingBox = this._camera({ 'details-panel': bar200 });
        var hiddenR = this._renderer(this._rect(0, 0, 0, 0), nodes);
        pendingBox.CanvasRenderer = hiddenR;
        this.check(pendingBox.TreeCamera.settleHome(hiddenR) === false && hiddenR.zoom === 0.75 && hiddenR.panY === 0,
            'settleHome: waits while the tree cannot be measured, the view untouched');
        hiddenR.canvas = this._el(this._rect(0, 0, 1000, 800));
        this.check(pendingBox.TreeCamera.settleHome(hiddenR) === true && hiddenR.zoom <= 0.75 && hiddenR.panY < 0,
            'settleHome: once shown, the fitted view goes in (above the bar)');

        hiddenR._homePending = true;
        pendingBox.TreeCamera.animateTo({ zoom: 1, panX: 5, panY: 5 });
        this.check(hiddenR._homePending === false, 'animateTo: a view asked for after the load wins over the pending home view');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.HomeViewTest = HomeViewTest;
if (typeof global !== 'undefined') global.HomeViewTest = HomeViewTest;
