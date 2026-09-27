// CHALK SIGIL · the hub emblem of the Heart of Magic "Chalkboard" design: the Arcane design's magic
// circle of the five schools (arcaneSigilTone.ts, same proportions, same thirty runes from the same
// seed), set out on the board the way a teacher does it with a string compass and a metre rule.
//
// SUBJECT & REFERENCE. Classroom geometry construction on a blackboard (a string compass pinned
// at the centre, the rule for straight lines) crossed with the grimoire summoning circle the
// Arcane sigil draws: a heavy outer ring gone round twice with a hairline inside it, a band of
// runes written round by hand, the band's inner edge, the inner ring, a ruled pentagram whose
// five points each hold a small freehand circle (one per school) with a tapped dot, the pentagon
// joining them in a lighter hand, and a small core. The one coloured chalk, pale yellow, is kept
// for the core: what a teacher underlines.
//
// ORDER. The erased construction first (the vertical through the top point, a first ring swung a
// little off and rubbed out), then the compass rings, the ruled star, the pentagon, the school
// circles, the runes, the core in yellow, a few specks of dust in the circle.
//
// SIZE. It must read at ~100 px: rings 4-7 px, star 4 px at 512, so each keeps a pixel when shrunk.
import { Gfx, rng, type Ctx, type Env, type P } from "./core";
import type { Film } from "./film";
import { CHALK, CHALK_M, YELLOW, chalk, circlePts, compass, dot, ghost, lift, ring, scumble, settle, stroke } from "./chalkKit";

const S = 512, CX = 256, CY = 256;
const R_OUT = 0.465 * S, R_HAIR = 0.44 * S, R_BAND0 = 0.345 * S, R_BAND1 = 0.425 * S, R_IN = 0.33 * S;
const R_SCHOOL = 0.042 * S, R_CORE = 0.07 * S;
const PTS: P[] = Array.from({ length: 5 }, (_, k) => { const a = -Math.PI / 2 + (k * 2 * Math.PI) / 5; return [CX + Math.cos(a) * R_IN, CY + Math.sin(a) * R_IN] as P; });

// the runes, the same thirty glyphs as arcaneSigilTone (rng(51127), same call sequence): each two
// or three strokes between points of a 3x3 grid, written upright with their tops facing out
const GLYPHS = (() => {
  const r = rng(51127), out: number[][] = [];
  for (let g = 0; g < 30; g++) { const n = 2 + (r() < 0.5 ? 1 : 0), st: number[] = []; for (let s = 0; s < n; s++) { const a = Math.floor(r() * 9); let b = Math.floor(r() * 9); if (b === a) b = (a + 4) % 9; st.push(a, b); } out.push(st); }
  return out;
})();
const BAND_MID = (R_BAND0 + R_BAND1) / 2, UNIT = ((R_BAND1 - R_BAND0) * 0.8) / 2, CELL = (2 * Math.PI) / 30;
// glyph grid point (i in 0..8) of glyph g, nudged by a hand that writes round a circle
const runePt = (g: number, i: number, hand: () => number): P => {
  const gx = ((i % 3) - 1) * 0.6, gy = (Math.floor(i / 3) - 1) * 0.6;
  const a = (g + 0.5) * CELL - Math.PI / 2 + (gx * UNIT * 0.82) / BAND_MID + (hand() - 0.5) * 0.008, d = BAND_MID - gy * UNIT + (hand() - 0.5) * 1.8;
  return [CX + Math.cos(a) * d, CY + Math.sin(a) * d];
};

const unit = (a: P, b: P): P => { const l = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1; return [(b[0] - a[0]) / l, (b[1] - a[1]) / l]; };
// a ruled line from school circle a to school circle b, stopping at (or just short of, or just into) each rim
const ruled = (a: P, b: P, seed: number): P[] => {
  const u = unit(a, b), r = rng(seed), s0 = R_SCHOOL + 1.5 + (r() - 0.5) * 4, s1 = R_SCHOOL + 1.5 + (r() - 0.5) * 4;
  const p0: P = [a[0] + u[0] * s0, a[1] + u[1] * s0], p1: P = [b[0] - u[0] * s1, b[1] - u[1] * s1];
  return [p0, [(p0[0] + p1[0]) / 2 + (r() - 0.5) * 0.8, (p0[1] + p1[1]) / 2 + (r() - 0.5) * 0.8], p1];
};

const drawSigil = (ctx: Ctx, _frame: number, env: Env) => {
  const g = new Gfx(ctx, env, 0, CHALK_M);

  // ---- ghosts: the construction line and a first ring swung off-centre, both rubbed out
  ghost(g, () => {
    stroke(g, false, [[CX + 1, CY + R_OUT - 4], [CX - 1, CY - R_OUT + 6]], { w: 2.4, seed: 5, op: 1 });
    stroke(g, false, compass(CX + 7, CY - 5, R_IN + 9, 0.4, Math.PI * 1.3, 0.004, 6), { w: 3, seed: 6, op: 1 });
  }, 0.1, 2.2);

  // ---- the compass work: outer ring twice (so it is heavy and doubled), hairline, band, inner ring
  chalk(g, (dust) => {
    ring(g, dust, CX, CY, R_OUT - 1.5, { w: 5.2, seed: 11, a0: -2.4, drift: -0.007 });
    ring(g, dust, CX, CY, R_OUT + 1.2, { w: 3.6, seed: 12, a0: 0.9, drift: -0.006, skip: 0.6 });
    ring(g, dust, CX, CY, R_IN, { w: 4.4, seed: 15, a0: -0.6, drift: 0.006 });
  }, { dust: 0.22, blur: 2.6 });
  chalk(g, (dust) => {
    ring(g, dust, CX, CY, R_HAIR, { w: 2.3, seed: 13, a0: 1.8, drift: 0.005, skip: 0.8, sweeps: 1 });
    ring(g, dust, CX, CY, R_BAND0, { w: 2.4, seed: 14, a0: -1.3, drift: -0.006, skip: 0.7, sweeps: 1 });
  }, { dust: 0.16, blur: 2.2, bite: "light" });

  // ---- the ruled star, then the pentagon joining its points in a lighter hand
  chalk(g, (dust) => {
    for (let k = 0; k < 5; k++) stroke(g, dust, ruled(PTS[k], PTS[(k + 2) % 5], 30 + k), { w: 4.6, seed: 30 + k, press: lift(0.05, 0.3, 0.55), skip: 0.5, wob: 0.25 });
  }, { dust: 0.2, blur: 2.4 });
  chalk(g, (dust) => {
    for (let k = 0; k < 5; k++) stroke(g, dust, ruled(PTS[k], PTS[(k + 1) % 5], 40 + k), { w: 2.2, seed: 40 + k, op: 0.8, press: lift(0.05, 0.35, 0.4), skip: 1, wob: 0.25 });
  }, { dust: 0.12, blur: 2, bite: "light" });

  // ---- the five school circles, freehand, and a dot tapped in each
  chalk(g, (dust) => {
    PTS.forEach(([x, y], k) => {
      stroke(g, dust, compass(x, y, R_SCHOOL, -2 + k * 1.1, Math.PI * 2 + 0.45, 0.07, 50 + k, 0.94), { w: 3.6, seed: 50 + k, press: lift(0.06, 0.15, 0.4), wob: 0.8 });
      if (!dust) { scumble(g, dust, circlePts(x, y, 6.2, 16), { angle: 0.8 + k, gap: 2.2, w: 3, alpha: 0.9, seed: 60 + k }); dot(g, x, y, 4.2, 65 + k); }
    });
  }, { dust: 0.2, blur: 2.4 });

  // ---- the runes, written round the band
  chalk(g, (dust) => {
    const hand = rng(71);
    GLYPHS.forEach((st, gi) => {
      for (let s = 0; s < st.length; s += 2) { const a = runePt(gi, st[s], hand), b = runePt(gi, st[s + 1], hand); stroke(g, dust, [a, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], b], { w: 3.3, seed: 100 + gi * 5 + s, press: lift(0.1, 0.3, 0.7), wob: 0.35, taper: 0.2 }); }
    });
  }, { dust: 0.2, blur: 2 });

  // ---- dust caught inside the circle: specks, thicker toward the core
  g.group("plain", () => {
    const r = rng(90);
    for (let i = 0; i < 26; i++) { const a = r() * Math.PI * 2, d = R_CORE * 1.25 + Math.pow(r(), 1.4) * (R_IN * 0.62 - R_CORE); dot(g, CX + Math.cos(a) * d, CY + Math.sin(a) * d, 0.8 + r() * 1.1, 91 + i, CHALK, 0.35 + 0.4 * (1 - d / R_IN)); }
  }, { textures: ["chalkGrain"] });
  settle(g, CHALK);   // one chalk colour throughout (the layers' rounding otherwise speckles it)

  // ---- the core, in the one yellow: a ring and a seed laid with the side of the stick, finger-smudged
  ghost(g, () => scumble(g, false, circlePts(CX, CY, R_CORE * 0.95, 40), { angle: 0.4, gap: 3, w: 5, alpha: 0.9, seed: 81, color: YELLOW }), 0.1, 5);
  chalk(g, (dust) => {
    ring(g, dust, CX, CY, R_CORE, { w: 3.6, seed: 82, a0: 2.6, drift: 0.03, sweeps: 1, color: YELLOW, wob: 0.6 });
    scumble(g, dust, circlePts(CX, CY, 0.028 * S, 24), { angle: -0.7, gap: 2.4, w: 3.4, alpha: 0.85, seed: 83, color: YELLOW });
    scumble(g, dust, circlePts(CX - 1, CY - 1, 0.018 * S, 20), { angle: 0.9, gap: 2.4, w: 3, alpha: 0.9, seed: 84, color: YELLOW });
  }, { dust: 0.26, blur: 3 });

};

export const chalkSigil: Film = {
  meta: { title: "Chalk sigil", W: S, H: S, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id: "chalkSigil", start: 0, end: 1, draw: drawSigil }],
};
