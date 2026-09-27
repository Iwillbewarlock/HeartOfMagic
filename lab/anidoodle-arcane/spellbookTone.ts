// SPELLBOOK TONE. The shared scene for the Heart of Magic "no tree yet" plate: an open grimoire
// lying on a dark desk, and one star lifting out of its gutter. No drawing here, only the
// question "how much LIGHT is at this point?" (0 = the dark panel shows through, 1 = full gold),
// answered from the book's real shape and one light source: the star itself.
//
// The plates print this in gold ink on a transparent sheet over the panel's dark brown, so
// "ink" means light: the stipple puts more dots where it is brighter, the engraving a wider line.
//
// SUBJECT & REFERENCE. A hardbound book opened flat, seen from the front and a little above
// (about 35 degrees down). Structure from knowledge of bound books: two page blocks rising from
// a sewn spine, each leaf curling up toward the gutter and dipping into it, the text block's
// fore-edges showing as a thin striped band at the bottom corners, the boards (covers) a margin
// wider than the pages all round, the spine's hinge a shadowed valley. Two columns of script on
// each page read as faint rows. The star is a four-point sparkle with a soft halo, the light
// source: pages brighten toward it and toward the gutter's rim that faces it, the gutter floor
// stays dark, the far corners fall off.
import { fractal } from "./core";

export const BOOK = {
  CX: 0.5,        // spine x, fraction of W
  X0: 0.13,       // outer edge of the left page
  X1: 0.87,       // outer edge of the right page
  TOP: 0.64,      // page top edge at the outer corners, fraction of H
  BOT: 0.9,       // page bottom edge at the outer corners
  RISE: 0.16,     // how far the leaves curl up near the gutter (fraction of H)
  STAR: [0.5, 0.2] as [number, number],
};
export type SpellTone = { raster: Float32Array };

const clamp = (v: number, a = 0, b = 1) => (v < a ? a : v > b ? b : v);
const smooth = (a: number, b: number, x: number) => { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); };

// a leaf's height above the table across the page: 0 at the outer edge, rising to the curl near
// the gutter, then dropping into the gutter valley (p: 0 = gutter, 1 = outer edge)
const curl = (p: number) => Math.pow(1 - p, 1.25) * (1 - 0.35 * Math.exp(-p * 60));

// mode 'light' (default): tone is light, for gold ink on the dark panel. mode 'shade': tone is
// shadow, for dark ink on a paper page - the star is left as bare paper with its rays and a
// dotted halo ring around it, lit leaves are pale, the gutter, script and boards dark.
export const spellbookTone = (W: number, H: number, mode: "light" | "shade" = "light"): SpellTone => {
  const shade = mode === "shade";
  const raster = new Float32Array(W * H);
  const cx = BOOK.CX * W, x0 = BOOK.X0 * W, x1 = BOOK.X1 * W, sx = BOOK.STAR[0] * W, sy = BOOK.STAR[1] * H;
  const half = (x1 - x0) / 2;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let t = 0;
    const dx = x - sx, dy = y - sy, d = Math.hypot(dx, dy);

    // ---- the star: core, four rays (long vertical, shorter horizontal), halo
    const core = Math.exp(-(d * d) / (2 * (0.012 * W) ** 2));
    const rayV = Math.exp(-(dx * dx) / (2 * (0.0035 * W) ** 2)) * Math.exp(-Math.abs(dy) / (0.12 * H));
    const rayH = Math.exp(-(dy * dy) / (2 * (0.0035 * W) ** 2)) * Math.exp(-Math.abs(dx) / (0.09 * W));
    const diag = (() => { const u = (dx + dy) / Math.SQRT2, v = (dx - dy) / Math.SQRT2; return (Math.exp(-(u * u) / (2 * (0.0025 * W) ** 2)) * Math.exp(-Math.abs(v) / (0.035 * W)) + Math.exp(-(v * v) / (2 * (0.0025 * W) ** 2)) * Math.exp(-Math.abs(u) / (0.035 * W))) * 0.55; })();
    const halo = 0.3 * Math.exp(-d / (0.045 * W)) + 0.08 * Math.exp(-d / (0.1 * W));
    if (!shade) t = Math.max(t, clamp(core * 1.2 + rayV * 0.9 + rayH * 0.75 + diag + halo));
    else {
      const hollow = 1 - Math.exp(-(d * d) / (2 * (0.022 * W) ** 2));      // the bright core stays bare paper
      const ring = Math.exp(-((d - 0.06 * W) ** 2) / (2 * (0.008 * W) ** 2));
      t = Math.max(t, clamp(0.9 * (rayV + 0.85 * rayH + diag) * hollow + 0.14 * Math.exp(-d / (0.05 * W)) * hollow + 0 * ring));
    }

    // ---- the trail: motes of light rising from the gutter to the star, thinning as they climb
    const pageTopAtSpine = (BOOK.TOP - BOOK.RISE) * H;
    if (y > sy && y < pageTopAtSpine + 0.02 * H) {
      const f = (y - sy) / (pageTopAtSpine - sy), wob = Math.sin(f * 9) * 0.012 * W * f, w = 0.01 * W + 0.02 * W * f;
      const across = Math.exp(-((x - cx - wob) ** 2) / (2 * w * w));
      const motes = clamp((fractal(314, x, y, 0.09, 0.09, 2) - 0.52) * 7);
      t = Math.max(t, across * (0.1 + 0.75 * motes) * (1 - 0.5 * f));
    }

    // ---- the book
    const bm = 0.022 * W;                                    // the boards stand this far proud of the pages
    if (x > x0 - bm && x < x1 + bm) {
      const side = x < cx ? -1 : 1, pr = Math.abs(x - cx) / half, p = clamp(pr), c = curl(p);
      const topY = (BOOK.TOP - BOOK.RISE * c) * H, botY = (BOOK.BOT - 0.35 * BOOK.RISE * c) * H;
      const boardTop = topY + 0.03 * H, boardBot = botY + 0.045 * H;
      const onPage = y >= topY && y <= botY && pr <= 1;
      if (onPage) {
        // the leaf's slope across the page gives its facing: toward the star near the curl's rise
        const e = 0.01, slope = (curl(clamp(p + e)) - curl(clamp(p - e))) / (2 * e);   // + when rising toward the gutter... per unit p
        const facing = clamp(0.55 - 0.35 * slope);                                      // the rise toward the gutter faces up and in: toward the star
        const v = (y - topY) / (botY - topY);
        const toStar = 1 / (1 + ((x - sx) ** 2 + (y - sy) ** 2) / (0.33 * W) ** 2);
        let lum = 0.06 + 0.95 * toStar * facing * (1.15 - 0.45 * v);
        // the gutter valley: the leaves turn away and meet in shadow
        lum *= smooth(0.0, 0.025, p) * 0.8 + 0.2;
        // two columns of script: faint darker rows, broken into words
        const col = p > 0.12 && p < 0.88 && Math.abs(p - 0.5) > 0.035;
        const rows = 15, rv = v * rows, inRow = rv % 1 > 0.35 && rv % 1 < 0.62 && v > 0.1 && v < 0.93;
        const word = fractal(55 + side, x, Math.floor(rv) * 13, 0.06, 0.5, 2) > 0.38;
        if (col && inRow && word) lum *= 0.55;
        // a small drawn sigil on the right page: a ring with a star inside
        if (side > 0) { const gx = cx + 0.52 * half, gy = topY + 0.42 * (botY - topY), r = Math.hypot(x - gx, (y - gy) * 1.35), ring = Math.abs(r - 0.055 * W) < 0.004 * W; if (ring) lum *= 0.4; }
        // edges of the leaf: a hair of shade at the top and outer edge
        if (y - topY < 0.006 * H || p > 0.985) lum *= 0.6;
        t = Math.max(t, shade ? clamp(0.06 + 0.85 * (0.85 - clamp(lum))) : clamp(lum));
      } else if (y > botY && y <= botY + 0.024 * H && p > 0.03 && pr <= 1) {
        // the text block's fore-edge at the bottom: thin stripes of page ends
        const stripes = 0.5 + 0.5 * Math.sin((y - botY) * 1.1);
        t = Math.max(t, shade ? (stripes > 0.5 ? 0.55 : 0.12) : stripes > 0.5 ? 0.38 * (1 - p * 0.35) : 0.03);
      } else if (y >= boardTop && y <= boardBot + 0.004 * H) {
        // the boards: dark leather, a rim of light along the edge nearest the star
        const rim = Math.min(boardBot - y, x - (x0 - bm), x1 + bm - x), lit = 1 / (1 + ((x - sx) ** 2 + (y - sy) ** 2) / (0.5 * W) ** 2);
        t = Math.max(t, shade ? (rim < 0.005 * W ? 0.35 : 0.62 - 0.2 * lit + 0.06 * fractal(7, x, y, 0.05, 0.05, 2)) : rim < 0.005 * W ? 0.2 + 0.25 * lit : 0.07 + 0.08 * lit + 0.04 * fractal(7, x, y, 0.05, 0.05, 2));
      }
    }
    raster[y * W + x] = t;
  }
  return { raster };
};
