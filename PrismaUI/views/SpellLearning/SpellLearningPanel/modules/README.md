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
| `constants.js` | 103 | Default tree rules and colour palette |
| `tagVocabulary.js` | 97 | Closed tag list for the librarian; mirrors `include/librarian/TagVocabulary.h` |
| `state.js` | 281 | Settings object, app state, XP overrides |
| `config.js` | 132 | Tree layout and visual configuration, hotkey key codes |
| `spellCache.js` | 141 | Async spell data caching |
| `colorUtils.js` | 227 | School colors, dynamic CSS generation |
| `uiHelpers.js` | 506 | Scan status bar (and its message again after a language switch), `restoreScanButton`, `onScanFailed` (a scan that threw, `{mode, reason}`: a full scan gives the button back and puts the reason in the bar and in edit mode's wait line; a `tomes` failure is only logged - the panel has asked for no tome scan since the tome filter went, 2026-09-30), the primed spell count (blacklist and plugin whitelist) |
| `panelSnap.js` | 44 | `PanelSnap`: keeps the panel on whole pixels (a canvas on a fractional position is resampled at every paint) |
| `treeParser.js` | 992 | Tree JSON parsing, validation, cycle detection; drops a spell listed as its own child or prerequisite, and any link that is not a formId string, from `children`, `prerequisites`, `hardPrereqs` and `softPrereqs` (`withoutId`, `clampSoftNeeded`, also used by `treeViewerUI.js`), keeps `persistentId` |
| `spellNames.js` | 138 | `SpellNames` / `spellDisplayName(id, node, shown)`: a spell's name in the card's lists (Unlocks, prerequisites, Locks, paths to other schools); a name more than one different spell in the tree carries (locked ones counted; an edit-mode duplicate is its original) gets its plugin, `불씨조각 (NoviceBoltSpells.esp)`; `???` while hidden |
| `wheelRenderer.js` | 2757 | SVG radial tree rendering engine |
| `settingsPanel.js` | 2973 | Settings UI initialization and persistence |
| `treeViewerUI.js` | 508 | Tree viewer setup, node lookup cache, SmartRenderer, Import dialog and tree merge |
| `treeLoad.js` | 537 | Loading a tree: the trusted fast path (`_loadTrustedTree`) and `loadTreeData` |
| `spellCardView.js` | 535 | Selecting a spell and drawing the spell card (`showSpellDetails`, `renderSpellCard`, `clearSpellSelection`) |
| `spellProgressView.js` | 218 | The card's progress bar and Learn/Unlock buttons, `selectNodeById` |
| `findSpell.js` | 318 | Find Spell (F): search window, list, keyboard navigation, pan to the spell |
| `detailsPeek.js` | 222 | Hover preview of the spell card (`DetailsPeek`); the panel stays open with nothing selected |
| `treeStyle.js` | 486 | `TreeStyle`: tree look as design-preset tokens; halos, labels (the label font follows the design's CSS per language, re-read when a stylesheet or web font arrives), sigil, heart runes, school ink |
| `treeStyleBook.js` | 515 | Spellbook effects added to `TreeStyle`: page (its texture painted over frames, `stepPage`, or by an idle timer while the tree is not drawn; the old texture or the plain colour meanwhile), chapter titles (and their boxes, `chapterBoxes`), ornament dividers, hub emblem, page ornaments |
| `treeStyleInk.js` | 248 | Drawn lines added to `TreeStyle`: hand-drawn shapes, stippled, broken and engraved lines, the inset outline |
| `designPresets.js` | 390 | `DesignPresets`: built-in and `presets/design/*.json` looks, selector, preset CSS |
| `reverseUnlockSetting.js` | 242 | Settings > Progression > Known Higher Spells: reverse unlock switch, down-to-root switch, XP share per tier, own XP gain rates for spells learned downward; saves/loads/resets its keys and adds them to settings presets |
| `layoutDeclutter.js` | 569 | `LayoutDeclutter`: before a built tree is saved, spaces it out, moves spells off its lines (`LayoutLineClear`), off each other and off the heart; every growth mode calls `applyAsync` and saves when it is done. In game `applyAsync` has the plugin do it (`DeclutterTree`, the same pass in C++ on a worker thread, answered through `window.onDeclutterResult`, same positions); without the plugin, on an error or an unreadable reply, or after 30 s without a reply (`NATIVE_TIMEOUT_MS`; the plugin is then not asked again for `NATIVE_RETRY_MS`, 5 minutes, unless a reply comes in) it runs here a piece at a time between frames, progress on the status line. A request it stops waiting for (timeout, unreadable reply, a newer `applyAsync`) is cancelled in the plugin (`DeclutterCancel`), which answers `{ id, cancelled: true }`: nothing applied and the pause kept (only a real reply ends it). A C++ twin: changes go to `plugins/spelllearning/src/treebuilder/Layout*.cpp` too |
| `layoutLineClear.js` | 570 | `LayoutLineClear`: a search that moves spells so the straight lines pass them by, meet at open angles, and keep apart from each other (bundles of long lines included); runs as a job (`start` / `step`) that can stop and resume |
| `layoutLineGrid.js` | 450 | `LayoutLineClear`'s spatial grids (spells by cell, lines by the cells they pass near) and the fans a spell's search looks at; load right after `layoutLineClear.js` |
| `canvasCullTest.js` | 133 | Node tests for the culling index: the spells and lines a box gets from the grids are exactly those the full loop draws, in order; rebuilt when the lists change; not used in edit mode |
| `layerFlowTest.js` | 405 | Node tests for how a frame gets its tree layer: LayerScroll's shift and piece queue, `_drawTree`'s order (scroll, spread build, at once, stale build dropped, glide build and restart, restart cap, urgent build, leftover pieces drawn on a stretched frame or left waiting), a new font's widths and letters made before the names |
| `pageBuildTest.js` | 210 | Node tests for the design page painted over frames (`TreeStyle.stepPage`): step order, bands adding up to the one rect, what stands in meanwhile, look changes, frames asked for without marking the tree, the idle timer |
| `layoutDeclutterTest.js` | 362 | Node tests for `LayoutDeclutter`, run by `run-tests.js` |
| `wheelScroll.js` | 71 | `WheelScroll`: the mouse wheel scrolls the nearest scrollable box `SPEED` (3) times as far as the game browser would; the tree and previews keep their wheel zoom |
| `logGate.js` | 43 | `LogGate`: `console.log`/`console.info` go nowhere unless developer mode is on (they used to cross into the plugin to be dropped there) |
| `canvasRendererV2.js` | 569 | `CanvasRenderer`, the tree's canvas renderer: its state and constants, start-up, canvas size, render loop, public calls, the `_needsRender` accessor. The methods live in the `canvasRenderer*.js` files below, each an IIFE that copies a table of functions onto `CanvasRenderer` (loaded right after this file) |
| `canvasRendererColors.js` | 132 | School colours, colours a design and the player both set (`_designOrPlayer`), colour helpers |
| `canvasRendererInput.js` | 464 | Mouse and wheel events, screen to world, hit testing (`findNodeAt`), hover, tooltip |
| `canvasRendererSelect.js` | 177 | Selecting a spell and focusing on it, the dependency path sets, turning the wheel |
| `canvasRendererData.js` | 506 | `setData`: lookup maps, school angles, fallback spiral layout, discovery set, spatial index; level of detail and node buckets; the culling index (`_cullIndex`: spell and line grids, each line's spells and key) so a strip or piece looks only at what is near it |
| `canvasRendererFrame.js` | 489 | `render()`, background (the page's texture painted after the tree, `TreeStyle.stepPage`), the tree layer (`_drawTree`: paste, slide, stretch, LayerScroll, LayerBuild, whole repaint) and what goes into it (`_renderTreeInto`) |
| `canvasRendererMoving.js` | 384 | What moves every frame over the layer: learning path animation, particles, sigil spots (FxLayer), the heart and its beat, particle core |
| `canvasRendererDividers.js` | 130 | School dividers, debug grid |
| `canvasRendererEdges.js` | 439 | The lines: root lines, batched passes bottom to top, hover, selected and learning paths, lock chains |
| `canvasRendererNodes.js` | 416 | All spells: the pass per level of detail, NodeBatch batching (`_batchPlainNode`), shapes, sizes, dimming |
| `canvasRendererSpell.js` | 294 | One spell drawn by itself: `renderNode`, `renderMysteryNode` |
| `canvasRendererLabels.js` | 221 | Spell names: candidates, boxes, placement without overlaps (shared with LayerScroll) |
| `canvasRendererLearning.js` | 529 | Learning paths: the growing-path animation, lasting paths, travelling pulses |
| `nodeBatch.js` | 262 | `NodeBatch`: locked (lock look too), learnable (no XP ring yet), undiscovered and known spells collected into one path per look, in three layers, and drawn with a few paint calls; the school shapes and their turn toward the centre |
| `animClock.js` | 37 | `AnimClock`: how many fixed animation steps are due since the last frame, so the globe, stars and pulses keep their speed at any frame rate |
| `openRefreshGateTest.js` | 64 | Node tests for `OpenRefreshGate`, run by `run-tests.js` |
| `statusLineTest.js` | 328 | Node tests for the status lines over a language switch (`TreeGrowth.setStatusText`/`relabelStatus`/`resetStatus`, the Easy copy, `updateScanStatus`, `onScanFailed` for a full and a tome scan (the tome one puts the scanned message back and keeps the tome list it has), `t()` keeping `$` patterns in a value) and the card's Magicka rounding, run by `run-tests.js` |
| `spellNamesTest.js` | 210 | Node tests for `SpellNames`: a shared name gets its plugin and a unique one does not, hidden names show `???` with no plugin, SpellCache's `plugin` before the `persistentId`, counts after a new tree or `invalidate()`, edit-mode duplicates; TreeParser dropping self ids and null links from all four lists for both `trustPrereqs` values, `softNeeded` clamped, no "unobtainable" warning; run by `run-tests.js` |
| `levelFilterTest.js` | 123 | Node tests for the level filter: `BridgeView` counts levels like traits (not `Unknown`), the card's level is pressable only when shown and counted, one click handler across card fills, a press switches or clears the filter, trait filters unchanged; run by `run-tests.js` |
| `treeGrowthStatus.js` | 118 | The builder status line (`#tgStatus`) and its Easy page copy, added to `TreeGrowth`: `setStatusText(text, tone, key, params)`, `relabelStatus`, `resetStatus`, `STATUS_COLORS` |
| `openRefreshGate.js` | 91 | `OpenRefreshGate`: opening the panel repaints the tree only if the progress or known-spells replies changed what it shows since it closed |
| `fxLayer.js` | 213 | `FxLayer`: small canvases over the tree for what moves every frame (heart, sigil, learning glow, particles) and the hover preview (kept while unchanged, under the rest), so neither touches the tree canvas |
| `staticBase.js` | 111 | `StaticBase`: background and tree layer kept as one picture while both are still, so an animation frame pastes it in one pass |
| `layerScroll.js` | 544 | `LayerScroll`: a drag shifts the tree layer and draws only the uncovered strips (the ones on screen at once, the rest within the frame's time left), names kept across strips, then chapter titles over them, also where a new name reaches past its strip onto a title (`_underTitles`) (instead of repainting the whole tree mid-drag); `drawPendingAside` draws pieces an urgent build left on stretched frames (not beside a build or after a tree change; `_stretchedViewRect` says which the screen shows) |
| `layerBuild.js` | 442 | `LayerBuild`: a whole repaint of the tree layer drawn onto the spare canvas in pieces over several frames, the old picture shown meanwhile; for a camera glide it is built for the glide's end while the camera moves; urgent (on-screen pieces first, early swap, the rest to LayerScroll, next frame asked for) when the view is held past the old picture's margin; at once after `MAX_RESTARTS` restarts in a row; a middling build leaves `_lastMs` as it is; after a design change (the names' font new) spread even for a quick tree, the names' widths and letters made first (`_warmText`) |
| `progressUpdates.js` | 210 | `ProgressUpdates` / `window.onProgressUpdate`: an XP gain from C++ repaints the tree only for a state change, a reveal threshold or 1% of ring; the spell card is rebuilt only when it must. Also `window.onSpellRelocked` (cheat-mode Relock: known spells, availability, learning targets, card and unlocked count follow) |
| `hoverOverlay.js` | 255 | `HoverOverlay`: the hover preview (path, nodes, focus ring, bridges) painted over the tree layer and cached, so hovering never repaints the tree |
| `renderSettings.js` | 182 | The render popup (gear in the zoom bar), one page of chips: the "still everything" master switch, moving parts, what is on the tree; the star twinkle switch; puts saved values back on the popup and on Settings > Tree View |
| `designEffectsSetting.js` | 120 | Render popup chips for a design's page, drawn lines, sigil, learning glow, heart runes (`TreeStyle.setEffectsOff`); greys out what the design lacks and the starfield under a page |
| `requiredXPSync.js` | 80 | `RequiredXPSync`: sends C++ the panel's required XP for learning targets when C++ reports another number (after a load, or when a known higher spell or a share slider changes it) |
| `progressionUI.js` | 918 | How-to-Learn panel, learning status badges |
| `buttonHandlers.js` | 149 | Scan, learn, import/export button handlers |
| `cppCallbacks.js` | 1244 | C++ SKSE plugin callback handlers |
| `proceduralTreeBuilder.js` | 205 | Spell blacklist / plugin whitelist filters and `onProceduralTreeComplete`, which hands the C++ build to the Classic growth mode (`classic/`) and ignores a `busy` answer |
| **script.js** | 797 | Main init, tabs, dragging, early learning |
| **TOTAL** | ~25,797 | the 58 files in this table (the whole `modules/` tree, tests and `classic/` included, is ~43,400 lines in 96 files) |

Removed 2026-09-27, with the Simple, Procedural+ and Visual-First builds that used them: the JS tree
builders (`visualFirstBuilder.js`, `settingsAwareTreeBuilder.js`, `layoutEngine.js`, `layoutGenerator.js`,
`growthBehaviors.js`, `edgeScoring.js`), `llmTreeFeatures.js`, `generationModeUI.js`, the `autoTest.js`
harness and the unloaded WebGL renderer (`webglRenderer.js`, `webglShaders.js`, `webglShapes.js`).
Removed 2026-09-28 with the LLM (OpenRouter) feature: `llmIntegration.js` (its `saveTreeToFile` moved to
`uiHelpers.js`) and `llmApiSettings.js`. Also on 2026-09-28: `growthDSL.js` - WheelRenderer's
growth recipe modifiers ran on a recipe set nothing filled once the LLM style generator was gone.

## Load Order (index.html)

Modules must load in dependency order before `script.js` (abridged - `index.html` has the full list):

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
<script src="modules/spellNames.js"></script>

<!-- 4. Renderer -->
<script src="modules/wheelRenderer.js"></script>

<!-- 5. UI Panels -->
<script src="modules/settingsPanel.js"></script>
<script src="modules/treeViewerUI.js"></script>
<script src="modules/treeLoad.js"></script>
<script src="modules/spellCardView.js"></script>
<script src="modules/spellProgressView.js"></script>
<script src="modules/findSpell.js"></script>
<script src="modules/progressionUI.js"></script>
<script src="modules/buttonHandlers.js"></script>

<!-- 6. Integrations -->
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
