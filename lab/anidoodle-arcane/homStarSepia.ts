import { stippleStill } from "./homStippleKit";
import { spellbookTone } from "./spellbookTone";

// THE RISING STAR · stipple in sepia, for the Arcane design's parchment page (the empty tree
// screen). Same scene and tone field as homStipple, in the ink the Arcane tree draws its lines
// with, so the plate and the tree look printed by one hand. On paper the ink is shadow, not light,
// so the tone field is used as-is only for the star (a printed star is dense dots) and the book's
// lit leaves read by their darker surround: see the note in spellbookTone.
export const homStarSepia = stippleStill("homStarSepia", "The rising star · sepia stipple", 960, 640, (W, H) => spellbookTone(W, H, "shade").raster, { ink: "#4a3012", seed: 20260927 });
