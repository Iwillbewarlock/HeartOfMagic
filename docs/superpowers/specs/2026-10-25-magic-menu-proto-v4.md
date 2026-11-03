# SL_MagicMenu prototype, build 4 (2026-10-25)

Read `2026-10-24-magic-menu-proto.md` first (build 1-3 spec). This file only lists what build 4 changes.
Evidence for every change: `C:/Users/qtuna/Desktop/Claude outputs/ACF-Phase0/M0/video3/LEDGER.md`
(build 3 video, Gemini video analysis, GPT diagnosis, game log 2026-10-25 11:56).

## Why

Build 3 placed the spell preview with a per-model solver: it measured the selected model's live
`worldBound.center`, solved a world move with a numeric Jacobian and added it to the shared rest place
(`itemPos += move`, Preview.cpp:218). Game run: moves pile up (itemPos -8,-500,11 -> 17.8,-628.9,20),
particle art (`치명적인 독 분사`, MAGINVHealSpellArt) reports a bound that changes on every read and was
moved 100+ units off screen; a failed check was never undone; a `Scene Root` model did not move at all
(local += world delta ignores the parent transform); models of an earlier selection were never moved.

The user decided (2026-10-25): use Inventory Reforged's method, which the user checked by eye in IR's
own magic menu, only with our preview box as the window. IR is GPL-3: write our own code after the
method, credit IR in the source and the mod page. Do not copy its code.

IR reference (read for the method only):
`D:/qtuna/Temp/Temp/claude/C--Users-qtuna-Desktop/9bb0af64-2672-4818-94e4-468fbb6a4650/scratchpad/ir/x/source/plugin/equipment-panel/source/`
- `ItemView.cpp:604` PlacePreview, `:805` FitPreview, `:763` Measured, `:907` RestorePreview
- `Scene.cpp:11-36` Lens/Frustum/Depth, `:102` Point, `:111` Length, `:195` EachVertex, `:234` EachSphere, `:257` Outline
- `main.cpp:1275` FitPreview runs every menu frame; `36-window.as:983` passes the window as shares of the screen.

## P1 Preview: IR's place-and-fit (replaces G1/G2 and the solver)

1. **Game place.** Once per menu, before the first change: save `itemPos`, `itemPosCopy`, `itemScale`,
   `itemScaleCopy`. Restore them on close (as now). Depth = `(saved itemPos - camera.world.translate) · look`
   (camera = `UI3DSceneManager::camera`, axes from `world.rotate` columns: 0 look, 1 up, 2 right, IR Scene.cpp:96).
   Compute depth from the saved place, never from the live itemPos.
2. **Place (absolute, no accumulation).** Box centre in shares of the screen: `fromLeft = (box.cx - visible.left) / visible.width`,
   `fromTop = (box.cy - visible.top) / visible.height` (the movie and the 3D both cover the screen; port logged l0 r1 t1 b0).
   `place = cam + (look + right*(fL + (fR-fL)*fromLeft) + up*(fT + (fB-fT)*fromTop)) * depth` (frustum fLeft/fRight/fTop/fBottom).
   Base scale = `saved itemScale * boxHeightShare / 0.45` (IR's share of the screen height the game's preview takes).
   At rest (zoomProgress == 0 && zoomDistance == 0): every loaded model `local.translate += place - itemPos` and `Update`;
   `itemPosCopy = place; itemScaleCopy = scale`. Always `itemPos = place; itemScale = scale`.
   Done when the box or the visible rect changes (first frame, tab change that moves the box), not per selection.
3. **Fit (every frame, every loaded, not culled model).** Measure a model once (cache by model pointer; measure again when
   its `local.scale` or `local.rotate` is no longer what we set, at most 6 times, then sphere only): points = world
   vertices of every drawn, non-skinned `BSGeometry` under it (`rawVertexData`, `VA_POSITION`) minus `world.translate`;
   a shape without vertex data gives 8 points round its `worldBound` sphere in the camera's right/up plane.
   Seen = width/height and middle of the points on the camera right/up axes, at the game's scale.
   `grow = min(min(roomW/seenW, roomH/seenH) * 0.92, 3.0)` (room = box size in world units at that depth).
   `local.scale = gameScale * grow`; then move so `world.translate + middle*grow` lands on the room middle,
   the move taken into the parent's space (`parent.world.rotate` transposed, divided by `parent.world.scale`),
   only the right/up part of it. `Update` when anything changed. No turning (IR's weapon lay/stand-up logic is not needed for spells).
4. **Where it runs.** On the main thread in `PostDisplayHook::thunk` right before our `Render()` (game run: PostDisplay runs on the
   main thread, log 11:56:13.911), so the over-pass draws the fitted model in the same frame. Place (2) runs there too.
   The frame handler only passes the box/visible rect (as now).
5. **Not while Inventory Reforged is active** in this menu (as now), and not while zooming.
6. **Self-test (screen-grounded, replaces G1/G2).** For each model, once per measure, log `SelfTest: P1 PASS|FAIL <model> <spell>`:
   project the 4 corners of the fitted seen rect (world points) with `WorldPtToScreenPt3` into stage units and check the rect's
   centre is within 4 px of the box centre and the rect lies inside the box. Also log the measure (vertex or sphere, seen size, grow).
   This checks what is drawn (projected geometry), not a solver's own number.

Known limit: particles that fly out of the model's measured size (fire sparks) can still leave the box; the 3D is not clipped.

### P1 changes from the GPT design review (gpt-6-astra, 2026-10-25; `video3/codex_v4_design_result.md`)

- **Parent space for the place too.** P1.2's move (`place - itemPos`) goes into each model's parent space like the fit:
  `local.translate += parent.world.rotate^T * delta / parent.world.scale` (identity when no parent). IR skips this; we do not.
- **Particle sphere guard.** `EachVertex` reads only CPU vertices (`rawVertexData` with `VF_VERTEX`) of non-skinned geometry;
  `NiParticleSystem` and anything without them fall back to the shape's sphere. A sphere-only measure is accepted only when
  it is finite, positive and stable: 3 consecutive frames whose radius and centre (relative to `world.translate`, divided by
  the radius) differ by under 10%. Until then, and if it never settles within 60 frames, the model keeps the base scale and its
  `world.translate` goes to the room middle (no bound-centre offset, no grow). Never track a sphere frame by frame.
- **Self-test is not called "on screen" unless it is.** After `Update`, project the model's real points again (vertices, or the
  sphere points) with `WorldPtToScreenPt3`, convert to screen pixels; convert the preview box's corners to screen pixels on the
  movie's own side (`GFxMovieView::TranslateToScreen`, or the visible-rect mapping the box uses, logged as which). Compare in pixels:
  centre within 4 px and the points' rect inside the box -> `P1 PASS`; sphere-only models are logged as `P1 BOUND` (not PASS).
  Once per measure, plus a one-time axis probe: project `cam + look*depth`, `+right`, `+up` and log that right goes right and up goes up.
- **SE/AE only.** On VR (`REL::Module::IsVR()`) the preview is left alone and one log line says so (VR's loadedModels layout differs).
- **Render order is not a promise.** Keep `original -> place/fit -> Render`. Log once per new model pointer whether it was fitted before
  its first over-pass render. The vanilla pass (under the movie) can still show one unfitted frame outside the box; accepted, logged.
- **Coordinates spelled out.** `roomW = depth * (fR - fL) * boxW / visibleW`, `roomH = depth * (fT - fB) * boxH / visibleH` (absolute
  values), shares measured on the visible rect (`visible -40..760` -> divide by 800).
- **State rules.** Frame handler (any thread) captures box/visible and queues; place/fit run on the main thread (PostDisplay) with the
  generation check. A place skipped while zooming stays dirty and is applied when at rest. The per-model cache drops entries that left
  `loadedModels`; it is not cleared on every open. The cache keeps each model's game baseline (scale/turn when first seen) apart from
  what we set, so a surviving model's fitted scale is never taken as its baseline.

### P1 changes from the build 4 code review (GPT gpt-6-astra + reviewer agent, 2026-10-25)

- **The box is the content area** (R9): place, fit and the P1 test use `Settings::PanelContent(preview)` = the panel minus its
  title band and padding (648+8 .. 957-8 x 67+22+8 .. 330-8 by default), so a tall model no longer covers the "PREVIEW" title.
- **Aim at the model's own depth** (R6): the room keeps the line of sight through the box centre (frustum `across`, `along`) and
  its depth. Fit aims at `cam + (look + right*across + up*along) * d_m` with `d_m = (world.translate - cam) . look`, and the room
  is scaled by `d_m / depth`, matching the perspective projection the self-test uses.
- **Re-measure on every turn** (G1): a fitted model of any kind (vertices, mixed, spheres) is measured again when its
  `local.rotate` is no longer what we set; the 6-measure vertex cap stays.
- **Keep the last reading while a new one settles** (R11): grow and middle of the last settled reading of this menu stay in use
  while a re-measure settles or when it never settles; only a model with no reading yet sits at base scale, centred.
- **World scale for the baseline** (R12): `toBaseline = baseline * parent.world.scale / model.world.scale`.
- **Models up before the place** (R2): Place multiplies each loaded model's `local.scale` by `new base scale / the base scale it
  was built at` (`itemScaleCopy`, or the cache entry's `builtAt` for a model kept from an earlier menu) and scales the entry's
  baseline, setScale and readings by the same ratio; one log line per model with the ratio and the new local.scale.
  A game-set scale (Notice) rescales the readings too.
- **Cache per menu** (R5): an entry first seen in a new menu generation is measured afresh (measures, phase, reading, log count
  reset; baseline kept); first-render marks are per (model, menu).
- **Log caps and close summary** (R4): per model and menu, the first 3 measures log their measure and P1 lines, later ones are
  counted only. At close: `SelfTest: P1 summary PASS|FAIL|BOUND - menu N: measures, models, lines not logged, every P1 verdict
  (logged and not), loadedModels at the last fitted frame and at close, cache size`. FAIL when any verdict, logged or not, failed.
- **First renders before the frame info** (R3): renders before this menu's reset or its first frame info are logged as
  "not fitted: no frame info yet" instead of being skipped.
- **The hook carries either job** (G4): MagicMenu PostDisplay is hooked when `bMovePreview` or `bDrawPreviewOver` is on. Place/fit
  runs with bMovePreview (before the over-pass when that is on, else ahead of the game's own next pass), Render with
  bDrawPreviewOver. One line per menu open: `Widget: preview place/fit ON|off (why)`.

Second review round (2026-10-25):
- **Perspective measure** (R4): each point is brought along its line of sight to the model's depth before it is measured
  (`(p-eye).right * d_m / (p-eye).look`, the same for up), as the self-test projects it.
- **Turns wait until still** (R3, I2): a turn (drag or the game's own) re-measures a fitted or unsettled model once its
  `local.rotate` has held still 5 frames; such measures do not count against the 6-measure vertex cap.
- **Re-created models** (X2): an entry of an earlier menu whose model is not at the scale we left it at is taken as a new model
  (Place drops it and uses itemScaleCopy; Entry takes its scale and the current base scale).
- **No empty box** (X3): Settings refuses an fPreview rect whose content area is empty (keeps the default, warns); Place refuses
  an empty box too.
- **Land/instance vertex layouts** (R5): shapes with `VF_LANDDATA` or `VF_INSTANCEDATA` count by their sphere
  (`VertexDesc::GetSize` leaves those out).
- **Summary wording** (I3): `P1 summary (no measure)` only when nothing was measured; `(not tested: bSelfTest=0)` otherwise
  without the self-test.
- **See-through box without the over-pass** (B1): when the over-pass is off for a menu (and IR is not active), the preview panel
  is drawn with its edge and title only, so the 3D the game draws before the movie shows through.

Third check (GPT, 2026-10-25):
- **Grow by re-projection**: growing about the model's place moves each point's depth, so the perspective size is not
  `seen * grow` (centre depth 10, vertices at 15, 20 x 20 outline, room 10 x 10: seen 13.33, grow .69, grown outline 10.26,
  overflows). A reading keeps its points (from world.translate, at the game's size); Apply bisects (20 steps) for the largest
  grow in (0, 3] whose grown points, projected as Read does from the aim on the line of sight, fill at most 0.92 of the room
  both ways, and centres by that extent. Bound-sphere points take the same path. Cached per reading, room and depth; the measure
  line gives the grow and the steps. Checked with a Python copy of the formula: the counter-example gives grow .597, width 9.20.

## P2 Item card follows a tree click on an unlearned spell

Research: GPT (`video3/codex_card_result.md`, SkyUI source on GitHub schlangster/skyui).
- SkyUI path: list highlight -> `GameDelegate.call("RequestItemCardInfo")` -> `Menu_mc.UpdateItemCardInfo(obj)` -> `itemCard.itemInfo = obj`
  (ItemMenu.as:68-94, 215-219). The card itself is `_root.Menu_mc.itemCard`. Spell card fields (ItemCard.as:247-261, Inventory.as:10):
  `type` 7 (`ICT_SPELL`), `name`, `effects` (finished description HTML), `castLevel` (level text), `spellCost`, `castTime` (0 = per-second cost frame).
- On a tree click whose spell is not in the list (`SelectInList` found nothing): build the info and set `itemCard.itemInfo = info`, then
  `itemCard.FadeInCard()`. Not `UpdateItemCardInfo` (that also rewrites the bottom bar).
  1. First try the game's own filler: `MagicMenu runtime itemCard->SetForm(spell)` (CommonLib ItemCard.h:21, IDs 51020/51898), then copy its
     `obj` to the setter. Log the keys it filled (`SelfTest: P2 SetForm keys ...`). Use it only when `type`, `name`, `effects` are present.
  2. Else build it: `name` = GetName, `effects` = `MagicSystem::GetMagicItemDescription` (MagicSystem.h:100; tag arguments as SkyUI shows them,
     log the result), `spellCost` = `CalculateMagickaCost(player)` rounded like the list, `castTime` from casting type, `castLevel` = the level
     text of the costliest effect's minimum skill level if a game string can be found, else empty (logged).
- Back to the list: when the list selection changes again, or the user clicks a tree node that is in the list, dispatch SkyUI's own
  `itemHighlightChange` for the current `selectedIndex` on `_root.Menu_mc.inventoryLists` (re-sending the same selectedIndex returns early,
  ScrollingList.as:307-310). Selection -1 -> `FadeOutCard()`.
- `SelfTest: P2 PASS|FAIL`: after the setter, read `itemCard.itemInfo.name` back and compare with the clicked spell's name.

Build 4 review notes (2026-10-25):
- **Menu fade flag** (G3): `FadeInCard` flips only the card's `bFadedIn`; SkyUI's menu keeps `_bItemCardFadedIn` (ItemMenu.as:344-367),
  and with it false a later highlight of -1 does not fade the card out. When our tree card goes up, `Menu_mc._bItemCardFadedIn` is
  set true (logged with its old value); when we fade the card out for -1 it is set false again. Every frame while our card is up:
  the card's `bFadedIn` false (SkyUI's own no-selection path) drops our state; a category change that leaves the list at -1
  fades the card out and drops our state too (WatchSelection alone saw FormID 0 -> 0 and did nothing).

Second review round (2026-10-25):
- **P3 follows the card out** (X1): every path that drops our tree card (the menu's own fade-out, a category with no selection,
  RestoreListCard) drops P3's wait too. RestoreListCard's no-selection path calls SkyUI's own `Menu_mc.onHideItemsList`
  (`UpdateItem3D(false)` and `FadeOutCard`, ItemMenu.as:375-379), falling back to `FadeOutCard` alone.

## P3 The 3D preview follows the same click (behind ini, try and log)

`bPreviewUnlearned = 1` (default on in this test build): on the same click call `Inventory3DManager::LoadInventoryItem(spell, nullptr)`
(SE 50885 / AE 51758; SpellItem is a TESBoundObject). Within 30 frames log `SelfTest: P3 PASS` if a loadedModels entry with `itemBase == spell`
appears, else `P3 FAIL` and stop trying for the rest of the menu. The list path puts the list's own model back (SkyUI's `UpdateItem3D` runs on
its highlight event). UNVERIFIED that the game builds a magic preview this way; that is what P3 tests.

Build 4 review notes (2026-10-25):
- **Request token** (G2, replaced in the second round: P3 now runs in the frame handler): every request and cancel bumped a token; the queued load and every check carry theirs and do nothing when
  it is stale ("P3 load for X dropped - a newer request or a cancel came first"). A load still queued when the list took another
  spell no longer replaces that spell's preview.
- **Logged before the call** (R7): "P3 calling LoadInventoryItem(...)" is written through (FlushLog) before the unverified call,
  "P3 LoadInventoryItem returned" after it.

Second review round (2026-10-25):
- **In the frame handler** (reviewer R1/R2): `LoadInventoryItem` and the 30 model checks run in the frame handler, inside the
  movie's Advance, where the game's own `UpdateItem3D` runs (SkyUI GameDelegate) and where Inventory Reforged calls it (its
  OutfitModel GFx function, main.cpp:789); not as SKSE main-thread tasks (build 3 log: frame handler and main thread differ).
  Nothing is queued, so the request token is gone.

## P4 Item card fit: overflow and the clipped "레벨" label

- Fit only after the card's external background has loaded and after each `itemInfo` change: re-measure every frame for the first 60 frames of a
  menu and after each card content change, and re-fit when the measured size changes. Measure the union of the visible parts (background and the
  text fields), not the background clip alone.
- `SelfTest: P4 PASS|FAIL` checks all four edges of that union (in `_root` stage units) against the slot, logged with the union, after the last re-fit.
- `SkillLevelLabel` (installed itemcard.swf: 23 x 22.95, autoSize false, wordWrap true; Korean text "레벨 "): set `wordWrap = false`,
  `autoSize = "right"` after each frame change of the card. Log its text and width once.

Build 4 review notes (2026-10-25):
- **Verdict covers what the holder draws** (R1): build 3's whole-card bounds ended at 954.8 while the black panel on screen reached
  about 1112, and the card was never fitted again in 86 s, so the late-load theory is half refuted. P4 PASS now needs the visible
  union inside the slot on all four edges and top-aligned (R13), and the whole card (`ItemCard_mc` getBounds) and the fade
  holder's `_root` bounds inside the slot; a missing bound is a FAIL. `SelfTest: P4 WARN` when the union fills the slot neither
  across nor down.
- **Layout log** (R1): during each measure window, once per change (at most 6 per menu): `_root` bounds of the holder and each of its
  display-object children, the whole card, Menu_mc and each of Menu_mc's children (hidden ones marked), to name the clip of the
  black panel from the log.
- **Level label sizes** (R14): the SkillLevelLabel line (once per menu, on a spell card) gives `_width x _height` and
  `textWidth x textHeight` before and after the fix.

Second review round (2026-10-25):
- **FAIL text** (I4): when the visible union passes but the whole card or the holder is outside, the FAIL line says
  "hidden part outside, not fitted (the fit counts the visible union only)".
- **Layout log cost** (I5): the layout is read every 10th measured frame, not every frame.

## P5 Out of scope for build 4

Clipping the 3D to the box; the school a non-tree spell inherits (`카이단 옵션` logged as Restoration then Alteration); tree label overlaps.
