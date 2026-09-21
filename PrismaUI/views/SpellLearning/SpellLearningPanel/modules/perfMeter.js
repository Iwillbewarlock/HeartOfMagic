/**
 * PerfMeter - numbers on screen for "the panel feels slow" (developer mode only)
 *
 * The panel runs in the game's CPU-drawn browser, where timings bear no
 * relation to a desktop browser's, so they have to be read in the game. Three
 * lines in the corner of the tree:
 *
 *   draw   how long one frame of the tree takes, and how many are drawn a second
 *   stall  the longest the browser went without running a frame - while it is
 *          stalled no click or mouse move is looked at
 *   press  the last mouse press: how long it was held, how far the cursor went
 *          while held, how late the events arrived, and whether it counted as a
 *          click or a drag. A click that reads as a drag shows up here as a
 *          short press with a long travel or a large delay.
 *
 * Costs nothing when developer mode is off: every entry point returns at once.
 *
 * Depends on: settings (developerMode), t()
 */
var PerfMeter = {

    WINDOW_MS: 1000,        // draw and stall figures cover the last second
    REFRESH_MS: 250,        // how often the text is rewritten
    SLOW_FRAME_MS: 33,      // a frame this long cannot keep 30 a second

    _el: null,
    _frames: [],            // {at, ms} of the last second
    _lastTick: 0,
    _worstGap: 0,
    _lastRefresh: 0,
    _press: null,
    _lastPress: null,

    isOn: function () {
        return typeof settings !== 'undefined' && settings.developerMode === true;
    },

    // =========================================================================
    // HOOKS - called by CanvasRenderer
    // =========================================================================

    /** Every turn of the render loop, drawn or not. */
    tick: function (now) {
        if (!this.isOn()) { this._lastTick = 0; return; }
        if (this._lastTick) {
            var gap = now - this._lastTick;
            if (gap > this._worstGap) this._worstGap = gap;
        }
        this._lastTick = now;
        if (now - this._lastRefresh >= this.REFRESH_MS) this._refresh(now);
    },

    /** One frame of the tree was drawn and took `ms`. */
    frame: function (ms) {
        if (!this.isOn()) return;
        var now = performance.now();
        this._frames.push({ at: now, ms: ms });
        while (this._frames.length && now - this._frames[0].at > this.WINDOW_MS) this._frames.shift();
    },

    press: function (e) {
        if (!this.isOn()) return;
        this._press = { at: performance.now(), x: e.clientX, y: e.clientY, travel: 0, moves: 0, lag: this._lagOf(e) };
    },

    move: function (e) {
        var p = this._press;
        if (!p) return;
        var dx = e.clientX - p.x, dy = e.clientY - p.y;
        var far = Math.sqrt(dx * dx + dy * dy);
        if (far > p.travel) p.travel = far;
        p.moves++;
        var lag = this._lagOf(e);
        if (lag > p.lag) p.lag = lag;
    },

    /** @param {boolean} wasDrag - what the renderer decided the press was */
    release: function (e, wasDrag) {
        var p = this._press;
        if (!p) return;
        this._press = null;
        var lag = this._lagOf(e);
        this._lastPress = {
            heldMs: Math.round(performance.now() - p.at),
            travelPx: Math.round(p.travel),
            moves: p.moves,
            lagMs: Math.round(Math.max(p.lag, lag)),
            wasDrag: !!wasDrag
        };
        this._refresh(performance.now());
    },

    // =========================================================================
    // INTERNALS
    // =========================================================================

    /**
     * How long ago the event happened by the time it is handled. Old engines
     * stamp events with the wall clock, new ones with the page clock.
     */
    _lagOf: function (e) {
        if (!e || typeof e.timeStamp !== 'number' || e.timeStamp <= 0) return 0;
        var lag = (e.timeStamp > 1e12) ? (Date.now() - e.timeStamp) : (performance.now() - e.timeStamp);
        return (lag >= 0 && lag < 60000) ? lag : 0;
    },

    _label: function (key, fallback) {
        if (typeof t !== 'function') return fallback;
        var text = t(key);
        return text === key ? fallback : text;
    },

    _ensureElement: function () {
        if (this._el) return this._el;
        var host = document.getElementById('tree-container');
        if (!host) return null;
        var el = document.createElement('div');
        el.id = 'perf-meter';
        host.appendChild(el);
        this._el = el;
        return el;
    },

    _refresh: function (now) {
        this._lastRefresh = now;
        var el = this._ensureElement();
        if (!el) return;

        var frames = this._frames;
        while (frames.length && now - frames[0].at > this.WINDOW_MS) frames.shift();
        var total = 0, worst = 0;
        for (var i = 0; i < frames.length; i++) {
            total += frames[i].ms;
            if (frames[i].ms > worst) worst = frames[i].ms;
        }
        var avg = frames.length ? total / frames.length : 0;

        var lines = [];
        lines.push(this._label('perf.draw', 'draw') + '  ' + avg.toFixed(1) + ' ms  (' +
            this._label('perf.worst', 'worst') + ' ' + worst.toFixed(0) + ')  x' + frames.length + '/s');
        lines.push(this._label('perf.stall', 'stall') + '  ' + Math.round(this._worstGap) + ' ms');
        var p = this._lastPress;
        if (p) {
            lines.push(this._label('perf.press', 'press') + '  ' + p.heldMs + ' ms, ' + p.travelPx + ' px, ' +
                this._label('perf.lag', 'late') + ' ' + p.lagMs + ' ms  >  ' +
                (p.wasDrag ? this._label('perf.drag', 'DRAG') : this._label('perf.click', 'click')));
        }
        el.textContent = lines.join('\n');
        el.className = (avg >= this.SLOW_FRAME_MS || this._worstGap >= 100) ? 'slow' : '';
        this._worstGap = 0;
    },

    /** Developer mode was switched: show or remove the read-out. */
    syncVisibility: function () {
        if (this.isOn()) return;
        if (this._el && this._el.parentNode) this._el.parentNode.removeChild(this._el);
        this._el = null;
        this._frames = [];
        this._lastPress = null;
    }
};
