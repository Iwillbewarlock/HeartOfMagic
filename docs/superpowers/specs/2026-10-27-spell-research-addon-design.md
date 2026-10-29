# Spell Research add-on block - design

Date 2026-10-27, revised 2026-10-29 for the Spell Research Prisma UI revamp (D:/DEV/SpellResearchPrismaUI, 0.2.1,
not released). Goal: the first add-on block of SL_MagicMenu, shipped as its own DLL that a later SL_MagicMenu FOMOD
offers as an option (user, 2026-10-29: "포모드로해서 homl용 블록이랑 스펠리서치 프리즈마용 블록을 선택해서 설치가능하게
할거고 추후에 만들 에드온들도 그런식으로"). The HoML block (moving HoM's windows out of SL_MagicMenu) and the FOMOD are
the next task (C:/Users/qtuna/Desktop/Claude outputs/magic-menu-no-hom-brief.md); this one builds the API they start from.

## What the player sees

(The Korean wording below is the intent; the shipped text is the English file until SR Prisma's languages land - see
"Strings and language".)

A block "연구 아키타입" (default: end of the right column, moved like any block), shown while a spell is chosen (the
columns are hidden when nothing is, Widget.cpp:3376-3388, 3417). For the spell the menu's card shows:

- Spell Research knows the spell (it is in one of SR's archetype spell lists): the spell's SR tier (초보자..달인) as a
  small label, then one chip per archetype the spell belongs to - school first, then elements, casting, targeting,
  techniques (SR's index order inside each group). Chips are text (a thin kPanelEdge frame, the polish of 2026-10-29)
  and wrap to the column width.
- A chip shows the archetype's name when the player has any knowledge of that archetype at the spell's tier
  (its tier GlobalVariable > 0, the rule SR Prisma's experience window also uses since 0.2.0), otherwise "?".
  (SR keeps no per-spell "researched" record - re-checked against SR Prisma 0.2.1: still none - so "researched" is
  read as "the player has knowledge in that archetype at that tier"; the reason line says so for "?" chips.)
- SR does not know the spell, or SR's lists are empty (Import never run), or the selection is not a spell: one "?"
  chip and a one-line reason. SR still setting up or importing: "Spell Research 준비 중" (snapshot not ready).
- A row of three text buttons under the chips: "연구 일지", "증류기", "연구 경험" - shown only when
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
  Prisma's `ARCHETYPES` (view/index.html:966) has the same order.
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

- Every player-visible string of the block (60 archetype names, 5 tier names, 3 buttons, title, reason lines,
  notifications) comes from a per-language JSON file, `SKSE/Plugins/SL_MagicMenu_SpellResearch/lang/<code>.json`,
  read at kDataLoaded; a missing key falls back to the English file, a missing English key to the key itself (and a
  log line).
- This task ships `en.json` only (archetype names as SR Prisma's `ARCHETYPES`, view/index.html:966; tiers Novice ..
  Master). The language code is chosen by one function, `ResolveLanguage()`, which returns "en" until SR Prisma's
  setting exists; it is the one place to connect when SR Prisma publishes the key or API. When SR Prisma's translation
  tables exist the block reads the same names (or ships copies generated from them), so a name never differs between
  the block and SR Prisma's window.
- Korean names are SR Prisma's decision (its session reconciles them with TAKEALOOK's existing Korean for Spell
  Research, Mod-Management Case 070, and the glossary). An Argon draft of the 60 names is kept in this session's notes
  only as input to that work. The word "아키타입" is the user's own term for SR's archetypes.

## Changes

### 1. SL_MagicMenu: Block API v2

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
  Widget.cpp:2434/3625-3633, else the committed list selection, else 0). SL_MagicMenu compares it once per UI frame
  after its own selection and tree-click handling and redraws v2 blocks when it changed (never from inside a draw).
- Redraw request: message add-on -> SL_MagicMenu `kMessageRedraw` 'SLMD', data = the block's final id; the redraw
  happens on the next UI frame (ignored while the menu is closed). Used when the add-on's snapshot lands and after an
  SRPO reply.
- Click: SL_MagicMenu puts one transparent hit clip over each v2 block that has `click`, with its own existing
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

### 2. New plugin SL_MagicMenu_SpellResearch (plugins/SL_MagicMenu_SpellResearch)

- kPostLoad: listen for SL_MagicMenu's result and for SR Prisma's replies ('SRPR', sender "SpellResearchPrismaUI");
  SR Prisma usable = `SKSE::LoadInterface::GetPluginInfo("SpellResearchPrismaUI")` (CommonLibSSE-NG Interfaces.h:157)
  is not null and its version is 0.2.1 or later, checked at kPostPostLoad.
- kDataLoaded: `TESDataHandler::LookupForm` the three FormLists and `_SR_GlobalImporting` 004C8D /
  `_SR_GlobalIsSetup` 00624F in "SpellResearch.esp"; missing plugin or a list of the wrong shape -> log once and
  register nothing. Then register block id "SpellResearch.archetypes", title "연구 아키타입", fixed height (title
  strip + tier label + two chip rows + button row + reason line), `drawSelection`, `click`.
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
  cancel rules as a pure function, and the add-on's pure parts (archetype grouping order, chip wrapping to a width,
  press+release -> button (same rect only), reply -> notification text, requestId matching and the 5 s drop).
- dist: a hand-made dist/SL_MagicMenu_SpellResearch/ (DLL, ini) like dist/HoM-MagicMenu-Proto until the FOMOD task.

## Rulings carried from the 2026-10-27 GPT design review

ABI freeze (above), the card's spell rather than `g_menu.sel.id`, the game-thread snapshot plus 'SLMD', no block
when nothing is chosen. All four stay.

## Not in scope

The archetype icons (SR Prisma's skald-icons.png atlas: loading bitmaps into the host movie is its own question, and
its distribution needs SR's author's agreement), the HoML block and the FOMOD (next task), languages other than
English until SR Prisma's language setting and tables exist, per-frame updates, knowledge numbers in the block.

## Verification

Offline: the check tool above, BuildRelease, the existing magic-menu-*-check tools stay green. In game (the user):
the block shows and follows the card; "?" vs names; the buttons open SR Prisma's windows after the menu closes - this
needs SR Prisma 0.2.1 installed, whose SRPO path its own session has not seen in game yet; the self-test log lines.
