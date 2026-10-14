/**
 * ChapterTitlesTest - node tests for where the book designs put their chapter
 * titles (TreeStyle._chapterPlaces / chapterBoxes, treeStyleBook.js):
 * - a title's box clears every spell under it, the outer ring of other schools
 *   included (the top title used to sit on the outermost ring), at the side
 *   (its long side along the spoke) as at the top, turned and zoomed
 * - the gap to the nearest spell is CHAPTER_GAP and not much more (titles stay
 *   by their school)
 * - a hidden school's spells push no title out (and showing it again does)
 * - a design without titles gets none
 *
 * Depends on: treeStyle.js, treeStyleBook.js - run in a sandbox of their own.
 */

var ChapterTitlesTest = {

    passed: 0,
    failed: 0,
    CHAR_PX: 8,
    NODE_R: 12,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _treeStyle: function() {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var quiet = function() {};
        var box = { console: { log: quiet, warn: quiet, error: quiet }, settings: { schoolVisibility: {} }, state: { treeData: null } };
        box.window = box;
        this._box = box;
        ['treeStyle.js', 'treeStyleBook.js'].forEach(function(f) {
            vm.runInNewContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), box, { filename: f });
        });
        return box.window.TreeStyle;
    },

    _ctx: function() {
        var self = this;
        return { font: '', save: function() {}, restore: function() {},
                 measureText: function(t) { return { width: String(t).length * self.CHAR_PX }; } };
    },

    /**
     * Four schools on spokes at -90, 0, 90 and 180 degrees. Each school's own
     * spells reach 200; a ring of another school's spells at 320 runs all round.
     */
    _renderer: function(zoom) {
        var nodes = [];
        var schools = { Top: -90, Right: 0, Bottom: 90, Left: 180 };
        var names = Object.keys(schools);
        names.forEach(function(name) {
            for (var k = -3; k <= 3; k++) {
                var a = (schools[name] + k * 6) * Math.PI / 180;
                nodes.push({ school: name, x: Math.cos(a) * 200, y: Math.sin(a) * 200 });
            }
        });
        for (var d = 0; d < 360; d += 3) {
            var b = d * Math.PI / 180;
            nodes.push({ school: names[d % 4], x: Math.cos(b) * 320, y: Math.sin(b) * 320 });
        }
        var sch = {};
        names.forEach(function(n) { sch[n] = { spokeAngle: schools[n] }; });
        return { nodes: nodes, schools: sch, zoom: zoom, panX: 0, panY: 0, _width: 2000, _height: 2000, MIN_NODE_SCREEN_RADIUS: 3 };
    },

    /** The smallest gap between any spell (its screen circle) and any of the boxes; negative when one overlaps. */
    _minGap: function(r, boxes, cx, cy, cos, sin) {
        var self = this, min = Infinity;
        r.nodes.forEach(function(n) {
            var sx = (n.x * cos - n.y * sin) * r.zoom + r.panX + cx;
            var sy = (n.x * sin + n.y * cos) * r.zoom + r.panY + cy;
            var rad = Math.max(self.NODE_R * r.zoom, r.MIN_NODE_SCREEN_RADIUS);
            boxes.forEach(function(b) {
                var dx = Math.max(b.l - sx, 0, sx - b.r), dy = Math.max(b.t - sy, 0, sy - b.b);
                var gap = Math.sqrt(dx * dx + dy * dy) - rad;
                if (gap < min) min = gap;
            });
        });
        return min;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('ChapterTitlesTest');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var TS = this._treeStyle();
        TS.tokens.chapterTitles = true;
        TS.tokens.chapterSize = 15;
        TS.tokens.labelHalo = null;
        var ctx = this._ctx();
        var self = this;

        [[1, 0], [0.5, 0], [1, 30], [0.75, -55]].forEach(function(view) {
            var r = self._renderer(view[0]);
            TS._chapterNodes = null;
            var rad = view[1] * Math.PI / 180, cos = Math.cos(rad), sin = Math.sin(rad);
            var boxes = TS.chapterBoxes(ctx, r, 1000, 1000, cos, sin, 0);
            var gap = self._minGap(r, boxes, 1000, 1000, cos, sin);
            var tag = ' (zoom ' + view[0] + ', turned ' + view[1] + ')';
            self.check(boxes.length === 4, 'four titles' + tag);
            self.check(gap > 0, 'no title over a spell, the other schools\' outer ring included' + tag);
            self.check(gap < TS.CHAPTER_GAP + 40, 'the titles stay by the tree (nearest spell ' + Math.round(gap) + ' px)' + tag);
        });

        var r = this._renderer(1);
        TS._chapterNodes = null;
        var places = TS._chapterPlaces(ctx, r, 1000, 1000, 1, 0, 0);
        var right = places.filter(function(p) { return p.text.indexOf('Right') !== -1; })[0];
        var top = places.filter(function(p) { return p.text.indexOf('Top') !== -1; })[0];
        this.check(right && right.x - right.w / 2 > 1000 + 320 + this.NODE_R,
            'the side title starts past the ring: its long side counted along the spoke');
        this.check(top && top.y + 15 * TS.CHAPTER_HALF_HEIGHT < 1000 - 320 - this.NODE_R,
            'the top title ends above the outer ring, not on it');

        // A school far out, hidden in the settings: not drawn, so it moves no title
        var withGhost = r.nodes.concat([{ school: 'Ghost', x: 0, y: -2000 }]);
        var rg = { nodes: withGhost, schools: r.schools, zoom: 1, panX: 0, panY: 0, _width: 2000, _height: 2000, MIN_NODE_SCREEN_RADIUS: 3 };
        var topOf = function() {
            return TS._chapterPlaces(ctx, rg, 1000, 1000, 1, 0, Infinity).filter(function(p) { return p.text.indexOf('Top') !== -1; })[0];
        };
        this._box.settings.schoolVisibility = { Ghost: false };
        var hiddenTop = topOf();
        this._box.settings.schoolVisibility = {};
        var shownTop = topOf();
        this.check(hiddenTop && Math.abs(hiddenTop.y - top.y) < 1e-9, 'a hidden school\'s spells push no title out');
        this.check(shownTop && shownTop.y < 1000 - 2000, 'shown again, they do (the outer edge is worked out again)');

        TS.tokens.chapterTitles = false;
        this.check(TS.chapterBoxes(ctx, r, 1000, 1000, 1, 0, 0).length === 0, 'no titles in a design without them');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.ChapterTitlesTest = ChapterTitlesTest;
if (typeof global !== 'undefined') global.ChapterTitlesTest = ChapterTitlesTest;
