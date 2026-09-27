import { arcaneHeartTone } from "./arcaneHeartTone";
import { engraveStill } from "./homEngraveKit";

// HOM HEART ENGRAVE · the faceted heart gem at the Arcane tree's hub, as one Mellan spiral in
// sepia (homEngraveKit.ts). The spiral is born at the heart's centre, so the gem is drawn from the
// inside out and every facet is a band of turns whose weight is that facet's tone. Shade
// semantics: a wider line is more sepia; the glint is bare paper. Transparent sheet.

export const homHeartEngrave = engraveStill("homHeartEngrave", "Heart of Magic · Arcane heart engraved", 512, 512,
  (W, H) => arcaneHeartTone(W, H),
  { ink: "#4a3012", pitch: 3.6, origin: [0.5, 0.5], gamma: 1.5, detail: "max", sharp: [[3, 1.0], [14, 0.4]] }, "heart");
