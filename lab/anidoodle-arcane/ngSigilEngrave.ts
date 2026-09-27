import { arcaneSigilTone } from "./arcaneSigilTone";
import { engraveStill } from "./homEngraveKit";

// NG SIGIL ENGRAVE · the magic circle in the tree's hub seal, for Heart of Magic's "Night
// Grimoire" design: gold ink on a deep indigo page, as one Mellan spiral (homEngraveKit.ts).
//
// The circle is flat line art (arcaneSigilTone.ts: rings, rune band, pentagram with a node per
// school, the ringed core, a faint drift of motes), and on a dark page the line IS the ink, so the
// field is printed as it stands: a wider turn is more gold. The spiral is born at the core and
// runs out concentric with the rings, so each ring is carried by the turns that lie along it,
// and the pentagram, the pentagon and the rune strokes are the places where the burin swells as
// it crosses them. detail "max": the fine detail here is MORE ink (a hairline, a rune stroke),
// so every turn takes the brightest sample of its band and no thin line falls between turns.
// Transparent sheet: the indigo is the panel's own.

// The field is re-weighted for the burin, never redrawn. Lines (tone 0.3 and up) are lifted so
// the pentagon's lighter line still survives the turns that cross it at a slant, while the order
// of weights (outer ring > rune strokes > inner ring > hairlines > pentagon) is kept. The motes
// (the faint wash inside the inner ring, 0.05 at the ring rising to 0.12 at the core) would print
// as sub-pixel hairlines on every turn and shimmer into moire; instead they become a glow that
// the spiral carries only near the core, where the line is born, and that runs out to nothing
// half way to the ring.
const LINE = 0.3, MOTE_RIM = 0.08, MOTE_CORE = 0.12, GLOW = 0.3;
const reweigh = (t: number): number =>
  t >= LINE ? 0.35 + 0.65 * t : GLOW * Math.max(0, (t - MOTE_RIM) / (MOTE_CORE - MOTE_RIM)) ** 1.5;

export const ngSigilEngrave = engraveStill("ngSigilEngrave", "Heart of Magic · Night Grimoire sigil engraved", 512, 512,
  (W, H) => arcaneSigilTone(W, H).map(reweigh),
  { ink: "#d9b35e", pitch: 3.2, origin: [0.5, 0.5], gamma: 1.0, detail: "max", sharp: [[3, 0.8], [14, 0.3]] }, "sigil");
