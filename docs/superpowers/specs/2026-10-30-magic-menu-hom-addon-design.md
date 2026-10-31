# Magic menu without Heart of Magic: the HoM add-on block (design draft, 2026-10-30)

Names (2026-10-31, before the first Nexus release): the public name is Magic Menu Rewrite and the shipped files and SKSE
plugin names are `MagicMenuRewrite*` (was the working name `SL_MagicMenu*`); the repo folders `plugins/SL_MagicMenu*` and the
CMake targets keep the working name. The text below uses the new product names where it says what ships.

Implemented 2026-10-30 (plan docs/superpowers/plans/2026-10-30-magic-menu-hom-addon.md). The contract as built is the
header comment of `plugins/SL_MagicMenu/src/BlockApi.h`; where it differs from the draft below, "As implemented" says how.

User (2026-10-28/29/30): "Homl에 대한 종속성은 없애도록하자. Hom에서 추가해주던 창들은 hom이 직접 에드온으로 추가하는 방향으로",
FOMOD picks the add-on blocks (HoML block now, the Spell Research block later), "마법메뉴랑 homl 에드온을 먼저 넥서스에".
Map of the coupling at f83dc6f3: session notes (explorer report), older map `Desktop/Claude outputs/magic-menu-no-hom-brief.md`.

## Goal

- `MagicMenuRewrite.dll` (the core) builds and runs with no HoM include, no `SpellLearning` listener, no `spell_tree.json` /
  HoM `config.json` read. Without any add-on it shows: 3D preview box, our spell card, the board, the layout mode,
  the key-hint strip. Today the core draws nothing at all without HoM (`Widget.cpp:8378` `if (!Hom::Ready()) return;`).
- `MagicMenuRewrite_HoM.dll` (new, `plugins/SL_MagicMenu_HoM`) registers three blocks through the Block API and draws the
  same tree window, learning progress card and node card as today (marks, legend, labels, badges, tooltip, U / G keys,
  HoM design presets). It needs SpellLearning.dll (HoM API v2); without it it registers no block and says so in
  its log (user 2026-10-30, Q3).
- A separate DLL, not inside SpellLearning.dll: the FOMOD picks blocks as files, HoM's plugin is another session's area,
  and the add-on can ship and update with the menu.

## What stays in the core

3D preview (Preview.*), our spell card, the board, columns, scroll, layout mode, key-hint strip, Block API host,
the tree-card / 3D path for a spell that is not in the list (P2 `ShowTreeSpellCard`, P3 `LoadUnlearned`; reached now by
an add-on request instead of the tree's own click), `bPreviewUnlearned`, the generic thread helper
(`Hom::SetGameThread/OnGameThread`, renamed `GameThread::*`), `Spells::SchoolOf(SpellItem*)`, text fitting helpers
(`Size`, `FieldH`, `Metrics`, `FitLine(s)`, `SizeFor`, W:842-1002) moved to a header both DLLs compile.

## What moves to the add-on

Hom.*, TreeData.*, HomStyle.*, Spells.* (but SchoolOf), the HoM regions of Widget.cpp (map 1-B: marks, legend,
RedrawTree and its helpers, progress card, node card, tooltip text, centre / school, OnNodeClicked, DoNext, DoSetTarget,
snapshot cover, the C/L self-tests of those cards), the HoM part of LayoutMath.h (Badges, LinkBoxes, PlanProgress,
ProgressNeeded, NodeCardNeeded), the HoM strings, the U / G handling of Input.cpp.

## Block API v3 (v1 and v2 stay readable unchanged; SR's v2 add-on keeps building and working)

`Registration` v3 = v2's fields in v2's places, then:

1. `frame(movie, const Context&, overlay, user)`: called once per UI frame while the menu is open, after the host's
   selection and tab handling, for every shown v3 block that sets it. For polling HoM state, acting on what the add-on's
   own handlers recorded (below), the tooltip. `Context` (apiVersion, structSize first): `shown` (as drawSelection's
   a_shown), `listForm` (the committed list entry's form, 0 = none), `tabBits` (the BlockRules tab bit), `tabSchool`
   ("Alteration".."Restoration" or "" - the host's ReadTab), `layoutMode`, `viewport` (the block's part inside the
   column's visible area, content clip coordinates), `moved` (the block was drawn again, moved or scrolled since the
   last call). `overlay`: a clip of the block's column above every panel in it, its origin on the content clip's origin
   (today's tooltip parent, W:7357-7400), cleared by the host when the block is hidden, moved or the layout mode starts;
   lent for the call only, like the content clip.
2. Own mouse handlers (flag `ownMouse`): the host lays no `mmxHit` clip; the add-on makes its own GFxFunctionHandlers
   (once per process, as the host does at W:8417-8425) and binds onRelease / onRollOver / onRollOut / hitArea to clips it
   draws, exactly as RedrawTree does today (W:6716-6728). Handlers only record; the add-on acts in `frame`. In the layout
   mode the host's covers lie above the panels (W:4730) and the handlers ignore events while `Context.layoutMode` was
   last seen on (today's `g_menu.layout.active` checks). Chosen over a host `pointer` callback because it lets RedrawTree,
   NodeClick / NodeRollOver / NodeRollOut move unchanged; a host pointer callback with add-on polygon hit tests is the
   fallback if a check below fails.
3. `key(scanCode, user) -> bool`: a keyboard key down while the menu is open, no text field takes keys and the layout
   mode is off, delivered on the next UI frame (Input.cpp queues; it never consumes, as today). Gamepad none (as today).
4. `pad`: the content clip's inset (default kPad 8; the cards use kCardTop 4 today), so the cards keep their place.
5. `legacyId`: one of the core's retired ids (`tree`, `progress`, `nodecard`) the block takes over. The host keeps an
   alias map (retired id -> final id) and applies it to every layout it reads (the saved file at load, the default, the
   file read back after a save, W:6145), so a player's moved place survives. Only the retired list; one block per legacy
   id; refused otherwise. Layouts are loaded at plugin load (Settings.cpp:433), registration closes and AddBlocks runs at
   the first menu open (W:1676-1697): the alias is applied there, before AddBlocks.

New add-on -> host messages (any thread, queued, taken on the next UI frame like 'SLMD'):

6. 'SLMS' ShowSpell{id, formId, nameShown}: what a tree node click does today after its own part (OnNodeClicked
   W:7790-7816): SelectInList + Commit(kTree) when the spell is in the list, else the tree card (P2) and, when
   bPreviewUnlearned, the 3D (P3, FollowClick's `a_nameShown` is HoM's hidden-name rule, so it comes with the request).
   Not answered; the block sees the result in the next Context.shown.
7. 'SLMV' Visible{id, shown}: hide a block but keep its place (the progress card on a tab with no school, W:3952).

Key-picked node (bug G1): the U key picks the next target inside the add-on; the add-on counts "a list / card spell
shown OR a node picked by key" as chosen for its own tree and node card, so the node card shows the pick and G acts on it.
The 3D preview and our spell card keep following the list selection only (no ShowSpell for a key pick).

## Changes after the GPT design review (gpt-6-astra, 2026-10-30; each checked in the code)

- G-a (high) Context carries `selectionSeq` (the host's `selSeq`, W:1132, which WatchSelection keys on, W:7749) and
  `selectionSource` (list, key, tree request) plus the `requestId` of the last 'SLMS' it served. A list A -> C -> A between
  two frames leaves `shown` equal but bumps the seq, so the add-on drops a key-picked node as today (W:7765), and the
  add-on's own tree click is told apart from a player's list change (today's `selectedByUs`, W:7762). A newer list
  choice beats a queued 'SLMS' (the host drops a request older than the seq it was sent at).
- G-b `frame` also lends the block's present content clip (DropStaleHover hit-tests the hovered node's hit clip each
  frame, W:6869-6885); never kept past the call.
- G-c `frame` is called in the layout mode too (Context.layoutMode true), and Context has `overlayGeneration`, bumped
  each time the host clears the overlay (mode change included, today W:6095-6100 clears hover and sets tooltipDue), so
  the tooltip cache (W:7352-7355) cannot think a removed tooltip is still up.
- G-d 'SLMH' stays wire version 2: ReadHeight accepts apiVersion 2 and 3 (BlockRules.h:91 accepts only kVersion today, so
  raising kVersion to 3 would refuse SR's v2 height requests). block-api-check sends SR's own v2 header bytes.
- G-e The saved layout keeps the retired ids on disk: the host maps legacy -> final when it reads a layout and final ->
  legacy when it writes one (SaveLayout, Settings.cpp:508). A file holding both ids keeps the retired one's place. So an
  older MagicMenuRewrite reads the file as before, and upgrade -> save -> downgrade -> upgrade keeps the place (layout-check).
- Handlers owned by the add-on: GPtr-held, made once per process on the UI thread, never released while the game runs
  (SKSE plugins are not unloaded); the add-on clears its per-menu state on the menu's close (MenuOpenCloseEvent, as SR).

## Settings and layout

- Ini keys keep section and name. The add-on reads its keys from the same `MagicMenuRewrite.ini`: `[Keys] iNext, iSetTarget`,
  `[Layout] fTree*, fProgress*, fNodeCard*, fTreeScale, fTreeMarkScale, iMaxSteps, iMaxNodes`, `[Debug] fDebugExtraHeight`.
  Block heights come from those rects (bottom - top) as today. The core still reads `fTreeRight` and the item card for
  `DefaultFrames` (the right column's frame) - numbers only, no HoM.
- Layout: the default layout keeps `tree`, `progress`, `nodecard` in their places as retired ids; `DefaultHeights`
  (Layout.cpp:75-84) drops them and `Arrange` skips an id with no height, so a core-only install has no gap - but its
  right column is empty (the board still spans both columns; open question Q4). The add-on registers `MagicMenuRewrite_HoM.tree` etc. with `legacyId`,
  the host renames the id in place. `Repair` keeps unknown ids (as now), `BuiltinBlocks` loses the three.
- Empty look: today the tree shows the centred "주문을 고르면 표시됩니다" line (EmptyLookOf kLineCentred) and drawSelection
  add-ons are closed up with nothing shown. v3 blocks get `drawSelection` with a_shown 0 like any block and are NOT
  closed up when they set `frame` (Q2 - or a flag `keepWhenEmpty`).

## Lost or changed on screen (to accept or fix)

- The row-2 baseline alignment between progress card and node card (W:7170-7186) goes (it is off by design today when the
  tree is fitted; the two cards become separate blocks).
- Our spell card's school name comes from the spell's own school, not the HoM tree's school (W:4278).
- Self-test lines L2 / C1 / C2 (node card, progress card) move to the add-on's log `MagicMenuRewrite_HoM.log`.

## Offline checks and files

- `LayoutMath.h` splits: TextMetrics and the generic rows stay; Badges, LinkBoxes, PlanProgress, ProgressNeeded,
  NodeCardNeeded go to the add-on (`HomLayoutMath.h`), so the core headers no longer include TreeData.h.
- `magic-menu-tree-check` and `magic-menu-homstyle-check` build from the add-on's sources (they become the HoM add-on's
  checks), `magic-menu-preview-check` keeps the generic part and the HoM part moves to tree-check,
  `magic-menu-layout-check` replaces its built-in tree / progress / nodecard cases with retired ids + alias rename cases,
  `magic-menu-block-api-check` adds v3 parsing (v1 / v2 / v3 bytes, cut-short v3), legacyId rules, the 'SLMS' / 'SLMV'
  queues and the key queue. New `magic-menu-hom-block-check`: the add-on's pure rules (chosen with a key pick, the G
  condition, the ShowSpell request, ini reading of the moved keys from MagicMenuRewrite.ini).
- The add-on's files keep the repo's 600 line rule (Widget.cpp's HoM regions split into Tree*.cpp, ProgressCard.cpp,
  NodeCard.cpp, Tooltip.cpp ...); the core's Widget.cpp shrinks by the moved parts.

## Packaging

New `fomod_magicmenu/` (ModuleConfig.xml + info.xml): step 1 core (required), step 2 "add-on blocks" (checkbox group):
Heart of Magic Librarian block (default on when SpellLearning.dll is present), place kept for the Spell Research block.
`Distribution.cmake`: a second assembly `dist/MagicMenuRewrite_<VERSION>/` + zip. The HoM `fomod/ModuleConfig.xml` is not touched.

As built (Task 9): the core is the FOMOD's `requiredInstallFiles` (`00 Core`), not a step; one step "Add-on blocks"
(SelectAny) with "Heart of Magic Librarian block" (`10 Heart of Magic Block`; Recommended when
`SKSE\Plugins\SpellLearning.dll` is active, else Optional) and a comment where the Spell Research block goes. The magic
menu has its own version, `MAGIC_MENU_VERSION` (1.0.0, top-level CMakeLists.txt), for the three magic-menu DLLs, the
FOMOD's info.xml and the zip names; target `assemble_magicmenu_dist` makes `dist/MagicMenuRewrite_<version>/` and its zip
(with LICENSE, its own THIRD-PARTY-NOTICES.md from `fomod_magicmenu/THIRD-PARTY-NOTICES.md.in`, licenses/) and
`MagicMenuRewrite_<version>_DebugSymbols.zip` (the two DLLs' .pdb).

## As implemented (2026-10-30)

Contract changes made during the implementation (each in BlockApi.h's header comment, checked by
`magic-menu-block-api-check`):

- `key` goes only to the v3 blocks shown in the frame it is delivered in (a place on this tab, not hidden by 'SLMV'),
  right before that block's `frame` call; a key pressed while a block is not shown is gone for it, not kept for later.
- 'SLMS': at most one request is served per frame - the newest sent, by any block; the others of that frame are dropped
  (one log line). It is also dropped while the layout mode is on, and when the list selection changed after it was queued.
- `frame` gets no overlay (null) in the layout mode: the mode's covers must stay on top.
- 'SLMV' is taken at the start of the frame and again after that frame's draws, before its `frame` calls, so a block can
  hide itself from its first draw.
- The saved layout keeps the retired ids (`tree`, `progress`, `nodecard`) on disk: the host maps them to the add-on's ids
  when it reads a layout and back when it writes one, so an older MagicMenuRewrite still reads the file.
- Context has no `moved` field: `overlayGeneration` (bumped when the host clears the overlay) and `contentGeneration`
  (bumped when the block is drawn into a new content clip) tell the add-on its clips changed; `ownMouse` and
  `keepWhenEmpty` are the flags `kFlagOwnMouse` and `kFlagKeepWhenEmpty`.

## Open questions

- Q1 frame / key in the layout mode: never (proposed).
- Q2 empty look of a v3 block: keep the place and call drawSelection with 0 (proposed).
- Q3 add-on without HoM: decided by the user 2026-10-30 ("블록창 없음"): no block at all, a log line only.
- Q4 core alone: the right column stays empty under the board (proposed: accept; the FOMOD's core-only choice is rare),
  or the core's default puts the spell card in the right column when no add-on block is there.

## Biggest risk

The tree's interaction (per-node hit polygons, rollover, tooltip, the click -> list / tree card / 3D path) is moved from
AS2 handlers in host-made clips to host -> add-on callbacks plus add-on hit tests and host-drawn tooltips. That is about
4k lines moved and re-wired, and only a game run shows hover feel and timing. Mitigation: move the code unchanged where
it can be (drawing), keep the geometry the hit tests use from the same drawing pass, offline checks for the hit tests,
the ShowSpell queue and legacy id rename, and the in-game self-tests carried over.
