/**
 * LabelHaloTest - node tests for the names' outline (labelHalo) and the zoom
 * under which it is left out (run-tests.js), with stand-in contexts counting
 * strokeText calls:
 * - TreeStyle.beginLabels / drawLabel: a design with an outline strokes each
 *   name at TreeStyle.LABEL_HALO_MIN_ZOOM and above, not below it; with no zoom
 *   given it strokes as before; a design without one never strokes.
 * - LayerBuild._warmText: below the threshold the names' letters are made
 *   filled only (no outline that will not be drawn), the chapter titles'
 *   letters still outlined (titles keep their outline at every zoom); above
 *   it both are outlined.
 *
 * Depends on: treeStyle.js (run here in a sandbox of its own, so the page's
 * globals are not touched), LayerBuild (layerBuild.js); swaps the global
 * TreeStyle for the sandboxed one while it runs.
 */

var LabelHaloTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** The real TreeStyle, from a sandbox with no document and no LayerBuild. */
    _treeStyle: function() {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var box = { window: {}, console: console };
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'treeStyle.js'), 'utf8'), box, { filename: 'treeStyle.js' });
        return box.window.TreeStyle;
    },

    /** A 2d context that draws nothing and logs its text calls. */
    _ctx: function(log) {
        return { font: '', setTransform: function() {}, measureText: function(t) { return { width: t.length }; },
                 strokeText: function(c) { log.push('stroke ' + c); }, fillText: function(c) { log.push('fill ' + c); } };
    },

    _strokes: function(log) {
        return log.filter(function(x) { return x.indexOf('stroke ') === 0; }).length;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('LabelHaloTest');
        var TS = this._treeStyle();
        var min = TS.LABEL_HALO_MIN_ZOOM;
        this.check(min === 0.8, 'the threshold is 0.8');

        var self = this;
        var names = function(zoom, halo) {
            var log = [], ctx = self._ctx(log);
            TS.tokens.labelHalo = halo;
            TS.beginLabels(ctx, 12, zoom);
            TS.drawLabel(ctx, 'Flames', 0, 0, '#fff');
            TS.drawLabel(ctx, 'Sparks', 0, 20, '#fff');
            return log;
        };
        var below = names(0.79, 'rgba(0,0,0,0.9)'), at = names(min, 'rgba(0,0,0,0.9)');
        this.check(this._strokes(below) === 0 && below.length === 2, 'under the threshold: two names filled, no strokeText');
        this.check(this._strokes(at) === 2 && at.length === 4, 'at the threshold: each name outlined (one strokeText each)');
        this.check(this._strokes(names(1.5, 'rgba(0,0,0,0.9)')) === 2, 'zoomed in: each name outlined');
        this.check(this._strokes(names(undefined, 'rgba(0,0,0,0.9)')) === 2, 'no zoom given: outlined as before');
        this.check(this._strokes(names(1.5, '')) === 0, 'a design without an outline: no strokeText at any zoom');

        this._warm(TS);
        TS.tokens.labelHalo = '';
        return { passed: this.passed, failed: this.failed };
    },

    /** The warm-up makes only the outlines that will be drawn. */
    _warm: function(TS) {
        var g = (typeof global !== 'undefined') ? global : window;
        var B = g.LayerBuild;
        if (!B) { this.check(false, 'LayerBuild loaded'); return; }
        var savedTS = g.TreeStyle, savedCanvas = B._warmCanvas;
        var log = [], wctx = this._ctx(log);
        TS.tokens.labelHalo = 'rgba(0,0,0,0.9)';
        TS.tokens.chapterTitles = true;
        TS.tokens.chapterSize = 15;
        TS._schoolName = function(n) { return n; };
        g.TreeStyle = TS;
        B._warmCanvas = { width: 0, height: 0, getContext: function() { return wctx; } };
        var warm = function(zoom) {
            log.length = 0;
            var r = { zoom: zoom, schools: { Fire: {} }, _labelWidths: null, _labelWidthFont: null,
                      _labelCandidates: function() { return { candidates: [{ text: 'ab' }], fontSize: 10 }; },
                      _labelFontFrom: function(ctx) { this._labelWidths = new Map(); },
                      _labelWidth: function(ctx, t) { return t.length; } };
            var b = { view: { cx: 0, cy: 0, cos: 1, sin: 0 }, margin: 0, dpr: 1 };
            B._warmText(r, b, function() { return 1; });
            return log.slice();
        };
        try {
            var low = warm(0.6), high = warm(0.9);
            var nameLetters = function(l) { return l.filter(function(x) { return / [ab]$/.test(x); }); };
            var titleStrokes = function(l) { return l.filter(function(x) { return /^stroke [—Fire ]$/.test(x); }).length; };
            this.check(nameLetters(low).join() === 'fill a,fill b', 'zoom 0.6: the names\' letters made filled only');
            this.check(titleStrokes(low) === 6, '...the chapter titles\' letters still outlined');
            this.check(nameLetters(high).join() === 'stroke a,fill a,stroke b,fill b', 'zoom 0.9: the names\' letters outlined and filled');
            this.check(titleStrokes(high) === 6, '...and the titles\' too');
        } finally {
            g.TreeStyle = savedTS;
            B._warmCanvas = savedCanvas;
        }
    }
};

window.LabelHaloTest = LabelHaloTest;
