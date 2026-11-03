# Spell Research add-on block - design

Names (2026-10-31, before the first Nexus release): the public name is Magic Menu Rewrite and the shipped files and SKSE
plugin names are `MagicMenuRewrite*` (was the working name `SL_MagicMenu*`); the repo folders `plugins/SL_MagicMenu*` and the
CMake targets keep the working name. The text below uses the new product names where it says what ships.

Date 2026-10-27, revised 2026-10-29 for the Spell Research Prisma UI revamp (D:/DEV/SpellResearchPrismaUI, 0.2.1,
not released). Goal: the first add-on block of SL_MagicMenu, shipped as its own DLL that a later SL_MagicMenu FOMOD
offers as an option (user, 2026-10-29: "포모드로해서 homl용 블록이랑 스펠리서치 프리즈마용 블록을 선택해서 설치가능하게
할거고 추후에 만들 에드온들도 그런식으로"). The HoML block (moving HoM's windows out of SL_MagicMenu) and the FOMOD are
the next task (C:/Users/qtuna/Desktop/Claude outputs/magic-menu-no-hom-brief.md); this one builds the API they start from.

## What the player sees

(The Korean wording below is the design's intent. Superseded by 2a-2d where they differ: the shipped Korean title is
"연구 원형", the links "연구 일지 / 증류기 / 경험치 책", from the block's own lang/ko.json - see 2d.)

A block "연구 아키타입" (shipped as "Research Archetypes" / "연구 원형"; default: end of the right column, moved like any block), shown while a spell is chosen (the
columns are hidden when nothing is, Widget.cpp:3376-3388, 3417). For the spell the menu's card shows:

- Only what the menu's own spell card does not show (user, 2026-10-29: "기본 주문 정보창에서 확인가능한건 빼"): the
  card already shows the school and level, the magicka cost and the casting type, so the block shows the spell's
  ELEMENT (2-35), TARGETING (41-44) and TECHNIQUE (45-59) archetypes only - no school chip, no casting chip, no tier
  label. The SR tier is still read: it decides "?" (below).
- Two layouts (user: "A는 폐기", B and C selectable). The choice lives in SR Prisma's SKSE Menu Framework settings tab
  (user's answer: "SR 프리즈마 설정 탭 안"); the block only reads it, once per menu open, through one function,
  `ResolveLayout()` (proposed key `[MagicMenuBlock] sLayout=list|line` in SpellResearchPrismaUI.ini, to be fixed by
  the SR Prisma session; missing or unknown -> list).
  - B "list": one row per group that has archetypes (Element / Targeting / Technique), the group name dim on the
    left, the archetypes after it separated by a middle dot; a group with none is left out.
  - C "line": every archetype in SR's index order joined by middle dots, wrapping to the column width.
  - In both, each archetype has its SR Prisma icon on its left (the line drawings of SR Prisma's
    tools/icons/skald-icons.js, converted to point data by tools/sr-icons/convert.mjs into ArchetypeIcons.h and drawn
    with the AS2 drawing API in kTitleColor; the frame tells the group).
- An archetype shows its name when the player has any knowledge of it at the spell's tier (its tier GlobalVariable
  > 0, the rule SR Prisma's experience window also uses since 0.2.0), otherwise "?" with only its group's frame (the
  inner glyph would give the archetype away). (SR keeps no
  per-spell "researched" record - re-checked against SR Prisma 0.2.1: still none - so "researched" is read as "the
  player has knowledge in that archetype at that tier"; the reason line says so when a "?" shows.)
- SR does not know the spell, or SR's lists are empty (Import never run), or the selection is not a spell: a "?"
  and a one-line reason. SR still setting up or importing: "Spell Research 준비 중" (snapshot not ready).
- A row of three text links under the archetypes: "연구 일지", "증류기", "연구 경험" - shown only when
  SpellResearchPrismaUI.dll 0.2.1 or later is loaded (older builds have no SRPO and never answer). A click (mouse;
  pad and keyboard are the HoML task's key-input work) sends SR Prisma an open request (SRPO, below) and, when the
  request was delivered, closes the magic menu; SR Prisma waits up to 2 s for the menu to close and then opens the
  window under its own hotkey's conditions. SRPR carries only a result code (SRPrismaAPI.h:58-65; SR Prisma logs its
  reason), so a refusal shows a general HUD notification per code after the menu has closed - Refused: "연구 창을 열 수
  없습니다 (전투 중이거나 필요한 물건이 없을 수 있습니다)", Busy: "다른 창이 열려 있어 연구 창을 열지 못했습니다".
  A request that was not delivered (Dispatch false) keeps the menu open and shows "Spell Research Prisma가 응답하지
  않습니다" in the block's reason line.
- The block shows on the spell tabs only (not on Powers, Shouts or Active Effects: SR files no powers or shouts).

## Spell Research data (SpellResearch.esp, checked with houseCARL and SR's winning scripts)

- `_SR_ListSpellsArchetypes` 000D9E: FormList of 60 FormLists, one per archetype, the spells in it. Index order =
  `_SRU_Utility.GetArchetypeString`: 0-1 casting, 2-35 elements, 36-40 schools Alteration..Restoration,
  41-44 targeting, 45-59 techniques (each sub-list's EditorID matches, e.g. [12] _SR_ListSpellsElementFire). SR
  Prisma's `ARCHETYPES` (view/index.html:960) has the same order.
- `_SR_ListAllSpells` 001343: FormList of 5 FormLists, spells by tier, [0] Novice .. [4] Master.
- `_SR_ListGlobalsArchetypes` 00133D: FormList of 60 FormLists of 5 GlobalVariables, [0] Novice .. [4] Master.
  Values only grow.
- The Librarian adds its catalog spells to these lists with AddForm (LibrarianSpellResearchPatch.cpp); `HasForm`
  sees record and script-added entries.
- Read only. The add-on never writes SR's data. No SR Prisma query API exists (its "ask without opening" item C-32 is
  still a to-do), so the block reads the esp's lists itself.

## Strings and language

The block's language follows SR Prisma's language setting (user, 2026-10-29: "sr 프리즈마에 skse메뉴 설정에 따른
언어설정을 만드는 중이야. 언어는 그걸 따라가게"). That setting, where it is stored, how another plugin reads it and
SR Prisma's translation tables are not decided yet (SR Prisma session, 2026-10-29: today SR Prisma has English
strings in view/index.html only and SpellResearchPrismaUI.ini holds ToggleKey, fUIScale, bSelfTest). So:

(Superseded in part by 2d: SR Prisma's tables now exist and are read first for the archetype names and group labels;
the block ships en.json and ko.json; the tier names were dropped with the tier label; the language is resolved again
at each menu open.)

- Every player-visible string of the block (archetype names, group labels, 3 links, title, reason lines,
  notifications) comes from a per-language JSON file, `SKSE/Plugins/MagicMenuRewrite_SpellResearch/lang/<code>.json`
  (archetype names and group labels from SR Prisma's tables first - 2d); a missing key falls back to the English
  file, a missing English key to the key itself (and a log line).
- Archetype names: the block shows SHORT names, without the
  group word (its group label or icon frame says it): "Area", not "Area Targeting" (the user-approved mock3). The keys
  are the ones SR Prisma's translation tables will have (SR Prisma session, 2026-10-29; its stage 3):
  `archetype.<index>.name` (SR Prisma's `ARCHETYPES` name, view/index.html:960) and `archetype.<index>.short` (the same
  without the group word), index = SR's archetype order. The block shows `.short`, falling back to `.name` (the
  language's short, its name, English short, English name, the key). Until those tables exist `en.json` carries both
  columns itself; then the block reads SR Prisma's (or ships copies generated from them), so a short name never
  differs between the block and SR Prisma's window.
- The language code is chosen by one function, `ResolveLanguage()`: SpellResearchPrismaUI.ini `[Display] sLanguage =
  auto | en | ko` (SR Prisma's settings design draft, docs/2026-10-29-settings-tab-design.md - the key is SR Prisma's,
  not final): en or ko -> that code; auto, missing or another value -> the game's Skyrim.ini `sLanguage:General`
  (KOREAN -> ko, anything else en). A code without its `lang/<code>.json` uses English. Read at kDataLoaded and again
  at each magic menu open (the tables are read again only when the code changed; the registered title keeps the
  language of game start).
- Korean names are SR Prisma's decision (its session reconciles them with TAKEALOOK's existing Korean for Spell
  Research, Mod-Management Case 070, and the glossary). An Argon draft of the 60 names is kept in this session's notes
  only as input to that work. The word "아키타입" is the user's own term for SR's archetypes.

## Changes

### 1. MagicMenuRewrite: Block API v2

v1 stays as it is for v1 add-ons. v2 adds, after the v1 fields of `Registration`:

```cpp
using DrawSelectionFn = void (*)(RE::GFxMovieView*, RE::GFxValue& clip, float w, float h,
                                 RE::FormID shown /*the card's form, 0 = none*/, void* user);
using ClickFn = void (*)(float pressX, float pressY, float releaseX, float releaseY, void* user);  // content clip coords
DrawSelectionFn drawSelection = nullptr;  // v2: when set, used instead of draw
ClickFn click = nullptr;                  // v2: optional
std::uint32_t tabs = 0;                   // v2: bitmask of tabs to show on, 0 = all
```

- `RegistrationV1` frozen as its own struct (`kRegistrationV1Size = sizeof(RegistrationV1)`); apiVersion 1 or 2
  accepted; the fields `min(structSize, dataLen)` covers are copied into a zero-initialised v2 struct; v1 needs
  `draw`, v2 needs `draw` or `drawSelection`. `Result` stays v1-shaped. Version 3+ refused with a reason.
- Which form: the one the menu's own card shows (the tree card when a tree click picked a spell not in the list,
  Widget.cpp:2434/3625-3633, else the committed list selection, else 0). MagicMenuRewrite compares it once per UI frame
  after its own selection and tree-click handling and redraws v2 blocks when it changed (never from inside a draw).
- Redraw request: message add-on -> MagicMenuRewrite `kMessageRedraw` 'SLMD', data = the block's final id; the redraw
  happens on the next UI frame (ignored while the menu is closed). Used when the add-on's snapshot lands and after an
  SRPO reply.
- Click: MagicMenuRewrite puts one transparent hit clip over each v2 block that has `click`, with its own existing
  handler mechanism (the one the tree's marks use, SetHandler Widget.cpp:2731; onPress and onRelease), and on release
  calls `click(pressX, pressY, releaseX, releaseY, user)` with both points inside the content clip. The host cancels a
  press (no call) when the release is outside the block or outside the column's visible viewport, when the column
  scrolled, the block was redrawn or layout mode changed between press and release, and never calls it in layout mode
  (the overlay owns clicks then). The add-on keeps the rects it drew in its last draw and acts only when press and
  release fall in the same button rect. (Chosen over add-on-owned AS2 handlers on the lent clip: those need the
  add-on's own GFx function objects living past the call, which the lent-clip rule forbids, and their behaviour under
  the layout overlay is unchecked.)
- Tabs: v2 `tabs` bitmask (spells, powers, shouts, active effects; 0 = every tab, the v1 behaviour) - a block is
  hidden on the other tabs the way the built-in tree/progress/nodecard are (Layout::NoTreeTabHidden). Each add-on
  picks its own tabs (user, 2026-10-29: "지금 추가하는 homl이랑 sr블록은 주문탭에서만 보여야하지만 추후에 다른
  에드온은 다른텝에서 보여야 할 수도 있어"): the SR block and the later HoML block take spells only; the mask must
  let a future add-on take any other set, Powers / Shouts / Active Effects alone included.
- `kVersion` 2. The header comment documents v2; Draw.h is not shipped with it (the add-on carries its own copy).

Reserved for the HoML block (not built here, listed so v2 is not mistaken for enough): add-on calls into the host
(select a spell in the list, load its 3D), hover and tooltips outside the block, key and pad input,
shared text-fitting helpers, and keeping a player's place for the old built-in ids `tree`/`progress`/`nodecard`.

### 2. New plugin MagicMenuRewrite_SpellResearch (plugins/SL_MagicMenu_SpellResearch)

- kPostLoad: listen for MagicMenuRewrite's result and for SR Prisma's replies ('SRPR', sender "SpellResearchPrismaUI");
  SR Prisma usable = that RegisterListener succeeded (SR Prisma is loaded). Superseded (final review R1): the design's
  `GetPluginInfo` version gate (0.2.1+) is not used - it calls a slot of the SKSE interface that older SKSE builds may
  not have, and the SKSE release that added it could not be established; an SR Prisma before 0.2.1 never answers
  'SRPO', which ends in "does not respond" or the 5 s drop. Heart of Magic is detected the same way.
- kDataLoaded: `TESDataHandler::LookupForm` the three FormLists and `_SR_GlobalImporting` 004C8D /
  `_SR_GlobalIsSetup` 00624F in "SpellResearch.esp"; missing plugin or a list of the wrong shape -> log once and
  register nothing. Then register block id "SpellResearch.archetypes", title from the language file, registered height
  134 (title strip + three archetype rows + link row + reason line; the list's; the line layout and reason-only states ask
  for less with 'SLMH' - 2b, 2c), `drawSelection`, `click`. The menu sink is attached only after the block is accepted.
- Shared data (HasForm walks forms + scriptAddedTempForms without a lock; the Librarian and SR Import write them on
  the game thread): never walk SR's lists in draw. A snapshot (FormID -> tier, archetype bitset; 60 x 5 knowledge
  values) is built in an SKSE task on the game thread when the magic menu opens (a MenuOpenCloseEvent sink), "not
  ready" while importing or before setup, and published as an immutable object by an atomic shared_ptr swap that draw
  reads; when it lands the add-on sends 'SLMD'. O(total list entries), once per open.
- Buttons: 'SRPO' `{version 1, requestId, what}` (1 experience window, 2 journal, 3 alembic; SRPrismaAPI.h copied
  from SR Prisma's committed HEAD f76e584 into the plugin, unchanged). On click (UI thread): set the pending
  requestId (atomic) before Dispatch; Dispatch true -> close the magic menu (UIMessageQueue kHide); false -> clear
  pending, reason line, 'SLMD'. The reply ('SRPR') arrives on whatever thread SR Prisma dispatches from (its game
  task, or the caller's thread without a task interface, main.cpp:2333-2343, 2695-2699): the listener checks size
  and version, copies the reply by value, ignores a requestId that is not the pending one, and hands the copy to an
  SKSE task that clears pending and, for Busy/Refused/BadMessage, shows the HUD notification. A pending request
  with no reply for 5 s is dropped (SR Prisma's own wait is 2 s). One request in flight at a time.
- Self-test (ini `bSelfTest`): at the first menu open logs PASS/FAIL for list sizes (60 / 5 / 60), Flames
  (Skyrim.esm 012FCD) filed under Fire and Destruction (SKIP when SR's lists are empty), snapshot ready / not ready
  (SKIP), a Librarian (script-added) spell found when HoM is installed (SKIP otherwise), and that the drawn id equals
  the card's id for each redraw.
- Offline checks (tools/magic-menu-block-api-check, no game): registration parsing (real v1 bytes in the old layout,
  a v2 message cut short, version 3 refused, v2 without either draw refused, the tabs mask), the host's press/release
  cancel rules as a pure function, and the add-on's pure parts (archetype grouping order, list and line layout and wrapping to a width, icon data bounds,
  press+release -> button (same rect only), reply -> notification text, requestId matching and the 5 s drop).
- dist: dist/MagicMenuRewrite_SpellResearch/ (DLL, PDB, ini, lang/en.json and ko.json), made by every build (2a, 2d) until
  the FOMOD task.

### 2a. As built (Task 2, 2026-10-29)

Files: plugins/SL_MagicMenu_SpellResearch/src - `Main.cpp` (load, messages, registration, the menu-open sink),
`Settings.cpp` (own ini `bSelfTest`, `ResolveLayout()`, `ResolveLanguage()`), `Lang.cpp`, `SrData.cpp` (lookups, shape
checks, the snapshot, the first-open self-test), `BlockDraw.cpp` (draw, click, SRPO/SRPR), and the pure
`SrBlockRules.h` that `tools/magic-menu-sr-block-check` runs; `BlockApi.h` and `Draw.h` are copies of the host's,
`SRPrismaAPI.h` is SR Prisma HEAD dd084c3's file unchanged (the same API as f76e584). Decisions the design left open:

- Names: `lang/en.json` keys `archetype.<i>.name` (SR Prisma's ARCHETYPES) and `archetype.<i>.short` (targeting and
  technique without " Targeting" / " Techniques"; elements, casting and schools as the name) - see "Strings and language".
  The pure parts of the strings and the ini (BOM skipped, `ReadIniValue`, `ParseLayout`, `ParseLanguage`,
  `ArchetypeName`) are in `SrBlockText.h`, also run by the check tool.
- Height 134 (content 96 = three 18-unit archetype lines, 8 gap, an 18-unit link row, a 16-unit reason line; plus the
  host's 22 title strip and 2 x 8 padding), the same for both layouts. List: one line per group, wrapping inside the
  archetype column; line: wrapping to the width. Neither passes three lines: what does not fit is left out and "..." ends
  the third line. A name longer than a whole line is cut with "...".
- "?" cases: SR does not file the spell, its lists are empty, not a spell (a power, an ability), no element / targeting /
  technique filed, and a spell in archetype lists but in no tier list (every archetype "?", reason "in no tier list").
- The snapshot is built at each magic menu open; the previous one stays published until the new one lands (no "getting
  ready" flash on every open), then 'SLMD'. It is dropped at kPreLoadGame and kNewGame (another character's data never
  shows): the first open after a load says "getting ready" for a frame.
- A Busy/Refused/BadMessage reply that arrives while the magic menu is still open (SR Prisma answers an early refusal -
  combat, a missing item - before it waits for the close) is held and shown when the menu closes.
- Icons: `ArchetypeIcons.h` now carries `Icon::frame` (tools/sr-icons/convert.mjs: the glyph converted alone is the rest of
  the subpaths; one frame per group); a dot subpath (two equal points) is drawn as a small filled polygon as wide as the
  stroke (a zero-length AS2 line draws nothing); strokes use Flash 8 `lineStyle(..., "round", "round")`.
- dist: `dist/MagicMenuRewrite_SpellResearch/SKSE/Plugins/` (DLL, PDB, ini, `MagicMenuRewrite_SpellResearch/lang/en.json`) is
  rebuilt by every build (custom target `SL_MagicMenu_SpellResearch_dist`), from `plugins/SL_MagicMenu_SpellResearch/dist/`.
- Self-test: `[selftest-sr] lists|snapshot|flames|outside|not-filed PASS|FAIL|SKIP` and `DONE` at the first open.
  `lists` counts the three lists' entries and each knowledge list's globals at that moment (and is written as FAIL, with
  DONE, at kDataLoaded when Init refuses SR's forms; SKIP when SpellResearch.esp is not loaded). `outside` replaces the
  "Librarian spell" check: up to 3 spells from plugins other than SpellResearch.esp and the game's masters that the
  snapshot files (lowest FormIDs) are checked against the engine's own `BGSListForm::HasForm` on all 60 archetype and 5
  tier lists; `not-filed`: the first spell the snapshot does not file has HasForm false on all of them (SKIP when none
  qualify). Per redraw (up
  to 16 a menu) `[selftest-sr] drawn PASS` when the drawn form is SkyUI's list selection, `SKIP` when the card shows
  another (a tree click) or the selection is unreadable - it cannot tell a wrong draw from a tree click, so it never FAILs.

### 2b. The line layout's height fits the spell (2026-10-30)

User: in the line layout (C) the block itself should get smaller - "주문마다 내용에 맞춤" (fit each spell's content, the blocks
under it moving by a line when the spell changes). The list layout keeps its fixed height.

- Host: Block API v2 message `kMessageHeight` 'SLMH', data `BlockApi::HeightRequest { apiVersion, structSize, id, height }`
  (kVersion stays 2; an older host ignores it). Any thread, queued per id (the last height wins, 64 ids, like 'SLMD'),
  applied on the next UI frame: the columns are arranged again (`Rearrange` - the blocks below move, the scroll is
  clamped, the block is drawn again at its size with its hit clip). Refused over the registered height (the maximum);
  every menu opens at the registered heights; the layout mode lays every add-on out at its registered height (it
  rearranges on entering and leaving when an add-on asked for another); MagicMenuRewrite_Layout.json is not touched. Pure
  parts in BlockRules.h (`ReadHeight`, `JudgeHeight`, `LaidOutHeight`, `Enqueue`), checked by magic-menu-block-api-check;
  the column restacking by magic-menu-layout-check (`AddonHeightChange`).
- Add-on: `SrBlock::ContentHeight(kind, lines, hasLinks, hasReason)` - list: kContentH (96); line: the archetype lines
  (a reason-only state counts as one), the link row and the reason line when drawn (without links the reason line takes
  their place): 58 for one line with links, 74 with a reason line too, at most 92. A reason-only state ("?" and the
  reason line: not ready, not a spell, lists empty, unknown spell, no archetypes) is placed and sized by the line
  layout's rules in the list layout too (user 2026-10-30, `SrBlock::PlacedKind`), so the list block shrinks from 134 to a
  one-line block's 112 instead of leaving the fixed height's gap; the list with archetypes keeps 134. Draw asks for that
  height (plus the host's 38 of title strip and padding) when it differs from the last one it asked for
  (`SrBlock::HeightRequests`), reset at each menu open and when the chosen layout changes - so the host's redraw at the
  new size, and the layout mode's redraw at the registered size, send nothing. The registered height stays
  kBlockHeight (134). The host applies a request made by the frame's second draw pass (after the selection handling)
  in the same frame: it lays the columns out again and draws the block once more, so a block drawn for a new spell is
  never shown past its box.

### 2c. One spacing scale and one left edge (UI checklist rule 1, 2026-10-30)

Spacing on 4 / 8 units: icon -> its name 4 (was 3), archetypes -> links 8 (was 6; the line layout's halved gap follows:
links at 40 / 49 / 58 after 1 / 2 / 3 lines), between links 8 (was 10); rows stay 18 (11-point text and a 14 icon).
Heights now: list 134 (content 96); line 1 / 2 / 3 lines with links and no reason 96 / 105 / 114 (content 58 / 67 / 76),
with links and the reason line 112 / 121 / 130; reason-only (any layout, links and reason) 112. One left edge
(`SrBlock::kTextInset` = 2, the AS2 text field's gutter): the host draws its title field at the content's left edge, so
the title's first letter is 2 units in; every line of the block starts there - the list's labels, the line layout's
first icon, the "?" line, the links and the reason line (the code placed text fields 2 units left to cancel the gutter,
so the list's labels started at 0, not at the title's letter, before this). The usable width is 2 units less.

### 2d. SR Prisma's language tables (2026-10-30)

SR Prisma's phase 3 shipped its translation tables (its docs/settings-tab.md "Language files", "Other plugins"):
`Data/PrismaUI/views/SpellResearch/lang/<code>.json` (en, ko), flat JSON objects of strings, BOM accepted, an empty
value counts as missing. The block now reads them (`Lang.cpp`, pure merge in `SrBlockText.h`: `Tables`, `LookUpShared`,
`ArchetypeName`, `GroupLabel`, `UnnamedArchetypes`):

- Archetype names (`archetype.<i>.short`, else `.name`) and the three group labels (SR's `page.group.elements` /
  `page.group.targeting` / `page.group.techniques`, the block's `group.element` / `.targeting` / `.technique`): SR <lang>,
  SR en, the block's own <lang>, the block's own en, the key (a warning once per key when it falls to English or the
  key).
- Everything else (title, "?", "...", links, reasons, notifications): the block's own <lang>, own en, the key.
- Without SR Prisma's files everything comes from the block's own files, as before.
- The block's own `lang/ko.json` ships (title 연구 원형, links 연구 일지 / 증류기 / 경험치 책 - SR Prisma's own window names,
  group fallbacks 요소 / 대상 지정 / 기법, the 14 reviewed reason and notification lines, and "?" / "..." as in English);
  it holds no archetype names (SR Prisma's ko.json does). The block's en.json keeps its short English names as the last
  fallback; its group fallbacks are now SR's English (Elements / Targeting / Techniques). The unused `tier.*` keys are gone.
- Self-test: `[selftest-sr] lang <code> PASS|FAIL - SR table <code> keys n, SR en keys n, own <code> keys n, own en keys n;
  shown archetypes named by SR <code> n, SR en n, own files n` (FAIL when an archetype the block can show has no name in
  any table) and a line with the three group labels and their sources.
- dist: the whole `lang/` folder is copied (en.json, ko.json).

### 2e. Final whole-branch review (2026-10-30)

- R1: SR Prisma and Heart of Magic are detected by RegisterListener, not GetPluginInfo (see "Changes / 2").
- R2: a waiting open request is dropped at kPreLoadGame / kNewGame with the snapshot and the held notice.
- R3: the held notice's "menu open" is the block's own flag, set by the menu sink under the same lock as the store.
- I1: the language self-test fails when SR Prisma's tables are installed but its table of the language named no
  archetype (`SrBlock::LanguageVerdict`), and skips when they are not installed.
- I2: the language is resolved again at each menu open (`Lang::Refresh`); the four tables are one immutable bundle
  swapped atomically.
- I3: the menu sink is attached only after the block is accepted.
- T2: without links the reason line takes the link row's place in the list layout too; the list's content height is
  then 78 (block 116).
- The language file reader is `SrBlock::ReadTableFile`, which the check tool runs on real files (BOM, invalid JSON, a
  JSON array, over the size limit, missing). Settings.cpp's ini file reading, the game INI setting and SrData's list
  walk are covered by the in-game self-test only; its tier comparison uses the snapshot's own lowest-tier rule (it
  checks the lists' reading, not the rule).

### 2f. Before SR's Import: the reason line alone (2026-10-30)

User-approved: while SR's Import has not run in the save (`_SR_GlobalIsSetup` 0) or is running (`_SR_GlobalImporting`
> 0) the block shows only its reason line - no "?" line and no links (SR Prisma's windows do not open before the Import
either: `_SR_QuestScript` UseEquippable asks to import). `SrBlock::Decide` returns `Show::kNotice` for those two states;
`ContentHeightOf` gives it the reason line's 16, so the block asks ('SLMH') for 54 (title strip 22 + 16 + padding 2 x 8)
in both layouts. The other reason-only states (getting ready, not a spell, lists empty, unknown spell, no archetypes)
keep their "?" line and links (112).

## Rulings carried from the 2026-10-27 GPT design review

ABI freeze (above), the card's spell rather than `g_menu.sel.id`, the game-thread snapshot plus 'SLMD', no block
when nothing is chosen. All four stay.

## Not in scope

SR Prisma's skald-icons.png bitmap atlas (loading bitmaps into the host movie is its own question, and its distribution
needs SR's author's agreement) - the block draws the same icons as vector point data instead (ArchetypeIcons.h, 2a);
the HoML block and the FOMOD (next task); per-frame updates; knowledge numbers in the block. (Languages: done in 2d.)

## Verification

Offline: the check tool above, BuildRelease, the existing magic-menu-*-check tools stay green. In game (the user):
the block shows and follows the card; "?" vs names; the buttons open SR Prisma's windows after the menu closes - this
needs SR Prisma 0.2.1 installed, whose SRPO path its own session has not seen in game yet; the self-test log lines.
