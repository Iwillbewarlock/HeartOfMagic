# Heart of Magic — UI & Feature Design

> Last updated: 2026-02-09

## UI Structure

The mod runs inside a single PrismaUI panel (CEF/Ultralight overlay) toggled with **F8**. Three top-level pages, plus modals.

```
┌─────────────────────────────────────────┐
│  [Spell Tree]   [Spell Scan]  [Settings]│  ← header tabs
├─────────────────────────────────────────┤
│                                         │
│           Active Page Content           │
│                                         │
└─────────────────────────────────────────┘
```

Default landing page: **Spell Tree**

---

## Page 1: Spell Tree (`contentSpellTree`)

The main gameplay page. Shows the interactive spell tree after it's been built.

**Modules:** `treeViewerUI.js`, `canvasRendererV2.js` (and its `canvasRenderer*.js` method files), `wheelRenderer.js`, `treeCore.js`, `progressionUI.js`

### Layout

```
┌──────────────────────────────────────────────┐
│ [Zoom -][100%][Zoom +]  [Heart⚙] [✏ Edit]   │
├──────────────────────────────────────────────┤
│                                              │
│         Canvas 2D Spell Tree                 │
│         (radial/wheel layout)                │
│         Pan + zoom + node click              │
│                                              │
│    ┌─────────┐              ┌────────────┐   │
│    │ Details │              │  How-to    │   │
│    │ Sidebar │              │  Learn     │   │
│    │(on click)│             │  Panel     │   │
│    └─────────┘              └────────────┘   │
├──────────────────────────────────────────────┤
│ [Import] Spells: 247  Legend                 │
└──────────────────────────────────────────────┘
```

### Features
- **Canvas 2D renderer** — primary renderer for 200+ node trees
- **Node interaction** — rest the cursor on a spell to preview its card, click to select it (see
  *Hover preview* below); the panel's close button, a click on empty canvas or Esc drop the
  selection. Esc works in two steps (`initializeKeyboardShortcuts` in `script.js`): with a spell
  selected on the tree the first press only drops it, the next closes the panel. An Esc a dialog's own
  field already used (Find Spell, a rename box) does neither
- **Tool bar** (zoom, home, Find Spell, render settings, edit; top left of the tree) — `.tree-top-tools`
  stacks it with the school tabs and the trait filter, so a bar that wraps pushes them down. The grip at
  its bottom-right corner (the corner away from its anchor) sizes the whole bar 1x - 3x by dragging
  (`modules/treeToolsScale.js`: a `scale()` transform plus a margin of the same growth so the rows below
  move; the bar's max width is the tree's width over the scale so it wraps rather than runs off). Double-click
  the grip for 1x. Saved as `treeToolsScale` with the panel settings, 1x on Reset to Defaults; the grip takes
  its own mouse presses (no tree pan), and a release the page never hears (a move with no button held, the
  page losing focus) ends the drag. The render settings and Find Spell popups open under the bar's scaled
  bottom (`TreeNav.placeUnder`)
- **Find Spell** — the magnifier button shows a dropdown under the bar with a search field and the list; the
  button again, Esc in the field or choosing a spell hides it (there is no F key since 1.2.1), and so do
  switching tabs, closing the panel (button, hotkey, Esc) and loading a tree (`dismissFindSpell`), so a
  dropdown out of sight never takes an Esc. Matching is `SpellSearch` (`modules/spellSearch.js`): the shown
  (translated) name or the school - English or its name in the panel's language (`chips.school.*`, also what
  the list shows) - ignoring case and spaces ("헝거소환" finds "헝거 소환", and the highlight ignores spaces
  too); the formId only for a text that looks like one (`0x...` or 4+ hex digits, so "fe" does not list every
  ESL spell). An exact name first, then names starting with the text, then the other name or school matches,
  then formId matches, alphabetical in each, at most 80. In discovery mode without cheat mode locked spells
  are left out (before the tree is drawn, when only the raw tree data without spell states is there, nothing
  is listed). Enter or a click centres the tree on the spell and selects it; the status line says "Found:
  <name>" (`tree.findFound`). Its CSS is in `patch-tree-tools.css` with the tool bar's
- **Details sidebar** — spell name, school, tier, effects, prerequisites, learning controls
- **Spell card** — the details panel reads top to bottom as a card: name (+ school badge) →
  keyword chips → description → figures (cost, type, effects with magnitude and duration).
  It opens in that order as learning progress grows: name at `revealName`, chips at
  `revealEffects`, description and figures together at `revealDescription`. Tier is always
  shown for non-locked nodes because the node's size already gives it away.
  Chips are stable ids built in C++ (`SpellScannerChips.cpp`, sent as `chips[]` by
  `GetSpellInfo`) from closed engine sets only — resist value, projectile type, delivery,
  archetype, school — and from the **base game's own magic keywords** (`MagicSummonFire`,
  `MagicSummonUndead`, `MagicRune`, `MagicWard` ...; only keywords a vanilla plugin defines, fixed
  table in `SpellScannerChips.cpp`). Never from names or mod keywords, so they are the same in
  every language and load order. The vanilla keywords are what give a summon its element: Flame
  Atronach reads as fire + summon, which is what lets a fire mage's branch include it. Chips only read effects the record does not flag **Hide in UI**:
  mods hang helper effects on spells built from whatever MGEF was at hand (in this load order
  vanilla Fire Storm carries a hidden `ScreenShake` that resists frost, which put a Frost chip
  next to Fire). If every effect is hidden, all of them are read. Since 2026-09-27 the element chips
  come from the tag librarian's catalog when it knows the spell (`Librarian::MergeCatalogChips`,
  catalog kept in memory): blood, water, air, shadow ... show and filter like fire does, a wrong
  vanilla-keyword element a rule removed is gone, plain Damage drops once an element is there, and the
  school chip is never the one trimmed. The catalog itself is rule data, so this stays load-order independent. `modules/spellCard.js` translates them through `chips.*` lang keys
  and falls back to English labels for languages that do not have the keys yet.
  Works in both the bottom bar and the side layout.
  **Icon** in front of the name. The mod draws no icons of its own: **Kome's Inventory Tweaks (KIT)
  is the icon requirement**, and its Wheeler SVG set is read from the player's install
  (`Data/SKSE/Plugins/wheeler/resources/`), never bundled. Picked in C++ (`SpellScannerCard.cpp`),
  best source first:
  1. the pack author's own choice - a keyword on the spell, then on its effects, that has
     `icons_custom/KWD_<keyword>.svg`. Any custom I4 icon pack that ships Wheeler SVGs counts.
  2. the **icon rules**, `SKSE/Plugins/SpellLearning/card_icons.json`. A rule is "spell has these
     traits -> this file", traits being the uncapped chip ids (`BuildSpellTraits`: element.fire,
     kind.cloak, kind.rune, kind.summon, school.* ...). Rules run top to bottom, specific first
     (fire + cloak before fire), and a rule only counts if its file is installed. `{S}` is the
     school letter KIT suffixes its files with (A C D I R), `{school}` the school name. The last
     rules are the vanilla school emblems (`icons/destruction_fire`, `icons/{school}`).
  `schoolIconKey` is always the plain emblem; while the name is hidden only that shows, since a
  pack icon or an element variant would give the spell away. Pictures are fetched on demand
  (`GetSpellIcon` -> `updateSpellIcon`) and shown as an `<img>` data URI, so nothing inside a
  mod's SVG can run. Without the icon set there is no icon.
  What the rules are and are not: the trait side is closed engine sets, the file-name side is a
  hand-written list of KIT's names as of 2026-09. If KIT renames a file that rule goes quiet and
  the next one (in the end the school emblem) takes over; the file is data so it can be fixed or
  pointed at another pack without a new DLL. On the dev load order (1440 tome spells): 977 get
  the pack's own icon, 266 an icon rule (248 of those the generic summon), 197 the school emblem,
  0 nothing. The SWF icons the inventory menus use cannot be drawn in a web view.
  **Effects list** is hidden outside edit mode: it shows how a spell is wired (helper effects,
  duplicates, internal names), which the description and chips already say in player terms.
  **Description**: `<mag>`/`<dur>`/`<area>` are filled in from the effect and `<25>` style emphasis
  marks are stripped, since the web view does not do what the game menus do with them.
  **Names in the card's lists** (Unlocks, hard/soft prerequisites, Locks, *Paths to other schools*) come
  from one helper, `spellDisplayName(id, node, shown)` in `modules/spellNames.js`. Two plugins can
  name two spells alike - NoviceBoltSpells.esp adds its own Flames, and in the Korean game both read
  불씨조각 - and the Classic builder, which scores name and effect alike, links such spells to each
  other, so Flames looked like it unlocked itself. A name that more than one spell in the loaded tree
  carries is shown with its plugin, `불씨조각 (NoviceBoltSpells.esp)` next to
  `불씨조각 (Skyrim.esm)`; a name no other spell has stays as it is. The plugin is the spell info's
  `plugin` (SpellCache), or the plugin half of the node's `persistentId` until that is in. What is
  counted is the different spells carrying a name, **locked ones included**: a label then stays the
  same as the player unlocks spells, instead of growing a plugin the moment the other copy opens. An
  edit-mode duplicate (`originalFormId`) is the same spell as its original - counted once with it
  and named by the original's plugin. The counts are made once per loaded tree and again after spell
  info renames a node or edit mode adds or removes one (`SpellNames.invalidate`, and
  `invalidateNodeLookup` so the id lookup the lists use finds the new node). A hidden name
  (locked, cheat mode off; a Lock not yet revealed) is `???` with no plugin either. Plain text
  (the Locks row too, no longer built as HTML), so every design shows it alike.
- **How-to-Learn panel** — shows what the player needs to do to unlock a spell
- **Discovery mode** — hides spell names/effects until XP thresholds are met
- **Zoom/pan** — mouse wheel + drag; one wheel notch zooms by `CanvasRenderer.WHEEL_ZOOM_STEP` (15%, was 10%; out is the exact inverse of in, so a notch in and out returns to the same zoom)
- **Heart Settings popup** — cosmetic settings (starfield, globe, dividers, connections, node sizes)
- **Edit mode** — move nodes, pen tool, eraser (developer feature). A spell's prerequisites are three
  lists: `hardPrereqs` (Required, all needed, "H" on the card), `softPrereqs` (Optional, `softNeeded` of
  them needed, "S") and `prerequisites` (the links drawn as tree lines; a PreReq Master lock is hard and a
  cross-school bridge soft without being in it; C++ never reads it - the panel sends hard, soft and
  `softNeeded` only - and a saved node with hard and soft both empty gets them from it at load, `treeLoad.js`).
  `modules/prereqEdit.js` (`PrereqEdit`) keeps them right on every edit: a pen link goes into
  `prerequisites` and Required once, H/S moves an id from one list to the other, × and the eraser take it
  out of every list, `softNeeded` is held to the soft list (and raised to 1 when an id moves into a soft
  list that needed none: a soft list with `softNeeded` 0 is met by nothing, so the prerequisite would be
  gone), and undo puts back the type, the line and `softNeeded`. An id is never in both hard and soft, and no two lists share an array. 1.2.0 built trees
  with one array for `prerequisites` and `softPrereqs`, so a link landed in both, a delete took both and
  H/S made a second entry; a tree saved that way is repaired when it loads (`PrereqEdit.repair`, called by
  both paths in `treeLoad.js`: an id in both lists stays Required only, repeats go). A PreReq Master lock
  on the layout parent is in both lists from the build, not from that bug, and already met `softNeeded`:
  taken out of soft, `softNeeded` goes down by one for each, so a bridge left in soft stays optional. A
  repaired node is written back to the saved tree (`PrereqEdit.copyTo` on `state.treeData.rawData`'s node),
  so the next save carries it and the card and C++ judge by the same lists before any edit. Deleting a spell
  (the eraser on a node) takes it out of every other spell's three lists, `children` and PreReq Master
  locks and drops the saved tree's bridges to or from it (`PrereqEdit.forget` / `forgetInTree`), so no
  spell waits on it after a reload; undo entries that name it are dropped, and an undo whose spells are
  not both in the tree changes and saves nothing. A school's root (`isRoot` or `schools[name].root`) is
  refused with a status line (`editMode.rootNotDeleted`), before Shift+eraser blacklists it: its children
  would be left with no prerequisite, and a reload would make the school's first node the root.
  The edits reach C++ (2026-10-17; before, only the next tree load sent them, so a tome was judged by the old
  links): every prerequisite edit and every added spell marks the tree changed, and leaving edit mode - or
  closing the panel with edit mode still on (`onPanelHiding`) - sends the whole tree's lists once
  (`EditMode.commitPrerequisites` -> `sendTreePrerequisites` in treeLoad.js, the same call a tree load makes)
  and recounts which spells are available. A spell added with Spawn keeps the scan's `persistentId` on its node
  and in the saved tree, so a load order change moves it like any other node; a second copy of a spell on the
  tree (`_dup_` id) gets none, since the persistentId would turn it into the original's formId.
- **Empty state** — shown when no tree is loaded, prompts user to scan
- **Font Size** (Settings, 0.7x to 2.0x; 1.5x before 1.2.1) — `applyFontSizeMultiplier` sets the body font
  (`PANEL_BASE_FONT_SIZE` times the value) that the page's em sizes follow, and `CanvasRenderer.labelScale`: the tree's spell
  names are Spell Name Size times it (a new size is a new label font, `LayerBuild.noteRestyle`). The spell
  card's sidebar width and the bottom bar's height and fixed columns are in em of that font, so the card
  grows with the text instead of cutting it off (at 2.0x the bottom bar is twice as tall and leaves the tree
  less room). Icon buttons keep their px sizes.
- **No code in front of the player** (2026-10-05): the spell card never shows a FormID or an English placeholder. A
  spell with no name reads `???` (cheat mode too; the `[LOCKED]` tag went - the Locked badge says it), a school the
  game does not name shows no badge, the badge, the level and the casting type are in the panel's language
  (`chips.school.*`, `settings.progression.tier*`, `details.typeFireAndForget`, `chips.cast.concentration`,
  `details.typeConstantEffect`; `SpellCard.levelText` / `typeText`), and Find Spell's "Found" line and the wheel's
  label fall back to `???` as well.
- **Smallest text** (2026-10-05): no panel text is under 0.75em of the body font (12px at 1.0x). The stylesheets'
  0.65em / 0.7em sizes went to 0.75em / 0.8em, fixed 8-12px sizes (styles, the Classic builder's settings,
  the colour picker, the complex tab's status) became em so they follow Font Size, a subsection's titles and
  descriptions are 0.9em of its 0.85em, and select boxes (language, design) are 0.9em. The Classic builder's
  Spell Matching descriptions and the Globe Mask unit are language keys (`preview.classic.match*Desc`,
  `preview.classic.tiersUnit`).
- **Default text sizes** (1.2.1, `modules/constants.js`): body text `PANEL_BASE_FONT_SIZE` 16px (was 14px; the
  `body` rule in `styles-skyrim.css` says 16px too, for the moment before the settings load) and Spell Name
  Size `DEFAULT_NODE_FONT_SIZE` 12 (was 10; slider still 6 to 18). Only defaults changed: a saved
  `nodeFontSize` stays, and Reset to Defaults now puts Spell Name Size back to 12 too (it did not reset it
  before). The designs (`themes/design-*.css`, the Chalkboard and Night Grimoire presets) set no base or
  name size of their own, so all five follow. Three fixed px boxes now scale with the text: the power step
  rows' columns (em), the tree builder's setting cells (`min-width` 7.5em) and the How to Play side tab
  (`min-height`, grows to fit). Checked in the dev harness at 1.0x and 2.0x (1600x900): no text cut off or
  spilling that was not there at 14px; one left as it was - the last power step row's "100% Power" runs a
  little past its column at every size. The label pass culls by the label's own size, so 12 needs no change
  there.

### Decided: the tree stays 2D (2026-09-22)

A 3D tree was considered and dropped. PrismaUI has no WebGL (see the note in `index.html`), so 3D would
mean projecting every spell by hand onto the 2D canvas each frame, the way the centre globe does with 200
particles. A probe that rotated a tree sized scene - 1440 points, 2370 lines, depth sort, 150 labels - was
run in game on the dev setup and judged unusable by the mod's owner; it was removed again afterwards
(commit `4fbed77` has it). No frame numbers were kept: the probe only wrote its report at the end of the
run. Depth, if wanted, has to come from 2D means - layering, size, dimming - not from a camera.

### Decided: the view travels, the wheel does not turn (2026-09-22)

Focusing a spell or pressing a school tab used to turn the whole wheel so that school stood on top.
Going from school to school that way is dizzying, so `TreeCamera.computeTarget` now only pans (and zooms)
unless `settings.focusRotate` is on - a toggle in Settings > Tree View, off by default, that brings the old
behaviour back. Nothing had to change for the text: `renderLabels` draws every label upright after the
tree transform is undone, whatever the wheel's angle. A caller can still force either way with
`opts.rotate`.

### The home view fits the part of the tree that shows (1.5.0, 2026-10-14)

The spell card bar (bottom layout, about 206 px at Font Size 1.0x) and the side panel are drawn over the
tree canvas, and with the hover preview on they stay open even when empty (see *Hover preview*). The
first view of a tree and the Home button used to centre the heart on the whole canvas at zoom 0.75, so the
heart sat about half the bar's height below the middle of what shows and the lower rings and chapter
titles were under the bar. Now `TreeCamera.homeTarget` fits the tree into the part no panel covers:

- `TreeCamera.visibleRect` is the canvas less each showing panel that overlaps it - the details panel
  (cut off the bottom when at least half the tree's width, else off the side it sits on) and the open
  How-to-Learn panel. A panel with no size covers nothing; with the canvas itself unmeasured (another tab
  in front) it is `null`. `getPanelOffset` (where a focused spell lands) is the middle of the same rect, so
  click-to-focus and Home agree.
- `fitTarget` fits the shown schools' spells and the heart (`treeBounds`, rotation 0) into that rect with
  `HOME_PAD` (24 px) round them, centred on the box, never closer than `TreeNav.HOME_ZOOM` (0.75, the old
  home zoom) and no further out than `TreeNav.HOME_MIN_ZOOM` (0.5). A tree that only fits below 0.5 opens
  at 0.5 with the heart (the globe) in the middle of the visible rect: 0.5 is `CanvasRenderer.LABEL_MIN_ZOOM`
  (no names under it) and inside the full level of detail (`_computeLODTier`: 0.45 and up; 0.25-0.45 simpler,
  under 0.25 no spokes and spells as dots). A real 1,419-spell tree (about +-2,300 world units) would fit
  only at 0.11-0.14 in the 1600x1000 harness, an almost empty star field; the 753-spell mock fits at
  0.55-0.66 and is shown whole. Designs with chapter titles
  fit them with the tree: at each zoom tried, `TreeStyle.chapterBoxes` gives the boxes the titles would be
  drawn in (no pan, rotation 0, nothing culled), and width and height are fitted apart - a side title
  takes room across, a top or bottom title room down, each only its own box. The titles are screen-sized
  while the tree scales, so the zoom is found by halving (`FIT_STEPS`, 30; at most 32 `chapterBoxes` calls
  on a load or a Home press). In the 1600x1000 harness (540 px of canvas above the bar, 753 spells) the
  book designs open at 55-56% (53-54% with a uniform clearance from the widest title on all four sides),
  Classic 65% and Modern Dark 66%: the height above the bar is what limits it, and a title above and
  below the tree costs about ten points of zoom there.
- `CanvasRenderer.centerView` (a tree loaded) sets the old view and `_homePending`; the first frame that
  has the tree on screen fits it (`TreeCamera.settleHome`, at the top of `render` before anything reads
  the view), so a tree built on the Scan page fits when its tab is opened. A view asked for before that
  frame (`animateTo`, an instant focus) wins. Home animates to `homeTarget`. Only a render-loop frame
  settles it (`_loopFrame` counts them; `centerView` notes it in `_homeAskedAt`): the loads call
  `forceRender` in the same task, before `DetailsPeek.applyLayout` has opened the bar, and that frame is
  skipped (`_needsRender` stays set) rather than drawn at a view about to change.
- `animateTo` times its glide with `performance.now()` (the start's clock) and keeps t in 0..1: with the
  frame's timestamp a first step dated before the start gave t < 0, and the ease threw the zoom far out.

The bar is not shrunk or collapsed (a bar that grew on hover would move the tree under the cursor). Tests:
`homeViewTest.js`.

**Chapter titles clear the outer ring** (`TreeStyle._chapterPlaces`). A title used to sit 40 px past its own
school's farthest spell along the spoke. The outer ring at a spoke is often another school's, so the top
title lay on it, and a side title, 150 px wide, reached 35 px inside its ring. A title's middle now sits
`CHAPTER_GAP` (14 px) plus half the box along the spoke (`|cos| x half width + |sin| x half height` on
screen) past the farthest spell of any school under it - `_schoolReach` also keeps the farthest spell per
2 degrees round the heart (`_chapterBins`, once per tree and per set of hidden schools: a hidden school's
spells are not drawn and push no title out), and `_outerEdge` reads those within the box's half diagonal. Classic and Modern Dark have no titles and are unchanged. Tests: `chapterTitlesTest.js`.

### Performance: the tree layer and the developer read-out (2026-09-22)

The panel runs in PrismaUI's CPU-drawn view (`CreateView`; the accelerated call is commented out in
`UIManagerCore.cpp`), so paint calls are what cost. One frame of a 1440-spell tree is about 3,400 of them,
and the heart, globe and starfield asked for a frame 20 times a second, each of which redrew the whole
tree.

- **Tree layer** (`CanvasRenderer._drawTree`). Dividers, edges, nodes, bridges and labels are drawn into
  a see-through offscreen canvas (see-through because the starfield behind keeps moving) and pasted until
  the tree changes. "Changed" is caught in one place: `_needsRender` is an accessor whose setter raises
  `_treeDirty`, so none of the dozens of call sites in other modules had to change. Animation asks for
  frames through `_requestAnimationOnlyFrame`, which bypasses the setter. Particles the globe throws off
  live inside the layer and force a redraw while they exist. Measured in a desktop browser: an idle frame
  went from 3,407 paint calls to 179 (what is left is the starfield and the hub), 4.1 ms to 0.7 ms; away
  from the hub the picture is pixel-identical. `USE_TREE_LAYER = false` draws directly as before, and the
  renderer falls back by itself if the offscreen canvas cannot be made.
- **Drag was capped at the idle rate.** Every frame that drew the heart raised `_animationOnlyRender`,
  so the loop's 20-a-second throttle also held back frames the player had asked for by dragging or
  zooming. The throttle now only applies when the tree is not dirty.
- **Labels are part of the layer**, so they are drawn before the hub instead of after it.
- **Developer read-out** (`modules/perfMeter.js`, developer mode only): draw time and frames a second,
  the longest gap between frames, and for the last mouse press how long it was held, how far the cursor
  went, how late the events arrived and whether it counted as a click or a drag. The desktop numbers
  above say nothing about the game; this is how to get the game's.

**Hover no longer repaints the tree** (`modules/hoverOverlay.js`, 2026-09-25). The hover preview - the
hovered spell's path in its school colour, the brighter nodes on it, its focus ring, its bridges - was
drawn into the tree layer, so every mouse move onto another spell threw the layer away: 40 hover changes
were 40 repaints of all 1,440 spells. The layer is now drawn with the hover state hidden
(`HoverOverlay.withoutHover`), and `HoverOverlay.composite` keeps a copy of the layer with the preview
painted on, rebuilt only when the hovered spell, the selection, the level of detail or the layer changes;
`_setHoveredNode` asks for an unthrottled frame that pastes it (`__needsRender`, not the dirty-marking
setter). A root's preview reaches its whole subtree, so painting it per frame would have cost more than
it saved; pasted, a frame costs the same with or without hover. Measured: 40 hover changes, 0 repaints;
the picture matches the old one. The hovered spell's label no longer wins label collisions (its hover card
shows the name), and the preview sits over labels rather than under them. `_renderNodeSimple` was split
out of `renderNodesSimple` so the overlay draws the hovered spell exactly as that level of detail does.
**On its own spot** (2026-09-26): with FxLayer on, the preview is no longer painted on a copy of the layer
(a hover change repainted the whole tree canvas, and once more for its StaticBase picture) but on a spot
of its own (`HoverOverlay.drawSpot`, key `hover`) over the box its spells cover (`_spotBox`: the path, the
hovered spell, its bridges' other ends; `SPOT_PAD` round them), under the other spots (`FxLayer` option
`under`) and kept while the hover, selection, tree and view stay the same (option `version`). The tree
canvas is not touched by a hover change at all (bench: 39 draws on it before, 0 now); the picture is the
same. Without FxLayer, `composite` works as before.
**Wheel over a spell** (2026-10-04). Zooming with the cursor on a first-tier spell stalled: the spot is
painted again at every zoom step, and its path (619 spells for Soul Trap) was drawn spell by spell with
`renderNode`, 55-80 ms, twice per wheel step (measured in the PrismaUI Lab, `D:\DEV\prismaui-lab`, the
game's Ultralight engine). Now: the path's plain spells go into `NodeBatch` like the layer's
(`_batchPlainNode` takes hover-path spells, at least `HOVER_PATH_ALPHA` bright as `renderNode` draws them);
path spells and lines off screen are left out (`_screenWorldBox`); the spot's version follows
`_treeChanges` (bumped by the `_needsRender` setter) and the view instead of the tree layer's draws, so the
layer being built again over the frames after a zoom step no longer paints the spot a second time; and
`_spotBox` is kept while nothing it reads changes. Lab, wheel over Soul Trap: p95 60 -> 7 ms, worst 73 ->
12 ms, no frame over 16 ms; hover at rest unchanged (0.72 ms, one paint); the picture is the same, except that where path spells overlap they now stack as the layer's batch does (halos under bodies). The spot also sets the hand-drawn look itself (`TreeStyle.beginInk`, put back after): it used to take whatever the last layer draw left, which after a level-of-detail change was the old level's.

**Smaller things in the repaint** (2026-09-26):
- The tree layer's margin is 128 css px, not 256 (`TREE_LAYER_MARGIN`): for the default panel the layer was
  about 1.6 times the pixels, cleared and copied on every repaint. (A drag past the margin now shifts the
  layer instead of repainting it, see below.)
- The simple level of detail (zoom 0.25-0.45) batches its spells too (`NodeBatch.begin(true)`: shapes not
  turned, as that level draws them): 3,916 → 116 paint calls per repaint in the bench; the selected and
  hovered spell are still drawn on their own.
- The tree canvas has a whole-pixel size (`updateCanvasSize` floors the container's), and the panel sits on
  whole pixels (`modules/panelSnap.js`: a margin under a pixel when centred, rounded drag positions;
  again on window resize, a panel resize and fullscreen). Otherwise the view resampled the canvas each
  time it painted it.
- Labels: a spell off the canvas is dropped before the name-reveal lookups, the shortened name is kept on
  the spell (`_labelText`), and the text widths per font (`_labelWidth`; since 2026-09-28 also dropped by
  `TreeStyle.fontsChanged`, since a web face arriving leaves the font string as it was and the widths kept
  were the fallback's - boxes too wide or narrow, and half names at strip edges). The names in the layer's margin
  are drawn too, after those in view (`renderLabels`' `margin`): a drag that slid the layer used to show
  spells there without names until the next repaint.
- A reply with spell names (`updateSpellInfoBatch`) now marks the canvas tree for a repaint; it only
  called the SVG renderer, which holds no nodes while the canvas one is in use, so the names showed with
  the next repaint for something else.
- Tried and dropped: recording the calls of a tree repaint and replaying them when only the view moved
  (zoom settling, glide end). In the desktop bench the recording made a tree change 8.6 → 21 ms (every
  call through a wrapper) and a replay drew as much as the whole recorded area, never less than a culled
  repaint; in game a repaint is already 12-19 ms (`[Perf]` worst).

**A drag shifts the tree layer** (`modules/layerScroll.js`, 2026-09-27). Once a drag had used up the
layer's margin, the whole layer was repainted - every spell, line and name - in the middle of the drag,
every ~150 px: on a CPU canvas in the desktop bench (1600x1000, every locked spell shown) 25-60 ms each,
nine hitches per full circle of dragging. Now, once half the margin is used (`EARLY_SHARE`), the picture
is copied onto a spare canvas moved by a whole number of device pixels (the spare cleared, then the
picture drawn source-over; `'copy'` compositing until 2026-09-28, dropped because an engine that
composited it as source-over would let the spare's old picture show through; the two canvases swap)
and `_layerPanX/Y` move by that much, so the paste's rounding keeps the remainder and nothing blurs. The
strips the move uncovered join a queue of pieces, each drawn clipped to itself with the spells and lines
culled to its world box (padded by a selected spell's halo; curved and hand-bowed lines widen their own
box by their bulge). A piece the screen shows, or will within `LOOKAHEAD_PX`, is drawn in that frame
whatever it costs; the others fill what is left of the frame (`TARGET_FRAME_MS` 8 from the frame's start,
the next piece at its running average cost, one at least) and the rest ask for the next frame through `__needsRender`
(animation frames are throttled and stop while a button is held, so without it a paused drag would leave
a strip bare). Another shift moves the queued pieces with the picture (`moveRects`). Names are
screen-aligned and must not overlap, so a name cut by a strip edge would show as half a name: the layer
keeps the names it placed (`_layerLabels`, from `renderLabels`), moves them with the picture, draws the
ones reaching into a strip (outline and descenders counted) again clipped to it, and places new names
only inside strips, against the kept ones; the name cap (150, 50 below zoom 0.8 since 2026-10-09) counts the names on screen, not those left
in the margin behind the drag nor the selected spell's (as `renderLabels` counts; since 2026-10-10) (with twice that kept on the layer at most) (`renderLabels` was split into `_labelCandidates`, `_labelRect` and
`_keepLabel`, which both use). Chapter titles are drawn through the layer's margin (`renderChapters`'
`margin`): culled at the screen edge, a strip in the margin never got the title a drag then brought into
view. In a strip they go on after its names (`LayerScroll._chapters`, clipped to the strip), as a whole
repaint draws them; until 2026-09-28 they went in with the tree, under the names, which left a seam at a
strip's edge where a name crossed a title. A new name is drawn whole, so it can reach past its strip onto
the old picture and over a title there (seen in Night Grimoire after a sideways drag); where such a name
meets a title outside the strip (`TreeStyle.chapterBoxes`), that spot is drawn again like a strip - tree,
the kept names, titles - with no new names placed (`LayerScroll._underTitles`, 2026-09-28): one spot per
title round every spilled name on it, the title boxes worked out once a frame. Bench (Night Grimoire, 8-frame
sideways drags): one spot a drag, 0.3-1.0 ms in all; against a whole repaint with the same names the title
boxes differ only where the strips' lines already did (11 px, 20 with no names drawn; 31 without the fix). A drag also lets go of the hover (the preview would be redrawn every frame of it). A full repaint
as before when the tree changed, the zoom or rotation changed, the layer is stale or edit mode is on.
Measured (circular drag, 120 frames, four designs, zoom 0.75-1.3, CPU canvases): frames over 16.7 ms 6-9
→ 0-2, worst frame 20-32 → 15-20 ms; a piece costs 2.5-8 ms, about 1.7 ms of it its own (chapter titles,
the name candidates, worked out once a frame), which is why strips are not cut smaller. The bench's full
repaint (25-60 ms) is heavier than the game's, which logged 12-19 ms worst (`[Perf]`), and it has
run-to-run noise of a few ms.

**A whole repaint spread over frames** (`modules/layerBuild.js`, 2026-09-27). A click on a spell (its path
highlighted, the rest dimmed; after the camera's glide) and a wheel zoom coming to rest repaint the whole
layer - one 33-40 ms frame each in the bench. Now, when the last whole repaint took more than
`SYNC_MAX_MS` (8), the new picture is drawn onto the spare canvas in 384 px pieces, nearest the middle
first, as many a frame as fit in what is left of it (`TARGET_FRAME_MS` 6 from the frame's start - the
paste, the heart and the moving parts come after the tree - the next piece at its running cost, one at
least), then the names and chapter titles whole, in a frame of their own if they do not fit; the
canvases swap when it is done. Each unfinished frame asks for the next (`__needsRender`). Until then the
old picture stays up, mapped onto the view as during a glide (moved, stretched, turned; the hover
preview is not composited onto a stretched layer).

A camera glide says where it ends (`TreeCamera` sets `CanvasRenderer._glideTarget`): the picture is
built for that view - pan, zoom, turn and level of detail swapped in while its pieces are drawn - during
the glide, so after a click the highlighted tree is usually ready before the camera arrives, however far
it went (bench: done by the fifth of 18 glide frames). A change during the glide restarts it. A build is
dropped when the view it was for is gone (zoom, rotation, glide target, pixel ratio, layer size, edit
mode) and the tree is then marked for a repaint again, so the change it carried is not lost. Still done
at once: a quick one (undiscovered spells hidden: 7-9 ms; not right after a design switch, see *A new face's
names* below), the first, one with no old picture (stale
layer, resize) and edit mode. When the view, held still, is past the old picture's margin (a jump
without a glide, a drag during the build) the build goes on urgently (2026-09-28; it used to finish in
that one frame): `URGENT_TARGET_FRAME_MS` (11) of the frame, the pieces the screen shows first, and the
swap as soon as those and the names are done; the pieces left go to LayerScroll's queue and are drawn
like a drag's strips, in the frames' time left from the next frame on (the swap asks for it at once,
unthrottled - animation frames come only ~12 a second and none when idle). A frame that pastes the
layer stretched (a wheel zoom or a glide straight after the swap) draws them too
(`LayerScroll.drawPendingAside`, 2026-09-28): into the layer on screen at the view it was drawn for (its
zoom, turn and level of detail), the pieces the stretched paste shows first (the screen's corners mapped
back through the paste, `_stretchedViewRect`), all within the frame's time left - a zoom out can show
every leftover piece at once (4-6 on a 1080p screen, 2-4 ms each), too much for one frame, and the view
is moving anyway. Not with a build under
way - its pieces have the frame, and its swap replaces that layer and drops them - nor with the tree
changed since, when they would show the new tree in the old picture, seams at the pieces' edges. Before,
those frames showed the leftover pieces as empty rectangles in the margin (the names drawn over them)
until the view came to rest. The old picture's bare edge shows for the frames that takes, as during a glide. Bench (a click's build, then
the view 200 px past the margin and held, CPU canvases, five designs, a quiet run): the frame that
finished the build went 65-76 ms → none; the urgent frames take 10-14 ms, the swap comes on the 4th to 6th
of them (the bare edge shows that long, then one frame for LayerScroll's shift), and the layer is whole
2-3 frames after. On a busy machine the same run gave 177-239 → 27-42 ms worst. A
tree that keeps changing faster than a build ends (`MAX_RESTARTS`, 4, builds started again in a row; the
count starts over when a build is done, drawn at once or dropped for another reason) is repainted at
once: one long frame, kept over letting each build finish with the next change pending, which would show
the tree a change late for as long as the changes keep coming. The pieces' own costs (dividers, clipping, bridge markers) make a build a little more
work than one repaint at once; `_lastMs`, which decides sync or spread, is what the last repaint at once
took, and a build leaves its pieces' overhead out but once (the cheapest piece stands for it). A clearly
quick build (`QUICK_SHARE`, half of `SYNC_MAX_MS`) sets it; a slower one raises it; one in between leaves
it: a slow tree is not sent back to long frames because its culled pieces cost less than one whole
repaint (in the bench a build's pieces took 8-10 ms where a repaint at once took 12-27 ms of drawing
calls - above `SYNC_MAX_MS`, so it stays spread). Lowering it a little each such build
(`LAST_MS_DECAY`, 0.9, for a day on 2026-09-28) was taken out again: a slow tree whose builds look quick
then tried a repaint at once about every 11 builds - one long frame each time. A tree slow once (a hitch)
and middling since stays spread; that costs its repaints a frame or two of the old picture, not a long
frame. The repaint right after the caches go is not counted (`LayerBuild.noteRestyle`, called by
`TreeStyle.set` - a design applied, Design Effects toggled -, `TreeStyle.fontsChanged` - a design's
stylesheet or a web font arriving late - and `switchLocale`): it makes the sprites, patterns and text
widths the first time (the 18-26 ms outlier below) and, counted, would keep a quick tree spread for good.
When that uncounted repaint was slow (over `SYNC_MAX_MS`) the figure still goes just past `SYNC_MAX_MS`:
the new design may be heavier (a light one to one with page, sigils and glow), and left low the next
click was a repaint at once - one long frame; now it is spread, and its build sets the figure.
A build swapped in early still made them, so it clears the flag too. With nothing measured yet - the
session's first repaint, as the saved design is applied at start-up - its cost is the figure all the same,
so a slow tree's first click is spread; later builds change it by the usual rule. Replacing that first
figure outright with the next build's (for a day) let a slow tree whose culled builds look middling drop
below `SYNC_MAX_MS`, and the click after was one long frame. A quick tree measured high at start-up stays
spread unless a build comes out clearly quick: a frame or two of the old picture per repaint, no long
frame. (Repainting at once while the panel is hidden, to measure, was not taken: the page still runs in
game while hidden, and a 12-19 ms task there can cost a game frame - not measured.) Measured (8 clicks with glides and 6 wheel zooms per design, four designs, CPU
canvases): the 33-40 ms frame is gone; frames after a click or zoom stop are 3-9 ms, with a rare
18-26 ms outlier right after a design switch (sprites, patterns and text widths made the first time).
Unlike the dropped "recording the calls" attempt below, each piece is a culled repaint of its own box.

**The design page painted over frames** (`TreeStyle.stepPage`, `modules/treeStyleBook.js`, 2026-09-28).
A page design's texture (Arcane, Night Grimoire, Chalkboard) costs 25-30 ms to paint on a CPU canvas at
1640x1160 - the grain's 40 soft blotches 13 ms, the light in the middle and the dark edges about 6 ms
each (gradients over most of the screen), the colour, fibres and corners 1-2 ms. It used to be painted
whole in the first frame after a design switch - with the build's first piece, a 35-50 ms frame, often
the frame of the next click - and again when the page's corner drawing finished loading, which in game
can land mid-glide: the image's `onload` drew a whole frame there and then (`forceRender`), page
included, and marked the tree changed, which restarted the glide's build. Now the texture is painted in
steps, the same calls in the same order - the colour, each blotch, the fibres (one stroke), the light in
`PAGE_BAND_PX` (128) bands, the corners, the dark edges in bands - into a canvas of its own, and swapped
in (`_pageBuilds` + 1, so `StaticBase` notices) when the last step is done. A gradient is a function of
the pixel, so the bands, with whole-pixel edges inside the page and the page's own edges outside, add up
to the one rect: the finished texture is pixel-identical to the one painted in one go (compared in the
bench for the three page designs at 1640x1160, 1641x1159, 1280x720 and 1000.5x700.25, corners loaded:
no byte differs; in Chrome - the game's engine was not compared, though the calls are the same). A frame paints steps after its tree (LayerBuild's pieces), up to
`PAGE_TARGET_FRAME_MS` (8) from the frame's start (the largest steps - the fibres, one stroke, or a big
blotch - are 1-1.5 ms at 1640x1160 and grow with the screen's area), and asks for the next frame without
marking the tree changed; it takes 9-12 frames at that size. A frame paints no step when the next one, at
the last step's cost, would end past `PAGE_FRAME_DEADLINE_MS` (14; at 4K a step after an urgent build's
11 ms would pass 16.7 ms), unless `PAGE_MAX_SKIPS` (8) frames in a row had none: then one, so a page still
comes while every frame is busy. (Skipping once past the 8 ms fill target instead made a big screen whose
frames take 8-12 ms anyway paint a step one frame in nine - ~10 s of the plain colour.) (That request, and a build's,
used to be turned into a throttled animation frame - ~12 a second, none while a button is held - by the
heart beating later in the same frame; `_requestAnimationOnlyFrame` now leaves a frame already asked for in
full as it is.)
Meanwhile the page shows the texture it had if that is of the same colour (a resize, the corner drawing
arriving: the page without corners stays until the one with them is done; an add-on design sharing another
design's page colour would show that one's texture for those frames), else the design's plain colour. The texture is kept while its look stays (the page tokens and whether the corner drawing is in,
`_pageLookKey`): toggling a Design Effect other than the page no longer repaints it, and a design
without a page drops it. The design is picked on the settings tab, where the tree's render loop is
stopped, so `TreeStyle.set` also starts a timer that paints it in `PAGE_IDLE_STEP_MS` (4) slices, one
every 16 ms, while no frame has come for `PAGE_IDLE_AFTER_MS` (100): by the time the player is back on
the tree the texture is usually ready and the switch looks as it did. A texture left pending by frames
that stopped (another tab) keeps that timer going, and a canvas that gets its size (`updateCanvasSize`)
starts it. Not while the panel is hidden (`window._panelVisible === false`): the timer neither paints nor
comes back then - it would paint behind the game, 4 ms slices, twice when the corner drawing arrives -
and `onPanelShowing` starts it again. So the first open of a session can show the plain colour for the
~150-200 ms the page takes (the tree loads and is drawn once while hidden). The corner drawing's `onload`
now asks for a frame through the backing field (`__needsRender`) and starts that timer - no frame drawn
inside the image callback, the tree not marked changed.

**A new face's names made before they are drawn** (`LayerBuild._warmText`, 2026-09-28). After a design
switch the build's last frame, the names and chapter titles, took 15-22 ms on Chalkboard: not the drawing
but the first use of the new face - measuring the names (`measureText`, 4-5 ms, into
`CanvasRenderer._labelWidths`), shaping them, and above all the glyph outlines the halo's `strokeText`
needs (8 ms for Chalkboard's chalk face, 3 ms for Arcane's book face; made at any zoom, also below 0.8 where
the names are not outlined - see *Names without their outline zoomed out* - since a zoom-in after a design
change made there would otherwise put them all in one frame); drawn again at once the same names
cost 2-3 ms. `LayerBuild.noteRestyle` (a design, Design Effects, a late stylesheet or web font, a
language) now also marks the names' font cold (`_textCold`). A build then, after its pieces and before
the names, measures the names it will draw into the renderer's width cache (the font set on a small
canvas as `renderLabels` sets it, so the cache is kept) and draws each distinct letter once, outlined and
filled, at the names' and the titles' size and scale on that canvas - as many as fit in the frame's time
left, one at least; the names follow when they fit. The widths are ours and always carried over; the
glyphs are the engine's cache, which Chrome shares across canvases (the bench shows it) - not measured in
the game's engine. Even a quick tree's repaint after a design switch is now spread while the font is cold
(it was one repaint at once of 40-54 ms with a page design, 8-16 of it the names); a repaint at once or a
finished build clears the flag - unless it drew its names below `TreeStyle.LABEL_HALO_MIN_ZOOM` in a design
with an outline without warming first (`LayerBuild._outlinesMadeAt`, 2026-10-10): no outline was stroked, so
the flag stays and the next build makes the outlines (a repaint at once at the opening zoom 0.75 used to
clear it, and the first zoom-in past 0.8 then made them in one frame). An uncounted build that was slow puts `_lastMs` just past `SYNC_MAX_MS`,
as `noteSync` does for an uncounted repaint at once (the new design may be heavier); the time spent on the
letters is left out of that estimate (it is the font's, not the tree's). The small canvas is sized to two
letters at the build's scale, so no letter falls off it (one off a canvas may not be drawn, and so not
made). With many distinct letters (a CJK language) the warming spans more frames, the old picture up
meanwhile. Bench: the finishing
frame after a page-to-page switch 17-21 → 10-11 ms; a quick tree's repaint after a switch 40-54 → 10-13 ms
(its frames).

**Every name's letters made while the panel is still** (`LayerBuild.warmIdle`, 2026-10-10). The warm-up
above makes only the letters of the names its build draws, and at the opening zoom (0.75) that is a handful
(4 in the lab with a quarter of the tree known). The first zoom past 0.8 then drew ~150 names whose letters
the engine had never drawn: 27-41 ms in one frame in the offline Ultralight lab (Korean names, 1452 spells,
639 distinct letters at ~0.12 ms each outlined and filled), every later visit 3-6 ms. Now an animation frame
with nothing to draw (the render loop's `!shouldRender` frames, not while panning and not within
`INPUT_QUIET_MS` of a press, wheel or key) gives a slice to a warm-up job over the whole tree: work goes on
while `IDLE_WARM_MS` (2 ms) is not used up, one item at least (a do-while, so a slice is `IDLE_WARM_MS`
plus the item that crosses it; Ultralight's clock is 1 ms coarse, so 2-3 ms). The names' letters are gathered
`WARM_GATHER` spells a step, letter by code point (a letter past U+FFFF is one): first each spell's name as
it shows now (`CanvasRenderer._labelTextOf`, the rule `_labelCandidates` uses: a locked spell has none
outside edit mode, an available one under Reveal Name shows `???`), then every spell's own name - states and
reveals change in place after the job has run (the player's progress arriving after the tree, a spell
learned), and with the locked names left out the lab's job made 30 of the 639 letters and the first zoom
past 0.8 was back at 37.8 ms; then each letter drawn once on the small canvas
(outlined when the design has an outline, at any zoom), then the chapter titles'. The lab confirmed Ultralight's glyphs
are shared across canvases: letters drawn on that canvas make the tree layer's first names cheap. The job
starts again for a new font (`_glyphFontKey`: size, face, outline width, scale, title size, name length
cap; `noteRestyle` too, a late web font keeping its name) and for a new tree (only its letters not made
yet: `_glyphsDone`), and when a reply from C++ changes a name in place (`TreeParser.updateNodeFromCache`,
which every spell-info reply goes through, drops the job: the nodes array and its length stay the same, so
the job's own tree check cannot see it; the letters made stay made). Done, an idle frame only compares the
job's size, scale and tree with the renderer's - no key string is made - since the face, outline, title size
and name cap change only with a design, a late font or a language, all of which call `noteRestyle`. Until it is done the font counts as cold (`LayerBuild._cold()` = `_textCold` or
`_glyphsPending`): repaints are spread and a build still makes its own names' letters first, leaving out
the ones the job made. Off with spell names off (turned off mid-way, the job is dropped and the font not left
cold; turned on again, it starts again and draws only the letters not made yet). Measured (`burst`, 3 notches, median of 3 runs; the job
done ~0.6 s after the tree is drawn, its frames under 8 ms): the first zoom past 0.8 37.5 → 13.4 ms on
Arcane, 34.9 → 10.8 ms on Classic, no frame over 16.7 ms left in the run. Measuring every name's width too
gained nothing (13.0 against 12.7 ms), so the job makes letters only.

**Where the renderer's code lives** (2026-09-28). `CanvasRenderer` is one object in thirteen files, all
under 600 lines: `canvasRendererV2.js` holds its state and constants, start-up, canvas size, the render
loop, the public calls and the `_needsRender` accessor; each `canvasRenderer*.js` file after it is an IIFE
that copies a table of functions onto the object (as `treeStyleBook.js` does for `TreeStyle`), and
`index.html` loads them right after it. A frame and the tree layer (`render`, `_drawTree`,
`_renderTreeInto`) are in `canvasRendererFrame.js`, what moves over it in `canvasRendererMoving.js`, the
lines in `canvasRendererEdges.js`, the spell passes and batching in `canvasRendererNodes.js`, a spell drawn
by itself in `canvasRendererSpell.js`, names in `canvasRendererLabels.js`, `setData` and the indexes in
`canvasRendererData.js`; `modules/README.md` lists them all. The split changed no behaviour: the same 222
members with the same function bodies, and the tree layer and canvas pixel-identical in all five designs.

**Pieces look only at what is near them** (2026-09-28). Every strip of a drag (LayerScroll) and every
piece of a spread repaint (LayerBuild) ran the edge passes over all ~1,400 lines - two map lookups, an
object and a `'from->to'` string per line, three times over with a selection or learning path - and the
spell pass over all ~1,400 spells, culling each by the box inside the loop. Now `_cullIndex`
(`canvasRendererData.js`) keeps grids of spell and line indices (`CULL_CELL` 100 world units; a line in
every cell its ends' box covers), each line's two spells and its key, and the roots, made when first
needed after `setData` or `buildSpatialIndex` (which edit mode calls when it moves spells) or when the
node or edge list is another or another length. A box asks the grid for the indices in the cells it
covers (`_nodesInBox`, `_edgesInBox`, the box grown by the largest line's bow allowance), each once and
sorted, so they are drawn in the same order as before - order matters where antialiased strokes overlap
- and the loop keeps its own box test, so the set is the same too. A box over more cells than half the
number of items (a whole repaint of the view and its margin) looks at every item, as before; spells off
any finite place and lines too long for the grid (`CULL_EDGE_MAX_CELLS`) are looked at by every box. In
edit mode, which changes lines in place without telling the renderer, the index is made fresh for each
use without grids. `renderEdges` works out the lines drawn at all once (`_visibleEdges`) and every pass
walks that list, with no lookups or strings. Names: the text widths are kept in a `Map` per font,
`ctx.font` is read once per run of names (`_labelFontFrom`, not per name - reading it builds a string),
and a strip skips the candidates above or below it before looking up their width. `NodeBatch.addShape`
compares a shape's look with the last one of its layer before building the look's key string. Spell
sizes, the halo scale, the learnable ring and the name padding are named constants (`KNOWN_SIZE`,
`LEARNABLE_SIZE`, `LOCKED_SIZE`, `FOCUS_GROW`, `HALO_SCALE`, `RING_GAP`, `RING_ALPHA`, `RING_WIDTH`,
`LABEL_PAD`) shared by `renderNode`, `_renderNodeSimple`, `_batchPlainNode`, `LayerScroll._worldBox` and
`TreeStyle`. The pictures are pixel-identical to before (all five designs, every locked spell shown or
discovery mode, strips, pieces and whole repaints frame by frame). JavaScript cost with every canvas call
a no-op (`node --jitless`, the repro tree, Arcane, zoom 0.9, a quiet machine): a drag strip 3.0-3.1 →
0.7 ms, a 384 px piece 2.8 → 0.4 ms (the same with a spell selected), a whole repaint at once 12-14 →
10 ms. In the desktop bench (Chrome with its JIT, CPU canvases, pieces rasterised as drawn, every locked
spell shown, two rounds of five designs) a strip went 1.3-1.8 → 0.9-1.2 ms and a piece 1.0-1.6 →
0.6-1.0 ms (the edge pass in a strip 0.7-1.0 → 0.2-0.3 ms); frames over 16.7 ms in the drags, clicks and
wheel zooms stayed where they were (61 before, 64 after, of about 3,400 frames - most of them spikes of
0.4-0.9 s in both, from the machine and Modern Dark's starfield, not from the tree). The shift itself
(clear and draw instead of `'copy'`) is 2.6 → 2.4 ms. `modules/canvasCullTest.js` checks the grid
against the full loop on 300 boxes.

**What a spell's or line's place decides, kept with the index** (2026-10-10). Every repaint worked out
again, per spell, its shape's turn toward the centre (`atan2`, `cos`, `sin`) and its hand-drawn variant
(`hash01`, a `sin`), and per straight line its hand-drawn bow (another `hash01`; the known lines up to
three times, glow, line and engraving) - ~6,000 transcendental calls a whole repaint of 1,452 spells. The
culling index now keeps them in typed arrays, made on first use and gone with it: `_turnsOf(ix)` (cos and
sin of `NodeBatch.turnAt`, `rotationAt` without the level-of-detail switch, in `ix.turn`; the variant in
`ix.variant`), `_bowsOf(ix)` (`TreeStyle.edgeBowUnit`, the bow before the design's amount, in `ix.bow`;
`edgeBowFrom(unit)` makes the same number `edgeBow` does). `renderNodes` tells `NodeBatch.useTurn` the
spell it queues next, and `addShape` takes the kept values when the place and school are that spell's
(not in the simple level of detail, which does not turn shapes); pass 1's batches now hold line indices,
and passes 1-3 hand `_drawEdgePath` the kept bow. Edit mode's index has no grids, so it uses none of it
and works them out as before; the hover preview and the lock chains also work theirs out (few lines).
Lab (Ultralight): the tree layer identical to the byte (Arcane, Classic), a whole repaint 24.0 → 23.1-23.9 ms
on Arcane and 20.6 → 19.8-19.9 ms on Classic (10 repaints each); the frames of a wheel burst and a drag
the same within noise (the spread repaint fills frames to its target either way).

**Names from the culling index, and fewer of them at mid zoom** (2026-10-09). `_labelCandidates` looped
over every spell to find the ones on the canvas and its margin. It now asks `_nodesInBox` for the world
box holding that screen box (`_labelWorldBox`: rotation, zoom and pan undone, padded by `LABEL_CULL_PAD`
world units against rounding) and keeps its own screen test, so the candidates and their order are the
full loop's (`_gridPick` gives indices ascending, the old loop's order, which placement depends on).
The candidates are worked out once a frame (`LayerScroll._found`) and once per build (`LayerBuild._warmText`,
`renderLabels`; twice in a build after a design change, which runs both), not per strip or piece. A canvas-sized box zoomed out covers more cells than `CULL_MAX_CELL_SHARE`
allows (in the measurement below at zoom 0.9 and under) and looks at every spell as before, so the gain is at higher zoom: JavaScript
cost of one `_labelCandidates` (`node --jitless`, 1,400 spells over a 6,000-unit radius, 1600x1000, margin
150) 0.26 → 0.15 ms at zoom 1.3, unchanged at 0.6 and 0.9; with JIT 0.05 → 0.04 ms. The ~1.7 ms of a piece above is not this loop (about 0.1 ms
with JIT in node); what the rest of it is was not measured here. Between `LABEL_MIN_ZOOM` (0.5) and
`LABEL_FOCUS_ZOOM` (0.8), where only selected, hovered, learning and learnable names are drawn, at most
`MAX_LABELS_FOCUS` (50) are placed instead of `MAX_LABELS` (150), in the same order (the selected spell,
then in view, then priority, then nearest the middle of the screen), so the selected, hovered and learning
names stay. This range includes the view the tree opens at (`centerView`, zoom 0.75 at most since 1.5.0:
see *The home view fits the part of the tree that shows*); the threshold stays
0.8. Within one priority the names nearest the screen's middle come first (`_byLabelOrder` on the squared
screen distance each candidate carries, `dist`; 2026-10-10): ordered by node index, a spell coming in at the
screen's edge while panning or wheel-zooming could push a name in the middle past the cap. A drag's strips
(`LayerScroll._labels`) place from the same candidate order, so a drag's repaint picks the same set; each name left out saves a
`fillText` (no outline below 0.8, see *Names without their outline zoomed out*; not measured in game). The
selected spell sorts first even in the layer's margin and is not held to the cap there or in a dragged
strip (`LayerScroll._labels` places it past a full cap, and neither path counts it), so a drag brings it
in named - except, as before, a selected spell that gets no name at all: locked, nameless, filtered out
(the trait filter), in a hidden school, or shown as `???` (below the reveal threshold); a hovered spell
(priority 4, like the spells on the hovered path, which can be many) stays under the cap.
`modules/labelCullTest.js` checks the index's candidates against the full loop's on 140 views (zooms
0.5-2.2, turned, with margins), both caps, the selected spell in the margin with 120 learnable names
in view at zoom 0.6, and the same 50 names kept when a spell comes in at the screen's edge.

Also in that pass: learnable spells without an XP ring are batched in `NodeBatch` like locked and known
ones (they were drawn one by one, some nine paint calls each; a tree has hundreds), with their thin ring
as a plain circle (`NodeBatch.RING`, never hand-drawn). On a CPU canvas it saves little - half of a node
repaint is the outlines' rasterising, not the calls - but it is the same picture for less work.

**Animation frames at ~12 a second** (`ANIMATION_FRAME_MS` 83; 66 before 2026-09-26, 50 before that): each
such frame is an upload of the whole panel in the game's browser. The moving parts keep their speed:
`modules/animClock.js` counts the steps due since the last frame (tuned at one per 66 ms, `STEP_MS`, the
rest carried over, at most `MAX_STEPS` caught up) and the globe (`Globe3D.advance`), the stars' drift and
twinkle and the learning pulses take that many - so they move in slightly bigger steps, not slower. It
also stops them speeding up while a drag draws every frame. Measured in the bench: 14.9 → 11.5 frames a
second, the globe turning 0.1234 and 0.1236 a second.

**The game log gets the numbers** (developer mode): every 5 s `PerfMeter` writes one `[Perf]` line to the
console, which lands in SpellLearning.log - frames and rate, average and worst frame, worst stall, how
many times the tree layer was repainted, design, whether animations are stilled, level of detail, zoom;
since 2026-09-26 also the average turn of the loop with the share of it our drawing took, and whether a
spell is selected or its card previewed. The loop turn is the telling number: an idle browser turns it
about 60 times a second, so a turn of 100+ ms while our drawing takes 2 ms is the game browser's own work
(painting the page), and no click is looked at meanwhile. Every press also writes an `[Input]` line: how
it was read (click or drag, travel, lateness), what the click hit, a release that never became a click
event, and a card preview that took 20 ms or more to build (`DetailsPeek.SLOW_CARD_MS`).
**Fewer paint calls per frame** (2026-09-25). Measured in the desktop bench (1,315 spells, Arcane,
zoom 0.9); in game a whole tree in cheat mode was about 7,800 paint calls before these changes.

- **Spells drawn in batches** (`modules/nodeBatch.js`). `renderNodes` sends every plain locked spell,
  every undiscovered one (`?` included) and every plainly known one to `NodeBatch`: one path per look
  (fill, outline, width, alpha, dashed), filled and stroked once, halos first and the known-spell centres
  on a second layer. Spells with anything of their own - available and learning ones (rings), the
  selected and hovered spell - are still drawn one by one by
  `renderNode`, after the batch (the hover path was too, until 2026-10-04: see "Wheel over a spell"). The school shapes live in `NodeBatch.SHAPES`/`ROTATION`, and
  `_initShapePaths`, `renderNode` and `renderMysteryNode` use them too. A full repaint went from 3,220 to
  813 paint calls; the picture is pixel-identical away from the (animated) heart. Where two spells overlap
  the outline of one can now lie over the other's fill - the batch draws all fills of a look first.
  **Lock looks batched too** (2026-09-26). `loadTreeData` gives every spell with prerequisites a hard
  one (the first), so in the game's trees nearly every spell has the lock look - and all of those were
  still drawn one by one (1,423 of 1,428 spells). `_batchPlainNode` now batches them with the same look:
  a locked spell's grey shell and school-coloured hole, a known spell's grey ring under its body
  (`LOCK_SHELL_*`, `LOCK_RING_*`, shared with `renderNode`). `NodeBatch` has three layers - under the body
  (shell, ring), bodies, on the body (centre, hole) - and a `bare` look (the ring, which lines show through)
  is not underlaid with the backdrop. Every spell in the bench with a lock: 5,559 → 541 paint calls per
  full repaint; the picture matches except that halos now all lie under the spells (a neighbour's glow
  used to lie over a ring drawn before it), a few pixels round known spells.
- **Globe** (`globe3D.js`). The 200 dots go into 16 alpha steps, one path each (`ALPHA_LEVELS`); the 18
  frontmost glow through `TreeStyle`'s glow sprite instead of a new radial gradient each; each dot keeps
  the angles it was made from instead of working them out with `atan2` every frame; the colour is parsed
  only when it changes; the depth sort's comparator is made once. A travelling particle's 30-dot trail is
  drawn in fade steps (`TRAIL_ALPHA_STEP`), and path lengths are worked out once per path.
- **Stars** (`starfield.js`). Drawn in opacity steps (`OPACITY_STEP` 0.04), one path each; a world-space
  tile's stars are generated from its seed once and kept (`_tileCache`, keyed by seed, size and density)
  instead of re-rolled every frame. The renderer also no longer re-rolls the fixed-mode stars every frame
  when no seed or density was set (it compared with `undefined`).
- **Background and tree as one picture** (`modules/staticBase.js`). The background is still between two
  frames - a design page, no stars, stars held still, or moving stars between two of their moves (below) -
  so `render` leaves it to `_drawTree`, which asks `StaticBase` to paste background and tree layer as one
  kept picture: an animation frame is one full-canvas pass instead of three (colour, page or stars,
  layer). The picture is keyed by the layer (its draws, the hover preview, the pan offset) and the
  background (`Starfield.moves` for the stars); a key is built only when it has held for two frames, so a
  drag or glide is drawn straight as before. A stretched paste always draws straight, and so do moving
  stars (a picture would last a frame or two; in the lab building it raised Modern Dark's worst frame on
  opening) - between their moves the tree canvas is left alone anyway (`_mainShownKey`).
  `TreeStyle._pageBuilds` tells it the page was rebuilt.
- **No colour under an opaque page** (2026-10-10). `_drawBackground` fills the whole buffer with the
  background colour and then, with a page design, draws the page over all of it; when the page hides every
  pixel (`TreeStyle.pageCovers`: `pageColor` is `#rgb`/`#rrggbb`, the texture starts with a fill of it, and
  the css size times the frame's scale is exactly the buffer's size - the LOD-lowered scale, as `render`
  passes it) the fill is left out. The same pixels (lab: 0 bytes differ on Arcane over a half-red buffer);
  one full-canvas fill fewer per straight-drawn frame (a drag or glide, a StaticBase rebuild). The lab puts a
  full fill of the 846x581 tree canvas at ~0.04 ms, so no frame time to see.
- **The stars move at their own pace** (2026-10-08). `CanvasRenderer._syncStarfield` (each frame, before
  the key) applies the star settings and moves the stars (`Starfield.advance`: drift, twinkle) only when
  `STAR_FRAME_MS` (160) has passed - every second animation frame; drawing them no longer moves them. A
  frame for the heart alone in between leaves the tree canvas as it is. Before, every animation frame of a
  starfield design (Classic, Modern Dark, a design without a page) repainted the whole tree canvas - in
  the lab 24% of the panel 12 times a second. The speed stays (`AnimClock`, 2-3 steps a move instead of
  1-2), and a move changes a star's opacity by at most 0.036 (brightest, fastest world-space star), under
  the 0.04 opacity step it is drawn in (`modules/starCadenceTest.js`). In game, where frames come on the
  loop's ~35 ms turns (an 83 ms throttle lands on ~105 ms), a move can be 4 steps, 0.048 - as the idle
  frames (250 ms) already moved them.
- **A changed picture costs one repaint** (2026-10-08). `_mainShownKey` was set only when `StaticBase`
  pasted, so after each change (a tree repaint, a hover preview, a star move) the frame that drew it
  straight was followed by a second whole repaint, the picture's paste. Drawn straight, the canvas shows
  that key's picture as well, so it counts as shown.
- **Zoom read-out** (`CanvasRenderer.showZoom`): written only when the number shown changes; the camera
  called it every frame of a glide.

**Moving parts on their own canvases** (`modules/fxLayer.js`, 2026-09-26). PrismaUI 1.5.1 draws the
whole panel on the CPU into one bitmap (Ultralight `BitmapSurface`) and copies it to a game texture each
time something changed; the accelerated view is not in its public API. So an animation frame costs what
it changes on the page, not what our drawing costs: in game the loop turned every ~110 ms for 2 ms of our
drawing, and clicks waited behind it. The heart (with its runes and globe), each spell's sigil or learning
glow, and the travelling particles are now drawn on small canvases laid over the tree canvas
(`FxLayer.draw(key, box, dpr, drawFn)`; drawn in the tree canvas's own coordinates, reused by key, hidden
when not drawn in a frame; where the tree canvas sits, `offsetLeft`/`offsetTop`, is read once in
`FxLayer.begin`, 2026-10-10 - read per spot after the last spot's style writes, each read laid the page out
again). The tree canvas is not touched on a frame whose picture it already shows
(`CanvasRenderer._mainShownKey`, the `StaticBase` key), so an idle frame changes a few small boxes instead
of most of the panel. In the bench the tree canvas gets 0 paint calls per idle frame and the picture is
the same as before. `USE_FX_LAYER = false` draws everything on the tree canvas as before; a spot that
cannot be made falls back to the tree canvas for that frame. Spots are kept by key (the heart, the
particles, one per selected or learning spell); past `MAX_SPOTS` (16) a new key takes over a spot not
drawn this frame. The heart keeps beating off screen (`_heartBeat`: phase, beat effects, the next frame),
since the sigil, glow and particles move in the frames it asks for. `hide()` hides the spots and `show()`
puts them back over the canvas (`FxLayer.reattach`). Also:

- **Input first**: no animation frame while a mouse button is held or within `INPUT_QUIET_MS` (150) of a
  press, wheel or key, so a click gets the browser at once. Hover does not pause the animations.
- **Idle**: after `IDLE_AFTER_MS` (15 s) without input no particle leaves the heart, and animation frames
  (with them the star moves) come every `IDLE_FRAME_MS` (250) at most.
- **Only the tree tab draws**: `switchTab` stops the render loop on the settings and scan tabs (it went on
  animating behind them) and starts it on the way back; `startRenderLoop` refuses while another tab is in
  front.

Together: under Arcane a full repaint went from 3,220 paint calls to 634 and an idle frame (heart, globe,
sigil) from 268 paint calls and three full-canvas passes to 55 and one pass; under Modern Dark with moving
stars a full repaint went from 2,727 to 494 and an idle frame from 361 to 64. Not done: drawing the simple tier while the view is moving, trimming the 500-unit cull margin, and
- separately, because it needs a plugin build and may simply not work - trying the accelerated view.

### Stutter while playing and using the panel (2026-09-26)

Found by reading the code for work that holds the game's browser (or the game thread) up, with the
panel closed and in use:

- **Availability recalculation was quadratic.** `recalculateNodeAvailability` (`cppCallbacks.js`) asked
  `findDuplicateSiblings` for every prerequisite of every spell, and that filtered all ~1,400 nodes each
  time - on every panel open, Learn, unlock and mastery. A canonical-id index (`duplicateGroup`, rebuilt
  when the node list changes, also by count for edit mode) makes it linear: 325 ms → 2 ms on the game's
  tree in a desktop browser, same result. `ProgressUpdates.nodesFor` now also notices nodes added or
  removed.
- **A state change repaints the canvas tree** (2026-10-17): `ProgressUpdates.repaintStates` (WheelRenderer's
  states, `SmartRenderer.refresh` - the canvas tree's discovery set, learning paths and buckets - and
  `_needsRender`) after a mastery, a relock, the Unlock answer (`onSpellUnlocked`) and the cheat Unlock and
  Relock buttons; the cheat Unlock also recounts availability at once, so the spell's children open. The
  Unlock paths told only WheelRenderer, which holds no nodes while the canvas tree is in use, and the
  `updateSpellState` C++ sends next skips its own refresh because the state is already 'unlocked'.
- **Info logging stays in the page** (`modules/logGate.js`). Every `console.log`/`console.info` crossed to
  SpellLearning.dll, which dropped it unless developer mode was on. `LogGate` drops it in the page instead;
  warnings and errors still go through. The heaviest log strings are not even built outside developer
  mode: the whole progress payload on opening (`onProgressData`), every progress key on each selection
  and the per-selection `LogMessage` call (`treeViewerUI.js`), and `_logToSKSE` on tree load.
- **Closed panel:** a spell state pushed from C++ while playing (`updateSpellState`: learned early,
  studying, mastered, target set or cleared) and `onSpellUnlocked` only update the state; the redraws,
  learning paths, counts and card wait for `onPanelShowing`, which refreshes all of them. A save loaded
  with the panel closed no longer fetches progress and known spells (opening does it again anyway), the
  PreReqMaster preview, TreeGrowth and TreePreview loops do not start while the panel is hidden, the build
  progress bar is not written, and `body.panel-hidden` pauses endless CSS animations (`patch-ui.css`).
  Also kept to the state while hidden (2026-10-08, a report of lower FPS with the panel closed): the
  tree reset on every save load (`onResetTreeStates`) and a learning target set by a tome
  (`onLearningTargetSet`) change the states but skip `WheelRenderer.render` / `updateNodeStates`; no
  separate "owed" flag - opening fetches progress and known spells, and `OpenRefreshGate` repaints on
  the replies only if the states differ from what the tree showed when it closed (the progress reply
  always calls `updateNodeStates`). The fallback tree (`TrustedRenderer`, only without `CanvasRenderer`)
  stops its frame loop on `onPanelHiding` and while `window._panelVisible === false`, and `onPanelShowing`
  starts it again when its canvas is the one shown. The view is created hidden, so `onPrismaReady` adds
  `body.panel-hidden` too (until the first `onPanelShowing`).
- **Opening the panel** refreshed the renderers itself and again when the known-spells reply came
  (`onPlayerKnownSpells`); now only the reply does. And only if something changed (`modules/openRefreshGate.js`,
  2026-09-26): closing the panel notes what the tree shows (every spell's state, and the XP ring of the ones
  being learned or learnable); opening with progress and known spells on their way repaints nothing at
  once, and each reply (`onProgressData`, `onPlayerKnownSpells`) repaints only if the note no longer
  matches - what changed while closed shows up that way. No replies within `WAIT_MS` (2 s): repainted
  anyway. It was up to three full repaints per opening, each a slow frame in game.
- **A click** selected the spell and started the focus glide on the same frame, and the tree was repainted
  at the old view and again when the glide stopped. A tree change during a glide or wheel zoom now waits
  for the motion to stop (`_drawTree`: the layer is pasted stretched, as for the view itself) - one repaint
  per click instead of two. The selection's highlight shows as the glide stops (450 ms); a click on a
  spell already in place has no glide at all (`TreeCamera.animateTo` finishes at once when the view is
  within `STILL_EPSILON`), so it shows at once.
- **Measured in game after the above** (2026-09/10): the slowest frame after a zoom went from 97-144 ms to
  12-19 ms and our drawing is 2-3% of the time, but the loop still turned only every 34-37 ms with the
  animations stilled (43-60 ms with them). The PerfMeter box now rewrites once a second, not four times
  (each rewrite is a repaint and an upload of the whole panel). A temporary experiment (PerfExperiment,
  removed once it had answered) switched the page between looks in developer mode, one [Perf] line each:
  - as shipped vs no box/text shadows, filters or background images anywhere (`flat`): 157 vs 66 ms
    worst stall, 6 vs 12 frames a second (Candlelit Tome). An idle loop turn booked with a timer instead
    of an animation frame changed little, nor did plain tools over the canvas alone;
  - what lies under the opaque tree canvas is never seen: while it shows the container drops its
    background image and blurred inner shadow (`.tree-container.canvas-shown`) - loop turns 58 -> 25 ms;
  - wide blurred shadows (the panel's 36-60 px, 16-40 px on cards, tools, tooltip, how-to panel, modals,
    popups, 18 px inner glows) became crisp rings - still 165 ms stalls;
  - the last run split `flat` (Classic vs Arcane): Classic 80 ms stalls and 15 frames a second whatever
    was turned off - the base theme has nothing costly; Arcane 223 ms as shipped, 130 without shadows,
    168 without background images, 69 without both.
  So the designs now use no box shadows and no gradients on their large surfaces: the panel, its header,
  the cards, tools, tooltip, how-to panel and modals are one colour (the gradient's middle); rings are
  `outline`s (the gold tooling line inside the panel is an outline with a negative `outline-offset`; the
  frame round the tree an outline while the canvas shows). Modern keeps only its border (an outline would
  not follow its rounded corners). Night Grimoire and Chalkboard set `--book-cover`, the leather's
  one colour; a design without it gets `--book-cover-top`. Small effects stay (a button's hover glow, the
  Spell Tree tab's text glow, slider fills).
- **What is left** (2026-10-08, measured in the offline lab `D:\DEV\prismaui-lab`: Ultralight 1.4.1 on the
  CPU as in PrismaUI, 1920x1080, the saved 1,381-spell tree, a learning target per school; median of 3
  runs per design, before and after taken in turns. The machine was shared with other work, so worst
  frames carry a few ms of noise and the odd 100+ ms outlier, dropped by the median; repainted area and
  CPU time are the steadier figures):

  | Design | Idle: panel repainted per s | Idle CPU ms/s | Open: worst frame | Zoom over a spell: p95 / worst | Hover: worst |
  |---|---|---|---|---|---|
  | Classic | 287% -> **154%** | 96-138 -> **57-70** | 24-25 -> 27 ms | 11-12 / 17-27 -> 12 / 19 ms | 14-17 -> 16 ms |
  | Modern Dark | 288% -> **155%** | 128-167 -> **115-122** | 30-37 -> 36 ms | 18-25 / 30-45 -> 16-33 / 22-106 ms | 24-26 -> 23 ms |
  | Arcane | 22% (same) | 26-52 | 32 ms | 10-13 / 14-19 ms | 14-17 ms |
  | Night Grimoire | 22% (same) | 63-89 | 30-34 ms | 12-18 / 20-29 ms | 17-31 ms |
  | Chalkboard | 22% (same) | 63-68 | 38-52 ms | 11-12 / 19-22 ms | 14-15 ms |

  (Before: this branch at `cb17d4f`; after: the star pace and the one-repaint change above. In the book
  designs only the second applies, after a tree change, which these phases do not measure apart: their
  rows give the range of both runs.)
  - **Idle with animations** (C2): every design asks for ~12 frames a second (`ANIMATION_FRAME_MS`). In
    the book designs each repaints only the heart's spot (1.8% of the panel); in the starfield designs
    each repainted the whole tree canvas (24%), now every second one does. Not changed, since it would
    show: the heart, its globe and runes move all the time (the globe turns, the runes rotate), so fewer
    heart frames or a shorter `IDLE_AFTER_MS` / `IDLE_STOP_MS` would make them visibly choppier or stop
    them sooner. That is the player's call ("Still everything", or a proposal for the user).
  - **Idle with everything stilled** (C2): the lab repaints nothing at all (0 dirty frames; 0.5-0.8 ms per
    host frame, the loop's JavaScript). The 34-37 ms loop turns seen in game with animations stilled are
    therefore not the page painting: it is PrismaUI's or the game's own time, which the panel cannot
    shorten. Two findings for whoever looks next, neither verified: the render loop keeps Ultralight's
    `needs_paint` true on every frame (an animation frame is always booked) although nothing is dirty -
    it would matter only if PrismaUI copied on `needs_paint`; `PrismaUI.dll` imports
    `ultralight::IntRect::IsEmpty`, which suggests it checks the dirty bounds instead (and the in-game
    experiment above - an idle turn booked with a timer - changed little).
  - **Arcane's worst stall** (C3): 223 ms in game was measured before the large surfaces lost their box
    shadows and gradients. In the lab Arcane is now in line with the others (open 32 ms, zoom 14-19 ms,
    hover 14-17 ms; Classic 24-27 / 17-27 / 14-17). Still to confirm with `[Perf]` in game.
  - **A proposal, not applied** (it would change the look): Modern Dark's tree frame clips the canvas to
    rounded corners (`border-radius` with `overflow: hidden`). Square corners saved ~1 ms per repainted
    frame in the lab (idle repaint 5.1 -> 4.1 ms, open worst 32 -> 27 ms), within the noise.
- **Glows where they barely show:** a known spell's halo is left out when its radius on screen is under
  `HALO_MIN_SCREEN_PX` (20; the learning spell's glow stays), and a design's glow under known lines below
  `EDGE_GLOW_MIN_ZOOM` (0.8): zoomed out, Arcane drew hundreds of halo sprites and wide strokes per repaint
  (spells 25-45 ms, lines 15-30 ms of it).
- **Names without their outline zoomed out** (2026-10-09): a design's `labelHalo` (the outline round each
  name, a `strokeText` before the `fillText`, as costly as it in the CPU-drawn view) is left out below
  `TreeStyle.LABEL_HALO_MIN_ZOOM` (0.8) - which includes the view the tree opens at (zoom 0.75); the threshold
stays 0.8. `TreeStyle.beginLabels(ctx, fontSize, zoom)` decides it once per run
  of names and returns it; `drawLabel` follows it; the layer's names (`renderLabels`) and the names in a
  dragged strip (`LayerScroll._labels`, the layer's zoom) pass their zoom. The warm-up
  (`LayerBuild._warmText`) passes none: it makes the names' outlined letters at any zoom when the design has
  an outline (it is spread over frames, so leaving them out below 0.8 saved nothing, and the first zoom-in
  past 0.8 then made the new font's outlines in one frame). Chapter titles
  keep their outline at every zoom (a handful, and the only text left below 0.5). Per design: Classic and
  Modern Dark have no outline; Arcane (dark ink on parchment), Night Grimoire (cream on indigo) and
  Chalkboard (chalk white on slate) all use a page-coloured outline, which knocks out the lines and spells
  under a name rather than lifting it off the page - their pages are plain colours with strong contrast to
  the names, and between 0.5 and 0.8 only the priority names are drawn and known lines lose their glow, so
  all three drop it alike (no per-design token). Readability there is not checked in the game yet.
- **Repaint parts in the log** (developer mode): each `[Perf]` line ends with the longest time each part of
  a tree repaint took in those 5 s (`PerfMeter.part`, from `_renderTreeInto`: dividers, edges, nodes,
  bridges, labels, chapters, and `raster` - a one-pixel `getImageData` that makes the browser rasterize the
  calls there, in case it defers them). It showed where repaints of 59-109 ms went: drawing the spells
  zoomed out, most of all in Arcane (glows), not the JavaScript that prepares them (4.7 ms for the whole
  tree without a JIT).
- **The trait filter's veil** fades the tree, not the page: drawn into the see-through tree layer, it takes
  `VEIL_ALPHA` of what the tree drew out of the layer (`destination-out`), so the page, its light and its
  texture show as they are. A black veil turned a dark design's page (Candlelit Tome) all but black, and
  one in the page's colour still flattened it. Drawn straight onto the tree canvas (no layer), it is the
  colour behind the tree (`CanvasRenderer._backdrop`).
- **Idle:** after `IDLE_AFTER_MS` (15 s) without input animation frames come every 250 ms, and after
  `IDLE_STOP_MS` (45 s) not at all, whatever the background - the next mouse move or key brings them back.
  Every frame, however small, is a repaint and a texture upload of the whole panel.
- **The PreReqMaster preview loop** stops after 60 frames with nothing to draw (`_markPreview` restarts it);
  it asked for a frame every frame the panel was open, on any tab.
- **Switching tabs** resized the tree canvas to 800x600 when its tab was hidden and back on return,
  clearing it both times (a full tree repaint); `updateCanvasSize` now leaves it alone when the container
  is 0x0 or the size has not changed.
- **Mouse moves** read `getBoundingClientRect` each time (a forced layout after the tooltip writes);
  `CanvasRenderer._canvasRect` keeps it until the pointer re-enters or the canvas resizes.
- **Settings sliders** (font size, reveal thresholds, learning colour) apply on release (`change`), not on
  every step of the drag (a page-wide relayout, a tree refresh and a card rebuild per step).
- **Retry-school list** (every 2 s) is only rebuilt when the list changed, and only while the Settings tab is
  in front. **A leaking 300 ms poll** in Oracle settings (a new one per visit to Oracle mode) is gone (and
  the Oracle mode itself since 2026-09-27).

**Measured and not done (2026-10-10, offline Ultralight lab, D:/DEV/prismaui-lab/results/hom-lens-2026-10-10.md):**
- **A lens** (full detail - names, outlines, glow - only in a circle round the cursor, a cheap base layer
  elsewhere, as in foveated renderers). The worst zoom-step frames (9-10 ms) are the stretched paste of the
  layer while the wheel turns and the piece build after it settles, and neither draws a name; names are in
  the last frame of a rebuild only (3-6 ms of 7-9). With every name turned off the settled worst went 9.7 to
  9.2-9.7 ms (noise); glow is under 0.1 ms a frame, the outline under 1 ms. At zoom 1.141 a 15%-area lens
  holds 15 of the 82 names on screen, and a lens that follows the cursor would repaint its inside on every
  mouse move, which the baked layer never does today. The one real stall - the first zoom past 0.8 - was
  the letters' first rasterisation, fixed by the idle warm-up above, not by where the names are.
- **Batching the selected-path (pass 2) and learning-path (pass 3) lines** as pass 1 is batched. With a
  school root selected (332 path lines) a whole repaint went 21.6 to 21.4 ms (Arcane) and 19.3 to 19.4
  (Classic) - nothing - while 2.7% (Arcane) and 2.0% (Classic) of the pixels changed where the lines meet
  (a seam stroked once instead of twice). Reverted. The lab inject that selects a root is
  `targets/hom/lens/_lab_select_root.js`.

### Chalkboard design (2026-09-27)

Replaced Candlelit Tome. A lesson on the board: `presets/design/Chalkboard.json` reuses
`design-darkbook.css` with a slate-and-wood `--book-*` palette (panels slate, frames wood, accents pale
yellow chalk), the tree page slate `#26302c` with chalk-dust grain, school colours mixed 35% toward chalk
white (`schoolInk`) so they read as pastel chalk, and three chalk drawings made with anidoodle's chalk
hand (`themes/chalkboard/`: a magic circle in the hub, a corner doodle, the empty screen's book and
star). Its whole UI - panel text, buttons, headings and the tree's labels - is set in chalk handwriting
fonts chosen per language (below).

### Drawn spells and lines (2026-09-27)

The three book designs also draw the tree's spells and lines in their medium (`modules/treeStyleInk.js`,
the Drawn lines tokens in PRESETS.md):

| Design | Spells | Lines |
|---|---|---|
| Arcane (pen and ink) | hand-drawn: shapes wobble (`handDrawn` 0.07) | locked lines stippled (`lockedEdgeStipple` 3), straight lines bow a little |
| Night Grimoire (engraving) | an engraved inset outline inside known spells (`innerLine` 0.7, page colour) | known lines engraved as a double line (`unlockedEdgeWidth` 2.4 with a 0.4 cut of page) |
| Chalkboard (chalk) | hand-drawn, looser (`handDrawn` 0.1) | known lines skip like chalk (`edgeBreaks`), straight lines bow |

The wobble is geometry only: every school's shape (the circle too, as a smooth loop of nine points) is
jittered into four variants once per design, and a spell picks one by its position, so the batch
(`NodeBatch`) and the spells drawn one by one (`renderNode`, the hover) give it the same shape. None of
it is drawn below the full level of detail, and the cut and the inset line wait until they are wide
enough on screen. Edit mode draws none of it (a dragged spell would change its hand-drawn shape every
frame). Measured on a CPU canvas (full tree, every locked spell shown, 1600x1000): the drawn lines add
1-5 ms to a repaint of the tree layer. Stippling the locked lines with a dash instead of a pattern had
cost 25-40 ms, so it is a pattern - about 6 ms still, so it starts at zoom 1, where the dots are far
enough apart to read as dots (below, a plain faint line looks the same). The pattern's dots sit like
queens on a 5x5 (or 7x7) board, one in every row, column and diagonal: an earlier 3x3 tile had an empty
row, and a line lying along it vanished. The Drawn lines chip turns all of it off.

### Design fonts per language (2026-09-27)

Handwriting and old-book fonts cover one or two scripts each, so a design that wants its own lettering
declares a CSS variable per language instead of one family: `@font-face` rules for each font file, then
`:root { --x: ... }` for the Latin/Cyrillic default and `html:lang(ko|ja|zh-cn|zh-tw) { --x: ... }`
overrides (the panel sets `<html lang>` when a language loads, see TRANSLATING.md). The design's
`body, button, input, select, textarea` use `var(--x)`, and its tree token `labelFont: "var(--x)"` makes
the canvas labels resolve the same list (`TreeStyle.labelFamily`, read once per design and language).
Each family list ends in the platform's own font for that script, so a missing glyph falls back instead
of showing a box. A browser fetches a face only when some text uses it, so a player downloads just their
language's file. Fonts are OFL (Hakgyoansim Bunpil by the KERIS download page's statement, see its
licence file), subset to the characters the UI and common spell names need
(KS X 1001 Hangul, Joyo kanji, GB2312 level 1, Big5 level 1, plus every character in the lang files);
each folder has a README with sources and licences.

Who uses what (Classic and Modern Dark keep the theme's own sans; Skald uses the system's Bahnschrift, see "Skald" below):

| Design | Variable | Latin/Cyrillic | ko | ja | zh-cn | zh-tw |
|---|---|---|---|---|---|---|
| Chalkboard | `--chalk-font` (`--chalk-title` for headings) | Pangolin | Gaegu (titles: Hakgyoansim Bunpil) | Klee One | Xiaolai | Iansui |
| Arcane | `--arcane-body`, `--arcane-serif` (titles) | Literata; titles EB Garamond | Hahmlet | BIZ UDPMincho | Noto Serif SC | Noto Serif TC |
| Night Grimoire | `--ng-body`, `--ng-title` (via `--book-serif`) | Literata; titles Cinzel (Cyrillic: Alegreya SC) | Hahmlet | BIZ UDPMincho | Noto Serif SC | Noto Serif TC |

The two book designs share one folder, `themes/fonts/book/` (Arcane's stylesheet declares the faces with
paths relative to `themes/`, Night Grimoire's inline css with paths relative to the page). Literata
comes first in every language's list, so Latin letters, digits and punctuation inside CJK text stay in
the book face; CJK titles use the language's body face.

### Arcane illustrations (2026-09-27)

The Arcane design carries three drawings made with anidoodle (code-drawn, pen-and-ink stipple in the
page's sepia `#4a3012`, transparent PNGs in `themes/arcane/`): a magic circle in the tree's hub seal in
place of the "HEART" text (`hubEmblem`) - rings, a band of runes, a pentagram joining the five schools
with a small circle at each point, a ringed core and a still drift of motes inside, a corner ornament - compass rose, ruled border, vines -
mirrored into the four corners of the parchment page (`pageOrnament`), and the "no tree yet" screen's
open grimoire with a star rising out of it (`.empty-icon` in `design-arcane.css`, which also turns that
screen into the page: flat parchment, sepia words, a leather button). Other designs leave both tokens
empty and are unchanged.

What each costs in game, by design:
- Corners: drawn into the page texture (`TreeStyle.stepPage`, painted over a few frames - see
  *The design page painted over frames* above), which is already pasted once per frame; nothing per frame.
- Heart: shrunk once to its on-screen size in device pixels (halving step by step, so the dots stay a
  clean tone) into a sprite, rebuilt only when that size changes by 8 px; a frame pastes it 1:1.
- Images load asynchronously; until one arrives the old drawing (text, bare page) shows, then a frame is
  asked for (the emblem shows on it) and the page is painted again with the corners over the next frames,
  the page without them up meanwhile.

Night Grimoire carries the same three drawings as single-line (Mellan) engravings in its gold `#d9b35e`
on the indigo page (`themes/nightgrimoire/`, set in `presets/design/NightGrimoire.json`: the two tree
tokens and, in the preset's inline `css`, the empty screen - kept there because `design-darkbook.css` is
shared with Chalkboard). On a dark page the gold is light, so the corner's vines carry more ink on
their lit flank.

The drawings' sources (tone fields and plates) are in `lab/anidoodle-arcane/`, with how to re-render them.

### Skald (2026-10-17)

Skald is the look of the mod's explainer pictures made into a design: Skyrim's own menus - smoky black,
thin pale lines (`#e8e4d8` text, `#cfc9b8` rules), woven knotwork corners, wide-set capital
titles, and the school colours mixed 40% toward the line colour (`schoolInk` 0.4) so they read as quiet
tints. Like Night Grimoire it is only a preset file (`presets/design/Skald.json`) over
`design-darkbook.css` with its own `--book-*` palette. Its inline `css` also turns the gold the sheet and
the theme use for the chosen school tab, toggle and Canvas badge into the pale line colour, sets the
titles in capitals and draws the empty screen. Two transparent PNGs in `themes/skald/`: the page corner
(`pageOrnament`, 200 px: a trefoil knot in the corner, a two-strand plait out along each edge, then a
hairline fading out to a small ring) and the empty screen (the five school marks in a plate with a trefoil
at each corner, between two plaited rules). The knots are woven over-under: their sources
(`lab/skald-art/`, `knots.js`) find each crossing and redraw
the over strand with a page-coloured edge. No tree effect past
the page and its corners, chapter titles and ornamented dividers (no glow sprites, no drawn lines), so it
costs about what Night Grimoire costs without its engraving. Icons: the neutral line set
(`themes/icons-line/`). Font: Windows' own Bahnschrift (`--skald-font`: SemiCondensed, then the theme's
narrow faces), the face closest to the game's menus that Windows 10 and 11 carry; no font file is
shipped, so a system without it falls back to the Skyrim Edge theme's narrow faces, and CJK text to the
system's sans. Whether the game's Ultralight finds the system Bahnschrift is not yet seen in game.

### Settings page layout and control size (2026-09-27)

The settings page used to pair blocks side by side whatever their height: Progression (about 1,800px)
next to Early Spell Learning (about 700px) left a column of empty space, Early Spell Learning's power table
was cut off in its half-width column, and Spell Tome Learning sat next to Developer & Debug. Now the short
blocks share the first row - Hotkey and Tree View on the left, UI Display and Developer & Debug on the
right, near equal in height - and Progression, Early Spell Learning and Spell Tome Learning follow at full
width under the presets row that governs them.

Buttons, switches and chips are drawn about a fifth larger (`patch-ui.css`, "Bigger controls"): header
buttons 34px, switches 52x26 (small ones 42x22), button and chip text a step up. Sizes only, so every
design keeps its colours, fonts and corners; none of the designs sets these sizes itself.

**The canvas hint above the footer (1.5.0).** "Drag to pan / Scroll to zoom ..." sat 14 px from the
tree page's bottom, under the footer (drawn over the page's bottom 44 px), so it lay on the legend
with the side panel, and in bar mode with the bar closed. `#contentSpellTree .tree-instructions` is now
52 px up (`patch-ui.css`, every design), inside the canvas; with the bar open it is still hidden.

**Readable dim text (1.5.0).** The presets row's label (*Presets*), its Save button and its chips (here
and on the Scan page) were the Skyrim gold written out at 45% and 70% (2.2-2.5:1 and 3.2-4.6:1 against
the five designs' backgrounds, measured in the desktop harness); the tree footer's legend was
`--text-muted` (3.8-5.2:1) and its Locked swatch a half-transparent dark square with a 15% white edge.
Now (`styles-skyrim.css`, the shared rules, so every design's own variables apply): the label in
`--text-secondary` (6.4-8.0:1), Save in `--accent-gold` (6.8-9.8:1), the legend in `--text-secondary`
(7.4-9.0:1) and the Locked swatch opaque with a `--text-muted` edge (dashed, from `patch-ui.css`). The
chips take the same colours - not picked `--text-secondary`, picked `--accent-gold` with an edge in it,
so picked and not picked stay apart (the harness has no presets, so the chips were not measured).

### Scrolling (2026-09-26)

The game's browser scrolls a short way per wheel notch, so the settings page and long lists took many
turns. `modules/wheelScroll.js` takes every wheel event nothing else took (the tree and the previews zoom
with the wheel and call `preventDefault`) and scrolls the nearest box under the cursor that can still move
that way `WheelScroll.SPEED` (3) times as far; form fields that use the wheel keep it. Scrollbars are twice
as thick as before: 12px in the designs' stylesheets and on the settings page, 8px on the side panels
under Classic.

### Language picker (2026-09-22)

*Settings > UI Display > Language* lists every language in `lang/languages.js` - generated by
`lang/build-preloads.js` from each translation's own `_meta.language`, so a new `.json` shows up without a
code change - plus the pack default from `locale.js` if its maker did not list it. Code: `i18n.js`
(`switchLocale`, `getLanguages`) and `modules/languageSetting.js`.

The language has to be known before anything draws, and the saved settings only arrive from C++ later, so
four places are consulted in order: the browser's `localStorage` (`hom_language`, read in `index.html`),
then `lang/user_locale.js`, then `locale.js`, then `settings.language` when the config arrives - which
switches if it differs and refreshes `localStorage` for next time. `lang/user_locale.js` is written by the
plugin (`WritePanelLocale`, `uimanager/UIManagerLocale.cpp`) every time the config is loaded or saved and
the language changed: one line, `window._i18nUserLocale = '<code>';`, the code checked to be lower-case
letters, digits and `-`. It does not exist until a language other than the default is picked (a missing
script just fails to load), under Mod Organizer it lands in overwrite so a mod update does not reset it, and
it is in `.gitignore`. A live switch re-labels everything marked `data-i18n`, then
`refreshScriptTexts` (`languageSetting.js`, 2026-09-29) builds again the script-built texts of the Settings
and scan pages: the Design row's description, the Easy page's preset chips (the whole preset section is hidden when there are no presets, 2026-09-30), the tree
builder status line, the scan status bar, the tome learning description, the power step rows and the how-to
lists - and since 1.5.0 the tree's school tabs (`TreeNav.relabelSchoolTabs`: `tree.schoolShort.*`, the
full name in the tooltip, a modded school with no key keeps its own name), the Scan page's school list
(`updatePrimedCount`, `chips.school.*` through `schoolText`) and the Scan page's tree previews, whose
school names are drawn on their canvases, and the tree is asked to repaint so its chapter titles
(on the tree layer) follow. The renderer chip in the tool bar ("Canvas") is written with
`setI18n` (`tree.rendererCanvas` / `tree.rendererSvg`), so the re-label covers it. Korean text breaks
between words only (`html:lang(ko) { word-break: keep-all; overflow-wrap: break-word }` in
`patch-ui.css`, under every design): the Easy page's status had wrapped "노드" into "노 / 드". Japanese and
Chinese put no spaces between words and keep normal breaking. Tests: `languageRefreshTest.js`. The two status lines are written with their language key and values (`TreeGrowth.setStatusText(text,
tone, key, params)`, `updateScanStatus(message, type, key, params)`, and `window.updateStatus(message, key,
params)`, which C++ also calls with a message alone) and come back in the new language
(`TreeGrowth.relabelStatus`, `relabelScanStatus`); every message the panel builds with `t()` passes its key.
A line written without a key - a C++ message, an add-on module's - comes back as written, and the scan bar's own `data-i18n` goes once a script writes it, so a
switch does not put "Ready to scan" back over "Tree built". The rest - the build progress window, the spell
card until the next pick, the hotkey field's "press a key" - stays until the next start, and the picker
says so. **Known now
(2026-09-23): PrismaUI does not keep `localStorage` between game sessions.** Seen in game with
`settings.language = 'en'` saved and `locale.js` saying `ko`: every start drew in Korean from `locale.js`,
then the late switch turned everything marked `data-i18n` English while text scripts had built (power
step rows, the spell card) stayed Korean - one screen, two languages. That is what `lang/user_locale.js`
is for: the start now draws in the picked language. The late switch stays as a fallback (first start
after the update, before the plugin has written the file). **Written before the panel opens (2026-09-26):** once the
view has loaded the file the game holds it, and replacing it failed ("Access is denied") on every settings
save - a language picked in game never reached it (seen: the file still said `en` with Korean chosen), and
the log got an error per save. `UIManager::Initialize` now calls `WritePanelLocaleFromSavedConfig` (the
saved `config.json`'s language) just before it makes the view, and a save whose write fails does not try
the same content again that session.

### Click and drag on the tree (2026-09-22)

A press on the canvas used to grab the tree at once, so the pixel or two a hand shakes during a click
slid the tree - and the spell under the cursor - along with it. Now a press moves nothing until it has
travelled `DRAG_THRESHOLD` (5px); from there the drag starts, without a jump. Two guards against the tree
staying stuck to the cursor: `mouseup` is also heard on `window` (a release over the details bar or
outside the canvas), and a `mousemove` that reports no button down ends a drag whose release never
arrived - only when the browser was seen reporting buttons at the press, so an engine that always says
0 cannot break dragging.

The game's browser may be such an engine, so more guards that need no button state
(`CanvasRenderer.releasePointer`, 2026-09-23): the `window` `mouseup` listener is in the capture phase (an
element that stops the event cannot keep it), and the drag is let go when the panel hides
(`onPanelHiding` - the release then happens in the game), when the page loses focus, and on Escape. The
first press of a session logs `buttons`/`which` and whether the lost-release check is on, so the game
log tells which case it is.

**Escape (2026-09-30).** With a spell selected on the tree the first press drops the selection, the next
closes the panel (`handleEscapePress`, script.js). In game the view does not always get the key, so the plugin
watches the game's input too: Escape with the panel open calls `onNativeEscape` (Main.cpp `InputHandler`,
`UIManager::OnEscapePressed`). One press can arrive both ways: the plugin's copy waits 80 ms and does nothing when the view's own keydown came meanwhile (only the view knows which field or dialog the press was for); the second arrival within 250 ms is ignored.
The Find Spell dropdown or an open dialog takes the press whichever arrival comes first (Find Spell hides; other dialogs close through their own keys), so the panel never closes behind one; a dialog field that uses the press itself marks it too.

## New spells and next goals (2026-10-02)

`modules/newSpells.js` (`NewSpells`) adds two things to the tree page's footer, both meant to show the
player what changed and where to go without costing the tree a repaint.

**New spells.** When the panel closes (`onPanelHiding`) the unlocked spells are noted. When it opens
(`onPanelShowing`, then the `GetProgress` and `GetPlayerKnownSpells` replies, compared for
`OPEN_WINDOW_MS` = 3 s after opening) every spell unlocked now that was not then is new - mastered
through XP or learned from a tome or vendor alike. Each gets a gold ring that spreads once
(`BURST_MS`, started `BURST_DELAY_MS` after the last reply so it does not share the frame of the
reply's tree repaint) and then a small gold mark (`--accent-gold`) at the spell's top right. The mark
stays until the spell's card is shown (`renderSpellCard` -> `NewSpells.markSeen`: a click, the hover
preview, or the footer chip). The footer chip `#footer-new` ("New 2") goes to the oldest one. A spell
the player unlocks with the panel open is not new, and edit-mode copies (`originalFormId`) never are.
Only from one opening to the next: the first opening after a save loads, or after a new tree
(`NewSpells.reset` in `treeLoad.js`), has nothing to compare with. The ring is skipped (the mark stays)
when the render popup's "still everything" switch is on (`DesignEffectsSetting._master`).

Cost: the ring and the marks are FxLayer spots (keys `newring:<n>`, `newmark:<n>`), drawn from
`CanvasRenderer._drawMoving`; the tree layer and the main canvas are not drawn for them. Measured in the
dev harness on a 3546-spell tree: during a ring 0 tree layer draws and 0 main canvas repaints, and a mark
going away is one spot hidden. At most `MAX_MARKS` = 4 marks are drawn (FxLayer has 16 spots; hub,
particles, hover and the learning glows use some), the chip counts all. A still mark is kept by its spot
`version` and not drawn again. The ring's frames are animation frames (`_requestAnimationOnlyFrame`),
throttled like the heart's.

**Next goals.** `#footer-goals` lists up to `GOAL_COUNT` = 3 spells: the learning targets first, then
learnable spells with XP, most first (`NewSpells.goals`; spells of a hidden school and at 100% left out).
Each chip is the name and the progress ring's percent; the name hides as the card's does (`???` under
`revealName` unless learning or cheat mode, through `spellDisplayName`). The footer stays one row: on a narrow panel the
chips shrink and long names end in an ellipsis (`.footer-center` clips). Pressing one does what a click on
the spell does (`CanvasRenderer.selectNodeAndFocus`). The chips are rebuilt only when the key (count, ids,
names, percents) changes; `updateUnlockedCount`, `ProgressUpdates.handle` and the progress reply ask.
The labels are `footer.newSpells` and `footer.nextGoals`; spell names are the game's.

## Hover preview (2026-09-23)

The details panel used to open only on a click. Now resting the cursor on a spell for `DWELL_MS`
(150 ms) shows its card, and the card goes back to the selected spell `GRACE_MS` (250 ms) after the
cursor leaves - unless it left for the panel itself, where the card's links can still be pressed.
`modules/detailsPeek.js` owns the timers; `settings.detailsOnHover` (*Settings > UI Display > Preview
on Hover*, on by default) turns it off, and it is always off in edit mode.

Selecting and previewing had to be pulled apart first: `showSpellDetails` did three things at once -
reveal the spell's locks (`PreReqMaster.revealLocksForNode`, permanent), make it the Learn/Unlock target
(`state.selectedNode`) and draw the card. A preview must do only the last, so `treeViewerUI.js` now has
`selectSpell(node)` (the consequences), `renderSpellCard(node, {preview})` (the drawing) and
`showSpellDetails` = both. While previewing the panel carries `.peeking`: Learn/Unlock are `disabled`
(they act on `state.selectedNode`, so a focused button must not answer Enter either), the cheat XP
inputs cannot be clicked, and the footer says to click. No preview starts while an input on the card
has focus, or a typed XP value would be redrawn and bound to another spell. Edit mode switching on and
the panel hiding (`onPanelHiding`) both drop a preview. The camera moves only on a click, and a preview
does not fire `nodeSelected`, so the school tabs do not follow the cursor either. A refresh of the
selected spell that arrives mid-preview (`onPanelShowing`, XP callbacks) keeps the previewed card.

With the preview on the panel never closes: with nothing selected it shows a hint (`.is-empty`). The
reason is the bottom bar. It is drawn over the tree, and the canvas gets no `mousemove` once the bar
covers the cursor - so a bar that opened on hover would appear over the spell the cursor is on and
leave it unclickable, with the preview stuck. Keeping the panel's place means nothing under the cursor
ever changes, and `TreeCamera.getPanelOffset` frames spells around it as before. Leaving the canvas
(`mouseleave`) now also clears the hover, which used to leave the tooltip standing.

### Design presets and the spellbook look (2026-09-23)

*Settings > UI Display > Design* picks the look of the whole panel - one choice since 2026-09-23; the
separate *UI Theme* selector is gone. A design sets the UI theme it is built on (`uiTheme`, the panel's
main stylesheet, Skyrim Edge when left out), how the tree is drawn (`tree`, a set of tokens) and extra
CSS over it (`cssFile` or inline `css`). Built in (`modules/designPresets.js`): **Classic** (Skyrim Edge
UI, tree as before), **Modern Dark** and **Arcane** - all three on Skyrim Edge, the last two with a
stylesheet over it. Every other UI theme in `themes/manifest.json` becomes a design of its own
(`DesignPresets.onThemesLoaded`), so old UI theme add-ons still show up. A saved config from before the
merge is carried over (`DesignPresets.savedChoice`: `uiTheme: default` becomes Modern Dark).

**Arcane's panel** (`themes/design-arcane.css`, over Skyrim Edge) is a book: a dark leather cover with a
gold tooled line round the panel, leather header, settings and tools (zoom bar, school tabs, trait
filter, footer), gold switches, thin leather scrollbars, serif titles, and parchment pages - the tree
(drawn by the canvas), the spell card, the hover card, the *How to learn* page (section headings in red
rubric ink) and every popup (a parchment slip over the darkened book). The pages re-point the theme's
text, divider and accent variables inside their own box, so whatever reads a variable turns to ink; the
few colours the theme writes out (card header and footer, list rows, fields and lists in popups, row
highlights, progress track, state stamps) are overridden there. The scan screen's tree previews get a
dark leaf (`--preview-bg`, below).

**Modern Dark** (`themes/design-modern.css`, over Skyrim Edge, 2026-09-23): slate-blue glass panel, amber
accent, bright accent colours, rounded corners, pill switches, Segoe UI. It used to be a UI theme of its
own (`styles.css`, `themes/default.json`), but that stylesheet had fallen some 200 classes behind Skyrim
Edge - the header buttons, popups, scan screen and build progress had no styles under it at all - so it
became a layer of colours and shapes like Arcane, and every screen added to Skyrim Edge is covered. The
`default` UI theme and its two files are removed.

**The render popup** (the gear in the zoom bar, `modules/renderSettings.js`, 2026-09-23; one page since
2026-09-26) holds only what a player reaches for - a master switch and eleven chips, no tabs:

- **Still everything** (`animationsOff`): the heart pulse, particle trail, stars and globe stop
  (`Starfield.still` / `Globe3D.still`: drawn where they are, no frames asked for them) and the design's
  moving effects go off, while every chip keeps its own value for when the master goes off again
  (`RenderSettings.stilled`, `DesignEffectsSetting.applyMaster`); the *Moving parts* chips are greyed out
  meanwhile.
- *Moving parts* - heart pulse, particle trail, star twinkle (`starTwinkle`), and the design's moving
  effects (`modules/designEffectsSetting.js`): the selection sigil, the learning
  glow and the heart runes.
- *On the tree* - the design's page, its drawn lines (`ink`: the hand-drawn wobble, stippled, broken
  or engraved lines, one chip for all of them), the starfield (greyed out with a note while a page is drawn, since
  the page replaces it), base lines and spell names.

A design effect turned off stays off in every design - `TreeStyle.setEffectsOff` blanks those tokens
after any design's are set - and a chip the current design has no use for is greyed out. Saved as
`designEffects` (`{ page, sigil, glow, runes, ink }`, false = off; an old `reveal` key is ignored). A chip is a checkbox inside a
`label.render-chip` (hidden box, lit text when checked, theme variables only, `patch-ui.css`), so the
wiring is the same as for a switch. A note at the bottom says what costs frames since the tree layer and
`StaticBase` (see *Performance* above): only what moves costs every frame - Still everything, or stars
without twinkle, keep the tree light - while names and lines cost only when the tree is redrawn. The page
is no longer a performance item (it is pasted with the tree as one picture), so it sits with the design's
effects.

The camera and selection behaviour (centre and zoom on click, focus zoom, wheel rotation, dim others,
selection path), curved edges and the spell name size moved out of the popup to their own block,
**Settings > Tree View**, under Hotkey. Same setting keys and element ids as before, except the two
sliders (`tree-focus-zoom`, `tree-node-font-size`); `RenderSettings.syncControls` still puts their saved
values back.

Everything else the old popup had - heart and globe colours, sizes, particle counts and dot sizes,
pulse speed and delay, globe text, star colour, density, size, seed and background, the school colours -
is gone from it. A design decides those in its `render` block (see [PRESETS.md](PRESETS.md#design-presets));
`DesignPresets.renderValue(key)` gives the design's value where it sets one, else the player's saved
setting, and `applyHeartSettingsToRenderer` / `applyGlobeSettings` read through it. A saved value from
the old popup counted once: with no control left to change it, a colour picked years ago would beat
every design for good, so a config from before (no `renderSettingsVersion`, or an older one) has those
values put back to the shipped ones on its first load (`RenderSettings.afterLoad`), and Reset to Defaults
does the same. Version 3 (2026-09-28) added `starfieldFixed` (stars fixed to the screen): its control went
with the old popup, so a saved `true` could not be turned off. A
wrongly typed value in a `render` block ("60" for 60) is converted or skipped (`DesignPresets._typed`).
The controls are put back from the config once it has loaded (`RenderSettings.syncControls`). The popup
is leather in Arcane and the cover colour in the dark-book designs.

**Scan screen previews:** the tree previews there (`treePreview.js`, `treeGrowth.js`, `prereqMaster.js`)
fill their canvas with `getPreviewBackground()` (`uiHelpers.js`): the design's `--preview-bg` CSS
variable, else the old near-black. They repaint every frame, so the value is read at most once a second.

**Tree builder status line** (`#tgStatus`, mirrored to the Easy page): idle, working, done and error
each have a token of their own - `--status-idle`, `--status-working`, `--status-done`, `--status-error`
(`TreeGrowth.STATUS_COLORS`, 2026-09-28). They used to be `--accent-gold/green/red`, and in Classic the
working gold was the idle text's own colour and the muted green and red hard to tell apart. Defaults in
`styles-skyrim.css` (amber, green, red; idle the old faint khaki), set again by `design-modern.css`,
`design-arcane.css` (on the leather) and `design-darkbook.css` (Night Grimoire; Chalkboard's preset sets
chalk tones for working, done and error; its idle follows its own `--book-text-faint`, which
`design-darkbook.css`'s `--status-idle` reads); the hex in `STATUS_COLORS` is the fallback for a design that sets none. The idle colour
comes from the stylesheet (`#tgStatus, #easyStatus` in `styles-skyrim.css`), not an inline style: the
language strings (`scanner.complexStatusWrap`, `easyMode.statusWrap`) rebuild both elements at start-up.
Until 2026-09-28 `scanner.complexStatusWrap` carried the old khaki inline, and `easyMode.js`, which copies
`#tgStatus`'s inline colour onto `#easyStatus`, passed it on, so `--status-idle` never showed in game (the
desktop harness, which has no language strings on that line, did not show it). `#easyStatus` needs the
rule too: the copied inline colour is empty while idle. Because those strings rebuild both spans, the Easy
copy is made by `TreeGrowth.setStatusText` calling `_syncEasyStatus` (2026-09-29), not by an observer on
`#tgStatus`: after a language switch the observer watched the old, detached span and the Easy page kept
"Waiting for scan" under a built tree. The switch itself resets both lines; `TreeGrowth.relabelStatus`
writes the last line again from its key ("Tree built", "Tree applied (N positioned)", "Build failed: ..."
in the new language), and a cleared tree (`setTreeBuilt(false)`) sets both lines back to the idle
"Waiting for scan..." from the language strings (`TreeGrowth.resetStatus`). The status-line code is
`modules/treeGrowthStatus.js`, loaded after `treeGrowth.js`, whose methods it adds to `TreeGrowth`;
`modules/statusLineTest.js` (run by `run-tests.js`) covers it.

**School tabs** sit in a row under the zoom tools (`patch-ui.css`, all designs). Centred on the top row
they covered the right half of the tools - the renderer badge, heart settings and edit tree - at the
default panel size. The trait filter pill moved down one row with them. Each tab shows a short label
(`tree.schoolShort.*` in the language files: `Destr.`, `파괴`; a school without one, such as a
modded school, is cut to five letters and a dot) with tight padding, about half the old width; the
tooltip keeps the full school name (`chips.school.*`).

**Fixed with the merge:** `themes/*.json` give `cssFile` relative to `themes/` (`../styles-skyrim.css`),
but the link lives in `index.html`, so the path pointed outside the panel, never loaded, and switching to
the modern theme left the old stylesheet in place - the modern theme had not actually been applied for a
while. `themeCssPath` in `settingsPanel.js` resolves it now.

Designs in detail:

- **Arcane** (default) - an open spellbook. A still parchment page instead of the starfield, school
  colours mixed toward brown ink (`schoolInk`, so the player's own colours still decide the hue), learned
  spells filled and edged in dark ink, learnable ones with a ring, locked ones a pencil sketch, serif
  names with a paper-coloured outline, the heart as a parchment seal, chapter titles (school names past
  each school's outer edge, the same size at every zoom), dividers as double rules, the selection path,
  sigil and heart runes in red rubric ink, and on opening the tree soaks out from the heart like ink.
- **Classic** - the tree exactly as before presets. `TreeStyle.DEFAULTS` is this look, so a preset only
  lists what it changes.

Add-ons add presets by dropping a `.json` into `SKSE/Plugins/SpellLearning/presets/design/` - the
plugin's `LoadPresets` already lists any preset folder, so no plugin change and no shared list two
add-ons could overwrite. Format and the full token list: [PRESETS.md](PRESETS.md#design-presets).
Three looks ship with the mod that way, always installed and picked in the list like the built-ins, and
double as examples for modders: **Night Grimoire** (indigo vellum, gold-leaf ink), **Chalkboard**
(slate in a wooden frame, the tree in pastel chalk; it replaced Candlelit Tome on 2026-09-27) and **Skald**
(Skyrim's own menus, see "Skald" above), `SKSE/Plugins/SpellLearning/presets/design/*.json`. They are a few KB
each, so there is no installer choice for them. Their panel is one shared sheet,
`themes/design-darkbook.css` (a tooled cover, serif gold lettering, dark pages where Arcane has
parchment, gold switches and tools), coloured by the `--book-*` palette each preset sets in its inline
`css`. `DesignPresets._applyCss` keeps the inline CSS after the stylesheet, so a palette always wins over
the sheet's fallback values.

Code: `modules/treeStyle.js` (`TreeStyle`: tokens, halos, labels, sigil, heart runes, school ink),
`modules/treeStyleBook.js` (adds the page, chapters and ornament dividers to `TreeStyle`), `modules/treeStyleInk.js` (the drawn lines),
`modules/designPresets.js` (`DesignPresets`: built-ins, disk presets, the selector, CSS). The renderer
reads colours from `TreeStyle.tokens` where it used literals before.

What it costs, with the tree layer in mind (see *Performance* above):

- **Edges are batched.** Pass 1 of `renderEdges` groups edges by look and strokes each group as one
  path - a dozen strokes instead of one per edge. It paid for the rest: on the 1,315-spell desktop test
  tree a full redraw is 3.4 ms under both presets (Classic 1,122 paint calls, Arcane 1,037).
- **The page replaces the starfield.** It is painted once into a texture per canvas size and look (over
  a few frames, `TreeStyle.stepPage`) and pasted, and a finished page asks for no frames; the starfield
  asked for 20 a second. Idle frames went from 409 paint calls to
  213 (what is left is the globe).
- **Moving parts sit on top of the layer.** The selection sigil and the learning glow
  (`TreeStyle.renderOverlay`) are painted after the layer is pasted, a handful of calls, and only move
  when something else already asks for frames. The ink reveal on opening (the layer pasted through a
  growing clip for 600 ms at full rate) was removed on 2026-09-26: in the game every open paid for those
  frames, in the CPU-drawn view, right when the panel was busiest. A design's `reveal`, `revealMs` and
  `inkColor` are ignored.
- Label outlines cost one `strokeText` per label (at most 150), at zoom 0.8 and above only
  (`TreeStyle.LABEL_HALO_MIN_ZOOM`); below it, the opening view (0.75) included, the names (at most 50) have
  none.

Not measured in the game yet; `perfMeter.js` is how to. The first draft of Arcane - glowing sprites
behind every learned spell and a wide glow stroke under every learned edge - doubled node and edge
time on the desktop, which is why the book look uses ink and outlines instead of glow.

### Visual Effects
- **Starfield** (`starfield.js`) — animated star background, parallax with pan (replaced by the page when a design preset sets one)
- **3D Globe** (`globe3D.js`) — rotating globe at tree center
- **Chain links** — drawn between nodes with hard prerequisites (locked spells)
- **Node states** — locked (gray fill + hole), learning (blue glow), unlocked (full color), weakened (partial opacity)

---

## Page 2: Spell Scan (`contentSpellScan`)

Where players scan spells and build their tree. Has two modes: **Easy** and **Complex**.

**Modules:** `easyMode.js`, `treeGrowth.js`, `treeGrowthStatus.js`, `buttonHandlers.js`, `scannerPresets.js`, `buildProgress.js`, `prereqMaster.js`

### Shared Components (both modes)

```
┌──────────────────────────────────────────────┐
│       [EASY]  [COMPLEX]  ← mode toggle       │
├──────────────────────────────────────────────┤
│ Status: Ready to scan                        │
│ [Scan Spells] [Export Scan]                  │
├──────────────────────────────────────────────┤
│ Total: 247 │ Mods: 12 │ Primed: 0           │
│ [Blacklist] [Whitelist]                      │
├──────────────────────────────────────────────┤
│                                              │
│         Mode-specific content below          │
│                                              │
└──────────────────────────────────────────────┘
```

### Easy Mode (`scannerEasyContent`)

Simplified one-click workflow.

```
┌─────────────────────┬────────────────────────┐
│  Preset Chips       │                        │
│  [DEFAULT] [Easy]   │   PRM Preview Canvas   │
│  [Hard] [Custom]    │   (shows tree + locks  │
│                     │    after build)         │
│  [Build Tree]       │                        │
│  [Apply Tree]       │                        │
│  [Clear Tree]       │                        │
│                     │                        │
│  Status: ...        │                        │
└─────────────────────┴────────────────────────┘
```

The buttons and the preview take what is left of the page under the scan results
(`#scannerEasyContent` and `.easy-panel-split` grow, 320 px at least; a shorter page scrolls; the
preview box fills `.easy-panel-right` as a flex item as well as by `height: 100%`, so it does not depend on
the browser treating a stretched flex item's height as definite). A fixed
500 px used to run past the page's bottom, so the preview's circle sat low and its lower schools were cut
off (1.5.0).

**The preview canvases** (the Easy/Pre Req Master preview in `prereqMaster.js`, `TreeGrowth`,
`TreePreview`) take their size with `TreePreviewUtils.fitCanvas`: a box that is not laid out (the Scan
page or the mode hidden) leaves the canvas as it is - it used to take a made-up 300 x 400 and keep it
until a ResizeObserver fired, which the game's browser may not have - and every redraw measures the box
again (a size set only on a change; setting it clears the canvas). Opening the Scan tab and a window
resize ask for a redraw. The Sun grid's school names are upright, in the panel's language (`schoolText`),
past their roots and outward arrows, hanging off the side they point to
(`TreePreviewUtils.schoolLabelPlace`); they used to be turned along the spoke and centred over the
root. The Sun mode records how far roots, arrows and names reach (`_lastRenderData.extent`, in
`TreePreview.getOutput`), and the three previews fit that into the box with `FIT_MARGIN` round it
(`contentExtent`, `fitScale`; the root ring plus 45 px when a mode gives no extent). Tests:
`previewFitTest.js`.

### Complex Mode (`scannerComplexContent`)

Full control over tree generation.

```
┌──────────────────────────────────────────────┐
│  Tree Building: [Classic] [Tree] [Sun] [Flat]│
│  ─────────────────────────────────────────── │
│  Tree Growth Settings                        │
│  (mode-specific: root count, trunk, branches)│
│  ─────────────────────────────────────────── │
│  Preview: [Sun preview] or [Flat preview]    │
│  ─────────────────────────────────────────── │
│                                              │
│  ▼ EXTRA SETTINGS (collapsible, post-scan)   │
│  ┌──────────────────────────────────────┐    │
│  │[Pre Req Master][Core][Alt Pathways]  │    │
│  ├────────────────────┬─────────────────┤    │
│  │  Tab Settings      │  Shared Preview │    │
│  │  (scrollable)      │  Canvas         │    │
│  └────────────────────┴─────────────────┘    │
│                                              │
│  [Build Tree] [Apply Tree] [Clear]           │
└──────────────────────────────────────────────┘
```

**Builder options** (2026-10-04, `modules/classic/treeBuilderSetting.js`): the Classic Growth Settings, under
the slider grid, have **Type grouping** (0 to 2, steps of 0.1, default 1) - how strongly spells of one kind
(bound weapons, runes) and one element (fire, frost, shock) stay on one branch; 0 builds as 1.2.0 did. A
one-line hint sits under it. It is a drag input like the sliders above it (`TreePreviewUtils.settingHTML`),
saved with the other settings in UnifiedConfig (`treeTypeGrouping`), reset by Reset to Defaults, and sent
with the next build (`ClassicBuildRequest`, `type_grouping`); a tree already built does not change. See
TREE_BUILDING_SYSTEM.md, *Type grouping*. Under it, a checkbox **Tiers by Magicka cost** (off by default,
`treeTiersByCost`, `tiers_by_cost`) with its own hint line: the tree's five tiers go by Magicka cost rank instead
of Novice to Master; the spells' levels (XP, spell card) stay. Both hints use the same 9px muted line as the
Spell Matching description (classes `tree-preview-option` and `tree-preview-hint`, the design's
`--text-secondary` and `--text-muted`). See TREE_BUILDING_SYSTEM.md, *Tiers by Magicka cost*.

### Extra Settings Tabs

Only visible after a spell scan.

#### Pre Req Master (`prmTabLocks`)
Adds hard prerequisite "locks" to spells using NLP similarity scoring.
- Master enable toggle
- Global lock % slider
- Per-tier lock % (Novice → Master)
- School distribution mode
- Pool source (Same School / Any / Nearby)
- Tier constraints (Same, Previous, Higher tiers allowed)
- Chain locks toggle
- [Apply Locks] [Clear]

#### Core (`prmTabCore`)
Globe position and radius controls. Radius writes to `spell_tree.json` and is used by the canvas renderer for the central globe size.
- H Offset, V Offset, Radius sliders

#### Alternate Pathways (`prmTabAltPaths`)
A "Not Yet Implemented" placeholder. Its greyed-out Bidirectional Soft Prerequisites toggle (nothing
applied the flag) and the empty stats line were removed on 2026-09-28, with `settings.treeGeneration`.

---

## Page 3: Settings (`contentSettings`)

All mod configuration. Split-row layout for space efficiency.

**Modules:** `settingsPanel.js`, `settingsPresets.js`

### Layout

```
┌──────────────────────┬───────────────────────┐
│  Hotkey Settings     │  UI Display           │
│  Panel key: F8       │  Theme, Colors, Font  │
│  Pause game on open  │                       │
├──────────────────────┴───────────────────────┤
│  [DEFAULT] [Easy] [Hard] [Save Preset]       │
├──────────────────────┬───────────────────────┤
│  Progression         │  Early Spell Learning │
│  Learning mode       │  Enable toggle        │
│  XP multipliers      │  Unlock threshold     │
│  XP caps per source  │  Self-cast required at│
│  XP per tier         │  Power steps          │
│  Reveal thresholds   │  Binary effect thresh │
├──────────────────────┬───────────────────────┤
│  Spell Tome Learning │  Developer & Debug    │
│  Progression toggle  │  Dev mode toggle      │
│  XP grant on read    │  Cheat mode toggle    │
│  Inventory boost     │  Debug options        │
│  Require prereqs     │  (hidden by default)  │
├──────────────────────┴───────────────────────┤
│         [Save Settings]  [Reset Defaults]    │
└──────────────────────────────────────────────┘
```

### Key Settings Groups

**Hotkey** (`modules/panelOpenSetting.js`, 2026-10-08): how the panel opens. The toggle key has Change, Reset
(F8) and **None** - `hotkeyCode` 0, `hotkey` "" (the field shows the None label as its placeholder, so a language
switch relabels it); None can always be picked, and its button stays pressed (disabled) while it is the setting.
Choosing a letter key (A-Z) shows a warning in the theme's red (`--accent-red`) under the row - typing that letter
in a search box reaches the game too and closes the panel - but is allowed. The warning is a `.setting-desc
.hotkey-note` line with its own `.hidden` rule in `patch-ui.css`. Reset to Defaults sets F8; a settings preset
never holds or changes the hotkey (a personal setting, like the language).
The same group has **Build Spell Tree at Start** (`modules/autoBuildSetting.js`, 1.5.0): `autoBuild` in
config.json, `whenMissing` (default, "When none") or `off`. With no saved tree the game start scans and builds one
in the closed panel; a tree built under another load order only gets a notice (docs/ARCHITECTURE.md, "Tree Built
at Game Start"). Reset to Defaults sets `whenMissing`; a settings preset never holds it.

**Progression:** How XP is earned and what thresholds unlock spells.
**Early Learning:** Grants nerfed spells before full mastery. Also holds **XP Alerts** (default on,
`notifications.enabled` - the key kept the first name, Show Notifications - `modules/notificationSetting.js`):
off hides the corner messages about XP and power (`HudNotify::IsXPAlert`: casting, power steps, studying, book
XP) wherever they come from - C++ (`HudNotify`), the Papyrus `SpellLearning.NotificationText` callers (ISL patch,
BookXP script) and the BookXP DLL. Learned, mastered, blocked tomes and the Spell Research line always show.
**Weakened Spell Alerts** and their interval sit under it as a sub-option, greyed out (`disabled-controls`)
while it is off. Reset to Defaults turns both on; a settings preset carries the alert options but never XP
Alerts. The corner messages' text follows the panel language (`hud.*` keys, docs/TRANSLATING.md), or
English with **Notification Language** set so (`notifications.language`: `panel` default, `en`; greyed out
with XP Alerts off; a preset never changes it): the game draws the corner with its own font, so a
panel language in a script that font lacks (Japanese on a Korean game font) showed boxes.
**Spell Tomes:** How reading spell tomes interacts with progression. With Spell Research loaded, its first line
says what Heart of Magic last added to Spell Research's lists (`modules/spellResearchStatus.js`,
docs/librarian/SPELL_RESEARCH.md section 3); hidden without it. **Skill XP from Studying**
(`spellTomeLearning.studySkillXP`, default on, 1.2.1): with the Immersive Spell Learning patch, whether a
study session raises the school's skill (`Game.AdvanceSkill` in `DEST_ISL_PlayerSpellLearningScript`, asked
through `SpellLearning_ISL.SkipStudySkillXP()`, true only when the setting is off, so a missing native - a
patch script with an older SpellLearning_ISL.pex - reads None = false and raises the skill as ISL does). Off
leaves skills to a skill mod such as Static Skill Leveling.
Reset to Defaults turns it on; a settings preset never changes it (like XP Alerts).
**Developer:** Import validation (Aggressive Path Validation), debug options (hidden by default).

---

## Modals

| Modal | Trigger | Module |
|-------|---------|--------|
| Build Progress | Build Tree button | `buildProgress.js` |
| Preset Name | Save Preset button | `uiHelpers.js` |
| Blacklist | Blacklist button | `buttonHandlers.js` (its lower lists are `gateReview.js`: "Spells to look over", spells the spell gate marked "review", with the reasons, Leave out / Keep; "Left out by itself", the "out" ones, Include) |
| Whitelist | Whitelist button | `buttonHandlers.js` |
| Import Tree | Import button (tree page) | `treeViewerUI.js` |
| Spawn Spell | Edit mode | `editMode.js` |
| Color Picker | Color swatches | `colorPicker.js` |

### Build Progress Modal

Shows staged progress during tree building:

```
┌─────────────────────────────────────┐
│  Building Spell Tree                │
│                                     │
│  ● Analyzing & Building Spell Tree  │
│  ○ Generating Prerequisites         │
│  ○ Finalizing Layout                │
│                                     │
│  ████████░░░░░░░░░  45%             │
│  Building tree structure...         │
│                                     │
│              [Done]  (hidden until  │
│                       complete)     │
└─────────────────────────────────────┘
```

Stages: Tree (C++ NLP build) → Prereqs (PRM if enabled) → Finalize

The bar does not measure anything: each stage starts at a fixed mark (10, 45, 80%) and creeps toward
the next one (40, 75, 95%) until the code that owns the next step calls `setStage`. A signal that
never comes therefore looks like a freeze just below the next mark - **39%** in the tree stage, 74% in
prereqs. Since 2026-09-23 `modules/buildProgress.js` writes every start, stage change, completion,
failure and early close to the log (`[BuildProgress] ...`, with seconds since start), and a stage that
has waited 20 s is logged once, so such a freeze can be traced from `SpellLearning.log`.

Fixed at the same time: `complete()` closes the modal 2.5 s later, and a build started inside those
2.5 s had its modal closed by the old timer - after which every stage change was ignored. `start()` now
cancels that timer.

---

## Main User Workflows

### 1. First-Time Setup

```
Open panel (F8)
  → Spell Tree page (empty state)
  → Navigate to Spell Scan
  → Click "Scan Spells"
  → C++ scans game → returns spell JSON
  → UI shows scan stats, enables Build
```

### 2. Build Tree (Easy Mode)

```
Scan complete
  → Select preset chip (DEFAULT/Easy/Hard)
  → Click "Build Tree"
  → Build Progress modal opens
  → Stage 1: C++ builds tree (TF-IDF themes → layout → edges)
  → Stage 2: PRM scores locks (if enabled)
  → Stage 3: Finalize
  → Modal completes → tree visible in preview
  → Click "Apply Tree"
  → C++ receives prerequisites → gameplay begins
```

### 3. Build Tree (Complex Mode)

```
Scan complete
  → Switch to Complex mode
  → Choose root mode (Sun/Flat); the growth mode is Classic (the only one since 2026-09-27)
  → Adjust settings (root count, branching, shapes)
  → Preview updates in real-time
  → Expand Extra Settings → tune PRM / Core
  → Click "Build Tree"
  → Same build pipeline as Easy
  → Click "Apply Tree"
```

### 4. Gameplay Loop

```
Tree applied
  → Navigate to Spell Tree page
  → See full tree with locked/available nodes
  → Click a node → Details sidebar shows prerequisites + XP
  → Cast spells in-game:
      Direct prereq cast → high XP (up to 50%)
      Same-school cast → medium XP (up to 15%)
      Any spell cast → low XP (up to 5%)
      Read spell tome → configurable XP%
  → At 25%: Early learning grants nerfed spell
  → At 100%: Mastery — full power spell
  → Discovery mode hides info until XP thresholds met
```

---

## JS Module Map

### Core Infrastructure
| Module | Purpose |
|--------|---------|
| `state.js` | Global state store (300+ properties) |
| `constants.js` | Default prompts, palettes, tier mappings |
| `config.js` | Tree config constants |
| `i18n.js` | Translation engine |
| `main.js` | Entry point, tab switching, module verification |
| `cppCallbacks.js` | All C++→JS callback handlers |
| `uiHelpers.js` | UI utilities, modal helpers, preset UI |

### Tree Rendering
| Module | Purpose |
|--------|---------|
| `canvasRendererV2.js` | **Primary** Canvas 2D renderer (`CanvasRenderer`): state, constants, start-up, canvas size, render loop, public calls. Its methods are in the files below, each adding a table of functions to the one object, loaded right after it |
| `canvasRendererColors.js`, `canvasRendererInput.js`, `canvasRendererSelect.js`, `canvasRendererData.js` | Colours; mouse, wheel, hit testing, hover; selection and wheel turns; `setData`, spatial index, level of detail |
| `canvasRendererFrame.js`, `canvasRendererMoving.js` | A frame and the tree layer (`render`, `_drawTree`, `_renderTreeInto`); what moves over it (heart, particles, sigil spots) |
| `canvasRendererDividers.js`, `canvasRendererEdges.js`, `canvasRendererNodes.js`, `canvasRendererSpell.js`, `canvasRendererLabels.js`, `canvasRendererLearning.js` | What the layer holds: dividers, lines, spells (batched), a spell by itself, names, learning paths |
| `treeStyle.js` | `TreeStyle`: the tree's look as tokens a design preset sets; halos, labels, sigil, heart runes, school ink |
| `treeStyleBook.js` | Adds the spellbook effects to `TreeStyle`: page, chapter titles, ornament dividers |
| `treeStyleInk.js` | Adds the drawn lines to `TreeStyle`: hand-drawn shapes, stippled, broken and engraved lines, the inset outline |
| `designPresets.js` | `DesignPresets`: built-in and `presets/design/*.json` looks, the Design Preset selector, preset CSS |
| `wheelRenderer.js` | Legacy SVG renderer (still loaded, not primary) |
| `treeViewerUI.js` | Tree viewer page setup and Import (with `treeLoad.js`, `spellCardView.js`, `spellProgressView.js`, `findSpell.js`, split out 2026-09-30) |
| `newSpells.js` | `NewSpells`: spells unlocked while the panel was closed (ring, mark, footer chip) and the footer's next goals (see *New spells and next goals*) |
| `detailsPeek.js` | Hover preview: the card follows the cursor without selecting; the panel keeps its place when nothing is selected |
| `treeParser.js` | Parses spell_tree.json, cycle detection, orphan fixing |
| `spellNames.js` | `spellDisplayName`: names in the spell card's lists, with the plugin when two spells share a name |
| `treeCore.js` | Core tree settings (globe position, size) |
| `trustedRenderer.js` | Renderer wrapper with validation |

### Tree Building
| Module | Purpose |
|--------|---------|
| `proceduralTreeBuilder.js` | Spell blacklist / plugin whitelist filters and `onProceduralTreeComplete`, which hands the C++ build to Classic |
| `shapeProfiles.js` | Shape definitions (explosion, tree, organic) read by `wheelRenderer.js` |

The trees themselves are built by the C++ Classic builder (see Growth Modes). The panel's JS builders -
`visualFirstBuilder.js`, `settingsAwareTreeBuilder.js`, `layoutGenerator.js`, `layoutEngine.js`,
`edgeScoring.js`, `growthBehaviors.js` - were removed on 2026-09-27 with the Simple, Procedural+ and
Visual-First builds that used them.

### Tree Preview
| Module | Purpose |
|--------|---------|
| `treePreview.js` | Preview orchestrator |
| `treePreviewSun.js` | Sun grid preview mode |
| `treePreviewFlat.js` | Flat grid preview mode |
| `treePreviewUtils.js` | Preview utilities: drag inputs, the canvases' size and fit (`fitCanvas`, `contentExtent`, `fitScale`), where a school's name goes (`schoolLabelPlace`) |
| `sunGridLinear.js` | Linear sun grid layout |
| `sunGridEqualArea.js` | Equal-area sun grid |
| `sunGridFibonacci.js` | Fibonacci spiral grid |
| `sunGridSquare.js` | Square grid layout |

### Growth Modes
| Module | Purpose |
|--------|---------|
| `treeGrowth.js` | Growth mode orchestrator (Classic is the one mode since 2026-09-27) |
| `treeGrowthStatus.js` | The builder status line and its Easy page copy (`TreeGrowth.setStatusText`, `relabelStatus`, `resetStatus`) |
| `classic/classicMain.js` | Classic growth mode |
| `classic/classicBuildRequest.js` | Classic build request to C++ and its failure path |
| `classic/classicTreeOutput.js` | Classic result and layout → the saved tree JSON |
| `classic/classicRenderer.js` | Classic mode renderer |
| `classic/classicLayout.js` | Classic layout engine |
| `classic/classicRadialOrder.js` | After the layout: nearby spells trade places so higher tier, then dearer Magicka, sits farther out (see TREE_BUILDING_SYSTEM.md, *Radial order*) |
| `classic/classicSettings.js` | Classic mode settings |
| `classic/treeBuilderSetting.js` | The builder options the player sets (Type grouping, Tiers by Magicka cost): controls, saved values, what a build request sends |
| `classic/classicThemeEngine.js` | Classic theme engine |

### UI Panels & Features
| Module | Purpose |
|--------|---------|
| `settingsPanel.js` | Settings page (all config) |
| `settingsPresets.js` | Settings preset save/load |
| `scannerPresets.js` | Scanner preset save/load |
| `easyMode.js` | Easy mode scan page: preset chips and the build/apply relays (the Tree Style picker and the Complex page's build mode tabs were removed with the other builders, 2026-09-27) |
| `schoolBridges.js` | Cross school bridges at build time: school order by kinship, bridge sources as soft prerequisites, traits and bridges baked into the saved tree |
| `bridgeView.js` | Cross school bridges in the viewer: ring on bridged spells the player has reached, dashed lines for the selected/hovered spell only, "Paths to other schools" list on the spell card, trait filter: the spell card's own keyword chips are pressable and veil the tree to light one trait across all schools, and so is the card's level (`bindLevel`, key `level.<Level>`: every spell of that level, 2026-09-30; levels come with the spell info after the tree was counted, so `TreeParser.updateNodeFromCache` drops the counts when a level changes and `countOf` counts again once, at the next ask), with a single pill (`#tree-trait-filter`) to clear it |
| `buttonHandlers.js` | Scan/build/apply button handlers |
| `buildProgress.js` | Build progress modal |
| `progressionUI.js` | Progression UI (learning targets, XP) |
| `prereqMaster.js` | Pre Req Master system |
| `editMode.js` | Tree edit mode |
| `colorPicker.js` | Color picker component |

### Visual Effects
| Module | Purpose |
|--------|---------|
| `starfield.js` | Animated starfield background |
| `globe3D.js` | 3D globe at tree center |
| `treeAnimation.js` | Build replay animation |

### Utilities
| Module | Purpose |
|--------|---------|
| `spellCache.js` | Spell data caching |
| `colorUtils.js` | Color management, school colors |

### Test only
| Module | Status |
|--------|--------|
| `unificationTest.js` | Run by `run-tests.js` and `test-runner.html`; not loaded by `index.html` |

The old renderers (`_archive/canvasRenderer.js`, and `spellTreeRenderer.js` with its `tierVisuals.js`,
which sat in `PrismaUI/views/SpellLearning/SpellTreeViewer/modules/` with no page loading them and were
removed on 2026-09-28) are no longer in the panel, and the WebGL renderer (`webglRenderer.js`, `webglShaders.js`, `webglShapes.js` -
PrismaUI has no WebGL) and the `autoTest.js` harness were removed on 2026-09-27.

---

## C++ ↔ JS Communication

### JS → C++ (31 listeners via `callCpp`; the main ones)

**Scanning:** `ScanSpells`, `SaveOutput`
**Tree:** `LoadSpellTree`, `SaveSpellTree`, `GetSpellInfo`, `GetSpellInfoBatch`
**Progression:** `SetLearningTarget`, `ClearLearningTarget`, `UnlockSpell`, `GetProgress`, `CheatUnlockSpell`, `RelockSpell`, `GetPlayerKnownSpells`, `SetSpellXP`, `SetTreePrerequisites`
**Config:** `LoadUnifiedConfig`, `SaveUnifiedConfig`, `SetPauseGameOnFocus`
**Presets:** `SavePreset`, `DeletePreset`, `LoadPresets`
**Tree Building:** `ProceduralTreeGenerate`, `PreReqMasterScore`
**Clipboard:** `GetClipboard`
**Other:** `HidePanel`, `LogMessage`, `LoadPrompt`

### C++ → JS (28 functions via `CallView`; the main ones)

**Lifecycle:** `onPrismaReady`, `onPanelShowing`, `onPanelHiding`
**Data:** `updateSpellData`, `updateTreeData`, `updateSpellInfo`, `updateSpellInfoBatch`
**State:** `updateSpellState`, `onResetTreeStates`, `onSaveGameLoaded`
**Progress:** `onProgressUpdate`, `onSpellReady`, `onSpellUnlocked`, `onSpellRelocked`, `onProgressData`
**Config:** `onUnifiedConfigLoaded`, `onPresetsLoaded`
**Tree Building:** `onProceduralTreeComplete`, `onPreReqMasterComplete`

`RelockSpell` (cheat mode, the Relock button on an unlocked spell) takes the spell from the player and,
since 2026-09-28, clears its progress in `ProgressionManager` (not unlocked, no XP, saved in the
co-save), so reopening the panel or loading the save does not unlock it and its children again. An
early-learned spell also gets its own name and descriptions back (no "(Learning - 40%)") and leaves
`SpellEffectivenessHook`'s early-learned set, which the co-save keeps; a learning target on the spell
is cleared. The footer's unlocked count is `updateUnlockedCount()` (`uiHelpers.js`) everywhere: every
node in the unlocked state, mastered or already known.

---

## Preset System

Two preset types, stored as individual JSON files:

### Settings Presets (`presets/settings/`)
Capture: Progression settings, early learning, tome learning, notifications (the weakened spell alerts; not
XP Alerts or Notification Language, which a preset never changes).
Bundled: `DEFAULT.json`, `Easy.json`, `Hard.json`

### Scanner Presets (`presets/scanner/`)
Capture: Tree generation settings, preview modes, PRM settings, growth configs.
Bundled: `DEFAULT.json`

Presets are loaded from files on panel open via `LoadPresets` → `onPresetsLoaded`.

---

## Input & Focus

The panel uses a `.focus-overlay` div with `background: rgba(0,0,0,0.01)` to capture mouse events (CEF requires non-transparent pixels for hit testing). `tabindex="-1"` captures keyboard input. The C++ side calls `Focus()`/`Unfocus()` on PrismaUI to control game input passthrough.

`pauseGameOnFocus` (default: true) freezes the game while the panel is open.
