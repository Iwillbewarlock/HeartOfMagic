# Magic menu without HoM, the HoM add-on block - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `SL_MagicMenu.dll` builds and runs with no Heart of Magic code; a new `SL_MagicMenu_HoM.dll` draws the same tree
window, progress card and node card through Block API v3; a magic menu FOMOD picks the add-on blocks.

**Architecture:** Block API v3 (frame callback with a Context, key callback, own mouse handlers, content inset, legacy
id, ShowSpell and Visible messages, an overlay clip) is added to the host first while the built-in HoM blocks still
work. Then the HoM code leaves the core (moved files, deleted Widget.cpp regions) and is rebuilt in the add-on from the
frozen base commit `f83dc6f3`, drawing code moved as unchanged as possible.

**Tech Stack:** C++23, CommonLibSSE-NG, SKSE messaging, Scaleform AS2 through GFxValue, CMake + Ninja via `BuildRelease.ps1`,
offline checks in `tools/` (PASS/FAIL lines, exit 1 on a failure).

**Spec:** `docs/superpowers/specs/2026-10-30-magic-menu-hom-addon-design.md` (with its "Changes after the GPT design
review"), on top of `docs/superpowers/specs/2026-10-26-magic-menu-blocks-design.md` and `plugins/SL_MagicMenu/src/BlockApi.h`.
Line numbers `W:<n>` are `plugins/SL_MagicMenu/src/Widget.cpp` at `f83dc6f3`; read the old code with
`git show f83dc6f3:plugins/SL_MagicMenu/src/Widget.cpp`.

## Global Constraints

- Build: worktree root `D:/DEV/HeartOfMagic/.claude/worktrees/magic-menu-proto`, `.\BuildRelease.ps1 -preset Release-2026`. Never while `SkyrimSE.exe` runs (`tasklist /FI "IMAGENAME eq SkyrimSE.exe"`).
- Every check must pass after every task: `build/tools/Release/magic-menu-*-check.exe` plain and with `--selftest`.
- New files at most 600 lines (repo rule). Widget.cpp is over it already: do not grow it; moved-out code shrinks it.
- `plugins/SL_MagicMenu_SpellResearch` (v2 add-on, own copy of the v2 BlockApi.h) is not edited and must keep building; its unchanged v2 bytes (registration, 'SLMD', 'SLMH', click) must keep working against the v3 host.
- Ini keys keep their section and name in `Data/SKSE/Plugins/SL_MagicMenu.ini`; the add-on reads its keys from that same file with today's defaults and clamp ranges.
- `SL_MagicMenu_Layout.json` keeps the retired ids `tree`, `progress`, `nodecard` on disk (final -> legacy on write, legacy -> final on read).
- On-screen Korean strings are moved unchanged (no new wording unless a task says so).
- Do not touch `plugins/spelllearning`, `PrismaUI/`, `fomod/ModuleConfig.xml`, the `feature/h1-core-state` worktree, `D:/DEV/SpellResearchPrismaUI`.
- Commits: name the files (`git add <files>`), message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No push, no deploy.
- Code style: repo CLAUDE.md "Code Style" (C++23, named constants, no raw new/delete, lock_guard, RE types checked in `plugins/external/commonlibsse-ng`).

## Review Focus

1. An unmodified SR v2 add-on against the v3 host: registration accepted, 'SLMH' (apiVersion 2) still resizes the block, clicks still arrive. Test: block-api-check feeds SR's own header bytes (Task 1).
2. A player's moved layout from before the split: `tree` in the left column survives upgrade -> save -> downgrade -> upgrade, and a file holding both `tree` and `SL_MagicMenu_HoM.tree` keeps the retired one's place. Test: layout-check (Task 2).
3. Core alone (no add-on; user 2026-10-30: "본체만 깔린 경우 기본적인 주문정보랑 프리뷰창은 있어야지"): the 3D preview box and our spell card (the basic spell info) are drawn as today, with board and gear; no gap where the HoM blocks were. Test: layout-check "core only" case (Task 2) + in-game list item.
4. List selection changing A -> C -> A between two frames while a node was key-picked: the pick is dropped (seq changed), a queued ShowSpell older than the newest list choice is dropped. Test: block-api-check `ShowSpellDue` cases (Task 1), hom-block-check pick rules (Task 6).
5. Layout mode entered and left with the mouse on a tree node: no tooltip left behind, no node click in the mode. Test: hom-block-check `TooltipValid(generation)` (Task 7) + in-game list item.

---

### Task 0: Diagnose the spell-change jump (video 12:13, 00:33-00:44) - read-only

**Files:** ledger `C:/Users/qtuna/AppData/Roaming/Claude/side-session-notes/local_9ced2b14-7638-44d5-892f-a71e8b856aa3/ledger-spell-change-jump.md` (create).

What the video shows (other session's report, frames `.../video1230/f/s0033.png`..`s0044.png`): changing the spell, the
right column's tree window appears and disappears and the SR block below it jumps between the middle and the bottom,
some torn frames. Not yet explained.

- [ ] Step 1: In `SL_MagicMenu.log` (`C:/Users/qtuna/Documents/My Games/Skyrim Special Edition/SKSE/`, run 12:07-12:30) find the selection changes of that time and the Rearrange / fit / "block X is W x H now" / WatchEmpty lines around them.
- [ ] Step 2: Write hypotheses as "if X, then Y, because Z" in the ledger. Candidates to check in code at f83dc6f3: `g_menu.chosen` false for a frame during a list change (WatchChosen W:7731-7743, two calls per frame) flipping EmptyLookOf; WatchEmpty closing the SR drawSelection block when `shown` is 0 for a frame; TabHidden for a power entry in the All tab; the 150 ms shrink delay (DecideFit) against an add-on's 'SLMH'.
- [ ] Step 3: Confirm one from the log and code, or name the log line to add. Hand the result to Task 4 or Task 8 (whichever owns the code).

### Task 1: Block API v3 contract and its pure rules

**Files:**
- Modify: `plugins/SL_MagicMenu/src/BlockApi.h` (v3 header, comment block extended)
- Modify: `plugins/SL_MagicMenu/src/BlockRules.h` (reading and judging), `BlockRegistry.h/.cpp` (store v3 fields)
- Test: `tools/magic-menu-block-api-check.cpp`

**Interfaces (Produces):**
- `BlockApi::kVersion = 3`, `kVersionV2 = 2`; `RegistrationV2` frozen (today's `Registration`), `Registration` v3 = v2 fields in place, then
  `FrameFn frame`, `KeyFn key`, `float pad = -1.0f` (< 0: host default kPad), `std::uint32_t flags` (`kFlagOwnMouse = 1`, `kFlagKeepWhenEmpty = 2`), `const char* legacyId`.
- `using FrameFn = void (*)(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const Context& a_context, void* a_user);`
- `using KeyFn = bool (*)(std::uint32_t a_scanCode, void* a_user);` (return value logged only; the host never consumes)
- `struct Context { apiVersion, structSize; std::uint32_t shown, listForm, selectionSeq; SelectionSource source; std::uint32_t servedRequest, tabBits; const char* tabSchool; bool layoutMode; std::uint32_t overlayGeneration, contentGeneration; float viewLeft, viewTop, viewRight, viewBottom, rowSize, lineFirst, linePitch; };`
  `enum class SelectionSource : std::uint32_t { kNone, kList, kAddon };`
- `kMessageShowSpell = 'SLMS'`, `struct ShowSpellRequest { apiVersion, structSize; const char* id; std::uint32_t formId, requestId; std::uint32_t nameShown; };`
- `kMessageVisible = 'SLMV'`, `struct VisibleRequest { apiVersion, structSize; const char* id; std::uint32_t shown; };`
- `BlockRules::ReadRegistration` reads v1, v2, v3; `ReadHeight` accepts apiVersion 2 and 3; `ReadShowSpell`, `ReadVisible`; `BlockRules::kRetiredIds = { "tree", "progress", "nodecard" }`; `JudgeLegacy(legacyId, alreadyTaken) -> std::optional<std::string reason>`; `ShowSpellDue(seqAtQueue, seqNow) -> bool` (false when the list selection changed after the request was queued).
- `BlockRegistry::Entry` gains `frame, key, pad, flags, legacyId`; `BlockRegistry::LegacyAliases() -> std::map<std::string, std::string>` (legacy -> final).

- [ ] Step 1: Tests in block-api-check: v3 bytes read whole; a v3 struct cut at v2's size reads as v2 fields + defaults; SR's v2 header bytes (copy `plugins/SL_MagicMenu_SpellResearch/src/BlockApi.h` structs into the check under a namespace) accepted with `Result.apiVersion == 2`; v4 refused with "update SL_MagicMenu"; unknown flag bit refused; `legacyId` not in kRetiredIds refused; second claim of `tree` refused; 'SLMH' with apiVersion 2 and 3 read, 1 and 4 refused; 'SLMS' / 'SLMV' cut short refused with the byte count; `ShowSpellDue(5,5) == true`, `ShowSpellDue(5,6) == false`.
- [ ] Step 2: Build, run `magic-menu-block-api-check.exe` - expected FAIL (no v3 yet).
- [ ] Step 3: Implement header, rules, registry. Keep v1 / v2 reading code paths and their logs as they are.
- [ ] Step 4: Build; all checks PASS, `--selftest` too; SR add-on builds.
- [ ] Step 5: Commit.

### Task 2: Legacy ids in the saved layout

**Files:** Modify `plugins/SL_MagicMenu/src/Layout.h/.cpp`, `Settings.cpp` (LoadLayout, ReadLayoutFile, SaveLayout W/Settings.cpp:229-250, 508-530, AddBlocks 459-472). Test: `tools/magic-menu-layout-check.cpp`.

**Interfaces:**
- Consumes: `BlockRegistry::LegacyAliases()` (Task 1).
- Produces: `Layout::ToFinal(State, const std::map<BlockId, BlockId>& legacyToFinal) -> State` (a retired id becomes its final id; if both are present, the retired one's place wins and the other is dropped); `Layout::ToLegacy(State, map) -> State` (final -> retired); `Settings::SetLegacyAliases(map)` called at the first menu open before `AddBlocks` (W:1676-1697); every layout read goes through `ToFinal`, every write through `ToLegacy`.

- [ ] Step 1: Tests: `{left:[preview,tree], right:[nodecard]}` with alias `tree -> SL_MagicMenu_HoM.tree` reads as the final id in the left column; written back it is `tree` again; a file with both ids keeps the retired one's place; round trip upgrade -> save -> read with no alias (downgrade) -> alias again keeps the place; no alias: retired ids stay as unknown ids (Repair keeps them, Arrange skips them).
- [ ] Step 2: Run layout-check - FAIL.
- [ ] Step 3: Implement. No visible change while the built-in blocks still own those ids (no alias registered).
- [ ] Step 4: Build, all checks PASS. Step 5: Commit.

### Task 3: Shared helpers out of Widget.cpp (no behaviour change)

**Files:** Create `plugins/SL_MagicMenu/src/TextFit.h` (header-only), `GameThread.h/.cpp`; split `LayoutMath.h` into `LayoutMath.h` (generic: TextMetrics, rows, LinesFit, RowsFit/RowsHeight) and `HomLayoutMath.h` (Badges, LinkBoxes, PlanProgress, ProgressNeeded, NodeCardNeeded; includes TreeData.h); modify Widget.cpp (W:842-1002 replaced by TextFit calls), Hom.h/.cpp (thread helpers moved out), Preview.cpp, Spells.cpp (callers), `tools/CMakeLists.txt`, the checks that include LayoutMath.h.

**Interfaces (Produces):**
- `namespace TextFit { struct Metrics { double rowSize, lineFirst, linePitch; }; struct Log { std::set<std::string> cut, q2; }; double Size(const Metrics&, double base); double LineH(...); double FieldH(const Metrics&, double size, int lines = 1); ... FitLine / FitLines / SizeFor / Q2State }` - today's signatures with `const Metrics&` and `Log&` first. The fallback constants (kListRowFallback, kGutter, kMaxCutLogs) move with them.
- `namespace GameThread { void Set(); bool On(); std::uint32_t Id(); }` (today's Hom::SetGameThread / OnGameThread / GameThreadId).
- `Spells::SchoolOf(SpellItem*)` moves to `SpellSchool.h` (`SpellSchool::Of`) in the core.

- [ ] Step 1: Move; host calls `TextFit::X(Metrics(), ...)` with its g_menu values.
- [ ] Step 2: Build; all checks PASS (preview-check now includes HomLayoutMath.h for the HoM cases).
- [ ] Step 3: Commit.

### Task 4: Host side of v3 in Widget.cpp and Input.cpp

**Files:** Modify `Widget.cpp` (DrawAddons W:3472-3518, AddonBoxOf 3284-3288, MakeAddonHit 3368-3387, WatchEmpty, EmptyLookOf use, Frame::Call 8127-8207, MeasureListRow 7875-7925, layout mode enter / leave 6095-6160), `Widget.h`, `Input.cpp` (339-356), `BlockRules.h` (EmptyLookOf 592-599: `kFlagKeepWhenEmpty` keeps the place). Test: block-api-check (pure parts).

**Interfaces:**
- Consumes: Task 1 types, Task 3 TextFit.
- Produces (host behaviour later tasks rely on):
  - `frame` is called every UI frame the menu is open for each v3 block that sets it and is shown (layout mode included), after the second `DrawAddons` of `Frame::Call`, with the block's present content clip, its overlay clip (`mmxOverlay_<id>` in the block's column clip above all panels, origin at the content clip's origin, removed and `overlayGeneration` bumped on hide, move, scroll, redraw, layout mode change) and the Context (selectionSeq = `g_menu.selSeq`; source kAddon when the last Commit came from a served ShowSpell, with `servedRequest` its requestId; tabSchool from ReadTab; viewport = the block's part inside `ColumnViewport`, content coords; text metrics = rowSize / lineFirst / linePitch).
  - `key`: Input.cpp queues every keyboard down that passes today's gate (menu open, no text entry, layout mode off, not a key the layout mode owns) and the host calls each v3 `key` on the next frame. The U/G handling stays in Input.cpp until Task 5 removes it.
  - `pad >= 0` replaces kPad as the content clip's inset (top, left, right, bottom).
  - `kFlagOwnMouse`: no `mmxHit` clip is made for the block.
  - 'SLMS': queued with the selSeq at queue time; on the next frame, if `ShowSpellDue`, the host runs `ShowSpellFromAddon(movie, formId, nameShown, requestId)`: the part of OnNodeClicked after its HoM lines (W:7801-7815: SelectInList, `selectedByUs`, Commit kTree -> now kAddon source, FollowClick).
  - 'SLMV': a hidden set; a hidden block keeps its place (like today's progress card with no school, W:3952) and gets no `frame`.
  - When MeasureListRow first finds the row size, every v3 block is drawn again.

- [ ] Step 1: Pure tests (block-api-check): overlay generation bumps per event list; viewport clip of a block partly scrolled out; ShowSpell queue keeps the newest per block id and drops stale ones.
- [ ] Step 2: Implement. With no v3 add-on installed, nothing on screen changes (SR is v2).
- [ ] Step 3: Use Task 0's result: if the jump's cause is host-side (chosen flip, WatchEmpty, shrink delay), fix it here with its own log line and SelfTest.
- [ ] Step 4: Build, all checks PASS, SR builds. Commit.

**Review gate A** (after Task 4): reviewer agent + GPT `review --base f83dc6f3`; fix only what the code confirms; second review.

### Task 5: The core without HoM

**Files:** `git mv` `Hom.*`, `TreeData.*`, `HomStyle.*`, `Spells.*`, `HomLayoutMath.h` to `plugins/SL_MagicMenu_HoM/src/`; modify `Widget.cpp` (delete the map's regions 1-B and the HoM lines of 1-C: the `Hom::Ready()` gate W:8378-8382, ReadHomStyle, node handlers, `kTree` panel title, U/G frame lines 8182-8186, polling, RedrawPass, PreviewFrame's `treeSpell` stays (it is the P2 card)), `Main.cpp` (no SpellLearningAPI.h, no "SpellLearning" listener, `GameThread::Set()`), `Input.cpp` (U/G and their logs out), `Settings.h/.cpp` (HoM keys out; `fTreeRight` and `fNodeCardRight` stay for DefaultFrames), `Layout.h/.cpp` (kTree / kProgress / kNodeCard become retired ids: in `Default`, out of `DefaultHeights` and `BuiltinBlocks`; `NoTreeTabHidden` gone), `BlockRules.h:595` (no kTree empty look), `CMakeLists.txt`, `plugins/SL_MagicMenu/dist/SL_MagicMenu.ini` (HoM keys kept, comment "read by the Heart of Magic block"), `tools/CMakeLists.txt` (tree-check, homstyle-check built from the add-on's src), layout-check and preview-check cases that used the built-in ids.

- [ ] Step 1: `grep -rn "Hom::\|Spells::\|TreeData\|HomStyle\|SpellLearningAPI" plugins/SL_MagicMenu/src` returns nothing; the core links no HoM source.
- [ ] Step 2: layout-check "core only" case: default layout with no add-on arranges preview / spell card with no gap, retired ids skipped.
- [ ] Step 3: Build, all checks PASS (tree-check and homstyle-check from their new paths), SR builds. Commit.

### Task 6: HoM add-on - plugin, settings, progress card, node card, keys

**Files:** Create `plugins/SL_MagicMenu_HoM/CMakeLists.txt` (as SR's, include dirs `src` and `../SL_MagicMenu/src` for BlockApi.h, Draw.h, TextFit.h, LayoutMath.h; a `SL_MagicMenu_HoM_dist` target like SR's), `src/Main.cpp`, `src/HomSettings.h/.cpp`, `src/Strings.h` (HoM rows of the W:227-400 table, unchanged), `src/ProgressCard.cpp`, `src/NodeCard.cpp`, `src/Selection.h` (pure pick rules), top-level `CMakeLists.txt` (add_subdirectory). Test: create `tools/magic-menu-hom-block-check.cpp`.

**Interfaces:**
- Consumes: Task 1 v3 types; Task 4 host behaviour; moved Hom / Spells / TreeData / HomStyle.
- Produces: blocks `SL_MagicMenu_HoM.tree` (Task 7 draws it), `.progress`, `.nodecard`, each with `legacyId`, `tabs = kTabSpells`, `kFlagKeepWhenEmpty`, heights from the ini rects (bottom - top), pad 4 (kCardTop) for the cards; registered at kDataLoaded only when HoM's API answered (else one log line, no block - user Q3); `Selection::Chosen(shownForm, keyPick) -> bool` (`shownForm != 0 || keyPick >= 0`); `Selection::DropPick(seqSeen, seqNow) -> bool`.

- [ ] Step 1: hom-block-check tests: ini reading of `[Keys] iNext/iSetTarget`, `[Layout] fTree*/fProgress*/fNodeCard*/fTreeScale/fTreeMarkScale/iMaxSteps/iMaxNodes`, `[Debug] fDebugExtraHeight` from a sample SL_MagicMenu.ini (defaults and clamps as Settings.h at f83dc6f3: 22, 34, 0.75 in 0.01-10, 1.0 in 0.5-3, 1 in 1-6, 60 in 1-200); `Chosen(0, -1) == false`, `Chosen(0, 7) == true` (G1: the U pick counts), `Chosen(x, -1) == true`; G acts only when chosen; `DropPick(3, 4) == true`.
- [ ] Step 2: Port the progress card (W:7119-7316, 7322-7337) and node card (W:7414-7603) drawing unchanged but for `TextFit` with `Context` metrics and the content clip the host lends; fits through 'SLMH'; progress card on a tab with no school: 'SLMV' hidden (W:3952). Row-2 baseline alignment (W:7170-7186) dropped.
- [ ] Step 3: U/G through `key`: U picks the next candidate (W:7818-7842) and sets the add-on's key pick, which `Selection::Chosen` counts; no ShowSpell for a key pick (3D and our card stay on the list). G (and the node card's button, own handler) acts when chosen (W:7844-7857). A list change (selectionSeq) drops the pick unless the source is this add-on's own served request.
- [ ] Step 4: Self-tests C1/C2 equivalents (`CheckRowBaselines`'s button part, `CheckButtonBottom` W:8050-8116) log `SelfTest:` PASS/FAIL to `SL_MagicMenu_HoM.log` when the host ini's `bSelfTest` is on.
- [ ] Step 5: Build, all checks PASS. Commit.

### Task 7: HoM add-on - the tree window

**Files:** Create `plugins/SL_MagicMenu_HoM/src/TreeMarks.cpp` (W:2879-3118 marks and legend), `TreeDraw.cpp` (W:6422-7117 helpers and RedrawTree), `TreeInput.cpp` (W:2798-2866 handlers, DropStaleHover 6855-6885, click -> ShowSpell, centre / school W:7610-7686 and WatchSelection's HoM part 7765-7783), `Tooltip.cpp` (W:7347-7412). Test: hom-block-check.

**Interfaces:**
- Consumes: Task 6's plugin and Selection; Task 4's frame / overlay / own mouse / ShowSpell.
- Produces: the tree block drawn as at f83dc6f3; handlers made once per process (GPtr, never released) and bound with `SetHandler` on the clips the add-on draws; they only record and ignore events while `Context.layoutMode` was last seen on; the frame callback acts on the recorded click (in the tree column's viewport only - L2 mask clicks from `Context.view*`), sends 'SLMS' with `nameShown` (HoM's hidden-name rule, W:2774-2792) and a new requestId; DropStaleHover hit-tests in the lent content clip; the tooltip is drawn into the overlay at today's place, `TooltipValid(drawnGeneration, overlayGeneration)` before reusing it.

- [ ] Step 1: hom-block-check: `TooltipValid(3,3) == true`, `(3,4) == false`; a click outside the viewport counted, not acted on; legend shown flag: false for the empty, no tree, no centre and hidden school states, true for a drawn tree (B1).
- [ ] Step 2: Port; B1 fix as verified: DrawLegend after the SchoolShown check, one `legendShown` flag applied to the legend clip in every return path.
- [ ] Step 3: The in-game self-tests of the tree (L2 mask clicks at close, T2 tooltip, P1 / P8 tree card paths that stay in the host) keep their log lines; the add-on's go to its own log.
- [ ] Step 4: Build, all checks PASS. Commit.

**Review gate B** (after Task 7): reviewer agent + GPT `review --base <gate A commit>`; fixes; second review.

### Task 8: Core review fixes (verified 2026-10-30 at f83dc6f3)

**Files:** `Widget.cpp` (strip and gear W:5546-5650, 4676, 8204, 8556), `BlockRules.h` (AllShownRead 513, HintRowRects 240), layout-check.

- [ ] R1: count consecutive read fails per signature; after `kPollFrames` frames build the union from the children that read, cache it with the signature, log the unread child once. Test: layout-check `VisibleUnionAfterFails`.
- [ ] R2: a file-static last strip size (set where `g_menu.stripSize` is set) places the gear while this menu's size is unknown, so it does not jump 31 units at open.
- [ ] R3: when the strip would be pulled up into the board (`strip.top < board.bottom + kBoardStripGap - tolerance`), leave SkyUI's strip where it is, warn once; the gear is still placed. Test: layout-check "low" case asserts the no-move verdict.
- [ ] T1: "no panel" becomes `SelfTest: L8 ... SKIP`; at close compare the last stored strip target with a fresh read (`L8 last PASS/FAIL`).
- [ ] T2: close-time SelfTests for the board rect (g_menu.board vs BoardRect), the reset button (rect and clickable only when fully out), the gear rect.
- [ ] DumpMenuClips only with `bSelfTest`; `g_menu.heldShrink.erase(Layout::kSpellCard)` beside the erase at W:4676.
- [ ] Task 0's fix if it is not host-side Task 4 work.
- [ ] Build, all checks PASS. Commit.

### Task 9: Packaging and docs

**Files:** Create `fomod_magicmenu/fomod/ModuleConfig.xml`, `fomod_magicmenu/fomod/info.xml`; modify `plugins/cmake/Distribution.cmake` (a `assemble_magicmenu_dist` target: `dist/SL_MagicMenu_<VERSION>/` = core `SKSE/Plugins/SL_MagicMenu.dll`, `SL_MagicMenu.ini`, `MCM/Settings/InventoryReforged.ini`, the HoM add-on's folder as an option folder, LICENSE, THIRD-PARTY-NOTICES.md, licenses/; zip `SL_MagicMenu_<VERSION>.zip`; pdbs in a `_DebugSymbols.zip`), `CLAUDE.md` (tools list: hom-block-check, moved checks, the v3 API), the blocks design spec (a "Block API v3" section), the new spec (state "implemented").

- [ ] FOMOD: step 1 "Core" (required: core folder); step 2 "Add-on blocks" (SelectAny): "Heart of Magic Librarian" (its folder; recommended when `SKSE/Plugins/SpellLearning.dll` is present, else optional) and a commented-out entry for the Spell Research block. Validate the XML against the FOMOD schema used by `fomod/ModuleConfig.xml` (same `xsi:noNamespaceSchemaLocation`).
- [ ] Build: the zip lists the expected files (print `tar -tf`).
- [ ] Commit.

**Review gate C** (final): reviewer agent on the whole range `f83dc6f3..HEAD` + GPT `review --base f83dc6f3`; fixes; second review; then the Korean report and the eye-only in-game list.
