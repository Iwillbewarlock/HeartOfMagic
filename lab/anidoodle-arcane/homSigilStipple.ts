import { stippleStill } from "./homStippleKit";
import { arcaneSigilTone } from "./arcaneSigilTone";
// Arcane hub emblem: the magic circle of the five schools, sepia stipple (see arcaneSigilTone.ts).
export const homSigilStipple = stippleStill("homSigilStipple", "Arcane sigil · stipple", 512, 512, arcaneSigilTone, { ink: "#4a3012", seed: 1103 });
