# Magic menu: block layout (design, 2026-10-26)

Prototype SL_MagicMenu (this worktree, build 13 deployed as Case 221). Read `2026-10-26-magic-menu-proto-v13.md` and
`...-v11.md` (tree) for the current state; ledger `Desktop/Claude outputs/ACF-Phase0/M0/video7/LEDGER.md`.

## Intent

The user wants a "block interface": the panels our plugin draws beside SkyUI's magic menu list are blocks the player can
rearrange by dragging, and they snap into place by themselves. Later, add-on mods may add more blocks, so the layout
must grow without breaking (user 2026-10-26: "추후에 에드온을 설치하면 ... 창이 늘어날 수 있어 ... 오른쪽 왼쪽이 각각
스크롤되도록"). The user first described it as "창들을 하나의 블록으로 연결되는거"; mockups of joined panels were the
wrong reading and are dropped (user: "배열을 유저가 조정하고 자동으로 정렬되는 기능 ... 드래그로 위치조종").

## Decided by the user

- Blocks: each of our panels is one block - 3D preview, spell tree, learning progress, spell card (our item card), node
  card. The game's spell list (SkyUI, left half of the screen) is NOT a block and stays where it is ("그건 빼고").
- Gaps between blocks stay as now (fPanelGap) ("각블록 사이에는 지금처럼 여백이 있어야해").
- Two fixed columns ("2열 고정").
- A block takes the width of the column it is in (option 1 of the 2026-10-26 mockups, `M0/mockup/block/1안_*`); its
  height stays. In the layout mode the border between the two columns can be dragged left/right within limits ("각 열의
  가운대를 어느정도 좌우로 움직일 수 있게 하자, 배치모드에서"), and both columns' blocks follow the new widths.
- Move only, no resizing for now ("일단 자리만"): a block keeps its size; only its column and order change.
- Moving is possible only in a layout mode switched on separately ("따로 켯을때만"), so clicks on tree nodes never drag
  blocks.
- The layout mode is toggled by a lock / unlock button placed at the bottom right of the left block column; toggling it
  that way also works with a gamepad ("잠금/잠금해제 버튼을 왼쪽칸 우하단에 놓고 그걸 토글해서 배치모드를 켜고 끄면 패드도
  가능하겠네"). A key for it as well: the user agreed to "a lock button and also a key" ("그래").
- Each column scrolls on its own, and only the column under the mouse scrolls (user: "마우스가 올라간 열만
  스크롤하는거지").
- Add-on mods may add blocks.

## Calls made by Claude (the user may change them)

- Snapping: dropping a block puts it into the column under the pointer at the slot nearest the pointer; the blocks of
  each column stack from the top with the gap between them; the order is the only state. No free positions, no grid.
- Column border limits: each column at least 240 stage units wide (the tree's legend and the cards' text stay readable;
  the builder checks the cards' and tree's narrowest working width and raises it if needed); the two columns together
  keep today's total (648..1238). The border position is saved with the order; reset restores today's 321 / 260. With a
  gamepad, the border is moved with the bumpers (LB/RB) in the layout mode - to be checked against SkyUI's own use.
- Every block must redraw for a new width: the tree window (its viewport query uses the rect), our cards (text wrap),
  the progress card, and the 3D preview's fit and scissor box (they take the block's rect).
- Save: one layout for all characters, in a file of its own next to the ini (not the save game), written when the layout
  mode is switched off; a "처음 배치로" (reset) button beside the lock restores the default order.
- Cancel: leaving the layout mode with Esc/B restores the layout as it was when the mode was entered (Hud NG's
  "cancel restores the whole layout", the user's own installed HUD editor: `Hud NG/HUD-NG-README.txt` :79, :114).
- Gamepad in layout mode: D-pad picks a block (focus outline), A picks it up, D-pad moves it (up/down within the column,
  left/right to the other column), A drops it, B cancels the pick. Keyboard: arrow keys / Enter / Esc the same.
- Scroll (gamepad / keyboard): the column holding the focused block scrolls to keep it in view; the right stick scrolls
  the column under the cursor (to be checked against what SkyUI already uses the right stick for in this menu).
- The lock button itself is not a block. It and the reset button sit in a small row at the bottom of the left column
  (inside today's 67..623 area, right-aligned); the left column's scroll area ends above that row, so buttons and blocks
  never overlap. Check that the row clears SkyUI's bottom bar.
- Gamepad toggle: one gamepad button (ini key, default chosen after checking which buttons SkyUI already uses in this
  menu) toggles the layout mode, beside the keyboard key; with the gamepad the right stick scrolls the column of the
  focused block (there is no cursor). Input.cpp only listens today: the layout mode needs to consume the D-pad / A / B /
  arrow / Enter / Esc it uses while it is on (and nothing while it is off), so SkyUI's list and menu close do not react.
- Safety net only (not normal play): the mod ships `MCM/Settings/InventoryReforged.ini` with `[Panel] bMagicMenu = 0`,
  so Inventory Reforged's magic-menu panel is off and never shares this menu with ours (user 2026-10-26: our mod
  turns IR's magic-menu editing off). Only if a player turns IR's magic-menu panel back on in its MCM (irActive), or
  turns off our own fallback switches (bOwnCard=0, bMovePreview=0, bDrawPreviewOver=0 - debugging/rollback switches),
  the layout mode rests (button greyed, a log line) and the default layout is used; the saved layout is kept.
- A block whose add-on is not installed any more is skipped; its saved place is kept (Hud NG :130-136 behaves the same).
- Default order (= today's layout E2): left column preview, progress, spell card; right column tree, node card.
- Tabs that hide a block today (Powers/Shouts: no tree, no progress; Active Effects: card only - changed in build 16, see
  "Build 16" at the end: Active Effects now hides what Powers hides and keeps the preview) keep doing so; the
  other blocks of that column close up while it is hidden; the saved order is not changed by that.

## Goals

1. Rearrange the five blocks by mouse drag and by gamepad/keyboard in a layout mode, snapping into two columns.
2. The layout persists across menus and game sessions; reset and cancel exist.
3. Per-column scrolling when a column is taller than the screen area.
4. A block interface add-ons can use (register a block with an id, title, height, and a draw callback or a movie clip),
   designed now, opened to other mods later.

## Non-goals (for this step)

- Resizing single blocks (only the column border changes widths; heights never), hiding/showing blocks from the menu, presets, free positions, more than two columns.
- Moving or restyling SkyUI's spell list.
- The new information blocks in the appendix (each is its own later step).

## Anti-goals (a failure even if it "works")

- Dragging in normal use (outside the layout mode), or a click on a tree node that moves a block.
- Losing the user's layout (a bad file, an add-on removed, a version change).
- The 3D preview or its scissor box not following its block (the preview draws in the block's rect: today's
  `Settings::preview` drives the over-pass box; it must follow the block's current place and scroll offset, and be
  clipped to the column's visible area).
- Breaking build 10-13 behaviour: selection rule (hover ignored), dark-cloud alpha restore, Powers 3D hidden, tree look.

## Constraints

- Scaleform (AS2) from C++: drag via onPress/onRelease/onMouseMove handlers like today's node clips; masks for the
  column scroll areas; the menu's mouse wheel and gamepad input must not also scroll SkyUI's list while over our column.
- Panel rects are ini values today (`fPreview*`, `fTree*`, `fProgress*`, `fItemCard*`, `fNodeCard*`, `fPanelGap`,
  stage 1280x720); with blocks, the ini keeps each block's size, the order comes from the layout file.
- Per-column scroll precedent: Spell Research Prisma UI (`index.html:2160-2166`, installed) fixes column heights and
  scrolls each list, so a long column does not shake the whole window.
- Gamepad input goes through the existing key sink (Input.cpp) and must not swallow SkyUI's own buttons outside the
  layout mode.

## Left to the builder

The exact files (a Layout module beside Widget.cpp is expected), the file format of the saved layout (JSON is the
project's habit: nlohmann_json is linked), animation of the snap (none or a short ease), the focus-outline look, the
add-on registration API shape, self-tests (e.g. layout file round trip, snap slot choice as a pure function, scroll
clamp), the in-game log lines.

## Appendix: candidate blocks and improvements (research 2026-10-26, not part of this step)

Sources: local investigation of Heart of Magic's Prisma UI panel, HoM config, installed menu mods (TAKEALOOK); web
survey by Gemini (two mod ids it gave were wrong and are left out: 132575, 85055). Effort S/M/L; "I1" = needs the planned
HoM interface beyond SpellLearningAPI v2.

| Block / improvement | What it shows or does | Source | Effort |
|---|---|---|---|
| Requirement detail | required n/m, "m of n", master lock chip, "still needed / met" | HoM spellCardView.js:305-537, spellCardNeeds.js | S |
| Spells that follow | the next spells unlocked by this one | spellCardView.js:486-495 | S |
| Weakened power | "N% power", effect numbers scaled, step label | spellCardView.js:91-106, 243-287; config earlySpellLearning.powerSteps | M (I1) |
| XP sources | direct prerequisite 100%, same school 50%, any spell 10%, combat counter, global multiplier | progressionUI.js:89-110; config xp* | M (I1, multiplier via API) |
| New spells + next 3 targets | spells learned since the panel was closed; the next three targets | newSpells.js:1-43 | M |
| Spell tome state | XP % when reading, carried tome bonus (+25%), prerequisite needed | config spellTomeLearning, islTomeBonus | M (I1) |
| School bridges | links to spells of other schools by shared keywords (display only, user's earlier rule) | schoolBridges.js | S/M |
| Hand / voice slots | what is in each hand and the voice slot | Inventory Reforged bMagicMenu (installed) | L |
| Player stats drawer | magicka/stamina regen, spell absorb, shout cooldown | Inventory Reforged bHeroStats (installed) | M |
| Hotbar slot badge | which Spell Hotbar NG slot holds the spell | Spell Hotbar NG (installed; data access unverified) | M |
| Real numbers | final magnitude/cost with skill, perks, items, dual cast; cost per second for concentration | Gemini web survey (SkyUI 12604 comments, unverified wording) | M/L |
| Side-by-side compare | the selected spell vs the one in each hand | Compare Equipment NG 158874 (pattern; not installed) | M |
| Earlier plan items | circle gauge / 4-info node card / compare (F5), shout words (F6), time-left text (F7), in-menu settings (F9) | 마법메뉴_다음계획.md | S-M |

Layout-editor patterns worth borrowing (Hud NG 191835, installed): cancel restores the whole layout, reset one block,
controller mode (D-pad move), skipped-but-kept entries for removed mods. Not needed with two fixed snap columns: grids,
guides, resize handles.

## Changes after the GPT design review (2026-10-26)

The button row sits just below the left column (bottom right), not inside it, so the default layout never scrolls;
the spell card keeps growing to fill its column on tabs that hide other blocks (as today); the mouse wheel over our
columns scrolls them in normal mode too. Details: the plan's "Design review round 1" (D1-D11).

## Input boundary (Task 0)

Investigation 2026-10-26 (plan D1; report `.superpowers/sdd/2026-10-26-magic-menu-blocks/task-0-report.md`). SkyUI lines are
schlangster/skyui master; the installed SkyUI is 6.11 (community build, `mods/SkyUI/meta.ini`), whose sources were not located
(doodlum/skyui: 404), so lines may differ there. The design keys off member names (`handleInput`, `AttemptEquip`, `inventoryLists`,
`itemList`, `disableInput`, `disableSelection`, `bFadedIn`), and the Task 5 self-tests judge the result.

### What this menu already uses

| Input | Owner in this menu | Evidence |
|---|---|---|
| Tab / Shift-Tab, B, Start | close (CloseTweenMenu), Cancel, Journal | MagicMenu.as:90-93; controlmap Menu Mode :61, Gameplay :48 |
| Alt / pad BACK 271, "Quick Inventory" (I) | switch to the inventory | MagicMenu.as:94-97; SkyUI config.txt:5,8 |
| Space | search box | InventoryLists.as:282-285; config.txt:4 |
| D-pad / arrows / left stick, Enter / A | list move, category LEFT/RIGHT, press | ScrollingList.as:113-140; CategoryList.as:218-233; controlmap :60-66 |
| LB 274 / RB 275 | previous / next sort column | TabularList.as:62-82; config.txt:9-10 |
| LS click 272 | sort order; held 1 s opens the column dialog; an early keyUp is turned into a keyDown (sort) | InventoryLists.as:241-280, 431-443 |
| LT / RT, mouse buttons | equip (native -> `AttemptEquip`) | MagicMenu.as:111-115; controlmap Item Menus :82-83 |
| X / Y | Unlock / Favorite | MagicMenu.as:198-206; controlmap :86-87 |
| RS click (0x0080 = 273), C | Item Zoom | controlmap Item Menus :84 |
| right stick (0x000c) | Rotate the 3D model (Inventory3DManager) | controlmap :85; Inventory3DManager.h:53-55 |
| mouse wheel / press over the list, the 3D rect, outside the menu | list scroll, zoom, rotation start, close | ScrollingList.as:303-318; ItemMenu.as:117-121, 325-332, 400-404 |

controlmap = the installed `Legendary Map - CoMAP Support and Some Improvements/Interface/controls/pc/controlmap.txt` (wins over
`No Numpad Hotkeys and Custom Keybinds`, same lines). No single gamepad button is free in this menu. K (0x25 = 37) is not in the
controlmap and was free in the build-5 key scan (`v5-key-scan.md`, K: SexLab scene only). The user's own remap
`Root/ControlMap_Custom.txt` is binary and was not read.

### Chosen mechanism (three layers)

1. **Native filter (primary).** A vtable hook on `MenuControls::ProcessEvent(InputEvent* const*)`
   (`REL::Relocation<std::uintptr_t>{ RE::VTABLE_MenuControls[0] }.write_vfunc(1, ...)`; MenuControls.h:22-59, slot 1 on SE/AE/VR).
   MenuControls is the only menu-side `BSTEventSink<InputEvent*>` (the others are PlayerControls and BGSMoviePlayer); its handlers
   (ClickHandler, DirectionHandler, MenuOpenHandler, FavoritesHandler, Inventory3DManager, ...) sit below it. While the magic menu
   is open the hook always walks the list to keep a held set (every non-mouse `ButtonEvent` with `IsPressed()` enters it, `IsUp()`
   removes it - `IsPressed`, not `IsDown`, so a press begun before the menu opened counts as held). Fast path: mode off and nothing
   owned -> after that walk, call the original with the caller's pointer, list untouched (no unlink/restore). Otherwise: walk the list, unlink the consumed events into a
   local `filteredHead`, call the original once with `&filteredHead` (also when it is empty), restore every `next` link it changed
   (RAII, on every return path), return the original's result (never an extra kStop). Other sinks (PlayerControls, SKSE, our
   Input.cpp sink) still get the full list.
   - Consumed while the mode is on: every event whose device is not `kMouse` of the types kButton, kThumbstick, kChar and the VR
     touchpad types, except buttons whose user event is "Console" or "Screenshot" (UserEvents.h:114,116). Passed: kMouseMove, mouse
     buttons and wheel, kDeviceConnect.
   - Ownership: a press belongs to whoever got its down. When the hook consumes a down (any press in the mode, the toggle press in or
     out of the mode, the B/Esc press that leaves it), `(device, idCode)` is owned; its holds and up are consumed even after the mode
     is off or the menu has closed (the hook stays armed while anything is owned). An entry ends only on its up or on a fresh down of
     the same code (then judged anew) - never on a frame without events. The right stick is owned from its first consumed sample until
     a neutral sample (so a tilted stick at exit does not rotate the model).
   - The mode switch is decided inside the hook, synchronously (not deferred to the UI task), so later events of the same batch see
     the new state. UI work (focus, moves, scroll) is queued with `AddUITask` as today.
2. **AS2 wraps (second boundary + leak detector).** With the GetMember / CreateFunction / SetMember pattern of
   `SelectionChangeWrap` (Widget.cpp:1065-1118, 1229-1300):
   - `_root.Menu_mc.handleInput`: FocusHandler sends every Scaleform key to `path[0].handleInput` only (FocusHandler.as:71-108,
     110-128), and that is Menu_mc. Mode off: call SkyUI's original. Mode on: return true without calling it, count it, log the
     first per menu (`Layout: input leak code/value/nav`). Our own clip cannot take this role: `mmx_root` is a sibling of Menu_mc,
     never on the focus path.
   - `_root.Menu_mc.AttemptEquip`: mode on -> no-op. Needed because a native mouse click (passed for the drag) can still call it,
     and on the gamepad platform SkyUI skips its mouse-over-list check (ItemMenu.as:498-523).
3. **Mouse shield.** A transparent full-stage clip at the lowest depth inside `mmx_root` (which is above Menu_mc: Draw.h:38-49,
   Widget.cpp:5078), visible only in the mode, with an empty onPress, so `Mouse.getTopMostEntity()` is never SkyUI's: no row
   press/rollover, no list wheel, no 3D zoom or rotation start, no outside-the-menu close. Blocks and the lock/reset row lie above it.

Rejected (evidence in the report): our own AS2 handleInput (never called), handleInput wrap alone (misses AttemptEquip, the sticks,
MenuOpenHandler), `MagicMenu::ProcessMessage` (per-menu, but the sticks are handled inside MenuControls before any UI message, so
a second hook would be needed anyway), `PrependEventSink` + kStop (drops the whole batch, mouse included: BSTEvent.h:146-149),
hooking MenuEventHandlers (vfunc numbers shift per runtime: MenuEventHandler.h:33-55), toggling SkyUI's disableInput (SkyUI owns
those flags: InventoryLists.as:446-457, 513-526).

Open premise (확인 필요): that the Scaleform key input of this menu is produced below `MenuControls::ProcessEvent`. The headers only
point that way. If it is wrong, layer 2 still stops SkyUI; the route-parity self-test below decides it.

### Defaults

- Keyboard: `[Keys] iLayout = 37` (K). The hook compares the DIK code.
- Gamepad: `[Keys] iGamepadLayout = 273` (RS click, SKSE code), `-1` = none. The hook converts `ButtonEvent::GetIDCode()` (an XInput
  mask, BSWin32GamepadDevice.h:26-27) with `SKSE::InputMap::GamepadMaskToKeycode` (InputMap.cpp:84) before comparing. Cost: the
  gamepad Item Zoom is gone in this menu (mouse wheel and C still zoom). Chosen because every pad button is taken (table above) and
  the mode already owns the right stick; the user may prefer no default (open question). A refused RS-click toggle is passed
  through untouched, so the player sees an Item Zoom on a refusal (consistent with "nothing consumed when off").
- The lock button (mouse) toggles the mode as well. In the mode: D-pad/arrows, A/Enter, B/Esc, LB/RB, right stick per the spec.

### Entry refusals and exits

The mode refuses to start - the toggle press is then passed through untouched, the lock button stays as it was, and one line is
logged (`Layout: mode refused - <reason>`) - when: the native hook or either AS2 wrap is not installed (no hook = SkyUI cannot be
stopped; no handleInput wrap = no leak detector); a text field takes keys (`ControlMap` textEntryCount > 0: the search box);
`itemList.disableInput` or `disableSelection` is set, `bFadedIn` is false or `inventoryLists.currentState` is not SHOW_PANEL (column
dialog, mouse rotation, fade); any keyboard/gamepad button other than the toggle is held (tracked in the hook; this keeps the LS
sort timer and other half presses out of the mode); Inventory Reforged's panel or our fallback switches are on (spec rule above).
The mode ends as Cancel when the menu closes or another menu that pauses the game, uses menu context or is modal opens on top
(e.g. a confirmation box from a Papyrus key mod). Owned presses outlive both.

### When the mode is off

Nothing is consumed, with these exceptions: the toggle press itself (down, holds, up) when it is accepted; the tail (holds, up) of
a press consumed in the mode, such as the B/Esc that leaves it; the right stick until neutral after the mode; the mouse wheel over
our columns (D7, AS2 listener, not this hook). Input.cpp's U/G sink ignores a key while the mode is on or the key is owned.

### Right stick

Read in the hook (`ThumbstickEvent::IsRight()`, yValue) while the mode is on: consumed (so Inventory3DManager does not rotate the
model) and its value queued as the scroll of the focused block's column. The left stick is consumed as navigation.

### Known limits

Other input sinks are not filtered: SKSE's Papyrus key events and other plugins' sinks still see keys in the mode (for example
Spell Organizer's R, `v5-key-scan.md`); a menu they open ends the mode. VR devices are filtered by the device rule but not tested.

### Self-tests for Task 5 (log PASS/FAIL)

- `L5 input passthrough`: mode off, a list move reaches SkyUI (index changes).
- Route parity: mode off, keyboard/gamepad downs seen by the hook vs keyDowns seen by the handleInput wrap agree (decides the open premise).
- Leak: in the mode the handleInput wrap receives 0 calls, the list index, tab and category do not change, the menu stays open.
- Tail: the B/Esc that leaves the mode does not reach the wrap; a refused entry with LS held logs the refusal.
- Shield: in the mode a click where the list is gives no itemPress (ListPress sees nothing) and selectedIndex stays (alpha-0 hit test).
- Unverified until then: held buttons send an event every frame, DirectionHandler repeat stops when its held events are removed,
  late ups after Alt-Tab.

### GPT opinion (Codex, read-only, 2026-10-26)

Agreed with the MenuControls boundary and the unlink/restore filter over PrependEventSink. Taken: refuse entry while a button is
held (LS early keyUp becomes a sort, InventoryLists.as:252-263), keep ownership until a real up or fresh down and past menu close,
RS click is Item Zoom (controlmap :84; the draft had called it free), AttemptEquip wrap (no mouse check on the gamepad platform),
disableSelection in the entry check (mouse rotation, ItemMenu.as:400-404), filter all non-mouse devices incl. VR and CharEvent,
the U/G sink checks the mode, right stick owned until neutral, synchronous switch, `GamepadMaskToKeycode`, a local filtered head
with RAII restore and the original's return value. Narrowed: the fade-out wheel zoom branch (ItemMenu.as:327) needs
`bFadedIn == false`; in this menu that comes from `startMenuFade` (MagicMenu.as:221-227: Tab, BACK/Alt/"Quick Inventory",
exitMenuRect - all behind inputs layer 1 consumes or the shield blocks) and from the game's `ToggleMenuFade` callback
(ItemMenu.as:235-242; its native triggers are 확인 필요), and entry is refused while `!bFadedIn`, so it gets no separate guard. Not checked (outside the allowed sources): its SkyrimSoulsRE and skse64 links.

## Focus model (Task 5)

Built 2026-10-26 (report `.superpowers/sdd/2026-10-26-magic-menu-blocks/task-5-report.md`). The pure part is `Layout::Navigate`
(Layout.h, tested in layout-check); the widget only carries out its effect.

- A pick does not move the block on each key: the block stays in place with a lifted look, a target slot moves with the D-pad /
  arrows and the insertion line shows it; A / Enter applies one `Layout::Move` (one redraw, as a mouse drop), B / Esc drops the
  target and nothing moved. Up / Down step one slot in the target column; Left / Right go to the other column before its first
  visible block whose centre is below the current insertion line. Focus moves: Up / Down to the visible neighbour (no wrap),
  Left / Right to the other column's visible block nearest by centre y.
- Only visible blocks (ApplyVisibility's rules: our card once shown, the progress card with a school) take the focus or a cover;
  a focused block that stops being visible hands the focus to the nearest visible one in its column.
- Keys are fixed raw codes (not the controlmap): arrows / D-pad, Enter / numpad Enter / A, Esc / "Cancel" user event (B, Tab),
  `[` `]` / PageUp PageDown / LB RB (border, 10 units, ClampBorder). Down only, no hold repeat. The right stick's y scrolls the
  focused block's column (14 units a frame at full tilt). The left stick is kept and unused.
- Change from Task 4: B / Esc in the mode no longer flips the mode off inside the hook; it is queued in order with the other keys
  (so an A then B in one frame cancels the pick, not the mode). The rest of that batch is kept (the mode is still on), the
  frame handler leaves the mode. The toggle and another menu on top still switch synchronously.
- Entered with the gamepad button, the first visible block has the focus at once; with K or the lock button the first arrow
  gives it (a mouse user sees no outline).

## Build 16 (2026-10-26)

- Active Effects tab (category flag 256): the preview block shows, as on Powers/Shouts. It hides the tree, the progress
  card and the node card (`Layout::NoTreeTabHidden`, the same set as Powers); our card fills the rest of its column under
  the preview (cut to its content when drawn, as before on this tab). The over-pass draws the selected item's 3D in the
  box exactly as on the spell tabs (fit, scissor, alpha restore, Q3 hide of the game's own pass); an item with no art
  leaves the box empty. Replaces build 9's rule (no box on Active Effects, every model hidden by `HideForTab`), whose
  one frame of the game's own pass before the hide flashed each clicked item's model over the game view (ledger
  "Build 15 game run", user: "특성창에서 프리뷰창이 없고 이펙트가 특성선택할때마다 깜빡거려").
- `HideForTab` / `GiveBackTab` stay: they still serve the states without a box - nothing chosen, and the Powers tab with
  `[Preview] bHidePowers3D=1`.
- SelfTest L1's reference (`Build13Rect`) expects the preview's ini rect on Active Effects and our card at Powers' rect.
- Our card's C2 line pairs the arranged rect with the drawn bottom only once the card was drawn on the tab now shown
  (build 15 logged a tab change before the redraw: "648,306..969,157", Powers' top with Active Effects' cut bottom
  67 + 90). C2 now has a verdict: FAIL when the card's box is inverted, empty or outside its column
  (`Layout::CardBoxProblem`).

## UI polish (2026-10-29)

The menu's own drawing loses its leftover "template" look (plan: the session's polish list, part B). The Skyrim parts
stay: titles, the game font, the flat dark panels with their kPanelEdge rules.

- No blue accent (`kBlue` removed). In the layout mode what rests is `kTitleColor` (the focus outline, the drag ghost's
  edge) and what acts is the brighter `kWhite` (the picked block's edge, the insertion line, the column border guide).
  The `[Debug] bDebugFillerBlock` bars are `kTitleColor` too.
- The picked block's lifted look is its thicker edge and the 12% fill; the shadow band along its inner right and bottom
  edges is gone (`kLiftShadow`, `kLiftShadowAlpha` removed).
- Buttons (the layout row's lock / reset, the node card's button) are `kPanelFill` with a `kPanelEdge` border in every
  state; the node card's label (`kWhite` / `kDimText`) and the layout row's grey alpha tell the states apart
  (`kButtonFill`, `kButtonOff`, `kLockedEdge` removed).
- The progress card's bar is a 2-unit line (`kBarH` 9 -> 2): `kBarBack` 0x4A4A4A (neutral grey; `kPanelEdge` read as
  the card's rule lines at 0%) behind, `kTitleColor` for the share learned. The bar row keeps the XP field's height, so the card's plan does not move.
- The spell rows of the requirement lines (mastered / not mastered) are `kLabelColor`; only the drawn check (`kOk`) and
  cross (`kNotYet`) carry the state colour.
- The tree legend starts where the window title's letters start (`kPad + kGutter / 2`), and its spare room goes only
  between the items. Its fit loop shrinks the text to `kMinFitSize` before cutting (it compared with `kLegendSize`
  and never shrank).

## Block API v2 (2026-10-29)

From the Spell Research add-on design (`2026-10-27-spell-research-addon-design.md`, "Changes / 1"); the contract is the
header comment of `plugins/SL_MagicMenu/src/BlockApi.h`, the pure rules `BlockRules.h`, checked offline by
`tools/magic-menu-block-api-check`.

- v1 is frozen as `BlockApi::RegistrationV1` (48 bytes); a v1 add-on is read and answered as before (`Result.apiVersion`
  echoes the version a registration was read as). v2's `Registration` keeps v1's fields in place and adds
  `drawSelection`, `click` and `tabs`; only the fields `min(structSize, dataLen)` covers whole are read, a v1 sender's
  bytes past its 48 never. Version 3+ and unknown `tabs` bits are refused with a reason.
- `drawSelection` gets the form the card shows (tree card spell, else the committed list selection, else 0); the frame
  handler compares it per block after its own selection and tree-click handling and draws the block again when it
  changed (DrawAddons runs twice a frame: after the layout mode, after the selection watch).
- `kMessageRedraw` ('SLMD', data = final id, up to 255 + 1 + 128 characters: an SKSE plugin name, '.', an id): queued from
  any thread (an id already queued is not queued again, at most 64 ids wait), drawn on the next frame, dropped while the
  menu is closed, an unknown id logged once. A drawSelection block its tabs mask hides is not redrawn for a new form until
  a tab shows it again.
- `click`: a transparent hit clip (`mmxHit`) over the content clip of each v2 block with `click`, made again after each
  draw; its onPress / onRelease / onReleaseOutside only record, the release is judged at once by `BlockRules::Judge`
  (cancelled when the release is outside the content area or the column's viewport, or the column scrolled, the block was
  drawn again or moved, or the layout mode changed between; never in the layout mode) and the call is made on the next
  frame, before that frame's redraws, with both points in content clip coordinates.
- `tabs`: the SkyUI category flag's bit (`BlockRules::TabBit`: Powers 128, Shouts 64, Active Effects 256 their own; All,
  Favorites, the school tabs and unknown categories are spell tabs). An add-on its mask leaves out joins the tab's hidden
  set (Arrange closes up, as for the built-in tree blocks) and its panel is hidden (ApplyVisibility).

## Block API v3 (2026-10-30)

From the HoM add-on design (`2026-10-30-magic-menu-hom-addon-design.md`): the Heart of Magic blocks (the tree window,
the learning progress card, the node card) left the core for the add-on `plugins/SL_MagicMenu_HoM`, and Block API v3
gives an add-on what they need - a per-frame call with a Context, its own mouse handlers, keys, a pad, a legacy id
that takes over a retired built-in block's saved place, and the 'SLMS' (show a spell) / 'SLMV' (hide or show the
block) messages. v1 and v2 add-ons are read as before. The contract is the header comment of
`plugins/SL_MagicMenu/src/BlockApi.h` (pure rules `BlockRules.h`, `BlockRulesV3.h`), checked offline by
`tools/magic-menu-block-api-check`; the changes made while building it are in the HoM add-on design's "As implemented".

## Blocks fit their content, the gear button (user 2026-10-30)

- Every built-in block but the 3D preview box fits its content: the progress card, the node card, our spell card and the
  tree window. One per-block height map (`g_menu.fitHeight`) holds both an add-on's 'SLMH' height and a built-in block's
  fit; `BlockRules::ApplyFits` puts it into Arrange's heights (and takes a fitted block out of the fill set, so our card
  no longer grows to the column's bottom). A block's full height (`FullHeight`: the ini rect, our card's fill, an add-on's
  registered height, arranged with no fit: `g_menu.blocksMax`) is its maximum and its size in the layout mode.
- Rule that keeps it loop-free: every room is worked out from the block's full height; only the bottom-anchored parts
  (the node card's bottom rule / XP / button, our card's footer) are placed at the fitted height. `FitBlock` stores
  `min(needed + kFitSlack, full)` and lays the columns out at once (the blocks below move up in the same frame); drawing
  at the fitted height asks for the same height. Pure parts: `LayoutMath::RowsFit / RowsHeight / LinesFit /
  ProgressNeeded / NodeCardNeeded / TreeTrim` (preview-check), `BlockRules::FittedHeight / ApplyFits` (layout-check).
  Log: "SelfTest: L7 fit <id> - H high (content needs N, full F, was W)" and Rearrange's "block X is W x H now".
- Needed heights: progress card - the last line's bottom + kCardBottom (a message counts the lines its text took); node
  card - hint only: kCardTop + the hint's lines + kPad; with a spell: its rows + kRuleGap + the bottom part (74); our card -
  the description's shown lines (FieldH, not textHeight) + the footer reserve; tree window - the query and layout at the
  full height, then the space the drawn boxes (marks, links, labels, badges, notes) leave above and below the body is
  taken off, and the drawing moves up by the part above (the marks keep their size and their places to each other; the
  tooltip reads the moved origin, `g_menu.treeOrigin`). Moving it up was chosen over keeping the centre point: keeping it
  could only take off the smaller of the two empty bands, on both sides. A message state (no tree, no centre, a hidden
  school) is the title strip, the lines and padding, with the legend strip hidden.
- The row under the left column has no button outside the layout mode (`BlockRules::RowButtons`); lock and reset in it as
  before. A gear (`layoutGear`, 18 units, kTitleColor, kWhite on hover) in mmx_root above the shield toggles the mode
  through the lock button's path (the K path); it shows whenever the menu is open, greyed like the lock in the safety-net
  states. Placed by `BlockRules::GearRect` right-aligned 4 under the first of `itemList.scrollbar`, `.scrollBar`,
  `.background`, `itemList` that has bounds in _root (SkyUI's member names not checked offline; the log names the one
  used), after 60 frames without one at the fallback share of the visible stage (580/1299, 740/876, from the user's
  16:10 capture). K with nothing chosen still enters the mode (no refusal bit for it; the mode shows every block).
- Review round 1 (2026-10-30): the gear is shape "B" (`BlockRules::GearOutline`: 6 flat-topped teeth tapering to a 0.72
  root circle, flat valleys, a 0.30 hole; both outlines closed on their first point; line 1.3 with round caps and joins,
  the SR block's lineStyle) and follows its anchor until it reads the same for 10 frames (SkyUI's list may slide in). A
  first fit equal to the full height is recorded too (it takes our card out of the fill); a stored fit is capped by the
  tab's full height at each Rearrange; the SkyUI-card fallback drops our card's fit; FitBlock's Rearrange leaves only the
  fitted block's dirty mark off. The tree's "no links" note moves right under the drawing before the fit; the tree
  tooltip is a clip of the tree's column (above the block below a short tree), clamped to the tree's full height. The
  preview's place goes to Preview after the frame's fits, and our card is drawn again in the frame the layout mode
  changes. Row 2's baseline alignment (spec v6 L2) is off whenever the node card sits higher than the progress card,
  which a fitted tree causes: by design, said in its log line.
- Later the same day (user: "트리창도 자동세로 줄이기 기능의 대상에서 빼자"): the tree window no longer fits its content. Like the
  3D preview box it keeps its ini height in every state: its drawing is centred in its body, the legend strip sits at its
  bottom, a message state is the centred two-line message, and the "no links" note sits at the body's bottom as before the
  fitting work (that place was the original design; review R1 only moved it for the trim). `LayoutMath::TreeTrim`, the
  message-state shrink and the legend hiding were removed. Kept: the tooltip as a clip of the tree's column, clamped to the
  column's visible part and drawn again when the tree moves (a card or an add-on above it still moves it, a scroll still
  hides part of it). The fitted blocks are the progress card, the node card, our spell card and the add-ons ('SLMH').
- The gear's anchor (second game run: under the scrollbar's end was too high; Inventory Reforged sizes SkyUI's window).
  x = the list window's right edge minus 2, from the first of `itemList.background`, `inventoryLists.panelContainer`,
  `itemList.scrollbar`, `itemList.scrollBar`. y = centred on the bottom bar's skill / XP row, from the first of
  `bottomBar.playerInfoCard`, `bottomBar.playerInfoCard.SkillTextInstance`, `bottomBar`; else 4 under the scrollbar or list;
  else the fixed share of the visible stage (`BlockRules::GearRectAt`, `GearRect`). The names come from Inventory Reforged's
  panel.swf strings and are not checked against SkyUI's movie offline. At the first settled placing of a game session the
  log lists `_root.Menu_mc`'s named clips to depth 3 with their stage bounds (80 lines at most), and the gear's log line
  names the clips its x and y came from.
- Gear hint and mode look (user 2026-10-30): on hover a one-line hint in the tooltip look ("배치 모드 [K]", in the mode
  "배치 모드 끝내기 [K]", the toggle key as the host names it) on the side of the gear that stays on screen
  (`BlockRules::GearHintX`). The gear stays lit (kWhite) for the whole layout mode. `UpdateGear` checks every frame, so any
  way the mode changes is followed.
- Less jumping: a fitted block grows at once and shrinks only after the same smaller height has been wanted for
  `BlockRules::kShrinkDelayMs` (150 ms, steady clock; `DecideFit`). A grow cancels a waiting shrink. While a shrink waits,
  the block draws at its present height. When it is due, the block is drawn again (cards: the redraw pass or our card's
  redraw, one Rearrange), and an add-on's held 'SLMH' request is taken again by TakeAddonHeights. Not in the layout mode,
  and not for a menu's first fit, which applies at once as before.
- Key-hint strip and gear under the columns (user 2026-10-30, sketch on the layout map). SkyUI's bottom-bar key hints move
  to one row under the board (see the board note below; it first hung under the lower column, which moved with fitted
  cards) (`BlockRules::HintRowRects`).
- The strip is one clip (redesigned 2026-10-30 from SkyUI's source, replacing the earlier search for several clips):
  `_root.Menu_mc.bottomBar.buttonPanel`. In SkyUI the bottom bar has two children, `playerInfoCard` (the skill / XP row)
  and `buttonPanel` (BottomBar.as:26,31). The panel's own `_x` is set only in `positionElements` (BottomBar.as:49-53,
  called from ItemMenu.as:444), and its buttons are laid out inside it (ButtonPanel.as:42,76,120,123; unused ones hidden
  with `_visible`). STB Hotkey System adds its "Assign hotkey" / "Add to group" hints as buttons of that same panel, so
  they move with it. STB's keycaps in the list rows are list icons, not hints, and are never touched. The skill / XP row
  stays where SkyUI puts it.
- The rule, each frame (`BlockRules::PlacePanel`): the panel's visible content (the union of its visible children,
  `BlockRules::VisibleUnion`), mapped to the host's stage through every parent (Inventory Reforged scales the menu 0.85),
  goes with its top-left to the strip's place (`BlockRules::HintRowRects`). The origin that does this is the panel's
  present origin on the stage moved by that step, taken back into the panel's parent (`MapPoint`), and it is set only
  when it differs from the present one by more than 0.5 (parent space; Flash keeps twips). The panel off the origin last
  known in place means SkyUI placed it again; SkyUI writes only its `_x` (BottomBar.as:51; nothing writes its `_y`), and
  the move log says so with that `_x`.
- Review of 4d50080a: with no button shown (`hideButtons` at ItemMenu.as:115, `clearButtons` at MagicMenu.as:195) the
  panel is not moved, since its own bounds would count the hidden buttons. The gear keeps the row of the strip's last
  visible size this menu, so it does not jump while the buttons are cleared. The visible content is read again only
  when its signature changes: the panel's stage origin, and each child's `_visible` and, when shown, `_x`, `_y`,
  `_width`, `_height`.
- GPT on 4846c6a5: a shown button whose bounds do not read is not "nothing shown". That frame nothing moves and nothing
  is cached, so the next frame reads again (`BlockRules::AllShownRead`); no part-union is used. The frames this happened
  on are counted at the menu's close.
- The panel is only moved, never drawn again. With no panel SkyUI's strip is left alone; a menu that never had one says
  so at its close.
- The gear goes one row under the strip, with its left edge on the strip's start, so both sit on one vertical line. The
  user's words: "단축키 시작이랑 톱니바퀴랑 Y축이 같게", then "톱니바퀴가 한칸 아래로" (an earlier same-row reading was wrong).
  The gear is 4 below the strip and the strip starts at the left column's left edge. Strip and gear move up together from
  the stage's bottom, and a too-wide strip moves left with the gear. Nothing else is drawn right of the gear on its row,
  because the gear's flyout will open there. The gear no longer anchors to SkyUI's list. The one-time clip dump now runs at a
  menu's 30th frame.
- Gear and reset (user 2026-10-30: "그냥 톱니바퀴 누르면 편집모드/보기모드로 전환되는거로 하자 ... 처음배치 스위치만"):
  - A gear click toggles the layout mode (view <-> edit), the same path as the K key. Leaving saves, as the old lock did.
  - The control row under the columns is gone.
  - In the edit mode one button, "처음 배치로" (the old reset, one-shot), unfolds out of the gear to its right on the gear's
    row. It is a box in the mock's look; its mask's width is eased over `kResetOpenMs` (180 ms, ease-out), and the text
    never scales. Leaving the mode folds it back over `kResetCloseMs` (140 ms).
  - The button takes a click only once fully out (`BlockRules::ResetClickable`). It is greyed in the safety-net states.
  - The gear is lit for the whole edit mode. Its hover hint reads "편집 모드 {key}" in view mode and "보기 모드로 {key}" in
    edit mode, and is hidden while the reset button shows.
- The board (user 2026-10-30, mock board.png, "이대로 만들기", fixed size): a panel behind both columns. It is drawn once
  per menu, always up, the layout mode included. Its rect is the two columns' full areas (the frames, so the border does not
  matter and fitted cards do not change it) `BlockRules::kBoardPad` (16) larger, cut to the visible stage
  (`BlockRules::BoardRect`). Look: black at 38%, a kPanelEdge edge at 75%, no title. It does not scroll. The key-hint strip
  hangs 8 under its bottom, and the gear one row below at the strip's start (the left column's left edge), so neither
  moves when cards fit.
- With nothing chosen the columns stay up (before, they were hidden). Each block shows its empty look
  (`BlockRules::EmptyLookOf`):
  - the 3D preview and the tree window keep their size, with one dim centred "주문을 고르면 표시됩니다" (no 3D is drawn);
  - our spell card collapses to that one line;
  - the progress card and the node card show what they can (school progress, the hint line);
  - an add-on drawn per selection (drawSelection) with nothing shown is not drawn and closed up;
  - a v1 block shows as before.
- Board review round 1:
  - The preview's empty line sits in a clip of its own, because removeMovieClip does not remove a text field.
  - The strip's size comes from its visible children only.
  - Each move is read back as a "SelfTest: L8 strip … PASS/FAIL" line, and the moves are counted at the menu's close.
  - In the layout mode the add-ons drawn per selection are not closed up (they can be moved), and entering or leaving the
    mode lays the columns out again when that matters.
  - The board is an edge only (no fill) when SkyUI's card or the game's own 3D pass would sit under it.
  - With nothing chosen the set-target key / button do nothing; the tree has no legend and no not-in-tree note.
  - In the edit mode the gear's hint shows on its left.
- GPT re-review of 0420eed9: whether SkyUI placed the strip again is read from the panel's own origin (its _x/_y in its
  parent, what the host sets), not from its visible children. A button hidden inside it moves the children but is not a
  new layout. (The per-group homes and the STB / Keycap clip search of round 1 went with the one-clip redesign above.)
