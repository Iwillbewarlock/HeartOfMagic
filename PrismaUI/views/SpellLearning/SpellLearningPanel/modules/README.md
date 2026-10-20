# SpellLearning JavaScript Modules

Modular JavaScript architecture for LLM maintainability. The original 8000+ line monolithic `script.js` was split into modules; the table lists the main ones (`index.html` loads them all).

## Architecture Goals

- **LLM Readability:** Each file under 600 lines (the project limit; several older modules are still over it)
- **Single Responsibility:** Each module handles one domain
- **Clear Dependencies:** Documented load order and dependencies
- **No Bundler Required:** Plain `<script>` tags, global scope

## Module Summary

| Module | Lines | Purpose |
|--------|------:|---------|
| `constants.js` | 154 | Default tree rules and colour palette; the default text sizes, the Font Size and Spell Name Size slider ranges and `clampSetting` (a saved value held to them at load); the XP cap range and defaults (`XP_CAP_*`); `foldPluginName` (plugin names compared A-Z folded only, as the DLL does) |
| `tagVocabulary.js` | 97 | Closed tag list for the librarian; mirrors `include/librarian/TagVocabulary.h` |
| `state.js` | 281 | Settings object, app state, XP overrides |
| `config.js` | 132 | Tree layout and visual configuration, hotkey key codes |
| `spellCache.js` | 141 | Async spell data caching |
| `colorUtils.js` | 227 | School colors, dynamic CSS generation |
| `uiHelpers.js` | 592 | Scan status bar (and its message again after a language switch), `restoreScanButton`, `onScanFailed` (a scan that threw, `{mode, reason}`: a full scan gives the button back and puts the reason in the bar and in edit mode's wait line; a `tomes` failure is only logged - the panel has asked for no tome scan since the tome filter went, 2026-09-30), the primed spell count (blacklist and plugin whitelist), `escapeHtml` (names into innerHTML), `schoolText` / `levelText` (a school's and a level's names in the panel's language) |
| `panelSnap.js` | 44 | `PanelSnap`: keeps the panel on whole pixels (a canvas on a fractional position is resampled at every paint) |
| `treeParser.js` | 551 | Tree JSON parsing, validation, cycle detection; drops a spell listed as its own child or prerequisite, and any link that is not a formId string, from `children`, `prerequisites`, `hardPrereqs` and `softPrereqs` (`withoutId`, always a new array, and `clampSoftNeeded`, also used by `treeLoad.js`), keeps `persistentId`; `updateNodeFromCache` fills a node from the spell info (every spell-info reply comes through it) and drops the level filter's counts when its level changes, the names' idle warm-up job when its name changes (`LayerBuild._glyphs`) and the culling index when its school changes (`CanvasRenderer._cullDirty`, for `_turnsOf`) |
| `prereqEdit.js` | 240 | `PrereqEdit`: a spell's `hardPrereqs`, `softPrereqs` and `prerequisites` (the drawn lines) as edit mode changes them - `add`, `remove`/`restore`, `toggle`/`setType`, `repair` (a tree saved by 1.2.0, with a link in both hard and soft; a lock on the parent lowers `softNeeded`), `copyTo` (a repair into the saved tree), `forget`/`forgetInTree` (a deleted spell out of every node's lists, children and locks, and the saved tree's bridges: edit mode's `deleteNode`, which refuses a school's root, so `root` never names a deleted spell); an id once, in one of hard and soft, every list its own array; tests in `prereqEditTest.js` |
| `orphanRepair.js` | 456 | `analyzeOrphans` / `repairOrphans` / `updateOrphanRepairButton`: spells the loaded tree cannot reach and prerequisite links to spells it does not have, and the orphan repair button (split from treeParser.js, 2026-09-30) |
| `spellNames.js` | 138 | `SpellNames` / `spellDisplayName(id, node, shown)`: a spell's name in the card's lists (Unlocks, prerequisites, Locks, paths to other schools); a name more than one different spell in the tree carries (locked ones counted; an edit-mode duplicate is its original) gets its plugin, `불씨조각 (NoviceBoltSpells.esp)`; `???` while hidden |
| `wheelRenderer.js` | 2757 | SVG radial tree rendering engine |
| `settingsPanel.js` | 3005 | Settings UI initialization and persistence |
| `settingsCategories.js` | 141 | `SettingsCategories`: Settings one category at a time (Display, Learning, Spell Tomes, Advanced), the list a column or a row on a narrow page, the blocks in one column below `ONE_COLUMN_PX`, each category's scroll position kept for the session, the Presets row on the Learning or Spell Tomes page; tests in `panelLayoutTest.js` |
| `resetConfirm.js` | 87 | `ResetConfirm`: a Reset to Defaults button (Settings, Early Learning power steps) asks for a second click: the label says so for `ARM_MS`, a click within `MIN_CONFIRM_MS` (double-click) is ignored, one button armed at a time, disarmed on tab switch and panel hide; tests in `panelTidyTest.js` |
| `treeViewerUI.js` | 509 | Tree viewer setup, node lookup cache, SmartRenderer, Import dialog and tree merge |
| `treeLoad.js` | 595 | Loading a tree: the trusted fast path (`_loadTrustedTree`) and `loadTreeData`; `sendTreePrerequisites` (the tree's prerequisites to C++, also after edit mode; an edit mode copy `_dup_` sent as its spell or not at all) |
| `treeLegend.js` | 45 | `TreeLegend`: the node states key behind the tree footer's "?" (a click opens and closes it, a tab switch closes it); tests in `panelLayoutTest.js` |
| `treeStatusLine.js` | 98 | The tree footer's status line: `setTreeStatus` / `setTreeStatusKey` (moved from `progressionUI.js`) and `TreeStatusLine`: a success line that repeats the footer (`BRIEF_KEYS`: loaded, tree saved) goes back to "Ready" after `SHOW_MS`; any other write cancels that; a warning or error (`KEEP_KEYS`, plain DLL text) is not covered by a success line within `KEEP_WINDOW_MS`; tests in `panelTidyTest.js` |
| `spellCardNeeds.js` | 348 | `SpellCardNeeds`: the spell card's state line (one sentence: the state word, XP share, "casts at N% power", bar; `refresh` after a progress-only redraw), "Still needed" chips (required, optional with "need N of M" counted on what is left, locks) and "Met" ("Requirements met" once nothing is left), the reason beside a locked Learn, "Leads to N later spells" folding Unlocks; tests in `spellCardNeedsTest.js` |
| `spellCardView.js` | 573 | Selecting a spell and drawing the spell card (`showSpellDetails`, `renderSpellCard`, `clearSpellSelection`); `spellWeakenedPower`, the power behind the "Weakened" badge and "casts at N% power" (none once mastered) |
| `spellProgressView.js` | 234 | The card's progress bar and Learn/Unlock buttons (then `SpellCardNeeds.refresh`), `selectNodeById` |
| `findSpell.js` | 300 | Find Spell (the tool bar's magnifier toggles it): dropdown under the bar, list, keyboard navigation, pan to the spell; `dismissFindSpell` (tab switch, panel close, tree load) |
| `spellSearch.js` | 180 | `SpellSearch`: which spells Find Spell lists for a text (name, school English or translated, formId only for a formId-like text; case and spaces ignored; exact, prefix, rest, formId; locked left out in discovery mode, raw tree data then lists nothing), the space-blind highlight range |
| `treeToolsScale.js` | 186 | The tree tool bar's size (1x - 3x) from the grip at its corner; saves/loads/resets `treeToolsScale`; a lost release (no button held, focus lost) ends the drag; `setInset` moves the tools in off a page's corner drawing (`TreeStyle.toolsInset`) |
| `detailsPeek.js` | 222 | Hover preview of the spell card (`DetailsPeek`); the panel stays open with nothing selected |
| `treeStyle.js` | 502 | `TreeStyle`: tree look as design-preset tokens; halos, labels (no name outline below `LABEL_HALO_MIN_ZOOM` 0.8; the label font follows the design's CSS per language, re-read when a stylesheet or web font arrives), sigil, heart runes, school ink |
| `treeStyleBook.js` | 585 | Spellbook effects added to `TreeStyle`: page (its texture painted over frames, `stepPage`, or by an idle timer while the tree is not drawn; the old texture or the plain colour meanwhile), chapter titles (and their boxes, `chapterBoxes`), ornament dividers, hub emblem, page ornaments |
| `treeStyleInk.js` | 259 | Drawn lines added to `TreeStyle`: hand-drawn shapes (their bow in two parts, `edgeBowUnit`/`edgeBowFrom`, for the culling index), stippled, broken and engraved lines, the inset outline |
| `designPresets.js` | 390 | `DesignPresets`: built-in and `presets/design/*.json` looks, selector, preset CSS |
| `notificationSetting.js` | 166 | Settings > Early Learning > Show Notifications (every corner message of the mod), Notification Language (the panel's or English) and the weakened spell alerts under it (greyed out while it is off); saves/loads/resets `config.notifications` |
| `panelOpenSetting.js` | 141 | Settings > Hotkey: the toggle key (Change, Reset, None = `hotkeyCode` 0) and the letter key warning; saves/loads/resets them; tests in `panelOpenSettingTest.js` |
| `spellResearchStatus.js` | 80 | Settings > Spell Tome Learning > "Spell Research": what the plugin last added to Spell Research's lists (`window.onSpellResearchStatus`), hidden without SR; redrawn after a language switch |
| `reverseUnlockSetting.js` | 242 | Settings > Progression > Known Higher Spells: reverse unlock switch, down-to-root switch, XP share per tier, own XP gain rates for spells learned downward; saves/loads/resets its keys and adds them to settings presets |
| `layoutDeclutter.js` | 569 | `LayoutDeclutter`: before a built tree is saved, spaces it out, moves spells off its lines (`LayoutLineClear`), off each other and off the heart; every growth mode calls `applyAsync` and saves when it is done. In game `applyAsync` has the plugin do it (`DeclutterTree`, the same pass in C++ on a worker thread, answered through `window.onDeclutterResult`, same positions); without the plugin, on an error or an unreadable reply, or after 30 s without a reply (`NATIVE_TIMEOUT_MS`; the plugin is then not asked again for `NATIVE_RETRY_MS`, 5 minutes, unless a reply comes in) it runs here a piece at a time between frames, progress on the status line. A request it stops waiting for (timeout, unreadable reply, a newer `applyAsync`) is cancelled in the plugin (`DeclutterCancel`), which answers `{ id, cancelled: true }`: nothing applied and the pause kept (only a real reply ends it). A C++ twin: changes go to `plugins/spelllearning/src/treebuilder/Layout*.cpp` too |
| `layoutLineClear.js` | 570 | `LayoutLineClear`: a search that moves spells so the straight lines pass them by, meet at open angles, and keep apart from each other (bundles of long lines included); runs as a job (`start` / `step`) that can stop and resume |
| `layoutLineGrid.js` | 450 | `LayoutLineClear`'s spatial grids (spells by cell, lines by the cells they pass near) and the fans a spell's search looks at; load right after `layoutLineClear.js` |
| `canvasCullTest.js` | 177 | Node tests for the culling index: the spells and lines a box gets from the grids are exactly those the full loop draws, in order; the turns, variants and bows it keeps; rebuilt when the lists change or a school changes in place (`TreeParser.updateNodeFromCache`); not used in edit mode |
| `labelCullTest.js` | 224 | Node tests for the spell names: the candidates from the culling index are exactly the full loop's, in order (140 views); 50 names at most below zoom 0.8, the learning ones kept, 150 above; the selected spell in the layer's margin named with the cap full (`renderLabels`, `LayerScroll._labels`); a spell coming in at the edge does not displace the names nearest the middle |
| `layerFlowTest.js` | 511 | Node tests for how a frame gets its tree layer: LayerScroll's shift and piece queue, `_drawTree`'s order (scroll, spread build, at once, stale build dropped, glide build and restart, restart cap, urgent build, leftover pieces drawn on a stretched frame or left waiting), a new font's widths and letters made before the names (left cold after names drawn with no outline below 0.8), the idle warm-up of every name's letters (`warmIdle`: the names as they show first, `???` included, then every name; restarted by a name changed in place, no key made once done) |
| `labelHaloTest.js` | 112 | Node tests for the names' outline: `strokeText` at `TreeStyle.LABEL_HALO_MIN_ZOOM` (0.8) and above, none below, none without `labelHalo`; `LayerBuild._warmText` outlines the names' and titles' letters at any zoom (ready for a zoom-in) |
| `homeViewTest.js` | 251 | Node tests for the home view (`TreeCamera`): the tree fitted into the part no panel covers (bar under the tree, sidebar, open How-to-Learn panel), centred on it, no closer than `TreeNav.HOME_ZOOM` and no further out than `HOME_MIN_ZOOM` (a real-size tree at that floor, heart centred), chapter titles fitted per axis, the pending home view kept until the tree can be measured and dropped for a view asked for after the load |
| `previewFitTest.js` | 160 | Node tests for the Scan page previews (`TreePreviewUtils`, `TreePreviewSun`): a hidden box leaves the canvas as it is, a shown one sets its size once; the fit holds roots, arrows and school names with `FIT_MARGIN`; no school name over a root |
| `chapterTitlesTest.js` | 138 | Node tests for where chapter titles go (`TreeStyle._chapterPlaces`): clear of every spell under them, the other schools' outer ring included, at any zoom and turn, and close to it; a hidden school's spells push no title out; none without titles |
| `languageRefreshTest.js` | 150 | Node tests for a live language switch (`refreshScriptTexts`): the tree's school tabs and tooltips and the Scan page's school list in the new language, a modded school keeps its name, the selected school stays marked |
| `starCadenceTest.js` | 181 | Node tests for the stars' own pace (`Starfield.advance` every `STAR_FRAME_MS`, never while still, drawing moves nothing, a move under one opacity step), `_syncStarfield`, and `_drawTree` leaving the tree canvas alone after a key's first frame (one repaint per change) |
| `pageBuildTest.js` | 241 | Node tests for the design page painted over frames (`TreeStyle.stepPage`): step order, bands adding up to the one rect, what stands in meanwhile, look changes, frames asked for without marking the tree, the idle timer |
| `layoutDeclutterTest.js` | 362 | Node tests for `LayoutDeclutter`, run by `run-tests.js` |
| `panelLayoutTest.js` | 511 | Node tests for the 2.0 layout: Settings categories (one page, scroll memory, Presets row, narrow row, every setting on its page, developer options only under Advanced), the Scan stage (stage, main button and what it presses, summary numbers and left-out reasons, saved tree with no scan, filter lines, applied and what drops it, details fold, markup and hooks), the tree legend and frame |
| `spellCardNeedsTest.js` | 281 | Node tests for `SpellCardNeeds`: chip grouping (required, "need 1 of 2" counted on what is left, locks once with their school, met apart, "Requirements met"), hidden names, the reason beside Learn, edit and cheat mode, the state line and the progress-only redraw, the folded Unlocks |
| `panelAuditTest.js` | 267 | Node tests for the 2.0 audit pass: the Settings page in one column below `ONE_COLUMN_PX`, rows that wrap their control, no clamped text, the category column's width, the last power step's columns, the card's chips as wrapping inline boxes, the canvas hint clear of the sidebar card, the Scan page's solid bar at the foot; every state badge a filled box; quiet text, inks and accents at 4.5:1 per design (computed from the tokens and the dark presets' palettes), the LOCK mark 11px |
| `panelTidyTest.js` | 347 | Node tests for the 1.6 tidy-up: Reset to Defaults confirm (`ResetConfirm`: double-click ignored, own label back, one armed, disarm on tab switch/hide), the status line (`TreeStatusLine`: success back to Ready, also C++ "Tree saved"; a warning or error not covered), the version at the foot of Settings and Import on the Scan page (index.html), the removed description keys gone from every language |
| `wheelScroll.js` | 71 | `WheelScroll`: the mouse wheel scrolls the nearest scrollable box `SPEED` (3) times as far as the game browser would; the tree and previews keep their wheel zoom |
| `logGate.js` | 43 | `LogGate`: `console.log`/`console.info` go nowhere unless developer mode is on (they used to cross into the plugin to be dropped there) |
| `canvasRendererV2.js` | 589 | `CanvasRenderer`, the tree's canvas renderer: its state and constants, start-up, canvas size, render loop (its frames with nothing to draw, not while panning or within `INPUT_QUIET_MS` of an input, give a slice to `LayerBuild.warmIdle`), public calls, the `_needsRender` accessor. The methods live in the `canvasRenderer*.js` files below, each an IIFE that copies a table of functions onto `CanvasRenderer` (loaded right after this file) |
| `canvasRendererColors.js` | 132 | School colours, colours a design and the player both set (`_designOrPlayer`), colour helpers |
| `canvasRendererInput.js` | 464 | Mouse and wheel events, screen to world, hit testing (`findNodeAt`), hover, tooltip |
| `canvasRendererSelect.js` | 177 | Selecting a spell and focusing on it, the dependency path sets, turning the wheel |
| `canvasRendererData.js` | 541 | `setData`: lookup maps, school angles, fallback spiral layout, discovery set, spatial index; level of detail and node buckets; the culling index (`_cullIndex`: spell and line grids, each line's spells and key; per spell its turn and hand variant, per line its bow: `_turnsOf`, `_bowsOf`) so a strip or piece looks only at what is near it |
| `canvasRendererFrame.js` | 517 | `render()`, the stars' settings and moves (`_syncStarfield`), background (the page's texture painted after the tree, `TreeStyle.stepPage`), the tree layer (`_drawTree`: paste, slide, stretch, LayerScroll, LayerBuild, whole repaint) and what goes into it (`_renderTreeInto`) |
| `canvasRendererMoving.js` | 384 | What moves every frame over the layer: learning path animation, particles, sigil spots (FxLayer), the heart and its beat, particle core |
| `canvasRendererDividers.js` | 130 | School dividers, debug grid |
| `canvasRendererEdges.js` | 443 | The lines: root lines, batched passes bottom to top, hover, selected and learning paths, lock chains |
| `canvasRendererNodes.js` | 420 | All spells: the pass per level of detail, NodeBatch batching (`_batchPlainNode`), shapes, sizes, dimming |
| `canvasRendererSpell.js` | 294 | One spell drawn by itself: `renderNode`, `renderMysteryNode` |
| `canvasRendererLabels.js` | 284 | Spell names: candidates (from the culling index), boxes, placement without overlaps (shared with LayerScroll); fewer below zoom 0.8 (`MAX_LABELS_FOCUS`); the selected spell's first, past the cap; nearest the screen's middle first within a priority; what a spell's name shows as (`_labelTextOf`: none, `???` or the name; also used by `LayerBuild.warmIdle`) |
| `canvasRendererLearning.js` | 529 | Learning paths: the growing-path animation, lasting paths, travelling pulses |
| `nodeBatch.js` | 281 | `NodeBatch`: locked (lock look too), learnable (no XP ring yet), undiscovered and known spells collected into one path per look, in three layers, and drawn with a few paint calls; the school shapes and their turn toward the centre (as the culling index keeps it, `useTurn`) |
| `animClock.js` | 37 | `AnimClock`: how many fixed animation steps are due since the last frame, so the globe, stars and pulses keep their speed at any frame rate |
| `openRefreshGateTest.js` | 144 | Node tests for `OpenRefreshGate`, run by `run-tests.js`; also the hidden-panel pushes (`onResetTreeStates`, `onLearningTargetSet`) draw nothing until the opening's replies |
| `statusLineTest.js` | 452 | Node tests for the status lines over a language switch (`TreeGrowth.setStatusText`/`relabelStatus`/`resetStatus`, the Easy copy, `updateScanStatus`, a key the language lacks (English or the line as written, not the key), `onScanFailed` for a full and a tome scan (the tome one puts the scanned message back and keeps the tome list it has), `t()` keeping `$` patterns in a value) and the card's Magicka rounding, run by `run-tests.js` |
| `spellNamesTest.js` | 210 | Node tests for `SpellNames`: a shared name gets its plugin and a unique one does not, hidden names show `???` with no plugin, SpellCache's `plugin` before the `persistentId`, counts after a new tree or `invalidate()`, edit-mode duplicates; TreeParser dropping self ids and null links from all four lists for both `trustPrereqs` values, `softNeeded` clamped, no "unobtainable" warning; run by `run-tests.js` |
| `spellResearchStatusTest.js` | 89 | Node tests for the Spell Research line: hidden until a state comes, each state's text and numbers, English fallback, bad input hides it, `onSpellResearchStatus`; run by `run-tests.js` |
| `levelFilterTest.js` | 176 | Node tests for the level filter: `BridgeView` counts levels like traits (not `Unknown`), the card's level is pressable only when shown and counted, one click handler across card fills, a press switches or clears the filter, trait filters unchanged, bridges with an end not in the tree left out; run by `run-tests.js` |
| `treeGrowthStatus.js` | 137 | The builder status line (`#tgStatus`) and its Easy page copy, added to `TreeGrowth`: `setStatusText(text, tone, key, params)`, `relabelStatus`, `resetStatus`, `onSpellsScanned` / `showReady` ("Ready to build" once spells are scanned), `STATUS_COLORS` |
| `openRefreshGate.js` | 91 | `OpenRefreshGate`: opening the panel repaints the tree only if the progress or known-spells replies changed what it shows since it closed |
| `fxLayer.js` | 219 | `FxLayer`: small canvases over the tree for what moves every frame (heart, sigil, learning glow, particles) and the hover preview (kept while unchanged, under the rest), so neither touches the tree canvas |
| `staticBase.js` | 109 | `StaticBase`: background and tree layer kept as one picture while both are still (moving stars between their moves too), so an animation frame pastes it in one pass |
| `starfield.js` | 404 | `Starfield`: the star background (world-space parallax tiles or fixed to the screen), drawn in opacity steps; `advance` moves it at most every `STAR_FRAME_MS`, drawing only draws |
| `layerScroll.js` | 549 | `LayerScroll`: a drag shifts the tree layer and draws only the uncovered strips (the ones on screen at once, the rest within the frame's time left), names kept across strips, then chapter titles over them, also where a new name reaches past its strip onto a title (`_underTitles`) (instead of repainting the whole tree mid-drag); `drawPendingAside` draws pieces an urgent build left on stretched frames (not beside a build or after a tree change; `_stretchedViewRect` says which the screen shows) |
| `layerBuild.js` | 568 | `LayerBuild`: a whole repaint of the tree layer drawn onto the spare canvas in pieces over several frames, the old picture shown meanwhile; for a camera glide it is built for the glide's end while the camera moves; urgent (on-screen pieces first, early swap, the rest to LayerScroll, next frame asked for) when the view is held past the old picture's margin; at once after `MAX_RESTARTS` restarts in a row; a middling build leaves `_lastMs` as it is; after a design change (the names' font new) spread even for a quick tree, the names' widths and letters made first (`_warmText`; the font stays cold after names drawn with no outline below 0.8, `_outlinesMadeAt`); every name's letters made in idle animation frames, 2 ms a frame (`warmIdle`; the names as they show first, then every spell's own name, by code point; started again for a new font or tree or a name changed in place), the font cold until done |
| `progressUpdates.js` | 226 | `ProgressUpdates` / `window.onProgressUpdate`: an XP gain from C++ repaints the tree only for a state change, a reveal threshold or 1% of ring; the spell card is rebuilt only when it must. Also `window.onSpellRelocked` (cheat-mode Relock: known spells, availability, learning targets, card and unlocked count follow); `repaintStates`, the canvas tree's repaint after a state change (also Unlock and the cheat buttons); `forgetWeakened`, a mastered or relocked spell out of `state.weakenedSpells` |
| `hoverOverlay.js` | 335 | `HoverOverlay`: the hover preview (path, nodes, focus ring, bridges) painted over the tree layer and cached, so hovering never repaints the tree; the path's plain spells batched (NodeBatch) and cut to the screen |
| `renderSettings.js` | 182 | The render popup (gear in the zoom bar), one page of chips: the "still everything" master switch, moving parts, what is on the tree; the star twinkle switch; puts saved values back on the popup and on Settings > Tree View |
| `designEffectsSetting.js` | 120 | Render popup chips for a design's page, drawn lines, sigil, learning glow, heart runes (`TreeStyle.setEffectsOff`); greys out what the design lacks and the starfield under a page |
| `requiredXPSync.js` | 80 | `RequiredXPSync`: sends C++ the panel's required XP for learning targets when C++ reports another number (after a load, or when a known higher spell or a share slider changes it) |
| `learningTargetKeys.js` | 61 | `LearningTargetKeys`: the school key holding a spell, looked up by id (Learn button, spell card); `onLearningTargetDropped` |
| `progressionUI.js` | 931 | How-to-Learn panel, learning status badges |
| `buttonHandlers.js` | 161 | Scan, learn, import/export button handlers |
| `scanStage.js` | 325 | `ScanStage`: the Scan page's summary line (found, in the tree, the stage; the left-out reasons), one main button by stage (Scan Spells, Build Tree, Apply Tree) pressing the real hidden buttons, Scan again / Import / Clear draft tree, the applied mark (dropped by a new draft, locks, Import or a new scan), Scan details fold, the filter lines; tests in `panelLayoutTest.js` |
| `cppStatusKeys.js` | 25 | `CPP_STATUS_KEYS` / `cppStatusKey`: the language key of each English status message the DLL sends (scan bar, tree status line) |
| `cppCallbacks.js` | 1294 | C++ SKSE plugin callback handlers |
| `proceduralTreeBuilder.js` | 252 | The tome filter (`isTaughtByTome`, `filterTomeSpells`: the tree takes only spells a tome teaches, whose tome something hands out, that are not in the voice slot and not another plugin's copy of a spell - `duplicateOf`, the kept record's formId, set by the scan, SpellScannerDuplicates.cpp; edit mode's spawn list leaves the copies out too, so one spell is never put on the tree twice, while the blacklist list keeps them, where picking one does no harm), spell blacklist / plugin whitelist filters and `onProceduralTreeComplete`, which hands the C++ build to the Classic growth mode (`classic/`) and ignores a `busy` answer |
| `spellTreeSave.js` | 75 | `SpellTreeSave`: the one way the panel writes `spell_tree.json` - `send(tree, source)` refuses a tree with no spell node (`status.treeNoSpellsNotSaved` on the tree status line), ends a run of the build at game start first unless it is that run's save (`AutoBuild.yieldTo`), and sends a build's Apply to `SaveBuiltSpellTree` (records the load order) and an edit, import or orphan repair to `SaveSpellTree` |
| `autoBuildSetting.js` | 78 | `AutoBuildSetting`: Settings > Hotkey > "Build Spell Tree at Start" (`settings.autoBuild`: `whenMissing` default, `off`); not in settings presets; `notRead` when C++ sent its defaults for a `config.json` it could not read (`configNotRead`) |
| `autoBuild.js` | 528 | `AutoBuild`: the tree built at game start when there is none - decides once tree info (`onStartupTreeInfo`), config and scanner presets are in (BUILD, NOTICE or NONE; a config that did not load or was not read builds nothing), then scan -> build (silent progress, shown if the panel opens during the build step) -> Apply, each step held while a game is played with the panel closed until the panel opens or the main menu (`_step`, `onMainMenuShown`, `onNewGameStarted`; `status.autoBuildHeld`); the player's own Scan, Build, Apply, Clear or save ends it (`yieldTo`); a run ending in its build step closes or fails its progress and drops the C++ result still to come (`_dropBuild`); every step to `SpellLearning.log` (`AutoBuildEvent`) |
| `treePreviewHidden.js` | 77 | `TreePreviewHidden.ensure()`: the root preview's output (its grid) with the panel closed - the active preview mode rendered once into a context that draws nothing, at the preview canvas's size or 400 |
| `autoBuildTest.js` | 489 | Node tests for the build at game start: `SpellTreeSave` refusing an empty tree, `AutoBuild.decide`, the start waiting for tree info, config and presets, the run scan -> build -> apply -> done through save loads, `TreePreviewHidden` equal to a drawn frame; run by `run-tests.js` |
| `autoBuildFlowTest.js` | 534 | Node tests against the panel's own code (treeLoad, buttonHandlers, proceduralTreeBuilder): the load order recorded only for a build's save, the waits, every yield, an empty tree, scans, a throwing step, the Sun cache, the config last, the player's Build queued behind the run's; run by `run-tests.js` |
| `autoBuildGuardTest.js` | 522 | Node tests for the guards: no scan debounce, a player's build under way, a partial or unread config (`configNotRead`), the progress shown on opening, Apply/Clear refused while building, stale lock scores dropped, the dev harness save, a run ending in its build step (progress closed or failed, late C++ result dropped, a player's queued Build still sent), a scan that throws, the auto build's keys in every language; run by `run-tests.js` |
| `autoBuildHoldTest.js` | 211 | Node tests for the run held during play: decided in game -> the scan held (status line, C++ "held", no timeout running), a save load or a new game between steps holds the next one, the panel opening resumes a tick later and the main menu at once (the timeout again from there), the player's Scan/Build/Apply/Clear while it waits wins, the panel open holds nothing; run by `run-tests.js` |
| `configSaveTest.js` | 267 | Node tests for the settings' load and save on settingsPanel.js's own code: XP caps read back, a part-way load blocking saves, closing the panel sending a waiting autosave, plugin list entries an older panel lowered renamed; run by `run-tests.js` |
| `panelRefreshTest.js` | 436 | Node tests for Unlock and cheat Unlock repainting the tree, the Classic tier zone chart on the settings' zones, edit mode's prerequisites sent to C++ (copies not erasing their spell's), a spawned spell's persistentId; run by `run-tests.js` |
| **script.js** | 797 | Main init, tabs, dragging, early learning |
| **TOTAL** | ~33,880 | the 95 files in this table (the whole `modules/` tree, tests and `classic/` included, is ~46,390 lines in 109 files) |

Removed 2026-09-27, with the Simple, Procedural+ and Visual-First builds that used them: the JS tree
builders (`visualFirstBuilder.js`, `settingsAwareTreeBuilder.js`, `layoutEngine.js`, `layoutGenerator.js`,
`growthBehaviors.js`, `edgeScoring.js`), `llmTreeFeatures.js`, `generationModeUI.js`, the `autoTest.js`
harness and the unloaded WebGL renderer (`webglRenderer.js`, `webglShaders.js`, `webglShapes.js`).
Removed 2026-09-28 with the LLM (OpenRouter) feature: `llmIntegration.js` (its `saveTreeToFile` moved to
`uiHelpers.js`) and `llmApiSettings.js`. Also on 2026-09-28: `growthDSL.js` - WheelRenderer's
growth recipe modifiers ran on a recipe set nothing filled once the LLM style generator was gone.

## Load Order (index.html)

Modules must load in dependency order before `script.js` (abridged - `index.html` has the full list):

`dev-harness.html` is generated from `index.html` (`node tools/make-dev-harness.js`), so a module added to
`index.html` reaches the desktop harness by regenerating it; `run-tests.js` fails until then.

```html
<!-- 1. Constants and Configuration -->
<script src="modules/constants.js"></script>
<script src="modules/tagVocabulary.js"></script>
<script src="modules/state.js"></script>
<script src="modules/config.js"></script>

<!-- 2. Core Utilities -->
<script src="modules/spellCache.js"></script>
<script src="modules/colorUtils.js"></script>
<script src="modules/uiHelpers.js"></script>

<!-- 3. Parsers -->
<script src="modules/treeParser.js"></script>
<script src="modules/orphanRepair.js"></script>
<script src="modules/spellNames.js"></script>

<!-- 4. Renderer -->
<script src="modules/wheelRenderer.js"></script>

<!-- 5. UI Panels -->
<script src="modules/resetConfirm.js"></script>
<script src="modules/settingsPanel.js"></script>
<script src="modules/settingsCategories.js"></script>
<script src="modules/treeViewerUI.js"></script>
<script src="modules/treeStatusLine.js"></script>
<script src="modules/treeLegend.js"></script>
<script src="modules/treeLoad.js"></script>
<script src="modules/spellCardView.js"></script>
<script src="modules/spellCardNeeds.js"></script>
<script src="modules/spellProgressView.js"></script>
<script src="modules/spellSearch.js"></script>
<script src="modules/treeToolsScale.js"></script>
<script src="modules/findSpell.js"></script>
<script src="modules/learningTargetKeys.js"></script>
<script src="modules/progressionUI.js"></script>
<script src="modules/easyMode.js"></script>
<script src="modules/scanStage.js"></script>
<script src="modules/buttonHandlers.js"></script>

<!-- 6. Integrations -->
<script src="modules/cppStatusKeys.js"></script>
<script src="modules/cppCallbacks.js"></script>

<!-- 7. Main Application -->
<script src="script.js"></script>
```

## Module Dependencies

```
constants.js          (no deps)
    ↓
tagVocabulary.js      (no deps; kept identical to TagVocabulary.h by hand,
    ↓                  checked by librarian-test --check-vocab)
state.js              (uses: constants.js)
    ↓
config.js             (no deps)
    ↓
spellCache.js         (uses: state.js)
colorUtils.js         (uses: state.js, constants.js)
uiHelpers.js          (uses: state.js)
    ↓
treeParser.js         (uses: state.js)
orphanRepair.js       (uses: state.js)
spellNames.js         (uses when called: state.js, spellCache.js, treeViewerUI.js)
    ↓
wheelRenderer.js      (uses: state.js, config.js, colorUtils.js, treeParser.js)
    ↓
settingsPanel.js      (uses: state.js, config.js, colorUtils.js, uiHelpers.js)
treeViewerUI.js       (uses: state.js, wheelRenderer.js, colorUtils.js)
progressionUI.js      (uses: state.js, wheelRenderer.js, uiHelpers.js)
buttonHandlers.js     (uses: state.js, treeParser.js, wheelRenderer.js, spellCache.js)
    ↓
cppCallbacks.js       (uses: state.js, treeParser.js, wheelRenderer.js, spellCache.js)
    ↓
script.js             (uses: all modules)
```

## Key Global Objects

| Object | Module | Description |
|--------|--------|-------------|
| `DEFAULT_TREE_RULES` | constants.js | Default tree rules written into the scan export's `llmPrompt` |
| `KEY_CODES` | config.js | Keyboard code mapping (DirectInput scancodes) |
| `settings` | state.js | All user settings (persisted) |
| `state` | state.js | Runtime state (tree, selection, etc.) |
| `xpOverrides` | state.js | Per-spell XP overrides |
| `TREE_CONFIG` | config.js | Tree layout configuration |
| `SpellCache` | spellCache.js | Spell data cache singleton |
| `TreeParser` | treeParser.js | Tree parsing utilities |
| `WheelRenderer` | wheelRenderer.js | SVG rendering engine |

## Key Functions by Module

### constants.js
- Exports `DEFAULT_TREE_RULES`, `DEFAULT_COLOR_PALETTE`

### state.js
- Exports `settings`, `state`, `xpOverrides`
- `updateSliderFillGlobal(slider)` - Update slider fill visual

### colorUtils.js
- `getOrAssignSchoolColor(school)` - Get/create school color
- `applySchoolColorsToCSS()` - Generate dynamic CSS

### treeParser.js
- `TreeParser.parse(data)` - Parse and validate tree JSON
- `TreeParser.detectAndFixCycles(nodes)` - Fix circular dependencies

### orphanRepair.js
- `analyzeOrphans()` - Count unreachable spells and missing prerequisite links (read only)
- `repairOrphans()` - Reconnect them; `updateOrphanRepairButton()` shows the button when there is something to repair

### wheelRenderer.js
- `WheelRenderer.init(svg)` - Initialize renderer
- `WheelRenderer.setData(treeData)` - Load tree data
- `WheelRenderer.render()` - Render tree to SVG
- `WheelRenderer.updateNodeStates()` - Update visual states

### settingsPanel.js
- `initializeSettings()` - Setup all settings UI
- `loadSettings()` / `saveSettings()` - Config persistence
- `window.onUnifiedConfigLoaded(data)` - C++ callback

### cppCallbacks.js
- `window.updateSpellData(json)` - Spell scan result
- `window.updateTreeData(json)` - Saved tree loaded
- `window.updateSpellState(formId, state)` - A spell's state changed
- `window.onPlayerKnownSpells(data)` - Spells the player knows
- `window.onPrismaReady()` / `onPanelShowing()` / `onPanelHiding()` - Panel lifecycle

## Notes for LLMs

- **Read one module at a time** - Each is self-contained for its domain
- **Check dependencies** - Load order matters for global object availability
- **All modules use globals** - No import/export (browser compatibility)
- **Settings persistence** - Handled by `settingsPanel.js` via C++ bridge
- **C++ callbacks** - Most `window.on*` / `window.update*` functions are in `cppCallbacks.js`; others sit with their module (`progressionUI.js`, `progressUpdates.js`, `settingsPanel.js`, ...)
