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

**Modules:** `treeViewerUI.js`, `canvasRendererV2.js`, `wheelRenderer.js`, `treeCore.js`, `progressionUI.js`

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
│ [Import] [LLM toolbar] Spells: 247  Legend   │
└──────────────────────────────────────────────┘
```

### Features
- **Canvas 2D renderer** — primary renderer for 200+ node trees
- **Node interaction** — rest the cursor on a spell to preview its card, click to select it (see
  *Hover preview* below); the panel's close button, a click on empty canvas or Esc drop the
  selection. Esc works in two steps (`initializeKeyboardShortcuts` in `script.js`): with a spell
  selected on the tree the first press only drops it, the next closes the panel. An Esc a dialog's own
  field already used (Find Spell, a rename box) does neither
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
- **How-to-Learn panel** — shows what the player needs to do to unlock a spell
- **Discovery mode** — hides spell names/effects until XP thresholds are met
- **Zoom/pan** — mouse wheel + drag; one wheel notch zooms by `CanvasRenderer.WHEEL_ZOOM_STEP` (15%, was 10%; out is the exact inverse of in, so a notch in and out returns to the same zoom)
- **Heart Settings popup** — cosmetic settings (starfield, globe, dividers, connections, node sizes)
- **Edit mode** — move nodes, pen tool, eraser (developer feature)
- **Empty state** — shown when no tree is loaded, prompts user to scan

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
`under`) and kept while the hover, selection, layer and view stay the same (option `version`). The tree
canvas is not touched by a hover change at all (bench: 39 draws on it before, 0 now); the picture is the
same. Without FxLayer, `composite` works as before.

**Smaller things in the repaint** (2026-09-26):
- The tree layer's margin is 128 css px, not 256 (`TREE_LAYER_MARGIN`): for the default panel the layer was
  about 1.6 times the pixels, cleared and copied on every repaint. A drag repaints after 128 px instead.
- The simple level of detail (zoom 0.25-0.45) batches its spells too (`NodeBatch.begin(true)`: shapes not
  turned, as that level draws them): 3,916 → 116 paint calls per repaint in the bench; the selected and
  hovered spell are still drawn on their own.
- The tree canvas has a whole-pixel size (`updateCanvasSize` floors the container's), and the panel sits on
  whole pixels (`modules/panelSnap.js`: a margin under a pixel when centred, rounded drag positions;
  again on window resize, a panel resize and fullscreen). Otherwise the view resampled the canvas each
  time it painted it.
- Labels: a spell off the canvas is dropped before the name-reveal lookups, the shortened name is kept on
  the spell (`_labelText`), and the text widths per font (`_labelWidth`). The names in the layer's margin
  are drawn too, after those in view (`renderLabels`' `margin`): a drag that slid the layer used to show
  spells there without names until the next repaint.
- A reply with spell names (`updateSpellInfoBatch`) now marks the canvas tree for a repaint; it only
  called the SVG renderer, which holds no nodes while the canvas one is in use, so the names showed with
  the next repaint for something else.
- Tried and dropped: recording the calls of a tree repaint and replaying them when only the view moved
  (zoom settling, glide end). In the desktop bench the recording made a tree change 8.6 → 21 ms (every
  call through a wrapper) and a replay drew as much as the whole recorded area, never less than a culled
  repaint; in game a repaint is already 12-19 ms (`[Perf]` worst).

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
  selected and hovered spell, the hover path - are still drawn one by one by
  `renderNode`, after the batch. The school shapes live in `NodeBatch.SHAPES`/`ROTATION`, and
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
- **Background and tree as one picture** (`modules/staticBase.js`). When the background is still - a design
  page, no stars, or stars held still - `render` leaves it to `_drawTree`, which asks `StaticBase` to paste
  background and tree layer as one kept picture: an animation frame is one full-canvas pass instead of
  three (colour, page or stars, layer). The picture is keyed by the layer (its draws, the hover preview,
  the pan offset) and the background; a key is built only when it has held for two frames, so a drag or
  glide is drawn straight as before. A stretched paste always draws straight.
  `TreeStyle._pageBuilds` tells it the page was rebuilt.
- **Zoom read-out** (`CanvasRenderer.showZoom`): written only when the number shown changes; the camera
  called it every frame of a glide.

**Moving parts on their own canvases** (`modules/fxLayer.js`, 2026-09-26). PrismaUI 1.5.1 draws the
whole panel on the CPU into one bitmap (Ultralight `BitmapSurface`) and copies it to a game texture each
time something changed; the accelerated view is not in its public API. So an animation frame costs what
it changes on the page, not what our drawing costs: in game the loop turned every ~110 ms for 2 ms of our
drawing, and clicks waited behind it. The heart (with its runes and globe), each spell's sigil or learning
glow, and the travelling particles are now drawn on small canvases laid over the tree canvas
(`FxLayer.draw(key, box, dpr, drawFn)`; drawn in the tree canvas's own coordinates, reused by key, hidden
when not drawn in a frame). The tree canvas is not touched on a frame whose picture it already shows
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
- **Idle**: after `IDLE_AFTER_MS` (15 s) without input no particle leaves the heart, and frames that repaint
  the whole tree canvas (moving stars) come every `IDLE_FRAME_MS` (250) at most.
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
- **What is left** (measured in game after the above): the slowest frame after a zoom went from 97-144 ms to
  12-19 ms and our drawing is 2-3% of the time, but the loop still turns only every 34-37 ms with the
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
  not follow its rounded corners). Candlelit Tome and Night Grimoire set `--book-cover`, the leather's
  one colour; a design without it gets `--book-cover-top`. Small effects stay (a button's hover glow, the
  Spell Tree tab's text glow, slider fills).
- **Glows where they barely show:** a known spell's halo is left out when its radius on screen is under
  `HALO_MIN_SCREEN_PX` (20; the learning spell's glow stays), and a design's glow under known lines below
  `EDGE_GLOW_MIN_ZOOM` (0.8): zoomed out, Arcane drew hundreds of halo sprites and wide strokes per repaint
  (spells 25-45 ms, lines 15-30 ms of it).
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
  in front. **A leaking 300 ms poll** in Oracle settings (a new one per visit to Oracle mode) is gone.

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
it is in `.gitignore`. A live switch re-labels everything marked `data-i18n`; text a
script already built with `t()` stays until the next start, and the picker says so. **Known now
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

### Hover preview (2026-09-23)

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
- *On the tree* - the design's page, the starfield (greyed out with a note while a page is drawn, since
  the page replaces it), base lines and spell names.

A design effect turned off stays off in every design - `TreeStyle.setEffectsOff` blanks those tokens
after any design's are set - and a chip the current design has no use for is greyed out. Saved as
`designEffects` (`{ page, sigil, glow, runes }`, false = off; an old `reveal` key is ignored). A chip is a checkbox inside a
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
every design for good, so a config from before (no `renderSettingsVersion`) has those values put back to
the shipped ones on its first load (`RenderSettings.afterLoad`), and Reset to Defaults does the same. A
wrongly typed value in a `render` block ("60" for 60) is converted or skipped (`DesignPresets._typed`).
The controls are put back from the config once it has loaded (`RenderSettings.syncControls`). The popup
is leather in Arcane and the cover colour in the dark-book designs.

**Scan screen previews:** the tree previews there (`treePreview.js`, `treeGrowth.js`, `prereqMaster.js`)
fill their canvas with `getPreviewBackground()` (`uiHelpers.js`): the design's `--preview-bg` CSS
variable, else the old near-black. They repaint every frame, so the value is read at most once a second.

**School tabs** sit in a row under the zoom tools (`patch-ui.css`, all designs). Centred on the top row
they covered the right half of the tools - the renderer badge, heart settings and edit tree - at the
default panel size. The trait filter pill moved down one row with them.

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
Two looks ship with the mod that way, always installed and picked in the list like the built-ins, and
double as examples for modders: **Night Grimoire** (indigo vellum, gold-leaf ink) and **Candlelit Tome**
(dark leather lit in the middle), `SKSE/Plugins/SpellLearning/presets/design/*.json`. They are a few KB
each, so there is no installer choice for them. Their panel is one shared sheet,
`themes/design-darkbook.css` (a tooled cover, serif gold lettering, dark pages where Arcane has
parchment, gold switches and tools), coloured by the `--book-*` palette each preset sets in its inline
`css`. `DesignPresets._applyCss` keeps the inline CSS after the stylesheet, so a palette always wins over
the sheet's fallback values.

Code: `modules/treeStyle.js` (`TreeStyle`: tokens, halos, labels, sigil, heart runes, school ink),
`modules/treeStyleBook.js` (adds the page, chapters and ornament dividers to `TreeStyle`),
`modules/designPresets.js` (`DesignPresets`: built-ins, disk presets, the selector, CSS). The renderer
reads colours from `TreeStyle.tokens` where it used literals before.

What it costs, with the tree layer in mind (see *Performance* above):

- **Edges are batched.** Pass 1 of `renderEdges` groups edges by look and strokes each group as one
  path - a dozen strokes instead of one per edge. It paid for the rest: on the 1,315-spell desktop test
  tree a full redraw is 3.4 ms under both presets (Classic 1,122 paint calls, Arcane 1,037).
- **The page replaces the starfield.** It is drawn once into a texture per canvas size and pasted, and
  it asks for no frames; the starfield asked for 20 a second. Idle frames went from 409 paint calls to
  213 (what is left is the globe).
- **Moving parts sit on top of the layer.** The selection sigil and the learning glow
  (`TreeStyle.renderOverlay`) are painted after the layer is pasted, a handful of calls, and only move
  when something else already asks for frames. The ink reveal on opening (the layer pasted through a
  growing clip for 600 ms at full rate) was removed on 2026-09-26: in the game every open paid for those
  frames, in the CPU-drawn view, right when the panel was busiest. A design's `reveal`, `revealMs` and
  `inkColor` are ignored.
- Label outlines cost one `strokeText` per label (at most 150).

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

**Modules:** `easyMode.js`, `treeGrowth.js`, `buttonHandlers.js`, `scannerPresets.js`, `buildProgress.js`, `prereqMaster.js`

### Shared Components (both modes)

```
┌──────────────────────────────────────────────┐
│       [EASY]  [COMPLEX]  ← mode toggle       │
├──────────────────────────────────────────────┤
│ Status: Ready to scan                        │
│ [Scan Spells] [Export Scan]                  │
├──────────────────────────────────────────────┤
│ Total: 247 │ Mods: 12 │ Primed: 0           │
│ [Blacklist] [Whitelist] [☑ Tomes Only]       │
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
Bidirectional soft prerequisite mirroring. Currently **disabled** (code commented out).
- Bidirectional toggle
- Stats display

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

**Progression:** How XP is earned and what thresholds unlock spells.
**Early Learning:** Grants nerfed spells before full mastery.
**Spell Tomes:** How reading spell tomes interacts with progression.
**Developer:** Tree generation tuning, procedural injection, LLM settings (hidden by default).

---

## Modals

| Modal | Trigger | Module |
|-------|---------|--------|
| Build Progress | Build Tree button | `buildProgress.js` |
| Preset Name | Save Preset button | `uiHelpers.js` |
| Blacklist | Blacklist button | `buttonHandlers.js` |
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
  → Choose growth mode (Classic/Tree/Sun/Flat)
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
| `canvasRendererV2.js` | **Primary** Canvas 2D renderer (handles 200+ nodes) |
| `treeStyle.js` | `TreeStyle`: the tree's look as tokens a design preset sets; halos, labels, sigil, heart runes, school ink |
| `treeStyleBook.js` | Adds the spellbook effects to `TreeStyle`: page, chapter titles, ornament dividers |
| `designPresets.js` | `DesignPresets`: built-in and `presets/design/*.json` looks, the Design Preset selector, preset CSS |
| `wheelRenderer.js` | Legacy SVG renderer (still loaded, not primary) |
| `treeViewerUI.js` | Tree viewer page logic, node selection, detail panels |
| `detailsPeek.js` | Hover preview: the card follows the cursor without selecting; the panel keeps its place when nothing is selected |
| `treeParser.js` | Parses spell_tree.json, cycle detection, orphan fixing |
| `treeCore.js` | Core tree settings (globe position, size) |
| `trustedRenderer.js` | Renderer wrapper with validation |

### Tree Building
| Module | Purpose |
|--------|---------|
| `proceduralTreeBuilder.js` | JS/C++ procedural tree builder |
| `visualFirstBuilder.js` | Visual-first builder (layout → assign → edges) |
| `settingsAwareTreeBuilder.js` | Settings-aware wrapper |
| `layoutGenerator.js` | Grid/shape layout generation |
| `edgeScoring.js` | Edge scoring for spell relationships |
| `shapeProfiles.js` | Shape definitions (explosion, tree, organic) |
| `growthDSL.js` | Growth recipe DSL parser |
| `growthBehaviors.js` | Growth behavior implementations |

### Tree Preview
| Module | Purpose |
|--------|---------|
| `treePreview.js` | Preview orchestrator |
| `treePreviewSun.js` | Sun grid preview mode |
| `treePreviewFlat.js` | Flat grid preview mode |
| `treePreviewUtils.js` | Preview utilities |
| `sunGridLinear.js` | Linear sun grid layout |
| `sunGridEqualArea.js` | Equal-area sun grid |
| `sunGridFibonacci.js` | Fibonacci spiral grid |
| `sunGridSquare.js` | Square grid layout |

### Growth Modes
| Module | Purpose |
|--------|---------|
| `treeGrowth.js` | Growth mode orchestrator |
| `treeGrowthTree.js` | Tree growth mode (trunk/branch) |
| `classic/classicMain.js` | Classic growth mode |
| `classic/classicRenderer.js` | Classic mode renderer |
| `classic/classicLayout.js` | Classic layout engine |
| `classic/classicSettings.js` | Classic mode settings |
| `classic/classicThemeEngine.js` | Classic theme engine |
| `tree/treeRenderer.js` | Tree mode renderer |
| `tree/treeTrunk.js` | Tree trunk generation |
| `tree/treeSettings.js` | Tree mode settings |

### UI Panels & Features
| Module | Purpose |
|--------|---------|
| `settingsPanel.js` | Settings page (all config) |
| `settingsPresets.js` | Settings preset save/load |
| `scannerPresets.js` | Scanner preset save/load |
| `easyMode.js` | Easy mode scan page, incl. the Tree Style picker (same choice as the Complex page's build mode tabs; the player's pick beats a preset's) |
| `schoolBridges.js` | Cross school bridges at build time: school order by kinship, bridge sources as soft prerequisites, traits and bridges baked into the saved tree |
| `bridgeView.js` | Cross school bridges in the viewer: ring on bridged spells the player has reached, dashed lines for the selected/hovered spell only, "Paths to other schools" list on the spell card, trait filter: the spell card's own keyword chips are pressable and veil the tree to light one trait across all schools, with a single pill (`#tree-trait-filter`) to clear it |
| `generationModeUI.js` | Complex mode per-school controls |
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

### LLM Integration
| Module | Purpose |
|--------|---------|
| `llmIntegration.js` | LLM API integration |
| `llmTreeFeatures.js` | LLM tree features |
| `llmApiSettings.js` | LLM API settings UI |

### Utilities
| Module | Purpose |
|--------|---------|
| `spellCache.js` | Spell data caching |
| `colorUtils.js` | Color management, school colors |

### Unused / Archive
| Module | Status |
|--------|--------|
| `_archive/canvasRenderer.js` | Replaced by canvasRendererV2 |
| `_archive/spellTreeRenderer.js` | Old renderer |
| `_archive/tierVisuals.js` | Old tier visuals |
| `webglRenderer.js` | Not supported in CEF |
| `webglShaders.js` | Not supported in CEF |
| `webglShapes.js` | Not supported in CEF |
| `unificationTest.js` | Test only |
| `autoTest.js` | Test only |

---

## C++ ↔ JS Communication

### JS → C++ (33 listeners via `callCpp`)

**Scanning:** `ScanSpells`, `SaveOutput`, `SaveOutputBySchool`
**Tree:** `LoadSpellTree`, `SaveSpellTree`, `GetSpellInfo`, `GetSpellInfoBatch`
**Progression:** `SetLearningTarget`, `ClearLearningTarget`, `UnlockSpell`, `GetProgress`, `CheatUnlockSpell`, `RelockSpell`, `GetPlayerKnownSpells`, `SetSpellXP`, `SetTreePrerequisites`
**Config:** `LoadUnifiedConfig`, `SaveUnifiedConfig`, `SetHotkey`, `SetPauseGameOnFocus`
**Presets:** `SavePreset`, `DeletePreset`, `LoadPresets`
**LLM:** `CheckLLM`, `LLMGenerate`, `PollLLMResponse`, `LoadLLMConfig`, `SaveLLMConfig`
**Tree Building:** `ProceduralTreeGenerate`, `PreReqMasterScore`
**Clipboard:** `CopyToClipboard`, `GetClipboard`
**Other:** `HidePanel`, `LogMessage`, `LoadPrompt`, `SavePrompt`

### C++ → JS (30+ calls via `InteropCall`)

**Lifecycle:** `onPrismaReady`, `onPanelShowing`, `onPanelHiding`
**Data:** `updateSpellData`, `updateTreeData`, `updateSpellInfo`, `updateSpellInfoBatch`
**State:** `updateSpellState`, `onResetTreeStates`, `onSaveGameLoaded`
**Progress:** `onProgressUpdate`, `onSpellReady`, `onSpellUnlocked`, `onProgressData`
**Config:** `onUnifiedConfigLoaded`, `onPresetsLoaded`
**Tree Building:** `onProceduralTreeComplete`, `onPreReqMasterComplete`, `onBuilderStatus`
**LLM:** `onLLMStatus`, `onLLMQueued`, `onLLMPollResult`, `onLLMConfigLoaded`

---

## Preset System

Two preset types, stored as individual JSON files:

### Settings Presets (`presets/settings/`)
Capture: Progression settings, early learning, tome learning, notifications.
Bundled: `DEFAULT.json`, `Easy.json`, `Hard.json`

### Scanner Presets (`presets/scanner/`)
Capture: Tree generation settings, preview modes, PRM settings, growth configs.
Bundled: `DEFAULT.json`

Presets are loaded from files on panel open via `LoadPresets` → `onPresetsLoaded`.

---

## Input & Focus

The panel uses a `.focus-overlay` div with `background: rgba(0,0,0,0.01)` to capture mouse events (CEF requires non-transparent pixels for hit testing). `tabindex="-1"` captures keyboard input. The C++ side calls `Focus()`/`Unfocus()` on PrismaUI to control game input passthrough.

`pauseGameOnFocus` (default: true) freezes the game while the panel is open.
