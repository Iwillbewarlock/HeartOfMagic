import { rng, type Ctx, type Env } from "./core";
import type { Film } from "./film";
import { spellbookTone } from "./spellbookTone";

// THE RISING STAR · pen-and-ink stipple, for Heart of Magic's "Arcane" design (the empty
// spell-tree screen). One technical pen, one dot size, touched straight down thousands of times;
// no line, no hatch, no wash. Printed in gold on a TRANSPARENT sheet: it sits on the panel's dark
// brown, so a dot is light, and tone is the number of gold dots per square centimetre.
//
// SUBJECT & REFERENCE. See spellbookTone.ts: an open hardbound grimoire seen from the front and a
// little above, two page blocks curling up to a sewn gutter, fore-edges striped at the bottom,
// the boards a margin wider than the pages; a four-point star lifting out of the gutter is the only
// light. The tone field is shared with the engraving plate (homEngrave).
//
// ORDER. As the nautilus plate: the brightest masses first (the star, the lit curls of the
// leaves), then the page mid-tones, sparse dots last in the shadowed boards. (Only matters for a
// time-lapse; the still shows the finished sheet.)

const GOLD = "#d9b56e", STAR_GOLD = "#f2dca2";
const DOT = 1.05;                                             // the nib's radius in px at 960x640

type Stip = { shape: Float32Array; alpha: Uint8Array; hot: Uint8Array; n: number };
const V = 7;

const build = (env: Env): Stip => {
  const key = `homStipple:${env.W}x${env.H}`; let S = env.cache.get(key) as Stip | undefined; if (S) return S;
  const W = env.W, H = env.H, T = spellbookTone(W, H).raster;
  const at = (x: number, y: number) => (x < 0 || y < 0 || x >= W || y >= H ? 0 : T[(y | 0) * W + (x | 0)]);
  const MIN_T = 0.03, spacing = (t: number) => 1.55 / Math.pow(Math.max(t, MIN_T), 0.64);
  const cell = 1.35, gw = Math.ceil(W / cell), gh = Math.ceil(H / cell), grid = new Int32Array(gw * gh).fill(-1);
  const xs: number[] = [], ys: number[] = [], rs: number[] = [], r = rng(20260927);
  const ok = (x: number, y: number, d: number) => {
    const reach = Math.ceil((d * 1.25) / cell), gx = Math.floor(x / cell), gy = Math.floor(y / cell);
    for (let j = Math.max(0, gy - reach); j <= Math.min(gh - 1, gy + reach); j++) for (let i = Math.max(0, gx - reach); i <= Math.min(gw - 1, gx + reach); i++) {
      const k = grid[j * gw + i]; if (k < 0) continue; const dx = xs[k] - x, dy = ys[k] - y, m = 0.5 * (d + rs[k]); if (dx * dx + dy * dy < m * m) return false;
    }
    return true;
  };
  const add = (x: number, y: number, d: number) => { const k = xs.length; xs.push(x); ys.push(y); rs.push(d); grid[Math.floor(y / cell) * gw + Math.floor(x / cell)] = k; return k; };
  const grow = (k0: number) => {
    const active = [k0];
    while (active.length) {
      const ai = Math.floor(r() * active.length), k = active[ai]; let found = false;
      for (let n = 0; n < 18; n++) {
        const a = r() * Math.PI * 2, rr = rs[k] * (1 + r()), x = xs[k] + Math.cos(a) * rr, y = ys[k] + Math.sin(a) * rr;
        if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1) continue; const t = at(x, y); if (t < MIN_T) continue;
        const d = spacing(t); if (!ok(x, y, d)) continue; active.push(add(x, y, d)); found = true;
      }
      if (!found) { active[ai] = active[active.length - 1]; active.pop(); }
    }
  };
  for (let y = 3; y < H; y += 6) for (let x = 3; x < W; x += 6) { const px = x + (r() - 0.5) * 4, py = y + (r() - 0.5) * 4, t = at(px, py); if (t < MIN_T) continue; const d = spacing(t); if (ok(px, py, d)) grow(add(px, py, d)); }

  // each dot: a slightly oval, slightly ragged 7-gon, the nib tilted ~35 degrees; a few run dry,
  // a few drag a tail as the pen lifts. Dots in the star's hottest light take the paler gold.
  const n = xs.length, shape = new Float32Array(n * V * 2), alpha = new Uint8Array(n), hot = new Uint8Array(n), tilt = -0.62;
  for (let i = 0; i < n; i++) {
    const s = rng(i * 7919 + 23), rad = DOT * (0.9 + s() * 0.24), el = 1.06 + s() * 0.12, ph = s() * 6.283, h2 = s() * 0.1, h3 = s() * 0.08, tail = s() < 0.035;
    alpha[i] = s() < 0.05 ? 140 : 225 + Math.floor(s() * 30);
    hot[i] = at(xs[i], ys[i]) > 0.8 ? 1 : 0;
    for (let v = 0; v < V; v++) {
      const a = (v / V) * Math.PI * 2, k = 1 + h2 * Math.sin(2 * a + ph) + h3 * Math.sin(3 * a + ph * 1.7);
      let ex = Math.cos(a) * rad * k * el; const ey = Math.sin(a) * rad * k;
      if (tail && v === 0) ex += rad * 1.3;
      shape[(i * V + v) * 2] = xs[i] + ex * Math.cos(tilt) - ey * Math.sin(tilt); shape[(i * V + v) * 2 + 1] = ys[i] + ex * Math.sin(tilt) + ey * Math.cos(tilt);
    }
  }
  S = { shape, alpha, hot, n }; env.cache.set(key, S);
  return S;
};

export const drawHomStipple = (ctx: Ctx, _frame: number, env: Env) => {
  const S = build(env);
  ctx.setTransform(env.scale, 0, 0, env.scale, 0, 0);
  // no sheet, no paper tooth: the page stays clear so the panel's brown is the paper
  for (const cls of [0, 1, 2]) {                               // 0 full nib, 1 running dry, 2 star-hot
    const path = new Path2D();
    for (let i = 0; i < S.n; i++) {
      const c = S.hot[i] ? 2 : S.alpha[i] < 200 ? 1 : 0; if (c !== cls) continue;
      const o = i * V * 2; path.moveTo(S.shape[o], S.shape[o + 1]); for (let v = 1; v < V; v++) path.lineTo(S.shape[o + v * 2], S.shape[o + v * 2 + 1]); path.closePath();
    }
    ctx.fillStyle = cls === 2 ? STAR_GOLD : GOLD; ctx.globalAlpha = cls === 1 ? 0.55 : 0.95; ctx.fill(path);
  }
  ctx.globalAlpha = 1;
};

export const homStipple: Film = {
  meta: { title: "The rising star · stipple", W: 960, H: 640, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id: "homStipple", start: 0, end: 1, draw: drawHomStipple }],
};
