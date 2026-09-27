// ARCANE SHAPES. Small geometry shared by the Arcane ornament tone fields: distance to a polyline
// with a varying half-width (a ribbon), and a shade value for a ribbon lit from the upper left.
// Tone fields built on these are SHADE fields: 0 bare paper, 1 densest ink.

export const clamp = (v: number, a = 0, b = 1) => (v < a ? a : v > b ? b : v);
export type Pt = [number, number];

/** Sample a cubic Bezier into n+1 points. */
export const bezier = (a: Pt, b: Pt, c: Pt, d: Pt, n = 48): Pt[] => {
  const out: Pt[] = [];
  for (let i = 0; i <= n; i++) {
    const t = i / n, u = 1 - t;
    out.push([u * u * u * a[0] + 3 * u * u * t * b[0] + 3 * u * t * t * c[0] + t * t * t * d[0], u * u * u * a[1] + 3 * u * u * t * b[1] + 3 * u * t * t * c[1] + t * t * t * d[1]]);
  }
  return out;
};

/** A spiral curl ending a vine: from `p` turning `turns` times, radius shrinking to 0. */
export const curlPts = (p: Pt, r0: number, a0: number, turns: number, dir: number, n = 60): Pt[] => {
  const out: Pt[] = [], cx = p[0] - Math.cos(a0) * r0, cy = p[1] - Math.sin(a0) * r0;
  for (let i = 0; i <= n; i++) { const f = i / n, a = a0 + dir * f * turns * Math.PI * 2, r = r0 * (1 - f * 0.85); out.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]); }
  return out;
};

/**
 * A ribbon along a polyline, half-width w(f) at fraction f of its length. Returns, for a point,
 * the signed depth inside (0 at the edge, 1 on the spine) and which side of the spine it is on
 * relative to a light from the upper left (+1 lit side, -1 shaded side), or null when outside.
 */
export type Ribbon = { pts: Pt[]; len: number[]; total: number; w: (f: number) => number; box: [number, number, number, number] };
export const ribbon = (pts: Pt[], w: (f: number) => number): Ribbon => {
  const len = [0]; for (let i = 1; i < pts.length; i++) len.push(len[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]));
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, wm = 0;
  for (let i = 0; i < 64; i++) wm = Math.max(wm, w(i / 63));
  for (const [x, y] of pts) { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); }
  return { pts, len, total: len[len.length - 1], w, box: [x0 - wm - 2, y0 - wm - 2, x1 + wm + 2, y1 + wm + 2] };
};

const LX = -0.7071, LY = -0.7071;                         // toward the light: upper left
export const ribbonAt = (R: Ribbon, x: number, y: number): { depth: number; lit: number } | null => {
  if (x < R.box[0] || y < R.box[1] || x > R.box[2] || y > R.box[3]) return null;
  let best = 1e9, bf = 0, bnx = 0, bny = 0;
  for (let i = 1; i < R.pts.length; i++) {
    const [ax, ay] = R.pts[i - 1], [bx, by] = R.pts[i], ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey || 1;
    const t = clamp(((x - ax) * ex + (y - ay) * ey) / l2), px = ax + ex * t, py = ay + ey * t, d = Math.hypot(x - px, y - py);
    if (d < best) { best = d; bf = (R.len[i - 1] + t * Math.sqrt(l2)) / (R.total || 1); const l = Math.sqrt(l2); bnx = -ey / l; bny = ex / l; if ((x - px) * bnx + (y - py) * bny < 0) { bnx = -bnx; bny = -bny; } }
  }
  const w = R.w(bf); if (best > w) return null;
  return { depth: 1 - best / w, lit: bnx * LX + bny * LY };
};

/** Shade inside a lit ribbon: pale on the lit flank, dark on the far flank, a crisp dark rim. */
export const ribbonShade = (h: { depth: number; lit: number }) => {
  const rim = h.depth < 0.18 ? 0.25 : 0;                    // the engraver's contour: a hair of dark at the edge
  const side = clamp(0.5 - 0.5 * h.lit * (1 - h.depth));    // across the round: lit flank 0 .. shaded flank 1
  return clamp(0.24 + 0.58 * side + rim);
};
