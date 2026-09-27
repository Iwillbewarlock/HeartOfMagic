# Arcane illustrations (anidoodle sources)

The drawings the Arcane design ships in `PrismaUI/views/SpellLearning/SpellLearningPanel/themes/arcane/`
were drawn in code with [anidoodle](https://github.com/alexgreensh/anidoodle) (Apache-2.0). These are
their sources: tone fields (what the picture is) and plates (how the pen puts it down).

| File | What |
|---|---|
| `spellbookTone.ts` | the open grimoire with a star rising from it; `mode "light"` (gold on dark) or `"shade"` (ink on paper) |
| `arcaneShapes.ts` | ribbon/Bezier helpers for the ornaments |
| `arcaneCornerTone.ts` | page corner: compass rose, ruled border, vines (shade) |
| `arcaneHeartTone.ts` | the hub's faceted heart gem (shade) |
| `homStippleKit.ts` | pen-and-ink stipple on a transparent sheet, from any tone field |
| `homEngraveKit.ts` | single-line (Mellan) engraving from any tone field |
| `homHeartStipple.ts`, `homCornerStipple.ts`, `homStarSepia.ts` | **shipped**: heart.png, corner.png, empty.png |
| `homStipple.ts`, `homEngrave*.ts`, `homCornerEngrave.ts`, `homHeartEngrave.ts` | alternatives (gold stipple; engraved versions) |

## Re-rendering

```bash
node <anidoodle skill>/engine/tools/scaffold.mjs D:/DEV/anidoodle-art
cd D:/DEV/anidoodle-art && npm install && npx playwright-core install chromium
# copy these .ts files into src/canvas-core/, and for each plate add src/hosts/page-<name>.ts:
#   import { <name> } from "../canvas-core/<name>"; import { mountFilm } from "./page"; mountFilm(<name>);
node tools/still.mjs homHeartStipple  --out <panel>/themes/arcane/heart.png            # 512x512
node tools/still.mjs homCornerStipple --out <panel>/themes/arcane/corner.png           # 200x200
node tools/still.mjs homStarSepia     --out <panel>/themes/arcane/empty.png --scale 0.5  # 480x320
```

Each render prints `reproducible` with its md5: the same source gives the same pixels on any machine.
Shipped hashes (2026-09-27): heart `0fb5e805af043e7e34ad7c8ab8a4b64f`, corner `5a10515dce17a7fdbf4084b1b4c5e5c5`,
empty `20fa763a02cd6dac45854192a00c7c77`.
