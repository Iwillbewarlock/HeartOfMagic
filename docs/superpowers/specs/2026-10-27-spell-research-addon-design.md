# Spell Research add-on block (experiment) - design

Date 2026-10-27. Goal: prove that an outside SKSE plugin can add a block to SL_MagicMenu's magic menu, with a
block that follows the selected spell. Not a release feature.

## What the player sees

A block "연구 아키타입" (default: end of the right column, moved like any block). For the selected spell:

- Spell Research knows the spell (it is in one of SR's archetype spell lists): one chip per archetype the spell
  belongs to, school first, then elements, casting, targeting, techniques (SR's index order inside each group),
  and the spell's SR tier (초보자..달인) as a small label.
- A chip shows the archetype's name when the player has any knowledge of that archetype at the spell's tier
  (its tier GlobalVariable > 0), otherwise "?". So a spell never researched shows only "?" chips.
  (Ruling: SR keeps no per-spell "researched" record - checked: none of SpellResearch.esp's 477 FormLists or its
  scripts store one - so "researched" is read as "the player has knowledge in that archetype at that tier".)
- SR does not know the spell, or SR's lists are empty (the player never ran SR's Import), or the selection is not
  a spell: one "?" chip and a one-line reason.
- Nothing chosen: the block shows its title and "주문을 고르면 표시됩니다".

No click action (SR Prisma UI's experience window has no outside open path: OpenWindow/ToggleWindow are internal to
SpellResearchPrismaUI.dll, only its Y key opens it).

## Spell Research data (SpellResearch.esp, checked with houseCARL and SR's winning scripts)

- `_SR_ListSpellsArchetypes` 000D9E: FormList of 60 FormLists, one per archetype, the spells in it. Index order =
  `_SRU_Utility.GetArchetypeString`: 0-1 casting, 2-35 elements, 36-40 schools Alteration..Restoration,
  41-44 targeting, 45-59 techniques (each sub-list's EditorID matches, e.g. [12] _SR_ListSpellsElementFire).
- `_SR_ListAllSpells` 001343: FormList of 5 FormLists, spells by tier, [0] Novice .. [4] Master
  (`_SRU_Utility.GetSpellRank`).
- `_SR_ListGlobalsArchetypes` 00133D: FormList of 60 FormLists of 5 GlobalVariables, [0] Novice .. [4] Master
  (`_SR_QuestScript.AdvanceSingleSkill`). Values only grow.
- The Librarian adds its catalog spells to these lists with AddForm at every load and after SR's Import
  (HeartOfMagic src/librarian/LibrarianSpellResearchPatch.cpp), so unpatched spells are covered; `HasForm`
  sees both record and script-added entries.
- Read only. The add-on never writes SR's data.

## Changes

### 1. SL_MagicMenu: Block API v2 (selection)

v1 draws a block once per menu and on size changes. v2 adds, at the end of `Registration` (v1 fields stay first and
unchanged; `structSize` tells versions apart):

```cpp
using DrawSelectionFn = void (*)(RE::GFxMovieView*, RE::GFxValue& clip, float w, float h,
                                 RE::FormID selected /*0 = nothing*/, void* user);
DrawSelectionFn drawSelection = nullptr;  // v2: when set, used instead of draw
```

- Called at the same moments as v1 draw plus whenever the menu's selection changes by the menu's own selection rule
  (click or keyboard/pad move; hover does not count), into a new empty clip each time.
- `kVersion` becomes 2; a v1 registration (structSize == v1 size) keeps working unchanged.
- The selection passed is the same id the menu's own windows use (`g_menu.sel.id`).

### 2. New plugin SL_MagicMenu_SpellResearch (plugins/SL_MagicMenu_SpellResearch)

- At kDataLoaded: `TESDataHandler::LookupForm` the three FormLists by local id in "SpellResearch.esp"; missing
  plugin or a list of the wrong shape -> log once and register nothing.
- Registers block id "SpellResearch.archetypes", title "연구 아키타입", fixed height (one title strip + tier label
  + up to two chip rows + reason line).
- drawSelection: SpellItem only; tier = first tier list that HasForm; archetypes = every index whose list HasForm;
  knowledge = GlobalVariable value at (archetype, tier). Chips wrap to the column width.
- Korean archetype names: a table in the plugin (60 entries), glossary terms first, Argon naturalness review.
- Self-test (ini bSelfTest): at the first menu open logs list sizes (60 / 5 / 60) PASS/FAIL, and for the vanilla
  spell Flames (Skyrim.esm 012FCD) whether SR files it under Fire and Destruction (PASS / FAIL / SKIP when SR's lists
  are empty, i.e. Import not run).

## Rulings after the GPT design review (2026-10-27, all four findings checked in the code and accepted)

1. ABI (BlockRegistry.cpp:47-56, 65; BlockApi.h:83): `RegistrationV1` is frozen as its own struct
   (`kRegistrationV1Size = sizeof(RegistrationV1)`), `Registration` (v2) = the v1 fields then `drawSelection`.
   apiVersion 1 or 2 accepted; the fields copied are the ones `min(structSize, dataLen)` covers for that version,
   into a zero-initialised v2 struct; v1 needs `draw`, v2 needs `draw` or `drawSelection`. Result stays v1-shaped.
   Tests: real v1 bytes (old header layout), a v2 message cut short, version 3 refused.
2. Which spell: not `g_menu.sel.id` alone. The block gets the spell the menu's own card shows (the tree card when a
   tree click picked a spell not in the list, Widget.cpp:2434/3625-3633, else the committed list selection, else 0).
   SL_MagicMenu compares that id once per UI frame after its own selection and tree-click handling and redraws
   v2 blocks when it changed (no redraw from inside a draw call; a change made during a draw is seen next frame).
3. Shared data (HasForm walks forms + scriptAddedTempForms without a lock; the Librarian and SR Import write them):
   the add-on never walks SR's lists in draw. It keeps a snapshot (FormID -> tier, archetype set; 60 x 5 knowledge
   values) built in an SKSE task (game thread, where those writes happen) when the magic menu opens; while SR is
   importing (`_SR_GlobalImporting` 004C8D != 0) or before it is set up (`_SR_GlobalIsSetup` 00624F == 0) the
   snapshot is "not ready" and the block says so. API v2 adds a message add-on -> SL_MagicMenu, 'SLMD'
   (`kMessageRedraw`, data = the block id), so the add-on asks for a redraw when its snapshot lands; the redraw
   happens on the next UI frame. Building the snapshot is O(total list entries), once per menu open.
4. Nothing chosen: the columns are hidden then (Widget.cpp:3376-3388, 3417), so the "주문을 고르면" line is dropped;
   the block shows only with a chosen spell, like the other blocks.

Self-test additions: snapshot ready / not-ready (SKIP) per menu; A -> B -> tree-only spell C follows the card (the id
drawn equals the card's id each time); a script-added entry (a Librarian spell) is found. The "?" rule is shared
knowledge per archetype and tier, not a per-spell history (SR keeps none) - said in the block's reason line.

## Not in scope

Click actions, per-frame updates, knowledge numbers in the block, other SR data.
