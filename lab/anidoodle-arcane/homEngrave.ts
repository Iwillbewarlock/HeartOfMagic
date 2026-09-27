import { engraveStill } from "./homEngraveKit";
import { BOOK, spellbookTone } from "./spellbookTone";

// HOM ENGRAVE · the grimoire and its star, in one unbroken line. The empty-state plate for Heart
// of Magic's "Arcane" design: an open spellbook on a dark desk, a four-point star lifting out of
// its gutter as the only light.
//
// MEDIUM. Claude Mellan's single-line engraving (homEngraveKit.ts, after mellan.ts): ONE
// Archimedean spiral, never lifted, whose weight alone makes the picture. Here the plate is
// printed in reverse: the ink is gold and the "paper" is the panel's own dark brown, so a WIDER
// line means MORE LIGHT. Where the scene is dark (the desk, the gutter floor) the burin runs out
// to nothing and the panel shows through. The sheet is left transparent.
//
// ORDER AND IDEA. The spiral starts at the star, exactly as Mellan's starts at the tip of the
// nose: the light source is where the line is born, and every turn carries it out across the
// book. Tone comes from the shared field in spellbookTone.ts (also printed by the stipple plate).
//
// ONE INK. An engraving prints one colour from one plate, so the star core is not a second
// colour: it reads brightest only because the line there swells to its full width.

export const homEngrave = engraveStill("homEngrave", "Heart of Magic · the grimoire engraved", 960, 640,
  (W, H) => spellbookTone(W, H).raster,
  { ink: "#d9b56e", pitch: 4.6, origin: BOOK.STAR, gamma: 1.25 }, "grimoire");
