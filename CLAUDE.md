# CLAUDE.md

Project instructions for Claude Code when working with Heart of Magic.

## What Is This Project?

Heart of Magic is an SKSE plugin (DLL) that transforms spell learning into an XP-based progression system for Skyrim SE/AE. It scans all spells in the player's load order, generates prerequisite trees with a native C++ Classic (tier-first) builder using NLP algorithms (TF-IDF, cosine similarity, fuzzy matching), and tracks learning progress through casting, tome study, and other XP sources.

**Key design**: All NLP runs natively in C++—no external Python or server dependencies. The build produces three release DLLs: `SpellLearning.dll` (main plugin), `DontEatSpellTomes.dll` (inert DEST compatibility shim), and `SL_BookXP.dll` (BookXP addon). The UI is rendered through PrismaUI (CEF/Ultralight-based web views).

## Documentation Decision Tree

**Find the right doc based on your task:**

```
What are you doing?
│
├─► Understanding the system?
│   ├─► Overall architecture → docs/ARCHITECTURE.md
│   ├─► Tree building (Classic builder) → docs/TREE_BUILDING_SYSTEM.md
│   └─► Design patterns & UI → docs/DESIGN.md
│
├─► Writing/modifying code?
│   ├─► Using RE/SKSE types? → Reference headers in plugins/external/commonlibsse-ng/
│   ├─► Module system? → docs/MODULE_CONTRACTS.md
│   └─► Public modder API? → docs/PLAN-PUBLIC-MODDER-API.md
│
├─► Working with PrismaUI frontend?
│   ├─► JS module structure → PrismaUI/views/SpellLearning/SpellLearningPanel/modules/README.md
│   └─► Translations → docs/TRANSLATING.md
│
├─► Working with presets?
│   └─► docs/PRESETS.md
│
├─► Working with the spell tag librarian (classification, spell_catalog.json)?
│   ├─► How it works, rule format → docs/librarian/LIBRARIAN.md
│   ├─► Tag definitions → docs/librarian/TAGS.md
│   ├─► Measured coverage and accuracy → docs/librarian/MEASURED.md
│   ├─► Perk overhaul keywords (adapters/*.json) → docs/librarian/PERK_ADAPTERS.md
│   ├─► Spell Research archetype lists (compat/spellresearch.json) → docs/librarian/SPELL_RESEARCH.md
│   └─► Why it exists, what is left → docs/librarian/Concept.md, docs/librarian/ToDo.md
│
├─► Working with HeartOfMagic.esp, explosion twins, environment mods (Frostwalker)?
│   └─► docs/ENVIRONMENT.md (record values, src/environment/, the 1.6 self-test)
│
├─► Working with DEST/ISL compatibility?
│   └─► docs/DEST-IMPROVEMENTS.md
│
├─► Working with BookXP addon?
│   └─► plugins/BookXP/README.md
```

## Critical Rules (MUST Follow)

1. **600 LOC limit**: Split files when approaching this limit
2. **RE/SKSE types**: ALWAYS read headers from `plugins/external/commonlibsse-ng/` before using
3. **No AI footers**: NEVER add "Generated with Claude" or similar attribution
4. **Explore before coding**: ALWAYS search for existing utilities before writing new helpers
5. **Build validation**: ALWAYS run `.\BuildRelease.ps1` before completing a task
6. **Documentation maintenance**: ALWAYS update relevant docs/ after code changes that affect documented systems
7. **PrismaUI theme compatibility**: UI changes must work with all designs (Classic, Modern Dark, Arcane, Night Grimoire, Chalkboard, Skald - `themes/skyrim.json` plus `themes/design-*.css`)
8. **JavaScript compatibility**: Use `var` declarations in PrismaUI code (Ultralight compatibility—no `let`/`const`)

## Do NOT (Anti-Patterns)

### Code Safety
- **NEVER** guess at RE/SKSE methods—always verify in header files first
- **NEVER** use raw `new`/`delete`—use smart pointers
- **NEVER** use manual `mutex_.lock()`/`unlock()`—use `lock_guard` or `unique_lock`
- **NEVER** hardcode magic numbers—use named constants

### Architecture
- **NEVER** exceed 600 lines in a single file
- **NEVER** create new singleton managers without understanding existing patterns
- **NEVER** bypass the existing hook patterns (SpellCastHandler, SpellTomeHook, SpellEffectivenessHook)

### Code Duplication
- **NEVER** copy-paste large blocks of code to create variants—extract shared logic into a common implementation
- **NEVER** create function overloads that duplicate logic—use a single implementation with parameters to control behavior
- When adding a new variant of an existing function:
  1. Extract the core logic into an `*Impl` function with parameters for the varying behavior, or have one override call the other with appropriate defaults
  2. Have both the original and new function call the shared implementation

### Documentation
- **NEVER** complete a task without checking if documentation needs updating
- **NEVER** add new public APIs, config options, or systems without documenting them
- **NEVER** change behavior documented in docs/ without updating those docs
- **NEVER** assume documentation updates can be done "later"—do them in the same session

### PrismaUI / JavaScript
- **NEVER** use `let` or `const` in PrismaUI JavaScript—Ultralight requires `var`
- **NEVER** use ES6 module syntax (`import`/`export`)—use the existing global/module pattern
- **NEVER** hardcode colors that don't respect the theme system—use theme variables

### Git Safety
- **NEVER** use `git reset --hard` when there are uncommitted changes—this destroys work permanently
- **NEVER** use destructive git commands without first running `git status` to check for uncommitted changes
- When moving commits to a new branch, use this safe pattern:
  1. `git status` — verify no uncommitted changes (or stash them first with `git stash`)
  2. `git branch new-branch-name` — create branch at current commit
  3. `git reset --soft HEAD~N` — move HEAD back, keeping changes staged (NOT `--hard`)
- If uncommitted changes exist and you need to reset, ALWAYS `git stash` first

## Sub-Agent Guidelines (Task Tool)

When launching sub-agents via the Task tool, choose the correct `subagent_type` based on the work needed:

| Need | Agent Type | Tools Available |
|------|-----------|----------------|
| Code exploration, search, reading | `Explore` | Read-only (Glob, Grep, Read, WebFetch) |
| Designing implementation plans | `Plan` | Read-only (same as Explore) |
| Running shell commands only | `Bash` | Bash only |
| **Implementation** (editing files, running commands) | `general-purpose` | **All tools** (Edit, Write, Bash, Read, etc.) |

### Rules
- **NEVER** use `Explore` or `Plan` agents for implementation—they cannot Edit, Write, or run Bash and will fail with permission errors
- **Prefer direct edits** over sub-agents when only a few files need changing. Sub-agents are best for multi-file, multi-step operations
- **Windows environment**: This project runs on Windows with Git Bash as the shell. Use Unix-style paths and syntax (forward slashes, `/dev/null`) in Bash tool calls. PowerShell scripts like `BuildRelease.ps1` must be run via `powershell.exe` or `pwsh`

## Build Commands

```powershell
# Default build (Visual Studio 2022, Release, x64)
.\BuildRelease.ps1

# Clean reconfigure + build
.\BuildRelease.ps1 -fresh

# Visual Studio 2026 preset
.\BuildRelease.ps1 -preset Release-2026

# Control parallelism (default: 32 threads)
.\BuildRelease.ps1 -threads 16
```

**ALWAYS use `BuildRelease.ps1`**—never run cmake directly. The script handles VS dev shell setup, configuration, and output paths.

The build also assembles the release: `dist/HeartOfMagic-Librarian_<VERSION>/` and its `.zip` (FOMOD, DLLs, Papyrus, runtime data, the PrismaUI views, `LICENSE`), and the DLLs' debug symbols apart in `dist/HeartOfMagic-Librarian_<VERSION>_DebugSymbols.zip` (`SKSE/Plugins/*.pdb`, an optional file on Nexus for crash logs, and `SKSE/Plugins/SpellLearning/debug.json` from `debug/`: with it the panel's full scan also writes `spell_scan_output.json`; `DeployDev.ps1` puts the same file in the dev install). The panel's development files stay out of it - the dev harness, `run-tests.js`, `test-runner.html`, `test-data/` and `modules/*Test.js` (`plugins/cmake/Distribution.cmake`; a new test module is picked up at the next configure). The magic menu ships apart with its own version (`MAGIC_MENU_VERSION` in the top-level CMakeLists.txt): `dist/SL_MagicMenu_<VERSION>/` and its `.zip`, the FOMOD of `fomod_magicmenu/` (`00 Core` with `SL_MagicMenu.dll` and its ini files, the `10 Heart of Magic Block` option with `SL_MagicMenu_HoM.dll`, `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/`), and its debug symbols in `dist/SL_MagicMenu_<VERSION>_DebugSymbols.zip` (target `assemble_magicmenu_dist`; the Spell Research block is not in it yet).

License files in the release: the project's own code is MIT (`LICENSE` at the zip root), but the DLLs statically
link CommonLibSSE-NG (GPL-3.0-or-later with the Modding and Linking Exceptions), so they ship as a GPL combined
work. `THIRD-PARTY-NOTICES.md` at the zip root (from `THIRD-PARTY-NOTICES.md.in`, version filled in at configure)
says so, lists every statically linked library with its license and points to the Corresponding Source (the
GitHub tag of the release). `licenses/CommonLibSSE-NG/` holds CommonLib's `COPYING.txt`, `EXCEPTIONS.md` and the
original CommonLibSSE `LICENSE-MIT.txt`; `licenses/<port>.txt` is each vcpkg library's copyright file. A new
statically linked library needs its port in `_vcpkg_license_ports` (Distribution.cmake) and a row in the notices;
for the magic-menu DLLs (SL_MagicMenu, SL_MagicMenu_HoM) also in `_mm_vcpkg_license_ports` (Distribution.cmake) and a
row in `fomod_magicmenu/THIRD-PARTY-NOTICES.md.in`.

### Deploying to the development mod folder

```powershell
.\DeployDev.ps1              # plugin + UI panel
.\DeployDev.ps1 -PanelOnly   # UI only, no rebuild needed
.\DeployDev.ps1 -WithISLPatch  # also the ISL patch's scripts; -WithBookXP the BookXP script
.\DeployDev.ps1 -NoScripts     # plugin + panel, the install's Papyrus scripts left as they are
.\DeployDev.ps1 -WithSelfTest  # also selftest.json: the in-game 1.6 self-test runs (any deploy without it removes it)
```

**ALWAYS use `DeployDev.ps1`**—never copy the panel by hand. It refuses to run while SkyrimSE is
open (PrismaUI holds the panel files, so a copy would leave the mod half written), it keeps the
dev install's `lang/locale.js` instead of resetting the language to the shipped `en`, and it
verifies the copy landed. Unless `-PanelOnly`, it also copies the compiled Papyrus scripts
(`build/Scripts/output/*.pex`, with their `.psc` in `Scripts/Source`) the way the release lays them out: the
core ones always, the BookXP script only when the install has `SL_BookXP.dll` (or the script) or with
`-WithBookXP`, the ISL patch's three only when the install already has the patch's script or with
`-WithISLPatch`, each checked by hash. Everything it will copy is checked before the first copy (the DLL, the
panel, each of those `.pex` present and newer than its `.psc`): a missing or stale build stops it with the install
untouched (`-NoScripts` deploys without the scripts on purpose). It copies the shipped design presets and removes only the ones the mod no
longer ships (a list in the script); any other design JSON in the install (an add-on being tried) stays.

### Local Build Configuration

Copy `Build_Config_Template.ps1` to `Build_Config_Local.ps1` and edit to set:
- `$defaultOutputPath` — where DLLs are copied after build (e.g., your MO2 mod folder)
- `$vsDevShellPath` — path to your VS dev shell script

`Build_Config_Local.ps1` is gitignored.

### Skyrim Process Check

**CRITICAL**: Before ANY build, check if Skyrim is running. If Skyrim has the DLL loaded, the build will fail because the file is locked.

```powershell
if (Get-Process -Name "SkyrimSE" -ErrorAction SilentlyContinue) {
    Write-Host "ERROR: Skyrim is running - cannot build (DLL is locked). Close Skyrim first."
    # DO NOT BUILD - inform user and skip
}
```

**If Skyrim is running:**
- Inform the user: "Skyrim is currently running. Cannot build because the DLL is locked. Please close Skyrim to proceed."
- Do NOT attempt the build—it will fail
- Continue with non-build work if possible

### Build Validation Workflow

```
1. Make changes
2. Check if Skyrim is running → if yes, inform user and skip build
3. Run .\BuildRelease.ps1
4. Fix any compilation errors
5. Only then is the task complete
```

## Testing

### JavaScript Tests

```powershell
# Run from the SpellLearningPanel directory
cd PrismaUI/views/SpellLearning/SpellLearningPanel
node run-tests.js
```

**Test files:**
| File | Purpose |
|------|---------|
| `run-tests.js` | Node.js test runner with browser-global mocks; every `loadModule`'d `*Test.js` (except `unificationTest`, run through `runAll`) is a suite - a new test file needs only its `loadModule` call; a module that fails to load, a suite that did not load (NOT RUN) or a suite that throws (THREW) fails the run (exit 1); it also fails when `dev-harness.html` is not what `tools/make-dev-harness.js` makes |
| `modules/unificationTest.js` | Shape profile and WheelRenderer checks (not loaded in game) |
| `modules/layoutDeclutterTest.js` | Tree declutter pass (tree spaced out, spells off lines, off each other, off the heart) |
| `modules/openRefreshGateTest.js` | Opening the panel repaints the tree only when the replies changed what it shows; a save load or tome read with the panel closed draws nothing until it opens |
| `modules/layerScrollTest.js` | Drag scroll of the tree layer (uncovered strips, their pieces, waiting strips moved, names moved with the picture, a name drawn past its strip onto a chapter title) and the whole repaint spread over frames (LayerBuild states) |
| `modules/canvasCullTest.js` | The tree renderer's culling index (the spells and lines a strip or piece of the tree layer looks at are exactly those the full loop draws, in the same order; the turns, hand variants and bows it keeps, made again when a school changes in place) |
| `modules/labelCullTest.js` | The spell names: candidates from the culling index = the full loop's (same spells, order, places over 140 views), at most 50 names below zoom 0.8 (`MAX_LABELS_FOCUS`, learning ones kept) and 150 above, none below 0.5, the selected spell in the layer's margin still named with the cap full (`renderLabels` and a dragged strip), the same names nearest the screen's middle kept when a spell comes in at the edge |
| `modules/layerFlowTest.js` | How a frame gets its tree layer: LayerScroll's shift and piece queue, and `_drawTree`'s order (scroll, build over frames, draw at once, stale builds dropped, glide builds, the restart cap, the urgent build), a new font's widths and letters made before the names, the font left cold by names drawn without outlines below zoom 0.8, the idle warm-up of every name's letters (`warmIdle`: a slice a frame, the font cold until done, restarted by a new font or tree or a name changed in place; the names as they show first, `???` included, then every name; code points) |
| `modules/labelHaloTest.js` | The names' outline (`labelHalo`): stroked at `TreeStyle.LABEL_HALO_MIN_ZOOM` (0.8) and above, not below; the warm-up makes the names' and titles' outlined letters at any zoom (ready for a zoom-in) |
| `modules/pageBuildTest.js` | The design page painted over frames (`TreeStyle.stepPage`): step order, bands, what stands in meanwhile, look changes, the idle timer; the corner drawings' size (96 - 112 px) and whether the tree's tools move past them (`hasCornerArt`: from the design's tokens, none without a page or a drawing; handed to `TreeToolsScale.setCornerArt` every frame) |
| `modules/statusLineTest.js` | The builder status line and the scan status bar over a language switch (keyed lines come back in the new language, unkeyed ones stay as written, a key the language lacks shows English or the line as written, a cleared tree goes idle - "Ready to build" once spells are scanned, also after a scan, but not over a built tree or a line someone wrote), the scan bar keeping out of sight what the Scan page's summary says (the scan count, Ready to scan, Scanning; errors and the build at game start's lines still show), the Easy page copy, `onScanFailed` (full scan: button back, reason in the bar), and the spell card's rounded Magicka cost |
| `modules/spellNamesTest.js` | Names in the spell card's lists (a shared name gets its plugin, a unique one does not, hidden names `???` with no plugin, SpellCache plugin before persistentId, counts after a new tree or invalidate) and TreeParser dropping self links |
| `modules/spellResearchStatusTest.js` | Settings > Spell Tome Learning > "Spell Research" line (`SpellResearchStatus`): hidden until the plugin sends a state, each state's text and numbers, one spell added in its own line (`srAddedOne`, "1 spell"; a language without it uses its own `srAdded` before English), English fallback, bad input hides it |
| `modules/spellCardWeakenedTest.js` | The spell card's "Weakened" badge and its power (`spellWeakenedPower`, said as "casts at N% power" on the state line): only for a spell the known spells list as early-learned (`state.weakenedSpells`, own or canonical id), its power from its XP by the power steps (the spell info's figure only without XP), none once mastered (node unlocked, progress unlocked - the cheat Unlock's shape too - or full power); the mastery paths drop the spell from the list (`ProgressUpdates.forgetWeakened`: a mastering progress update, onSpellUnlocked, the cheat Unlock); no inline colours on the card; `TreeParser.updateNodeFromCache` carries effectiveness and scaled effects |
| `modules/levelFilterTest.js` | The spell card's level as a tree filter (`level.<Level>` lights every spell of that level; pressable only when shown and counted), and bridges to spells the tree does not have left out |
| `modules/tomeFilterTest.js` | The tree's tome filter: spells marked `taughtByTome` false, `tomeUnreachable`, `voiceSlot` or `freeSpell` true, or with a `duplicateOf`, stay out of the tree build and the primed count; a scan without the marks keeps every spell |
| `modules/newSpellsTest.js` | The footer's new spells and next goals (`NewSpells`): new only between a close and the next opening, once, no edit-mode copies, a seen card drops the mark, a new tree resets, goals learning first then by XP (three, hidden schools out), goal names hide like the card's, marks capped |
| `modules/classicRadialOrderTest.js` | The Classic layout's radial order pass (`ClassicRadialOrder`): cost ranks per tier (ties, no cost, a tier of one), tier before cost, out-of-order neighbours trade places (cost within a tier, and across tiers), roots stay, no trade that stretches the lines past `ALLOW`, grid points stay distinct |
| `modules/prereqEditTest.js` | Edit mode's prerequisite editing (`PrereqEdit`, `EditMode`): a link goes into Required once, delete and the eraser take only that one, H/S moves without a second entry, undo of each (type, line, `softNeeded`), a move into an empty Optional list needing 1, deleting a spell (`deleteNode`: no reference left in the saved tree - children, locks, bridges - its undo entries dropped so Ctrl+Z after it links nothing to it and saves nothing, a school root refused), the repair of a tree saved by 1.2.0 (a PreReq Master lock on the parent lowers `softNeeded` instead), `copyTo`, and no two lists of a `ClassicTreeOutput` node sharing an array |
| `modules/notificationSettingTest.js` | Settings > Show Notifications (`NotificationSetting`): defaults, save/load into `config.notifications`, wrong or missing values, Reset to Defaults, the weakened alert controls disabled while it is off |
| `modules/panelOpenSettingTest.js` | Settings > Hotkey (`PanelOpenSetting`): None (`hotkeyCode` 0) through save and load, None by the code alone, a leftover `magicMenu` in the config ignored (None always pickable), wrong codes back to F8, the letter key warning, Reset to Defaults, no hotkey in a settings preset |
| `modules/starCadenceTest.js` | The starfield moves at most every `STAR_FRAME_MS` at the old speed (no star changes more than about one `OPACITY_STEP` per move), drawing is apart from moving, and a frame where only the heart beats does not repaint the tree layer |
| `modules/spellSearchTest.js` | Find Spell's matcher (`SpellSearch`): shown name, school (English and translated) and formId (only a formId-like text, "fe" is not one; after name matches), case and spaces ignored (Korean too), exact - prefix - rest - formId order alphabetical within each, the 80 cap, locked spells out in discovery mode and in with cheat mode or discovery mode off, the raw tree data (no states) listing nothing in discovery mode, the space-blind highlight range |
| `modules/treeToolsScaleTest.js` | The tree tool bar's size (`TreeToolsScale`): clamp 1x - 3x in 0.05 steps, the scale a drag gives, save/load, Reset to Defaults, saving only a finished drag, a lost release (a move with no button held, focus lost) ending the drag - the no-button check only when the press reported its button, so an engine sending buttons 0 on every event keeps dragging until the release; `setCornerArt` moving the tools right past corner drawings (left and max-width, top unchanged, the bar's room) and back, writing only on a change |
| `modules/learningTargetKeysTest.js` | A learning target found by spell id, not school key (`LearningTargetKeys`): a multi-school spell under another key than its node's, the canonical id, `onLearningTargetDropped` forgetting only a key that still holds that spell and then redrawing the learning paths, wheel marks and open spell card, the Learn button (cheat mode too), spell card and wheel learning mark using it and the clear sending the formId |
| `modules/gateReviewTest.js` | The spell gate's lists (`GateReview`): "review" and "out" tree candidates each in their list, not blacklisted ones (plugin:localFormId or formId) or kept ones (`gateReviewed`); "out" spells stay out of the tree unless included, an included one comes back; an old scan's `freeSpell` still leaves a spell out; reasons in words; plugin keys fold A-Z only (`foldPluginName`), the same bytes the DLL's `PluginKey` makes for Korean, Cyrillic and accented names; `migrateKeys` replaces a key saved before with `toLowerCase` once (not before the config is read, not for a configNotRead config, saved once, a new key or an unknown old key left alone) |
| `modules/i18nRelabelTest.js` | Text written with `setI18n` (i18n.js) comes back in the new language on a switch, with its values, as text or a title; text written after `clearI18n` stays; a missing key shows the English fallback; no `data-i18n` element in index.html holds another element (a switch rewrites its text and would drop the spell card's "need 2 of 3" count and LOCK badge) |
| `modules/stableIdsTest.js` | Saved settings that name spells after a load order shift: chosen roots sent with the spell's current formId (by plugin + localFormId, light plugins too), a root whose spell is gone left out, legacy roots and no-scan roots sent as saved; XP overrides by persistentId, legacy formId-keyed ones still read and replaced on the next set; the root picker's Current badge on the root's spell in this scan |
| `modules/homIconsTest.js` | `HomIcons` (homIcons.js): the 19 icon ids, slots painted from the design's icon folder, an unknown id left alone, a design without icons or a bad path falls back to the line set, `slot()` markup, `setLabel` writes the label span, not the button |
| `modules/iconSetsTest.js` | Every icon folder a design uses has its PNGs: the 19 ids in the line sets (neutral, gold) and each drawn design's set, and the ids a single CSS rule takes from a partial set (`themes/arcane/icons-ink/close.png`) |
| `modules/iconSlotsTest.js` | No bracket glyphs left: every icon in index.html is an empty `.hom-icon` slot with a known id, no slot sits inside an element a translation rewrites, and none of the 13 lang files holds an icon glyph |
| `modules/findSpellDismissTest.js` | The Find Spell dropdown going away by itself (`dismissFindSpell`): hidden from any tab, not "open" behind another tab (Escape goes on), and called by the tab switch, the close button, `onPanelHiding` and `loadTreeData` |
| `modules/homeViewTest.js` | The tree's home view (`TreeCamera.homeTarget`): the tree fitted into the part of the canvas no panel covers (bar under the tree, sidebar, open How-to-Learn panel), centred there, no closer than `TreeNav.HOME_ZOOM` and no further out than `HOME_MIN_ZOOM` (a real-size tree opens at that floor with the heart in the middle of the visible part), chapter titles fitted per axis (side titles take width, top and bottom titles height; the real titles all inside); `getPanelOffset` as before; the pending home view waits for a measurable tree and gives way to a view asked for after the load |
| `modules/previewFitTest.js` | The Scan page's tree previews (`TreePreviewUtils`, `TreePreviewSun`): a box not laid out leaves the canvas as it is (no 300 px stand-in), a shown one sets its size once; the fit holds roots, arrows and school names with `FIT_MARGIN`; no school name over a root |
| `modules/chapterTitlesTest.js` | Chapter titles (`TreeStyle._chapterPlaces`): clear of every spell under them, another school's outer ring included, at the side and on top, zoomed and turned, and still close to the tree; a hidden school's spells push no title out; none without titles |
| `modules/languageRefreshTest.js` | A live language switch (`refreshScriptTexts`): the tree's school tabs (short names, tooltips) and the Scan page's school list come back in the new language, a modded school keeps its name, the selected school stays marked, no tabs built without a tree |
| `modules/autoBuildTest.js` | The tree built at game start: no tree without spells is saved, `AutoBuild.decide` (no tree: build; load order changed: notice once; setting off: nothing), the run's steps (scan, silent build, apply, done) through save loads, and `TreePreviewHidden` giving the preview's output off screen equal to a drawn frame's (box measured, and box never laid out) |
| `modules/autoBuildFlowTest.js` | The build at game start against the panel's own code: a save load during the run, the waits on a fake clock, which scan feeds the run, the player's Build, Import or Apply winning over it, trees without spells refused |
| `modules/autoBuildGuardTest.js` | The guards round the build at game start: no debounce on its scan, a player's build makes it yield, a partly applied or unread (`configNotRead`) config builds nothing, its progress shown when the panel opens, Pre Req Master scores for a stale tree dropped, a tree loaded with the panel closed drawn when it opens, a scan that throws fails the run, a run ending in its build step leaves no progress up and drops the late C++ result (a player's queued Build still goes out), the auto build's keys in every language |
| `modules/autoBuildHoldTest.js` | The build at game start held during play (a save loaded or a new game, the panel closed): the next step - scan, build or Apply - waits, the status line says why, no timeout runs; the panel opening or the main menu resumes it; the player's own action while it waits wins |
| `modules/configSaveTest.js` | Settings load and save, on settingsPanel.js's own `onUnifiedConfigLoaded` / `saveUnifiedConfig` / `saveSettings`: the XP caps read back (0-100, defaults 5/15/50, a hand-edited value held, the next save keeps them), a load that throws part way, holds nothing or does not parse blocks every save (logged as an error) until a whole load, a `configNotRead` config still saves, closing the panel sends a waiting autosave at once and cancels it; the scan status bar shows the DLL's translated "not written" line; plugin list entries an older panel saved lowered (`toLowerCase`, non-ASCII capitals) take the scanned name (`migrateLoweredWhitelist`) |
| `modules/panelRefreshTest.js` | What the panel shows and sends after the player's own change: Unlock (C++'s answer, also for a spell already shown unlocked) and cheat Unlock/Relock repaint the canvas tree and open the children, nothing with the panel closed or a refused unlock; Classic's tier zone chart reads and drags the settings' zones (a preset shows, a drag keeps the preset's other end, defaults copied); edit mode sends the whole tree's prerequisites to C++ when it ends or the panel hides, once, nothing without a change; `sendTreePrerequisites` leaves out an edit mode copy (`_dup_`) of a spell the tree has, sends a lone copy as its spell and strips `_dup_` from lists, so a copy cannot erase its spell's requirements; a spawned spell keeps the scan's `persistentId` (not a `_dup_` copy) and is in the node list once |
| `modules/panelTidyTest.js` | The 1.6 panel tidy-up: Reset to Defaults asks for a second click (`ResetConfirm`: a double-click ignored, one button armed at a time, the label back after `ARM_MS`), the tree footer's status line (`TreeStatusLine`: a success goes back to "Ready" after `SHOW_MS`, a warning or error stays), the version at the foot of Settings, Import on the Scan tab, the description keys taken out gone from every language |
| `modules/panelLayoutTest.js` | The 2.0 panel layout: Settings categories (`SettingsCategories`: one page at a time, each category's scroll kept across a switch and the tab closing, Presets on the Learning or Spell Tomes page, the list as a row on a narrow page, every setting on one of the four pages, developer options only under Advanced); the Scan page (`ScanStage`: the stage and its one main button and what it presses, the summary's numbers with the left-out reasons adding up to the difference - one reason alone when it is the whole gap, checked over every language file, and the "N left out:" lead not reusing the excluded list's word -, the filter lines, "applied" set and dropped, the details fold, a scan with no spell saying "0 spells found", a scan C++ reports running without the Scan button (edit mode) reading as scanning; Alternate Pathways only in Developer Mode); the tree's one frame (the tools back in their corner when the canvas goes, the legend behind "?" on click, the footer status, school labels in their colour, no box round the tools) |
| `modules/spellCardNeedsTest.js` | The spell card around "can I learn this now" (`SpellCardNeeds`): Still needed chips (required, optional with "need 1 of 2" counted on what is left, a lock once with its LOCK badge and school), Met apart ("Requirements met" once nothing is left), hidden names "???" (a lock's only once revealed), the reason beside a locked Learn (`whyText`; not in cheat mode or a preview), edit mode's lists instead of chips, the state line as one sentence (word, XP share, "casts at N% power") with its bar in every state, the "Weakened" badge with no second figure on a progress-only redraw, the folded Unlocks and its memory, the ids the card's code and tests use |
| `test-runner.html` | Browser-based test runner |
| `dev-harness.html` | Desktop harness (open in a browser, mock C++ bridge in `dev-harness-bridge.js`). Generated: `node tools/make-dev-harness.js` puts the parts of `dev-harness-parts.html` into `index.html`, so it loads exactly the panel's scripts in the panel's order - never edit it by hand, edit `dev-harness-parts.html` and regenerate |

### Code review

Reviews use two reviewers: an independent reviewer agent and OpenCodeReview (`ocr`, delegation mode:
`ocr delegate preview --from <a> --to <b>`, `ocr delegate rule <paths>`). The project's OCR rules are in
`.opencodereview/rule.json` (var-only Ultralight JS, frame-drop checks, the C++ rules above); they replace
OCR's generic JS rules, which would ask for let/const.

### Librarian check (accuracy and speed)

```powershell
node tools/librarian-check.js                 # compare with tools/librarian-baseline.json
node tools/librarian-check.js --update        # after an intended change: new baseline
node tools/librarian-check.js --log <SpellLearning.log>   # also the game's adapter timings
```

Run it after any change to the librarian rules, the scanner fields they read, the perk adapters or the
tree builder. It first runs `librarian-test --self-test` (a failure stops the check, no baseline is written), then scores the pinned dump (`lab/librarian-judge/dumps/`, not in git) against the Spell
Research answer set (all rules and the vanilla-structure tier), every AI-judged round
(`lab/librarian-judge/<round>/verdicts.json`) and the share of untagged spells, and fails (exit 1) when
any of them gets worse; timings over the baseline's budgets only warn. Local paths (the answer set, another
dump) go in `tools/librarian-check.local.json` (not in git). Needs `BuildRelease.ps1`'s tools. See
docs/librarian/LIBRARIAN.md, "검증".

### No C++ Test Framework

This project does not have a C++ test framework. Validation is done through:
- Compilation (build succeeds)
- JavaScript unit tests (above)
- In-game testing by manual play (the old autoTest.js harness and its `test_config.json` went with the
  JS tree builders it ran, 2026-09-27)
- The in-game 1.6 self-test (`src/environment/EnvironmentSelfTest*.cpp`, dev only): on while
  `Data/SKSE/Plugins/SpellLearning/selftest.json` holds `{"enabled": true}` (`DeployDev.ps1 -WithSelfTest` puts
  `debug/selftest.json` there; never shipped). After `delaySeconds` (30) of unpaused play following a load it writes
  `[selftest-1.6] <id> PASS|FAIL|SKIP|ANSWER <values>` lines and `[selftest-1.6] DONE pass= fail= skip= answer=` to
  SpellLearning.log (FAIL = a defect; ANSWER yes|no = H2's finding):
  H2 (does a spell's virtual `HasKeyword` see keywords on its effects, also one added at runtime), the explosion twin
  (MAG_ScreenShakeEffect on the twin, the Blizzard hazard on the original, values equal), the Mysticism patch
  (`HeartOfMagic_Mysticism.esp` is MAG_ScreenShakeEffect's last override), Frostwalker's matcher on the
  installed lists, the env adapter files in `perk_adapters_report.json`, Desecration's necrotic spells. Claude reads the
  log and judges it; docs/ENVIRONMENT.md, "Self-test"
- `node tools/frostwalker-twin-check.js [--patterns <Data>/SKSE/Plugins/Frostwalker] [--mysticism <MysticismMagic.esp>]`:
  HeartOfMagic.esp's header (light flag, HEDR 1.70, masters), each twin's record (a new record, fields equal to the
  original's bytes) and its strings against Frostwalker's matcher; then `HeartOfMagic_Mysticism.esp`
  (`tools/mysticism-patch-check.js`: header, the one MGEF override, `ExplosionRef` resolving to the twin; with
  `--mysticism`, every subrecord equal to Mysticism's own after FormID mapping but `ExplosionRef`); exit 1 on a failure.
  Both read plugins with `tools/plugin-reader.js`
- Offline harnesses in `tools/` built by `BuildRelease.ps1` into `build/tools/Release/`:
  `treebuilder-test` (the Classic tree builder; it prints each school's shape - `Structure <school>: nodes, links,
  branching, maxChildren, maxDepth, nonStringLinks` - and exits 2 on a link that is not a formId or a school of 10+
  spells that never branches), `librarian-test` (spell tag librarian; `--adapters <dir> --catalog <file>` plans the perk adapter
  keyword writes with the plugin's own planner; `--spellresearch <file> --catalog <file> [-a <answers>]` counts the
  Spell Research registrations and scores their labels; `librarian-test --self-test` classifies made-up spells - the
  evidence conflict stage, per-element evidence tiers, the axis source after removals - and plans adapter lines on
  made-up catalogs - `tierMin`, `requires.config` and `requires.dlls`, the per-effect `damage` lines (`tools/librarian-adapters-selftest.cpp`) -
  and checks the explosion twin rules - Frostwalker's matcher and pattern files, which effects take a twin, the
  in-game `mysticism-patch` verdict (`tools/environment-selftest.cpp`, `include/environment/EnvironmentMatch.h`) and
  `AsciiText::ToLower` (A-Z only, other bytes kept) - no dump needed, exit 1 on a failure, and `librarian-check.js` runs it first; `--adapters ... -v` lists every planned
  effect write with the spells using it), `tome-reach-test` (which tree spells have tomes nothing hands
  out: `tome-reach-test -i inputs.json -s spell_scan_output.json [-v]`, inputs from `lab/tome-reach/make_inputs.py`;
  `tome-reach-test --self-test` checks the reader on made-up plugins, no game or load order needed, exit 1 on a failure;
  it must list what `lab/tome-reach/expected-*.txt` lists on the same load order) and `declutter-test`
  (the native tree declutter pass: `declutter-test -i tree.json -o reply.json [-r runs]`; its positions
  must match `LayoutDeclutter.apply` in `modules/layoutDeclutter.js` on the same tree - see
  docs/TREE_BUILDING_SYSTEM.md, "Decluttering before save"), `tree-save-check` (SaveSpellTree's no-spells guard,
  `include/uimanager/TreeSaveCheck.h`: `--self-test` on made-up trees, or `tree-save-check <tree.json>...` against a
  full parse; exit 1 on a disagreement) and `tree-remap-check` (a saved tree's FormIDs and links after a load order
  change, `include/TreeRemap.h`, the part of `ValidateAndFixTree` with no game types: `tree-remap-check --self-test`
  on made-up trees - light plugin removed and the rest shifted, two plugins swapping indices, legacy nodes without a
  persistentId (following a copy, colliding, gone), the same spell kept in one school and dropped in another, a
  spell that resolves but is no player's spell, links to removed spells, formIds in another letter case, roots no
  node has, broken shapes - every link read back by spell name, and a second pass must change nothing; exit 1 on a
  failure; also `include/FormIdText.h`, the one reader of formId text - hex, `0x` optional, any case; junk, a sign,
  a space, empty or more than 8 digits rejected; `ParseSpellOf` reads an edit mode copy's `<formId>_dup_<n>` as its
  spell - and its `0x%08X` writer), `tree-prereq-check` (the tree prerequisites game start applies,
  `include/TreePrereqEntries.h`: `tree-prereq-check --self-test` on made-up trees - roots, the fallback from
  `prerequisites`, lock repair with object and string locks, `_dup_` copies, a spell in two schools, school key
  order, non-string entries, trees the panel sends nothing for; `tree-prereq-check <tree.json>` prints the array, which
  must equal what the panel's `sendTreePrerequisites` sends for the same text; exit 1 on a failure, 3 when nothing
  is sent; docs/ARCHITECTURE.md, "Progression state at game start") and `progression-check` (`progression-check --self-test`: the co-save's
  progress (SLPR v1-v3, written as v2), cap tracker (SLPX) and tome XP (SLTX) records, `include/ProgressionCoSave.h`,
  written and read in byte buffers - cut short, over the limits, SLPX without an SLPR entry - and the XP cap, study, SetSpellXP and mastery arithmetic of `include/ProgressionMath.h`;
  exit 1 on a failure); the magic menu prototype's (plugins/SL_MagicMenu, docs/superpowers/specs/2026-10-26-magic-menu-blocks-design.md):
  `magic-menu-layout-check` (the block layout, the add-on registry, and the core alone: the default layout with no add-on,
  the retired ids skipped), `magic-menu-preview-check` (the preview math) and
  `magic-menu-block-api-check` (Block API v3: registration parsing of v1, v2 and v3 bytes - cut short, version 4 refused,
  v3's flags, pad and legacy ids -, the tabs mask per SkyUI tab, the press / release cancel rules of `BlockRules.h`, the
  'SLMD' ids, the 'SLMH' height requests - reading, judging against the registered height, the layout mode's height, the
  per-id queue -, the 'SLMS' / 'SLMV' messages and their queues, the key queue, the v3 content box, visible part and
  overlay clears; `--selftest` the same); the Heart of Magic add-on block's (plugins/SL_MagicMenu_HoM,
  docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md), which build from plugins/SL_MagicMenu_HoM:
  `magic-menu-tree-check` (`--selftest`, or a spell_tree.json), `magic-menu-homstyle-check` (`--selftest`, or HoM's
  config) and `magic-menu-hom-block-check` (the block's keys read from SL_MagicMenu.ini - the shipped one and made-up
  ones -, the registered heights, the pick rules, the strings, the cards' height requests and the tree window / progress
  card / node card arithmetic; `--selftest` the same); and the Spell Research add-on block's
  (plugins/SL_MagicMenu_SpellResearch, docs/superpowers/specs/2026-10-27-spell-research-addon-design.md)
  `magic-menu-sr-block-check` (`SrBlockRules.h`: archetype grouping and order, what the block shows per state, the list and
  line layouts and their wrapping, the unknown archetype's frame-only icon, the icon data's bounds, the link click rule,
  reply code -> notification, the open request's requestId matching and 5 s drop, line overlap; `SrBlockText.h`: the
  `[MagicMenuBlock] sLayout` and `[Display] sLanguage` ini reading (BOM too), the game language mapping, archetype short /
  name fallback, and `dist/lang/en.json` against the block's keys; `--selftest` the same, `--lang <file>` another language file) -
  PASS/FAIL lines, exit 1 on a failure

## Code Style

### C++ (C++23)

| Element | Convention | Example |
|---------|-----------|---------|
| Classes/Structs | PascalCase | `SpellLearningAPIImpl`, `InputHandler` |
| Member functions | PascalCase | `GetSingleton()`, `ProcessEvent()` |
| Free functions | PascalCase | `SetupLog()`, `OnDataLoaded()` |
| Member variables | `m_` prefix + camelCase | `m_hotkeyCode`, `m_targetPrerequisites` |
| Global variables | `g_` prefix | `g_api`, `g_sourceRegistered` |
| Constants | `k` prefix + PascalCase | `kSerializationUniqueID` |
| Local variables | camelCase | `buttonEvent`, `serialization` |
| Namespaces | PascalCase | `TreeNLP`, `SpellLearning` |
| Indentation | 4 spaces | (no tabs) |

- Smart pointers only—no raw `new`/`delete`
- `override` on all virtual function overrides
- `= delete` on copy constructors/assignment for singletons
- `static constexpr` for compile-time constants
- Section dividers: `// ==========...` box-style headers

### JavaScript (PrismaUI)

| Element | Convention | Example |
|---------|-----------|---------|
| Variables | `var` (NOT `let`/`const`) | `var spellData = {};` |
| Functions | camelCase | `updateTreeView()`, `handleSpellClick()` |
| Module pattern | Globals, no ES6 modules | Attach to window or use IIFE |
| Comments | JSDoc-style | `/** @param {string} name */` |

## Key Locations

| System | Location |
|--------|----------|
| Main plugin source | `plugins/spelllearning/src/` |
| Plugin headers | `plugins/spelllearning/include/` |
| Public C++ API | `plugins/spelllearning/src/SpellLearningAPI.h` |
| DEST compatibility shim | `plugins/DummyDEST/` |
| BookXP addon | `plugins/BookXP/` |
| Shared CMake modules | `plugins/cmake/` |
| CommonLibSSE-NG | `plugins/external/commonlibsse-ng/` |
| PrismaUI frontend | `PrismaUI/views/SpellLearning/SpellLearningPanel/` |
| PrismaUI JS modules | `PrismaUI/views/SpellLearning/SpellLearningPanel/modules/` |
| PrismaUI themes | `PrismaUI/views/SpellLearning/SpellLearningPanel/themes/` |
| PrismaUI languages | `PrismaUI/views/SpellLearning/SpellLearningPanel/lang/` |
| Papyrus scripts | `Scripts/Source/` |
| Runtime SKSE data | `SKSE/Plugins/SpellLearning/` |
| Documentation | `docs/` |
| FOMOD installer | `fomod/` |
| Design presets shipped as files | `SKSE/Plugins/SpellLearning/presets/design/` |
| Optional ISL patch | `optional/ISLPatch/` |
| Build output | `build/` (gitignored) |
| Lab/experiments | `lab/` |

## Environment Setup

### Prerequisites

- Visual Studio 2022 (or 2026) with C++ desktop workload
- CMake 3.25+
- vcpkg (set `$env:VCPKG_ROOT` or configure in vcpkg-configuration.json)
- Git (for submodules; CommonLibSSE-NG is pinned at tag `v10.0.0`, the release that knows 1.7.99 / 1.7.104 -
  after pulling a submodule change run `git submodule update --init --recursive`)
- `plugins/cmake/commonlibsse.cmake` turns CommonLibSSE-NG 10's `SKSE_SUPPORT_PATCH_SAFETY` off: it only checks
  `SKSE::Trampoline` branch/call writes, which this project does not make, and when on, every fresh configure
  fetches MinHook's `hde64` from GitHub with `FetchContent`. With it off a configure needs no network (vcpkg aside)

### First-Time Setup

```powershell
# 1. Initialize submodules
git submodule update --init --recursive

# 2. (Optional) Run dependency downloader for submodule repair
.\DownloadExternalDeps.ps1

# 3. Copy and edit local build config
Copy-Item Build_Config_Template.ps1 Build_Config_Local.ps1
# Edit Build_Config_Local.ps1 to set your output path and VS dev shell path

# 4. Build
.\BuildRelease.ps1
```

### vcpkg Dependencies

Managed via `vcpkg.json` manifest. Key packages: `fmt`, `spdlog`, `nlohmann-json`, `rapidcsv`, `xbyak`, `directxmath`, `directxtk`, `rapidfuzz-cpp`, `highway`.

Triplet: `x64-windows-static` (statically linked MSVC runtime).

## Common Workflows

### Before Modifying Any Code
```
1. Read the file you're modifying
2. Check docs/ for relevant architecture/design docs
3. If using RE/SKSE types, reference actual headers in plugins/external/commonlibsse-ng/
4. Verify the 600 LOC limit won't be exceeded
5. Identify which docs/ files will need updating after the change
```

### Adding a New Feature
```
1. Read docs/ARCHITECTURE.md to understand where it fits
2. Check existing patterns in the codebase
3. Implement following existing conventions
4. Run .\BuildRelease.ps1 to verify
5. Update relevant docs/ (MANDATORY)
6. If adding new system, create new doc AND update this decision tree
```

### Fixing a Bug
```
1. Reproduce the issue (check logs, understand state)
2. Read surrounding code context
3. Check if similar patterns exist elsewhere
4. Fix with minimal changes
5. Run .\BuildRelease.ps1 to verify
6. If fix changes documented behavior, update relevant docs/
```

### After Any Code Change
```
1. Run .\BuildRelease.ps1 to verify compilation
2. Fix any compilation errors
3. Run JS tests if UI code was changed (node run-tests.js)
3b. Run node tools/librarian-check.js if the librarian, scanner, perk adapters or tree builder changed
4. Review the Post-Change Documentation Checklist
5. Update affected docs before considering the task complete
```

## Keeping Documentation Current (MANDATORY)

**Documentation is part of the task, not a follow-up.** A code change is NOT complete until the relevant documentation is updated.

### The Rule

> **Every code change that affects documented behavior MUST include documentation updates IN THE SAME SESSION.**

### Post-Change Documentation Checklist

**STOP before marking any task complete.** Verify each item:
```
Did I add/modify a public API?          → Update docs/PLAN-PUBLIC-MODDER-API.md
Did I change system architecture?       → Update docs/ARCHITECTURE.md
Did I change tree building algorithms?  → Update docs/TREE_BUILDING_SYSTEM.md
Did I change design patterns or UI?     → Update docs/DESIGN.md
Did I change the module system?         → Update docs/MODULE_CONTRACTS.md
Did I change the preset system?         → Update docs/PRESETS.md
Did I change DEST/ISL compatibility?    → Update docs/DEST-IMPROVEMENTS.md
Did I change translation/i18n?          → Update docs/TRANSLATING.md
Did I add a new system?                 → Create new doc AND update decision tree in CLAUDE.md
```

### Do NOT
- Let documentation drift from implementation
- Complete a task saying "documentation can be updated later"
- Skip doc updates because the change seems "minor"
- Duplicate information across docs (cross-reference instead)

## All Documentation

| Document | Purpose |
|----------|---------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design, data flow, component responsibilities |
| [docs/TREE_BUILDING_SYSTEM.md](docs/TREE_BUILDING_SYSTEM.md) | Tree builder algorithm (Classic, the one builder since 2026-09-27) and layout |
| [docs/DESIGN.md](docs/DESIGN.md) | Design patterns and UI documentation |
| [docs/MODULE_CONTRACTS.md](docs/MODULE_CONTRACTS.md) | How to create modules for Heart of Magic |
| [docs/PRESETS.md](docs/PRESETS.md) | Preset system documentation |
| [docs/TRANSLATING.md](docs/TRANSLATING.md) | Translation guide for localizing the mod |
| [docs/PLAN-PUBLIC-MODDER-API.md](docs/PLAN-PUBLIC-MODDER-API.md) | Public modder API reference and design |
| [docs/DEST-IMPROVEMENTS.md](docs/DEST-IMPROVEMENTS.md) | DEST compatibility comparison |
| [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md) | HeartOfMagic.esp, explosion twins (Frostwalker), environment adapters, the 1.6 in-game self-test |
| [plugins/BookXP/README.md](plugins/addons/BookXP/README.md) | BookXP addon documentation |
