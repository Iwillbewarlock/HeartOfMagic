/**
 * PerfExperiment - what holds the game's browser at 35-60 ms a loop turn?
 *
 * In game our drawing is 2-3% of the time and a stilled panel still turns
 * its loop only every 34-37 ms. From the log alone that could be the game's
 * own frame (PrismaUI runs the page once per game frame), the loop always
 * keeping an animation frame booked, or the page's CSS (shadows, gradients,
 * filters) painted on the CPU. Only the game can tell them apart, so in
 * developer mode this switches between modes by itself, one [Perf] line
 * (PerfMeter.LOG_MS) at a time, and each line says which (", exp <mode>"):
 *
 *   base   as shipped;
 *   timer  a loop turn with nothing to draw books the next with setTimeout
 *          instead of requestAnimationFrame (CanvasRenderer loop);
 *   over   body.perf-over: the same, only for what lies over the tree canvas
 *          (zoom bar, footer, card, tooltip, tabs; patch-ui.css);
 *   flat   body.perf-flat: no shadows, filters or background images anywhere
 *          on the page (patch-ui.css) - the panel looks plain meanwhile.
 *
 * First run (2026-09-26, Candlelit Tome): flat 12 frames a second, 38 ms
 * turns, 66 ms worst stall; base 6, 58 ms, 157 ms; timer 9, 40 ms, 147 ms.
 *
 * ROUNDS rounds of LINES_PER_MODE lines per mode, then it stays on base and
 * says so in the log. Developer mode only; nothing happens otherwise.
 * Remove once the cause is known.
 *
 * Depends on: settings (developerMode), PerfMeter (calls onLog)
 */

var PerfExperiment = {

    MODES: ['base', 'timer', 'over', 'flat'],
    LINES_PER_MODE: 2,      // the first line after a switch is a mix; the second is clean
    ROUNDS: 3,

    mode: null,             // null: not running
    _index: 0,
    _lines: 0,
    _done: false,

    /** Should the render loop book its idle turns with a timer? */
    timerLoop: function() {
        return this.mode === 'timer';
    },

    /** PerfMeter wrote a line: count it, and change mode when due. */
    onLog: function() {
        if (this._done || typeof settings === 'undefined' || settings.developerMode !== true) return;
        if (this.mode === null) {
            this._set(0);
            console.warn('[PerfExperiment] started: ' + this.MODES.join(', ') + ', ' +
                this.LINES_PER_MODE + ' lines each, ' + this.ROUNDS + ' rounds');
            return;
        }
        if (++this._lines < this.LINES_PER_MODE) return;
        var next = this._index + 1;
        if (next >= this.MODES.length * this.ROUNDS) {
            this._set(0);
            this._done = true;
            this.mode = null;
            console.warn('[PerfExperiment] done');
            return;
        }
        this._set(next);
    },

    _set: function(index) {
        this._index = index;
        this._lines = 0;
        this.mode = this.MODES[index % this.MODES.length];
        if (document.body) {
            if (this.mode === 'flat') document.body.classList.add('perf-flat');
            else document.body.classList.remove('perf-flat');
            if (this.mode === 'over') document.body.classList.add('perf-over');
            else document.body.classList.remove('perf-over');
        }
    }
};

window.PerfExperiment = PerfExperiment;
