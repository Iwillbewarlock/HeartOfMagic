# Tree graft Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spells a newly added mod brings join the existing spell tree automatically at the main menu, without moving any existing node.

**Architecture:** A game-free core (`TreeGraft`, candidate rules, parent choice against fixed nodes) is checked offline; the declutter learns "fixed" nodes and "no spread"; a game-side runner at the main menu reads the last scan, grafts, writes the tree and its record, re-applies the tree state and tells the panel. Quest-only tomes are told apart in the tome reach pass and left out of every build.

**Tech Stack:** C++23 SKSE plugin (CommonLibSSE-NG, nlohmann::json), offline tools via tools/CMakeLists.txt, Prisma UI panel JS (node run-tests.js), lang/*.json.

**Spec:** docs/superpowers/specs/2026-10-28-tree-graft-design.md (read it first; its "Decisions" are the author's and bind every task).

## Global Constraints

- Branch feature/h1-core-state, worktree D:/DEV/HeartOfMagic/.claude/worktrees/h1-core-state. Never push.
- Build: `.\BuildRelease.ps1 -preset Release-2026` (Build_Config_Local.ps1 is in the worktree). 0 warnings.
- No existing node of the saved tree changes position, tier, or links, except a parent gaining a child.
- Nodes of a plugin that is missing stay in `spell_tree.json`; the graft never removes or re-parents them.
- New corner message key `hud.spellsGrafted`, English `"{{count}} new spells joined the spell tree"`; every language file gets it; the translations get the Argon final review (CLAUDE.md "Translation final review").
- Reviews: GPT is over its limit until 2026-11-02 15:26 KST - use the reviewer agent for every review until then.
- Comment density and naming as the surrounding code; header-only pure parts for offline checks (the project's pattern: TreeRemap.h, ScanRecordRules.h).

## Review Focus

1. A tree whose saved nodes lack `persistentId` (built before 2026-10, `schoolBridges.js:46-62`): such a node is never matched against the seen list by id - it must still count as "in the tree" by formId, so its spell is not grafted twice. Test in Task 4.
2. A school the tree does not have yet (the new mod adds the first Alteration spells of a tree built without them): the graft must not crash or invent a school; spells of a school with no nodes are left out and logged. Test in Task 4.
3. A scan output on disk that is older than the current load order (a panel scan made before the mod was added): the runner must not graft from it. Test in Task 7 (CatalogMatches gate).
4. Many new spells at once (a big pack, 300+): the graft must finish within one main-menu task budget or step like the auto scan; at least log the time. Test in Task 4 (300 spells, completes, no node moved).
5. The player opens the panel at the main menu while the graft writes: the panel's tree must be the grafted one afterwards (SendTreeData), and an edit-mode save after that keeps the grafted nodes. Checked by the dev self-test in Task 7 (panel copy hash = file hash after the graft).

---

### Task 1: Tome reach tells quest-only tomes apart

**Files:**
- Modify: `plugins/spelllearning/src/tomereach/TomeReachRecords.h:88-95` (Evidence), `TomeReachRecords.cpp:139-202`
- Modify: `plugins/spelllearning/src/tomereach/TomeReachPass.h:21-33`, `TomeReachPass.cpp:27-49`
- Modify: `plugins/spelllearning/include/tomereach/TomeReach.h`, `src/tomereach/TomeReach.cpp` (publish)
- Modify: `plugins/spelllearning/src/spellscanner/SpellScannerScan.cpp:381,404` (mark)
- Test: `tools/tome-reach-test.cpp`

**Interfaces:**
- Produces: `Evidence::foundQuest` (`std::unordered_set<Key>`: loose hits from a QUST record's VMAD or alias subrecords); `PassResult::questOnly` (`std::unordered_set<Key>`: tomes reached only through `foundQuest` - not `found`, not another record's loose hit, not a config, not a damaged plugin); `bool TomeReach::QuestOnlySpells(std::unordered_set<RE::FormID>& out)` (false while the pass runs or failed, as `UnreachableSpells`); scan JSON field `"questOnly": true` on a spell all of whose tomes are quest-only.

- [ ] **Step 1: Write the failing tests** in `tools/tome-reach-test.cpp`, made-up plugins as the file's other cases:
  - `quest-only: a tome named only in a QUST VMAD is questOnly`
  - `quest-only: the same tome also in an LVLI (a merchant list) is not` (the College novice case)
  - `quest-only: also in a BOOK/NPC_ VMAD (another record's script) is not`
  - `quest-only: also in a distribution config is not`; `a damaged plugin's tome is not`
- [ ] **Step 2: Run** `build/tools/Release/tome-reach-test.exe --self-test` - expect the new cases to FAIL (fields missing).
- [ ] **Step 3: Implement.** In `ReadRecord`, pass `quest` (already computed, `:169`) to `AddLoose` so a QUST's loose hits go to `foundQuest` as well as `foundLoose`; record other records' loose hits apart. `FindReachedTomes` fills `questOnly`. `TomeReach.cpp` keeps the spells of quest-only tomes beside `unreachable` (a spell is quest-only when every tome of it is). `SpellScannerScan.cpp` Finish reads them next to `UnreachableSpells` (same `OutcomeKey` rule) and Build writes `questOnly`.
- [ ] **Step 4: Run** tome-reach-test - all PASS; spell-tomes-check, librarian-test unchanged.
- [ ] **Step 5: Commit** `Tome reach: quest-only tomes (handed out only by a quest's script or alias) are told apart`.

### Task 2: Quest-only spells leave the tree; the blacklist window lists them

**Files:**
- Modify: `PrismaUI/views/SpellLearning/SpellLearningPanel/modules/proceduralTreeBuilder.js:44-66`
- Modify: `modules/gateReview.js` (a third list), `index.html` blacklist modal (`#quest-only-entries`, `#quest-only-count`), run `node tools/make-dev-harness.js`
- Modify: `lang/*.json` (keys below), run `node lang/build-preloads.js`
- Test: `modules/gateReviewTest.js`

**Interfaces:**
- Consumes: scan field `questOnly` (Task 1).
- Produces: `isTreeCandidate(spell)` false for `spell.questOnly === true` unless `GateReview.isKept(spell)`; `GateReview.questOnly(spells, blacklist, reviewed)` → the quest-only spells neither kept nor blacklisted; the list rendered with an "Include" button that calls `GateReview.keep(spell)`.
- i18n keys: `modals.gateReview.questHeader` "Quest only", `modals.gateReview.questDescription` "Spells whose tomes only a quest hands out. Include one to let it into the tree.", `modals.gateReview.questEmpty` "No quest-only spells in the last scan."

- [ ] **Step 1: Failing tests** in gateReviewTest.js: `quest-only: left out of the tree`, `quest-only: kept (Include) goes back in`, `quest-only: a blacklisted one is not listed`, `quest-only: a spell without the field is a candidate` (a scan from before Task 1).
- [ ] **Step 2: Run** `node run-tests.js` - the new checks FAIL.
- [ ] **Step 3: Implement** the rule and the list as the gate's "out" list (`_renderList`), keys in en.json and the other language files (first drafts), preloads rebuilt.
- [ ] **Step 4: Run** `node run-tests.js` - 0 FAIL (DevHarness matches index.html).
- [ ] **Step 5: Argon final review** of the three keys in every language (CLAUDE.md procedure), apply the rewrites that keep tags; note "N flagged, M changed" in the commit.
- [ ] **Step 6: Commit** `Quest-only spells are left out of the tree and listed in the blacklist window`.

### Task 3: The seen list is written at Apply

**Files:**
- Modify: `modules/classic/classicMain.js:407-442` (Apply), `modules/spellTreeSave.js:54-70`
- Modify: `plugins/spelllearning/src/uimanager/UIManagerAutoBuild.cpp:50-91,182-198`, `src/uimanager/UIManagerTree.cpp:218-279`
- Create: `plugins/spelllearning/include/uimanager/TreeRecord.h` (pure: the record and its JSON)
- Test: `tools/tree-save-check.cpp` (record cases), `modules/spellTreeSaveTest.js` (new, registered in run-tests.js)

**Interfaces:**
- Produces: the built tree's payload carries a top-level `"seenSpells": [persistentId...]` - every spell of `state.lastSpellData.spells` with a `persistentId`, kept or left out; C++ `SaveSpellTreeFromPanel(argument, built=true)` moves it into the record and writes the tree file **without** it.
- `TreeRecord.h`: `struct TreeRecord { std::string loadOrder, noticedLoadOrder; std::size_t plugins = 0, spells = 0; std::vector<std::string> seen; bool hasSeen = false; std::size_t pendingGrafted = 0; nlohmann::json extra; }` (`extra` keeps unknown fields); `TreeRecord ParseTreeRecord(const nlohmann::json&)`; `nlohmann::json ToJson(const TreeRecord&)`; `std::vector<std::string> TakeSeenSpells(nlohmann::json& tree)` (removes the field, returns it; empty when absent). Record `version` 2.
- An `SaveSpellTree` from Import (`treeViewerUI.js`, source `'import'`) clears `seen` and `hasSeen` (the Import passes source `'import'` to SpellTreeSave.send; C++ needs the source: send it as `SaveImportedSpellTree`, a third listener registered next to the other two).

- [ ] **Step 1: Failing tests:** tree-save-check `record: seen list round-trips, unknown fields kept`, `record: version 1 file reads with hasSeen false`, `TakeSeenSpells: removed from the tree text, absent gives empty`; spellTreeSaveTest `apply: payload has seenSpells of every scanned spell with a persistentId`, `edit save: no seenSpells`.
- [ ] **Step 2: Run** both - FAIL.
- [ ] **Step 3: Implement.** ReadRecord/WriteRecord use TreeRecord.h; `RecordTreeLoadOrder` takes the seen list; Import clears it.
- [ ] **Step 4: Run** tree-save-check, `node run-tests.js`, build - PASS, 0 warnings.
- [ ] **Step 5: Commit** `Tree record: the spells a built tree's scan saw (seen list)`.

### Task 4: The graft core (offline)

**Files:**
- Modify: `plugins/spelllearning/src/treebuilder/TreeBuilderClassic.cpp:98-205` (FindBestClassicParent gets `std::mt19937* rng`, nullptr = no jitter; moved behind a declaration in `include/treebuilder/TreeBuilderInternal.h`)
- Create: `plugins/spelllearning/include/treebuilder/GraftCandidates.h` (pure rules), `include/treebuilder/TreeGraft.h`, `src/treebuilder/TreeGraft.cpp`
- Create: `tools/graft-check.cpp`; Modify: `tools/CMakeLists.txt` (target `graft-check`, sources as `treebuilder-test` plus TreeGraft.cpp)

**Interfaces:**
- `GraftCandidates.h`: `struct PlayerRules { std::unordered_set<std::string> blacklistKeys, gateKept, pluginsOff; }` (keys `foldedPlugin:localid`, as the panel's `GateReview.key`); `bool IsTreeCandidate(const nlohmann::json& spell, const PlayerRules&)` = the panel's `isTaughtByTome` + blacklist + whitelist + quest-only (`proceduralTreeBuilder.js:44-66`, `filterBlacklistedSpells` `:87`, `filterWhitelistedSpells` `:124`); `std::vector<const nlohmann::json*> NewSpells(const nlohmann::json& scanSpells, const nlohmann::json& tree, const std::unordered_set<std::string>& seen)` - not seen, not in the tree by formId or persistentId, persistentId present, no node of its duplicate group.
- `TreeGraft.h`: `struct GraftResult { nlohmann::json tree; std::vector<std::string> grafted, leftOut, skippedNoSchool; }`; `GraftResult Graft(const nlohmann::json& tree, const std::vector<nlohmann::json>& scanSpells, const std::vector<const nlohmann::json*>& candidates, const PlayerRules&, const TreeBuilder::BuildConfig&)`. Rules per spec §3: school/tier as the build (`SpellTiers`), parent by FindBestClassicParent over the school's existing nodes (child cap = the tree's own maximum children, at least `maxChildrenPerNode`) with no jitter plus a distance term (weight left to the implementer, documented in the header), lowest tier first; new node gets `formId`, `persistentId`, `name`, `tier`, `school`, `prerequisites`/`softPrereqs` = [parent], `softNeeded: 1`, `x`/`y` = a provisional point beside the parent (outward from the school centre, same tier ring); parent's `children` gains it. No other field of an existing node changes.

- [ ] **Step 1: Failing tests** in graft-check (made-up trees and scans):
  - `candidates: a seen spell is not new`; `in the tree by formId only (no persistentId on the node) is not new`; `a new copy whose group has a node is not new`; `no persistentId is not new`
  - `rules: gate out, blacklist, plugin off, questOnly, tomeUnreachable each left out; gateKept brings gate-out and questOnly back`
  - `graft: every existing node's x, y, tier, prerequisites unchanged; one parent gained one child`
  - `graft: repeatable - the same input twice gives the same tree`
  - `graft: a fire spell goes under the fire branch (element trait) not the frost one`
  - `graft: a school the tree lacks - spell in skippedNoSchool, tree unchanged`
  - `graft: 300 new spells - completes, existing nodes unchanged, each grafted node has a parent in its school`
- [ ] **Step 2: Run** `build/tools/Release/graft-check.exe --self-test` - FAIL (not built).
- [ ] **Step 3: Implement**; the build's own call keeps passing its rng (no behaviour change: treebuilder-test with `--seed 42` gives the same tree before and after).
- [ ] **Step 4: Run** graft-check, treebuilder-test (seed 42 output hash equal to before), build - PASS.
- [ ] **Step 5: Commit** `Tree graft core: new spells under fixed existing nodes (offline checked)`.

### Task 5: The declutter keeps fixed nodes and can skip the spread

**Files:**
- Modify: `plugins/spelllearning/src/treebuilder/LayoutDeclutter.cpp:120,354-357`, `include/treebuilder/LayoutDeclutter.h` (request doc)
- Test: `tools/declutter-test.cpp`

**Interfaces:**
- Request: node field `"fixed": true` makes an item fixed (as `isRoot` does); request field `"spread": false` skips the ×1.35 step (default true - the build's call unchanged).

- [ ] **Step 1: Failing tests:** `declutter: fixed nodes keep their x/y exactly`; `declutter: spread false leaves fixed and free nodes unscaled`; `declutter: a free node on top of a fixed one moves off it and off its links`; `declutter: no fields given - result equal to before` (regression).
- [ ] **Step 2-4:** run declutter-test (FAIL, then implement, then PASS), build 0 warnings.
- [ ] **Step 5: Commit** `Declutter: fixed nodes and no spread, for a graft`.

### Task 6: A renamed plugin's spell is found by editor id

**Files:**
- Modify: `modules/schoolBridges.js:46-62` (bake `editorId` on nodes from the scan, as `persistentId`)
- Modify: `plugins/spelllearning/src/spellscanner/SpellScannerFormId.cpp:200-246` (JudgeNode fallback)
- Create: `plugins/spelllearning/include/EditorIdRematch.h` (pure: pick one match)
- Test: `tools/tree-remap-check.cpp`, `modules/schoolBridgesTest.js` (or the existing test that covers applyToOutput)

**Interfaces:**
- `EditorIdRematch.h`: `std::optional<std::uint32_t> PickRematch(std::string_view school, const std::vector<std::pair<std::uint32_t, std::string>>& candidatesWithSchool)` - exactly one candidate in the school, else none.
- JudgeNode: when `ResolveWith(persistentId)` fails and the node has `editorId`, `RE::TESForm::LookupByEditorID<RE::SpellItem>` (needs powerofthree's Tweaks; `SpellScannerCasters.cpp:82` uses it) gives candidates; `PickRematch` decides; the verdict keeps the node with the new formId (logged "found again by editor id"). The graft's write (Task 7) rewrites that node's `persistentId` and `formId` in the file.

- [ ] **Step 1: Failing tests:** `rematch: one match in the school`, `two matches - none`, `other school - none`, `no editor id - none`; panel `applyToOutput bakes editorId`.
- [ ] **Step 2-4:** run, implement, run tree-remap-check and `node run-tests.js` - PASS.
- [ ] **Step 5: Commit** `Tree load: a node whose plugin was renamed is found again by editor id and school`.

### Task 7: The graft runs at the main menu

**Files:**
- Create: `plugins/spelllearning/src/uimanager/TreeGraftRun.cpp`, declaration in `include/uimanager/StartupState.h`
- Modify: `plugins/spelllearning/src/AutoScan.cpp:509-650` (call it on both paths: after a scan, and when the catalog already matched), `:645-649` (TREE self-test claim)
- Modify: `src/uimanager/UIManagerTreeState.cpp:263` (factor `void ApplyTreeState(const nlohmann::json& tree)` out of LoadTreeStateAtStartup and reuse it)
- Modify: `src/uimanager/UIManagerNotify.cpp:284` / `UIManagerAutoBuild.cpp:200-219` (pending `hud.spellsGrafted` at save load)
- Modify: `src/environment/` (dev self-test `tree-graft`), `lang/*.json` (`hud.spellsGrafted`)

**Interfaces:**
- `void RunTreeGraftAtMainMenu(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog)` - game thread.
- Runs only when: the tree file exists; `ReadAutoBuildSetting() != Off`; the record's `loadOrder` differs from `loadOrder.hash`; `ScanRecord::CatalogMatches(loadOrder)`; the scan output on disk has empty `incomplete` and the tome reach outcome was `ready`. Seen = the record's list; none (`hasSeen` false) → the previous catalog's spells (AutoScan reads `kCatalogFile` into `previousCatalog` before it writes, `AutoScan.cpp:613`), none either → the current scan's spells, nothing grafted.
- `PlayerRules` from config.json: `spellBlacklist` and `pluginWhitelist` as `ReadPlayerFilter` reads them (`src/librarian/LibrarianSpellResearchFilter.cpp:28-62`; share that reader, do not copy it), `gateReviewed` as `JudgeNode`'s `gateKept` (`SpellScannerFormId.cpp:200`).
- Then: `Graft` (Task 4) on the file's own tree (missing plugins' nodes kept), declutter with existing nodes `fixed` and `spread: false` (Task 5) applied to the grafted nodes only, rematched nodes rewritten (Task 6), write with `.bak` (`FileUtils::WriteAtomically`), record: `loadOrder`, seen += every candidate judged, `pendingGrafted`; `ApplyTreeState`; `SendTreeData` (validated copy); at the next save load `HudNotify::Show("hud.spellsGrafted", "{{count}} new spells joined the spell tree", {{"count", n}})` once, and `ShowPendingLoadOrderNotice` stays quiet for this load order.
- Fails (unreadable tree or scan, graft throws): nothing written, logged, the old notice applies.

- [ ] **Step 1: Dev self-test** `tree-graft` (selftest.json): PASS when after a graft every pre-graft node's x/y/children/prereqs hash is unchanged, each grafted node has a live parent in its school, no seen spell was grafted, and the panel's last `SendTreeData` text hashes like the file's validated tree; SKIP when no graft ran (says why). AutoScan's `TREE` check becomes "unchanged, or changed by a graft and the graft check passed".
- [ ] **Step 2: Implement**; `hud.spellsGrafted` in en.json and every language (Argon review as Task 2 Step 5).
- [ ] **Step 3: Build** 0 warnings; every offline check and `node run-tests.js` as before plus Tasks 1-6's.
- [ ] **Step 4: Commit** `Tree graft: new spells join the tree at the main menu`.

### Task 8: Docs and the quest-only dump

**Files:**
- Modify: `CHANGELOG.md` (Unreleased, New: the graft; Changed: quest-only spells leave the tree), `docs/ARCHITECTURE.md` (graft section: trigger, seen list, record v2, quest-only, rematch), `CLAUDE.md` (graft-check named with the other checks)
- Create: dev self-test line or log listing every quest-only spell of the load order (`TomeReach`/scan Finish, one line per spell up to all)

- [ ] **Step 1:** Write the docs.
- [ ] **Step 2:** Deploy to the dev folder with the self-test (`DeployDev.ps1 -WithSelfTest`) only when the author asks; the quest-only list from that run goes to the author for a look before release (spec decision 3).
- [ ] **Step 3: Commit** `Docs: tree graft, quest-only spells`.
