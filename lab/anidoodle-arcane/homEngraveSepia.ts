import { engraveStill } from "./homEngraveKit";
import { spellbookTone } from "./spellbookTone";

// HOM ENGRAVE SEPIA · the same grimoire and star, for the Arcane design's parchment page. Now the
// engraving is printed the ordinary way: dark sepia ink on a light page, so a WIDER line means
// MORE SHADOW (spellbookTone's "shade" mode). The star's core is bare paper, its rays and a faint
// halo are inked around it, the lit leaves stay pale, the gutter and the boards go dark. The
// spiral is still born at the star, so the light is where the line begins, and it starts bare.
// Transparent sheet: the parchment is the panel's own.

export const homEngraveSepia = engraveStill("homEngraveSepia", "Heart of Magic · the grimoire engraved in sepia", 960, 640,
  (W, H) => spellbookTone(W, H, "shade").raster,
  { ink: "#4a3012", pitch: 4.6, origin: [0.5, 0.2], gamma: 1.9, detail: "max" }, "grimoire");
