// CHALK KIT · the Heart of Magic "Chalkboard" design. Warm white chalk on a TRANSPARENT sheet:
// the panel paints the slate (#26302c-ish), these plates carry only what the chalk left on it.
//
// THE MEDIUM, PHYSICALLY. A stick of soft calcium chalk dragged over a slate that has a tooth.
// The chalk only reaches the high points of the board, so a stroke is never solid: the grain
// punches it full of holes, crisp ones (the board decides, not the pressure). Press harder and
// the holes close; lighten off, or move fast, and the stick skips whole stretches, so a light
// stroke is broken into runs. Every stroke sheds a haze of dust to either side of itself. A big
// circle is swung on a string from a pivot, in two sweeps because an arm cannot turn a full
// circle, and the string stretches, so the end never lands on the start. Straight lines are
// ruled, firm at the start, lifting at the end. Wide areas are laid with the SIDE of the stick,
// back and forth (a scumble). An eraser never quite gets it all: ghosts stay behind.
//
// ORDER. Ghosts first (they were there before), then the compass work, the ruled lines, the
// freehand marks, the writing, the accent colour last, the way a teacher adds emphasis.
//
// TILES. Two tooth tiles are REGISTERED here under new names (nothing existing is changed): the
// board's fine grain, thresholded steeply so the holes are crisp, and a slow wear pattern, the
// patches of a board polished by years of erasers where chalk takes less.
import { Gfx, TILES, fractal, rng, type Medium, type P } from "./core";
import { blob, fillShape, hatchRuns, inside } from "./gallery";

export const CHALK = "#ece8dc", YELLOW = "#f2e2a0";
export const CHALK_M: Medium = { nib: 1, taper: 0.45, pressure: 0.7, retrace: true, wobble: 1, rough: 1.3 };

TILES.chalkGrain ??= { fx: 0.85, oct: 2, seed: 61, k: -4.2, o: 2.8 };    // the slate's tooth: crisp holes
TILES.chalkWear ??= { fx: 0.045, oct: 3, seed: 67, k: -2.2, o: 1.98 };   // polished patches take less chalk

// ---------------------------------------------------------------- layers
// chalk(): the dust pass (wide, blurred, faint) under the stroke pass (broken by the tooth).
// `bite: "light"` adds the pinholes of a stick barely touching.
export const chalk = (g: Gfx, draw: (dust: boolean) => void, o: { dust?: number; blur?: number; bite?: "firm" | "light"; alpha?: number } = {}) => {
  const { dust = 0.2, blur = 3, bite = "firm", alpha = 1 } = o;
  if (dust > 0) g.group("plain", () => draw(true), { blur, alpha: dust });
  g.group("plain", () => draw(false), { alpha, textures: bite === "firm" ? ["chalkGrain", "chalkWear"] : ["chalkGrain", "risoSpeck", "chalkWear"] });
};
// an old lesson rubbed out with a felt: only a soft, faint ghost of it is left
export const ghost = (g: Gfx, draw: () => void, alpha = 0.08, blur = 2.6) => g.group("plain", draw, { blur, alpha });

// ---------------------------------------------------------------- strokes
// resample by arc length: [point, distance along]
const along = (pts: P[], step: number): { p: P; d: number }[] => {
  const out = [{ p: pts[0], d: 0 }]; let d = 0;
  for (let i = 1; i < pts.length; i++) { const a = pts[i - 1], b = pts[i], l = Math.hypot(b[0] - a[0], b[1] - a[1]), n = Math.max(1, Math.ceil(l / step)); for (let k = 1; k <= n; k++) out.push({ p: [a[0] + ((b[0] - a[0]) * k) / n, a[1] + ((b[1] - a[1]) * k) / n], d: d + (l * k) / n }); d += l; }
  return out;
};
// pressure along a stroke: set down a touch soft, firm through the middle, lifting off at the end
export const lift = (inT = 0.06, outT = 0.2, end = 0.35) => (t: number) => Math.min(1, 0.7 + (0.3 * t) / inT) * (t > 1 - outT ? 1 - ((t - 1 + outT) / outT) * (1 - end) : 1);

export type StrokeOpts = { w: number; seed: number; color?: string; op?: number; press?: (t: number) => number; skip?: number; wob?: number; taper?: number; dustW?: number; fade?: boolean };
// One chalk stroke. Where the pressure is light the stick skips: the line breaks into runs, and
// each run is as wide and as solid as the pressure under it. `fade`: the pressure also takes the
// chalk away, so a line lifting off along a border thins to nothing instead of just breaking.
export const stroke = (g: Gfx, dust: boolean, pts: P[], o: StrokeOpts) => {
  const { w, seed, color = CHALK, op = 0.94, press = () => 1, skip = 0, wob = 0.5, taper = 0.35, dustW = 2.1, fade = false } = o;
  if (pts.length < 2) return;
  const s = along(pts, 2), L = s[s.length - 1].d || 1;
  if (dust) {   // dust settles along the whole pass, skips included
    if (!fade) { g.pen(s.filter((_, i) => i % 5 === 0 || i === s.length - 1).map((q) => q.p), { w: w * dustW, color, seed, wobble: wob, boil: 0, opacity: op, retrace: false, taper: 0.6 }); return; }
    for (let i = 0; i < s.length - 1; i += 10) { const seg = s.slice(i, Math.min(s.length, i + 13)), pr = press(seg[0].d / L); if (seg.length > 1 && pr > 0.02) g.pen(seg.filter((_, k) => k % 4 === 0 || k === seg.length - 1).map((q) => q.p), { w: w * dustW * (0.4 + 0.6 * pr), color, seed: seed + i, wobble: wob, boil: 0, opacity: op * pr, retrace: false, taper: 0 }); }
    return;
  }
  const runs: { p: P[]; pr: number }[] = []; let cur: P[] = [], acc = 0, cnt = 0;
  s.forEach((q, i) => {
    const t = q.d / L, pr = press(t), thr = 0.7 - skip * (1.1 - pr) * 0.34;
    const gap = skip > 0 && fractal(seed + 900, q.d, seed % 53, 0.05, 0.05, 3) > thr;
    if (!gap) { if (!cur.length || i % 4 === 0 || i === s.length - 1) cur.push(q.p); acc += pr; cnt++; }
    if ((gap || i === s.length - 1) && cur.length) { if (cur.length > 1) runs.push({ p: cur, pr: acc / cnt }); cur = []; acc = 0; cnt = 0; }
  });
  runs.forEach((run, k) => {
    const pr = Math.min(1, Math.max(fade ? 0 : 0.2, run.pr));
    if (fade && pr < 0.03) return;
    g.pen(run.p, { w: w * (fade ? 0.35 + 0.65 * pr : 0.5 + 0.5 * pr), color, seed: seed + k * 13, wobble: wob, boil: 0, opacity: op * (fade ? pr : 0.62 + 0.38 * pr), retrace: true, taper });
  });
};

// The string compass: points of a circle swung from a pivot. The string stretches (or the pivot
// creeps) by `drift` of the radius over a full turn, so a turn does not close on itself.
export const compass = (cx: number, cy: number, r: number, a0: number, sweep: number, drift: number, seed: number, squash = 1): P[] => {
  const n = Math.max(12, Math.ceil((Math.abs(sweep) * r) / 5)), ph = rng(seed)() * 6.28, out: P[] = [];
  for (let i = 0; i <= n; i++) { const f = i / n, a = a0 + sweep * f, rr = r * (1 + drift * f + 0.0035 * Math.sin(2 * a + ph)); out.push([cx + Math.cos(a) * rr, cy + Math.sin(a) * rr * squash]); }
  return out;
};
// A compass ring in two sweeps that overlap where they meet (an arm cannot turn a full circle).
export const ring = (g: Gfx, dust: boolean, cx: number, cy: number, r: number, o: StrokeOpts & { a0?: number; drift?: number; sweeps?: 1 | 2; squash?: number }) => {
  const { a0 = -2.2, drift = 0.006, sweeps = 2, squash = 1 } = o, TAU = Math.PI * 2, over = 0.16;
  if (sweeps === 1) { stroke(g, dust, compass(cx, cy, r, a0, TAU + over * 1.6, drift, o.seed, squash), { press: lift(0.05, 0.12, 0.3), ...o }); return; }
  const mid = a0 + TAU * 0.54;
  stroke(g, dust, compass(cx, cy, r, a0, mid - a0 + over, drift * 0.5, o.seed, squash), { press: lift(0.05, 0.14, 0.4), ...o });
  stroke(g, dust, compass(cx, cy, r * (1 + drift * 0.45), mid - over * 0.3, a0 + TAU + over - mid, drift * 0.5, o.seed + 7, squash), { press: lift(0.05, 0.16, 0.3), ...o, seed: o.seed + 7 });
};

// ---------------------------------------------------------------- the side of the stick
// back-and-forth drags clipped to a shape; `keep` thins it where it says so
export const scumble = (g: Gfx, dust: boolean, region: P[], o: { angle: number; gap: number; w: number; alpha: number; seed: number; color?: string; keep?: (x: number, y: number) => boolean }) => {
  const { angle, gap, w, alpha, seed, color = CHALK, keep = () => true } = o, r = rng(seed);
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; region.forEach(([x, y]) => { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); });
  const c = g.cur; g.touch(x0 - 12, y0 - 12, x1 + 12, y1 + 12);
  c.save(); c.lineCap = "round"; c.lineJoin = "round"; c.strokeStyle = color; c.lineWidth = dust ? w * 2.2 : w;
  hatchRuns({ x0, y0, x1, y1 }, angle, gap, (x, y) => inside(region, x, y) && keep(x, y), 3, seed).forEach((run) => {
    c.globalAlpha = alpha * (0.8 + r() * 0.3); c.beginPath();
    run.filter((_, k, a) => k % 2 === 0 || k === a.length - 1).forEach(([x, y], k) => (k ? c.lineTo(x + (r() - 0.5) * 1.4, y + (r() - 0.5) * 1.4) : c.moveTo(x, y)));
    c.stroke();
  });
  c.restore(); c.globalAlpha = 1;
};
// a dot tapped in with the end of the stick
export const dot = (g: Gfx, x: number, y: number, r: number, seed: number, color = CHALK, alpha = 0.92) => fillShape(g, blob(x, y, r, r * (0.85 + (seed % 7) * 0.03), seed, 0.22, 9), color, alpha);
export const circlePts = (cx: number, cy: number, r: number, n = 48, sy = 1): P[] => Array.from({ length: n }, (_, i) => [cx + Math.cos((i / n) * Math.PI * 2) * r, cy + Math.sin((i / n) * Math.PI * 2) * r * sy] as P);
// Every chalk layer composited at its own alpha leaves the white a hair different from pixel to
// pixel (8-bit rounding). Setting the colour of everything drawn so far to one chalk keeps the
// sheet clean and small; call it before the accent colour goes on.
export const settle = (g: Gfx, color = CHALK) => { const m = g.main; m.save(); m.setTransform(1, 0, 0, 1, 0, 0); m.globalCompositeOperation = "source-atop"; m.globalAlpha = 1; m.fillStyle = color; m.fillRect(0, 0, Math.round(g.env.W * g.env.scale), Math.round(g.env.H * g.env.scale)); m.restore(); };
