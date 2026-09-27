import { rng, type Ctx, type Env } from "./core";
import type { Film } from "./film";

// HOM STIPPLE KIT. The pen-and-ink stipple of the nautilus plate, as one reusable pen for the
// Heart of Magic plates: give it a tone field (0 = no ink, 1 = densest) and an ink, and it lays
// weighted blue-noise dots (variable-radius Poisson disk, spacing ~ 1.55 px / tone^0.64) on a
// TRANSPARENT sheet. No paper fill, no tooth: the panel is the paper.
//
// A dot is a slightly oval, slightly ragged 7-gon, the nib tilted ~35 degrees; a few come up grey
// from a nib running dry, a few drag a hair of tail. `hotInk` (optional) takes the dots whose tone
// is above `hotAbove`: the star's core in gold plates, or nothing in sepia ones.

export type ToneFn = (W: number, H: number) => Float32Array;
export type StippleInk = { ink: string; hotInk?: string; hotAbove?: number; dot?: number; seed?: number };

type Stip = { shape: Float32Array; alpha: Uint8Array; hot: Uint8Array; n: number };
const V = 7;

const build = (env: Env, id: string, tone: ToneFn, o: StippleInk): Stip => {
  const key = `homStippleKit:${id}:${env.W}x${env.H}`; let S = env.cache.get(key) as Stip | undefined; if (S) return S;
  const W = env.W, H = env.H, T = tone(W, H), DOT = o.dot ?? 1.05;
  const at = (x: number, y: number) => (x < 0 || y < 0 || x >= W || y >= H ? 0 : T[(y | 0) * W + (x | 0)]);
  const MIN_T = 0.03, spacing = (t: number) => (1.55 * DOT / 1.05) / Math.pow(Math.max(t, MIN_T), 0.64);
  const cell = 1.35, gw = Math.ceil(W / cell), gh = Math.ceil(H / cell), grid = new Int32Array(gw * gh).fill(-1);
  const xs: number[] = [], ys: number[] = [], rs: number[] = [], r = rng(o.seed ?? 20260927);
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

  const n = xs.length, shape = new Float32Array(n * V * 2), alpha = new Uint8Array(n), hot = new Uint8Array(n), tilt = -0.62;
  const hotAbove = o.hotInk ? o.hotAbove ?? 0.8 : 2;
  for (let i = 0; i < n; i++) {
    const s = rng(i * 7919 + 23), rad = DOT * (0.9 + s() * 0.24), el = 1.06 + s() * 0.12, ph = s() * 6.283, h2 = s() * 0.1, h3 = s() * 0.08, tail = s() < 0.035;
    alpha[i] = s() < 0.05 ? 140 : 225 + Math.floor(s() * 30);
    hot[i] = at(xs[i], ys[i]) > hotAbove ? 1 : 0;
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

export const stippleDraw = (id: string, tone: ToneFn, o: StippleInk) => (ctx: Ctx, _frame: number, env: Env) => {
  const S = build(env, id, tone, o);
  ctx.setTransform(env.scale, 0, 0, env.scale, 0, 0);
  for (const cls of [0, 1, 2]) {                               // 0 full nib, 1 running dry, 2 hot
    const path = new Path2D(); let any = false;
    for (let i = 0; i < S.n; i++) {
      const c = S.hot[i] ? 2 : S.alpha[i] < 200 ? 1 : 0; if (c !== cls) continue; any = true;
      const k = i * V * 2; path.moveTo(S.shape[k], S.shape[k + 1]); for (let v = 1; v < V; v++) path.lineTo(S.shape[k + v * 2], S.shape[k + v * 2 + 1]); path.closePath();
    }
    if (!any) continue;
    ctx.fillStyle = cls === 2 ? o.hotInk ?? o.ink : o.ink; ctx.globalAlpha = cls === 1 ? 0.55 : 0.95; ctx.fill(path);
  }
  ctx.globalAlpha = 1;
};

/** A one-frame still film around a tone field. */
export const stippleStill = (id: string, title: string, W: number, H: number, tone: ToneFn, o: StippleInk): Film => ({
  meta: { title, W, H, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id, start: 0, end: 1, draw: stippleDraw(id, tone, o) }],
});
