# SR archetype icons -> AS2 point data

Turns Spell Research Prisma's archetype icons (`tools/icons/skald-icons.js` in
the SpellResearchPrismaUI repo: SVG markup on a 24-unit grid, one colour, round
strokes 1.3 wide) into polylines that SL_MagicMenu_SpellResearch draws with the
Scaleform AS2 drawing API (`lineStyle` / `moveTo` / `lineTo`, `beginFill` /
`endFill` for filled shapes).

Output: `plugins/SL_MagicMenu_SpellResearch/src/ArchetypeIcons.h` (generated,
do not edit). It holds a flat point array, a subpath table
`{first, count, closed, fill}`, and an icon table indexed by archetype 0-59
(range in the subpath table), plus `kIconGrid = 24` and `kIconStroke = 1.3`.

## Regenerate

Node 18+, no npm packages. Always convert SR Prisma's committed file:

```bash
SR=D:/DEV/SpellResearchPrismaUI
TMP=$(mktemp -d)
git -C "$SR" show HEAD:tools/icons/skald-icons.js > "$TMP/skald-icons.js"
node tools/sr-icons/convert.mjs "$TMP/skald-icons.js" "$(git -C "$SR" rev-parse HEAD)"
node tools/sr-icons/preview.mjs "$TMP/skald-icons.js"    # writes tools/sr-icons/preview.html
```

`convert.mjs` prints, per icon, the subpath and point counts and the largest
distance between the converted polyline and the exact outline (sampled at 1/100
of the tolerance, both directions), and exits non-zero if that passes the
tolerance plus rounding. The commit hash goes into the header comment.

`preview.html` shows each icon twice, SR's SVG on the left and the polylines
read back from `ArchetypeIcons.h` on the right (dark background, `#cfc9b8`).
Open it in a browser after every regeneration and compare.

## What is converted

- Archetypes 2-35 (Elements, ring frame), 41-44 (Targeting, ring with ticks)
  and 45-59 (Techniques, rounded square), frame included: 53 icons.
- Not converted, empty entries in the icon table: 0-1 (Casting) and 36-40
  (Schools). The menu does not show them.
- SVG handled: `path` (M L H V C S Q T A Z, absolute and relative), `circle`,
  `ellipse`, `rect` (with `rx`/`ry`), `line`, `polyline`, `polygon`,
  `transform` (rotate, translate, scale, matrix), `stroke-dasharray`, and
  `fill` / `stroke` of `none` or `currentColor`. Anything else (another
  element, attribute, colour, path command or transform) stops the converter
  with an error naming the archetype and element; nothing is skipped silently.
- Dashed strokes (archetype 4, Apparitions) are cut into one open subpath per
  dash, the pattern restarting at each subpath as browsers draw it.
- Identical point runs (the frames) are stored once and shared.
- The icon file runs in a `node:vm` context that has only an empty `window`
  object. That is isolation from mistakes, not a security sandbox: only feed it
  SR Prisma's own file.

## Tolerance

Curves, arcs, circles and ellipses are flattened so the polyline stays within
0.05 grid units of the true outline (Béziers by recursive subdivision of the
control polygon, arcs by angle step from the sagitta), then rounded to 2
decimals. At 24 px that is 0.05 px; the worst measured deviation is about
0.051 units.

## Licence

The converted drawings are Spell Research Prisma's own icons, drawn for that
project by the same user who maintains this one. The five school
sigils (archetypes 36-40) are traced from Skyrim's skill icons via UESP; they
are not converted and are not part of the generated header.
