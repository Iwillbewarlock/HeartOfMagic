# SpellLearning JavaScript Modules

Modular JavaScript architecture for LLM maintainability. Original 8000+ line monolithic `script.js` split into 17 focused modules.

## Architecture Goals

- **LLM Readability:** Each file under ~1000 lines (16/18 achieved)
- **Single Responsibility:** Each module handles one domain
- **Clear Dependencies:** Documented load order and dependencies
- **No Bundler Required:** Plain `<script>` tags, global scope

## Module Summary

| Module | Lines | Purpose |
|--------|------:|---------|
| `constants.js` | 267 | Core constants, difficulty profiles, keycodes |
| `tagVocabulary.js` | 97 | Closed tag list for the librarian; mirrors `include/librarian/TagVocabulary.h` |
| `state.js` | 143 | Settings object, app state, XP overrides |
| `config.js` | 266 | Tree layout and visual configuration |
| `spellCache.js` | 114 | Async spell data caching |
| `colorUtils.js` | 258 | School colors, dynamic CSS generation |
| `uiHelpers.js` | 189 | Status updates, tooltips, tier helpers |
| `panelSnap.js` | 44 | `PanelSnap`: keeps the panel on whole pixels (a canvas on a fractional position is resampled at every paint) |
| `growthDSL.js` | 301 | LLM-driven procedural tree visual DSL |
| `treeParser.js` | 461 | Tree JSON parsing, validation, cycle detection |
| `wheelRenderer.js` | 1296 | SVG radial tree rendering engine |
| `settingsPanel.js` | 1001 | Settings UI initialization and persistence |
| `treeViewerUI.js` | 618 | Tree viewer, spell details, node selection |
| `detailsPeek.js` | 213 | Hover preview of the spell card (`DetailsPeek`); the panel stays open with nothing selected |
| `treeStyle.js` | 349 | `TreeStyle`: tree look as design-preset tokens; halos, labels, sigil, heart runes, school ink |
| `treeStyleBook.js` | 277 | Spellbook effects added to `TreeStyle`: page, chapter titles, ornament dividers, ink reveal |
| `designPresets.js` | 336 | `DesignPresets`: built-in and `presets/design/*.json` looks, selector, preset CSS |
| `reverseUnlockSetting.js` | 242 | Settings > Progression > Known Higher Spells: reverse unlock switch, down-to-root switch, XP share per tier, own XP gain rates for spells learned downward; saves/loads/resets its keys and adds them to settings presets |
| `layoutDeclutter.js` | 500 | `LayoutDeclutter`: before a built tree is saved, spaces it out, moves spells off its lines (`LayoutLineClear`), off each other and off the heart; every growth mode calls `applyAsync` and saves when it is done. In game `applyAsync` has the plugin do it (`DeclutterTree`, the same pass in C++ on a worker thread, answered through `window.onDeclutterResult`, same positions); without the plugin, on an error or after 30 s without a reply it runs here a piece at a time between frames, progress on the status line. A C++ twin: changes go to `plugins/spelllearning/src/treebuilder/Layout*.cpp` too |
| `layoutLineClear.js` | 519 | `LayoutLineClear`: a search that moves spells so the straight lines pass them by, meet at open angles, and keep apart from each other (bundles of long lines included); runs as a job (`start` / `step`) that can stop and resume |
| `layoutLineGrid.js` | 297 | `LayoutLineClear`'s spatial grids (spells by cell, lines by the cells they pass near) and the fans a spell's search looks at; load right after `layoutLineClear.js` |
| `layoutDeclutterTest.js` | 133 | Node tests for `LayoutDeclutter`, run by `run-tests.js` |
| `wheelScroll.js` | 71 | `WheelScroll`: the mouse wheel scrolls the nearest scrollable box `SPEED` (3) times as far as the game browser would; the tree and previews keep their wheel zoom |
| `logGate.js` | 43 | `LogGate`: `console.log`/`console.info` go nowhere unless developer mode is on (they used to cross into the plugin to be dropped there) |
| `nodeBatch.js` | 211 | `NodeBatch`: locked (lock look too), undiscovered and known spells collected into one path per look, in three layers, and drawn with a few paint calls; the school shapes and their turn toward the centre |
| `animClock.js` | 37 | `AnimClock`: how many fixed animation steps are due since the last frame, so the globe, stars and pulses keep their speed at any frame rate |
| `openRefreshGateTest.js` | 64 | Node tests for `OpenRefreshGate`, run by `run-tests.js` |
| `openRefreshGate.js` | 91 | `OpenRefreshGate`: opening the panel repaints the tree only if the progress or known-spells replies changed what it shows since it closed |
| `fxLayer.js` | 213 | `FxLayer`: small canvases over the tree for what moves every frame (heart, sigil, learning glow, particles) and the hover preview (kept while unchanged, under the rest), so neither touches the tree canvas |
| `staticBase.js` | 111 | `StaticBase`: background and tree layer kept as one picture while both are still, so an animation frame pastes it in one pass |
| `progressUpdates.js` | 150 | `ProgressUpdates` / `window.onProgressUpdate`: an XP gain from C++ repaints the tree only for a state change, a reveal threshold or 1% of ring; the spell card is rebuilt only when it must |
| `hoverOverlay.js` | 255 | `HoverOverlay`: the hover preview (path, nodes, focus ring, bridges) painted over the tree layer and cached, so hovering never repaints the tree |
| `renderSettings.js` | 182 | The render popup (gear in the zoom bar), one page of chips: the "still everything" master switch, moving parts, what is on the tree; the star twinkle switch; puts saved values back on the popup and on Settings > Tree View |
| `designEffectsSetting.js` | 119 | Render popup chips for a design's page, ink reveal, sigil, learning glow, heart runes (`TreeStyle.setEffectsOff`); greys out what the design lacks and the starfield under a page |
| `requiredXPSync.js` | 80 | `RequiredXPSync`: sends C++ the panel's required XP for learning targets when C++ reports another number (after a load, or when a known higher spell or a share slider changes it) |
| `progressionUI.js` | 547 | How-to-Learn panel, learning status badges |
| `difficultyProfiles.js` | 429 | Profile management, presets, custom profiles |
| `llmApiSettings.js` | 230 | OpenRouter API configuration UI |
| `buttonHandlers.js` | 264 | Scan, learn, import/export button handlers |
| `cppCallbacks.js` | 438 | C++ SKSE plugin callback handlers |
| `llmIntegration.js` | 621 | LLM tree generation, color suggestions |
| **script.js** | 802 | Main init, tabs, dragging, early learning |
| **TOTAL** | ~8245 | |

## Load Order (index.html)

Modules must load in dependency order before `script.js`:

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

<!-- 3. DSL and Parsers -->
<script src="modules/growthDSL.js"></script>
<script src="modules/treeParser.js"></script>

<!-- 4. Renderer -->
<script src="modules/wheelRenderer.js"></script>

<!-- 5. UI Panels -->
<script src="modules/settingsPanel.js"></script>
<script src="modules/treeViewerUI.js"></script>
<script src="modules/progressionUI.js"></script>
<script src="modules/difficultyProfiles.js"></script>
<script src="modules/llmApiSettings.js"></script>
<script src="modules/buttonHandlers.js"></script>

<!-- 6. Integrations -->
<script src="modules/cppCallbacks.js"></script>
<script src="modules/llmIntegration.js"></script>

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
growthDSL.js          (no deps)
treeParser.js         (uses: state.js)
    ↓
wheelRenderer.js      (uses: state.js, config.js, colorUtils.js, treeParser.js)
    ↓
settingsPanel.js      (uses: state.js, constants.js, colorUtils.js, uiHelpers.js)
treeViewerUI.js       (uses: state.js, wheelRenderer.js, colorUtils.js)
progressionUI.js      (uses: state.js, wheelRenderer.js, uiHelpers.js)
difficultyProfiles.js (uses: state.js, constants.js, uiHelpers.js)
llmApiSettings.js     (uses: state.js, uiHelpers.js)
buttonHandlers.js     (uses: state.js, treeParser.js, wheelRenderer.js, spellCache.js)
    ↓
cppCallbacks.js       (uses: state.js, treeParser.js, wheelRenderer.js, spellCache.js)
llmIntegration.js (uses: state.js, growthDSL.js, wheelRenderer.js, colorUtils.js)
    ↓
script.js             (uses: all modules)
```

## Key Global Objects

| Object | Module | Description |
|--------|--------|-------------|
| `DEFAULT_TREE_RULES` | constants.js | Default LLM tree generation rules |
| `DIFFICULTY_PROFILES` | constants.js | 6 preset difficulty profiles |
| `KEY_CODES` | constants.js | Keyboard code mapping |
| `settings` | state.js | All user settings (persisted) |
| `state` | state.js | Runtime state (tree, selection, etc.) |
| `customProfiles` | state.js | User-created difficulty profiles |
| `xpOverrides` | state.js | Per-spell XP overrides |
| `TREE_CONFIG` | config.js | Tree layout configuration |
| `SpellCache` | spellCache.js | Spell data cache singleton |
| `TreeParser` | treeParser.js | Tree parsing utilities |
| `WheelRenderer` | wheelRenderer.js | SVG rendering engine |
| `GROWTH_DSL` | growthDSL.js | Procedural tree visual DSL |

## Key Functions by Module

### constants.js
- Exports `DEFAULT_TREE_RULES`, `DIFFICULTY_PROFILES`, `KEY_CODES`, `DEFAULT_COLOR_PALETTE`

### state.js
- Exports `settings`, `state`, `customProfiles`, `xpOverrides`
- `updateSliderFillGlobal(slider)` - Update slider fill visual

### colorUtils.js
- `getOrAssignSchoolColor(school)` - Get/create school color
- `applySchoolColorsToCSS()` - Generate dynamic CSS
- `updateSchoolColorPickerUI()` - Update color picker UI

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
- `window.onScanComplete(data)` - Spell scan callback
- `window.onTreeDataReceived(data)` - Tree load callback
- `window.onProgressionDataReceived(data)` - XP data callback
- `window.onSpellLearned(data)` - Spell learned notification

## Backup Files

- `script-backup.js` - Pre-modularization backup
- `script-full-backup.js` - Complete original (8190 lines)

## Notes for LLMs

- **Read one module at a time** - Each is self-contained for its domain
- **Check dependencies** - Load order matters for global object availability
- **All modules use globals** - No import/export (browser compatibility)
- **Settings persistence** - Handled by `settingsPanel.js` via C++ bridge
- **C++ callbacks** - All `window.on*` functions in `cppCallbacks.js`
