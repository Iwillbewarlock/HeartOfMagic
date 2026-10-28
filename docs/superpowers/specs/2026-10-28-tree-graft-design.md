# New spells join the existing tree (graft) - design

Date 2026-10-28. Branch: feature/h1-core-state. Status: design, approved decisions below; not built.
Reviews: a builder check and an adversarial design review (reviewer agents; GPT was over its usage limit
until 2026-11-02), both against the code; their findings are folded in.

## Why

A player who adds a magic mod mid-save today has one way to get its spells into the tree: Scan, Build and
Apply on the Scan tab. That rebuilds every school, and the tree they had is scattered: different parents,
different places. The author: "이미 만들어둔 트리가 흩어지는게 문제" - the tree already built falls apart.

What scatters it (code, 2026-10-28):
- The build is not repeatable: the panel sends no seed (`classic/classicBuildRequest.js:29-38`), so the C++
  builder seeds from the clock (`TreeBuilderClassic.cpp:214-219`, `TreeBuilderCore.cpp:89`).
- One `std::mt19937` is shared by the five schools in `unordered_map` order (`TreeBuilderClassic.cpp:219,238,
  255`): one more spell in one school shifts every later school's draws.
- The panel layout picks parents again and places nodes breadth-first on a grid (`classicLayout.js:340-605`;
  `classicTreeOutput.js:6-9`: the C++ parents are dropped), swaps neighbours (`classicRadialOrder.js`), and the
  declutter step scales everything by 1.35 and pushes overlaps apart (`LayoutDeclutter.cpp:352-358`).

What makes a fix possible: the saved tree (`Data/SKSE/Plugins/SpellLearning/spell_tree.json`, one file for every
save) keeps each node's `x`, `y`, `tier` (from 1, `TreeBuilderCore.cpp:64`), `children`, `prerequisites`,
`softPrereqs`/`softNeeded`, `hardPrereqs` (locks only), `requiredXP` and `persistentId`
(`classicTreeOutput.js:143-169`); positions are read back as they are (`treeLoad.js:134-135,345-355`). Progress
is per spell in the co-save, independent of the tree (`ProgressionManagerSerialization.cpp:55-65`).

## Goal

When the load order brings spells the tree has never seen, they join the existing tree automatically, each in
the school and branch it fits, and **no existing node moves or changes** (it may gain a child link).

## Non-goals

- Re-laying out or re-parenting any existing node; making the full rebuild repeatable (a separate fix).
- Per-save trees; Spell Research or perk-overhaul changes.
- Bringing back a spell the player restores later (blacklist removal, Keep, a plugin re-enabled in the
  whitelist): with the seen list kept (decision 2) such a spell joins at the next full rebuild, as today.

## Anti-goals

- An existing node moves, loses a link, or changes tier.
- A spell left out before comes back on its own: gate "out", blacklist, plugin whitelist off, a copy, a tome
  nothing hands out, quest-only, a node the player deleted in edit mode.
- The College of Winterhold's novice spells (its quests hand them out, its merchants sell them) are taken for
  quest-only.

## Decisions (the author's)

1. **Automatic.** No preview; the player is told afterwards.
2. **A seen list decides what is new** (2026-10-28, chosen over re-judging every run): a spell the tree's build,
   or an earlier graft, has seen is never a candidate again, whatever happened to it.
3. **Quest-only spells are left out of the tree** (full build too) and listed in the blacklist window, where the
   player can put one back. Quest-only = a tome handed out **only by quest records** (a quest's script or
   alias); a tome any other way reaches (a merchant, leveled list, container, placed, recipe, SPID/KID config,
   another record's script) is not quest-only. Before release the quest-only list of the author's load order is
   dumped for the author to look over.
4. **A mod turned off and on again comes back in its place.** Nodes of a missing plugin stay in the file (as
   today, TreeRemap drops them in memory only, `UIManagerTreeState.cpp:249`); the graft never removes or
   re-parents them. No automatic orphan re-attach.
5. **A renamed/merged/compacted plugin keeps its spells in place**: when a node's `persistentId` no longer
   resolves, it is found again by editor id + school (needs powerofthree's Tweaks; without it, as today).

## The design

### 1. The seen list

- **Written at Apply** of a build: the panel sends, with the tree, the `persistentId` of every spell of the scan
  the build used, kept or left out; C++ stores them in the tree record `spell_tree_loadorder.json`
  (`UIManagerAutoBuild.cpp:28-37`; `WriteRecord` keeps unknown fields from now on, `:80-91`). Import, edit-mode
  saves and Clear do not touch the record (as today); an Import also clears the seen list (it belongs to
  another build) - the next graft then takes the baseline below.
- **Grown by a graft**: every candidate it judged (grafted or left out by the rules) is added.
- **Baseline when there is none** (a tree from before this feature, or after an Import): the spells of the
  catalog the last scan left (`spell_catalog.json` before the scan at game start replaces it,
  `AutoScan.cpp:613`) are seen; so a mod added just before the first run is still grafted. No catalog either (or
  none the scan record says was another load order's): nothing is grafted and no baseline is saved that run
  (logged); the seen list is seeded at the next load order change, from the catalog this run's scan left.
- **Only from a whole scan**: a graft runs on a scan that marked everything (`incomplete` empty) with the tome
  reach pass `ready` (`TomeReach::OutcomeKey()`); otherwise it waits for the next start. This keeps a spell
  whose tome marks were missing from being judged and frozen in the seen list.
- **Never candidates**: a spell already in the tree by formId or persistentId (an edit-mode spawn), a spell
  whose duplicate group already has a node (`SpellScannerDuplicates.cpp:30-39`: a new mod's copy that becomes
  the kept record), a spell without a persistentId (`SpellScannerFormId.cpp:42`).
- Known limit, logged: Spell Research's own pairs exist only after a save loads (`autoBuild.js:55-58`); the main
  menu scan has the JSON pairs and Spell Research's plugin part, so with neither a tome Spell Research teaches
  only through a save is judged without its pair.

### 2. Quest-only

- The tome reach pass records, per tome, how it was reached: a record field, a non-quest script property, a
  quest record (script or alias), a distribution config, a damaged plugin (`TomeReachRecords.cpp:145-185`,
  `TomeReachPass.cpp:27-49`; today only the totals and the unreachable set leave the pass, `TomeReachPass.h:23-32`,
  `TomeReach.cpp:112-117`). Config and damaged reaches count as handed out.
- A spell is quest-only when every tome of it was reached only through quest records. The scan marks
  `questOnly: true`; the tree's candidate rule (`proceduralTreeBuilder.js:22-66`) leaves it out; the blacklist
  window lists it as a group with GateReview's "Keep" (`gateReview.js`).

### 3. Where a grafted spell goes (C++, works without the panel)

- **School and tier** as the build: skill level, or with `tiers_by_cost` where its cost falls among the school's
  existing nodes (`TreeBuilderClassic.cpp:48-78`); existing nodes keep theirs.
- **Parent**: the build's scoring (`FindBestClassicParent`, `TreeBuilderClassic.cpp:98-204`), moved out of its
  file and run without the random jitter, over the school's existing **live** nodes (after TreeRemap: a node of a
  missing plugin is never a parent) under the tree's child cap. Its inputs are rebuilt per school from the
  current scan for the existing nodes plus the new spells (spell data, themes, mod tags, the similarity matrix,
  type grouping, `TreeBuilderSimilarity.cpp:69`, `TreeBuilderTypeGrouping.cpp:70-97`); they only score, nothing
  of an existing node changes. A distance term favours parents with free room near them, so a link does not
  cross the school. Several new spells in one school go lowest tier first (one may hang under another).
- **Position**: existing nodes are fixed; new nodes start beside their parent, outward, inside their tier's ring
  and the school's sector (`classicTreeOutput.js:185-193`, `classicLayout.js:404-407,555-556`), and the C++
  declutter settles them with every existing node `fixed` and without its spread step
  (`LayoutDeclutter.cpp:199-204,253,354-357`, `LayoutLineClear.cpp:83`), so neither nodes nor link lines overlap.
- **Links and XP**: the parent's `children`; the new node's `prerequisites` and `softPrereqs` with `softNeeded: 1`
  as built nodes have (`classicTreeOutput.js:143-151`; hard is for locks, `schoolBridges.js:66-68`); no
  `requiredXP` (the tier's number, `RequiredXPTable.h:22`); `persistentId` set, and the scan's `editorId` (when
  not empty) and `traits`, as a build bakes them (`schoolBridges.js`); no locks.
- **A formId the file already holds**: a new spell whose current formId a node of the file keeps (a missing
  plugin's node, which Decision 4 keeps with its saved formId, or a stale id held) is deferred: not grafted, not
  seen. It waits for as long as that node keeps the id (its mod back, the node deleted, the tree built again), not
  one more start; each such spell is logged with the node that holds its formId. Re-keying such nodes' saved
  formIds is a follow-up.

### 4. When it runs, what the player sees

- **Trigger**: at the main menu, after the scan at game start, when the tree record's load order differs from the
  current one (not the scan record: a panel scan alone must not hide a graft, `AutoScan.cpp:489-492`,
  `UIManagerScanner.cpp:260-263`). It follows "Build Spell Tree at Start" (Off: no graft, the old notice stays).
- **Writing**: on the game thread: the file's own tree (not the remapped copy) gains the new nodes; `.bak` first
  (`UIManagerTree.cpp:261`); the record gets the new load order and seen list, so the "rebuild" notice
  (`autoBuild.js:116-130`) no longer fires for it. Then, same session: `ApplyTreePrerequisiteEntries` and
  `SyncRequiredXPFromTree` again, and the panel told to reload its tree (it holds a copy from
  `onPrismaReady`, `treeLoad.js:230-239`, which a later edit-mode save would write back).
- **Told**: a corner message at the next save load ("N new spells joined the spell tree", every panel language).
- **Fails** (tree unreadable, no school for a spell): nothing written, logged; the old notice applies.
- The scan-at-start dev self-test's "tree file unchanged" claim (`AutoScan.cpp:645-649`) becomes "unchanged
  unless a graft ran, and then every pre-graft node unchanged".

### 5. Renamed plugins

TreeRemap (`TreeRemap.h`, `SpellScannerFormId.cpp:200-246`) tries `persistentId` first; when it does not resolve,
a spell with the node's editor id in the node's school (powerofthree's Tweaks) takes the node, and the node's
`persistentId` is rewritten at the next tree write. Several matches: none taken (logged).

## Tests the build must ship

- Offline (tools/, no game types): seen list (baseline from the old catalog, Import clears, edit-mode spawn and
  duplicate group never candidates, no persistentId never), parent choice against fixed nodes (repeatable, child
  cap, tier fallback, root fallback, a missing plugin's node never a parent), placement (no existing coordinate
  changes, spacing kept, tier ring), quest-only classification (quest-only vs merchant/leveled/config/other
  script), editor-id rematch (one match, none, several).
- Dev self-test: after a graft every pre-graft node's x/y/children/prereqs hash unchanged, every grafted node has
  a live parent in its school, no seen spell grafted; the quest-only list of the load order logged.
- In game (author, by eye only): the new spells look like they belong where they landed.

## Left to the builder

File layout (a `TreeGraft` unit with pure headers for the offline checks), how the seen list is stored, the
distance term's weight, the free-point search, log wording, and how the panel is told to reload.
