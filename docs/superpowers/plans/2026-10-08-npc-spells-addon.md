# Heart of Magic NPC Spells Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship "Heart of Magic - NPC Spells": a second SKSE DLL (`HeartOfMagic_NPCSpells.dll`) plus a tiny ESL (`HeartOfMagic_NPCSpells.esp`, ESL flag, empty LVSP records at fixed FormIDs) that, at `kDataLoaded`, fills the LVSP records with Heart of Magic tree spells per (school x tier-rung x group x variant) and attaches the right lists to every NPC base record that uses its own spell list, then writes a report. The engine resolves each list by the actor's current school skill (ledger: confirmed by disassembly), so skill tiers and level scaling are the engine's job.

**Architecture:** All rules, classification, slot enumeration, list building and NPC decisions live in RE-free units under `plugins/NPCSpells/src/core/` (compiled into both the DLL and the offline harness `tools/npcspells-test` through the `tools/include` stub PCH). A thin game layer under `plugins/NPCSpells/src/game/` reads forms (`TESNPC`, `TESClass`, `TESRace`, `SpellItem`, `TESLevSpell`) and writes `TESLeveledList::entries` / `TESSpellList::SpellData::AddLevSpell`. A C++ tool `tools/npcspells-esl` writes the ESL from the same slot table the DLL fills, so FormIDs can never disagree.

**Tech Stack:** C++23, CommonLibSSE-NG v10.0.0 (`plugins/external/commonlibsse-ng`), nlohmann-json, spdlog, CMake super-build (`.\BuildRelease.ps1 -preset Release-2026`, VS 2026, build dir `build/`), PowerShell scripts. No Python anywhere.

**Spec:** `docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md` (repo-relative; §5.4 is the distribution design; §5.1-5.3 rules; §8 tests; §9 verified facts). Ledger: `lab/addon-research/npc-spells-ledger.md` - it exists only in the main checkout `D:/DEV/HeartOfMagic/` (untracked); read it from there, read-only. Every other path in this plan is relative to the worktree you implement in, never to the main checkout (it has its own uncommitted changes).

## Global Constraints

- Every source file <= 600 lines (CLAUDE.md rule 1); split before approaching it.
- Read the CommonLib header before using any RE type; every RE member used is cited `header:line` in this plan and must match on the implementer's checkout.
- No raw `new`/`delete` in addon code (CommonLib's own `TESNPC::GetSpellList()` allocates `SpellData` with `TES_HEAP_REDEFINE_NEW`, `src/RE/T/TESNPC.cpp:164-170`; the addon only calls it).
- No manual mutex lock/unlock (the addon is single-threaded at `kDataLoaded`; no mutex needed).
- Named constants only (`k` prefix); members `m_`, globals `g_`; PascalCase functions; 4 spaces. No bare numbers in code: the player check uses `TESForm::IsPlayer()` (`RE/T/TESForm.h:351`), the chain walk `kMaxTemplateDepth`, the ESL writer `kObndSize` / `kTes4DataSize` / `kTopGroupCount`, `Dice` `kDiceScale`.
- One LVSP holds one school's spells, school = `SpellItem::GetAssociatedSkill()` (`RE/S/SpellItem.h:81`, `RE/M/MagicItem.h:100`), entries sorted ascending by level, entry level = tier skill floor 0/25/50/75/100, `llFlags & 1` = all levels <= skill (`kCalculateFromAllLevelsLTOrEqPCLevel = 1 << 0`, `RE/T/TESLeveledList.h:47`; member `llFlags` `:72`), <= 255 entries per list (`numEntries` is `std::uint8_t`, `TESLeveledList.h:73`).
- No magicka filtering (ledger: AI never puts a spell costing more than max magicka into its combat list).
- Only tree spells (`spell_tree.json` nodes' `persistentId`), one representative per node; catalog key = `persistentId` (`Plugin.esp|0x00XXXX`).
- Class tiers: mage if `magicSum * 10 >= totalSum * 6`, hybrid if `magicSum * 10 >= totalSum * 3`, integer compare on `CLASS_DATA::SkillWeights` (`RE/T/TESClass.h:36-59`); conjuration class = conjuration weight >= 3 and not lower than any other magic school weight.
- Every record in the rules table is a `XXXXXX:Plugin.ext` FormID, mod records included; `ParseFormRef` rejects a bare EditorID (mods rename vanilla EditorIDs, and `TESForm::GetFormEditorID()` returns "" for most forms without powerofthree's Tweaks, `RE/T/TESForm.h:175`). The game layer never matches forms by EditorID.
- Add lists only to NPC records that use their own spell list: skip `TEMPLATE_USE_FLAG::kSpells` (`RE/T/TESActorBaseData.h:52`, bit 1<<3; CK calls it "Use Spell List").
- ESL: TES4 flag `0x200`, master `Skyrim.esm`, HEDR version 1.70, form version 44, own records at in-file FormIDs `0x01000800 + i`, local IDs `0x800..0xFFF` (2048 max), uncompressed, no runtime (0xFF) forms. Slots beyond the ESL's records are skipped and reported.
- The slot table and FormID formula come from a built-in constexpr table (`kGroups`, `kMaxChunks = 3`), never from the user-editable JSON; new slots append only.
- Build: `.\BuildRelease.ps1 -preset Release-2026`, preceded by the SkyrimSE process check; never run cmake by hand; `DeployDev.ps1` for the dev install.
- Docs updated in the same task that changes behaviour (CLAUDE.md harness rows in Tasks 1 and 6, "four DLLs" and Key Locations in Task 8, DeployDev and dist lines in Task 11; README, ARCHITECTURE and measured numbers in Task 12); commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; never a "Generated with Claude" line.
- Shipped artifact is the player mod; no AI at runtime; no raw FormIDs shown to players (the report is a developer file, the log is a log; no in-game UI).

## Review Focus

Failure modes most likely to bite a player; each pinned by a test in the owning task:

1. **Tree or catalog missing, stale, or a tree spell's plugin removed/renamed after a load-order change.** Owner: Task 3 (`PoolSelfTest`: a node whose `persistentId` is not in the catalog is counted `notInCatalog`; the game layer's resolver callback returning `nullopt` drops the spell and counts `unresolved`), Task 9 logs the counts and the report lists them. Catalog `version != 1` (`plugins/spelllearning/include/librarian/Librarian.h:234`) or a tree without `schools` stops the addon with a reason.
2. **A tree spell whose engine school (`GetAssociatedSkill`) differs from the catalog school.** Owner: Task 3 (`ApplyEngineSchool` test: a Destruction-catalogued spell reported as Restoration by the callback moves to Restoration and increments `schoolMismatch`; a `kNone` school drops it). The engine stops walking a list when it meets a spell of another school, so a wrong-school entry would hide every entry after it.
3. **A slot overflowing 255 entries (every flag-1 "below" list holds up to a whole school; TAKEALOOK has ~290 spells per school).** Owner: Task 5 (`BuildSelfTest`: a slot of <= 255 spells builds one list, never split; a 700-spell synthetic school builds 3 chunks of <= 255 each, every chunk holds every tier the slot holds; 800 spells drops the surplus and counts `droppedOverCapacity`).
4. **NPCs whose spell list, class, race or factions come from a template, including a leveled-character template.** Owner: Task 7 (`TemplateSelfTest`: `kSpells` set -> ineligible; a chain whose kStats provider is a `TESLevCharacter` -> `unresolvableStats`, the record's own class is used and the NPC is counted; a concrete-NPC chain resolves class at depth 2).
5. **The ESL record is missing (plugin not enabled, user compacted/renamed it, record of another type at the FormID).** Owner: Task 6 (ESL read-back test asserts exactly `SlotTable().size() * kMaxChunks` LVSP records at `0x01000800..`), Task 8 (`LookupLoadedLightModByName` null -> "not loaded" error path; `As<TESLevSpell>()` null -> "wrong type" counted per slot), Task 7 (`DecideNpc` never attaches a (slot, chunk) absent from `ListIndex`). A mod renaming vanilla EditorIDs is covered by Task 1 (the parser rejects every non-FormID entry, vanilla or mod); a user ladder of the wrong length is rejected by `ParseRules` (Task 1), so the DLL never indexes past `kRungCount`.

## File Structure

Create:
- `plugins/NPCSpells/CMakeLists.txt` - DLL target `HeartOfMagic_NPCSpells` (copy of BookXP's `add_commonlibsse_plugin` call) plus a `npcspells_core` object list shared with tools.
- `plugins/NPCSpells/README.md` - player and developer documentation (what it does, rules file, report, checklist).
- `plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json` - shipped rules table (§5.3 content).
- `plugins/NPCSpells/src/core/NpcSpellsTypes.h` - `School`, `Tier`, `ClassTier`, `UndeadPolicy`, `kTierFloor`, `kMaxEntries`, `kRungCount`, name<->enum helpers.
- `plugins/NPCSpells/src/core/NpcSpellsRules.h/.cpp` - `Rules` struct, `DefaultRules()`, `ParseRules()`, FormID-string parsing.
- `plugins/NPCSpells/src/core/NpcSpellsClassTier.h/.cpp` - `ClassifyClass`, `IsConjurationClass`, `SchoolsWeighted`.
- `plugins/NPCSpells/src/core/NpcSpellsPool.h/.cpp` - tree ∩ catalog -> `SpellFacts`, tag/exclusion filters, `ApplyEngineSchool`.
- `plugins/NPCSpells/src/core/NpcSpellsSlots.h/.cpp` - built-in `kGroups`, `Variant`, `SlotKey`, `SlotTable()`, `LocalFormId()`, EditorIDs.
- `plugins/NPCSpells/src/core/NpcSpellsBuild.h/.cpp` - `BuildLists`: per-slot membership, sort, round-robin chunking, flags.
- `plugins/NPCSpells/src/core/NpcSpellsTemplate.h/.cpp` - template chain provider resolution (pure).
- `plugins/NPCSpells/src/core/NpcSpellsNpc.h/.cpp` - `Dice`, `DecideNpc` (eligibility, group, undead policy, rung/variant/chunk choice).
- `plugins/NPCSpells/src/core/NpcSpellsEsl.h/.cpp` - `BuildEslBytes(const std::vector<SlotDef>&)`, the TES4/GRUP/LVSP writer (pure bytes).
- `plugins/NPCSpells/src/core/NpcSpellsReport.h/.cpp` - `ReportData`, `RenderReport`.
- `plugins/NPCSpells/src/game/GameFiles.h/.cpp` - reads tree, catalog, rules JSON, the `*_DISTR.ini` texts only (via `TomeReach::ConfigText`).
- `plugins/NPCSpells/src/game/GamePool.h/.cpp` - persistentId -> `SpellItem*`, engine school.
- `plugins/NPCSpells/src/game/GameLists.h/.cpp` - ESL record lookup, entry filling.
- `plugins/NPCSpells/src/game/GameNpcs.h/.cpp` - `TESNPC` -> `NpcFacts` (template chain), `AddLevSpells` once per NPC.
- `plugins/NPCSpells/src/game/GameRun.h/.cpp` - orchestration, timing, report write.
- `plugins/NPCSpells/src/Main.cpp` - `SKSEPluginLoad`, `SetupLog`, `kDataLoaded`.
- `tools/npcspells-test.cpp` - harness main (`--self-test`, exit 1 on failure; `--tree <f> --catalog <f>` prints slot stats on real files).
- `tools/npcspells-selftest-rules.cpp`, `tools/npcspells-selftest-build.cpp`, `tools/npcspells-selftest-npc.cpp`, `tools/npcspells-selftest-esl.cpp` - suites.
- `tools/include/NpcSpellsCheck.h` - `Check()`, counters shared by the suites.
- `tools/npcspells-esl.cpp` - writes `HeartOfMagic_NPCSpells.esp` from `SlotTable()`.
- `plugins/cmake/DistributionNpcSpells.cmake` - `assemble_npcspells` target and its zip.

Modify:
- `CMakeLists.txt` (top level) - `add_subdirectory(plugins/NPCSpells)`, `include(DistributionNpcSpells)`.
- `tools/CMakeLists.txt` - `npcspells-test`, `npcspells-esl`, `npcspells_esl` custom target.
- `DeployDev.ps1` - `-NpcSpellsOnly` switch and `$NpcSpellsModPath`.
- `CLAUDE.md` - harness rows (Tasks 1, 6), "four DLLs" and key locations row (Task 8), DeployDev and dist lines (Task 11), decision tree entry and documentation row (Task 12, with the README they point to).
- `docs/ARCHITECTURE.md` - DLL list, file-structure lines for `plugins/NPCSpells/` (Task 12).
- `docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md` - §8 TAKEALOOK timing line (Task 12). (§4 per-NPC report, §5.1 own-tier share and the two-group `never` limit, §5.3 FormID-only refs and §7 unresolved refs were updated with this plan, before Task 1.)

---

### Task 1: Harness scaffold, types, rules table

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsTypes.h`, `plugins/NPCSpells/src/core/NpcSpellsRules.h`, `plugins/NPCSpells/src/core/NpcSpellsRules.cpp`, `plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json`, `tools/include/NpcSpellsCheck.h`, `tools/npcspells-test.cpp`, `tools/npcspells-selftest-rules.cpp`, stubs `tools/npcspells-selftest-build.cpp`, `tools/npcspells-selftest-npc.cpp`, `tools/npcspells-selftest-esl.cpp`
- Modify: `tools/CMakeLists.txt`, `CLAUDE.md` (harness row)
- Test: `tools/npcspells-selftest-rules.cpp`

**Interfaces**
- Produces (`NpcSpellsTypes.h`, complete file):
```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
namespace NpcSpells {
    enum class School : std::uint8_t { Alteration = 0, Conjuration, Destruction, Illusion, Restoration, Count };
    enum class Tier : std::uint8_t { Novice = 0, Apprentice, Adept, Expert, Master, Count };
    enum class ClassTier : std::uint8_t { NonMage, Hybrid, Mage };
    enum class UndeadPolicy : std::uint8_t { NoUndead, Conjurer, Necromancer };
    inline constexpr std::size_t kSchoolCount = 5;
    inline constexpr std::size_t kTierCount = 5;
    inline constexpr std::array<std::uint16_t, kTierCount> kTierFloor{ 0, 25, 50, 75, 100 };
    inline constexpr std::size_t kMaxEntries = 255;   // TESLeveledList::numEntries is uint8
    inline constexpr std::size_t kRungCount = 5;      // ladder length; the slot table has 5 rungs per (school, group, variant)
    inline constexpr std::array<std::string_view, kSchoolCount> kSchoolNames{ "alteration", "conjuration", "destruction", "illusion", "restoration" };
    inline constexpr std::array<std::string_view, kTierCount> kTierNames{ "novice", "apprentice", "adept", "expert", "master" };
    // inline: defined in the header, included by many TUs
    inline std::optional<School> SchoolFromName(std::string_view lowered) {
        for (std::size_t i = 0; i < kSchoolCount; ++i) if (kSchoolNames[i] == lowered) return static_cast<School>(i);
        return std::nullopt;
    }
    inline std::optional<Tier> TierFromName(std::string_view lowered) {
        for (std::size_t i = 0; i < kTierCount; ++i) if (kTierNames[i] == lowered) return static_cast<Tier>(i);
        return std::nullopt;
    }
}
```
- Produces (`NpcSpellsRules.h`; includes `core/NpcSpellsTypes.h`, `<optional>`, `<string>`, `<string_view>`, `<vector>`):
```cpp
namespace NpcSpells {
    struct FormRef { std::string pluginLower; std::uint32_t local = 0; };  // "XXXXXX:Plugin.ext" only, mod records included
    struct Rung { Tier startTier; bool below; double chance; };
    struct GroupRule { std::string name; std::vector<FormRef> factions; std::vector<std::string> prefer; double preferShare = 0.75; std::vector<std::string> never; };
    struct Rules {
        std::array<double, kTierCount> tierTarget{ 1.0, 1.25, 1.5, 2.0, 2.5 };
        std::vector<Rung> ladder;             // default: see DefaultRules()
        double hybridScale = 0.5;
        double nonMageChance = 0.05;
        std::vector<std::string> nonMageTags{ "curing", "armor" };
        int mageMinMagicShareTenths = 6;      // magicSum*10 >= total*6
        int hybridMinMagicShareTenths = 3;
        std::vector<FormRef> necromancerClasses;
        double conjurationShare = 0.75;
        std::vector<std::string> excludeTags{ "cloak", "trap", "sense" };
        std::vector<std::string> excludeSpells;   // persistentIds (crash/script reject list, default empty)
        std::vector<FormRef> beastRaces;
        std::vector<GroupRule> groups;
        std::vector<FormRef> undeadExceptNpcs;
        FormRef redguardRace;
        FormRef keywordNpc, keywordCreature, keywordAnimal;
    };
    Rules DefaultRules();
    // Parses the user JSON (§5.3 shape). Returns false and fills error on malformed JSON or a ladder that is not kRungCount rungs;
    // unknown keys are ignored; an entry that is not a FormID drops that entry only (error names it, §7).
    bool ParseRules(const std::string& jsonText, Rules& out, std::string& error);
    // "0C969F:Skyrim.esm" -> FormRef; anything else (a bare EditorID, "EditorID:Plugin.esp", more than 6 hex digits, no plugin) -> nullopt + error.
    std::optional<FormRef> ParseFormRef(std::string_view text, std::string& error);
}
```
- Consumes: nlohmann::json.

**Steps**

- [ ] Write `tools/include/NpcSpellsCheck.h`:
```cpp
#pragma once
#include <cstdio>
namespace NpcSpellsCheck {
    inline int g_failed = 0;
    inline int g_passed = 0;
    inline void Check(bool ok, const char* what) {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }
}
int RunRulesSelfTest();
int RunBuildSelfTest();
int RunNpcSelfTest();
int RunEslSelfTest();
```
- [ ] Write the failing test `tools/npcspells-selftest-rules.cpp`:
```cpp
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsRules.h"
#include <cmath>
#include <fstream>
#include <sstream>
using namespace NpcSpells;
using NpcSpellsCheck::Check;

int RunRulesSelfTest()
{
    std::printf("rules\n");
    std::string err;
    const auto necro = ParseFormRef("0C969F:Skyrim.esm", err);
    Check(necro && necro->pluginLower == "skyrim.esm" && necro->local == 0x0C969F, "FormID:Plugin parses");
    const auto modRef = ParseFormRef("000D62:MyMod.esp", err);
    Check(modRef && modRef->pluginLower == "mymod.esp" && modRef->local == 0xD62, "a mod record is a FormID too");
    const auto bad = ParseFormRef("OREO_HagravenRace:Skyrim.esm", err);
    Check(!bad, "an EditorID with a plugin is rejected");
    const auto modId = ParseFormRef("MyMod_Faction", err);
    Check(!modId && !err.empty(), "a bare EditorID is rejected (FormIDs only)");

    const Rules defaults = DefaultRules();
    Check(defaults.ladder.size() == kRungCount, "default ladder has five rungs");
    double expected[5]{};
    for (const auto& rung : defaults.ladder)
        for (std::size_t t = static_cast<std::size_t>(rung.startTier); t < kTierCount; ++t) expected[t] += rung.chance;
    bool ladderOk = true;
    for (std::size_t t = 0; t < kTierCount; ++t) ladderOk = ladderOk && std::abs(expected[t] - defaults.tierTarget[t]) < 1e-9;
    Check(ladderOk, "ladder expectations equal tierTarget per tier (1.0 1.25 1.5 2.0 2.5)");
    Check(defaults.groups.size() == 5 && defaults.groups[1].name == "Vampire", "five default groups in spec order");

    std::ifstream shipped(NPCSPELLS_DATA_DIR "/HeartOfMagic_NPCSpells.json");   // compile definition, absolute path (CWD-independent)
    std::stringstream buf; buf << shipped.rdbuf();
    Rules parsed;
    Check(ParseRules(buf.str(), parsed, err), "shipped JSON parses");
    Check(parsed.groups.size() == defaults.groups.size() && parsed.necromancerClasses.size() == 1 &&
          parsed.excludeTags == defaults.excludeTags, "shipped JSON equals built-in defaults");
    Check(parsed.keywordNpc.local == 0x013794 && parsed.keywordCreature.local == 0x013795 && parsed.keywordAnimal.local == 0x013798 &&
          parsed.keywordAnimal.pluginLower == "skyrim.esm", "shipped keywords are read");
    Rules keywordsEdited;
    Check(ParseRules(R"({ "keywords": { "animal": "000801:MyMod.esp" } })", keywordsEdited, err) && keywordsEdited.keywordAnimal.local == 0x801 &&
          keywordsEdited.keywordAnimal.pluginLower == "mymod.esp" && keywordsEdited.keywordNpc.local == 0x013794, "an edited keyword replaces only that keyword");
    Rules unresolved; err.clear();
    Check(ParseRules(R"({ "beastRaces": ["0CDD84:Skyrim.esm", "SomeRace"] })", unresolved, err) && unresolved.beastRaces.size() == 1 && !err.empty(),
          "a non-FormID entry drops that entry only, the rest of the list stays (§7)");
    Rules shortLadder; err.clear();
    Check(!ParseRules(R"({ "ladder": [ { "startTier": "novice", "below": false, "chance": 1.0 } ] })", shortLadder, err) && !err.empty(),
          "a ladder that is not five rungs is rejected");
    Rules broken;
    Check(!ParseRules("{ not json", broken, err) && !err.empty(), "malformed JSON reports an error");
    return NpcSpellsCheck::g_failed;
}
```
- [ ] Write `tools/npcspells-test.cpp`:
```cpp
// npcspells-test  -  offline harness for the NPC Spells addon's RE-free core
//   npcspells-test [--self-test]            all suites, exit 1 on any failure
//   npcspells-test --tree <spell_tree.json> --catalog <spell_catalog.json>   slot statistics on real files (Task 5)
#include "NpcSpellsCheck.h"
#include <cstdio>
#include <string>
// Stub until Task 5 replaces it in this file (slot statistics on real files)
int RunRealFiles(const std::string&, const std::string&) { return 0; }
int main(int argc, char** argv)
{
    std::string tree, catalog;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) tree = argv[++i];
        else if (a == "--catalog" && i + 1 < argc) catalog = argv[++i];
    }
    if (!tree.empty() && !catalog.empty()) return RunRealFiles(tree, catalog);
    RunRulesSelfTest();
    RunBuildSelfTest();
    RunNpcSelfTest();
    RunEslSelfTest();
    std::printf("\n%d passed, %d failed\n", NpcSpellsCheck::g_passed, NpcSpellsCheck::g_failed);
    return NpcSpellsCheck::g_failed == 0 ? 0 : 1;
}
```
  Until Tasks 3/7/4 land, `RunBuildSelfTest`, `RunNpcSelfTest`, `RunEslSelfTest` are stubs returning 0 in `tools/npcspells-selftest-build.cpp`, `-npc.cpp`, `-esl.cpp` (created now, each `#include "NpcSpellsCheck.h"` plus the one function; filled later). `RunRealFiles` is the stub in `npcspells-test.cpp` above, not in a suite file, so Task 3 rewriting the build suite cannot drop it.
- [ ] Add to `tools/CMakeLists.txt`:
```cmake
# =============================================================================
# npcspells-test  -  offline harness for the NPC Spells addon core
# =============================================================================
set(NPCSPELLS_CORE_DIR "${CMAKE_SOURCE_DIR}/plugins/NPCSpells/src")
set(NPCSPELLS_CORE_SOURCES
    ${NPCSPELLS_CORE_DIR}/core/NpcSpellsRules.cpp
)
add_executable(npcspells-test
    npcspells-test.cpp
    npcspells-selftest-rules.cpp
    npcspells-selftest-build.cpp
    npcspells-selftest-npc.cpp
    npcspells-selftest-esl.cpp
    ${NPCSPELLS_CORE_SOURCES}
)
target_compile_features(npcspells-test PRIVATE cxx_std_23)
target_include_directories(npcspells-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NPCSPELLS_CORE_DIR}
    ${SL_SRC_DIR}
)
target_link_libraries(npcspells-test PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json)
# The rules suite reads the shipped JSON by absolute path, so the harness runs from any directory
target_compile_definitions(npcspells-test PRIVATE NPCSPELLS_DATA_DIR="${CMAKE_SOURCE_DIR}/plugins/NPCSpells/data")
```
- [ ] Check SkyrimSE is not running, then build: `if (Get-Process -Name "SkyrimSE" -ErrorAction SilentlyContinue) { Write-Host "Skyrim is running - close it first" } else { .\BuildRelease.ps1 -preset Release-2026 }` - expected: link error (`ParseFormRef` undefined) or, with an empty `NpcSpellsRules.cpp`, the test binary runs: `.\build\tools\Release\npcspells-test.exe` prints `[FAIL]` lines and exits 1.
- [ ] `NpcSpellsTypes.h` is the complete file in Interfaces above. Implement `NpcSpellsRules.cpp`:
```cpp
#include "core/NpcSpellsRules.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
namespace NpcSpells {
namespace {
    constexpr std::size_t kMaxLocalHexDigits = 6;
    std::string Lower(std::string_view s) { std::string r(s); for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return r; }
    FormRef Ref(const char* text) { std::string e; return *ParseFormRef(text, e); }
    template <class T> void Read(const nlohmann::json& j, const char* key, T& out) { if (auto it = j.find(key); it != j.end()) out = it->get<T>(); }
    // A bad entry drops that entry only (§7); the rest of the list stays
    void ReadRefs(const nlohmann::json& j, const char* key, std::vector<FormRef>& out, std::string& error) {
        auto it = j.find(key); if (it == j.end() || !it->is_array()) return;
        out.clear();
        for (const auto& v : *it) { if (auto ref = ParseFormRef(v.get<std::string>(), error)) out.push_back(*ref); }
    }
    void ReadRef(const nlohmann::json& j, const char* key, FormRef& out, std::string& error) {
        if (auto it = j.find(key); it != j.end() && it->is_string()) if (auto ref = ParseFormRef(it->get<std::string>(), error)) out = *ref;
    }
}
std::optional<FormRef> ParseFormRef(std::string_view text, std::string& error)
{
    const auto colon = text.find(':');
    const auto hex = colon == std::string_view::npos ? std::string_view{} : text.substr(0, colon);
    const bool allHex = !hex.empty() && hex.size() <= kMaxLocalHexDigits && std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    if (!allHex || colon + 1 >= text.size()) { error = "not a FormID (XXXXXX:Plugin.ext): " + std::string(text); return std::nullopt; }
    FormRef ref;
    ref.pluginLower = Lower(text.substr(colon + 1));
    ref.local = static_cast<std::uint32_t>(std::stoul(std::string(hex), nullptr, 16));
    return ref;
}
Rules DefaultRules()
{
    Rules r;
    r.ladder = { { Tier::Novice, false, 1.0 }, { Tier::Apprentice, true, 0.25 }, { Tier::Adept, true, 0.25 }, { Tier::Expert, true, 0.5 }, { Tier::Master, false, 0.5 } };
    r.necromancerClasses = { Ref("0C969F:Skyrim.esm") };
    r.beastRaces = { Ref("0CDD84:Skyrim.esm"), Ref("00283A:Dawnguard.esm") };
    r.groups = {
        { "Forsworn", { Ref("043599:Skyrim.esm"), Ref("04359E:Skyrim.esm") }, { "nature", "earth", "flesh", "fire", "curse" }, 0.75, { "sun" } },
        { "Vampire", { Ref("027242:Skyrim.esm"), Ref("003376:Dawnguard.esm") }, { "siphon", "health", "shadow", "frost", "undead" }, 0.75, { "sun", "light", "curing" } },
        { "Necromancer", { Ref("034B74:Skyrim.esm") }, { "undead", "soul", "frost", "siphon" }, 0.75, { "sun" } },
        { "Thalmor", { Ref("039F26:Skyrim.esm"), Ref("0B80F7:Skyrim.esm"), Ref("0135A0:Skyrim.esm") }, { "shock", "fire", "arcane", "light", "shield" }, 0.75, { "undead" } },
        { "Vigilant", { Ref("0B3292:Skyrim.esm") }, { "sun", "light", "curing", "shield" }, 0.75, { "undead", "daedra", "shadow" } },
    };
    r.undeadExceptNpcs = { Ref("0135E9:Skyrim.esm") };   // Falion
    r.redguardRace = Ref("013748:Skyrim.esm");
    r.keywordNpc = Ref("013794:Skyrim.esm"); r.keywordCreature = Ref("013795:Skyrim.esm"); r.keywordAnimal = Ref("013798:Skyrim.esm");
    return r;
}
bool ParseRules(const std::string& jsonText, Rules& out, std::string& error)
{
    nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { error = "rules JSON is not an object"; return false; }
    out = DefaultRules();
    if (auto t = j.find("tierTarget"); t != j.end())
        for (std::size_t i = 0; i < kTierCount; ++i) Read(*t, std::string(kTierNames[i]).c_str(), out.tierTarget[i]);
    if (auto l = j.find("ladder"); l != j.end() && l->is_array()) {
        out.ladder.clear();
        for (const auto& rung : *l) {
            const auto tier = TierFromName(rung.value("startTier", "novice"));
            if (!tier) { error = "ladder: unknown tier"; return false; }
            out.ladder.push_back({ *tier, rung.value("below", false), rung.value("chance", 1.0) });
        }
        // The slot table has kRungCount rungs; InSlot/BuildLists index rules.ladder[rung] for each
        if (out.ladder.size() != kRungCount) { error = "ladder must have 5 rungs"; return false; }
    }
    Read(j, "hybridScale", out.hybridScale); Read(j, "nonMageChance", out.nonMageChance); Read(j, "nonMageTags", out.nonMageTags);
    if (auto c = j.find("classTiers"); c != j.end()) {
        double mage = 0.6, hybrid = 0.3; Read(*c, "mageMinMagicShare", mage); Read(*c, "hybridMinMagicShare", hybrid);
        out.mageMinMagicShareTenths = static_cast<int>(std::lround(mage * 10)); out.hybridMinMagicShareTenths = static_cast<int>(std::lround(hybrid * 10));
    }
    if (auto c = j.find("conjuration"); c != j.end()) { ReadRefs(*c, "necromancerClasses", out.necromancerClasses, error); Read(*c, "share", out.conjurationShare); }
    Read(j, "excludeTags", out.excludeTags); Read(j, "excludeSpells", out.excludeSpells);
    ReadRefs(j, "beastRaces", out.beastRaces, error);
    if (auto g = j.find("groups"); g != j.end() && g->is_array()) {
        for (const auto& gj : *g) {
            const std::string name = gj.value("name", "");
            auto it = std::find_if(out.groups.begin(), out.groups.end(), [&](const GroupRule& r) { return r.name == name; });
            if (it == out.groups.end()) continue;   // unknown group names are logged by the caller (built-in slot table)
            ReadRefs(gj, "factions", it->factions, error); Read(gj, "prefer", it->prefer); Read(gj, "preferShare", it->preferShare); Read(gj, "never", it->never);
        }
    }
    if (auto u = j.find("undead"); u != j.end()) ReadRefs(*u, "exceptNpcs", out.undeadExceptNpcs, error);
    if (auto rg = j.find("redguard"); rg != j.end()) ReadRef(*rg, "race", out.redguardRace, error);
    if (auto k = j.find("keywords"); k != j.end()) { ReadRef(*k, "npc", out.keywordNpc, error); ReadRef(*k, "creature", out.keywordCreature, error); ReadRef(*k, "animal", out.keywordAnimal, error); }
    return true;
}
}
```
- [ ] Write `plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json` with exactly the keys `ParseRules` reads (a shipped key that does nothing would mislead users). The §5.3 keys `ParseRules` does not read - `tierSkill` (the engine's skill bands), `ownTierShare` (realised by the ladder), `conjuration.necromancerGroups` / `necromancerPrefer` / `conjurerAvoidPrefer` (built-in group policies, `kGroups`), `undead.groups`, `undead.conjurerClasses` (computed from weights), `redguard.mageOnly` / `undeadOnlyVia` (built-in Redguard rule) - are left out and listed in the README (Task 12) as fixed values. Keywords verified with houseCARL 2026-10-08 (ActorTypeNPC / ActorTypeCreature / ActorTypeAnimal):
```json
{
  "tierTarget": { "novice": 1.0, "apprentice": 1.25, "adept": 1.5, "expert": 2.0, "master": 2.5 },
  "ladder": [
    { "startTier": "novice", "below": false, "chance": 1.0 },
    { "startTier": "apprentice", "below": true, "chance": 0.25 },
    { "startTier": "adept", "below": true, "chance": 0.25 },
    { "startTier": "expert", "below": true, "chance": 0.5 },
    { "startTier": "master", "below": false, "chance": 0.5 }
  ],
  "hybridScale": 0.5,
  "nonMageChance": 0.05,
  "nonMageTags": ["curing", "armor"],
  "classTiers": { "mageMinMagicShare": 0.6, "hybridMinMagicShare": 0.3 },
  "conjuration": { "necromancerClasses": ["0C969F:Skyrim.esm"], "share": 0.75 },
  "excludeTags": ["cloak", "trap", "sense"],
  "excludeSpells": [],
  "beastRaces": ["0CDD84:Skyrim.esm", "00283A:Dawnguard.esm"],
  "groups": [
    { "name": "Forsworn", "factions": ["043599:Skyrim.esm", "04359E:Skyrim.esm"],
      "prefer": ["nature", "earth", "flesh", "fire", "curse"], "preferShare": 0.75, "never": ["sun"] },
    { "name": "Vampire", "factions": ["027242:Skyrim.esm", "003376:Dawnguard.esm"],
      "prefer": ["siphon", "health", "shadow", "frost", "undead"], "preferShare": 0.75, "never": ["sun", "light", "curing"] },
    { "name": "Necromancer", "factions": ["034B74:Skyrim.esm"],
      "prefer": ["undead", "soul", "frost", "siphon"], "preferShare": 0.75, "never": ["sun"] },
    { "name": "Thalmor", "factions": ["039F26:Skyrim.esm", "0B80F7:Skyrim.esm", "0135A0:Skyrim.esm"],
      "prefer": ["shock", "fire", "arcane", "light", "shield"], "preferShare": 0.75, "never": ["undead"] },
    { "name": "Vigilant", "factions": ["0B3292:Skyrim.esm"],
      "prefer": ["sun", "light", "curing", "shield"], "preferShare": 0.75, "never": ["undead", "daedra", "shadow"] }
  ],
  "undead": { "exceptNpcs": ["0135E9:Skyrim.esm"] },
  "redguard": { "race": "013748:Skyrim.esm" },
  "keywords": { "npc": "013794:Skyrim.esm", "creature": "013795:Skyrim.esm", "animal": "013798:Skyrim.esm" }
}
```
- [ ] `CLAUDE.md`, "No C++ Test Framework" harness list: add `npcspells-test` (the NPC Spells addon's RE-free core: `npcspells-test` runs every suite, exit 1 on a failure; the shipped rules JSON is read from `plugins/NPCSpells/data/`). Task 5 adds its `--tree --catalog` mode to the same row.
- [ ] Rebuild (same SkyrimSE check) and run `.\build\tools\Release\npcspells-test.exe` - expected: all `rules` lines `[PASS]`, exit 0.
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsTypes.h plugins/NPCSpells/src/core/NpcSpellsRules.h plugins/NPCSpells/src/core/NpcSpellsRules.cpp plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json tools/include/NpcSpellsCheck.h tools/npcspells-test.cpp tools/npcspells-selftest-rules.cpp tools/npcspells-selftest-build.cpp tools/npcspells-selftest-npc.cpp tools/npcspells-selftest-esl.cpp tools/CMakeLists.txt CLAUDE.md; git commit -m "NPC Spells: rules table and offline harness scaffold" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 2: Class tier classification

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsClassTier.h`, `plugins/NPCSpells/src/core/NpcSpellsClassTier.cpp`
- Modify: `tools/CMakeLists.txt` (add the .cpp to `NPCSPELLS_CORE_SOURCES`), `tools/npcspells-selftest-rules.cpp` (append suite)
- Test: `tools/npcspells-selftest-rules.cpp`

**Interfaces** (`NpcSpellsClassTier.h` includes `core/NpcSpellsRules.h`, `<array>`, `<cstdint>`)
```cpp
namespace NpcSpells {
    // The 18 CLASS_DATA::SkillWeights in record order (RE/T/TESClass.h:36-59); magic schools are indices 12..16.
    struct ClassWeights { std::array<std::uint8_t, 18> w{}; };
    inline constexpr std::size_t kFirstMagicSkill = 12;
    ClassTier ClassifyClass(const ClassWeights& weights, const Rules& rules);   // integer compare
    bool IsConjurationClass(const ClassWeights& weights);                      // conj >= 3 and >= every other magic weight
    std::array<bool, kSchoolCount> SchoolsWeighted(const ClassWeights& weights);  // weight > 0 per school (hybrid scope)
}
```

**Steps**
- [ ] Add `#include "core/NpcSpellsClassTier.h"` to the include block of `tools/npcspells-selftest-rules.cpp` (`<cmath>` is already there from Task 1), then append to `RunRulesSelfTest()` (before `return`):
```cpp
    auto W = [](std::initializer_list<int> magic, int nonMagicSum) { ClassWeights c; int i = 0; for (int m : magic) c.w[kFirstMagicSkill + i++] = static_cast<std::uint8_t>(m); c.w[0] = static_cast<std::uint8_t>(nonMagicSum); return c; };
    // Skyrim.esm values (ledger): CombatMageConjurer alt1 conj3 des2 ill1 res3 = 10 of 11 -> .909
    Check(ClassifyClass(W({ 1, 3, 2, 1, 3 }, 1), defaults) == ClassTier::Mage, "CombatMageConjurer .909 is a mage");
    Check(ClassifyClass(W({ 2, 1, 2, 1, 1 }, 4), defaults) == ClassTier::Mage, "CombatSorcerer .636 is a mage");
    Check(ClassifyClass(W({ 1, 1, 2, 1, 1 }, 5), defaults) == ClassTier::Hybrid, "CombatWitchblade .545 is hybrid");
    Check(ClassifyClass(W({ 1, 0, 1, 1, 1 }, 7), defaults) == ClassTier::Hybrid, "CombatMonk .364 is hybrid");
    Check(ClassifyClass(W({ 0, 0, 1, 1, 1 }, 8), defaults) == ClassTier::NonMage, "CombatScout .273 is a non-mage");
    Check(ClassifyClass(W({ 0, 0, 0, 0, 0 }, 9), defaults) == ClassTier::NonMage, "a warrior with no magic weight is a non-mage");
    Check(ClassifyClass(W({ 3, 0, 3, 0, 0 }, 4), defaults) == ClassTier::Mage, "exactly 0.600 counts as mage (integer compare)");
    Check(ClassifyClass(W({ 3, 0, 0, 0, 0 }, 7), defaults) == ClassTier::Hybrid, "exactly 0.300 counts as hybrid");
    Check(IsConjurationClass(W({ 1, 3, 2, 1, 3 }, 1)), "conjuration 3 = restoration 3 tie is a conjuration class");
    Check(!IsConjurationClass(W({ 1, 3, 4, 1, 1 }, 1)), "destruction above conjuration is not (conjuration 3 meets the floor)");
    Check(!IsConjurationClass(W({ 0, 2, 0, 0, 0 }, 1)), "conjuration 2 is below the floor of 3");
    const auto scope = SchoolsWeighted(W({ 1, 0, 2, 0, 1 }, 3));
    Check(scope[0] && !scope[1] && scope[2] && !scope[3] && scope[4], "weighted schools are those with weight > 0");
```
- [ ] Build (SkyrimSE check first) and run the harness - expected: compile error (missing header) -> after adding an empty header, link errors; then `[FAIL]`s.
- [ ] Implement `NpcSpellsClassTier.cpp`:
```cpp
#include "core/NpcSpellsClassTier.h"
namespace NpcSpells {
ClassTier ClassifyClass(const ClassWeights& c, const Rules& rules)
{
    int total = 0, magic = 0;
    for (std::size_t i = 0; i < c.w.size(); ++i) { total += c.w[i]; if (i >= kFirstMagicSkill && i < kFirstMagicSkill + kSchoolCount) magic += c.w[i]; }
    if (total == 0) return ClassTier::NonMage;
    if (magic * 10 >= total * rules.mageMinMagicShareTenths) return ClassTier::Mage;
    if (magic * 10 >= total * rules.hybridMinMagicShareTenths) return ClassTier::Hybrid;
    return ClassTier::NonMage;
}
bool IsConjurationClass(const ClassWeights& c)
{
    constexpr int kConjurationFloor = 3;
    const int conj = c.w[kFirstMagicSkill + static_cast<std::size_t>(School::Conjuration)];
    if (conj < kConjurationFloor) return false;
    for (std::size_t s = 0; s < kSchoolCount; ++s) if (c.w[kFirstMagicSkill + s] > conj) return false;
    return true;
}
std::array<bool, kSchoolCount> SchoolsWeighted(const ClassWeights& c)
{
    std::array<bool, kSchoolCount> out{};
    for (std::size_t s = 0; s < kSchoolCount; ++s) out[s] = c.w[kFirstMagicSkill + s] > 0;
    return out;
}
}
```
- [ ] Build, run harness - expected: all PASS, exit 0.
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsClassTier.h plugins/NPCSpells/src/core/NpcSpellsClassTier.cpp tools/CMakeLists.txt tools/npcspells-selftest-rules.cpp; git commit -m "NPC Spells: class tier classification from skill weights" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 3: Spell pool from tree and catalog

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsPool.h`, `plugins/NPCSpells/src/core/NpcSpellsPool.cpp`
- Modify: `tools/CMakeLists.txt`, `tools/npcspells-selftest-build.cpp` (start the suite)
- Test: `tools/npcspells-selftest-build.cpp`

**Interfaces** (`NpcSpellsPool.h` includes `core/NpcSpellsRules.h`, `<nlohmann/json.hpp>`, `<functional>`, `<optional>`, `<string>`, `<vector>`)
```cpp
namespace NpcSpells {
    struct SpellFacts {
        std::string persistentId;           // "Plugin.esp|0x00XXXX" (catalog key)
        School school;                      // catalog school, replaced by the engine school in ApplyEngineSchool
        Tier tier;
        std::vector<std::string> elements;  // catalog "elements"
        std::vector<std::string> techniques;// catalog "techniques"
        bool undeadRule = false;            // element "undead" and technique "summoning" or "control" (§5.1 undead rule spells)
        bool HasAnyTag(const std::vector<std::string>& tags) const;
    };
    struct PoolStats { std::size_t treeNodes = 0, notInCatalog = 0, noSchoolOrTier = 0, excludedTag = 0, excludedList = 0, excludedDistr = 0, unresolved = 0, schoolMismatch = 0, kept = 0; };
    using DistributedFn = std::function<bool(const std::string& persistentId)>;   // another *_DISTR.ini (SPID) already names it
    // Stops with an empty pool and stats.treeNodes == 0 when catalog["version"] != 1 or tree has no "schools".
    std::vector<SpellFacts> BuildPool(const nlohmann::json& tree, const nlohmann::json& catalog, const Rules& rules, const DistributedFn& distributed, PoolStats& stats);
    using EngineSchoolFn = std::function<std::optional<School>(const std::string& persistentId)>;  // nullopt = form not found or not a spell
    void ApplyEngineSchool(std::vector<SpellFacts>& pool, const EngineSchoolFn& engineSchool, PoolStats& stats);
    inline constexpr int kCatalogVersion = 1;   // plugins/spelllearning/include/librarian/Librarian.h:234
}
```

**Steps**
- [ ] Write the failing test (`tools/npcspells-selftest-build.cpp`, `RunBuildSelfTest` first half; keep helpers in an anonymous namespace):
```cpp
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsPool.h"
#include <nlohmann/json.hpp>
#include <algorithm>
using namespace NpcSpells; using NpcSpellsCheck::Check; using json = nlohmann::json;
namespace {
    json Node(const char* pid) { return json{ { "formId", "0x00000000" }, { "persistentId", pid } }; }
    json Entry(const char* school, const char* tier, std::vector<std::string> el, std::vector<std::string> te) {
        return json{ { "school", school }, { "tier", tier }, { "elements", el }, { "techniques", te } }; }
    json SmallTree() { return json{ { "schools", { { "Destruction", { { "nodes", json::array({ Node("A.esp|0x000801"), Node("A.esp|0x000802"), Node("A.esp|0x000803"), Node("A.esp|0x000809") }) } } },
                                                   { "Conjuration", { { "nodes", json::array({ Node("A.esp|0x000804"), Node("A.esp|0x000805"), Node("A.esp|0x000806") }) } } },
                                                   { "Restoration", { { "nodes", json::array({ Node("A.esp|0x000807"), Node("A.esp|0x000808") }) } } } } } }; }
    json SmallCatalog() {
        json c; c["version"] = 1; c["spells"] = json::object();
        c["spells"]["A.esp|0x000801"] = Entry("destruction", "novice", { "fire" }, { "curse" });
        c["spells"]["A.esp|0x000802"] = Entry("destruction", "adept", { "fire" }, { "cloak" });      // excluded technique
        c["spells"]["A.esp|0x000803"] = Entry("destruction", "master", { "frost" }, {});
        c["spells"]["A.esp|0x000804"] = Entry("conjuration", "apprentice", { "undead", "flesh" }, { "control", "infuse" }); // Raise Zombie
        c["spells"]["A.esp|0x000805"] = Entry("conjuration", "adept", { "undead", "soul" }, { "summoning", "control" });    // Conjure Boneman
        c["spells"]["A.esp|0x000806"] = Entry("conjuration", "novice", { "daedra", "weapon" }, { "summoning", "transform" });
        c["spells"]["A.esp|0x000807"] = Entry("restoration", "apprentice", { "sun", "undead" }, { "fear" });               // Turn Undead: not an undead-rule spell
        c["spells"]["A.esp|0x000808"] = Entry("restoration", "novice", { "health" }, { "curing" });
        return c;   // 0x809 is in the tree, not in the catalog
    }
}
int RunBuildSelfTest()
{
    std::printf("pool\n");
    const Rules rules = DefaultRules();
    PoolStats stats;
    auto pool = BuildPool(SmallTree(), SmallCatalog(), rules, [](const std::string& id) { return id == "A.esp|0x000808"; }, stats);
    Check(stats.treeNodes == 9, "nine tree nodes seen");
    Check(stats.notInCatalog == 1, "a node missing from the catalog is counted");
    Check(stats.excludedTag == 1 && stats.excludedDistr == 1, "cloak technique and a _DISTR.ini spell are excluded");
    Check(pool.size() == 6, "six spells kept");
    auto find = [&](const char* id) { return std::find_if(pool.begin(), pool.end(), [&](const SpellFacts& s) { return s.persistentId == id; }); };
    Check(find("A.esp|0x000804")->undeadRule && find("A.esp|0x000805")->undeadRule, "Raise Zombie and Conjure Boneman are undead-rule spells");
    Check(!find("A.esp|0x000807")->undeadRule && !find("A.esp|0x000806")->undeadRule, "Turn Undead and a bound weapon are not");
    Check(find("A.esp|0x000803")->tier == Tier::Master && find("A.esp|0x000803")->school == School::Destruction, "school and tier come from the catalog");
    ApplyEngineSchool(pool, [](const std::string& id) -> std::optional<School> {
        if (id == "A.esp|0x000801") return School::Restoration;   // planted mismatch
        if (id == "A.esp|0x000806") return std::nullopt;          // plugin gone
        if (id == "A.esp|0x000804" || id == "A.esp|0x000805") return School::Conjuration;   // every other spell keeps its catalog school
        if (id == "A.esp|0x000807" || id == "A.esp|0x000808") return School::Restoration;
        return School::Destruction; }, stats);
    Check(stats.schoolMismatch == 1 && find("A.esp|0x000801")->school == School::Restoration, "the engine school replaces the catalog school and is counted");
    Check(stats.unresolved == 1 && find("A.esp|0x000806") == pool.end(), "an unresolvable spell is dropped and counted");
    json oldCatalog = SmallCatalog(); oldCatalog["version"] = 2; PoolStats s2;
    Check(BuildPool(SmallTree(), oldCatalog, rules, nullptr, s2).empty() && s2.treeNodes == 0, "another catalog version stops the pool");
    PoolStats s3;
    Check(BuildPool(json{ { "nodes", json::array() } }, SmallCatalog(), rules, nullptr, s3).empty(), "a tree without schools stops the pool");
    Rules rejecting = rules; rejecting.excludeSpells = { "A.esp|0x000803" }; PoolStats s4;
    BuildPool(SmallTree(), SmallCatalog(), rejecting, nullptr, s4);
    Check(s4.excludedList == 1, "excludeSpells drops a spell by persistentId");
    return NpcSpellsCheck::g_failed;   // Task 5 continues this function
}
```
- [ ] Build (SkyrimSE check), run - expected: `[FAIL]` lines / link errors.
- [ ] Implement `NpcSpellsPool.cpp`:
```cpp
#include "core/NpcSpellsPool.h"
#include <algorithm>
namespace NpcSpells {
bool SpellFacts::HasAnyTag(const std::vector<std::string>& tags) const
{
    for (const auto& t : tags) if (std::find(elements.begin(), elements.end(), t) != elements.end() || std::find(techniques.begin(), techniques.end(), t) != techniques.end()) return true;
    return false;
}
std::vector<SpellFacts> BuildPool(const nlohmann::json& tree, const nlohmann::json& catalog, const Rules& rules, const DistributedFn& distributed, PoolStats& stats)
{
    std::vector<SpellFacts> pool;
    if (!catalog.is_object() || catalog.value("version", 0) != kCatalogVersion || !catalog.contains("spells")) return pool;
    const auto schools = tree.find("schools");
    if (schools == tree.end() || !schools->is_object()) return pool;
    const auto& spells = catalog["spells"];
    for (const auto& [schoolName, schoolData] : schools->items()) {
        const auto nodes = schoolData.find("nodes");
        if (nodes == schoolData.end() || !nodes->is_array()) continue;
        for (const auto& node : *nodes) {
            ++stats.treeNodes;
            const std::string pid = node.value("persistentId", "");
            const auto entry = spells.find(pid);
            if (pid.empty() || entry == spells.end()) { ++stats.notInCatalog; continue; }
            SpellFacts f; f.persistentId = pid;
            const auto school = SchoolFromName(entry->value("school", "")); const auto tier = TierFromName(entry->value("tier", ""));
            if (!school || !tier) { ++stats.noSchoolOrTier; continue; }
            f.school = *school; f.tier = *tier;
            f.elements = entry->value("elements", std::vector<std::string>{}); f.techniques = entry->value("techniques", std::vector<std::string>{});
            if (f.HasAnyTag(rules.excludeTags)) { ++stats.excludedTag; continue; }
            if (std::find(rules.excludeSpells.begin(), rules.excludeSpells.end(), pid) != rules.excludeSpells.end()) { ++stats.excludedList; continue; }
            if (distributed && distributed(pid)) { ++stats.excludedDistr; continue; }
            f.undeadRule = f.HasAnyTag({ "undead" }) && (f.HasAnyTag({ "summoning" }) || f.HasAnyTag({ "control" }));
            pool.push_back(std::move(f)); ++stats.kept;
        }
    }
    return pool;
}
void ApplyEngineSchool(std::vector<SpellFacts>& pool, const EngineSchoolFn& engineSchool, PoolStats& stats)
{
    std::vector<SpellFacts> kept; kept.reserve(pool.size());
    for (auto& f : pool) {
        const auto school = engineSchool(f.persistentId);
        if (!school) { ++stats.unresolved; continue; }
        if (*school != f.school) { ++stats.schoolMismatch; f.school = *school; }
        kept.push_back(std::move(f));
    }
    pool.swap(kept); stats.kept = pool.size();
}
}
```
  `HasAnyTag({"undead"})` with a braced list needs the parameter type `const std::vector<std::string>&`; it does.
- [ ] Build, run - expected: all PASS.
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsPool.h plugins/NPCSpells/src/core/NpcSpellsPool.cpp tools/CMakeLists.txt tools/npcspells-selftest-build.cpp; git commit -m "NPC Spells: spell pool from tree and catalog with exclusions" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 4: Slot table and FormID layout

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsSlots.h`, `plugins/NPCSpells/src/core/NpcSpellsSlots.cpp`
- Modify: `tools/CMakeLists.txt`, `tools/npcspells-selftest-esl.cpp` (start the suite)
- Test: `tools/npcspells-selftest-esl.cpp`

**Slot arithmetic (the table the ESL and the DLL share):**
- Groups (built-in order, index = group): 0 None, 1 Forsworn, 2 Vampire, 3 Necromancer, 4 Thalmor, 5 Vigilant. Undead policy per group: None, Forsworn = Mixed; Vampire, Necromancer = AlwaysNecro; Thalmor, Vigilant = NeverUndead.
- Variants: `All` (every allowed spell of the school), `Prefer` (allowed ∩ group prefer tags; for Conjuration in a Mixed group also minus undead-rule spells = the conjurer's 3/4), `NecroPrefer` (allowed ∩ (undead-rule ∪ prefer)), `NoUndeadAll` (allowed minus undead-rule).
- Variants used: non-Conjuration school x None = {All} (1); non-Conjuration x faction group = {All, Prefer} (2); Conjuration x None = {All, NecroPrefer, NoUndeadAll} (3; `Prefer` would equal `NoUndeadAll`); Conjuration x Forsworn = {All, Prefer, NecroPrefer, NoUndeadAll} (4); Conjuration x Vampire/Necromancer = {All, NecroPrefer} (2); Conjuration x Thalmor/Vigilant = {All, Prefer} (2; `never: undead` already removes undead-rule spells).
- Rungs: 5 per (school, group, variant) - the ladder R1..R5.
- Count: non-Conjuration 4 schools x (1 + 5 x 2) = 44 variant rows x 5 rungs = 220; Conjuration (3 + 4 + 2 + 2 + 2 + 2) = 15 x 5 = 75; non-mage lists 6 groups x 2 schools (Restoration `curing`, Alteration `armor`, novice only, 1 rung) = 12. **Slots = 307.** `kMaxChunks = 3` chunks per slot (255 x 3 = 765 spells per slot, ~2.5x the biggest TAKEALOOK school). **ESL records N = 307 x 3 = 921**, local FormIDs `0x800 .. 0xB98`, in-file `0x01000800 .. 0x01000B98`. Headroom to the ESL limit (0xFFF): 1127 records. Changing `kMaxChunks` or the enumeration order renumbers every FormID: new slots append only.
- EditorID: `HoMNS_<School>_<Group>_<Variant>_R<rung+1>_C<chunk>`, e.g. `HoMNS_Destruction_Thalmor_Prefer_R2_C0`; non-mage: `HoMNS_Restoration_Vampire_NonMage_C0`.

**Interfaces** (`NpcSpellsSlots.h` includes `core/NpcSpellsTypes.h`, `<array>`, `<cstdint>`, `<optional>`, `<string>`, `<string_view>`, `<vector>`; `kRungCount` comes from `NpcSpellsTypes.h`)
```cpp
namespace NpcSpells {
    enum class UndeadGroupPolicy : std::uint8_t { Mixed, AlwaysNecro, NeverUndead };
    struct GroupDef { std::string_view name; UndeadGroupPolicy policy; };
    inline constexpr std::array<GroupDef, 6> kGroups{ { { "None", UndeadGroupPolicy::Mixed }, { "Forsworn", UndeadGroupPolicy::Mixed }, { "Vampire", UndeadGroupPolicy::AlwaysNecro },
        { "Necromancer", UndeadGroupPolicy::AlwaysNecro }, { "Thalmor", UndeadGroupPolicy::NeverUndead }, { "Vigilant", UndeadGroupPolicy::NeverUndead } } };
    inline constexpr std::uint8_t kGroupNone = 0;
    enum class Variant : std::uint8_t { All, Prefer, NecroPrefer, NoUndeadAll };
    struct SlotKey { School school; std::uint8_t group; Variant variant; std::uint8_t rung; bool nonMage; bool operator==(const SlotKey&) const = default; };
    struct SlotDef { SlotKey key; std::string editorId; };
    inline constexpr std::uint32_t kFirstLocalFormId = 0x800;
    inline constexpr std::uint8_t kMaxChunks = 3;        // renumbers every FormID if changed
    const std::vector<SlotDef>& SlotTable();             // built once, append-only order
    std::optional<std::size_t> FindSlot(const SlotKey& key);
    constexpr std::uint32_t LocalFormId(std::size_t slotIndex, std::uint8_t chunk) { return kFirstLocalFormId + static_cast<std::uint32_t>(slotIndex) * kMaxChunks + chunk; }
    std::vector<Variant> VariantsFor(School school, std::uint8_t group);
    std::string EditorIdFor(const SlotKey& key, std::uint8_t chunk);
}
```

**Steps**
- [ ] Write the failing test (`tools/npcspells-selftest-esl.cpp`, start of `RunEslSelfTest`):
```cpp
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace NpcSpells; using NpcSpellsCheck::Check;
int RunEslSelfTest()
{
    std::printf("slots\n");
    const auto& table = SlotTable();
    Check(table.size() == 307, "307 slots (220 non-conjuration + 75 conjuration + 12 non-mage)");
    Check(LocalFormId(table.size() - 1, kMaxChunks - 1) == 0xB98, "last record is local 0xB98");
    Check(LocalFormId(0, 0) == 0x800, "first record is local 0x800");
    Check(VariantsFor(School::Destruction, kGroupNone).size() == 1 && VariantsFor(School::Conjuration, 1).size() == 4 && VariantsFor(School::Conjuration, 4).size() == 2, "variant sets per school and group");
    const auto thalmorPrefer = FindSlot({ School::Destruction, 4, Variant::Prefer, 1, false });
    Check(thalmorPrefer && table[*thalmorPrefer].editorId == "HoMNS_Destruction_Thalmor_Prefer_R2", "EditorID names school, group, variant and rung");
    Check(EditorIdFor(table[*thalmorPrefer].key, 2) == "HoMNS_Destruction_Thalmor_Prefer_R2_C2", "chunk suffix");
    Check(!FindSlot({ School::Destruction, kGroupNone, Variant::Prefer, 0, false }), "no Prefer slot for the None group");
    Check(FindSlot({ School::Restoration, 2, Variant::All, 0, true }).has_value(), "non-mage Restoration slot exists for Vampire");
    std::vector<std::string> ids; for (const auto& s : table) ids.push_back(s.editorId);
    std::sort(ids.begin(), ids.end());
    Check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "EditorIDs are unique");
    return NpcSpellsCheck::g_failed;   // Task 6 continues
}
```
- [ ] Build (SkyrimSE check), run - expected failures.
- [ ] Implement `NpcSpellsSlots.cpp`:
```cpp
#include "core/NpcSpellsSlots.h"
#include <cctype>
#include <format>
namespace NpcSpells {
namespace {
    std::string Cap(std::string_view lower) { std::string s(lower); if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0]))); return s; }
    constexpr std::array<std::string_view, 4> kVariantNames{ "All", "Prefer", "NecroPrefer", "NoUndeadAll" };
    constexpr std::array<School, 2> kNonMageSchools{ School::Restoration, School::Alteration };
}
std::vector<Variant> VariantsFor(School school, std::uint8_t group)
{
    const bool none = group == kGroupNone;
    if (school != School::Conjuration) return none ? std::vector<Variant>{ Variant::All } : std::vector<Variant>{ Variant::All, Variant::Prefer };
    switch (kGroups[group].policy) {
    case UndeadGroupPolicy::Mixed: return none ? std::vector<Variant>{ Variant::All, Variant::NecroPrefer, Variant::NoUndeadAll }
                                               : std::vector<Variant>{ Variant::All, Variant::Prefer, Variant::NecroPrefer, Variant::NoUndeadAll };
    case UndeadGroupPolicy::AlwaysNecro: return { Variant::All, Variant::NecroPrefer };
    case UndeadGroupPolicy::NeverUndead: return { Variant::All, Variant::Prefer };
    }
    return { Variant::All };
}
std::string EditorIdFor(const SlotKey& k, std::uint8_t chunk)
{
    const std::string base = k.nonMage ? std::format("HoMNS_{}_{}_NonMage", Cap(kSchoolNames[static_cast<std::size_t>(k.school)]), kGroups[k.group].name)
        : std::format("HoMNS_{}_{}_{}_R{}", Cap(kSchoolNames[static_cast<std::size_t>(k.school)]), kGroups[k.group].name, kVariantNames[static_cast<std::size_t>(k.variant)], k.rung + 1);
    return std::format("{}_C{}", base, chunk);
}
const std::vector<SlotDef>& SlotTable()
{
    static const std::vector<SlotDef> table = [] {
        std::vector<SlotDef> t;
        for (std::size_t s = 0; s < kSchoolCount; ++s)
            for (std::uint8_t g = 0; g < kGroups.size(); ++g)
                for (Variant v : VariantsFor(static_cast<School>(s), g))
                    for (std::uint8_t r = 0; r < kRungCount; ++r) {
                        SlotKey k{ static_cast<School>(s), g, v, r, false };
                        std::string id = EditorIdFor(k, 0); id.resize(id.size() - 3);   // strip "_C0"
                        t.push_back({ k, id });
                    }
        for (std::uint8_t g = 0; g < kGroups.size(); ++g)
            for (School s : kNonMageSchools) { SlotKey k{ s, g, Variant::All, 0, true }; std::string id = EditorIdFor(k, 0); id.resize(id.size() - 3); t.push_back({ k, id }); }
        return t;
    }();
    return table;
}
std::optional<std::size_t> FindSlot(const SlotKey& key)
{
    const auto& t = SlotTable();
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i].key == key) return i;
    return std::nullopt;
}
}
```
- [ ] Build, run - expected PASS (if 307 does not come out, the arithmetic above is wrong and must be fixed in the header comment too).
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsSlots.h plugins/NPCSpells/src/core/NpcSpellsSlots.cpp tools/CMakeLists.txt tools/npcspells-selftest-esl.cpp; git commit -m "NPC Spells: built-in slot table and FormID layout" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 5: List builder (membership, sort, chunks, flags)

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsBuild.h`, `plugins/NPCSpells/src/core/NpcSpellsBuild.cpp`
- Modify: `tools/CMakeLists.txt`, `tools/npcspells-selftest-build.cpp` (second half), `tools/npcspells-test.cpp` (replace the `RunRealFiles` stub), `CLAUDE.md` (harness row: `--tree --catalog`)
- Test: `tools/npcspells-selftest-build.cpp`

**Interfaces** (`NpcSpellsBuild.h` includes `core/NpcSpellsPool.h`, `core/NpcSpellsSlots.h`, `<array>`, `<cstdint>`, `<vector>`)
```cpp
namespace NpcSpells {
    struct Entry { std::uint32_t spellIndex; std::uint16_t level; };        // index into the pool
    struct BuiltList { std::size_t slotIndex; std::uint8_t chunk; std::uint8_t flags; std::vector<Entry> entries; };  // flags: 0 or 1 (kCalculateFromAllLevelsLTOrEqPCLevel)
    struct BuildStats { std::size_t lists = 0, emptySlots = 0, droppedOverCapacity = 0; std::array<std::size_t, kSchoolCount> perSchool{}; };
    // Every (slot, chunk) with at least one entry. Entries sorted ascending by level, one school per list. A slot is split only when it
    // holds more than kMaxEntries spells (ceil(total / 255) chunks, at most kMaxChunks, fed round-robin across tiers); otherwise one list, chunk 0.
    std::vector<BuiltList> BuildLists(const std::vector<SpellFacts>& pool, const Rules& rules, BuildStats& stats);
    // Membership of one spell in one slot (public for the test): allowed = not in group never; variant rules of Task 4.
    bool InSlot(const SpellFacts& spell, const SlotKey& key, const Rules& rules);
}
```
Rung semantics: rung r = `rules.ladder[r]`; a list for rung r holds spells with `tier >= startTier`; `flags = below ? 1 : 0`; entry level = `kTierFloor[tier]`. Non-mage slots hold novice spells with any `nonMageTags` tag, flags 0.

**Steps**
- [ ] Add `#include "core/NpcSpellsBuild.h"`, `#include "core/NpcSpellsSlots.h"` and `#include <format>` to the include block of `tools/npcspells-selftest-build.cpp` (`<algorithm>` is there from Task 3), then append to `RunBuildSelfTest()` (replacing its `return`):
```cpp
    std::printf("build\n");
    // Group rules for the test: group index 1 = Forsworn (prefer fire..., never sun), 4 = Thalmor (never undead)
    auto pool2 = BuildPool(SmallTree(), SmallCatalog(), rules, nullptr, stats);
    auto idx = [&](const char* id) { return static_cast<std::uint32_t>(std::find_if(pool2.begin(), pool2.end(), [&](const SpellFacts& s) { return s.persistentId == id; }) - pool2.begin()); };
    Check(InSlot(pool2[idx("A.esp|0x000801")], { School::Destruction, 1, Variant::Prefer, 0, false }, rules), "a fire spell is in the Forsworn Prefer list");
    Check(!InSlot(pool2[idx("A.esp|0x000803")], { School::Destruction, 1, Variant::Prefer, 0, false }, rules), "a frost spell is not");
    Check(!InSlot(pool2[idx("A.esp|0x000807")], { School::Restoration, 1, Variant::All, 0, false }, rules), "a sun spell is kept out of every Forsworn list (never)");
    Check(!InSlot(pool2[idx("A.esp|0x000805")], { School::Conjuration, 4, Variant::All, 0, false }, rules), "Thalmor never undead removes Conjure Boneman");
    Check(InSlot(pool2[idx("A.esp|0x000805")], { School::Conjuration, 0, Variant::NecroPrefer, 0, false }, rules) && !InSlot(pool2[idx("A.esp|0x000806")], { School::Conjuration, 0, Variant::NecroPrefer, 0, false }, rules), "NecroPrefer holds undead-rule spells only");
    Check(InSlot(pool2[idx("A.esp|0x000806")], { School::Conjuration, 0, Variant::NoUndeadAll, 0, false }, rules) && !InSlot(pool2[idx("A.esp|0x000804")], { School::Conjuration, 0, Variant::NoUndeadAll, 0, false }, rules), "NoUndeadAll excludes undead-rule spells");
    Check(!InSlot(pool2[idx("A.esp|0x000801")], { School::Destruction, 0, Variant::All, 4, false }, rules) && InSlot(pool2[idx("A.esp|0x000803")], { School::Destruction, 0, Variant::All, 4, false }, rules), "rung 5 (master) holds master spells only");
    Check(InSlot(pool2[idx("A.esp|0x000808")], { School::Restoration, 0, Variant::All, 0, true }, rules) && !InSlot(pool2[idx("A.esp|0x000807")], { School::Restoration, 0, Variant::All, 0, true }, rules), "non-mage list: novice curing only");

    BuildStats bs; auto lists = BuildLists(pool2, rules, bs);
    bool invariants = true;
    for (const auto& l : lists) {
        invariants = invariants && !l.entries.empty() && l.entries.size() <= kMaxEntries && (l.flags == 0 || l.flags == 1);
        const School school = pool2[l.entries.front().spellIndex].school;
        for (std::size_t i = 0; i < l.entries.size(); ++i) {
            const auto& e = l.entries[i];
            invariants = invariants && pool2[e.spellIndex].school == school && e.level == kTierFloor[static_cast<std::size_t>(pool2[e.spellIndex].tier)] && (i == 0 || l.entries[i - 1].level <= e.level);
            const auto& rung = rules.ladder[SlotTable()[l.slotIndex].key.rung];
            invariants = invariants && (SlotTable()[l.slotIndex].key.nonMage || pool2[e.spellIndex].tier >= rung.startTier);
            invariants = invariants && (l.flags == (SlotTable()[l.slotIndex].key.nonMage ? 0 : (rung.below ? 1 : 0)));
        }
    }
    Check(invariants, "every list: one school, ascending levels, level = tier floor, <= 255, tier >= rung start, flag per rung");
    Check(bs.emptySlots > 0, "slots without spells are counted (small pool)");
    { const auto small = *FindSlot({ School::Destruction, kGroupNone, Variant::All, 0, false }); std::size_t n = 0; bool chunk0 = true;
      for (const auto& l : lists) if (l.slotIndex == small) { ++n; chunk0 = chunk0 && l.chunk == 0; }
      Check(n == 1 && chunk0, "a slot under 255 spells builds one list (chunk 0), never split"); }

    // 700 novice..master spells in one school -> 3 chunks, every chunk holds every tier
    std::vector<SpellFacts> big;
    for (int i = 0; i < 700; ++i) { SpellFacts f; f.persistentId = std::format("B.esp|0x{:06X}", 0x800 + i); f.school = School::Illusion; f.tier = static_cast<Tier>(i % 5); big.push_back(f); }
    BuildStats bigStats; auto bigLists = BuildLists(big, rules, bigStats);
    const auto slot = *FindSlot({ School::Illusion, kGroupNone, Variant::All, 0, false });
    std::size_t chunks = 0; bool coverage = true, cap = true;
    for (const auto& l : bigLists) if (l.slotIndex == slot) {
        ++chunks; cap = cap && l.entries.size() <= kMaxEntries;
        std::array<bool, kTierCount> seen{}; for (const auto& e : l.entries) seen[static_cast<std::size_t>(big[e.spellIndex].tier)] = true;
        for (bool b : seen) coverage = coverage && b;
    }
    Check(chunks == 3 && cap, "700 spells split into 3 chunks of <= 255");
    Check(coverage, "every chunk holds every tier (round-robin per tier)");
    Check(bigStats.droppedOverCapacity == 0, "nothing dropped at 700");
    for (int i = 700; i < 800; ++i) { SpellFacts f; f.persistentId = std::format("B.esp|0x{:06X}", 0x800 + i); f.school = School::Illusion; f.tier = Tier::Novice; big.push_back(f); }
    BuildStats overStats; BuildLists(big, rules, overStats);
    Check(overStats.droppedOverCapacity > 0, "800 spells: the surplus over 3 x 255 is dropped and counted");
    return NpcSpellsCheck::g_failed;
```
- [ ] Build (SkyrimSE check), run - expected failures.
- [ ] Implement `NpcSpellsBuild.cpp`:
```cpp
#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
namespace NpcSpells {
namespace {
    const GroupRule* GroupFor(const Rules& rules, std::uint8_t group) {
        if (group == kGroupNone) return nullptr;
        for (const auto& g : rules.groups) if (g.name == kGroups[group].name) return &g;
        return nullptr;
    }
}
bool InSlot(const SpellFacts& s, const SlotKey& k, const Rules& rules)
{
    if (s.school != k.school) return false;
    const GroupRule* g = GroupFor(rules, k.group);
    if (g && s.HasAnyTag(g->never)) return false;
    if (k.nonMage) return s.tier == Tier::Novice && s.HasAnyTag(rules.nonMageTags);
    if (s.tier < rules.ladder[k.rung].startTier) return false;
    const bool prefer = g && s.HasAnyTag(g->prefer);
    switch (k.variant) {
    case Variant::All: return true;
    case Variant::Prefer: return prefer && !(k.school == School::Conjuration && kGroups[k.group].policy == UndeadGroupPolicy::Mixed && s.undeadRule);
    case Variant::NecroPrefer: return s.undeadRule || prefer;
    case Variant::NoUndeadAll: return !s.undeadRule;
    }
    return false;
}
std::vector<BuiltList> BuildLists(const std::vector<SpellFacts>& pool, const Rules& rules, BuildStats& stats)
{
    std::vector<BuiltList> out;
    const auto& table = SlotTable();
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        const SlotKey& key = table[slot].key;
        std::array<std::vector<std::uint32_t>, kTierCount> byTier;
        for (std::uint32_t i = 0; i < pool.size(); ++i) if (InSlot(pool[i], key, rules)) byTier[static_cast<std::size_t>(pool[i].tier)].push_back(i);
        std::size_t total = 0; for (const auto& v : byTier) total += v.size();
        if (total == 0) { ++stats.emptySlots; continue; }
        // Split only over 255 (§5.4): a small slot stays one list, so a flag-0 list never misses a tier it holds
        const std::size_t chunkCount = std::min<std::size_t>(kMaxChunks, (total + kMaxEntries - 1) / kMaxEntries);
        std::array<std::vector<Entry>, kMaxChunks> chunks;
        std::size_t next = 0;
        for (std::size_t t = 0; t < kTierCount; ++t)
            for (std::uint32_t spell : byTier[t]) {
                auto& target = chunks[next % chunkCount]; ++next;
                if (target.size() >= kMaxEntries) { ++stats.droppedOverCapacity; continue; }
                target.push_back({ spell, kTierFloor[t] });
            }
        const std::uint8_t flags = (!key.nonMage && rules.ladder[key.rung].below) ? 1 : 0;
        for (std::uint8_t c = 0; c < kMaxChunks; ++c) {
            if (chunks[c].empty()) continue;
            std::stable_sort(chunks[c].begin(), chunks[c].end(), [](const Entry& a, const Entry& b) { return a.level < b.level; });
            out.push_back({ slot, c, flags, std::move(chunks[c]) });
            ++stats.lists; ++stats.perSchool[static_cast<std::size_t>(key.school)];
        }
    }
    return out;
}
}
```
  Chunks: `chunkCount = ceil(total / 255)` capped at `kMaxChunks`, so a slot of <= 255 spells is one list. Over 255, round-robin across tiers: `next` advances across tiers so each chunk receives every tier when a tier has >= `chunkCount` spells; when a chunk is full the spell is dropped and counted (the chunk index keeps advancing, so a later chunk still fills).
- [ ] Replace the `RunRealFiles` stub in `tools/npcspells-test.cpp` (add `#include "JsonFile.h"`, `core/NpcSpellsPool.h`, `core/NpcSpellsBuild.h`, `<algorithm>`): read both files with `ReadJsonFile` (`tools/include/JsonFile.h`), `BuildPool` with a null `distributed`, `BuildLists`, then print per school `lists`, biggest list size, `emptySlots`, `droppedOverCapacity`, and each slot with `entries >= kBigListReport` (`constexpr std::size_t kBigListReport = 200;` next to the function). Return 0. Run on the TAKEALOOK files (the main plugin writes them to MO2's overwrite, checked 2026-10-08): `.\build\tools\Release\npcspells-test.exe --tree "D:\TAKEALOOK\overwrite\SKSE\Plugins\SpellLearning\spell_tree.json" --catalog "D:\TAKEALOOK\overwrite\SKSE\Plugins\SpellLearning\spell_catalog.json"` - expected: `droppedOverCapacity 0`, no school over 765.
- [ ] `CLAUDE.md`: extend the `npcspells-test` harness row with `--tree <spell_tree.json> --catalog <spell_catalog.json>` (slot statistics on real files).
- [ ] Build, run harness - expected all PASS.
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsBuild.h plugins/NPCSpells/src/core/NpcSpellsBuild.cpp tools/CMakeLists.txt tools/npcspells-selftest-build.cpp tools/npcspells-test.cpp CLAUDE.md; git commit -m "NPC Spells: build leveled lists per slot with chunking" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 6: ESL writer tool and read-back test

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsEsl.h`, `plugins/NPCSpells/src/core/NpcSpellsEsl.cpp`, `tools/npcspells-esl.cpp`
- Modify: `tools/CMakeLists.txt`, `tools/npcspells-selftest-esl.cpp`, `CLAUDE.md` (harness row)
- Test: `tools/npcspells-selftest-esl.cpp`

**Interfaces** (`NpcSpellsEsl.h` includes `core/NpcSpellsSlots.h`, `<cstdint>`, `<string_view>`, `<vector>`)
```cpp
namespace NpcSpells {
    inline constexpr std::string_view kEslFileName = "HeartOfMagic_NPCSpells.esp";
    inline constexpr std::string_view kEslMaster = "Skyrim.esm";
    inline constexpr std::uint32_t kTes4LightFlag = 0x00000200;
    inline constexpr float kHeaderVersion = 1.70f;          // keeps 0x800..0xFFF valid on 1.5.97
    inline constexpr std::uint16_t kFormVersion = 44;
    inline constexpr std::uint32_t kOwnModIndexInFile = 0x01000000;   // one master -> own records are index 1 in the file
    inline constexpr std::uint32_t kTopGroupCount = 1;                // HEDR numRecords counts records and groups (one GRUP LVSP)
    // TES4 (HEDR, CNAM "HeartOfMagic", MAST, DATA) + GRUP LVSP with one empty record per (slot, chunk):
    // EDID, OBND (12 zero bytes), LVLD 0, LVLF 0, LLCT 0. Uncompressed. The DLL fills entries and LVLF (llFlags) at runtime.
    std::vector<unsigned char> BuildEslBytes(const std::vector<SlotDef>& slots);
}
```
Note: the `Plugin()` helper in `tools/tome-reach-selftest.cpp` writes only MAST; it is not loadable by the game and must not be copied verbatim.

**Steps**
- [ ] Add `#include "core/NpcSpellsEsl.h"` and `#include <cstring>` to the include block of `tools/npcspells-selftest-esl.cpp` (`<algorithm>`, `<string>` are there from Task 4), then append to `RunEslSelfTest()` (replacing its `return`) a minimal reader and checks:
```cpp
    std::printf("esl\n");
    const auto bytes = BuildEslBytes(table);
    auto u32 = [&](std::size_t at) { return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24; };
    auto u16 = [&](std::size_t at) { return std::uint16_t(bytes[at] | bytes[at + 1] << 8); };
    auto tag = [&](std::size_t at, const char* t) { return std::memcmp(&bytes[at], t, 4) == 0; };
    Check(tag(0, "TES4") && (u32(8) & 0x200) != 0, "TES4 header carries the ESL flag");
    const std::size_t tes4Size = u32(4); std::size_t p = 24; bool hedr = false, mast = false, data = false, cnam = false;
    while (p < 24 + tes4Size) { const std::uint16_t n = u16(p + 4);
        if (tag(p, "HEDR")) { hedr = n == 12 && u32(p + 10) == table.size() * kMaxChunks + kTopGroupCount && u32(p + 14) == kFirstLocalFormId + table.size() * kMaxChunks; }
        if (tag(p, "CNAM")) cnam = true; if (tag(p, "MAST")) mast = std::string(reinterpret_cast<const char*>(&bytes[p + 6])) == "Skyrim.esm"; if (tag(p, "DATA")) data = n == 8;
        p += 6 + n; }
    Check(hedr && cnam && mast && data, "HEDR (records + groups = 922, next object id), CNAM, MAST Skyrim.esm, DATA");
    Check(tag(p, "GRUP") && tag(p + 8, "LVSP"), "one top group LVSP");
    std::size_t rec = p + 24; std::size_t count = 0; bool ids = true, bodies = true;
    while (rec < bytes.size()) {
        const std::uint32_t size = u32(rec + 4), formId = u32(rec + 12);
        ids = ids && tag(rec, "LVSP") && formId == (kOwnModIndexInFile | LocalFormId(count / kMaxChunks, static_cast<std::uint8_t>(count % kMaxChunks))) && u16(rec + 20) == kFormVersion && u32(rec + 8) == 0;
        std::size_t q = rec + 24; bool edid = false, obnd = false, lvld = false, lvlf = false, llct = false;
        while (q < rec + 24 + size) { const std::uint16_t n = u16(q + 4);
            if (tag(q, "EDID")) edid = std::string(reinterpret_cast<const char*>(&bytes[q + 6])) == EditorIdFor(table[count / kMaxChunks].key, static_cast<std::uint8_t>(count % kMaxChunks));
            if (tag(q, "OBND")) obnd = n == 12; if (tag(q, "LVLD")) lvld = n == 1 && bytes[q + 6] == 0; if (tag(q, "LVLF")) lvlf = n == 1; if (tag(q, "LLCT")) llct = n == 1 && bytes[q + 6] == 0;
            q += 6 + n; }
        bodies = bodies && edid && obnd && lvld && lvlf && llct;
        rec += 24 + size; ++count;
    }
    Check(count == table.size() * kMaxChunks && count == 921, "921 LVSP records (slots x kMaxChunks)");
    Check(ids, "in-file FormIDs are 0x01000800.. in table order, form version 44, uncompressed");
    Check(bodies, "each record: EDID, OBND(12), LVLD 0, LVLF, LLCT 0");
    return NpcSpellsCheck::g_failed;
```
- [ ] Build (SkyrimSE check), run - expected failures.
- [ ] Implement `NpcSpellsEsl.cpp`:
```cpp
#include "core/NpcSpellsEsl.h"
#include <cstring>
namespace NpcSpells {
namespace {
    using Bytes = std::vector<unsigned char>;
    constexpr std::uint32_t kGroupHeaderSize = 24;
    constexpr std::size_t kObndSize = 12;       // OBND: six int16 bounds, all zero for a leveled list
    constexpr std::size_t kTes4DataSize = 8;    // TES4 DATA after each MAST: uint64 master size, 0
    void Put16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<unsigned char>(v)); b.push_back(static_cast<unsigned char>(v >> 8)); }
    void Put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i))); }
    void PutTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
    void Join(Bytes& into, const Bytes& more) { into.insert(into.end(), more.begin(), more.end()); }
    Bytes Sub(const char* t, const Bytes& data) { Bytes b; PutTag(b, t); Put16(b, static_cast<std::uint16_t>(data.size())); Join(b, data); return b; }
    Bytes Str(std::string_view s) { Bytes b(s.begin(), s.end()); b.push_back(0); return b; }
    Bytes Record(const char* type, std::uint32_t flags, std::uint32_t formId, const Bytes& body)
    { Bytes b; PutTag(b, type); Put32(b, static_cast<std::uint32_t>(body.size())); Put32(b, flags); Put32(b, formId); Put32(b, 0); Put16(b, kFormVersion); Put16(b, 0); Join(b, body); return b; }
}
std::vector<unsigned char> BuildEslBytes(const std::vector<SlotDef>& slots)
{
    const std::uint32_t records = static_cast<std::uint32_t>(slots.size()) * kMaxChunks;
    // numRecords = records + groups (TES4 convention; xEdit flags a mismatch, the game does not read it)
    Bytes hedr; { float v = kHeaderVersion; unsigned char raw[sizeof v]; std::memcpy(raw, &v, sizeof v); hedr.insert(hedr.end(), raw, raw + sizeof v); Put32(hedr, records + kTopGroupCount); Put32(hedr, kFirstLocalFormId + records); }
    Bytes header; Join(header, Sub("HEDR", hedr)); Join(header, Sub("CNAM", Str("HeartOfMagic"))); Join(header, Sub("MAST", Str(kEslMaster))); { Bytes zero(kTes4DataSize, 0); Join(header, Sub("DATA", zero)); }
    Bytes out = Record("TES4", kTes4LightFlag, 0, header);
    Bytes group;
    for (std::size_t i = 0; i < slots.size(); ++i)
        for (std::uint8_t c = 0; c < kMaxChunks; ++c) {
            Bytes body; Join(body, Sub("EDID", Str(EditorIdFor(slots[i].key, c))));
            { Bytes obnd(kObndSize, 0); Join(body, Sub("OBND", obnd)); }
            { Bytes one(1, 0); Join(body, Sub("LVLD", one)); Join(body, Sub("LVLF", one)); Join(body, Sub("LLCT", one)); }
            Join(group, Record("LVSP", 0, kOwnModIndexInFile | LocalFormId(i, c), body));
        }
    PutTag(out, "GRUP"); Put32(out, kGroupHeaderSize + static_cast<std::uint32_t>(group.size())); PutTag(out, "LVSP"); Put32(out, 0); Put32(out, 0); Put32(out, 0); Join(out, group);
    return out;
}
}
```
- [ ] Write `tools/npcspells-esl.cpp`: `main(argc, argv)` takes `-o <path>`, writes `BuildEslBytes(SlotTable())` with `std::ofstream(path, std::ios::binary)`, prints `wrote <path>: <N> LVSP records, local 0x800..0x<last>`, exits 1 on an unwritable path.
- [ ] `tools/CMakeLists.txt`: add `npcspells-esl` executable (sources `npcspells-esl.cpp`, `${NPCSPELLS_CORE_DIR}/core/NpcSpellsSlots.cpp`, `${NPCSPELLS_CORE_DIR}/core/NpcSpellsEsl.cpp`; includes as the test) and the generation step:
```cmake
set(NPCSPELLS_ESL_DIR "${CMAKE_BINARY_DIR}/npcspells")
set(NPCSPELLS_ESL_FILE "${NPCSPELLS_ESL_DIR}/HeartOfMagic_NPCSpells.esp" CACHE INTERNAL "generated ESL")
add_custom_command(OUTPUT "${NPCSPELLS_ESL_FILE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NPCSPELLS_ESL_DIR}"
    COMMAND npcspells-esl -o "${NPCSPELLS_ESL_FILE}"
    DEPENDS npcspells-esl
    COMMENT "Writing HeartOfMagic_NPCSpells.esp from the slot table"
    VERBATIM)
add_custom_target(npcspells_esl ALL DEPENDS "${NPCSPELLS_ESL_FILE}")
```
- [ ] Build, run harness - expected all PASS; `build/npcspells/HeartOfMagic_NPCSpells.esp` exists (about 100 KB: 921 records x ~110 bytes). Optional sanity: open it in xEdit (SSEEdit) - 921 LVSP, ESL flag, no errors.
- [ ] `CLAUDE.md`, harness list: add `npcspells-esl` (writes `HeartOfMagic_NPCSpells.esp` from the slot table at build time into `build/npcspells/`; `npcspells-esl -o <path>`).
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsEsl.h plugins/NPCSpells/src/core/NpcSpellsEsl.cpp tools/npcspells-esl.cpp tools/CMakeLists.txt tools/npcspells-selftest-esl.cpp CLAUDE.md; git commit -m "NPC Spells: ESL writer tool with empty LVSP records" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 7: NPC facts, template chain, decisions

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsTemplate.h`, `plugins/NPCSpells/src/core/NpcSpellsTemplate.cpp`, `plugins/NPCSpells/src/core/NpcSpellsNpc.h`, `plugins/NPCSpells/src/core/NpcSpellsNpc.cpp`
- Modify: `tools/CMakeLists.txt`, `tools/npcspells-selftest-npc.cpp`
- Test: `tools/npcspells-selftest-npc.cpp`

**Interfaces** (`NpcSpellsTemplate.h` includes `<cstdint>`, `<optional>`, `<vector>`; `NpcSpellsNpc.h` includes `core/NpcSpellsClassTier.h`, `core/NpcSpellsSlots.h`, `<array>`, `<string>`, `<string_view>`, `<vector>`)
```cpp
namespace NpcSpells {
    // Template chain: link 0 is the NPC itself; link i+1 is link i's baseTemplateForm. Flags are TEMPLATE_USE_FLAG bits (RE/T/TESActorBaseData.h:46-62).
    struct ChainLink { std::uint16_t templateFlags; bool isLeveledCharacter; };
    enum class Category : std::uint16_t { Traits = 1 << 0, Stats = 1 << 1, Factions = 1 << 2, Spells = 1 << 3, Keywords = 1 << 12 };
    // Index of the link that provides the category, or nullopt when a leveled character is met first (unknowable until spawn).
    std::optional<std::size_t> ProviderIndex(const std::vector<ChainLink>& chain, Category category);

    struct NpcFacts {
        std::string pluginLower; std::uint32_t localId = 0;   // dice key
        bool inheritsSpellList = false;   // own kSpells flag
        bool unresolvableTraits = false, unresolvableStats = false, unresolvableFactions = false;
        bool isPlayer = false, isBeast = false, isCreature = false, isRedguard = false, isUndeadException = false, isNecromancerClass = false;
        bool hasClass = false; ClassWeights weights;
        std::vector<std::uint8_t> groupsMatched;   // built-in group indices, any order
    };
    struct ListIndex { std::vector<std::array<bool, kMaxChunks>> present; };   // [slot][chunk] exists and is non-empty
    struct Attachment { std::size_t slotIndex; std::uint8_t chunk; };
    struct NpcDecision { bool eligible = false; const char* skipReason = nullptr; ClassTier tier = ClassTier::NonMage; std::uint8_t group = kGroupNone; UndeadPolicy undead = UndeadPolicy::NoUndead; std::vector<Attachment> lists; std::size_t slotsWithoutList = 0; };
    double Dice(std::string_view pluginLower, std::uint32_t localId, std::string_view salt);   // FNV-1a 64 -> [0,1)
    NpcDecision DecideNpc(const NpcFacts& npc, const Rules& rules, const ListIndex& lists);
    // One report row per NPC that got lists (Task 9 fills it, Task 10 renders it): persistentId "Plugin.esp|0x00XXXX" (catalog form), display name
    struct NpcLine { std::string persistentId; std::string name; NpcDecision decision; };
}
```
Decision rules (§5.1-5.2 + §5.4): skip if `isPlayer`, `inheritsSpellList` ("template spell list"), `isBeast`; tier = `ClassifyClass` (no class -> NonMage); creature and tier != Mage -> skip ("creature not a mage"); group = lowest built-in index in `groupsMatched` that is > 0 (Forsworn < Vampire < Necromancer < Thalmor < Vigilant), else None; undead policy: group AlwaysNecro or `isNecromancerClass` -> Necromancer; else `isUndeadException` or (`IsConjurationClass` and not Redguard) -> Conjurer; else NoUndead; a NeverUndead group makes every policy behave as plain Prefer/All. Redguard and tier != Mage: a Hybrid in an AlwaysNecro group keeps only Conjuration (if weighted); every other non-mage Redguard - NonMage tier in any group, or Hybrid outside Vampire/Necromancer - is skipped ("redguard non-mage") and never reaches the 5% curing/armor branch (§5.1 Redguard rule). Schools: Mage all five, Hybrid `SchoolsWeighted`, NonMage none. For each school and rung r: attach if `Dice(key, "<school>:R<r>") < chance * (hybrid ? hybridScale : 1)`; variant: non-Conjuration None -> All; faction group -> `Dice(key, "<school>:R<r>:prefer") < preferShare ? Prefer : All`; Conjuration by policy (Necromancer in Mixed/AlwaysNecro -> NecroPrefer/All at `conjurationShare`; Conjurer in Mixed -> (Forsworn: Prefer, None: NoUndeadAll)/All; NoUndead in Mixed -> Forsworn: Prefer (non-undead) / NoUndeadAll, None: NoUndeadAll only; NeverUndead -> Prefer/All). Fallbacks: Prefer or NecroPrefer absent -> All; NoUndeadAll absent -> nothing for that slot (`slotsWithoutList++`). Chunk = first present among `chunk = (hash >> 8) % kMaxChunks` scanning forward. NonMage: `u = Dice(key, "nonmage")`: `u < nonMageChance / 2` -> Restoration non-mage slot of the group, `u < nonMageChance` -> Alteration; absent -> none.

**Steps**
- [ ] Write the failing test `tools/npcspells-selftest-npc.cpp`:
```cpp
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsTemplate.h"
#include "core/NpcSpellsSlots.h"
#include <algorithm>
#include <cmath>
#include <format>
using namespace NpcSpells; using NpcSpellsCheck::Check;
namespace {
    ListIndex AllPresent() { ListIndex li; li.present.assign(SlotTable().size(), { true, true, true }); return li; }
    ClassWeights Mage() { ClassWeights c; c.w[12] = 1; c.w[13] = 3; c.w[14] = 2; c.w[15] = 1; c.w[16] = 3; c.w[0] = 1; return c; }       // CombatMageConjurer
    ClassWeights Elemental() { ClassWeights c; c.w[12] = 2; c.w[13] = 1; c.w[14] = 4; c.w[15] = 1; c.w[16] = 2; c.w[0] = 1; return c; }  // destruction-led mage
    ClassWeights Warrior() { ClassWeights c; c.w[0] = 3; c.w[3] = 3; c.w[5] = 3; return c; }
    ClassWeights Spellsword() { ClassWeights c; c.w[0] = 3; c.w[5] = 3; c.w[12] = 1; c.w[14] = 2; c.w[16] = 2; return c; }               // .455 hybrid, no conjuration weight
    ClassWeights BattleConjurer() { ClassWeights c; c.w[0] = 3; c.w[5] = 3; c.w[13] = 2; c.w[14] = 1; return c; }                         // 3 of 9 = .333 hybrid, conjuration and destruction weighted
    NpcFacts Npc(std::uint32_t id, ClassWeights w) { NpcFacts n; n.pluginLower = "test.esp"; n.localId = id; n.hasClass = true; n.weights = w; return n; }
    std::size_t CountSchool(const NpcDecision& d, School s) { std::size_t n = 0; for (const auto& a : d.lists) if (SlotTable()[a.slotIndex].key.school == s && !SlotTable()[a.slotIndex].key.nonMage) ++n; return n; }
    bool AnyVariant(const NpcDecision& d, School s, Variant v) { for (const auto& a : d.lists) if (SlotTable()[a.slotIndex].key.school == s && SlotTable()[a.slotIndex].key.variant == v) return true; return false; }
}
int RunNpcSelfTest()
{
    std::printf("template\n");
    Check(ProviderIndex({ { 0, false } }, Category::Stats) == 0, "no flags: the record provides its own stats");
    Check(ProviderIndex({ { 0x2, false }, { 0, false } }, Category::Stats) == 1, "kStats: the template provides");
    Check(ProviderIndex({ { 0x2, false }, { 0x2, false }, { 0, false } }, Category::Stats) == 2, "chain of two templates");
    Check(!ProviderIndex({ { 0x2, false }, { 0, true } }, Category::Stats).has_value(), "a leveled character provider is unresolvable");
    Check(ProviderIndex({ { 0x2, false }, { 0, true } }, Category::Factions) == 0, "a category without the flag stays on the record even when the template is leveled");

    std::printf("npc\n");
    const Rules rules = DefaultRules(); const ListIndex all = AllPresent();
    NpcFacts inherits = Npc(1, Mage()); inherits.inheritsSpellList = true;
    Check(!DecideNpc(inherits, rules, all).eligible, "template spell list -> skipped");
    NpcFacts beast = Npc(2, Mage()); beast.isBeast = true;
    Check(!DecideNpc(beast, rules, all).eligible, "beast race -> skipped");
    NpcFacts creatureWarrior = Npc(3, Warrior()); creatureWarrior.isCreature = true;
    Check(!DecideNpc(creatureWarrior, rules, all).eligible, "creature without a mage class -> skipped");
    NpcFacts hagraven = Npc(4, Mage()); hagraven.isCreature = true;
    Check(DecideNpc(hagraven, rules, all).eligible && DecideNpc(hagraven, rules, all).tier == ClassTier::Mage, "creature with a mage class receives the mage share");
    NpcFacts redWarrior = Npc(5, Warrior()); redWarrior.isRedguard = true;
    Check(!DecideNpc(redWarrior, rules, all).eligible, "Redguard non-mage -> nothing");
    NpcFacts redMage = Npc(6, Elemental()); redMage.isRedguard = true;
    { bool noUndead = true; for (int i = 0; i < 200; ++i) { redMage.localId = 1000 + i; const auto d = DecideNpc(redMage, rules, all);
        noUndead = noUndead && d.eligible && d.undead == UndeadPolicy::NoUndead && !AnyVariant(d, School::Conjuration, Variant::All) && !AnyVariant(d, School::Conjuration, Variant::NecroPrefer); }
      Check(noUndead, "Redguard mage: conjuration only from NoUndeadAll"); }
    NpcFacts falion = Npc(7, Mage()); falion.isRedguard = true; falion.isUndeadException = true;
    Check(DecideNpc(falion, rules, all).undead == UndeadPolicy::Conjurer, "Falion is a conjurer despite being Redguard");
    NpcFacts redVampire = Npc(8, BattleConjurer()); redVampire.isRedguard = true; redVampire.groupsMatched = { 2 };
    { const auto d = DecideNpc(redVampire, rules, all); Check(d.eligible && d.group == 2 && d.undead == UndeadPolicy::Necromancer, "Redguard vampire hybrid is eligible via the Vampire group"); 
      std::size_t conjLists = 0; bool onlyConj = true;
      for (int i = 0; i < 200; ++i) { redVampire.localId = 3000 + i;
          for (const auto& a : DecideNpc(redVampire, rules, all).lists) { const bool conj = SlotTable()[a.slotIndex].key.school == School::Conjuration && !SlotTable()[a.slotIndex].key.nonMage; onlyConj = onlyConj && conj; conjLists += conj ? 1 : 0; } }
      Check(onlyConj && conjLists > 0, "...and only for Conjuration (destruction is weighted too but dropped)"); }
    NpcFacts redWarriorVampire = Npc(12, Warrior()); redWarriorVampire.isRedguard = true; redWarriorVampire.groupsMatched = { 2 };
    { bool none = true; for (int i = 0; i < 200; ++i) { redWarriorVampire.localId = 4000 + i; const auto d = DecideNpc(redWarriorVampire, rules, all); none = none && !d.eligible && d.lists.empty(); }
      Check(none, "Redguard non-mage vampire: nothing, not even the 5% curing/armor list"); }
    NpcFacts vampNecro = Npc(9, Mage()); vampNecro.groupsMatched = { 3, 2 };
    Check(DecideNpc(vampNecro, rules, all).group == 2, "Vampire precedes Necromancer (built-in order)");
    NpcFacts necroClass = Npc(10, Elemental()); necroClass.isNecromancerClass = true;
    Check(DecideNpc(necroClass, rules, all).undead == UndeadPolicy::Necromancer, "CombatMageNecro is a necromancer without a faction");
    NpcFacts thalmorNecro = Npc(11, Mage()); thalmorNecro.isNecromancerClass = true; thalmorNecro.groupsMatched = { 4 };
    { bool ok = true; for (int i = 0; i < 100; ++i) { thalmorNecro.localId = 2000 + i; const auto d = DecideNpc(thalmorNecro, rules, all); ok = ok && !AnyVariant(d, School::Conjuration, Variant::NecroPrefer) && !AnyVariant(d, School::Conjuration, Variant::NoUndeadAll); }
      Check(ok, "Thalmor group: never undead overrides the necromancer class"); }

    // Expected counts: 20,000 mage NPCs; rung attach frequency equals chance; per-tier expectation equals tierTarget
    std::array<double, kRungCount> rungHits{}; std::size_t preferHits = 0, preferTotal = 0; const int kNpcs = 20000;
    for (int i = 0; i < kNpcs; ++i) { NpcFacts m = Npc(100000 + i, Mage()); m.groupsMatched = { 4 }; const auto d = DecideNpc(m, rules, all);
        for (const auto& a : d.lists) { const auto& k = SlotTable()[a.slotIndex].key; if (k.school != School::Destruction || k.nonMage) continue; rungHits[k.rung] += 1; ++preferTotal; if (k.variant == Variant::Prefer) ++preferHits; } }
    bool freq = true; for (std::size_t r = 0; r < kRungCount; ++r) freq = freq && std::abs(rungHits[r] / kNpcs - rules.ladder[r].chance) < 0.02;
    Check(freq, "rung attach frequency within 0.02 of its chance over 20,000 NPCs");
    Check(std::abs(double(preferHits) / preferTotal - 0.75) < 0.02, "Prefer variant chosen ~75% of the time");
    std::array<double, kTierCount> expectedPerTier{}; for (std::size_t r = 0; r < kRungCount; ++r) for (std::size_t t = static_cast<std::size_t>(rules.ladder[r].startTier); t < kTierCount; ++t) expectedPerTier[t] += rungHits[r] / kNpcs;
    bool targets = true; for (std::size_t t = 0; t < kTierCount; ++t) targets = targets && std::abs(expectedPerTier[t] - rules.tierTarget[t]) < 0.05;
    Check(targets, "expected spells per tier match tierTarget (1.0 1.25 1.5 2.0 2.5) within 0.05");
    // Hybrid: half, weighted schools only
    double hybridDest = 0; std::size_t hybridConj = 0;
    for (int i = 0; i < kNpcs; ++i) { const auto d = DecideNpc(Npc(200000 + i, Spellsword()), rules, all); hybridDest += CountSchool(d, School::Destruction); hybridConj += CountSchool(d, School::Conjuration); }
    Check(hybridConj == 0, "hybrid gets nothing in an unweighted school");
    Check(std::abs(hybridDest / kNpcs - 0.5 * (1.0 + 0.25 + 0.25 + 0.5 + 0.5)) < 0.05, "hybrid attach expectation is half the mage's");
    // Non-mage: 5% one novice curing/armor list
    std::size_t nonMageLists = 0; for (int i = 0; i < kNpcs; ++i) { const auto d = DecideNpc(Npc(300000 + i, Warrior()), rules, all); nonMageLists += d.lists.size(); }
    Check(std::abs(double(nonMageLists) / kNpcs - 0.05) < 0.01, "non-mage: about 5% get one list");
    // Determinism and missing lists
    const auto a = DecideNpc(Npc(42, Mage()), rules, all), b = DecideNpc(Npc(42, Mage()), rules, all);
    Check(a.lists.size() == b.lists.size() && std::equal(a.lists.begin(), a.lists.end(), b.lists.begin(), [](const Attachment& x, const Attachment& y) { return x.slotIndex == y.slotIndex && x.chunk == y.chunk; }), "same NPC, same decision");
    ListIndex none; none.present.assign(SlotTable().size(), { false, false, false });
    const auto empty = DecideNpc(Npc(43, Mage()), rules, none);
    Check(empty.eligible && empty.lists.empty() && empty.slotsWithoutList > 0, "no lists present: nothing attached, counted");
    ListIndex onlyChunk2; onlyChunk2.present.assign(SlotTable().size(), { false, false, true });
    { bool ok = true; for (const auto& at : DecideNpc(Npc(44, Mage()), rules, onlyChunk2).lists) ok = ok && at.chunk == 2; Check(ok, "only present chunks are attached"); }
    Check(Dice("a.esp", 1, "x") != Dice("a.esp", 2, "x") && Dice("a.esp", 1, "x") >= 0.0 && Dice("a.esp", 1, "x") < 1.0, "dice differ per NPC and stay in [0,1)");
    return NpcSpellsCheck::g_failed;
}
```
- [ ] Build (SkyrimSE check), run - expected failures.
- [ ] Implement `NpcSpellsTemplate.cpp`:
```cpp
#include "core/NpcSpellsTemplate.h"
namespace NpcSpells {
std::optional<std::size_t> ProviderIndex(const std::vector<ChainLink>& chain, Category category)
{
    const auto bit = static_cast<std::uint16_t>(category);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if ((chain[i].templateFlags & bit) == 0) return i;
        if (i + 1 >= chain.size() || chain[i + 1].isLeveledCharacter) return std::nullopt;
    }
    return std::nullopt;
}
}
```
- [ ] Implement `NpcSpellsNpc.cpp` (`Dice` and `DecideNpc`); core of it:
```cpp
#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsClassTier.h"
#include "core/NpcSpellsSlots.h"
#include <format>
namespace NpcSpells {
namespace {
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull, kFnvPrime = 1099511628211ull;
    std::uint64_t Hash(std::string_view pluginLower, std::uint32_t localId, std::string_view salt) {
        std::uint64_t h = kFnvOffset;
        auto mix = [&](unsigned char c) { h ^= c; h *= kFnvPrime; };
        for (unsigned char c : pluginLower) mix(c); mix('|'); for (int i = 0; i < 4; ++i) mix(static_cast<unsigned char>(localId >> (8 * i))); mix('|'); for (unsigned char c : salt) mix(c);
        return h;
    }
    std::uint8_t PickChunk(const ListIndex& lists, std::size_t slot, std::uint64_t hash, bool& found) {
        const auto start = static_cast<std::uint8_t>((hash >> 8) % kMaxChunks);
        for (std::uint8_t i = 0; i < kMaxChunks; ++i) { const auto c = static_cast<std::uint8_t>((start + i) % kMaxChunks); if (lists.present[slot][c]) { found = true; return c; } }
        found = false; return 0;
    }
    bool Attach(NpcDecision& d, const ListIndex& lists, const SlotKey& key, std::uint64_t hash) {
        const auto slot = FindSlot(key);
        if (!slot || *slot >= lists.present.size()) return false;
        bool found = false; const auto chunk = PickChunk(lists, *slot, hash, found);
        if (found) d.lists.push_back({ *slot, chunk });
        return found;
    }
    // Variant for one school rung; fallbacks Prefer/NecroPrefer -> All, NoUndeadAll -> nothing
    void AttachRung(NpcDecision& d, const NpcFacts& npc, const Rules& rules, const ListIndex& lists, School school, std::uint8_t rung) {
        const std::string salt = std::format("{}:R{}:prefer", kSchoolNames[static_cast<std::size_t>(school)], rung);
        const double u = Dice(npc.pluginLower, npc.localId, salt);
        const std::uint64_t h = Hash(npc.pluginLower, npc.localId, salt);
        const auto policy = kGroups[d.group].policy;
        const GroupRule* g = nullptr; for (const auto& gr : rules.groups) if (gr.name == kGroups[d.group].name) g = &gr;
        const double share = g ? g->preferShare : rules.conjurationShare;
        Variant first = Variant::All, second = Variant::All; bool noUndeadOnly = false;
        if (school != School::Conjuration || policy == UndeadGroupPolicy::NeverUndead) { if (d.group != kGroupNone) first = Variant::Prefer; }
        else if (d.undead == UndeadPolicy::Necromancer) first = Variant::NecroPrefer;
        else if (d.undead == UndeadPolicy::Conjurer) first = (d.group == kGroupNone) ? Variant::NoUndeadAll : Variant::Prefer;
        else { noUndeadOnly = true; first = (d.group == kGroupNone) ? Variant::NoUndeadAll : Variant::Prefer; second = Variant::NoUndeadAll; }
        const Variant chosen = (first != Variant::All && u < (school == School::Conjuration && d.undead == UndeadPolicy::Necromancer ? rules.conjurationShare : share)) ? first : second;
        if (Attach(d, lists, { school, d.group, chosen, rung, false }, h)) return;
        if (chosen != Variant::All && !noUndeadOnly && Attach(d, lists, { school, d.group, Variant::All, rung, false }, h)) return;
        if (noUndeadOnly && chosen != Variant::NoUndeadAll && Attach(d, lists, { school, d.group, Variant::NoUndeadAll, rung, false }, h)) return;
        ++d.slotsWithoutList;
    }
}
double Dice(std::string_view pluginLower, std::uint32_t localId, std::string_view salt)
{
    constexpr int kMantissaBits = 53;                                       // a double holds 53 bits exactly
    constexpr double kDiceScale = 1.0 / static_cast<double>(1ull << kMantissaBits);
    return static_cast<double>(Hash(pluginLower, localId, salt) >> (64 - kMantissaBits)) * kDiceScale;   // top 53 bits -> [0,1)
}
NpcDecision DecideNpc(const NpcFacts& npc, const Rules& rules, const ListIndex& lists)
{
    NpcDecision d;
    if (npc.isPlayer) { d.skipReason = "player"; return d; }
    if (npc.inheritsSpellList) { d.skipReason = "template spell list"; return d; }
    if (npc.isBeast) { d.skipReason = "beast race"; return d; }
    d.tier = npc.hasClass ? ClassifyClass(npc.weights, rules) : ClassTier::NonMage;
    if (npc.isCreature && d.tier != ClassTier::Mage) { d.skipReason = "creature not a mage"; return d; }
    for (std::uint8_t g : npc.groupsMatched) if (g != kGroupNone && (d.group == kGroupNone || g < d.group)) d.group = g;
    const auto policy = kGroups[d.group].policy;
    if (policy == UndeadGroupPolicy::AlwaysNecro || npc.isNecromancerClass) d.undead = UndeadPolicy::Necromancer;
    else if (npc.isUndeadException || (npc.hasClass && IsConjurationClass(npc.weights) && !npc.isRedguard)) d.undead = UndeadPolicy::Conjurer;
    std::array<bool, kSchoolCount> schools{};
    if (d.tier == ClassTier::Mage) schools.fill(true); else if (d.tier == ClassTier::Hybrid) schools = SchoolsWeighted(npc.weights);
    if (npc.isRedguard && d.tier != ClassTier::Mage) {
        // Only a hybrid vampire/necromancer keeps its conjuration share; a NonMage-tier Redguard never gets the 5% list (§5.1)
        if (policy != UndeadGroupPolicy::AlwaysNecro || d.tier == ClassTier::NonMage) { d.skipReason = "redguard non-mage"; return d; }
        const bool conj = schools[static_cast<std::size_t>(School::Conjuration)]; schools.fill(false); schools[static_cast<std::size_t>(School::Conjuration)] = conj;
    }
    d.eligible = true;
    const double scale = d.tier == ClassTier::Hybrid ? rules.hybridScale : 1.0;
    for (std::size_t s = 0; s < kSchoolCount; ++s) {
        if (!schools[s]) continue;
        for (std::uint8_t r = 0; r < rules.ladder.size() && r < kRungCount; ++r) {
            const std::string salt = std::format("{}:R{}", kSchoolNames[s], r);
            if (Dice(npc.pluginLower, npc.localId, salt) < rules.ladder[r].chance * scale) AttachRung(d, npc, rules, lists, static_cast<School>(s), r);
        }
    }
    if (d.tier == ClassTier::NonMage) {
        const double u = Dice(npc.pluginLower, npc.localId, "nonmage");
        const std::uint64_t h = Hash(npc.pluginLower, npc.localId, "nonmage");
        if (u < rules.nonMageChance / 2) { if (!Attach(d, lists, { School::Restoration, d.group, Variant::All, 0, true }, h)) ++d.slotsWithoutList; }
        else if (u < rules.nonMageChance) { if (!Attach(d, lists, { School::Alteration, d.group, Variant::All, 0, true }, h)) ++d.slotsWithoutList; }
    }
    return d;
}
}
```
  If `NpcSpellsNpc.cpp` approaches 600 lines when the implementer adds comments, move `AttachRung` and the variant choice to `NpcSpellsNpcVariant.cpp`.
- [ ] Build, run - expected all PASS (tolerances are generous; if the Monte Carlo tests flap, the hash is biased - fix `Dice`, do not widen the tolerance).
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsTemplate.h plugins/NPCSpells/src/core/NpcSpellsTemplate.cpp plugins/NPCSpells/src/core/NpcSpellsNpc.h plugins/NPCSpells/src/core/NpcSpellsNpc.cpp tools/CMakeLists.txt tools/npcspells-selftest-npc.cpp; git commit -m "NPC Spells: NPC eligibility, template chain and list decisions" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 8: DLL skeleton, files, ESL lookup

**Files**
- Create: `plugins/NPCSpells/CMakeLists.txt`, `plugins/NPCSpells/src/Main.cpp`, `plugins/NPCSpells/src/game/GameFiles.h`, `plugins/NPCSpells/src/game/GameFiles.cpp`, `plugins/NPCSpells/src/game/GameLists.h`, `plugins/NPCSpells/src/game/GameLists.cpp`, `plugins/NPCSpells/src/game/GameRun.h`, `plugins/NPCSpells/src/game/GameRun.cpp`
- Create (empty-but-compiling stubs, filled in Tasks 9-10): `plugins/NPCSpells/src/game/GamePool.cpp`, `plugins/NPCSpells/src/game/GameNpcs.cpp`, `plugins/NPCSpells/src/core/NpcSpellsReport.cpp`
- Modify: `CMakeLists.txt` (top level: `add_subdirectory(plugins/NPCSpells)` after BookXP), `CLAUDE.md` ("four DLLs", Key Locations row)
- Test: build succeeds; manual log check (no RE in the harness). The offline guarantees come from Tasks 1-7.

**Interfaces**
```cpp
namespace NpcSpellsGame {
    inline constexpr const char* kTreePath = "Data/SKSE/Plugins/SpellLearning/spell_tree.json";        // UIManagerCore.cpp:28
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    inline constexpr const char* kRulesPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells.json";
    inline constexpr const char* kReportPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells_report.txt";
    inline constexpr const char* kDataDir = "Data";
    // distr: the *_DISTR.ini (SPID) files only
    struct Inputs { nlohmann::json tree, catalog; NpcSpells::Rules rules; bool rulesFromDefaults = false; std::string rulesError; TomeReach::ConfigText distr; bool ok = false; std::string stopReason; };
    Inputs ReadInputs();                                     // GameFiles
    struct EslRecords { bool loaded = false; std::vector<RE::TESLevSpell*> byRecord; std::size_t missing = 0, wrongType = 0; };   // index = slot*kMaxChunks+chunk
    EslRecords LookupEslRecords();                           // GameLists; TESDataHandler::LookupLoadedLightModByName + LookupForm<TESLevSpell>
    void Run();                                              // GameRun: everything at kDataLoaded
}
```
RE members used (verify on the checkout): `RE::TESDataHandler::GetSingleton()` (`RE/T/TESDataHandler.h:46`), `LookupLoadedLightModByName` (`:78`, light plugins only), `LookupLoadedModByName` (`:74`, to tell "present but not light"), `LookupForm(localId, modName)` (untyped `:62`, template `:65`; `LookupFormID` `src/RE/T/TESDataHandler.cpp:125-141` builds `compileIndex<<24 + smallFileCompileIndex<<12 + local` at `:136-138` without checking the form exists, so `LookupForm` + null check is the existence test), `RE::TESForm::As<RE::TESLevSpell>()` (`RE/T/TESForm.h:259`), `RE::TESLevSpell::FORMTYPE` (`RE/T/TESLevSpell.h:16`). `TomeReach::FindDistributionConfigs(dataDir)` (`plugins/spelllearning/src/tomereach/TomeReachConfig.h:25`), `ConfigText::Add` (`:30`) / `Names` (`:34`), `TomeReach::LowerAscii` (`TomeReachRecords.h:68`, defined in `TomeReachRecords.cpp:16`).

**Steps**
- [ ] `plugins/NPCSpells/CMakeLists.txt` (BookXP's shape; sources list every `src/core/*.cpp` and `src/game/*.cpp` plus `src/Main.cpp` and the two tomereach files `TomeReachConfig.cpp` and `TomeReachRecords.cpp` - `TomeReachConfig.cpp` calls `TomeReach::LowerAscii`, defined in `TomeReachRecords.cpp:16`, which includes `<zlib.h>`, so the DLL links `ZLIB::ZLIB` as the main plugin does, `plugins/spelllearning/CMakeLists.txt:124`; `tools/tome-reach-test` builds the same pair the same way):
```cmake
project(HeartOfMagic_NPCSpells VERSION ${PROJECT_VERSION} DESCRIPTION "Heart of Magic - NPC Spells" LANGUAGES CXX)
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Use: cmake -B build")
endif()
set(NPCSPELLS_SOURCES
    src/Main.cpp
    src/core/NpcSpellsRules.cpp src/core/NpcSpellsClassTier.cpp src/core/NpcSpellsPool.cpp src/core/NpcSpellsSlots.cpp
    src/core/NpcSpellsBuild.cpp src/core/NpcSpellsTemplate.cpp src/core/NpcSpellsNpc.cpp src/core/NpcSpellsReport.cpp
    src/game/GameFiles.cpp src/game/GameLists.cpp src/game/GamePool.cpp src/game/GameNpcs.cpp src/game/GameRun.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/tomereach/TomeReachConfig.cpp
    ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src/tomereach/TomeReachRecords.cpp
)
add_commonlibsse_plugin(${PROJECT_NAME} NAME ${PROJECT_NAME} AUTHOR "HeartOfMagic" USE_ADDRESS_LIBRARY MINIMUM_SKSE_VERSION "2.0.12" VERSION ${PROJECT_VERSION} SOURCES ${NPCSPELLS_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/plugins/spelllearning/src)
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../PCH.h)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json ZLIB::ZLIB)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "HeartOfMagic_NPCSpells")
```
  Core files must not include `PCH.h`; they include only std/nlohmann headers so the DLL's PCH and the tools' stub PCH both work (`GamePool.cpp`, `GameNpcs.cpp`, `NpcSpellsReport.cpp` are filled in Tasks 9-10; create them now as empty-but-compiling files - a comment line - and commit them with this task).
- [ ] `src/Main.cpp`:
```cpp
#include "Common.h"
#include "game/GameRun.h"
namespace {
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            NpcSpellsGame::Run();
            FlushLog();
        }
    }
}
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("Heart of Magic - NPC Spells: fills the ESL's leveled spell lists from the tree at data load");
    SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
    return true;
}
```
- [ ] `GameFiles.cpp`: `ReadInputs()` reads the three files with `std::ifstream` (missing tree or catalog -> `stopReason = "spell_tree.json or spell_catalog.json not found (run a Heart of Magic scan and tree build first)"`); rules: parse with `ParseRules`, on failure `rulesFromDefaults = true`, `rulesError`, `rules = DefaultRules()` (§7); logs each group whose name is not in `kGroups` ("rules: group '<name>' is not a built-in group, ignored"), and logs `rulesError` when `ParseRules` succeeded but dropped a non-FormID entry; `distr`: SPID files only (§5.1 "다른 `_DISTR.ini` 가 이미 주는 주문"; `FindDistributionConfigs` also returns KID, FLM, SWAP, CID, CDF, LLI, SkyPatcher and LLOS files, whose mentions do not give spells):
```cpp
constexpr std::string_view kSpidSuffix = "_distr.ini";   // in = the Inputs ReadInputs returns
for (const auto& f : TomeReach::FindDistributionConfigs(kDataDir))
    if (TomeReach::LowerAscii(f.filename().string()).ends_with(kSpidSuffix)) in.distr.Add(f);
```
  `ConfigText` keeps each file's whole text, so a spell named only in a `_DISTR.ini` filter (not on a `Spell =` line) is excluded too; that over-match is accepted (a few spells left out needlessly).
- [ ] `GameLists.cpp`:
```cpp
NpcSpellsGame::EslRecords NpcSpellsGame::LookupEslRecords()
{
    EslRecords out;
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return out;
    if (!dh->LookupLoadedLightModByName(NpcSpells::kEslFileName)) {
        if (dh->LookupLoadedModByName(NpcSpells::kEslFileName)) logger::error("NPCSpells: {} is loaded without the ESL flag - reinstall the mod", NpcSpells::kEslFileName);
        else logger::error("NPCSpells: {} is not loaded - enable the plugin in your mod manager", NpcSpells::kEslFileName);
        return out;
    }
    out.loaded = true;
    const auto& table = NpcSpells::SlotTable();
    out.byRecord.assign(table.size() * NpcSpells::kMaxChunks, nullptr);
    for (std::size_t slot = 0; slot < table.size(); ++slot)
        for (std::uint8_t c = 0; c < NpcSpells::kMaxChunks; ++c) {
            auto* form = dh->LookupForm(NpcSpells::LocalFormId(slot, c), NpcSpells::kEslFileName);
            if (!form) { ++out.missing; continue; }
            auto* lev = form->As<RE::TESLevSpell>();
            if (!lev) { ++out.wrongType; logger::warn("NPCSpells: record 0x{:03X} is not a leveled spell list", NpcSpells::LocalFormId(slot, c)); continue; }
            out.byRecord[slot * NpcSpells::kMaxChunks + c] = lev;
        }
    return out;
}
```
- [ ] `GameRun.cpp` (first version): `Run()` times `ReadInputs`, logs the stop reason and returns if `!ok`; calls `LookupEslRecords`, logs `loaded/missing/wrongType`; later tasks add pool, lists, NPCs, report. Wrap the body in `try { } catch (const std::exception& e) { logger::error(...); }` so a malformed file never takes the game down.
- [ ] Top-level `CMakeLists.txt`: `add_subdirectory(plugins/NPCSpells)` after `add_subdirectory(plugins/BookXP)`.
- [ ] `CLAUDE.md`: "The build produces four DLLs: ... and `HeartOfMagic_NPCSpells.dll` (NPC Spells addon, own ESL and zip)"; Key Locations row `| NPC Spells addon | plugins/NPCSpells/ |`.
- [ ] Build (SkyrimSE check) - expected: `build/plugins/NPCSpells/Release/HeartOfMagic_NPCSpells.dll` exists; `npcspells-test` still passes.
- [ ] Commit: `git add CMakeLists.txt plugins/NPCSpells/CMakeLists.txt plugins/NPCSpells/src/Main.cpp plugins/NPCSpells/src/game plugins/NPCSpells/src/core/NpcSpellsReport.cpp CLAUDE.md; git commit -m "NPC Spells: SKSE plugin skeleton, input files and ESL record lookup" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 9: Game layer - resolve spells, fill lists, attach to NPCs

**Files**
- Create: `plugins/NPCSpells/src/game/GamePool.h/.cpp`, `plugins/NPCSpells/src/game/GameNpcs.h/.cpp`
- Modify: `plugins/NPCSpells/src/game/GameRun.cpp`, `plugins/NPCSpells/src/game/GameLists.h/.cpp`
- Test: build; in-game log (Task 12 checklist). Offline: Tasks 3, 5, 7 already pin the logic this layer feeds.

**Interfaces**
```cpp
namespace NpcSpellsGame {
    struct ResolvedPool { std::vector<NpcSpells::SpellFacts> facts; std::vector<RE::SpellItem*> forms; };   // parallel arrays: forms[i] is facts[i]'s spell
    ResolvedPool ResolvePool(const nlohmann::json& tree, const nlohmann::json& catalog, const NpcSpells::Rules& rules, const TomeReach::ConfigText& distr, NpcSpells::PoolStats& stats);
    // Writes entries into each ESL record present; returns the ListIndex for DecideNpc.
    NpcSpells::ListIndex FillLists(const std::vector<NpcSpells::BuiltList>& lists, const ResolvedPool& pool, const EslRecords& records, std::size_t& skippedNoRecord);
    struct NpcStats { std::size_t scanned = 0, eligible = 0, attached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0; std::map<std::string, std::size_t> skipped; std::array<std::size_t, 3> byTier{}; std::array<std::size_t, 6> byGroup{}; };
    // lines: one row per NPC that got at least one list (every one, no cap; the report lists them all)
    NpcStats AttachToNpcs(const NpcSpells::Rules& rules, const NpcSpells::ListIndex& index, const EslRecords& records, std::vector<NpcSpells::NpcLine>& lines);
}
```
RE members: `RE::TESDataHandler::GetFormArray<RE::TESNPC>()` (`RE/T/TESDataHandler.h:89`); `RE::TESNPC::npcClass` (`RE/T/TESNPC.h:272`), `GetRace()` (`:252`), `GetSpellList()` (`:251`, allocates `SpellData` through CommonLib when null, `src/RE/T/TESNPC.cpp:164-170`), `actorData.templateUseFlags` (`REX::EnumSet<TEMPLATE_USE_FLAG, std::uint16_t>`, `.underlying()` `REX/REX/EnumSet.h:56`) / `baseTemplateForm` / `factions` (`RE/T/TESActorBaseData.h:74` inside `ACTOR_BASE_DATA`, member `actorData` `:133`; `baseTemplateForm` `:136` and `factions` `:139` are direct `TESActorBaseData` members; `FACTION_RANK::faction` `RE/F/FactionRank.h:11`), `TESLevCharacter::FORMTYPE` (`RE/T/TESLevCharacter.h:18`), `TESClass::data` (`RE/T/TESClass.h:118`) `.skillWeights` (`CLASS_DATA`, `TESClass.h:75`; `SkillWeights` `:36-59`), `TESRace::HasKeywordID` (`RE/B/BGSKeywordForm.h:36`; TESRace is a BGSKeywordForm, `RE/T/TESRace.h:147`), `TESForm::GetFile(0)` (`RE/T/TESForm.h:275`), `GetLocalFormID()` (`:295`), `GetFormID()` (`:290`), `Is(FormType)` (`:318`), `As<T>()` (`:259`), `GetName()` (`:308`), `IsPlayer()` (`:351`), `TESFile::GetFilename()` (`RE/T/TESFile.h:58`), `TESDataHandler::LookupForm(local, plugin)` (`RE/T/TESDataHandler.h:62`), `TESSpellList::SpellData::AddLevSpells(const std::vector<TESLevSpell*>&)` (`RE/T/TESSpellList.h:24`; skips lists already present, copies the array once, `src/RE/T/TESSpellList.cpp:69-77`), `TESLeveledList::entries/chanceNone/llFlags/numEntries` (`RE/T/TESLeveledList.h:70-73`; `chanceNone` is `std::int8_t`; `Flag : std::uint8_t` `:45-51`), `SimpleArray::resize` (`RE/M/MemoryManager.h:368`, game heap, default-constructs new elements), `LEVELED_OBJECT{form,count,level,pad0C,itemExtra}` (`TESLeveledList.h:15-24`), `SpellItem::GetAssociatedSkill` (`RE/S/SpellItem.h:81`), `GetSpellType` (`:67`), `RE::ActorValue::kAlteration..kRestoration = 18..22` (`RE/A/ActorValues.h:26-30`).

**Steps**
- [ ] `GamePool.cpp`: `ResolvePool` = `BuildPool(tree, catalog, rules, distributedFn, stats)` where `distributedFn` splits the persistentId at `|` (plugin, hex local) and calls `distr.Names(lowerPlugin, local, "")` (editor IDs are not in the catalog; the FormID forms are 394 of 431 SPID lines in TAKEALOOK, ledger). Then `ApplyEngineSchool` with a callback that resolves `dh->LookupForm<RE::SpellItem>(local, plugin)`, requires `GetSpellType() == RE::MagicSystem::SpellType::kSpell`, maps `GetAssociatedSkill()` 18..22 -> `School`, anything else -> `nullopt`, and records every spell it accepts in a side map; `ApplyEngineSchool` drops entries, so `forms` is filled after it from that map:
```cpp
std::unordered_map<std::string, RE::SpellItem*> resolved;   // persistentId -> form, filled inside the EngineSchoolFn
NpcSpells::ApplyEngineSchool(out.facts, [&](const std::string& pid) -> std::optional<NpcSpells::School> {
    RE::SpellItem* spell = LookupSpell(pid);                    // split at '|', LookupForm<RE::SpellItem>(local, plugin)
    if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) return std::nullopt;
    const auto school = SchoolFromSkill(spell->GetAssociatedSkill());   // kAlteration..kRestoration -> School, else nullopt
    if (school) resolved[pid] = spell;
    return school; }, stats);
out.forms.reserve(out.facts.size());
for (const auto& f : out.facts) out.forms.push_back(resolved.at(f.persistentId));   // parallel to facts
``` Persistent-id plugin names with non-ASCII bytes: use `PluginNames::Lookup` from `plugins/spelllearning/include/PluginNames.h` the way `SpellScannerFormId.cpp:46-112` does, or accept the miss and count it (document the choice in README).
- [ ] `GameLists.cpp` `FillLists`:
```cpp
NpcSpells::ListIndex NpcSpellsGame::FillLists(const std::vector<NpcSpells::BuiltList>& lists, const ResolvedPool& pool, const EslRecords& records, std::size_t& skippedNoRecord)
{
    NpcSpells::ListIndex index; index.present.assign(NpcSpells::SlotTable().size(), {});
    for (const auto& list : lists) {
        auto* lev = records.byRecord[list.slotIndex * NpcSpells::kMaxChunks + list.chunk];
        if (!lev) { ++skippedNoRecord; continue; }
        lev->entries.resize(list.entries.size());                       // SimpleArray, game allocator
        for (std::size_t i = 0; i < list.entries.size(); ++i) {
            auto& e = lev->entries[i];
            e.form = pool.forms[list.entries[i].spellIndex]; e.count = 1; e.level = list.entries[i].level; e.pad0C = 0; e.itemExtra = nullptr;
        }
        lev->numEntries = static_cast<std::uint8_t>(list.entries.size());
        lev->chanceNone = 0;
        lev->llFlags = static_cast<RE::TESLeveledList::Flag>(list.flags);   // 1 = kCalculateFromAllLevelsLTOrEqPCLevel
        index.present[list.slotIndex][list.chunk] = true;
    }
    return index;
}
```
- [ ] `GameNpcs.cpp`: first resolve every rules `FormRef` once to a runtime FormID - `dh->LookupForm(ref.local, ref.pluginLower)` (`TESDataHandler.h:62`), null -> log the ref once ("rules: <XXXXXX:plugin> not found in this load order, ignored"), `++unresolvedRefs`, drop that ref only (the rest of its group or list stays, §7); else keep `form->GetFormID()`. Results: `std::unordered_set<RE::FormID>` for `necromancerClasses`, `beastRaces`, `undeadExceptNpcs`; one FormID each for `redguardRace` and the three keywords; `std::unordered_map<RE::FormID, std::uint8_t>` faction -> built-in group index from `rules.groups`. Forms are matched by runtime FormID only - never by `GetFormEditorID()` (empty for most forms without powerofthree's Tweaks). Then build `NpcFacts` per `TESNPC`:
  - `pluginLower` = `TomeReach::LowerAscii(npc->GetFile(0)->GetFilename())` (null file -> skip, "no source file"); `localId = npc->GetLocalFormID()`; `isPlayer = npc->IsPlayer()` (`TESForm.h:351`, FormID 0x7).
  - Chain (a link may be a `TESLevCharacter`, which has no `actorData`; never dereference it as an NPC):
```cpp
constexpr std::size_t kMaxTemplateDepth = 8;   // a longer chain is malformed data; the walk stops and ProviderIndex reports it unresolvable
std::vector<NpcSpells::ChainLink> chain;
RE::TESForm* cur = npc;
for (std::size_t depth = 0; cur && depth < kMaxTemplateDepth; ++depth) {
    auto* n = cur->As<RE::TESNPC>();
    chain.push_back({ n ? n->actorData.templateUseFlags.underlying() : std::uint16_t{ 0 }, cur->Is(RE::TESLevCharacter::FORMTYPE) });
    if (!n) break;
    cur = n->baseTemplateForm;   // TESActorBaseData member, not inside actorData
}
```
    `inheritsSpellList = (chain[0].templateFlags & static_cast<std::uint16_t>(Category::Spells)) != 0`. Providers via `ProviderIndex` for Stats (class), Traits (race), Factions; `unresolvable*` when nullopt (then use the record's own fields and count). A provider index points at a concrete `TESNPC` link (ProviderIndex returns nullopt before stepping onto a leveled link); keep the walked `TESNPC*` pointers beside `chain` (push `n` in the same loop) to read it.
  - Class: `provider->npcClass` -> `weights.w[i] = reinterpret_cast<const std::uint8_t*>(&cls->data.skillWeights)[i]` for 0..17 (`SkillWeights` is 18 contiguous `uint8_t`, `TESClass.h:59` static_assert 0x12); `hasClass = cls != nullptr`; `isNecromancerClass` = `cls->GetFormID()` in the resolved `necromancerClasses` set.
  - Race: `isBeast` = race FormID in the resolved `beastRaces` set or `race->HasKeywordID(animal)`; `isCreature = race->HasKeywordID(creature) || !race->HasKeywordID(npc)`; `isRedguard` = race FormID == resolved `redguardRace`.
  - Factions: for each `FACTION_RANK` in the provider's `factions`, look `faction->GetFormID()` up in the faction -> group map; `multiGroup++` when more than one distinct group.
  - `isUndeadException` = `npc->GetFormID()` in the resolved `undeadExceptNpcs` set.
  - `DecideNpc`; when `d.lists` is non-empty, collect the `TESLevSpell*` (`records.byRecord[slot*kMaxChunks+chunk]`) into one `std::vector` and call `npc->GetSpellList()->AddLevSpells(vec)` once per NPC (`TESSpellList.h:24`; it skips lists the NPC already has and copies the array once - `AddLevSpell` per list would re-copy it for every list). Collect `byTier/byGroup`, and push one `NpcLine{ persistentId = std::format("{}|0x{:06X}", npc->GetFile(0)->GetFilename(), localId), name = npc->GetName(), decision }` into `lines` for every NPC that got lists (no cap; ruling S8).
- [ ] `GameRun.cpp`: order and timing with `std::chrono::steady_clock`: read inputs -> ESL records -> `ResolvePool` -> `BuildLists` -> `FillLists` -> `AttachToNpcs` (keep its `std::vector<NpcLine>` for the Task 10 report); log every count, including `unresolvedRefs`; stop early (with the reason) when inputs fail or the ESL is not loaded. Log total ms; the spec's budget is 0.5 s for 26k NPCs (§9).
- [ ] Build (SkyrimSE check). Deploy with Task 11's switch once it exists; for now copy by hand is forbidden - wait for Task 11, or test after Task 11.
- [ ] Commit: `git add plugins/NPCSpells/src/game plugins/NPCSpells/CMakeLists.txt; git commit -m "NPC Spells: resolve tree spells, fill ESL lists, attach to NPC records" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 10: Report file

**Files**
- Create: `plugins/NPCSpells/src/core/NpcSpellsReport.h`, `plugins/NPCSpells/src/core/NpcSpellsReport.cpp`
- Modify: `plugins/NPCSpells/src/game/GameRun.cpp`, `tools/CMakeLists.txt`, `tools/npcspells-selftest-esl.cpp` (append a report check)
- Test: `tools/npcspells-selftest-esl.cpp`

**Interfaces** (`NpcSpellsReport.h` includes `core/NpcSpellsPool.h`, `core/NpcSpellsBuild.h`, `core/NpcSpellsNpc.h`, `<array>`, `<string>`, `<tuple>`, `<utility>`, `<vector>`)
```cpp
namespace NpcSpells {
    struct ReportData {
        std::string version, generatedUtc; bool rulesFromDefaults = false; std::string rulesError; std::string stopReason;
        PoolStats pool; BuildStats build; std::size_t eslRecords = 0, eslMissing = 0, eslWrongType = 0, listsSkippedNoRecord = 0;
        std::vector<std::tuple<std::string, std::size_t, std::uint8_t>> listSizes;   // editorId, entries, flags
        std::size_t npcsScanned = 0, npcsEligible = 0, npcsAttached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0;
        std::vector<std::pair<std::string, std::size_t>> skipped;                     // reason, count (copied from NpcStats::skipped)
        std::array<std::size_t, 3> byTier{}; std::array<std::size_t, 6> byGroup{};
        std::vector<NpcLine> npcs;                                                    // every NPC that got lists, in scan order (no cap)
        std::vector<std::pair<std::string, double>> timingsMs;
    };
    std::string RenderReport(const ReportData& d);
}
```
NPC row format (one line per `NpcLine`; group = `kGroups[decision.group].name`, reason = class tier and undead policy, lists = `EditorIdFor(SlotTable()[a.slotIndex].key, a.chunk)` per attachment):
`Skyrim.esm|0x01CB4E  Bandit Wizard  group None  mage, conjurer: HoMNS_Destruction_None_All_R1_C0, HoMNS_Conjuration_None_NoUndeadAll_R1_C0`

**Steps**
- [ ] Add `#include "core/NpcSpellsReport.h"` to the include block of `tools/npcspells-selftest-esl.cpp`, then append to `RunEslSelfTest()` (replacing its `return`):
```cpp
    std::printf("report\n");
    ReportData rd; rd.version = "1.2.0"; rd.stopReason.clear(); rd.pool.kept = 1400; rd.build.lists = 600; rd.build.emptySlots = 12; rd.build.droppedOverCapacity = 0;
    rd.eslRecords = 921; rd.listSizes = { { "HoMNS_Destruction_None_All_R1_C0", 240, 0 } }; rd.npcsScanned = 26090; rd.npcsAttached = 2; rd.skipped = { { "template spell list", 14389 } };
    rd.byTier = { 170, 69, 48 }; rd.timingsMs = { { "total", 123.4 } };
    const auto destSlot = *FindSlot({ School::Destruction, kGroupNone, Variant::All, 0, false });
    const auto thalmorSlot = *FindSlot({ School::Destruction, 4, Variant::Prefer, 1, false });
    NpcDecision mage; mage.eligible = true; mage.tier = ClassTier::Mage; mage.undead = UndeadPolicy::Conjurer; mage.lists = { { destSlot, 0 } };
    NpcDecision thalmor; thalmor.eligible = true; thalmor.tier = ClassTier::Hybrid; thalmor.group = 4; thalmor.lists = { { thalmorSlot, 2 } };
    rd.npcs = { { "Skyrim.esm|0x01CB4E", "Bandit Wizard", mage }, { "Skyrim.esm|0x0D8D62", "Thalmor Justiciar", thalmor } };
    const std::string text = RenderReport(rd);
    Check(text.find("HoMNS_Destruction_None_All_R1_C0") != std::string::npos && text.find("240") != std::string::npos, "report lists each list with its entry count");
    Check(text.find("template spell list") != std::string::npos && text.find("14389") != std::string::npos, "report lists skip reasons with counts");
    Check(text.find("total") != std::string::npos && text.find("123.4") != std::string::npos, "report carries timings");
    Check(text.find("Skyrim.esm|0x01CB4E") != std::string::npos && text.find("Bandit Wizard") != std::string::npos && text.find("conjurer") != std::string::npos,
          "every NPC row: persistent id, name, reason");
    Check(text.find("Skyrim.esm|0x0D8D62") != std::string::npos && text.find("group Thalmor") != std::string::npos && text.find("hybrid") != std::string::npos &&
          text.find(EditorIdFor(SlotTable()[thalmorSlot].key, 2)) != std::string::npos, "every NPC row: group and attached list EditorIDs");
    ReportData stopped; stopped.stopReason = "spell_tree.json not found";
    Check(RenderReport(stopped).find("spell_tree.json not found") != std::string::npos, "a stopped run says why");
    return NpcSpellsCheck::g_failed;
```
- [ ] Build (SkyrimSE check), run - expected failures.
- [ ] Implement `RenderReport` with `std::format` sections: header (`Heart of Magic - NPC Spells report`, version, time, rules source and `rulesError`), `Stopped: <reason>` when set, `Spells` (pool counts), `Lists` (per group/slot: for each built list `EditorID  entries  flag`; empty slots; dropped over capacity; ESL missing/wrong type; lists skipped for a missing record), `NPCs` (scanned, eligible, attached, by tier, by group, skip reasons, unresolvable template, multi-group, unresolved rule refs), `NPC lists` (every `NpcLine`, the row format above; a TAKEALOOK run is ~1-2 MB, accepted - spec §4), `Timing` (each phase in ms). Tier names `non-mage`/`hybrid`/`mage`, undead names `no undead`/`conjurer`/`necromancer`, as `constexpr` arrays next to `RenderReport`.
- [ ] `GameRun.cpp`: fill `ReportData` (`npcs` = the `AttachToNpcs` lines, `skipped` copied from the `NpcStats::skipped` map) and write to `kReportPath` with `std::ofstream` (create `Data/SKSE/Plugins` if missing with `std::filesystem::create_directories`); always write, even on a stop (§7).
- [ ] Build, run harness - PASS.
- [ ] Commit: `git add plugins/NPCSpells/src/core/NpcSpellsReport.h plugins/NPCSpells/src/core/NpcSpellsReport.cpp plugins/NPCSpells/src/game/GameRun.cpp tools/CMakeLists.txt tools/npcspells-selftest-esl.cpp; git commit -m "NPC Spells: report file with list, NPC and timing counts" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 11: Distribution zip and dev deploy

**Files**
- Create: `plugins/cmake/DistributionNpcSpells.cmake`
- Modify: `CMakeLists.txt` (top level: `include(DistributionNpcSpells)` after `include(Distribution)`), `DeployDev.ps1`, `CLAUDE.md` (DeployDev section, dist paragraph), `plugins/cmake/Distribution.cmake` (only if `THIRD-PARTY-NOTICES.md` generation must be shared - it is already configured into `${CMAKE_BINARY_DIR}`; reuse it)
- Test: build produces `dist/HeartOfMagic-NPCSpells_1.2.0.zip`; `Expand-Archive` listing check.

**Steps**
- [ ] `plugins/cmake/DistributionNpcSpells.cmake`:
```cmake
# The NPC Spells addon ships as its own archive, apart from the main mod:
# HeartOfMagic-NPCSpells_<version>.zip = the ESL, the DLL, the rules JSON, the
# licence files (the DLL statically links CommonLibSSE-NG, same notice as the
# main archive); its PDB goes to HeartOfMagic-NPCSpells_<version>_DebugSymbols.zip.
if(NOT DEFINED DIST_DIR)
    return()
endif()
set(NPCSPELLS_DIST_NAME "HeartOfMagic-NPCSpells")
set(NPCSPELLS_DIST_DIR "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}")
set(NPCSPELLS_PDB_DIR "${NPCSPELLS_DIST_DIR}_DebugSymbols")
add_custom_target(assemble_npcspells ALL COMMENT "Assembling the NPC Spells addon...")
add_dependencies(assemble_npcspells HeartOfMagic_NPCSpells npcspells_esl assemble_dist)
add_custom_command(TARGET assemble_npcspells POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${NPCSPELLS_DIST_DIR}" "${NPCSPELLS_PDB_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NPCSPELLS_DIST_DIR}/SKSE/Plugins" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins" "${NPCSPELLS_DIST_DIR}/licenses/CommonLibSSE-NG"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:HeartOfMagic_NPCSpells>" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E $<IF:$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>,copy,true> "$<$<BOOL:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>:$<TARGET_PDB_FILE:HeartOfMagic_NPCSpells>>" "${NPCSPELLS_PDB_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${NPCSPELLS_ESL_FILE}" "${NPCSPELLS_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/plugins/NPCSpells/data/HeartOfMagic_NPCSpells.json" "${NPCSPELLS_DIST_DIR}/SKSE/Plugins/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/THIRD-PARTY-NOTICES.md" "${NPCSPELLS_DIST_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${DIST_VERSION_DIR}/licenses" "${NPCSPELLS_DIST_DIR}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}.zip" --format=zip -- "${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}_DebugSymbols.zip" --format=zip -- "${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}_DebugSymbols"
    WORKING_DIRECTORY "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Creating ${NPCSPELLS_DIST_NAME}_${PROJECT_VERSION}.zip..."
    VERBATIM)
```
  (`assemble_dist` dependency guarantees `${DIST_VERSION_DIR}/licenses` exists when this runs; `THIRD-PARTY-NOTICES.md` already names the same libraries.)
- [ ] `DeployDev.ps1`: add `[string]$NpcSpellsModPath = 'D:\TAKEALOOK\mods\HeartOfMagic-NPCSpells-Dev'` and `[switch]$NpcSpellsOnly`; right after the SkyrimSE check:
```powershell
if ($NpcSpellsOnly) {
    if (-not (Test-Path $NpcSpellsModPath)) { New-Item -ItemType Directory -Force -Path (Join-Path $NpcSpellsModPath 'SKSE\Plugins') | Out-Null }
    $nsDll = Join-Path $repo 'build\plugins\NPCSpells\Release\HeartOfMagic_NPCSpells.dll'
    $nsEsl = Join-Path $repo 'build\npcspells\HeartOfMagic_NPCSpells.esp'
    $nsJson = Join-Path $repo 'plugins\NPCSpells\data\HeartOfMagic_NPCSpells.json'
    foreach ($f in @($nsDll, $nsEsl, $nsJson)) { if (-not (Test-Path $f)) { throw "Missing $f. Run .\BuildRelease.ps1 -preset Release-2026 first." } }
    Copy-Item $nsDll (Join-Path $NpcSpellsModPath 'SKSE\Plugins\HeartOfMagic_NPCSpells.dll') -Force
    Copy-Item $nsJson (Join-Path $NpcSpellsModPath 'SKSE\Plugins\HeartOfMagic_NPCSpells.json') -Force
    Copy-Item $nsEsl (Join-Path $NpcSpellsModPath 'HeartOfMagic_NPCSpells.esp') -Force
    foreach ($pair in @(@($nsDll, 'SKSE\Plugins\HeartOfMagic_NPCSpells.dll'), @($nsEsl, 'HeartOfMagic_NPCSpells.esp'))) {
        if ((Get-FileHash $pair[0]).Hash -ne (Get-FileHash (Join-Path $NpcSpellsModPath $pair[1])).Hash) { throw "Copy did not take: $($pair[1])" }
    }
    Write-Host "npcspells  dll, esl, json -> $NpcSpellsModPath (enable HeartOfMagic_NPCSpells.esp in MO2)"
    return
}
```
  Update the script's comment header (`.EXAMPLE .\DeployDev.ps1 -NpcSpellsOnly`). `$repo` must be defined before this block (move `$repo = $PSScriptRoot` up).
- [ ] Build (SkyrimSE check) - expected: `dist/HeartOfMagic-NPCSpells_1.2.0.zip` and `_DebugSymbols.zip`. Verify: `Expand-Archive dist\HeartOfMagic-NPCSpells_1.2.0.zip -DestinationPath $env:TEMP\ns -Force; Get-ChildItem -Recurse $env:TEMP\ns | Select-Object FullName` - expected: `HeartOfMagic_NPCSpells.esp`, `SKSE/Plugins/HeartOfMagic_NPCSpells.dll`, `SKSE/Plugins/HeartOfMagic_NPCSpells.json`, `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/...`; no `.pdb`, no report.
- [ ] `.\DeployDev.ps1 -NpcSpellsOnly` - expected: the three files in the dev mod folder; then in MO2 enable the mod and tick the ESL.
- [ ] `CLAUDE.md`: in "Deploying to the development mod folder" add `.\DeployDev.ps1 -NpcSpellsOnly   # NPC Spells addon: DLL, ESL, rules JSON -> D:\TAKEALOOK\mods\HeartOfMagic-NPCSpells-Dev`; in the dist paragraph mention the second archive `dist/HeartOfMagic-NPCSpells_<VERSION>.zip` (ESL, DLL, rules JSON, licence files; `plugins/cmake/DistributionNpcSpells.cmake`) and its `_DebugSymbols.zip`.
- [ ] Commit: `git add plugins/cmake/DistributionNpcSpells.cmake CMakeLists.txt DeployDev.ps1 CLAUDE.md; git commit -m "NPC Spells: own release archive and dev deploy switch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

### Task 12: Documentation, spec measurement, in-game checklist

**Files**
- Create: `plugins/NPCSpells/README.md`
- Modify: `CLAUDE.md`, `docs/ARCHITECTURE.md`, `docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md`
- Test: in-game checklist below; `node` not involved; `npcspells-test` passes.

**Steps**
- [ ] `plugins/NPCSpells/README.md` sections: What it does (one paragraph, no FormIDs); Requirements (Heart of Magic with a built tree; enable the ESL); Files (table from spec §2); How lists are made (schools x groups x variants x rungs; the ladder table with chances and the realised expected spells per tier 1.0/1.25/1.5/2.0/2.5 and own-tier share 1.0/.8/.67/.5/.6; "below" lists include the own tier - entry level = tier floor; a Prefer list missing a tier gives the next lower tier's bundle; chunks; one spell per attached list; the engine re-rolls when an actor enters high process); Rules file (`HeartOfMagic_NPCSpells.json`, every key it ships; every record is a `XXXXXX:Plugin.ext` FormID, mod records too - a bare EditorID is rejected and logged; a ref not found in the load order drops that ref only; group names fixed, unknown groups ignored; the ladder must have 5 rungs; fixed values not in the file: skill bands 0/25/50/75/100 (the engine's), own-tier share (realised by the ladder), which groups are necromancers (Vampire, Necromancer), the conjurer's non-undead preference, conjurer classes (from weights), the Redguard rule); Report (`HeartOfMagic_NPCSpells_report.txt` sections, one row per NPC that got lists); Group priority (first match in Forsworn, Vampire, Necromancer, Thalmor, Vigilant; other matched groups' `never` tags are not applied, counted as multiGroup - known limit, lists are prebuilt per group); Known limits (same spell may be given twice if the NPC already knows it - harmless; Vigilant `never: undead` also removes Turn Undead since `undead` covers repelling; undead-rule spells are `undead` + `summoning`/`control`, so an Illusion "command undead" spell is treated as an ordinary Illusion spell; spells another mod's `*_DISTR.ini` names anywhere in the file - also in a filter - are left out, a few needlessly); Development (harness commands, `npcspells-esl`, `DeployDev.ps1 -NpcSpellsOnly`, FormID stability rule: `kMaxChunks` and slot order are frozen, append only).
- [ ] `CLAUDE.md` (the harness rows, "four DLLs", Key Locations, DeployDev and dist lines landed in Tasks 1, 5, 6, 8 and 11): decision tree `└─► Working with the NPC Spells addon? → plugins/NPCSpells/README.md`; All Documentation table row for the README.
- [ ] `docs/ARCHITECTURE.md`: "Game Versions" sentence lists four DLLs; File Structure tree adds `plugins/NPCSpells/` (CMakeLists, data/, src/core, src/game, Main.cpp) and `tools/npcspells-*.cpp`; one line under Deployment Structure for the addon zip.
- [ ] Spec §8 "TAKEALOOK 실측": after the in-game run, fill in NPC count, attached count, lists built, biggest list, and total ms from the report (the spec asks for it), and in §9 move "[미확인] 걸린 시간" to measured. (§4, §5.1, §5.3 and §7 already match what ships; they were updated with this plan.)
- [ ] In-game checklist (TAKEALOOK, dev install, More Informative Console installed). First: enable `HeartOfMagic_NPCSpells.esp` in MO2. Start the game to the main menu, open `HeartOfMagic_NPCSpells.log`: expect "ESL loaded, 921 records, 0 missing, 0 wrong type", pool counts, list count, NPC counts, total ms (< 500 ms expected). Open the report: no `Stopped:`; `droppedOverCapacity 0`. Then load a save and for each NPC below open the console on the actor, MIC base-record tab, Spells: the base must list `FExxx800+` leveled lists from our ESL as described; then press the actor's spell list (MIC shows the actor's spells) and confirm every spell from our lists is of the NPC's school skill tier or lower (compare with the NPC's skills shown by MIC) and matches the group rule:
  1. Bandit mage (`EncBandit01MagicF` etc., class `EncClassBanditWizard`): base has R1 lists for every school, group None; conjuration lists are `NoUndeadAll` or `NecroPrefer`/`All` per its class; no `cloak`/`trap`/`sense` spells.
  2. Thalmor wizard: lists of group Thalmor (`..._Thalmor_...`); no undead spell anywhere, no conjured undead.
  3. Necromancer (Necromancer faction, e.g. a warlock in Rannveig's Fast): `..._Necromancer_...`; conjuration from `NecroPrefer`/`All`; no `sun` spell.
  4. Vampire (e.g. Movarth's lair): `..._Vampire_...`; no `sun`, `light`, `curing` spells.
  5. Redguard warrior (e.g. a Redguard mercenary / Alik'r warrior): no `HoMNS_` list at all.
  6. Redguard mage (non-vampire): lists present; conjuration only `NoUndeadAll`.
  7. Falion (Morthal): conjuration lists `NoUndeadAll`/`All` (conjurer), undead allowed.
  8. Hagraven: lists only if its class is a mage tier (check the report's row for its record; no row = no lists); otherwise none.
  9. Wolf: no lists (beast/animal).
  10. A `LvlBanditWizard...` leveled NPC placed with `player.placeatme`: the spawned actor's base is the end record (`EncBandit0xMagic...`) and shows our lists; the leveled record itself has none.
  11. Non-mage sanity: among ~20 guards/bandits (`player.placeatme` a few `EncBandit01Melee...`), about one has a `..._NonMage_...` list.
  12. Save and reload: the lists are still on the base records (re-added at load), no CTD, no save bloat warning; disable the addon, load the same save: no leftover spells on NPCs you did not meet (the LVSP results are not saved), no missing-form warnings beyond the ESL itself.
  Record the TAKEALOOK numbers in the spec (§8/§9) and the README.
- [ ] Run `.\build\tools\Release\npcspells-test.exe` once more - exit 0.
- [ ] Commit: `git add plugins/NPCSpells/README.md CLAUDE.md docs/ARCHITECTURE.md docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md; git commit -m "NPC Spells: documentation, decision tree and measured numbers" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

---

## Self-review

**Spec coverage (rule -> task):**
- §5.1 only tree spells, one representative per node -> Task 3 (`BuildPool` walks `schools[*].nodes[*].persistentId`; copies are not nodes).
- §5.1 class tiers 0.6/0.3 integer compare -> Task 2. Conjuration class rule (>= 3, not lower than any other) -> Task 2.
- §5.1 creatures: beasts excluded, creatures mage-only, humanoids by tier -> Task 7 (`isBeast`, `isCreature`), Task 9 (keywords, beastRaces).
- §5.1 companions not special -> no code (Task 7 has no companion check).
- §5.1 tiers up to own skill tier -> engine (§5.4); entry level = tier floor -> Task 5 invariant test.
- §5.1 more at higher skill (1.0/1.25/1.5/2.0/2.5), half own tier -> Task 1 ladder + Task 7 expectation test; own-tier share realised 1.0/.8/.67/.5/.6 (spec §5.1 now says "approximately", README in Task 12) - "half" is approximate because every rung yields exactly one spell.
- §5.1 hybrid half, weighted schools only -> Task 7 (`hybridScale`, `SchoolsWeighted`).
- §5.1 non-mage 5% novice curing/armor -> Task 4 non-mage slots, Task 5 membership, Task 7 dice.
- §5.1 magicka: not filtered (§5.4) -> none.
- §5.1 faction prefer 3/4, never always -> Task 4/5 variants, Task 7 75/25 dice.
- §5.1 Redguard rules (a non-mage Redguard never gets the 5% curing/armor list), undead rules, Falion exception -> Task 7 tests.
- §5.1 conjuration necromancer/conjurer split, CombatMageNecro -> Task 4 variants, Task 7 policy.
- §5.1 never remove spells, don't re-give -> nothing removed; re-giving an already-known spell cannot be prevented with LVSPs (README known limit).
- §5.1 exclusions cloak/trap/sense, reject list, other `_DISTR.ini` -> Task 3 (`excludeTags`, `excludeSpells`, `DistributedFn`), Task 8 (`TomeReach::ConfigText` over `*_DISTR.ini` files only; a filter mention over-matches, accepted).
- §5.2 step 0 template spell list skip -> Task 7 (`inheritsSpellList`), Task 9 (`kSpells`).
- §5.4 one school per list, ascending, tier floors, flag 1 for "below", one LVSP = one spell, a slot split into chunks only over 255, group lists, ESL with fixed FormIDs, skip slots beyond the ESL -> Tasks 4, 5, 6, 8, 9.
- §5.3 rules JSON shape (only keys `ParseRules` reads ship; the rest are fixed values in the README), FormIDs for every record, vanilla and mod -> Task 1.
- §4 report: per-NPC rows (persistent id, name, group, reason, list EditorIDs) and counts -> Tasks 9, 10.
- §7 error handling (missing files, bad JSON -> defaults, an unresolved ref drops that ref only, missing spells skipped, empty slots reported) -> Tasks 1, 3, 8, 9, 10.
- §8 offline checks (counts per tier, no higher tier, list invariants, tree-only, Redguard/undead/creature/beast rules, determinism) -> Tasks 3, 5, 7; in-game checklist -> Task 12; TAKEALOOK measurement -> Task 12.
- §2 distribution files and separate zip -> Task 11.

**Spec observations (rules kept as written):** Vigilant `never: undead` also removes Turn Undead because TAGS.md's `undead` covers repelling; undead-rule spells are defined concretely as element `undead` with technique `summoning` or `control` (tested on Raise Zombie, Conjure Boneman, Turn Undead); when an NPC is in two groups the first in built-in order wins and the other group's `never` tags are not applied (counted; a known limit in spec §5.1 and the README); a Prefer list lacking the NPC's tier yields the next lower tier's bundle (flag 0).

**Could not verify (needs the in-game checklist or further disassembly):**
- That the engine resolves each attached LVSP independently (one spell per list) - §5.4's assumption from the school-walk disassembly; the Task 12 checklist (bandit mage spell count vs attached lists) is the pin.
- Whether `TESNPC::CopyFromTemplateForms` has already run at `kDataLoaded` for concrete templates; the chain walk in Task 9 reads the provider record directly, so it is right either way.
- The engine's treatment of a bundle with a single entry, and of `numEntries` when `entries.size()` differs - the DLL sets both consistently.
- `TESSpellList::SpellData::AddLevSpells` on a record the engine has already resolved into a high-process cache before `kDataLoaded` - not possible at that point (no actors yet), so not a concern.
- Exact TAKEALOOK counts (lists, biggest list, ms) - measured in Task 12.

### Critical Files for Implementation
- plugins/NPCSpells/src/core/NpcSpellsSlots.cpp (the slot table and FormID formula the ESL and the DLL share)
- plugins/NPCSpells/src/core/NpcSpellsNpc.cpp (eligibility, group, undead policy, rung/variant/chunk dice)
- plugins/NPCSpells/src/core/NpcSpellsBuild.cpp (membership, sorting, chunking, flags)
- plugins/NPCSpells/src/game/GameNpcs.cpp (TESNPC facts via the template chain, AddLevSpells)
- tools/CMakeLists.txt (npcspells-test, npcspells-esl, the generated ESL)