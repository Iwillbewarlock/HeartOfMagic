// ARCANE HEART. The emblem set inside the tree's hub seal in the Arcane design: a faceted
// heart-shaped gem, the "heart of magic", with a glint. SHADE semantics: 0 bare paper, 1 densest
// sepia.
//
// SUBJECT & REFERENCE. A step-cut gemstone drawn the way engraved plates of jewels show them:
// the outline of a heart (the classic implicit heart curve), a flat table in the upper middle, and
// facets fanning from the table to the girdle, each flat facet one even tone set by how it faces
// one light from the upper left - pale facets up and left, dark ones down and right, the lower
// point darkest. A bare-paper glint (a four-point star) on the upper-left lobe, a crisp dark
// girdle line, and a short soft shadow down and to the right.
import { clamp } from "./arcaneShapes";

const heartF = (x: number, y: number) => { const a = x * x + y * y - 1; return a * a * a - x * x * y * y * y; };   // <= 0 inside; y up

export const arcaneHeartTone = (W: number, H: number): Float32Array => {
  const raster = new Float32Array(W * H), S = Math.min(W, H), cx = W / 2, cy = H * 0.5, k = S * 0.34;
  const inside = (px: number, py: number) => heartF((px - cx) / k, -(py - cy) / k - 0.05) <= 0;
  const table: [number, number] = [cx - 0.02 * S, cy - 0.06 * S];               // the table sits up and a little left
  const FACETS = 12;
  const facetTone = (i: number) => {                                             // each facet's normal tilts outward at angle ai
    const ai = ((i + 0.5) / FACETS) * Math.PI * 2, nx = Math.cos(ai) * 0.6, ny = Math.sin(ai) * 0.6, nz = 0.8;
    const lam = clamp(nx * -0.55 + ny * -0.55 + nz * 0.63);                       // lamp upper left, ~40 degrees up
    return clamp(0.2 + 0.5 * (1 - lam));
  };
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let t = 0;
    if (inside(x, y)) {
      // the girdle: a dark band where the outline is near
      let edge = false; for (const [ox, oy] of [[3, 0], [-3, 0], [0, 3], [0, -3]]) if (!inside(x + ox * S / 512, y + oy * S / 512)) edge = true;
      const tx = x - table[0], ty = y - table[1], tr = Math.hypot(tx / 1.25, ty);
      if (edge) t = 0.85;
      else if (tr < 0.075 * S) t = Math.abs(tr - 0.075 * S) < 0.005 * S ? 0.7 : 0.06;  // the table: nearly bare, edged
      else {
        const a = Math.atan2(ty, tx), i = Math.floor(((a + Math.PI * 2) % (Math.PI * 2)) / (Math.PI * 2) * FACETS);
        const within = ((a + Math.PI * 2) % (Math.PI * 2)) / (Math.PI * 2) * FACETS - i;
        t = facetTone(i) + (within < 0.05 || within > 0.95 ? 0.35 : 0);             // facet edges: engraved ridges
        t += 0.1 * clamp((y - cy) / (0.4 * S));                                     // the lower point sits in shadow
      }
      // the glint: a four-point star of bare paper on the upper-left lobe
      const gx = x - (cx - 0.13 * S), gy = y - (cy - 0.16 * S);
      const glint = Math.exp(-(gx * gx) / (2 * (0.004 * S) ** 2)) * Math.exp(-Math.abs(gy) / (0.05 * S)) + Math.exp(-(gy * gy) / (2 * (0.004 * S) ** 2)) * Math.exp(-Math.abs(gx) / (0.05 * S)) + Math.exp(-(gx * gx + gy * gy) / (2 * (0.012 * S) ** 2));
      t = t * (1 - clamp(glint * 1.4));
    } else if (inside(x - 0.02 * S, y - 0.025 * S)) t = 0.2;                         // cast shadow down-right
    raster[y * W + x] = clamp(t);
  }
  return raster;
};
