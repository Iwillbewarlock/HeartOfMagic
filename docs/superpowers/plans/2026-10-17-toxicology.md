# Heart of Magic Librarian - Toxicology Implementation Plan

> **2026-10-18:** the tome is no longer sold at Elgrim's Elixirs; it is the reward of the quest Few and Far Between
> (spec section 4a, `game/ToxReward.h`). The shop steps below (PutTomeInShop, the shop self test) are history.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An SKSE addon that, once the player has learned the "Toxicology" ability, makes Librarian-tagged poison spells apply the top favorited poison to the targets they hit, refreshes our own applications instead of stacking, and consumes one bottle per 100 magicka spent on poison spells.

**Architecture:** A pure C++ core (`plugins/Toxicology/src/core`, no game types) holds every decision a test can check offline (catalog parsing, poison choice, dose arithmetic, settings, interval). A game layer (`src/game`) wires CommonLibSSE-NG events, tasks, a frame hook and the co-save to that core. A small ESL carries the keyword, ability, effect and tome; I4 paints the icon from a json rule and a one-frame SWF.

**Tech Stack:** C++23, CommonLibSSE-NG v10 (submodule), nlohmann-json, spdlog, CMake super-build (`BuildRelease.ps1 -preset Release-2026`), houseCARL for the ESL, JPEXS FFDec 18.5.0 for the SWF.

**Spec:** `docs/superpowers/specs/2026-10-17-toxicology-addon-design.md` (Korean; read it whole before any task).

## Global Constraints

- Worktree `D:/DEV/HeartOfMagic/.claude/worktrees/toxicology`, branch `feature/toxicology`. First command there: `git submodule update --init --recursive` (the worktree's CommonLib is empty until then).
- Build only with `powershell -File .\BuildRelease.ps1 -preset Release-2026` (build/ is VS 2026). Check `Get-Process SkyrimSE` first; never build while it runs.
- Project rules from `CLAUDE.md`: 600 lines per file max; read CommonLib headers before using any RE type; smart pointers only; `std::lock_guard`/`std::unique_lock` only; named `k`-constants, `m_` members, PascalCase functions; `// ====` section headers; no AI attribution in code or docs.
- No Python anywhere (tools are C++ or PowerShell).
- Names (verbatim): mod "Heart of Magic Librarian - Toxicology"; DLL `HeartOfMagic_Toxicology.dll`; ESL file `HeartOfMagic_Toxicology.esp` (ESL flag); keyword EditorID `HoMTox_MimicPoison`; settings `SKSE/Plugins/HeartOfMagic_Toxicology.json` with `enabled` (true), `showIcon` (true), `minIntervalSeconds` (1.0), `magickaPerDose` (100); I4 rule file `SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json`; icon `Interface/HeartOfMagicToxicology/icons.swf`, label `potion_mimic`, color `#62A636`; log `HeartOfMagic_Toxicology.log`; zip `dist/HeartOfMagic-Librarian-Toxicology_<version>.zip`; version file `plugins/Toxicology/VERSION` = `1.0.0`; source tag `toxicology-v1.0.0`; dev self test file `SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json`.
- Catalog: `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`, top-level `version` must be 1, `spells` keyed by `"Plugin.esp|0x00XXXX"`, poison = `"poison"` in the entry's `elements`.
- Tome sold by Elgrim's Elixirs: merchant chest ref `0A31BE:Skyrim.esm`, base container `MerchantRiftenElgrimsElixirsChest` `0A31AE:Skyrim.esm` (only that ref uses it — houseCARL reverse reference, 2026-10-17). Tome value 350.
- Everything the ability gates (choice, keyword, magicka counting, consumption, poison application) does nothing unless the player has the ability.
- Docs and README in English except the spec (Korean). In-game strings (ability, effect, tome names and descriptions, tome text) are drafted by Gemini Argon from a fact sheet (CLAUDE.md "Prose writing by Gemini Argon").

## Review Focus

1. A save made before the addon was installed, or another character's save: accumulator 0, no stale ownership records, no queued task from the old session acts — test in Task 6 (`RevertResetsAccumulator`) and Task 7 self test step "load another save".
2. The cloned poison's last bottle consumed while a hit task for it is queued: the task re-checks the inventory and does nothing — test in Task 5 self test step and core test `PickTopPoisonSkipsZeroCount`.
3. Two favorited poisons whose names differ only by case or by a Korean name: deterministic pick — core tests `PickTopPoisonCaseInsensitive`, `PickTopPoisonKoreanAfterAscii`, `PickTopPoisonTieByFormId`.
4. Settings file with wrong types, negative or NaN numbers: defaults, with a log warning per key — core tests `ParseSettings*`.
5. A weapon-applied poison identical to the cloned one already on the target: not extended, not stacked — Task 5 self test step 4.

---

### Task 1: Addon scaffold and pure core with offline harness

**Files:**
- Create: `plugins/Toxicology/VERSION` (`1.0.0`), `plugins/Toxicology/CMakeLists.txt` (shape of `plugins/BookXP/CMakeLists.txt`; target `HeartOfMagic_Toxicology`, `OUTPUT_NAME HeartOfMagic_Toxicology`, define `TOXICOLOGY_VERSION`), `plugins/Toxicology/src/Main.cpp` (logging and `SKSEPluginLoad` only for now), `plugins/Toxicology/src/core/ToxCore.h`, `plugins/Toxicology/src/core/ToxCore.cpp`
- Modify: top-level `CMakeLists.txt` (read `TOXICOLOGY_VERSION` from `plugins/Toxicology/VERSION`, `add_subdirectory(plugins/Toxicology)` after BookXP), `tools/CMakeLists.txt` (add `toxicology-test`)
- Test: `tools/toxicology-test.cpp`

**Interfaces:**
- Produces (namespace `Toxicology`, header `core/ToxCore.h`, no RE types):
  - `struct Settings { bool enabled = true; bool showIcon = true; float minIntervalSeconds = 1.0f; float magickaPerDose = 100.0f; };`
  - `Settings ParseSettings(const nlohmann::json& a_json, std::vector<std::string>& a_warnings);` — a missing key keeps the default silently; a key of the wrong type, a negative `minIntervalSeconds`, a `magickaPerDose` <= 0, or a non-finite number keeps the default and adds one warning naming the key and the value.
  - `std::vector<std::string> PoisonSpellKeys(const nlohmann::json& a_catalog, std::string& a_error);` — empty and `a_error` set when `version` != 1 or `spells` is not an object; otherwise every key whose `elements` array contains `"poison"`, sorted.
  - `struct PoisonCandidate { std::string name; std::uint32_t formId = 0; bool favorited = false; std::int32_t count = 0; };`
  - `std::optional<std::size_t> PickTopPoison(const std::vector<PoisonCandidate>& a_candidates);` — among favorited with count > 0, the smallest by ASCII-lowercased UTF-8 bytes, ties by smaller formId.
  - `struct DoseResult { std::int32_t doses = 0; float remainder = 0.0f; };`
  - `DoseResult ConsumeMagicka(float a_accumulated, float a_spent, float a_perDose);` — negative or non-finite `a_spent` adds nothing.
  - `bool IntervalElapsed(double a_lastSeconds, double a_nowSeconds, float a_minInterval);` — true when never seen (`a_lastSeconds < 0`) or `now - last >= minInterval`.

- [ ] **Step 1: Submodules and baseline build**

Run: `git submodule update --init --recursive` then `powershell -File .\BuildRelease.ps1 -preset Release-2026`
Expected: build succeeds before any change.

- [ ] **Step 2: Write the failing harness `tools/toxicology-test.cpp`**

`toxicology-test --self-test` runs every check, prints `PASS <name>` / `FAIL <name>: <detail>`, exits 1 on any FAIL. Checks (names exact):

```cpp
// ParseSettingsDefaults: {} -> enabled, showIcon true; 1.0; 100; no warnings
// ParseSettingsValues: {"enabled":false,"showIcon":false,"minIntervalSeconds":2.5,"magickaPerDose":40} -> those values
// ParseSettingsBadTypes: {"enabled":"yes","minIntervalSeconds":-1,"magickaPerDose":0} -> defaults, 3 warnings
// PoisonSpellKeysBasic: {"version":1,"spells":{"A.esp|0x000801":{"elements":["poison"]},"A.esp|0x000802":{"elements":["fire"]}}} -> ["A.esp|0x000801"]
// PoisonSpellKeysWrongVersion: {"version":2,"spells":{}} -> empty, error non-empty
// PoisonSpellKeysNoElements: entry without "elements" or with a non-array -> skipped, no throw
// PickTopPoisonBasic: {"Weak Poison",fav,1},{"Deadly Poison",fav,1} -> index of "Deadly Poison"
// PickTopPoisonIgnoresUnfavorited: {"A",!fav,1},{"B",fav,1} -> "B"
// PickTopPoisonSkipsZeroCount: {"A",fav,0},{"B",fav,2} -> "B"
// PickTopPoisonCaseInsensitive: {"b poison",fav},{"A poison",fav} -> "A poison"
// PickTopPoisonKoreanAfterAscii: {"독약",fav},{"Zebra",fav} -> "Zebra"
// PickTopPoisonTieByFormId: same name, formIds 0x20 and 0x10 -> formId 0x10
// PickTopPoisonEmpty: {} -> nullopt
// ConsumeMagickaUnder: (0, 60, 100) -> {0, 60}
// ConsumeMagickaExact: (40, 60, 100) -> {1, 0}
// ConsumeMagickaMulti: (90, 250, 100) -> {3, 40}
// ConsumeMagickaBadSpent: (10, -5, 100) and (10, NaN, 100) -> {0, 10}
// IntervalFirstTime: (-1, 5, 1) -> true
// IntervalTooSoon: (5, 5.5, 1) -> false
// IntervalElapsedExactly: (5, 6, 1) -> true
```

- [ ] **Step 3: Wire `toxicology-test` in `tools/CMakeLists.txt`** (sources: the harness + `plugins/Toxicology/src/core/ToxCore.cpp`; links nlohmann_json) and run the build.
Expected: link error / failing checks (core not written).

- [ ] **Step 4: Implement `ToxCore.cpp`** to the Interfaces above.

- [ ] **Step 5: Build and run** `build/tools/Release/toxicology-test.exe --self-test`
Expected: all PASS, exit 0. The DLL target also builds (Main.cpp logs one start line).

- [ ] **Step 6: Commit** `Toxicology: addon scaffold, pure core and offline harness`

---

### Task 2: The ESL and the in-game text

**Files:**
- Create: `plugins/Toxicology/data/HeartOfMagic_Toxicology.esp` (binary, committed), `plugins/Toxicology/data/HeartOfMagic_Toxicology.json` (settings with the four defaults), `plugins/Toxicology/ESL.md` (how the ESL was made, record list, FormIDs)
- Scratch: Argon fact sheet and draft in the session scratchpad.

**Interfaces:**
- Produces: records at fixed local FormIDs in `HeartOfMagic_Toxicology.esp`, which Task 3 looks up with `TESDataHandler::LookupForm`:
  - `0x800` KYWD `HoMTox_MimicPoison`
  - `0x801` MGEF `HoMTox_ToxicologyEffect` — archetype Script (no script attached), casting type Constant Effect, delivery Self, not hostile, not detrimental; description = the ability text
  - `0x802` SPEL `HoMTox_ToxicologyAbility` — type Ability, Constant Effect, Self, one effect 0x801 (magnitude 0, duration 0)
  - `0x803` BOOK `HoMTox_ToxicologyTome` — copied from a vanilla Alteration spell tome (model, inventory art, pickup sound), "Teaches Spell" flag with spell 0x802, value 350, weight as the source tome, book text from Argon

- [ ] **Step 1: Fact sheet for Argon** — names "Toxicology" (ability), "Spell Tome: Toxicology"; what it does (poison spells tagged by Heart of Magic Librarian carry the effect of your top favorited poison; a hit while it lasts only renews its duration; every 100 Magicka spent on poison spells uses up one bottle); tone: lore-friendly alchemist's treatise, Elgrim's Elixirs flavour allowed, no invented mechanics or numbers beyond those; lengths: effect description <= 200 characters, tome text 3-6 short paragraphs.
- [ ] **Step 2: Argon draft** (`agy`, `gemini-4-argon-high`, from the scratchpad) and line-by-line check against the sheet; cut anything invented.
- [ ] **Step 3: Author the ESL with houseCARL** (`housecarl_create_plugin` / `housecarl_create`, copy the vanilla tome as a new record), ESL flag on, masters `Skyrim.esm` only, then copy the written file into `plugins/Toxicology/data/`.
- [ ] **Step 4: Verify** with `housecarl_records` `source={"file":"HeartOfMagic_Toxicology.esp"}` on the four FormIDs: types, EditorIDs, the tome's teaches-spell link = 0x802, the ability's effect = 0x801, ESL flag set, no master beyond Skyrim.esm.
Expected: every field as listed.
- [ ] **Step 5: Commit** `Toxicology: ESL (keyword, effect, ability, tome) and default settings`

---

### Task 3: Game data at load: settings, poison spell set, forms, tome in the shop

**Files:**
- Create: `plugins/Toxicology/src/game/ToxData.h`, `ToxData.cpp`
- Modify: `plugins/Toxicology/src/Main.cpp` (messaging listener: kDataLoaded, kPreLoadGame, kPostLoadGame, kNewGame)

**Interfaces:**
- Consumes: Task 1 core (`ParseSettings`, `PoisonSpellKeys`).
- Produces (namespace `ToxGame`):
  - `const Toxicology::Settings& GetSettings();`
  - `bool IsPoisonSpell(RE::FormID a_spell);` — in the set built from the catalog, filtered to spells with at least one effect whose `EffectSetting::IsHostile()`.
  - `RE::BGSKeyword* MimicKeyword(); RE::SpellItem* Ability(); RE::TESObjectBOOK* Tome();` — null when the ESL is not loaded.
  - `bool PlayerHasAbility();`
  - `void LoadSettings(); void LoadPoisonSpells(); void PutTomeInShop();` — `LoadPoisonSpells` runs at kDataLoaded and again at kPostLoadGame/kNewGame (the catalog can change after a rescan).
- Cloak poison spells: also put in the set every `SpellItem` that is the associated item of one of the spell's effects (the cloak hits with that spell).
- Catalog re-read: `LoadPoisonSpells()` also runs from the hit task when the catalog file's last-write time changed, checked at most every 5 s.
- Catalog key → FormID: reuse the persistentId resolution the Librarian uses (search `plugins/spelllearning/src` for the function that resolves `Plugin|0x...` including light plugins and non-ASCII names, e.g. `EncodingUtils.cpp` / `SpellScannerFormId.cpp`); do not write a second resolver.

- [ ] **Step 1:** Implement; log counts: catalog keys, resolved, hostile kept, unresolved; ESL present or not; tome added (count after) or why not.
- [ ] **Step 2: Build** — Expected: success.
- [ ] **Step 3: Commit** `Toxicology: settings, poison spell set, ESL forms, tome at Elgrim's Elixirs`

---

### Task 4: Choosing the poison, moving the keyword, I5

**Files:**
- Create: `plugins/Toxicology/src/game/ToxSelection.h`, `ToxSelection.cpp`

**Interfaces:**
- Consumes: Task 1 `PickTopPoison`, Task 3 `MimicKeyword`, `PlayerHasAbility`, `GetSettings`.
- Produces:
  - `RE::AlchemyItem* CurrentPoison(bool a_forceRecompute = false);` — cached; recomputed when forced, when older than `kSelectionCacheSeconds = 0.5`, after a load (generation change). Null without the ability or with `enabled` false.
  - `void OnSelectionMaybeChanged();` — recompute; when the result differs, remove `HoMTox_MimicPoison` from the old form, add it to the new one (only if `showIcon`), then purge I5 once (below).
  - A `MenuOpenCloseEvent` sink calling `OnSelectionMaybeChanged()` when `InventoryMenu` or `FavoritesMenu` opens.
- Candidates: player inventory filtered to `AlchemyItem` with `IsPoison()`; name = `InventoryEntryData::GetDisplayName()`; favorited = `IsFavorited()`; count from the inventory map.
- Keyword bookkeeping: remember the keyworded poison by FormID, never by pointer. On kPreLoadGame look it up again and remove the keyword (a keyword added to a static form stays in memory across loads); a 0xFF form that no longer resolves is just forgotten.
- I5 purge: only when `InventoryInjectorImproved.dll` is loaded (`GetModuleHandle`), run the console line `i5 cache purge` through `RE::Script` (exact call from the header research in the session; if no safe path exists, skip and log once). Log the outcome.

- [ ] **Step 1:** Implement. **Step 2:** Build. **Step 3:** Commit `Toxicology: poison choice, mimic keyword, I5 cache purge`

---

### Task 5: Hits: apply or refresh our own poison

**Files:**
- Create: `plugins/Toxicology/src/game/ToxHits.h`, `ToxHits.cpp` (sink + task), `ToxApply.h`, `ToxApply.cpp` (ownership records, refresh, cast)

**Interfaces:**
- Consumes: Tasks 1, 3, 4; `IntervalElapsed`.
- Produces:
  - `std::uint32_t SessionGeneration(); void BumpGeneration();` — bumped on kPreLoadGame, kNewGame and co-save Revert (Task 6 calls it).
  - `void OnPoisonSpellHit(RE::ActorHandle a_target, RE::FormID a_spell);` — task body.
  - `void ClearApplications();`
- Sink: `TESHitEvent`; keep when `cause` is the player, `LookupByID(source)` is `FormType::Spell` and `IsPoisonSpell`, target is a live actor and not the player. Queue a task carrying `ActorHandle`, spell id, generation. No `TESMagicEffectApplyEvent` fallback (spec 5.3).
- Task: drop when generation differs; re-check ability, `enabled`, target alive, `CurrentPoison()` non-null; interval per (target FormID, poison FormID) with `IntervalElapsed`, clock = `std::chrono::steady_clock`.
- Effects flagged `kInactive` or `kDispelled` (`ActiveEffect.h:45-54`) count as gone everywhere.
- Caster: `player->GetMagicCaster(kInstant)`; if the self test (step 2) shows the poison landing on the player (Self delivery), switch to `target->GetMagicCaster(kInstant)` with the player as blame actor.
- Refresh fallback: if the self test (step 3) shows `elapsedSeconds = 0` does not extend the effect, refresh by dispelling our effects and casting again.
- Ownership: after `CastSpellImmediate(poison, false, target, 1.0f, false, 0.0f, player)`, collect the target's active effects whose `spell == poison` and whose caster is the player and that were not there before the cast; store them under (target, poison). Refresh = set `elapsedSeconds = 0` on stored effects still present in the target's list. A same-poison effect not in our records → do nothing (spec 5.4).

- [ ] **Step 1:** Implement. **Step 2:** Build. **Step 3:** Commit `Toxicology: poison spell hits apply or refresh the mimicked poison`

---


### Task 5b (only if the Task 7 self test shows a gap): `MagicTarget::AddTarget` hook

Run only when step 2 of the self test logs a delivery whose hits never reach the `TESHitEvent` sink or arrive with a non-spell `source` (explosion, concentration). Hook the player-cast path where `AddTargetData` carries the casting `MagicItem` (`MagicTarget.h:90-110`; read the header and pick the vfunc), keep the same filters and the same task (`OnPoisonSpellHit`), and dedupe across both sources by (target, spell) inside `minIntervalSeconds`. Remove the `TESHitEvent` sink only if the hook covers every delivery the self test checks. Commit `Toxicology: poison spell hits from AddTarget (deliveries without hit events)`.

---

### Task 6: Magicka accounting, bottles, co-save

**Files:**
- Create: `plugins/Toxicology/src/game/ToxMagicka.h`, `ToxMagicka.cpp` (cast event, frame hook), `ToxSave.h`, `ToxSave.cpp`

**Interfaces:**
- Consumes: Task 1 `ConsumeMagicka`; Task 4 `CurrentPoison`, `OnSelectionMaybeChanged`; Task 5 `BumpGeneration`, `ClearApplications`.
- Produces: `void AddSpentMagicka(float a_amount);` (applies `ConsumeMagicka`, removes the doses with a silent `RemoveItem` from the player, then `OnSelectionMaybeChanged()` when the poison ran out); `float Accumulated();`.
- Both kinds go through one hook: `ActorMagicCaster::Update(float a_delta)`, vfunc `0x1D` on `RE::VTABLE_ActorMagicCaster[0]` (`ActorMagicCaster.h:70-81`; install like `plugins/spelllearning/src/SpellCastImmediateHook.cpp:56-57`). Per call: `actor` is the player, `castingSource` is kLeftHand or kRightHand, `currentSpell` is a poison spell.
  - Concentration (`currentSpell->GetCastingType() == kConcentration`) while `state` is the casting state: add `GetCurrentSpellCost() * a_delta` (`MagicCaster.h:94`; it is the caster's own cost, so dual casting and perks are in it — confirm in the debug check below). Which `State` value means "concentrating" is not named in the header (`MagicCaster.h:30-42`): log the state values seen during a concentration cast once per session and pick from that; until then accept kCasting (6).
  - Fire-and-forget: `TESSpellCastEvent` for the player with a poison spell (it comes only for a released cast; an interrupted charge costs nothing) → task → the cost is that hand caster's `currentSpellCost` read in the hook at release; when it cannot be matched to a caster, `CalculateMagickaCost(player)` and, if `player->IsDualCasting()` (`Actor.h:637`), `RE::MagicFormulas::CalcDualCastCost(cost)` (`MagicFormulas.h:7`).
- `TESSpellCastEvent` arrives on any thread (`SpellCastHandler.cpp:63-66`): only the spell id crosses into the task.
- Not counted without the ability, with `enabled` false, or with `CurrentPoison()` null.
- Removing a dose: take it from a stack without `ExtraHotkey` first (pass that stack's `ExtraDataList` to `RemoveItem`); only when the favorited stack is all that is left, reduce it.
- Debug check: every 5 s of poison casting, log `magicka counted X / player magicka drop Y`.
- Co-save: unique id `'HMTX'`, record `'ACCM'` version 1 = one float. Revert: accumulator 0, `BumpGeneration()`, `ClearApplications()`. Load: read if present, reject non-finite or negative (log, keep 0).

- [ ] **Step 1:** Implement. **Step 2:** Build. **Step 3:** Commit `Toxicology: magicka spent on poison spells uses up bottles, kept in the co-save`

---

### Task 7: In-game self test

**Files:**
- Create: `plugins/Toxicology/src/game/ToxSelfTest.h`, `ToxSelfTest.cpp` (+ a second file if past 600 lines), `plugins/Toxicology/data/dev/HeartOfMagic_Toxicology_selftest.json`

Runs only when the dev file exists, once per session, starting about 30 s after the first save load (memory: self tests start after the load settles). Logs `SELFTEST PASS|FAIL <step>: <detail>` and `SELFTEST DONE pass=N fail=M`. Undoes what it added (ability, poisons, spawned actors, keyword).

Steps (spec 9): (1) give the ability, two poisons with a duration effect chosen from the load order, favorite both → the name-first one is `CurrentPoison()` and only it carries the keyword; (2) spawn a non-hostile target (ignores the player, does not flee: pick and set in code), cast one poison spell per delivery present in the set (aimed, touch, target-location/rune, concentration) via `CastSpellImmediate` with the player as caster → log whether `TESHitEvent` reached the sink and whether the poison effect is on the target; (3) second hit after `minIntervalSeconds` → one instance, elapsed back near 0, still running after its original duration; (4) apply the same poison as a weapon poison effect first on a fresh target → not extended, not stacked; (5) I5 purge outcome; (6) effect caster is the player; (7) `AddSpentMagicka(250)` with 2 bottles → one bottle left... then all gone → next poison chosen; save and reload keeps `Accumulated()` (checked on the next run with a marker in the dev file).

- [ ] **Step 1:** Implement. **Step 2:** Build. **Step 3:** Commit `Toxicology: dev-only in-game self test`

---

### Task 8: The icon

**Files:**
- Create: `plugins/Toxicology/data/Interface/HeartOfMagicToxicology/icons.swf`, `plugins/Toxicology/data/SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json`, `plugins/Toxicology/art/` (chosen PNG, the SVG, `ICON.md` with how the SWF was made)

- [ ] **Step 1:** Pick between the Gemini and GPT drafts (both from the same brief in the scratchpad `icon/brief.txt`); ask for fixes until it reads at 24 px.
- [ ] **Step 2:** Vector SVG (white fill, no strokes), drawn or traced by hand-written path data (no Python).
- [ ] **Step 3:** SWF from the template `D:\TAKEALOOK\mods\Diseases - I4 icon\Interface\IAmNotSimon\Disease.swf` with FFDec `-importShapes`, frame label `potion_mimic`; keep the `DefineSprite` wrapper. Check with FFDec `-export frame` PNG.
- [ ] **Step 4:** I4 rule json exactly: `{"$schema":"https://raw.githubusercontent.com/Exit-9B/InventoryInjector/main/docs/InventoryInjector.schema.json","rules":[{"match":{"formType":"Potion","keywords":"HoMTox_MimicPoison"},"assign":{"iconSource":"HeartOfMagicToxicology/icons.swf","iconLabel":"potion_mimic","iconColor":"#62A636"}}]}` (verify the schema URL exists; drop `$schema` if not).
- [ ] **Step 5:** Commit `Toxicology: mimic poison icon for I4`

---

### Task 9: Packaging, deploy, docs

**Files:**
- Create: `plugins/cmake/DistributionToxicology.cmake` (shape of the NPC Spells one on branch `feature/npc-spells-1.5`: zip rooted at Data, PDB zip, notices, licenses of only the linked ports), `plugins/Toxicology/THIRD-PARTY-NOTICES.md.in`, `plugins/Toxicology/README.md`
- Modify: top-level `CMakeLists.txt` (include), `DeployDev.ps1` (`-ToxicologyOnly`, `-ToxicologySelfTest`, `-ToxicologyModPath` default `D:\TAKEALOOK\mods\HeartOfMagic-Toxicology-Dev`; self test file removed on every deploy without the switch), `CLAUDE.md` (decision tree + Key Locations + test harness line), `docs/ARCHITECTURE.md` (addon line)

Zip contents: `HeartOfMagic_Toxicology.esp`, `SKSE/Plugins/HeartOfMagic_Toxicology.dll`, `SKSE/Plugins/HeartOfMagic_Toxicology.json`, `SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json`, `Interface/HeartOfMagicToxicology/icons.swf`, `SKSE/Plugins/HeartOfMagic_Toxicology/{LICENSE,THIRD-PARTY-NOTICES.md,licenses/}`.

README sections: what it does, requirements (SKSE, Address Library, Heart of Magic Librarian 1.5.0+ with one full scan; not the original Heart of Magic; optional I4), getting the ability (Elgrim's Elixirs, Riften), settings, load order (after `I4IconAddon.esp`), known limits (spec 5.3-5.6 and 11), uninstalling.

- [ ] **Step 1:** Implement. **Step 2:** Build; check the zip's file list. **Step 3:** `DeployDev.ps1 -ToxicologyOnly` to the dev mod folder. **Step 4:** Commit `Toxicology: release archive, dev deploy, docs`

---

### Task 10: Reviews and in-game run

- [ ] **Step 1:** Reviews of `master..feature/toxicology` in parallel: independent `reviewer` agent, OCR (`ocr delegate preview --from master --to feature/toxicology`), Codex `review --base master --scope branch`, Gemini Argon. Verify each finding against code/headers; fix the real ones; second review round on the fixes.
- [ ] **Step 2:** In-game self test: `DeployDev.ps1 -ToxicologyOnly -ToxicologySelfTest`, run through `mo2_guard -Mode Test` + DevBench like NPC Spells (memory `npc-spells-addon-status`), read the log, judge PASS/FAIL, then redeploy without the switch.
- [ ] **Step 3:** Hand the user only the by-eye checks: the icon's look in the favorites menu and the inventory (I4 renders it; no headless capture), the tome on Elgrim's shelf, how casting feels.
