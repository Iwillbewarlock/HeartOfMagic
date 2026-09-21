/**
 * SpellLearning 3D Performance Probe (Developer Mode only)
 *
 * Answers one question before any 3D tree is designed: can this engine redraw a
 * tree sized scene every frame? PrismaUI has no WebGL, so a 3D tree would be
 * what the centre globe already is - points projected by hand onto a 2D canvas -
 * only with a whole load order of spells instead of 200 particles.
 *
 * The probe rotates a fake tree and steps through the things a real one would
 * add, one at a time, so the cost of each shows up on its own:
 *
 *   points -> + lines -> + depth sort -> + labels -> cheaper dots -> stress
 *
 * Results go on screen and into the SKSE log (LogMessage), so they can be read
 * back without a screenshot. Nothing here touches the real tree or any setting.
 */

var Perf3DTest = {
    PHASE_SECONDS: 4,
    SPELLS: 1440,          // the dev load order's tome scan
    STRESS_SPELLS: 3000,
    LINK_FACTOR: 1.65,     // the tree builder's links per spell (2370 / 1440)
    LABELS: 150,           // a generous count of labels on screen at once
    SCHOOLS: 5,
    FOCAL: 520,            // projection focal length, px
    CAMERA_Z: 900,
    CLUSTER_RADIUS: 300,
    SCHOOL_COLORS: ['#ef4444', '#facc15', '#22c55e', '#a855f7', '#38bdf8'],

    _overlay: null,
    _canvas: null,
    _ctx: null,
    _running: false,
    _results: [],

    /** Phases run in order; each changes one thing against the one before. */
    _phases: function() {
        return [
            { name: 'points only',            n: this.SPELLS,        lines: false, sort: false, labels: false, dots: 'arc' },
            { name: '+ lines',                n: this.SPELLS,        lines: true,  sort: false, labels: false, dots: 'arc' },
            { name: '+ depth sort',           n: this.SPELLS,        lines: true,  sort: true,  labels: false, dots: 'arc' },
            { name: '+ labels',               n: this.SPELLS,        lines: true,  sort: true,  labels: true,  dots: 'arc' },
            { name: 'same, square dots',      n: this.SPELLS,        lines: true,  sort: true,  labels: true,  dots: 'rect' },
            { name: 'stress: 3000 spells',    n: this.STRESS_SPELLS, lines: true,  sort: true,  labels: true,  dots: 'arc' }
        ];
    },

    /** A fake tree: schools as clusters on a sphere, spells scattered round them, links mostly inside a school. */
    _buildScene: function(count) {
        var points = [];
        var i;
        for (i = 0; i < count; i++) {
            var school = i % this.SCHOOLS;
            var theta = (school / this.SCHOOLS) * Math.PI * 2;
            var cx = Math.cos(theta) * this.CLUSTER_RADIUS;
            var cz = Math.sin(theta) * this.CLUSTER_RADIUS;
            var spread = 170;
            points.push({
                x: cx + (Math.random() - 0.5) * 2 * spread,
                y: (Math.random() - 0.5) * 2 * spread * 1.4,
                z: cz + (Math.random() - 0.5) * 2 * spread,
                school: school,
                sx: 0, sy: 0, scale: 0, depth: 0,
                label: 'Spell ' + i
            });
        }

        var links = [];
        var linkCount = Math.floor(count * this.LINK_FACTOR);
        for (i = 0; i < linkCount; i++) {
            var a = Math.floor(Math.random() * count);
            // nine in ten links stay inside the school, the rest cross over
            var b = (Math.random() < 0.9)
                ? (a + this.SCHOOLS * (1 + Math.floor(Math.random() * 12))) % count
                : Math.floor(Math.random() * count);
            links.push([a, b]);
        }
        return { points: points, links: links };
    },

    _project: function(scene, angle, w, h) {
        var cosA = Math.cos(angle), sinA = Math.sin(angle);
        var tilt = 0.35, cosT = Math.cos(tilt), sinT = Math.sin(tilt);
        var pts = scene.points;
        for (var i = 0; i < pts.length; i++) {
            var p = pts[i];
            var x = p.x * cosA - p.z * sinA;
            var z = p.x * sinA + p.z * cosA;
            var y = p.y * cosT - z * sinT;
            z = p.y * sinT + z * cosT;
            var depth = z + this.CAMERA_Z;
            var scale = this.FOCAL / depth;
            p.sx = w / 2 + x * scale;
            p.sy = h / 2 + y * scale;
            p.scale = scale;
            p.depth = depth;
        }
    },

    _draw: function(scene, phase, order, w, h) {
        var ctx = this._ctx;
        var pts = scene.points;
        var i;

        ctx.fillStyle = '#050505';
        ctx.fillRect(0, 0, w, h);

        if (phase.lines) {
            ctx.strokeStyle = 'rgba(200,200,184,0.16)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (i = 0; i < scene.links.length; i++) {
                var a = pts[scene.links[i][0]], b = pts[scene.links[i][1]];
                ctx.moveTo(a.sx, a.sy);
                ctx.lineTo(b.sx, b.sy);
            }
            ctx.stroke();
        }

        for (i = 0; i < order.length; i++) {
            var p = pts[order[i]];
            var r = Math.max(1.2, 6 * p.scale);
            // further away = dimmer, the cue that makes a flat canvas read as depth
            ctx.globalAlpha = Math.max(0.25, Math.min(1, p.scale * 1.1));
            ctx.fillStyle = this.SCHOOL_COLORS[p.school];
            if (phase.dots === 'rect') {
                ctx.fillRect(p.sx - r, p.sy - r, r * 2, r * 2);
            } else {
                ctx.beginPath();
                ctx.arc(p.sx, p.sy, r, 0, 6.2832);
                ctx.fill();
            }
        }
        ctx.globalAlpha = 1;

        if (phase.labels) {
            // nearest spells get the labels, as a real view would do
            ctx.fillStyle = '#e8e8d8';
            ctx.font = '11px sans-serif';
            var shown = Math.min(this.LABELS, order.length);
            for (i = order.length - shown; i < order.length; i++) {
                var lp = pts[order[i]];
                ctx.fillText(lp.label, lp.sx + 8, lp.sy + 3);
            }
        }
    },

    _now: function() {
        return (window.performance && performance.now) ? performance.now() : Date.now();
    },

    _runPhase: function(phase, done) {
        var self = this;
        var scene = this._buildScene(phase.n);
        var order = [];
        for (var k = 0; k < phase.n; k++) order.push(k);

        var w = this._canvas.width, h = this._canvas.height;
        var start = this._now(), last = start;
        var frames = 0, workMs = 0, worstMs = 0, angle = 0;

        function frame() {
            if (!self._running) return;
            var t0 = self._now();

            angle += 0.012;
            self._project(scene, angle, w, h);
            if (phase.sort) {
                order.sort(function(i, j) { return scene.points[j].depth - scene.points[i].depth; });
            }
            self._draw(scene, phase, order, w, h);

            var t1 = self._now();
            workMs += (t1 - t0);
            var gap = t1 - last;
            if (frames > 0 && gap > worstMs) worstMs = gap;
            last = t1;
            frames++;

            self._status(phase.name + '  -  ' + frames + ' frames');

            if (t1 - start < self.PHASE_SECONDS * 1000) {
                window.requestAnimationFrame(frame);
            } else {
                var seconds = (t1 - start) / 1000;
                done({
                    name: phase.name,
                    spells: phase.n,
                    fps: frames / seconds,
                    workMs: workMs / frames,
                    worstMs: worstMs
                });
            }
        }
        window.requestAnimationFrame(frame);
    },

    _status: function(text) {
        var el = document.getElementById('perf3d-status');
        if (el) el.textContent = text;
    },

    _verdict: function(fps) {
        if (fps >= 50) return 'smooth';
        if (fps >= 30) return 'usable';
        if (fps >= 18) return 'choppy';
        return 'too slow';
    },

    _report: function() {
        var lines = ['3D probe  (canvas ' + this._canvas.width + 'x' + this._canvas.height + ')'];
        var html = '<table class="perf3d-table"><tr><th>phase</th><th>spells</th><th>fps</th>' +
                   '<th>script ms/frame</th><th>worst frame ms</th><th></th></tr>';
        for (var i = 0; i < this._results.length; i++) {
            var r = this._results[i];
            var verdict = this._verdict(r.fps);
            html += '<tr><td>' + r.name + '</td><td>' + r.spells + '</td><td>' + r.fps.toFixed(1) + '</td><td>' +
                    r.workMs.toFixed(1) + '</td><td>' + r.worstMs.toFixed(0) + '</td><td>' + verdict + '</td></tr>';
            lines.push(r.name + ' | spells=' + r.spells + ' | fps=' + r.fps.toFixed(1) +
                       ' | scriptMs=' + r.workMs.toFixed(1) + ' | worstMs=' + r.worstMs.toFixed(0) + ' | ' + verdict);
        }
        html += '</table>';
        var out = document.getElementById('perf3d-results');
        if (out) out.innerHTML = html;
        this._status('done - results are also in the SKSE log (search "PERF3D")');

        if (window.callCpp) {
            for (var j = 0; j < lines.length; j++) {
                window.callCpp('LogMessage', JSON.stringify({ level: 'info', message: '[PERF3D] ' + lines[j] }));
            }
        }
        console.log('[PERF3D]\n' + lines.join('\n'));
    },

    start: function() {
        if (this._running) return;
        var host = document.getElementById('contentSpellTree') || document.body;

        var overlay = document.createElement('div');
        overlay.id = 'perf3d-overlay';
        overlay.innerHTML =
            '<canvas id="perf3d-canvas"></canvas>' +
            '<div class="perf3d-hud">' +
                '<span id="perf3d-status">starting...</span>' +
                '<button id="perf3d-close" class="btn btn-secondary">Close</button>' +
            '</div>' +
            '<div id="perf3d-results"></div>';
        host.appendChild(overlay);
        this._overlay = overlay;

        var canvas = document.getElementById('perf3d-canvas');
        canvas.width = Math.max(640, overlay.clientWidth);
        canvas.height = Math.max(400, overlay.clientHeight);
        this._canvas = canvas;
        this._ctx = canvas.getContext('2d');

        var self = this;
        document.getElementById('perf3d-close').addEventListener('click', function() { self.stop(); });

        this._running = true;
        this._results = [];
        var phases = this._phases();
        var index = 0;

        function next() {
            if (!self._running) return;
            if (index >= phases.length) { self._report(); return; }
            self._runPhase(phases[index], function(result) {
                self._results.push(result);
                index++;
                next();
            });
        }
        next();
    },

    stop: function() {
        this._running = false;
        if (this._overlay && this._overlay.parentNode) {
            this._overlay.parentNode.removeChild(this._overlay);
        }
        this._overlay = null;
    }
};

document.addEventListener('DOMContentLoaded', function() {
    var btn = document.getElementById('perf3d-btn');
    if (btn) btn.addEventListener('click', function() { Perf3DTest.start(); });
});
