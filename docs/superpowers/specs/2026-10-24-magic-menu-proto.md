# Magic menu prototype (ACF M1-0) - spec

Date: 2026-10-24. Branch `feature/magic-menu-proto` (from `feature/h1-core-state` @ 69ad5ccf).
Design this prototype tests: `C:\Users\qtuna\Desktop\Claude outputs\ACF-Phase0\M0\M0_MAGIC_MENU_DESIGN.md` (v5), mockups `...\M0\mockup\v7_*.png`.
User: "build a prototype first, fix later". This is a vertical slice to answer questions in game, not the product.

## 1. Decisions

1. **Where:** a new DLL `SL_MagicMenu` in this repo (`plugins/SL_MagicMenu/`, same pattern as `plugins/BookXP`). Separate from `SpellLearning.dll`. Includes only `SpellLearningAPI.h` from HoM - never `UIManager`, `ProgressionManager` or anything else from `plugins/spelllearning`.
2. **Drawing:** C++ GFx drawing into the magic menu movie's `_root` (the bookmark way: `feature/magic-menu-bookmark`, `plugins/spelllearning/src/magicmenu/`). No SWF. This prototype does NOT answer the SWF toolchain question.
3. **Data (PROTOTYPE ONLY - replaced by the I1 contract):**
   - HoM API v2 (`SpellLearningAPI.h`, received via `RegisterListener("SpellLearning")`, `kMessageType_APIReady`): `GetLearningTarget(school)`, `GetProgress`, `GetRequiredXP`, `IsSpellMastered`, `IsSpellAvailableToLearn`, `SetLearningTarget`.
   - Tree geometry/links: parse `Data/SKSE/Plugins/SpellLearning/spell_tree.json` directly at `kDataLoaded` (strip a UTF-8 BOM).
4. **Working name** `SL_MagicMenu`. Real name later (design U6).

## 2. In scope

- Menu open (`MenuOpenCloseEvent`, `MagicMenu`) -> UI task -> one clip `_root.mmx_root` with four children. Removed/ignored when the menu closes (the movie dies with it).
- **Preview box**: black filled box with a thin border, label "PREVIEW". Fixed stage position (ini).
- **Tree window**: black box, title "SPELL TREE". Centre = the spell selected in the SkyUI list if it is in the tree; otherwise keep the previous centre; on first open with a non-tree selection, the school's current learning target, else the school root. Nodes within 2 link steps (structural `children`/`prerequisites` + lock `locks[].nodeId`), visible inside the window after rotation, max 40 (nearest to centre first; the centre always kept). Rotation per school so the school's `spokeAngle` (degrees, canvas y-down) points up: rotate world by `(-90 - spokeAngle)` degrees; no rotation when `noRotate` or `layoutMode == "flat"` or no `spokeAngle`. Fixed scale (ini `fTreeScale`, stage units per world unit). Node shapes: mastered = filled gold diamond; learning target = hollow diamond + blue arc (progress); available = hollow white diamond; locked = dark grey diamond + small padlock mark; ready to unlock = full blue ring. Name under the node (small text, cut at ~14 chars). Edges: structural solid, lock dashed; an edge to a node outside the window is drawn to the window edge.
- **Progress card**: "<SCHOOL> - LEARNING", target name, bar, "xp / required XP", "N spells can be learned  [R] Next". School = the active school of the list (from the selected spell's school; keep last). Hidden when no school yet.
- **Node card**: shown only after a node was clicked in the tree. Name, state, "Requires all of:" hard list and "N of:" soft list with check marks (mastered via API `IsSpellMastered`), required XP. Click outside / `T` closes it.
- **Input**: click a node -> select it (white frame, node card). Click the selected node again -> if available, `SetLearningTarget(formId)` and log the result; else nothing. `R` key (InputEvent sink, only while the magic menu is open and not in a text field) -> centre on the next learnable spell of the school (order: tier, name, FormID), select it. `T` closes the node card.
- **Preview gates (log only, ini `bSelfTest`):**
  - G1: on each selection change, log `Inventory3DManager` `loadedModels` count and each `spModel` name. PASS if a model appears within 30 frames of a spell selection.
  - G2: if G1 passes: project the model's `worldBound.center` with `UI3DSceneManager` camera (`NiCamera::WorldPtToScreenPt3`), log pixels vs preview-box centre; then move it (only when `bMovePreview=1`): set `itemPos`, `itemPosCopy`, `itemScale(Copy)` and, at rest, `spModel->local.translate += delta` + `Update` (the idea from Inventory Reforged `ItemView.cpp` `PlacePreview`; write our own code, GPL - do not copy). Log the distance after. PASS < 4 px. Restore the saved originals on menu close. Never move when `InventoryReforged.dll` is loaded (log it).
  - G3 (visual, user): black box drawn; does the effect show on top of it? Needs a screenshot.
- **Other logs:** thread id at `kDataLoaded` and in the first frame handler call; tree parse counts (schools, nodes, nodes without x/y, structural edges, lock edges, unresolved FormIDs); per redraw: centre, node count before/after cap.
- **ini** `Data/SKSE/Plugins/SL_MagicMenu.ini`: `[General] bEnabled=1 bSelfTest=1 bMovePreview=1`, `[Keys] iNext=19 (R) iClose=20 (T)` (DirectInput scan codes), `[Layout]` stage rects for the four boxes and `fTreeScale`.
- 2026-10-24 user layout change: node card has its own slot; no focus toggle. Layout: preview 649..957 x 67..329, tree 971..1240 x 67..329, progress card 649..957 x 336..420, SkyUI item card moved (its fade holder's `_x/_y/_xscale/_yscale` only, `bMoveItemCard=1`, re-applied when SkyUI moves it back, never with `InventoryReforged.dll`) into 649..957 x 428..636, node card always shown at 971..1240 x 336..636 (hint before a selection; "Learn this next" button for an available, not-target, not-mastered node; locked nodes show their requirements and a greyed "Locked"). Keys: `iNext=19` (R) selects the next learnable node and centres on it, `iSetTarget=34` (G) does what the button does; no `T`/`iClose`. A list click re-centres the tree and keeps the selected tree node while it is still in the window. `fTreeScale` is stage units per tree unit (TreeData's `View::scale`), default 0.35.
- 2026-10-24 user request: clicking a tree node also selects that spell in the SkyUI list when it is there: find the entry in `itemList.entryList` whose `formId` is the node's runtime FormID and set the list's public `selectedIndex` (SkyUI's setter calls `doSetSelectedIndex(i, SELECT_MOUSE)`, ignores an entry the current tab filters out, scrolls it into view and updates the item card; nothing is equipped). Not in the list: the list is left alone. The list change that follows re-centres on the same spell and keeps the clicked node selected (remembered for one change). One log line per tree click: FormID, in list yes/no, index, member used, whether it took.
- 2026-10-25 after the first game run (supersedes the lines above where they differ):
  - Layout = the user's mockup `v8_1_grid_fire_rune.png`, frame lines measured and scaled to 1280x720: preview 648..957 x 67..330, tree 971..1238 x 67..330, progress card 648..969 x 339..441, item card slot 648..969 x 450..623, node card 979..1238 x 339..623. `fPanelGap` (default 9 = progress top - preview bottom).
  - Inventory Reforged is off in the magic menu while this mod is installed: the mod ships `MCM/Settings/InventoryReforged.ini` with `[Panel] bMagicMenu = 0` (IR reads that file over its own ini; the inventory keeps IR). Per menu open, IR counts as active only when `_root.equipmentPanelScript` exists (read only); only then are the item card and the preview left in place.
  - Threads: the main thread is taken in `SKSEPluginLoad` (kDataLoaded ran on another thread; menu frame handlers run on several). The frame handler never calls HoM: a main-thread SKSE task reads HoM for the active and the centre's school into an immutable snapshot (on open, on school/centre change, after our SetLearningTarget, every 15 frames) and drawing uses that snapshot. G1/G2 and the preview move run in main-thread tasks too.
  - Tree: the centre and only the spells directly linked to it (1 step, both kinds of link), and of those only learned ones (mastered, known, target, ready) and learnable ones; locked spells are not drawn; edges only between drawn nodes; legend Mastered / Learning / Can learn. A label that would overlap one already drawn is dropped (the centre's and the selected node's are kept).
  - Category tabs (SkyUI `categoryList.selectedEntry.flag`, acted on at once): a school tab sets the school (the tree moves to it); Powers/Shouts hide the tree, progress and node cards, keep the preview, and move the item card up under the preview (`fPanelGap` below it, same edges and height; back under the progress card on other tabs, worked out again on the tab change); Active Effects hide all our panels; All/Favorites follow the selected spell. A selection not in the tree keeps the centre and shows "Not in the Heart of Magic tree".
  - Node card: one text field per requirement line ("✓ name - mastered", "✗ name - not mastered yet").
  - Note: clicking a row of the left list equips the spell; that is SkyUI's own behaviour. A click in our tree only selects.
- 2026-10-25 after the second game run:
  - Design rule R1 changes (M0_MAGIC_MENU_DESIGN.md section 6; that file is outside this worktree and was not edited): we DO take `MagicMenu::PostDisplay` (vfunc 6), for one thing only: after the original, `Inventory3DManager::Render()` once more, because the magic menu draws the spell's 3D art before its movie and our black preview box covered it (the method Inventory Reforged uses for its own magic menu panel). Only while our widget is on this menu's movie, Inventory Reforged is not active in this menu (`_root.equipmentPanelScript` check), the preview box is shown (not Active Effects) and `bDrawPreviewOver=1` (default). Chain rule: the hook always calls the original it replaced, so whichever of us and IR hooks later wraps the other; IR's own pass runs only with its `bMagicMenu=1`, and then ours is off, so the preview is never drawn twice.
  - Item card: the slot is in `_root` stage units; SkyUI places the fade holder in its parent (Menu_mc), which follows the visible rect. The slot's corners are mapped `_root` -> parent (localToGlobal/globalToLocal), the background measured in the holder's own space, and `_x/_y/_xscale` compared and re-applied in parent space; the transform is worked out again only when the card's content changes size (checked every 15 frames, in the holder's space) or the tab moves the slot. After each placement a `SelfTest: item card PASS/FAIL` line compares the drawn background (`getBounds(_root)`) with the slot (2 stage units).
  - 16:10 (game run 2): the visible rect is larger than 1280x720 (x 0..1280, y -40..760). Our panel rects stay in `_root` stage units, as now; they landed correctly.
  - Node card: a prerequisite whose state is not in the snapshot yet shows "... name - checking" (never a cross); one that resolves to no spell "? name - not in this game". A spell the player knows but HoM does not count mastered (HoM's early-learned tracking) is drawn as learning (blue arc), not gold, so the tree and the card agree.
  - School of a list spell not in the tree: the costliest effect's magic skill, else the first effect's that has one (a healing spell had kept "Destruction").
  - A tree click centres the tree on the clicked node (and, outside school tabs, takes its school); the list change it causes does not move the tree again.
  - Tree labels beside the nodes (right, or left of a node right of the centre), kept off other nodes and labels. Raising `fTreeScale` was measured instead: 0.6 would bring 1-step links of 60+ stage units from 29% to 50% but keep only 71% of them inside the window (86% at 0.35).

## 3. Out of scope (later)

SWF, layout editor, keyboard/gamepad tree navigation, translation files, Inventory Reforged coexistence beyond "do not move the preview", `handleInput` wrapping, `ClearTarget`/`Unlock`, config-spell polish, the H1 contract, following the item card.

## 4. Files

| File | What |
|---|---|
| `src/Main.cpp` | plugin load, log, ini, SKSE + SpellLearning listeners, sinks install |
| `src/Settings.h/.cpp` | ini read (GetPrivateProfile*), defaults |
| `src/Hom.h/.cpp` | API v2 pointer and thin wrappers (null-safe) - PROTOTYPE ONLY |
| `src/TreeData.h/.cpp` | spell_tree.json parse, FormID resolve, window query (pure, no GFx) |
| `src/Draw.h` | GFx drawing helpers (re-port of bookmark `MagicMenuDraw.h` + diamond, ring, dashed line, rect) |
| `src/Widget.h/.cpp` | menu sink, `mmx_root`, frame handler, the four boxes, clicks |
| `src/Preview.h/.cpp` | G1/G2 gates, move/restore |
| `src/Input.cpp` | R/T keys |
| `tools/magic-menu-tree-check.cpp` | offline: parse the real tree, run the window query for every node, report counts (max nodes before cap, rotation sanity) |

## 5. Checks

- Build: `cmake --build --preset=Release-2026 --target SL_MagicMenu`.
- Offline: the tree check tool on `D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_tree.json`.
- In game (user): enable the mod, open the magic menu, look; logs at `Documents/My Games/Skyrim Special Edition/SKSE/SL_MagicMenu.log` (path to confirm), read by Claude.
