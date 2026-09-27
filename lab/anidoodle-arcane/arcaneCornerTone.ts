// ARCANE CORNER. The ornament for the top-left corner of the Arcane parchment page (the panel
// mirrors it into the other three). SHADE semantics: 0 bare paper, 1 densest sepia.
//
// SUBJECT & REFERENCE. The corner piece of an engraved book border (the kind printers set at the
// corners of a title page frame): a small eight-point compass rose inside a ring at the corner, two
// ruled border lines running out along the top and left edges, and a vine from the rose along
// each edge that swells, thins and ends in a curl, with small leaves on alternate sides. One light,
// upper left, as every Arcane plate: each round form is pale on its upper-left flank and dark on
// the far one, and throws a short soft shadow down and to the right onto the paper.
//
// `inkIsLight` (default false) prints the same ornament for LIGHT ink on a dark page (the Night
// Grimoire's gold on indigo): the ink is then the light, so each form's lit flank and each rose
// point's lit half carry the most ink, the far ones the least, and the cast shadow is dropped (a
// shadow on a dark page is simply no gold). The default path is unchanged.
import { bezier, clamp, curlPts, ribbon, ribbonAt, ribbonShade, type Pt, type Ribbon } from "./arcaneShapes";

export const arcaneCornerTone = (W: number, H: number, inkIsLight = false): Float32Array => {
  const raster = new Float32Array(W * H), S = Math.min(W, H);
  const P = (x: number, y: number): Pt => [x * S, y * S];
  const rose: Pt = P(0.17, 0.17), R0 = 0.105 * S;

  // the vines: along the top edge and down the left edge, each from the rose to a curl
  const vine = (horizontal: boolean): Pt[] => {
    const q = (a: number, b: number): Pt => (horizontal ? P(a, b) : P(b, a));
    const main = bezier(q(0.27, 0.15), q(0.45, 0.07), q(0.6, 0.22), q(0.84, 0.12), 80);
    const end = main[main.length - 1], tail = curlPts(end, 0.035 * S, horizontal ? -Math.PI / 2 : Math.PI, 1.1, horizontal ? 1 : -1, 50);
    return main.concat(tail.slice(1));
  };
  const vines: Ribbon[] = [true, false].map((h) => ribbon(vine(h), (f) => (0.03 * S) * Math.sin(Math.PI * Math.min(1, f * 1.4 + 0.12)) * (1 - 0.7 * f) + 0.004 * S));
  // leaves: small teardrops off the vines, alternating sides
  const leaves: Ribbon[] = [];
  for (const h of [true, false]) for (let k = 0; k < 3; k++) {
    const q = (a: number, b: number): Pt => (h ? P(a, b) : P(b, a)), side = k % 2 ? 1 : -1, x = 0.4 + k * 0.14, y = 0.12 + side * 0.012;
    leaves.push(ribbon(bezier(q(x, y), q(x + 0.02, y + side * 0.06), q(x + 0.07, y + side * 0.075), q(x + 0.1, y + side * 0.06), 24), (f) => 0.022 * S * Math.sin(Math.PI * f)));
  }
  const forms = vines.concat(leaves);

  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let t = 0;
    // ruled border: a firm line and a hairline inside it, fading out as they leave the corner
    const fadeX = clamp(1.15 - x / W), fadeY = clamp(1.15 - y / H);
    const e1 = 0.035 * S, e2 = 0.052 * S;
    if (Math.abs(y - e1) < 0.0045 * S && x > e1) t = Math.max(t, 0.75 * fadeX);
    if (Math.abs(x - e1) < 0.0045 * S && y > e1) t = Math.max(t, 0.75 * fadeY);
    if (Math.abs(y - e2) < 0.002 * S && x > e2 && x > rose[0] + R0) t = Math.max(t, 0.5 * fadeX);
    if (Math.abs(x - e2) < 0.002 * S && y > e2 && y > rose[1] + R0) t = Math.max(t, 0.5 * fadeY);

    // the compass rose: eight points, each split along its ridge into a lit and a shaded half
    const dx = x - rose[0], dy = y - rose[1], d = Math.hypot(dx, dy), a = Math.atan2(dy, dx);
    if (Math.abs(d - R0) < 0.006 * S) t = Math.max(t, 0.8);                     // the ring
    if (Math.abs(d - R0 * 1.12) < 0.0022 * S) t = Math.max(t, 0.45);            // its hairline
    for (let k = 0; k < 8; k++) {
      const ak = (k * Math.PI) / 4, long = k % 2 === 0, len = (long ? 0.95 : 0.6) * R0, base = (long ? 0.2 : 0.16) * R0;
      let rel = a - ak; while (rel > Math.PI) rel -= 2 * Math.PI; while (rel < -Math.PI) rel += 2 * Math.PI;
      const along = d * Math.cos(rel), across = d * Math.sin(rel);
      if (along < 0 || along > len) continue;
      const half = base * (1 - along / len); if (Math.abs(across) > half) continue;
      // the half facing the light (upper left) is pale, the other dark
      const nx = Math.cos(ak + (across > 0 ? Math.PI / 2 : -Math.PI / 2)), ny = Math.sin(ak + (across > 0 ? Math.PI / 2 : -Math.PI / 2));
      const lit = nx * -0.7071 + ny * -0.7071;
      t = Math.max(t, (lit > 0) !== inkIsLight ? 0.14 + (long ? 0 : 0.08) : 0.72 + (long ? 0.08 : 0));
    }
    if (d < 0.012 * S) t = Math.max(t, 0.85);                                     // the pivot

    // vines and leaves, with a soft shadow cast down-right onto the paper
    for (const R of forms) {
      const h = ribbonAt(R, x, y); if (h) { t = Math.max(t, ribbonShade(h, inkIsLight)); continue; }
      if (inkIsLight) continue;
      const sh = ribbonAt(R, x - 0.012 * S, y - 0.012 * S); if (sh) t = Math.max(t, 0.18 * sh.depth + 0.06);
    }
    raster[y * W + x] = clamp(t);
  }
  return raster;
};
