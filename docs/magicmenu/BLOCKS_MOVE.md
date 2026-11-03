# The blocks move to their mods (Magic Menu Rewrite 1.2.0, Heart of Magic Librarian 1.9.0)

Decided 2026-11-04: Magic Menu Rewrite (MMR) is the framework - the magic menu, the layout mode and the Block API -
and each block ships with the mod it belongs to.

| Block | DLL (unchanged name) | Shipped by, up to now | Shipped by, from now |
|---|---|---|---|
| Heart of Magic (spell tree window, learning progress card, node card) | `SKSE/Plugins/MagicMenuRewrite_HoM.dll` + `MagicMenuRewrite_HoM/lang/` | MMR 1.0.0 - 1.1.0, FOMOD folder `10 Heart of Magic Block` | Heart of Magic Librarian 1.9.0, FOMOD page "Magic Menu Rewrite" (`optional/MagicMenuRewriteBlocks/`) |
| Spell Research (archetypes, links to Spell Research's windows) | `SKSE/Plugins/MagicMenuRewrite_SpellResearch.dll` + `.ini` + `MagicMenuRewrite_SpellResearch/lang/` | not released | Spell Research - Prisma UI 2, which takes `dist/SpellResearchBlock_<version>/` from this repo's build |

The sources stay in this repository (`plugins/SL_MagicMenu_HoM`, `plugins/SL_MagicMenu_SpellResearch`): one build,
the host's shared headers and `GameThread.cpp`, the harnesses. Only who ships the DLL changes
(`plugins/cmake/Distribution.cmake`: section 2b for the HoM block, 9 for MMR, 10 for the SR block package). File
names, SKSE plugin names (log names), block ids, ini keys and lang paths are unchanged, so a player's
`MagicMenuRewrite.ini`, `MagicMenuRewrite_User.ini`, `MagicMenuRewrite_Layout.json` and `[Blocks] sHidden` keep
working. The HoM block still reads its keys from `MagicMenuRewrite.ini`, which MMR 1.2.0 still ships with them.

## Versions

- The HoM block DLL carries Heart of Magic's version (`HOM_VERSION` = `VERSION`), its `.pdb` is in HoM's
  DebugSymbols zip, its notice in HoM's `THIRD-PARTY-NOTICES.md` (source tag `v<version>`).
- The SR block DLL carries `SPELL_RESEARCH_BLOCK_VERSION` (top-level `CMakeLists.txt`; 2.0.0 is a placeholder until
  the Spell Research - Prisma UI 2 release is numbered), its notice names the source tag
  `spell-research-block-v<version>` (see "Releasing the Spell Research block" below).
- MMR: `MAGIC_MENU_VERSION` 1.2.0, the core and the Block API only (`MagicMenuRewrite.dll`).

## Releasing the Spell Research block

1. Set `SPELL_RESEARCH_BLOCK_VERSION` in the top-level `CMakeLists.txt` to the Spell Research - Prisma UI 2 version and
   commit it.
2. Build that commit (`BuildRelease.ps1`), run `node tools/release-zips-check.js`.
3. Copy `dist/SpellResearchBlock_<version>/` into the Spell Research - Prisma UI 2 package: `SKSE/` into its Data
   files, the notices, licence and `licenses/` beside its own, `DebugSymbols/` into its debug-symbols archive.
4. When that release goes out, tag the commit the package was built from `spell-research-block-v<version>` on this
   repository and push the tag: the notices point there for the Corresponding Source.

## Install order: what happens and how to fix it

"MMR 1.2.0" below means a clean install or an update that replaces the old files (MO2: Replace, Vortex: the normal
update). "1.1.0 left over" means MMR 1.1.0's `MagicMenuRewrite_HoM.dll` is still in the MMR mod folder (MO2: an
update installed with Merge, or a hand copy).

| Case | Result | Fix |
|---|---|---|
| MMR 1.2.0 first, then HoM 1.9.0 | The HoM installer finds `SKSE\Plugins\MagicMenuRewrite.dll`: the "Magic Menu Rewrite" option is picked (Recommended). The block works. | - |
| HoM 1.9.0 first, then MMR 1.2.0 | At HoM's install MMR was not there: the option was not picked (Optional). No HoM block in the menu; the preview and spell card work. | Reinstall HoM 1.9.0 (MO2: Reinstall) and pick "Heart of Magic block for Magic Menu Rewrite". |
| HoM 1.9.0 first while MMR 1.1.0 is installed, then MMR 1.2.0 | MMR 1.1.0's DLL counted: the option was picked. The block works after the MMR update. | - |
| MMR 1.2.0 + HoM older than 1.9.0 | The HoM block is gone (MMR 1.2.0 no longer ships it). Its place in the layout and its hidden setting are kept (`magic-menu-layout-check`, "a block away and back"). | Update HoM to 1.9.0 with the option. |
| HoM 1.9.0 with the option, MMR 1.2.0, MO2 priority HoM above (left pane higher) or below MMR | No file in common: priority does not matter. | - |
| 1.1.0 left over, HoM 1.9.0 with the option, HoM below MMR (HoM wins) | HoM 1.9.0's block is loaded; MO2 shows the conflict. | Optional: delete the leftover from the MMR mod folder (reinstall MMR 1.2.0 with Replace). |
| 1.1.0 left over, HoM 1.9.0 with the option, HoM above MMR (MMR wins) | MMR 1.1.0's copy of the block is loaded (the same code as HoM 1.9.0's today); a later HoM fix to the block would not reach the game. | Reinstall MMR 1.2.0 with Replace, or put HoM below MMR. |
| 1.1.0 left over, HoM 1.9.0 without the option | The 1.1.0 copy keeps working until it is removed. | Reinstall HoM 1.9.0 with the option, then reinstall MMR 1.2.0 with Replace. |
| HoM 1.9.0 option not picked, no leftover | No HoM block; nothing else changes. The layout keeps its place. | Reinstall HoM 1.9.0 with the option. |
| HoM option picked without MMR (or MMR's `bEnabled=0`) | The block DLL loads and does nothing: `MagicMenuRewrite_HoM.log` says "MagicMenuRewrite did not listen (not installed, or bEnabled=0) - no block ...". | Install MMR, or uncheck the option. |
| Vortex | The same files; Vortex asks for a rule only when the 1.1.0 copy is still deployed: let Heart of Magic Librarian win. | - |

The Spell Research block was never in an MMR release, so it has no leftover cases: it comes with Spell Research -
Prisma UI 2 and needs Magic Menu Rewrite to show anything.

## Checked by the build

- `node tools/release-zips-check.js` (after `BuildRelease.ps1`): the MMR zip has no block file and no install step,
  its DebugSymbols zip no block `.pdb`; the HoM zip has the block's three files only under
  `optional/MagicMenuRewriteBlocks/SKSE/Plugins/` and a FOMOD option for them (Recommended with
  `MagicMenuRewrite.dll`, Optional otherwise, never in `requiredInstallFiles` or `conditionalFileInstalls`), its
  DebugSymbols zip `MagicMenuRewrite_HoM.pdb`; the SR block package is complete (DLL, ini, en/ko, `.pdb`, notices,
  licence texts). Exit 1 on a failure.
- `magic-menu-layout-check` ("a block away and back"): an old `MagicMenuRewrite_Layout.json` with the retired ids
  `tree` / `progress` / `nodecard` and `[Blocks] sHidden` holding the block's ids, read with the block, without it,
  saved without it, read with it again: the same places and the same hidden blocks.

Not checked here: MO2's and Vortex's own install behaviour (the FOMOD's file test on a DLL, Replace / Merge, the
conflict view) and the game itself.
