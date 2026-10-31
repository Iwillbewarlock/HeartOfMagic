# Magic menu block layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The plugin's five panels become blocks the player rearranges (drag, gamepad/keyboard) in two snapping columns with a movable border, per-column scrolling and a saved layout, in a layout mode toggled by a lock button or key.

**Architecture:** A new game-free `Layout` module owns the layout state (column order, border x, scroll offsets) and computes every block's rect; `Widget.cpp` stops reading panel rects from `Settings` and asks `Layout` instead, so drawing, the tree window, the cards and the 3D preview box all follow the blocks. A `LayoutMode` part of the widget handles the lock/reset buttons, dragging, the border handle, input in the mode, and saving. Add-on blocks register through a small internal registry first; the SKSE-message API for other mods is the last task.

**Tech Stack:** C++23, CommonLibSSE-NG, Scaleform GFx from C++ (AS2 handlers via `GFxFunctionHandler`, as `NodeClick` etc. today), nlohmann_json (already linked), CMake + MSVC, offline check tools in `tools/`.

**Spec:** `docs/superpowers/specs/2026-10-26-magic-menu-blocks-design.md` (read it first; also `...-proto-v13.md` for the current state).

## Global Constraints

- Two fixed columns; blocks never resize by themselves except their width following their column; heights from the ini.
- Gap between blocks = `Settings::panelGap` (9). Default layout = today's E2 exactly: left 648..969 (preview, progress, spell card), right 978..1238 (tree, node card), rows from 67.
- Column border movable only in the layout mode; each column >= 240 stage units (raise if a block's narrowest working width is larger); total span 648..1238 unchanged.
- Layout mode only via the lock button (bottom row of the left column, right-aligned, with "처음 배치로" beside it) or a key / gamepad button (ini). Outside the mode no drag ever starts and no input is consumed.
- Scroll: only the column under the mouse (gamepad: the focused block's column).
- Save: one JSON file for all characters next to the ini (`Data/SKSE/Plugins/SL_MagicMenu_Layout.json`), written when the mode is left with confirm; Esc/B restores the layout from when the mode was entered.
- SkyUI's spell list is not touched. Inventory Reforged's magic-menu panel is off by our shipped ini; irActive / bOwnCard=0 / bMovePreview=0 / bDrawPreviewOver=0 are a safety net only: mode rests, default layout used, saved file kept.
- Must keep builds 10-13 behaviour: hover ignored / click selects, alpha restore, Powers 3D hidden, HoM tree look.
- Korean UI strings go through the existing `Str` table (Widget.cpp) and get the Argon final review before release.

## Review Focus

1. A broken, old or hand-edited layout file (bad JSON, unknown ids, a block missing, border out of range) -> default for what is broken, never a crash or a lost block; tested in Task 1.
2. A column taller than its area while a block is dragged or the border is moved -> scroll offsets clamp, the dragged block stays under the pointer; tested in Tasks 1 and 4.
3. The 3D preview block scrolled partly or fully out of its column -> the box is clipped to the visible part, nothing drawn when fully out, alpha restore still on the drawn part; tested in Task 3.
4. A tab that hides blocks (Powers/Shouts: tree, progress; Active Effects: card only) during the layout mode -> hidden blocks close up, cannot be dragged, saved order unchanged; tested in Tasks 1 and 4.
5. Leaving the menu with the mode on (Tab/close, menu closed by the game) -> treated as cancel, nothing saved, input released; tested in Task 4 (log self-test).

---

### Task 1: Layout core (pure)

**Files:**
- Create: `plugins/SL_MagicMenu/src/Layout.h`, `plugins/SL_MagicMenu/src/Layout.cpp`
- Create: `tools/magic-menu-layout-check.cpp`
- Modify: `tools/CMakeLists.txt` (new target like `magic-menu-tree-check`), `plugins/SL_MagicMenu/CMakeLists.txt` (add Layout.cpp)

**Interfaces:**
- Produces:
  - `namespace Layout { using BlockId = std::string; inline constexpr const char* kPreview = "preview", kTree = "tree", kProgress = "progress", kSpellCard = "spellcard", kNodeCard = "nodecard"; }`
  - `struct State { std::array<std::vector<BlockId>, 2> columns; double border; };` (border = the left column's right edge; the right column starts at border + gap)
  - `State Default(const Settings::Values&)` -> columns {preview, progress, spellcard} / {tree, nodecard}, border 969
  - `struct Frame { double left, right, top, bottom, gap, minColumn; };` (left 648, right 1238, top 67, bottom = 623 minus the button row for column 0)
  - `std::map<BlockId, Settings::Rect> Arrange(const State&, const std::map<BlockId, double>& heights, const std::set<BlockId>& hidden, const std::array<Frame,2>&, const std::array<double,2>& scroll)` -> block rects in stage units (scrolled), hidden blocks absent
  - `std::array<double,2> ContentHeight(...)` and `double ClampScroll(double offset, double content, double visible)`
  - `std::pair<int,int> SnapSlot(const State&, const BlockId& dragged, double x, double y, const std::map<BlockId, Settings::Rect>& arranged)` -> (column, index) by pointer x vs border and y vs the arranged blocks' centres
  - `State Move(State, const BlockId&, int column, int index)`; `double ClampBorder(double x, const Frame& left, const Frame& right)`
  - `std::optional<State> FromJson(std::string_view, const std::set<BlockId>& known, std::string* why)`; `std::string ToJson(const State&)`; `State Repair(State, const std::set<BlockId>& known, const State& fallback)` (drops duplicates, appends missing known blocks to their default column, keeps unknown ids in place for absent add-ons)

- [ ] **Step 1: Write the failing tests** in `tools/magic-menu-layout-check.cpp` (Expect/Text helpers as in magic-menu-preview-check):
  - `Arrange(Default, iniHeights, {}, frames, {0,0})` gives exactly preview 648,67,969,297; progress 648,306,969,408; spellcard 648,417,969,623; tree 978,67,1238,297; nodecard 978,306,1238,623.
  - border 900 -> left blocks right edge 900, right blocks 909..1238; `ClampBorder(500)` -> 648+240; `ClampBorder(1200)` -> 1238-9-240.
  - hidden {tree, progress} -> nodecard top 67; spellcard top 306.
  - `SnapSlot` of preview dropped at x 1100, y between tree and nodecard -> (1, 1); dropped onto its own place -> unchanged.
  - `ClampScroll(-50, 400, 300)` -> 0; `ClampScroll(500, 400, 300)` -> 100; content <= visible -> 0.
  - JSON: round trip; `{"columns":[["tree"],["preview"]],"border":2000}` -> repaired: missing blocks appended, border clamped; `"not json"` -> nullopt with a reason; unknown id `"addon.x"` kept; duplicate id dropped.
- [ ] **Step 2: Run** `cmake --build build --config Release --target magic-menu-layout-check` then `build/tools/Release/magic-menu-layout-check.exe`; expect a build failure (no Layout.h).
- [ ] **Step 3: Implement the interfaces in Layout.h/.cpp.** No game headers; Settings.h only for `Rect`/`Values`. `SnapSlot`: column = x < border + gap/2 ? 0 : 1; index = number of blocks in that column (other than the dragged one) whose arranged centre y is above y.
- [ ] **Step 4: Run** the check: all PASS; also `magic-menu-tree-check --selftest` still PASS.
- [ ] **Step 5:** no commit (the worktree is kept uncommitted until the user says so); note the results in the ledger.

### Task 2: Widget draws from Layout (no behaviour change at the default layout)

**Files:**
- Modify: `plugins/SL_MagicMenu/src/Widget.cpp` (every `settings.preview/tree/progress/itemCard/nodeCard` and `Settings::Get().<rect>` read - 31 places today, e.g. :1451-1453 card slot on Powers, :3720/:3751/:3836 tree, :3958 progress, :3967/:4291 node card, :4141 tree tooltip, :4832 card baseline test, :4936 `info.box`, :5111-5121 panel creation), `Settings.cpp` (load the layout file at plugin load), `dist/SL_MagicMenu.ini` (comment: rects are default sizes/places; the layout file overrides the order and border)

**Interfaces:**
- Consumes: Task 1 (`Layout::State`, `Arrange`, `Default`, `FromJson`, `Repair`).
- Produces: `Settings::Rect BlockRect(const Layout::BlockId&)` (current arranged rect, scrolled) and `bool BlockShown(const Layout::BlockId&)` in Widget.cpp; `g_layout` (State) loaded once from the file (fallback Default with one log line); `RedrawAll()` that re-creates panels and redraws tree/cards/progress for new rects.

- [ ] **Step 1: Self-test first:** add a `L1 layout` SelfTest line at menu open: logs the arranged rects and `PASS` when the default layout equals the ini rects (FAIL lists the differences).
- [ ] **Step 2:** replace each rect read with `BlockRect(...)`; the Powers/Shouts card slot rule (:1451) becomes "hidden blocks close up" via `Arrange(hidden)`; the tree's `TreeContent`, legend and tooltip use the tree block's rect; `info.box = PanelContent(BlockRect(kPreview))`.
- [ ] **Step 3:** width changes: tree window, node/spell/progress cards and the item-card fit (`cardFit`) re-run when a block's width changed since the last draw (compare against the last drawn rect per block).
- [ ] **Step 4: Build + checks:** 0 warnings; preview-check, tree-check (selftest + real tree), homstyle-check, layout-check all PASS. In-game expectation (log): `L1 layout PASS`, everything else as build 13.

### Task 3: Column scroll and the preview box clip

**Files:**
- Modify: `Widget.cpp` (two column clips with a mask each - block panels are children of their column clip; mouse wheel over a column), `Preview.cpp` (box intersect with the column's visible rect; skip the over-pass and the alpha restore save when empty), `Layout.cpp` (no change if Task 1 covers ClampScroll)

**Interfaces:**
- Consumes: `Layout::ClampScroll`, `ContentHeight`, `BlockRect`.
- Produces: `Settings::Rect ColumnViewport(int column)`; `FrameInfo` (Preview.h) gains `std::optional<Settings::Rect> visible` = the preview column's viewport; `Preview` draws in `box ∩ visible` and nothing when empty.

- [ ] **Step 1: Pure test** in layout-check: `VisibleBox(box, viewport)` -> intersection; fully out -> nullopt.
- [ ] **Step 2:** column masks + wheel: the wheel scrolls only the column under the mouse (hit test against `ColumnViewport`); the event is consumed only there (SkyUI's list keeps its wheel elsewhere) - find how the menu delivers the wheel (GFx `onMouseWheel` on our clip, or the input sink) and use the one that can stop SkyUI's list scrolling when over our column.
- [ ] **Step 3:** Preview: `info.box` clipped to `visible`; SelfTest `L2 preview clip PASS` when the scissor box lies inside the viewport.
- [ ] **Step 4: Build + checks** as Task 2. In game: with the default layout nothing scrolls (content fits); the scroll is exercised once add-on blocks or a moved layout overflow a column (Task 6 adds a debug filler block behind an ini switch to test it).

### Task 4: Layout mode (mouse)

**Files:**
- Modify: `Widget.cpp` (button row, mode state, drag, border handle, save/cancel), `Settings.*` + ini (`[Keys] keyLayout`, default chosen after a key scan like v5-key-scan.md), Str table (button labels/tooltips: "배치 바꾸기", "배치 잠금", "처음 배치로")

**Interfaces:**
- Consumes: Tasks 1-3.
- Produces: `bool g_layoutMode`; `EnterLayoutMode()`, `LeaveLayoutMode(bool save)`; `SaveLayout()` -> writes `SL_MagicMenu_Layout.json` (write to temp + rename).

- [ ] **Step 1: Pure tests** (layout-check): `Move` + `Repair` keep every known block exactly once; cancel restores the entry State bit-for-bit.
- [ ] **Step 2:** button row (lock / reset) at the bottom of the left column; lock toggles the mode; key toggles it too; reset sets the working State to `Default` (saved only when the mode is left with the lock); the safety-net states grey the lock and log once.
- [ ] **Step 3:** in the mode: tree node clicks and the tooltip are off; each block gets a drag handler (onPress on the panel, follow `_xmouse/_ymouse`, live preview of the snap slot as a thin insertion line, onRelease -> `Move`); the border handle (a thin vertical strip between the columns) drags with `ClampBorder`; blocks redraw on release (not every frame).
- [ ] **Step 4:** leaving: lock again -> save; Esc, menu close or tab change -> cancel (restore entry State). SelfTests: `L3 layout saved PASS` (file re-read equals State), `L4 cancel PASS`.
- [ ] **Step 5: Build + checks** as before.

### Task 5: Layout mode (gamepad / keyboard)

**Files:**
- Modify: `Input.cpp` (today a listening key sink - see how it is registered), `Widget.cpp`, Settings/ini (`[Keys] gamepadLayout`)

**Interfaces:**
- Consumes: Task 4's mode functions.
- Produces: focus model `g_layoutFocus` (BlockId), pick/move/drop per the spec (D-pad / arrows pick and move, A / Enter pick and drop, B / Esc cancel the pick or leave with cancel, LB/RB move the border by a step of 10, right stick scrolls the focused column).

- [ ] **Step 1: Investigation step (write findings into the spec):** which gamepad buttons SkyUI's magic menu already uses (SkyUI sources in the session scratchpad `skyui/`, ItemMenu/MagicMenu/InventoryLists handleInput), and how to consume input while the mode is on (options to evaluate: our clip's AS2 `handleInput` placed before SkyUI's, a `MenuControls` / `BSInputDeviceManager` hook, or swallowing via the selection wraps); pick the least invasive that can stop SkyUI's list and the menu close. Get a GPT diagnosis on the choice (rules: technical second opinion).
- [ ] **Step 2: Pure test:** the focus/move state machine (pick, move up/down/left/right, drop, cancel) as a function of events.
- [ ] **Step 3:** implement; outside the mode nothing is consumed (SelfTest `L5 input passthrough PASS`: a list move outside the mode still reaches SkyUI).
- [ ] **Step 4: Build + checks** as before.

### Task 6: Add-on blocks

**Files:**
- Create: `plugins/SL_MagicMenu/src/BlockApi.h` (public header for other plugins)
- Modify: `Main.cpp` (SKSE messaging listener), `Widget.cpp` (registry: id, title, height, draw callback into the block's GFx clip), `Layout` (unknown ids already kept)

**Interfaces:**
- Produces: `struct BlockApi::Registration { const char* id; const char* title; float height; void (*draw)(RE::GFxMovieView*, RE::GFxValue& clip, float width, float height, void* user); void* user; };` sent with an SKSE message type `'SLMB'` version 1; blocks appended to the right column by default; ini `bDebugFillerBlock=0` registers a tall filler block to test scrolling.

- [ ] **Step 1: Pure test:** registry rejects duplicate / empty ids, keeps order of registration; layout file keeps a missing add-on's place.
- [ ] **Step 2:** implement; SelfTest `L6 addon <id> drawn`.
- [ ] **Step 3: Build + checks.**

### Task 7: Reviews, build, deploy

- [ ] Reviewer agent + GPT review of the whole change; cross-check, fix, GPT second check (rules).
- [ ] Argon final review of the new Korean strings.
- [ ] Ledger entry; ask the user before deploying (Case 222, ini + layout defaults); in-game checklist: logs L1-L6 judged by Claude; eyes only: dragging feels right, the snap preview line, the border handle, gamepad pick/move.

---

## Design review round 1 (GPT gpt-6-astra, 2026-10-26) - all 11 taken; these override the tasks above

D1 Input first: Input.cpp only observes keyboard downs; returning kStop cannot take back what other sinks got. Task 5
   Step 1 (input investigation + GPT diagnosis) moves BEFORE Task 4 as Task 0. The chosen boundary must own a consumed
   press down/hold/up (the B/Enter release that leaves the mode must not reach SkyUI), cover focus loss, menu close,
   search box/dialogs, and the right stick (an analog path the key sink does not see).
D2 Preview: keep three rects apart - `box` (the block's full content rect: model place and fit, unchanged),
   `FrameInfo::visible` (exists: the movie's visible stage rect), new `columnClip` (the preview column's viewport). Only the
   scissor and the alpha save/restore use box ∩ columnClip. Column clipping does not depend on the fit (zoom / not
   fitted frames still clip). Fully out: skip Render() and the alpha save, keep the game-pass hide (do not use
   a_active=false, it gives models back and unplaces).
D3 SnapSlot returns the insertion anchor `std::optional<BlockId> before` (the visible block the dragged one goes before;
   nullopt = end of column), not an index: the saved arrays also hold hidden blocks and absent add-ons. Tests: hidden
   and absent ids around the drop, a move within the same column.
D4 Coordinates and lookups: define `BlockClip(id)` (the panel clip wherever it sits) and one stage <-> column-local
   transform; scroll applied in exactly one place (the column clip's _y, Arrange returns unscrolled rects + a separate
   scroll per column). Every `Child/Content/SetVisible` lookup of a panel and the `s.preview` / `s.itemCard` aliases go
   through it.
D5 Default layout and the button row: the default blocks already fill 67..623 (556). The lock/reset row sits just below
   the left column (top 627, height 22, right-aligned at the column's right edge), so the default layout never scrolls
   and the row never overlaps a block (user: "왼쪽칸 우하단" - read as the bottom right of the left column). Settings:
   clamp panelGap so two minimum columns fit (590 - 2x240 -> gap <= 110).
D6 Tab behaviour as the code has it (Widget.cpp ~2688, ~1465): Powers/Shouts hide tree, progress AND nodecard; progress
   is also hidden when no school; the spell card's height grows to the rest of its column when blocks above/below are
   hidden (Powers 317, Active Effects up to 556) - keep that: `Arrange` takes per-block heights per tab, and a block
   may be marked "fills its column" (the spell card). The "nothing chosen" hide of the whole widget must not hide the
   button row.
D7 Wheel: SkyUI uses Mouse.addListener and its ScrollingList scrolls only when it is under the mouse; ItemMenu has a
   preview zoom on the wheel. Register our own Mouse listener per menu (removed at close), hit-test our column areas
   incl. gaps and empty space, and wrap ItemMenu's wheel handler only if the preview zoom must not fire over our
   columns. The wheel over our columns scrolls them in normal mode too (an exception to "no input consumed outside the
   mode"). setMask hit behaviour in Skyrim is unverified: Task 3 logs a self-test.
D8 One drag session {blockId, grab offset, entry State, menu generation}: ends on onRelease, onReleaseOutside / mouse
   up anywhere, focus loss, menu close (= cancel). The drag ghost lives in a top overlay outside the column masks; the
   column under the pointer auto-scrolls near its top/bottom edge; a block taller than the viewport is grabbed at the
   pointer offset.
D9 Add-on API: `struct Registration { std::uint32_t apiVersion; std::uint32_t structSize; const char* id; const char*
   title; float height; DrawFn draw; void* user; }`; the host copies id/title, validates version/size/finite positive
   height/non-null draw, answers with a result message; callbacks are called on the main (UI) thread, the clip is
   lent only for the call; registration only before the first menu open after kDataLoaded; documented as a CommonLib
   C++ ABI interface (GFxValue), not a C ABI.
D10 Layout file safety: write to `SL_MagicMenu_Layout.json.tmp` in the same folder, check every write/flush/close, then
   copy the old file to `SL_MagicMenu_Layout.json.bak` (CopyFileW, overwriting) and move the `.tmp` over it (MoveFileExW
   REPLACE_EXISTING | WRITE_THROUGH; no file yet: the move alone). ReplaceFileW, the first plan, made the file vanish under
   MO2's usvfs in the 2026-10-31 game run. A missing or unreadable file is read from `.bak` at load; an unreadable file
   is never copied over `.bak` (it goes to `.bad`), and a failed move over it keeps the `.tmp`; a file that fails
   to read is copied to `.bad` before a save may overwrite it; a schema `"version": 1` (future versions: read-only,
   not overwritten); a failed save logs, does not claim success, and keeps the mode's State to retry.
D11 Redraw: moves and scrolls only change transforms; a confirmed width change (border released, block dropped in the
   other column) invalidates that block's content cache (tree content, own card signature + cardFit, progress card,
   node card) and redraws it once; the border shows a guide line while dragged. Use the existing redraw time logs.

Task order after D1: Task 0 (input investigation), Task 1, Task 2, Task 3, Task 4, Task 5, Task 6, Task 7.
