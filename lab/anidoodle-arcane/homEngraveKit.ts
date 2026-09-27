import type { Ctx, Env, P } from "./core";
import type { Film } from "./film";

// HOM ENGRAVE KIT · Claude Mellan's single-line engraving (see mellan.ts) for any tone field.
// ONE Archimedean spiral, born at a chosen point (the picture's light or focal point), walked out
// until it has left every corner; its width at each step is the only variable. `tone(W, H)` is a
// raster of INK: 0 = no line (the sheet shows through), 1 = the widest line. For gold on the dark
// panel that is light; for sepia on parchment it is shadow. The sheet is always left transparent:
// no paper fill, no g.paper() grain (formats.md), so the plate drops onto the panel's own colour.
//
// The engraver's reading (why a raw tone is not printed as is):
//  - local contrast at two scales cuts edges and thin rows harder than the field has them, and
//    never adds ink where the field has none (t - blur(t) <= 0 wherever t = 0);
//  - each line's tone is half its band's mean and half its band's extreme sample (the lowest for
//    gold-on-dark, where detail is less light; the highest for sepia-on-paper, where detail is more
//    shadow), so a hair of detail between turns changes the line instead of being averaged away.
// One ink, one plate: nothing is ever printed in a second colour.

export type ToneFn = (W: number, H: number) => Float32Array;
export type EngraveOpts = {
  ink: string;
  pitch?: number;                    // distance between neighbouring turns, logical px (default 4.6)
  origin?: [number, number];         // where the spiral is born, fraction of W, H (default the centre)
  gamma?: number;                    // ink -> width exponent (default 1.25)
  sharp?: [number, number][];        // [box radius px, gain] local-contrast passes (default [[4,1.2],[20,0.5]])
  detail?: "min" | "max";            // which extreme of a line's band is the fine detail worth keeping: "min" when the
                                     // detail is LESS ink (dark rows on a gold-lit page), "max" when it is MORE ink
                                     // (sepia rows on a pale leaf). Default "min".
};
type Ribbon = P[][];                 // one closed outline per inked run of the spiral

const FILL = 0.9;                    // widest line as a fraction of the pitch: turns never quite merge
const STEP = 1.4;                    // arc length per sample, px
const WMIN = 0.015;                  // half-widths below this print nothing: the burin has left the plate
const BAND = [-0.4, -0.2, 0, 0.2, 0.4];
const MINW = 0.5;

// A box blur, radius r, separable running sums. Pure.
const blur = (src: Float32Array, W: number, H: number, r: number): Float32Array => {
  const tmp = new Float32Array(W * H), out = new Float32Array(W * H), n = 2 * r + 1;
  for (let y = 0; y < H; y++) { let acc = 0; for (let k = -r; k <= r; k++) acc += src[y * W + Math.min(W - 1, Math.max(0, k))]; for (let x = 0; x < W; x++) { tmp[y * W + x] = acc / n; acc += src[y * W + Math.min(W - 1, x + r + 1)] - src[y * W + Math.max(0, x - r)]; } }
  for (let x = 0; x < W; x++) { let acc = 0; for (let k = -r; k <= r; k++) acc += tmp[Math.min(H - 1, Math.max(0, k)) * W + x]; for (let y = 0; y < H; y++) { out[y * W + x] = acc / n; acc += tmp[Math.min(H - 1, y + r + 1) * W + x] - tmp[Math.max(0, y - r) * W + x]; } }
  return out;
};

const cutTone = (t: Float32Array, W: number, H: number, sharp: [number, number][]): Float32Array => {
  const bl = sharp.map(([r]) => blur(t, W, H, r)), c = new Float32Array(W * H);
  for (let i = 0; i < c.length; i++) { let v = t[i]; sharp.forEach(([, g], j) => { v += g * (t[i] - bl[j][i]); }); c[i] = Math.min(1, Math.max(0, v)); }
  return c;
};

// bilinear sample; outside the sheet there is no ink
const sampler = (t: Float32Array, W: number, H: number) => (x: number, y: number): number => {
  if (x < 0 || y < 0 || x > W - 1 || y > H - 1) return 0;
  const x0 = Math.floor(x), y0 = Math.floor(y), fx = x - x0, fy = y - y0, x1 = Math.min(x0 + 1, W - 1), y1 = Math.min(y0 + 1, H - 1);
  const a = t[y0 * W + x0], b = t[y0 * W + x1], c = t[y1 * W + x0], d = t[y1 * W + x1];
  return a + (b - a) * fx + (c + (d - c) * fx - (a + (b - a) * fx)) * fy;
};

// Walk r = a*theta out from C in ~STEP px arcs. Consecutive steps with ink form one run; a run is
// a closed ribbon (left edge out, right edge back) that swells out of a point at both ends.
const walk = (tone: (x: number, y: number) => number, W: number, H: number, C: P, pitch: number, gamma: number, useMax: boolean): Ribbon => {
  const a = pitch / (Math.PI * 2);
  const rMax = Math.max(Math.hypot(C[0], C[1]), Math.hypot(W - C[0], C[1]), Math.hypot(C[0], H - C[1]), Math.hypot(W - C[0], H - C[1])) + pitch;
  const runs: Ribbon = [];
  let L: P[] = [], R: P[] = [], th = 0.6, prev: P = [C[0], C[1]];
  const close = () => { if (L.length > 1) runs.push([...L, ...R.reverse()]); L = []; R = []; };
  while (a * th < rMax) {
    const r = a * th, c = Math.cos(th), s = Math.sin(th), x = C[0] + c * r, y = C[1] + s * r;
    const tx = -s * r + c * a, ty = c * r + s * a, tl = Math.hypot(tx, ty), nx = ty / tl, ny = -tx / tl;   // outward normal
    let mean = 0, lo = 1, hi = 0;
    for (const o of BAND) { const t = tone(x + nx * o * pitch, y + ny * o * pitch); mean += t; lo = Math.min(lo, t); hi = Math.max(hi, t); }
    const v = (1 - MINW) * (mean / BAND.length) + MINW * (useMax ? hi : lo);
    const w = (pitch * FILL * Math.pow(Math.min(1, Math.max(0, v)), gamma)) / 2;
    if (w > WMIN) {
      if (!L.length) { L.push(prev); R.push(prev); }
      L.push([x + nx * w, y + ny * w]); R.push([x - nx * w, y - ny * w]);
    } else if (L.length) { L.push([x, y]); R.push([x, y]); close(); }
    prev = [x, y];
    th += STEP / Math.max(r, STEP);
  }
  close();
  return runs;
};

// The ribbon for one plate. `id` names the tone function in the cache key; every other input the
// geometry depends on is spelled out in it too.
export const engraveRibbon = (id: string, W: number, H: number, tone: ToneFn, o: EngraveOpts, cache?: Map<string, unknown>): Ribbon => {
  const pitch = o.pitch ?? 4.6, origin = o.origin ?? [0.5, 0.5], gamma = o.gamma ?? 1.25, sharp = o.sharp ?? [[4, 1.2], [20, 0.5]], detail = o.detail ?? "min";
  const key = `engrave:${id}:${W}x${H}:${pitch}:${origin.join(",")}:${gamma}:${sharp.join("|")}:${detail}:${FILL}:${STEP}:${WMIN}:${MINW}:${BAND.join(",")}`;
  let rb = cache?.get(key) as Ribbon | undefined;
  if (!rb) { rb = walk(sampler(cutTone(tone(W, H), W, H, sharp), W, H), W, H, [origin[0] * W, origin[1] * H], pitch, gamma, detail === "max"); cache?.set(key, rb); }
  return rb;
};

export const printRibbon = (ctx: Ctx, env: Env, rb: Ribbon, ink: string) => {
  ctx.setTransform(env.scale, 0, 0, env.scale, 0, 0);
  ctx.fillStyle = ink;
  ctx.beginPath();
  for (const run of rb) run.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
  ctx.fill();
};

export const engraveStill = (id: string, title: string, W: number, H: number, tone: ToneFn, o: EngraveOpts, shotId = "plate"): Film => ({
  meta: { title, W, H, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id: shotId, start: 0, end: 1, draw: (ctx, f, env) => { void f; printRibbon(ctx, env, engraveRibbon(id, W, H, tone, o, env.cache), o.ink); } }],
});
