/**
 * PerfMeter - numbers on screen for "the panel feels slow" (developer mode only)
 *
 * The panel runs in the game's CPU-drawn browser, where timings bear no
 * relation to a desktop browser's, so they have to be read in the game.
 *
 *   draw   how long one frame of the tree takes, and how many are drawn a second
 *   stall  the longest the browser went without running a frame, over the last
 *          few seconds and over the whole session - while it is stalled no
 *          click and no mouse move is looked at
 *   press  the last mouse press: how long it was held, how far the cursor went,
 *          how many moves were delivered, how late the events arrived, and
 *          whether it counted as a click or a drag
 *
 * Reading the press line: a click the tree treats as a drag shows up as a short
 * press whose travel passed the 5px threshold. A travel far larger than the
 * number of moves can account for means the browser dropped the moves in
 * between - press and release landed far apart with nothing reported between
 * them, which is what a stall does to the mouse.
 *
 * One clock only. The render loop is handed a timestamp by requestAnimationFrame
 * and older engines do not draw that from the same origin as performance.now();
 * mixing the two made every recorded frame look a thousand years old, so the
 * draw line sat at "0.0 ms x0/s" while the tree was drawing perfectly well.
 * Nothing here takes a time from its caller.
 *
 * Costs nothing when developer mode is off: every entry point returns at once,
 * and nothing is allocated per frame.
 *
 * Depends on: settings (developerMode), t()
 */
var PerfMeter = {

    DRAW_WINDOW_MS: 1000,     // the draw line covers the last second
    STALL_WINDOW_MS: 5000,    // a one-off stall stays readable this long
    REFRESH_MS: 250,          // how often the text is rewritten
    SLOW_FRAME_MS: 33,        // a frame this long cannot keep 30 a second
    BAD_STALL_MS: 100,        // a gap this long is felt as a hitch
    // Past this, the loop was not running at all rather than stalling - the
    // panel was shut, or the engine stopped calling for frames. Recording it
    // would put minutes into the session maximum and leave the read-out
    // permanently red, which is worse than missing a freeze this long.
    MAX_CREDIBLE_GAP_MS: 2000,

    _el: null,
    _shown: '',               // what the element already says
    // Parallel arrays of numbers, not objects: this is written once per drawn
    // frame, and a measurement tool must not be the thing making the garbage
    // it is measuring.
    _frameAt: [],
    _frameMs: [],
    _gapAt: [],
    _gapMs: [],
    _worstEver: 0,
    _lastTick: 0,
    _lastRefresh: 0,
    _press: null,
    _lastPress: null,

    _now: function () {
        return (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
    },

    isOn: function () {
        return typeof settings !== 'undefined' && settings.developerMode === true;
    },

    // =========================================================================
    // HOOKS - called by CanvasRenderer
    // =========================================================================

    /** Every turn of the render loop, drawn or not. Takes no time from its caller. */
    tick: function () {
        if (!this.isOn()) { this._lastTick = 0; return; }
        var now = this._now();
        if (this._lastTick) {
            var gap = now - this._lastTick;
            if (gap <= this.MAX_CREDIBLE_GAP_MS) {
                this._gapAt.push(now);
                this._gapMs.push(gap);
                if (gap > this._worstEver) this._worstEver = gap;
                this._trim(this._gapAt, this._gapMs, now, this.STALL_WINDOW_MS);
            }
        }
        this._lastTick = now;
        if (now - this._lastRefresh >= this.REFRESH_MS) this._refresh(now);
    },

    /** One frame of the tree was drawn and took `ms`. */
    frame: function (ms) {
        if (!this.isOn()) return;
        var now = this._now();
        this._frameAt.push(now);
        this._frameMs.push(ms);
        this._trim(this._frameAt, this._frameMs, now, this.DRAW_WINDOW_MS);
    },

    press: function (e) {
        if (!this.isOn()) return;
        this._press = { at: this._now(), x: e.clientX, y: e.clientY, travel: 0, moves: 0, lag: this._lagOf(e) };
    },

    move: function (e) {
        var p = this._press;
        if (!p) return;
        p.moves++;
        this._reach(p, e);
    },

    /** @param {boolean} wasDrag - what the renderer decided the press was */
    release: function (e, wasDrag) {
        var p = this._press;
        if (!p) return;
        this._press = null;
        // Where the release landed counts too. Without it a press whose moves
        // were all dropped reads as "0 px", hiding the very thing being hunted.
        this._reach(p, e);
        var lag = this._lagOf(e);
        this._lastPress = {
            heldMs: Math.round(this._now() - p.at),
            travelPx: Math.round(p.travel),
            moves: p.moves,
            lagMs: Math.round(Math.max(p.lag, lag)),
            wasDrag: !!wasDrag
        };
        this._refresh(this._now());
    },

    /**
     * The render loop stopped, so the coming gap is the panel being shut, not
     * a stall. Without this the first frame after reopening reported the whole
     * time the panel was closed.
     */
    pause: function () {
        this._lastTick = 0;
    },

    /** Developer mode was switched: show or drop the read-out. */
    syncVisibility: function () {
        if (this.isOn()) { this._lastRefresh = 0; return; }
        if (this._el && this._el.parentNode) this._el.parentNode.removeChild(this._el);
        this._el = null;
        this._shown = '';
        this._frameAt = []; this._frameMs = [];
        this._gapAt = []; this._gapMs = [];
        this._worstEver = 0;
        this._lastTick = 0;
        this._press = null;
        this._lastPress = null;
    },

    // =========================================================================
    // INTERNALS
    // =========================================================================

    /** How far this event is from where the press started, and how late it was. */
    _reach: function (p, e) {
        if (!e || typeof e.clientX !== 'number') return;
        var dx = e.clientX - p.x, dy = e.clientY - p.y;
        var far = Math.sqrt(dx * dx + dy * dy);
        if (far > p.travel) p.travel = far;
        var lag = this._lagOf(e);
        if (lag > p.lag) p.lag = lag;
    },

    _trim: function (times, values, now, windowMs) {
        while (times.length && now - times[0] > windowMs) { times.shift(); values.shift(); }
    },

    /**
     * How long ago the event happened by the time it is handled. Old engines
     * stamp events with the wall clock, new ones with the page clock; anything
     * that does not come out as a sane span is reported as nothing, rather than
     * as a number that would be believed.
     */
    _lagOf: function (e) {
        if (!e || typeof e.timeStamp !== 'number' || e.timeStamp <= 0) return 0;
        var lag = (e.timeStamp > 1e12) ? (Date.now() - e.timeStamp) : (this._now() - e.timeStamp);
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
        this._shown = '';
        return el;
    },

    _refresh: function (now) {
        this._lastRefresh = now;
        var el = this._ensureElement();
        if (!el) return;

        this._trim(this._frameAt, this._frameMs, now, this.DRAW_WINDOW_MS);
        this._trim(this._gapAt, this._gapMs, now, this.STALL_WINDOW_MS);

        var i, total = 0, worstFrame = 0;
        for (i = 0; i < this._frameMs.length; i++) {
            total += this._frameMs[i];
            if (this._frameMs[i] > worstFrame) worstFrame = this._frameMs[i];
        }
        var avg = this._frameMs.length ? total / this._frameMs.length : 0;

        var worstGap = 0;
        for (i = 0; i < this._gapMs.length; i++) {
            if (this._gapMs[i] > worstGap) worstGap = this._gapMs[i];
        }

        var lines = [];
        lines.push(this._label('perf.draw', 'draw') + '  ' + avg.toFixed(1) + ' ms  (' +
            this._label('perf.worst', 'worst') + ' ' + worstFrame.toFixed(0) + ')  x' +
            this._frameMs.length + '/s');
        lines.push(this._label('perf.stall', 'stall') + '  ' + Math.round(worstGap) + ' ms  (' +
            this._label('perf.max', 'max') + ' ' + Math.round(this._worstEver) + ')');
        var p = this._lastPress;
        if (p) {
            lines.push(this._label('perf.press', 'press') + '  ' + p.heldMs + ' ms, ' +
                p.travelPx + ' px, ' + p.moves + ' ' + this._label('perf.moves', 'moves') + ', ' +
                this._label('perf.lag', 'late') + ' ' + p.lagMs + ' ms  >  ' +
                (p.wasDrag ? this._label('perf.drag', 'DRAG') : this._label('perf.click', 'click')));
        }

        // Writing the same text again costs a layout for nothing
        var text = lines.join('\n');
        if (text !== this._shown) {
            el.textContent = text;
            this._shown = text;
        }
        var slow = (avg >= this.SLOW_FRAME_MS || worstGap >= this.BAD_STALL_MS) ? 'slow' : '';
        if (el.className !== slow) el.className = slow;
    }
};
