// ARCANE SIGIL. The magic circle set inside the tree's hub seal in the Arcane design: the heart of
// magic is where the five schools meet, so the circle is drawn as a binding of five. SHADE
// semantics: 0 bare paper, 1 densest sepia.
//
// SUBJECT & REFERENCE. The summoning circles of grimoire engravings (the Lesser Key's seals, the
// circles in Renaissance magical manuscripts): a firm outer ring with a hairline inside it, a band of
// runes read round the circle, an inner ring, a pentagram whose five points each hold a small
// circle (here one per school), the pentagon joining them in a lighter line, and a small ringed core
// at the centre. Inside the circle a sparse drift of motes, the stillness of the particle core the
// heart used to show. All flat line work: an engraved circle has no light and shade, only line.
import { clamp } from "./arcaneShapes";
import { rng } from "./core";

const segDist = (x: number, y: number, ax: number, ay: number, bx: number, by: number) => {
  const ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey || 1, t = clamp(((x - ax) * ex + (y - ay) * ey) / l2);
  return Math.hypot(x - ax - ex * t, y - ay - ey * t);
};

export const arcaneSigilTone = (W: number, H: number): Float32Array => {
  const raster = new Float32Array(W * H), S = Math.min(W, H), cx = W / 2, cy = H / 2;
  const R_OUT = 0.465 * S, R_HAIR = 0.44 * S, R_BAND0 = 0.345 * S, R_BAND1 = 0.425 * S, R_IN = 0.33 * S;

  // the pentagram's points, one per school, the first straight up
  const pts: [number, number][] = [];
  for (let k = 0; k < 5; k++) { const a = -Math.PI / 2 + (k * 2 * Math.PI) / 5; pts.push([cx + Math.cos(a) * R_IN, cy + Math.sin(a) * R_IN]); }
  const star: [number, number, number, number][] = [], gon: [number, number, number, number][] = [];
  for (let k = 0; k < 5; k++) { const a = pts[k], b = pts[(k + 2) % 5], c = pts[(k + 1) % 5]; star.push([a[0], a[1], b[0], b[1]]); gon.push([a[0], a[1], c[0], c[1]]); }

  // the runes: 30 glyphs round the band, each two or three strokes on a 3x3 grid
  const GLYPHS = 30, r = rng(51127), glyphs: number[][] = [];
  for (let g = 0; g < GLYPHS; g++) {
    const n = 2 + (r() < 0.5 ? 1 : 0), strokes: number[] = [];
    for (let s = 0; s < n; s++) { const a = Math.floor(r() * 9); let b = Math.floor(r() * 9); if (b === a) b = (a + 4) % 9; strokes.push(a, b); }
    glyphs.push(strokes);
  }
  const bandMid = (R_BAND0 + R_BAND1) / 2, bandH = (R_BAND1 - R_BAND0) * 0.62, cellA = (2 * Math.PI) / GLYPHS;

  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const dx = x - cx, dy = y - cy, d = Math.hypot(dx, dy);
    let t = 0;
    if (d > R_OUT + 0.02 * S) { raster[y * W + x] = 0; continue; }
    if (Math.abs(d - R_OUT) < 0.011 * S) t = 0.9;                                   // outer ring
    if (Math.abs(d - R_HAIR) < 0.0035 * S) t = Math.max(t, 0.65);                    // its hairline
    if (Math.abs(d - R_BAND0) < 0.0035 * S) t = Math.max(t, 0.65);                   // inner edge of the rune band
    if (Math.abs(d - R_IN) < 0.006 * S) t = Math.max(t, 0.8);                        // inner ring

    // runes: map the point into its glyph cell (angle across, radius up) and test the strokes
    if (d > bandMid - bandH / 2 - 0.01 * S && d < bandMid + bandH / 2 + 0.01 * S) {
      const a = (Math.atan2(dy, dx) + Math.PI * 2.5) % (Math.PI * 2), g = Math.floor(a / cellA), u = (a / cellA - g - 0.5) * 1.55, v = (bandMid - d) / (bandH / 2);
      // cell coordinates in -1..1; grid points at -0.6, 0, 0.6 across and up
      const gp = (i: number): [number, number] => [((i % 3) - 1) * 0.6, (Math.floor(i / 3) - 1) * 0.6];
      const st = glyphs[g], px = u * (cellA * bandMid) / 2, py = v * bandH / 2, unit = bandH / 2;
      for (let s = 0; s < st.length; s += 2) {
        const [ax, ay] = gp(st[s]), [bx, by] = gp(st[s + 1]);
        if (segDist(px, py, ax * unit, ay * unit, bx * unit, by * unit) < 0.0055 * S) { t = Math.max(t, 0.85); break; }
      }
    }

    if (d < R_IN) {
      for (const [ax, ay, bx, by] of star) if (segDist(x, y, ax, ay, bx, by) < 0.006 * S) t = Math.max(t, 0.8);
      for (const [ax, ay, bx, by] of gon) if (segDist(x, y, ax, ay, bx, by) < 0.003 * S) t = Math.max(t, 0.5);
      if (Math.abs(d - 0.07 * S) < 0.005 * S) t = Math.max(t, 0.8);                  // the core's ring
      if (d < 0.03 * S) t = Math.max(t, 0.7);                                        // and its seed
      // a sparse drift of motes: faint, thicker toward the core
      t = Math.max(t, 0.05 + 0.07 * clamp(1 - d / R_IN));
    }
    // a small circle at each school's point, cleared inside so it reads as a node on the star
    for (const [px, py] of pts) {
      const e = Math.hypot(x - px, y - py);
      if (e < 0.042 * S) t = Math.abs(e - 0.042 * S) < 0.006 * S ? 0.85 : e < 0.014 * S ? 0.75 : 0.02;
    }
    raster[y * W + x] = clamp(t);
  }
  return raster;
};
