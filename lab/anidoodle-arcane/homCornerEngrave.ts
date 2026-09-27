import { arcaneCornerTone } from "./arcaneCornerTone";
import { engraveStill } from "./homEngraveKit";

// HOM CORNER ENGRAVE · the Arcane parchment page's corner ornament (compass rose, ruled border,
// vines) as one Mellan spiral in sepia (homEngraveKit.ts). The spiral is born at the rose's pivot,
// the ornament's focal point, so its turns ring the rose and sweep out along both vines. Shade
// semantics: a wider line is more sepia; bare paper is no line at all. Transparent sheet.

export const homCornerEngrave = engraveStill("homCornerEngrave", "Heart of Magic · Arcane corner engraved", 360, 360,
  (W, H) => arcaneCornerTone(W, H),
  { ink: "#4a3012", pitch: 3.4, origin: [0.17, 0.17], gamma: 1.5, detail: "max", sharp: [[3, 1.0], [12, 0.4]] }, "corner");
