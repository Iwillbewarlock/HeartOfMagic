import { engraveStill } from "./homEngraveKit";
import { BOOK, spellbookTone } from "./spellbookTone";

// NG EMPTY ENGRAVE · the "no tree yet" plate for Heart of Magic's "Night Grimoire" design: the
// open grimoire with a four-point star lifting out of its gutter, engraved in one unbroken Mellan
// line (homEngraveKit.ts) in the design's gold on its deep indigo page. It is the Arcane gold
// plate (homEngrave.ts) re-inked: spellbookTone's "light" mode, where a wider line is more light,
// the spiral born at the star so the light is where the line begins. Drawn at 960x640 and
// delivered at half size (still --scale 0.5, 480x320). Transparent sheet.
//
// One change from the Arcane plate, for the size it is shown at: the turns are set wider (5.2
// instead of 4.6), so at half size they sit 2.6 output pixels apart and still resolve as lines;
// at 4.6 they fall to 2.3 px and the pages alias into a woven moire that hides the script rows.
// Gamma follows a little (1.35), to keep the leaves' mid tones from filling in.

export const ngEmptyEngrave = engraveStill("ngEmptyEngrave", "Heart of Magic · Night Grimoire grimoire engraved", 960, 640,
  (W, H) => spellbookTone(W, H, "light").raster,
  { ink: "#d9b35e", pitch: 5.2, origin: BOOK.STAR, gamma: 1.35 }, "grimoire");
