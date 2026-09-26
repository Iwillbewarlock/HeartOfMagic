/**
 * AnimClock - moving parts keep their speed whatever the frame rate.
 *
 * The globe (rotation, particle lifecycles, orbiting stars, particles on their
 * way down a learning path), the stars' drift and twinkle and the learning
 * pulses move one fixed step per call, tuned at one frame every STEP_MS.
 * Called less often (fewer animation frames, CanvasRenderer.ANIMATION_FRAME_MS)
 * they slowed down; called more often (every frame of a drag) they sped up.
 * steps(key) says how many of those steps are due since that key last asked,
 * the remainder carried over, so the speed is the tuned one at any rate. A
 * gap longer than MAX_STEPS steps (panel closed, a stall) is not caught up.
 *
 * Depends on: nothing
 */

var AnimClock = {

    STEP_MS: 66,        // the frame the per-step speeds were tuned at (15 a second)
    MAX_STEPS: 4,

    _last: {},          // key -> time of its last call
    _carry: {},         // key -> ms not yet used for a step

    /** How many steps `key` takes now (0 or more). The first call takes one. */
    steps: function(key) {
        var now = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
        var last = this._last[key];
        this._last[key] = now;
        if (last === undefined) return 1;
        var t = (this._carry[key] || 0) + Math.min(Math.max(now - last, 0), this.STEP_MS * this.MAX_STEPS);
        var n = Math.floor(t / this.STEP_MS);
        this._carry[key] = t - n * this.STEP_MS;
        return n;
    }
};

window.AnimClock = AnimClock;
