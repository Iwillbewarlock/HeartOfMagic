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
- Tabs that hide a block today (Powers/Shouts: no tree, no progress; Active Effects: card only) keep doing so; the
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
