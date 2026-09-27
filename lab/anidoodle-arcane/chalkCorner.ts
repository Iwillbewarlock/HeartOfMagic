// CHALK CORNER · the top-left corner of the Heart of Magic "Chalkboard" board (the panel mirrors it
// into the other three). 200 x 200 native, transparent: the panel paints the slate.
//
// SUBJECT & REFERENCE. What a teacher puts in the corner of a board ruled up for a term: a double
// border rule, the outer one firm, the inner one lighter, both run out along the edges from a
// small emblem and lifted off as the arm tires, so they thin and break and are gone before the
// middle of the edge. The emblem is the design's motif, a four-point star over a small freehand
// ring, each point outlined, one half laid in solid with the side of the stick and the other
// half lightly (the compass-rose convention, the solid halves turning the same way round). Under each rule a loose
// flourish ending in a curl, and on the diagonal a small constellation, dotted and joined in a
// light hand, its brightest star the one touch of yellow. The middle stays empty: the spell tree is
// drawn there.
//
// The top edge is drawn and the left edge is the same marks reflected in the diagonal, with their
// own seeds, so the hand is never quite the same twice.
import { Gfx, rng, type Ctx, type Env, type P } from "./core";
import type { Film } from "./film";
import { CHALK, CHALK_M, YELLOW, chalk, dot, lift, ring, scumble, settle, stroke } from "./chalkKit";

const C: P = [31, 31];
const flip = (pts: P[]): P[] => pts.map(([x, y]) => [y, x]);
const both = (pts: P[], draw: (pts: P[], left: number) => void) => { draw(pts, 0); draw(flip(pts), 1); };
// pressure that holds, then eases off to nothing: an arm running a rule out along the edge
const runOut = (hold: number) => (t: number) => (t < hold ? 1 : Math.max(0, 1 - Math.pow((t - hold) / (1 - hold), 0.8)));

// a flourish under the rule: a slow wave that turns into a curl
const flourish = (): P[] => {
  const pts: P[] = [];
  for (let i = 0; i <= 14; i++) { const u = i / 14; pts.push([51 + 70 * u, 26 + 3.2 * Math.sin(u * Math.PI * 1.7 + 0.3)]); }
  const [ex, ey] = pts[pts.length - 1], cx = ex + 1, cy = ey + 6.5;
  for (let i = 1; i <= 16; i++) { const f = i / 16, a = -Math.PI / 2 + f * Math.PI * 2.1, r = 6.5 * (1 - 0.62 * f); pts.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]); }
  return pts;
};

// the star: four points on the axes, waists on the diagonals
const TIP = 25, WAIST = 7.5;
const polar = (a: number, r: number): P => [C[0] + Math.cos(a) * r, C[1] + Math.sin(a) * r];
const pointOf = (k: number) => { const a = (k * Math.PI) / 2 - Math.PI / 2, h = Math.PI / 4; return { t: polar(a, TIP), l: polar(a - h, WAIST), r: polar(a + h, WAIST) }; };

// the constellation on the diagonal; A is the bright one
const STARS: [number, number, number][] = [[62, 60, 2.2], [80, 47, 1.7], [93, 66, 1.5], [75, 83, 1.8], [47, 92, 1.2], [104, 42, 1.1]];
const JOIN: [number, number][] = [[0, 1], [1, 2], [2, 3], [3, 0]];

const drawCorner = (ctx: Ctx, _frame: number, env: Env) => {
  const g = new Gfx(ctx, env, 0, CHALK_M);

  // ---- the double rule, run out along both edges
  chalk(g, (dust) => {
    both([[52, 10.6], [120, 10.9], [197, 11.8]], (p, l) => stroke(g, dust, p, { w: 3.2, seed: 11 + l * 100, press: runOut(0.4), fade: true, skip: 0.8, wob: 0.3 }));
  }, { dust: 0.2, blur: 1.8 });
  chalk(g, (dust) => {
    both([[53, 17.2], [100, 17.4], [152, 18.2]], (p, l) => stroke(g, dust, p, { w: 2, seed: 13 + l * 100, op: 0.85, press: runOut(0.25), fade: true, skip: 1, wob: 0.3 }));
  }, { dust: 0.14, blur: 1.6, bite: "light" });

  // ---- the emblem: a freehand ring, the star outlined, half of each point laid in
  chalk(g, (dust) => {
    ring(g, dust, C[0], C[1], 16, { w: 2.3, seed: 21, a0: -2.6, drift: 0.07, sweeps: 1, wob: 0.5 });
    for (let k = 0; k < 4; k++) {
      const { t, l, r } = pointOf(k);
      if (!dust) { scumble(g, false, [t, r, C], { angle: 0.9 + k * 0.5, gap: 1.4, w: 2, alpha: 0.95, seed: 40 + k }); scumble(g, false, [l, t, C], { angle: -0.6 + k * 0.5, gap: 2.6, w: 1.6, alpha: 0.4, seed: 44 + k }); }
      stroke(g, dust, [l, t], { w: 2.2, seed: 30 + k * 2, press: lift(0.1, 0.2, 0.6), wob: 0.2, taper: 0.3 });
      stroke(g, dust, [t, r], { w: 2.2, seed: 31 + k * 2, press: lift(0.1, 0.2, 0.6), wob: 0.2, taper: 0.3 });
    }
    if (!dust) dot(g, C[0], C[1], 2.6, 49);   // the pivot, where the points meet
  }, { dust: 0.2, blur: 1.6 });

  // ---- the flourishes under the rules
  chalk(g, (dust) => {
    both(flourish(), (p, l) => stroke(g, dust, p, { w: 2.2, seed: 61 + l * 100, op: 0.88, press: (t) => (t < 0.6 ? 1 : 1 - (t - 0.6) * 1.1), skip: 0.5, wob: 0.6, taper: 0.6 }));
  }, { dust: 0.16, blur: 1.6 });

  // ---- the constellation: joined first in a light hand, then the stars dotted over the joins
  chalk(g, (dust) => {
    JOIN.forEach(([i, j], k) => {
      const [ax, ay] = STARS[i], [bx, by] = STARS[j], l = Math.hypot(bx - ax, by - ay), ux = (bx - ax) / l, uy = (by - ay) / l, m = 4.5;
      stroke(g, dust, [[ax + ux * m, ay + uy * m], [bx - ux * m, by - uy * m]], { w: 1.5, seed: 70 + k, op: 0.75, press: lift(0.1, 0.3, 0.5), skip: 0.8, wob: 0.3 });
    });
  }, { dust: 0.1, blur: 1.4, bite: "light" });
  g.group("plain", () => { const r = rng(80); STARS.slice(1).forEach(([x, y, s], i) => dot(g, x, y, s * 1.2, 81 + i, CHALK, 0.8 + r() * 0.15)); }, { textures: ["chalkGrain"] });
  settle(g, CHALK);

  // ---- the bright one, in yellow: a tapped dot and a tiny four-ray sparkle
  const [sx, sy, sr] = STARS[0];
  chalk(g, (dust) => {
    if (!dust) dot(g, sx, sy, sr * 1.2, 90, YELLOW, 0.95);
    stroke(g, dust, [[sx, sy - 7], [sx, sy + 7]], { w: 1.6, seed: 91, color: YELLOW, press: lift(0.2, 0.4, 0.3), wob: 0.1, taper: 0.9 });
    stroke(g, dust, [[sx - 5.5, sy], [sx + 5.5, sy]], { w: 1.5, seed: 92, color: YELLOW, press: lift(0.2, 0.4, 0.3), wob: 0.1, taper: 0.9 });
  }, { dust: 0.3, blur: 1.8 });
};

export const chalkCorner: Film = {
  meta: { title: "Chalk corner", W: 200, H: 200, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id: "chalkCorner", start: 0, end: 1, draw: drawCorner }],
};
