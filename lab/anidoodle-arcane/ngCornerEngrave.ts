import { arcaneCornerTone } from "./arcaneCornerTone";
import { engraveStill } from "./homEngraveKit";

// NG CORNER ENGRAVE · the page-corner ornament (compass rose, ruled border, vines ending in
// curls) for Heart of Magic's "Night Grimoire" design: gold ink on deep indigo, as one Mellan
// spiral (homEngraveKit.ts), drawn at its shown size, 200 px.
//
// Gold catches the light, so the tone is the corner field in its light-ink reading
// (arcaneCornerTone(W, H, true)): the upper-left flank of every vine and leaf and the lit half of
// each rose point carry the most ink, the far flank the least, and there is no cast shadow (on a
// dark page a shadow is no gold at all). The spiral is born at the rose's pivot, the ornament's
// focal point, so its turns ring the rose and sweep out along both vines. Transparent sheet.
//
// Tuned at 1:1: a tight pitch (2.2 px) puts five or six turns across a vine's belly, and a steep
// gamma (1.7) opens the gap between the lit flank and the far one, so the vines read round.

export const ngCornerEngrave = engraveStill("ngCornerEngrave", "Heart of Magic · Night Grimoire corner engraved", 200, 200,
  (W, H) => arcaneCornerTone(W, H, true),
  { ink: "#d9b35e", pitch: 2.2, origin: [0.17, 0.17], gamma: 1.7, detail: "max", sharp: [[2, 1.0], [6, 0.4]] }, "corner");
