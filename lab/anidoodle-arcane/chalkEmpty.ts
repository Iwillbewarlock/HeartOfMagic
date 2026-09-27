// CHALK EMPTY · the "no spell tree yet" screen of the Heart of Magic "Chalkboard" design: an open
// book on the board with a star rising out of its gutter, the way a teacher would chalk it for a
// first lesson. Designed at 960 x 640, delivered at 480 x 320 (--scale 0.5); transparent sheet.
//
// SUBJECT & REFERENCE. The same book and star as the other designs' empty plates (spellbookTone.ts:
// its BOOK proportions, the leaves' curl, the star's place), turned into a line drawing the way a
// board drawing works: a hardbound book opened flat, seen from the front and a little above, each
// leaf's top edge rising to its curl near the gutter and dipping into it, the gutter a single line,
// the page block's fore-edge a few ruled stripes at the bottom, the boards a margin proud of the
// pages with their front edge hatched. On the pages, rows of scribbled "writing" that follow each
// leaf's curve, and on the right page a small magic circle. The star is the one yellow on the board:
// a four-point star laid in with the side of the stick, outlined, with short rays round it and a
// finger-smudged glow; a dotted trail climbs to it from the gutter. The light is the star: the
// leaves near the gutter, facing it, get a thin scumble of chalk; the far corners get none.
//
// Behind it, very faint, an old diagram rubbed out: nodes and links of a tree that is not there yet.
import { Gfx, fractal, rng, type Ctx, type Env, type P } from "./core";
import type { Film } from "./film";
import { BOOK } from "./spellbookTone";
import { CHALK, CHALK_M, YELLOW, chalk, circlePts, compass, dot, ghost, lift, scumble, settle, stroke } from "./chalkKit";

const W = 960, H = 640;
const CX = BOOK.CX * W, X0 = BOOK.X0 * W, X1 = BOOK.X1 * W, HALF = (X1 - X0) / 2, SX = BOOK.STAR[0] * W, SY = BOOK.STAR[1] * H;
const BM = 0.022 * W;                                             // the boards stand this far proud of the pages
const clamp = (v: number, a = 0, b = 1) => (v < a ? a : v > b ? b : v);
// the leaf's lift across the page (p: 0 gutter .. 1 outer edge), as spellbookTone's curl
const curl = (p: number) => Math.pow(1 - p, 1.25) * (1 - 0.35 * Math.exp(-p * 60));
const pOf = (x: number) => clamp(Math.abs(x - CX) / HALF);
const topY = (x: number) => (BOOK.TOP - BOOK.RISE * curl(pOf(x))) * H;
const botY = (x: number) => (BOOK.BOT - 0.35 * BOOK.RISE * curl(pOf(x))) * H;
const onLeaf = (x: number, v: number): P => [x, topY(x) + v * (botY(x) - topY(x))];
// a curve across one leaf, from p0 to p1 (side -1 left, 1 right), at height fraction v (or an offset below the bottom edge)
const across = (side: number, p0: number, p1: number, y: (x: number) => number, n = 24): P[] => Array.from({ length: n + 1 }, (_, i) => { const x = CX + side * HALF * (p0 + ((p1 - p0) * i) / n); return [x, y(x)] as P; });

// ---------------------------------------------------------------- the old diagram, rubbed out
const ghostTree = (g: Gfx) => {
  const trees: { nodes: P[]; links: [number, number][] }[] = [
    { nodes: [[200, 282], [140, 206], [262, 200], [98, 130], [170, 126], [240, 122], [306, 134], [272, 56]], links: [[0, 1], [0, 2], [1, 3], [1, 4], [2, 5], [2, 6], [5, 7]] },
    { nodes: [[762, 272], [706, 196], [820, 192], [668, 120], [742, 114], [864, 112]], links: [[0, 1], [0, 2], [1, 3], [1, 4], [2, 5]] },
  ];
  trees.forEach((t, ti) => {
    t.links.forEach(([a, b], k) => { const A = t.nodes[a], B = t.nodes[b], l = Math.hypot(B[0] - A[0], B[1] - A[1]), ux = (B[0] - A[0]) / l, uy = (B[1] - A[1]) / l; stroke(g, false, [[A[0] + ux * 13, A[1] + uy * 13], [B[0] - ux * 13, B[1] - uy * 13]], { w: 3.4, seed: 200 + ti * 20 + k, op: 0.45 + ((k * 37 + ti * 11) % 10) * 0.055, wob: 0.6 }); });
    t.nodes.forEach(([x, y], k) => stroke(g, false, compass(x, y, 11, -2, Math.PI * 2 + 0.3, 0.05, 240 + ti * 20 + k), { w: 3.2, seed: 240 + ti * 20 + k, op: 0.5 + ((k * 53 + ti * 7) % 10) * 0.05, wob: 0.6 }));
  });
};

// scribbled "writing": words of small arches along a row that follows the leaf
const writing = (g: Gfx, dust: boolean, side: number, p0: number, p1: number, v: number, seed: number) => {
  const r = rng(seed); let p = p0 + r() * 0.03;
  while (p < p1 - 0.03) {
    const len = Math.min(p1 - p, 0.05 + r() * 0.1), n = Math.max(4, Math.round((len * HALF) / 3.2)), amp = 2.4 + r() * 1.2, ph = r() * 6, pts: P[] = [];
    for (let i = 0; i <= n; i++) { const x = CX + side * HALF * (p + (len * i) / n), [, y] = onLeaf(x, v); pts.push([x, y - Math.abs(Math.sin(i * 1.3 + ph)) * amp + (r() - 0.5) * 1.2]); }
    if (side < 0) pts.reverse();                                                     // written left to right on both pages
    stroke(g, dust, pts, { w: 2.2, seed: seed + Math.round(p * 1000), op: 0.72, press: lift(0.1, 0.3, 0.5), wob: 0.4, taper: 0.5, dustW: 1.8 });
    p += len + 0.025 + r() * 0.03;
  }
};

const drawEmpty = (ctx: Ctx, _frame: number, env: Env) => {
  const g = new Gfx(ctx, env, 0, CHALK_M);

  // ---- ghosts: yesterday's diagram and the wide arcs of the felt that rubbed it out
  ghost(g, () => ghostTree(g), 0.05, 3.4);
  ghost(g, () => { const r = rng(300); for (let i = 0; i < 4; i++) { const cx = 180 + r() * 600, cy = 120 + r() * 200, rr = 150 + r() * 120, a0 = r() * 6; g.pen(Array.from({ length: 11 }, (_, k) => { const a = a0 + (k / 10) * 2; return [cx + Math.cos(a) * rr, cy + Math.sin(a) * rr * 0.5] as P; }), { w: 46, color: CHALK, seed: 301 + i, wobble: 3, boil: 0, opacity: 1, retrace: false, taper: 0.4 }); } }, 0.025, 9);

  // ---- the star's light on the leaves: a thin scumble, thickest near the gutter's rise, none at the far corners
  // built up the way chalk shading is: drag over drag at turning angles, each pass reaching less far
  const lightF = (x: number, y: number) => { const d = Math.hypot(x - SX, (y - SY) * 0.8) / (0.5 * W), p = pOf(x); return clamp(1.3 - 1.1 * d - 1.6 * p) * clamp(p * 30) + (fractal(77, x, y, 0.028, 0.028, 3) - 0.5) * 0.5; };
  [-1, 1].forEach((side, k) => {
    const leaf = [...across(side, 0.02, 1, topY), ...across(side, 0.02, 1, botY).reverse()];
    chalk(g, (dust) => [0.14, 0.5].forEach((th, i) => scumble(g, dust, leaf, { angle: side * (0.3 + i * 0.6), gap: 7.5, w: 5.5, alpha: 0.27, seed: 70 + k * 3 + i, keep: (x, y) => lightF(x, y) > th })), { dust: 0.12, blur: 4, bite: "light" });
  });

  // ---- the book: boards first (they are behind), then the page block, the leaves, the gutter
  chalk(g, (dust) => {
    const bt = (x: number) => (BOOK.BOT - 0.35 * BOOK.RISE * Math.pow(1 - pOf(x), 1.25)) * H + 0.045 * H;   // the board's edge: the leaves' curve without their dip into the gutter
    stroke(g, dust, [[X0 - BM, topY(X0) + 0.03 * H], [X0 - BM - 1, bt(X0) - 6], [X0 - BM + 6, bt(X0)]], { w: 3.6, seed: 10, wob: 0.4 });
    stroke(g, dust, [[X1 + BM, topY(X1) + 0.03 * H], [X1 + BM + 1, bt(X1) - 6], [X1 + BM - 6, bt(X1)]], { w: 3.6, seed: 11, wob: 0.4 });
    stroke(g, dust, [[X0 - BM + 6, bt(X0)], ...across(-1, 0.9, 0.04, (x) => bt(x) + 1, 16), [CX, bt(CX) + 3]], { w: 4, seed: 12, wob: 0.5 });
    stroke(g, dust, [[CX, bt(CX) + 3], ...across(1, 0.04, 0.9, (x) => bt(x) + 1, 16), [X1 + BM - 6, bt(X1)]], { w: 4, seed: 13, wob: 0.5 });
  }, { dust: 0.2, blur: 3.2 });
  chalk(g, (dust) => {   // the fore-edge: page ends ruled as stripes; the board's front edge hatched
    [-1, 1].forEach((side, k) => {
      for (let j = 1; j <= 3; j++) stroke(g, dust, across(side, 0.05, 0.99, (x) => botY(x) + j * 4.2, 18), { w: 1.7, seed: 20 + k * 5 + j, op: 0.75, press: lift(0.1, 0.25, 0.5), skip: 0.6, wob: 0.4 });
      const r = rng(30 + k);
      for (let q = 0.06; q < 1.02; q += 0.024 + r() * 0.006) { const x = CX + side * HALF * q, y0 = botY(x) + 16, y1 = botY(x) + 0.045 * H - 2; stroke(g, dust, [[x - 3, y1], [x + 4, y0]], { w: 1.6, seed: 40 + Math.round(q * 500) + k * 1000, op: 0.55, wob: 0.3, taper: 0.6 }); }
    });
  }, { dust: 0.12, blur: 2.4, bite: "light" });
  chalk(g, (dust) => {
    [-1, 1].forEach((side, k) => {
      stroke(g, dust, across(side, 1, 0, topY, 40), { w: 4.2, seed: 50 + k, press: lift(0.05, 0.15, 0.6), wob: 0.4 });      // the leaf's top edge, from the outer corner into the gutter
      stroke(g, dust, across(side, 1, 0.01, botY, 30), { w: 3.6, seed: 52 + k, press: lift(0.05, 0.15, 0.6), wob: 0.4 });   // its bottom edge
      const xo = CX + side * HALF; stroke(g, dust, [[xo, topY(xo)], [xo + side * 1.5, (topY(xo) + botY(xo)) / 2], [xo, botY(xo)]], { w: 3.4, seed: 54 + k, wob: 0.4 });
      stroke(g, dust, across(side, 0.97, 0.04, (x) => topY(x) + 5, 30), { w: 1.6, seed: 56 + k, op: 0.6, press: lift(0.05, 0.3, 0.3), skip: 1, wob: 0.4 });   // the leaf under it
    });
    stroke(g, dust, [[CX, topY(CX) - 1], [CX + 1, (topY(CX) + botY(CX)) / 2], [CX, botY(CX) + 2]], { w: 3.2, seed: 58, press: lift(0.05, 0.3, 0.5), wob: 0.3 });   // the gutter
  }, { dust: 0.22, blur: 3.2 });

  // ---- the writing, and the little magic circle on the right page
  const gx = CX + 0.52 * HALF, gv = 0.42, [, gy] = onLeaf(gx, gv), RX = 0.052 * W, RY = RX / 1.35;
  chalk(g, (dust) => {
    const rows = [0.16, 0.29, 0.42, 0.55, 0.68, 0.81];
    rows.forEach((v, i) => { writing(g, dust, -1, 0.1, 0.46, v, 400 + i * 17); writing(g, dust, -1, 0.53, 0.9, v, 500 + i * 17); });
    rows.forEach((v, i) => { writing(g, dust, 1, 0.08, 0.3, v, 600 + i * 17); if (Math.abs(v - gv) > 0.24) writing(g, dust, 1, 0.36, 0.7, v, 650 + i * 17); writing(g, dust, 1, 0.76, 0.92, v, 700 + i * 17); });
  }, { dust: 0.12, blur: 2.4, bite: "light" });
  chalk(g, (dust) => {
    stroke(g, dust, compass(gx, gy, RX, -2.4, Math.PI * 2 + 0.3, 0.04, 80, RY / RX), { w: 2.8, seed: 80, press: lift(0.05, 0.12, 0.4), wob: 0.5 });
    const pts: P[] = Array.from({ length: 5 }, (_, k) => { const a = -Math.PI / 2 + (k * 4 * Math.PI) / 5; return [gx + Math.cos(a) * RX * 0.8, gy + Math.sin(a) * RY * 0.8] as P; });
    stroke(g, dust, [...pts, pts[0]], { w: 2.4, seed: 81, press: lift(0.05, 0.2, 0.5), wob: 0.3, taper: 0.3 });
  }, { dust: 0.16, blur: 2.4 });

  // ---- the trail: dots climbing from the gutter to the star, closer and bigger as they rise
  g.group("plain", () => {
    const y0 = topY(CX) - 14, y1 = SY + 0.12 * H;
    for (let i = 0; i < 12; i++) { const f = Math.pow(i / 11, 0.85), y = y0 + (y1 - y0) * f, x = CX + Math.sin(f * 5.2) * 14 * (1 - f); dot(g, x, y, 1.4 + f * 2.2, 90 + i, CHALK, 0.55 + 0.4 * f); }
  }, { textures: ["chalkGrain"] });
  settle(g, CHALK);

  // ---- the star, in the one yellow: a smudged glow, the body laid in, the outline, short rays round it
  ghost(g, () => { const r = rng(110); scumble(g, false, circlePts(SX, SY, 58, 36), { angle: 0.6, gap: 3.5, w: 6, alpha: 0.9, seed: 111, color: YELLOW, keep: (x, y) => Math.hypot(x - SX, y - SY) < 30 + r() * 30 }); }, 0.16, 7);
  const TV = 0.11 * H, TH = 0.058 * W, WAIST = 11;
  const tips: P[] = [[SX, SY - TV], [SX + TH, SY], [SX, SY + TV * 0.92], [SX - TH, SY]];
  const waists: P[] = [0, 1, 2, 3].map((k) => { const a = -Math.PI / 4 + (k * Math.PI) / 2; return [SX + Math.cos(a) * WAIST, SY + Math.sin(a) * WAIST] as P; });
  const body: P[] = tips.flatMap((t, k) => [t, waists[k]]);
  chalk(g, (dust) => {
    scumble(g, dust, body, { angle: 0.7, gap: 2.4, w: 3.6, alpha: 0.72, seed: 120, color: YELLOW });
    scumble(g, dust, body, { angle: -0.8, gap: 3.4, w: 3, alpha: 0.6, seed: 121, color: YELLOW, keep: (x, y) => Math.hypot((x - SX) * 1.4, y - SY) < 30 });   // gone over again at the heart
    tips.forEach((t, k) => { stroke(g, dust, [waists[(k + 3) % 4], t], { w: 3.4, seed: 122 + k * 2, color: YELLOW, press: lift(0.1, 0.2, 0.5), wob: 0.3, taper: 0.4 }); stroke(g, dust, [t, waists[k]], { w: 3.4, seed: 123 + k * 2, color: YELLOW, press: lift(0.1, 0.2, 0.5), wob: 0.3, taper: 0.4 }); });
    for (let k = 0; k < 4; k++) { const a = Math.PI / 4 + (k * Math.PI) / 2 + (k % 2 ? 0.06 : -0.05); stroke(g, dust, [[SX + Math.cos(a) * 26, SY + Math.sin(a) * 26], [SX + Math.cos(a) * 44, SY + Math.sin(a) * 44]], { w: 2.6, seed: 140 + k, color: YELLOW, press: lift(0.1, 0.4, 0.4), wob: 0.2, taper: 0.7 }); }   // short rays on the diagonals
  }, { dust: 0.3, blur: 3.6 });
};

export const chalkEmpty: Film = {
  meta: { title: "Chalk empty", W, H, fps: 30, bpm: 120, durationFrames: 1 },
  assets: { images: {} },
  shots: [{ id: "chalkEmpty", start: 0, end: 1, draw: drawEmpty }],
};
