# Magic menu checks

The offline checks of the magic menu (`plugins/SL_MagicMenu`), its Heart of Magic block (`plugins/SL_MagicMenu_HoM`)
and its Spell Research block (`plugins/SL_MagicMenu_SpellResearch`), moved here from CLAUDE.md (2026-11-04). The
harnesses are built by `BuildRelease.ps1` into `build/tools/Release/` (targets in `tools/magicmenu.cmake`).

## The release zips

`node tools/release-zips-check.js [--dist <dir>] [--hom <ver>] [--mmr <ver>] [--sr <ver>]` (after a build; versions from
  `VERSION` and the top-level CMakeLists.txt): who ships the magic menu blocks (docs/magicmenu/BLOCKS_MOVE.md) - the MMR zip
  and its symbols without a block file, the HoM zip with `MagicMenuRewrite_HoM*` only under `optional/MagicMenuRewriteBlocks/`
  and a FOMOD option for it (Recommended with `MagicMenuRewrite.dll`, never required or conditional), its symbols with
  `MagicMenuRewrite_HoM.pdb`, the Spell Research block package complete; exit 1 on a failure or a missing zip

## The harnesses

The magic menu prototype's (plugins/SL_MagicMenu, docs/superpowers/specs/2026-10-26-magic-menu-blocks-design.md):
  `magic-menu-layout-check` (the block layout, the add-on registry, and the core alone: the default layout with no add-on,
  the retired ids skipped; `[Blocks] sHidden`; a block away and back - an old layout file with tree / progress / nodecard and
  sHidden read with the HoM block, without it, saved, read with it again: the same places and hidden blocks), `magic-menu-preview-check` (the preview math) and
  `magic-menu-block-api-check` (Block API v3: registration parsing of v1, v2 and v3 bytes - cut short, version 4 refused,
  v3's flags, pad and legacy ids -, the tabs mask per SkyUI tab, the press / release cancel rules of `BlockRules.h`, the
  'SLMD' ids, the 'SLMH' height requests - reading, judging against the registered height, the layout mode's height, the
  per-id queue -, the 'SLMS' / 'SLMV' messages and their queues, the key queue, the 'SLMK' key hints - reading, labels cut at
  a UTF-8 boundary, the per-block list, the strip's buttons per tab / layout mode / gamepad, the panel's growth -, the v3 content box, visible part and
  overlay clears, the opening height of 2026-11-03 - a block that asks again after its first draw opens the next menu at its last asked
  height, never above the registered one, put back when it stops asking, the layout mode at the registered height - and
  `Context::columnRoom` at 16:9 / 16:10 / 21:9 / 4:3 and any scroll; `--selftest` the same); the Heart of Magic add-on block's (plugins/SL_MagicMenu_HoM,
  docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md), which build from plugins/SL_MagicMenu_HoM:
  `magic-menu-tree-check` (`--selftest`, or a spell_tree.json - it also prints how long `TreeData::Load` takes and the
  nodes' element / kind traits), `magic-menu-homstyle-check` (`--selftest`, or HoM's
  config) and `magic-menu-hom-block-check` (the block's keys read from MagicMenuRewrite.ini - the shipped one and made-up
  ones -, the registered heights, the pick rules, the strings, the cards' height requests and the tree window / progress
  card / node card arithmetic, and the progress card's next-spell rows of 2026-11-03, `ChoiceRules.h` - their order (the
  last target's children, then the spells akin to it: a shared element, a kind on at most a fifth of the school when not of
  different elements), the rows
  that fit with the "N more" line, the rooms at four screen shapes, the registered height, `[Progress] iMaxChoices`;
  `--selftest` the same); `magic-menu-lang-check` (both DLLs' language files: the core's
  `WidgetStrings.h` and the HoM block's `Strings.h` tables against their `dist/lang/en.json` and `ko.json` - every key, no
  extra key, the files parse, `{placeholders}` and the `" · "` separators equal in en, ko and the built-in English, the
  built-in text equal to en.json - and the shared `plugins/SL_MagicMenu/src/LangFiles.h`: `[Display] sLanguage` of
  MagicMenuRewrite.ini + the game's sLanguage -> the code (auto + KOREAN -> ko, else Hangul in a localized game string - the
  magic skills' names - -> ko, else en; `HasHangul` on valid, CP949 and broken UTF-8), the chain `<code>.json`,
  en.json, built-in on the shipped and on made-up folders, the one load per session, the log lines, and the shipped ini's
  `sLanguage=auto`; `--selftest` the same); and the Spell Research add-on block's
  (plugins/SL_MagicMenu_SpellResearch, docs/superpowers/specs/2026-10-27-spell-research-addon-design.md)
  `magic-menu-sr-block-check` (`SrBlockRules.h`: archetype grouping and order, what the block shows per state, the list and
  line layouts and their wrapping, the unknown archetype's frame-only icon, the icon data's bounds, the link click rule,
  reply code -> notification, the open request's requestId matching and 5 s drop, line overlap; `SrBlockText.h`: the
  `[MagicMenuBlock] sLayout` and `[Display] sLanguage` ini reading (BOM too), the game language mapping, archetype short /
  name fallback, and `dist/lang/en.json` against the block's keys; `--selftest` the same, `--lang <file>` another language file) -
  PASS/FAIL lines, exit 1 on a failure
