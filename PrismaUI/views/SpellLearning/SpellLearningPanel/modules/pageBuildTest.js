/**
 * PageBuildTest - node tests for the design page painted over frames
 * (TreeStyle.stepPage, treeStyleBook.js), with a stand-in canvas that records
 * its calls (run-tests.js):
 * - the steps go in the page's drawing order (colour, blotches, fibres, light,
 *   corners, dark edges), one a call when there is no time, the texture swapped
 *   in (and StaticBase told) only when all are done;
 * - the light and the dark edges are painted in bands that add up to the one
 *   rect, with whole-pixel edges inside the page;
 * - meanwhile the old texture of the same colour stands in, else the plain colour;
 * - a change of look (another design, the corner drawing arriving) starts again,
 *   an effect that does not touch the page keeps it, turning the page off drops it;
 * - frames are asked for without marking the tree changed, the corner drawing's
 *   arrival included.
 * The pixels themselves were compared with the one-go painting in a browser
 * (identical at several sizes, fractional ones too); see docs/DESIGN.md.
 *
 * Depends on: treeStyle.js and treeStyleBook.js (loaded here, then put back)
 */

var PageBuildTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** A stand-in canvas whose context records what it is asked to draw. */
    _canvas: function(log) {
        var ctx = { calls: log };
        ['fillRect', 'beginPath', 'moveTo', 'lineTo', 'stroke', 'save', 'restore', 'translate', 'scale'].forEach(function(n) {
            ctx[n] = function() { log.push(n + '(' + Array.prototype.slice.call(arguments).join(',') + ')'); };
        });
        ctx.createRadialGradient = function() { log.push('gradient'); return { addColorStop: function() {} }; };
        ctx.drawImage = function(img, x, y, w, h) { log.push('drawImage(' + (img && img.id) + ',' + x + ',' + y + ',' + w + ',' + h + ')'); };
        return { width: 0, height: 0, id: 'page' + (this._made = (this._made || 0) + 1), getContext: function() { return ctx; } };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        var saved = { ts: g.TreeStyle, cr: g.CanvasRenderer, doc: g.document, lb: g.LayerBuild,
                      unm: g.LayerBuild ? g.LayerBuild._unmeasured : false, cold: g.LayerBuild ? g.LayerBuild._textCold : false, st: g.setTimeout, img: g.Image };
        var self = this, log = [];
        var timers = [];
        g.setTimeout = function(fn) { timers.push(fn); return timers.length; };
        g.document = { createElement: function() { return self._canvas(log); }, documentElement: { getAttribute: function() { return ''; } } };
        g.Image = function() { this.width = 200; this.height = 200; };
        // What TreeStyle needs of the renderer: colours, and a frame asked for (the accessor marks the tree)
        var cr = { __needsRender: false, _treeDirty: false, _animationOnlyRender: true, _width: 0, _height: 0, _frameStartAt: 0,
                   parseColor: function() { return { r: 1, g: 2, b: 3 }; } };
        Object.defineProperty(cr, '_needsRender', {
            get: function() { return this.__needsRender; },
            set: function(v) { this.__needsRender = v; if (v) this._treeDirty = true; }
        });
        g.CanvasRenderer = cr;
        try {
            delete require.cache[require.resolve('./treeStyle.js')];
            delete require.cache[require.resolve('./treeStyleBook.js')];
            require('./treeStyle.js');
            require('./treeStyleBook.js');
            this._tests(g.TreeStyle, cr, log, timers);
        } catch (e) {
            this.check(false, 'page tests ran: ' + (e && e.stack || e));
        } finally {
            g.TreeStyle = saved.ts; g.CanvasRenderer = saved.cr; g.document = saved.doc; g.setTimeout = saved.st; g.Image = saved.img;
            if (g.LayerBuild) { g.LayerBuild._unmeasured = saved.unm; g.LayerBuild._textCold = saved.cold; }
        }
        return { passed: this.passed, failed: this.failed };
    },

    _tests: function(T, cr, log, timers) {
        var page = { pageColor: '#e6d6b0', pageGrain: 0.9, pageGrainColor: '#5a4020', pageGlow: '#fff4d6',
                     pageEdge: '#3a2810', pageEdgeAlpha: 0.55, pageOrnament: 'corner.png', selectionSigil: true };
        T.set(page);
        this.check(timers.length === 1, 'a design with a page starts the idle timer (the tree may not be drawn)');

        // Painted one step a call when there is no time
        var w = 1000.5, h = 700.25, calls = 0, state;
        log.length = 0;
        cr._treeDirty = false;                    // (set() marks the tree: new colours)
        timers.length = 0; T._pageTimer = 0;
        do { cr.__needsRender = false; state = T.stepPage(w, h, 0, -1); calls++; }
        while (state === 'pending' && calls < 500);
        var steps = T._pageBuilds === 1 && T._page ? calls : -1;
        var bands = Math.ceil(h / T.PAGE_BAND_PX);
        this.check(state === 'done' && steps === 1 + T.PAGE_BLOTCHES + 1 + bands + 1 + bands,
            'no time left: one step a frame (colour, 40 blotches, fibres, light and dark edges in bands, corners), then swapped in');
        this.check(cr.__needsRender === true && cr._treeDirty === false && cr._animationOnlyRender === false,
            'the next frame is asked for, unthrottled, without marking the tree changed');
        this.check(timers.length === 1 && T._pageTimer === 1, '...and the idle timer, should the frames stop before it is done');
        var order = log.filter(function(c) { return /^(fillRect|stroke|drawImage)/.test(c); }).map(function(c) { return c.split('(')[0]; });
        var strokeAt = order.indexOf('stroke');
        this.check(order[0] === 'fillRect' && strokeAt === 1 + T.PAGE_BLOTCHES && order.length === strokeAt + 1 + 2 * bands &&
            order.indexOf('drawImage') < 0, 'the steps keep the order the page was drawn in (no corners while their drawing loads)');
        var rects = log.filter(function(c) { return c.indexOf('fillRect') === 0; }).map(function(c) {
            return c.slice(9, -1).split(',').map(Number);
        });
        var light = rects.slice(1 + T.PAGE_BLOTCHES, 1 + T.PAGE_BLOTCHES + bands), edge = rects.slice(-bands);
        var tiles = function(list) {
            var y = 0;
            for (var i = 0; i < list.length; i++) {
                var r = list[i];
                if (r[0] !== 0 || r[2] !== w || r[1] !== y) return false;
                y = r[1] + r[3];
                if (i < list.length - 1 && y !== Math.round(y)) return false;
            }
            return y === h;
        };
        this.check(tiles(light) && tiles(edge), 'the light and the dark edges: bands that add up to the one rect, whole-pixel edges inside');

        // Ready: nothing more to do
        log.length = 0;
        this.check(T.stepPage(w, h, 0, -1) === 'none' && log.length === 0, 'the texture ready: no steps');

        // The old texture of the same colour stands in while another size is painted
        var screen = this._canvas([]).getContext();
        T.stepPage(800, 600, 0, -1);
        T.renderPage(screen, 800, 600);
        this.check(screen.calls.join() === 'drawImage(' + T._page.id + ',0,0,800,600)' && T._pageJob,
            'while painting a new size the old texture is shown, stretched');

        // An effect other than the page keeps the texture; another design's page starts again
        var kept = T._page, builds = T._pageBuilds;
        T.setEffectsOff({ sigil: true });
        this.check(T._page === kept && T._pageBuilds === builds, 'an effect that does not touch the page keeps the texture');
        T.set({ pageColor: '#26302c', pageGrain: 0.8, pageGrainColor: '#000000' });
        this.check(T.stepPage(w, h, 0, -1) === 'pending' && T._pageJob.steps.length === 1 + T.PAGE_BLOTCHES + 1 + 1,
            'another design: a new texture is painted (only the steps its tokens use)');
        screen.calls.length = 0;
        T.renderPage(screen, w, h);
        this.check(screen.calls.join() === 'fillRect(0,0,' + w + ',' + h + ')', "...and meanwhile the page's plain colour, not the other design's texture");
        T.stepPage(w, h, 0, 1e9);
        this.check(T._page !== kept && !T._pageJob && T._pageColor === '#26302c', 'time enough: all its steps in one call');

        // The corner drawing arriving: the look changes, the old texture stays up meanwhile
        T.set(page);
        T.stepPage(w, h, 0, 1e9);
        var entry = T._images['corner.png'];
        cr.__needsRender = false; cr._treeDirty = false; timers.length = 0; T._pageTimer = 0;
        entry.img.onload();
        this.check(cr.__needsRender === true && cr._treeDirty === false && timers.length === 1,
            'the corner drawing arriving asks for a frame (not the tree marked changed) and the idle timer');
        var before = T._page;
        log.length = 0;
        this.check(T.stepPage(w, h, 0, -1) === 'pending' && T._page === before, '...and the page is painted again, the old texture up meanwhile');
        T.stepPage(w, h, 0, 1e9);
        order = log.filter(function(c) { return /^(fillRect|stroke|drawImage)/.test(c); }).map(function(c) { return c.split('(')[0]; });
        strokeAt = order.indexOf('stroke');
        this.check(order.indexOf('drawImage') === strokeAt + 1 + bands && order.lastIndexOf('drawImage') === strokeAt + bands + 4 &&
            order.length === strokeAt + 1 + 2 * bands + 4, '...with the four corners between the light and the dark edges');


        // The idle timer paints while no frames come, and leaves it to them when they do
        cr._width = 900; cr._height = 650; cr._frameStartAt = 0;      // a new size, the tree not drawn
        timers.length = 0; T._pageTimer = 0;
        T.PAGE_IDLE_STEP_MS = -1;                                      // (a stand-in canvas draws in no time)
        T._pageIdleTick();
        var next = T._pageJob ? T._pageJob.next : -1;
        this.check(next > 0 && T._pageJob.key === '900x650' && timers.length === 1, 'no frames: the idle timer paints a slice and comes back');
        cr._frameStartAt = (typeof performance !== 'undefined' ? performance.now() : Date.now());
        timers.length = 0; T._pageTimer = 0;
        next = T._pageJob.next;
        T._pageIdleTick();
        this.check(T._pageJob.next === next && timers.length === 1, 'frames coming: the timer leaves the painting to them');

        // The panel hidden: the timer neither paints nor comes back (it would paint behind the game)
        var wasVisible = window._panelVisible;
        window._panelVisible = false; cr._frameStartAt = 0;
        timers.length = 0; T._pageTimer = 0; next = T._pageJob.next;
        T._pageIdleTick(); T._pageIdleLater();
        this.check(T._pageJob.next === next && timers.length === 0, 'panel hidden: the timer neither paints nor comes back');
        window._panelVisible = wasVisible;

        // A frame with no time left paints no step - but not PAGE_MAX_SKIPS frames in a row
        var frameAt = (typeof performance !== 'undefined' ? performance.now() : Date.now()), frames = 0;
        next = T._pageJob.next;
        while (T._pageJob && T._pageJob.next === next && frames < 50) { T.stepPage(900, 650, frameAt, -1, true); frames++; }
        this.check(frames === T.PAGE_MAX_SKIPS + 1 && cr.__needsRender === true,
            'frames with no time left: no step (the next frame asked for), then one after ' + T.PAGE_MAX_SKIPS + ' in a row');

        // The timer stops: the texture done, no size, no page; a failed canvas is tried again for a new look
        T.PAGE_IDLE_STEP_MS = 1e9; cr._frameStartAt = 0;
        timers.length = 0; T._pageTimer = 0;
        T._pageIdleTick();
        this.check(T._pageReady(900, 650) && timers.length === 0, 'the texture done: the timer does not come back');
        cr._width = 0; T._pageFailed = false;
        T._pageIdleTick();
        this.check(timers.length === 0, 'no canvas size: the timer does not come back (a size starts it)');
        T._pageFailed = true; cr._width = 900; cr._height = 400;
        T._pageIdleTick();
        this.check(timers.length === 0 && !T._pageJob, 'a canvas that failed: no timer, no painting');
        T.set(page);
        this.check(T._pageFailed === false, '...until a design is applied again');
        T.set({});
        timers.length = 0; T._pageTimer = 0;
        T._pageIdleTick();
        this.check(!T._page && !T._pageJob && T.renderPage(screen, w, h) === false && timers.length === 0,
            'a design without a page drops the texture, and the timer stops');
    }
};

if (typeof window !== 'undefined') window.PageBuildTest = PageBuildTest;
