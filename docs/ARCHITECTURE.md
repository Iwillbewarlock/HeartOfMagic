# Spell Learning System - Architecture

**Purpose:** Concise reference for LLMs to understand system structure and implementation status.

**Version:** 3.0 (Updated February 14, 2026 - Native C++ tree builders, Python eliminated)

---

## System Overview

**What It Does:**
- Scans all spells from loaded plugins
- Generates spell learning trees with prerequisites using native C++ NLP builders (TF-IDF, fuzzy matching)
- Tracks XP-based progression (casting prerequisites, tome study, spell tome hook)
- Displays interactive tree UI (PrismaUI) with configurable hotkey (default F8)
- Progressive revelation system (names/effects/descriptions unlock with XP)
- Discovery Mode: progressive tree reveal based on XP progress
- Difficulty profile system with 6 presets and custom profiles
- **Early Spell Learning:** Spells granted early but nerfed, scaling with progress
- **Progressive Effectiveness:** Runtime spell magnitude scaling via C++ hooks
- **Spell Tome Hook:** Intercepts tome reading to grant XP instead of instant learning
- **FormID Persistence:** Spell trees survive load order changes via plugin-relative IDs
- **Plugin Whitelist:** Per-plugin opt-in/out filtering for spell tree generation
- **BUILD TREE:** Native C++ NLP Classic builder, laid out by the panel's Classic growth mode (the one builder since 2026-09-27; the Tree, Graph, Thematic and Oracle builders and the panel's old JS builds - Simple, Procedural+, Visual-First/SettingsAware - were removed)
- **Edit Mode:** Manual drag-drop and in-tree editing (add/remove nodes, links)

**Game Versions:** one set of DLLs for SE 1.5.97, AE 1.6.x and AE 1.7.x (1.7.99, 1.7.104), built on
CommonLibSSE-NG v10.0.0 (`plugins/external/commonlibsse-ng`, tag `v10.0.0`). All the DLLs
(`SpellLearning`, `DontEatSpellTomes`, `SL_BookXP`)
use `add_commonlibsse_plugin(... USE_ADDRESS_LIBRARY)`
with the default struct compatibility, so their `SKSEPlugin_Version` declares Address Library
independence (`versionIndependence` = AddressLibraryPostAE) and `versionIndependenceEx` = NoStructUse |
AddressLibraryV5 (0x3). NoStructUse is right because the plugin reads no struct whose layout moved without
going through CommonLib's run-time accessors. The one member that moved at 1.6.629, `Actor::addedSpells`, is read
through `GetActorRuntimeData()`, which splits at 1.6.629 (runtime data at 0xE0 before, 0xE8 from 1.6.629 on; so
`addedSpells` is at 0x188 / 0x190) - and 1.7.104's own `Actor::AddSpell` uses `[actor+0x190]` for it. The other
reads are fixed offsets: `ActiveEffect` (`spell` 0x40, `effect` 0x48, `magnitude` 0x78, `caster` 0x34; in the
effectiveness hooks), `ButtonEvent`/`IDEvent` (`eventType`, `device`, `idCode` 0x20; the hotkey sink) and
`IMenu::uiMovie` 0x10 (the container menu, whose target handle comes from an address library ID, not a member).
They are safe on 1.7 because CommonLibSSE-NG 10, which models the 1.7.99 layout changes (the types it versions at
`RUNTIME_SSE_1_7_99`: PlayerCharacter, SkyrimVM, BookMenu, the input handlers, `BSInputEventQueue`, `State` and a few
more), leaves these types fixed-size (`static_assert` 0x90 / 0x30 / 0x30) with no 1.7 variant. `STRUCT_DEPENDENT` would be wrong: the macro sets no StructsPost629 flag, so SKSE on
1.6.629+ would refuse the DLL. AddressLibraryV5 is the flag SKSE asks of an address-library plugin on 1.7,
whose address library is the new format 5 (`versionlib-1-7-*.bin`). The plugin's own game addresses come only
from address library IDs (TESObjectBOOK::Read 17439/17842, the 41 ActiveEffect subclass vtables; all resolve in the
1.7.99 library); the one hand-measured spot is the tome hook's patch site (see SpellTomeHook below and
docs/DEST-IMPROVEMENTS.md).

**Toxicology addon** (`plugins/Toxicology/`, `HeartOfMagic_Toxicology.dll` with `HeartOfMagic_Toxicology.esp`;
released on its own with its own version, `plugins/cmake/DistributionToxicology.cmake`): with the ESL's ability
learned, a poison spell the player casts (a spell tagged `poison` in the Librarian's `spell_catalog.json` with a
hostile effect) applies the player's first favorited poison to the target it hits (`TESHitEvent`), then leaves that target and
poison immune until the poison's actual duration plus max(duration x `immunityMultiplier`, `minImmunitySeconds`) has passed
in game time (memory only, reset on a load), and uses one bottle per `magickaPerDose` Magicka (default 100) spent on poison spells (co-save record `HMTX`). It
reads only the catalog file from Heart of Magic and compiles in no Heart of Magic source besides `EncodingUtils.cpp`. Same CommonLib
setup as the DLLs above (`add_commonlibsse_plugin(... USE_ADDRESS_LIBRARY)`, default struct compatibility); its one
game address is the `ActorMagicCaster` vtable (`RE::VTABLE_ActorMagicCaster[0]`, address library) for the Magicka
count's update hook. Details: plugins/Toxicology/README.md.

**Core Flow:**
```
Scan Spells → Generate Tree (C++ NLP builders) → Validate FormIDs → Display Tree → Track XP → Grant Early (nerfed) → Reveal Details → Master Spells
```

---

## Component Architecture

### 1. **SpellScanner** (`plugins/spelllearning/src/spellscanner/`, `plugins/spelllearning/include/SpellScanner.h`)
Split across: SpellScannerScan.cpp, SpellScannerJson.cpp, SpellScannerFormId.cpp, SpellScannerHelpers.cpp, SpellScannerEncoding.cpp, SpellScannerCopies.cpp
**Status:** ✅ Implemented

**Responsibilities:**
- Enumerate all `SpellItem` forms from data handler
- Extract spell properties (name, school, tier, cost, effects, etc.)
- Generate JSON output for LLM consumption
- Generate LLM prompt with tree-building instructions
- Filter learnable spells (exclude abilities/powers)

**Key Functions:**
- `ScanAllSpells(config)` - Main scan function
- `ScanSpellTomes(config)` - Alternative scan via tomes
- `CollectAllSpells(config)` / `CollectSpellTomes(config)` - The game-data half of those two (every form read; game thread): the scans above are `JsonText::Dump(<this>, 2)`, the panel's scan does that dump on a worker thread (see "Where a scan runs" below)
- `RunScanToFile(mode, preset)` - Scan and write the dump in one call, for callers outside the UI
- `BuildSpellJson(spell, formId, fields)` - The single source of the scan JSON shape (SpellScannerJson.cpp)
- `BuildEffectJson(effect, fields)` - One effect, including MGEF structure when `effectDetails` is on
- `WriteScanOutput(content)` - Write `spell_scan_output.json`, return its path
- `DebugScanDumpEnabled()` - `debug.json` next to it says `writeScanDump: true` (debug installs: the debug symbols archive and `DeployDev.ps1` ship `debug/debug.json`); then the panel's full scan writes the dump too, on its worker thread after the librarian (`UIManager::PrepareScanText`)
- `GetSpellInfoByFormId(formId)` - Lookup spell details
- `GetSystemInstructions()` - LLM output format spec
- `GetPersistentFormId(formId)` - Convert runtime FormID to `PluginName.esp|0x00123456` format (plugin name in UTF-8, see below)
- `ResolvePersistentFormId(persistentId)` - Resolve persistent ID back to runtime FormID
- `ValidateAndFixTree(treeData)` - Give every tree node its FormID in this load order (persistentId first), drop nodes that are no player's spell now, fix the links and locks (see "FormID Validation Flow")
- `IsFormIdValid(formId)` - Check if a FormID resolves to a valid form
- `SpellsTaughtByTomes()` - The spells some tome teaches; the scan marks each spell `taughtByTome` from it
- `IsEquippedInVoiceSlot(spell)` - The spell is equipped in the voice slot (the default object `kVoiceEquip`); the scan marks `voiceSlot` from it and the tome hook hands such a spell to the vanilla path, as it does a power, lesser power or ability (`GetSpellType() != kSpell`, which the scan never keeps; 2026-10-07)
- `LogSpellsOutOfTree(spells, taught, unreachable)` - Logs, by kind, the scanned spells the tree leaves out (SpellScannerCopies.cpp, below)

**The scan keeps every spell; the tree takes what a tome teaches (2026-09-30):** the panel's "Tomes only"
switch was on by default and filtered at tree build time only: Classic's build request kept the scanned
spells a tome teaches, while the scan, the librarian's catalog and the perk adapters saw every spell. When
the switch went, that filter went with it, and about 1,100 spells no tome teaches came into the tree (2,542
spells against 1,428). Now the scan keeps every spell that passes its other filters, so the catalog and the
perk adapters see them all (a perk overhaul's keyword reaches a quest or perk spell too, and an effect shared
with a spell outside the tree is still checked against that spell's tags), and marks facts from the
record on each: `taughtByTome` (from `SpellsTaughtByTomes`), `voiceSlot` (`IsEquippedInVoiceSlot`),
`freeSpell` (`IsFreeSpell`: under 0.5 Magicka to cast - a pet's, mount's or follower's call, a mod's utility
spell, 2026-10-05), `gate` (the spell gate, `SpellScannerGate.cpp` `JudgeSpell`, 2026-10-06: `{band, reasons}`
when the band is not "in". "out" - free, or only dismisses a summon (banish only), or an "out" rule from
`gate/rules.json` (none ships) - leaves the tree, a saved tree's load (`ValidateAndFixTree`) and Spell Research
unless the player includes it again from the blacklist window's "Left out by itself" list (`GateLeavesOut`
with `ReadGateKept`, the panel's `gateReviewed` in config.json); "review" - a summon or reanimation over an
hour, a horse summon, a unique NPC summon, a summon of a non-hostile race (`nonHostileSummon`, the race's
RACE_DATA flags2 NonHostile: mounts and pets, 2026-10-10), a script-only spell under 20 Magicka, a rule from
`gate/rules.json` (plugin masters plus effect archetypes, never spell names; the shipped one is "review") -
stays and is listed in "Spells to look over" (`modules/gateReview.js`: Leave out blacklists it, Keep stores
it in `gateReviewed`)),
`tomeUnreachable` (below, "Tomes nothing hands out") and `duplicateOf` (below, "One spell
from two plugins"). The tree takes a spell a tome teaches, whose tome something hands out, that is not in the
voice slot, costs something to cast, is not left out by the spell gate and is not another plugin's copy of a tree spell: `isTaughtByTome` / `filterTomeSpells`
(proceduralTreeBuilder.js) in Classic's build request, the primed count (`getPrimedSpells`) and the tree
preview's school sizes. A scan from before the marks has no fields and keeps every spell. The scan status's
spell count still counts every scanned spell. Papyrus `RunScan`'s tome scan (`ScanSpellTomes`) is unchanged
and has neither mark.

`LogSpellsOutOfTree` logs what that leaves out, by kind, naming the first five of each
(`SpellScanner: N <kind> stay in the scan, out of the tree: ...`):

- **Voice slot spells**, whether a tome teaches them or not. A player's spells equip in the hands; voice slot
  spells are mods' script and animation spells (11 of Smooth Animation's - ChargeEffect, dodge and shield
  bash effects - and a follower call of CJ03Elroy's on the author's load order). One learned would take the
  shout's place, so the tome hook (`OnSpellTomeReadImpl`) gives such a spell the vanilla handling - learned
  at once, no learning target, early learning, weakening or ISL study. A tome that teaches a power, lesser power
  or ability (a mod's teleport power, for one) gets the same handling: the scan keeps only `kSpell`, so the tree
  never has it, and the progression path would learn it weakened with no way to raise it (2026-10-07).
  A spell with no school (neither its first effect nor its costliest has one of the five: `ProgressionManager::TargetSchoolOf`) is the same case: the tree builder keeps
  only the five schools, it can never be a learning target and casting it earns no XP.
  `SpellEffectivenessHook::CanLearnEarly` (kSpell of one of the five schools) guards every way into early learning - `GrantEarlySpell`,
  `RegisterISLPendingSpell`, `AddXP`'s threshold grant (so the public API and Papyrus cannot weaken a power
  either) - and the co-save load drops powers from the early-learned list and the learning targets, so a save
  from before the fix gets the power back at full strength with its own name.
- **Non-player copies:** the game keeps copies of many spells for NPCs, traps and scripts (TrapFireball01,
  HazardGuardianCircleSpell, Miraak's Lightning Storm, cloak damage spells). They share the real spell's name
  and school but have no half-cost perk, so `DetermineSpellTier` falls back to the effect's minimum skill -
  often 0 - and a Master spell's copy was built into the tree as Novice (a player report: Bane of the Undead
  and Harmony at the roots with the tome filter off). A copy is a spell no tome teaches with no half-cost perk
  when another scanned spell of the same name (ASCII case ignored) and school has one. On the author's load
  order (3546 spells) there are 391, all NPC, trap, hand or cloak-damage copies. The editor id filter
  (`isNonPlayerSpell` in the scan loop) cannot find them: it reads `GetFormEditorID()`, which the engine
  leaves empty for spells, so it never matches (turning it on would also drop Soul Trap on "trap").
- **Free spells:** no tome teaches them and their magicka cost (`CalculateMagickaCost`) is below 0.5: a
  player spell always costs something, while creatures' attacks, followers' calls (Inigo, Val Serano), pets'
  whistles and mods' test or utility spells (SexLab, BowRapidCombo, PhotoMode) cost nothing. About 610 on the
  author's load order.
- **Spells whose tomes nothing hands out** (`tomeUnreachable`, below).
- **The other spells no tome teaches:** quest, perk and script spells.

**One spell from two plugins (1.2.1):** a mod and its add-on can both carry a spell, each record with its own
tome: Saints and Seducers' Conjure Hunger (`EC_SS_ConjureHunger`, Adept) and ECSS Summonable Creatures' record
with the same editor id (Expert). The scan and the tree go by FormID, so both were tree nodes and both tomes
taught a copy (a Nexus report: three Conjure Hungers in the tree and the spellbook). `FindDuplicateSpells`
(SpellScannerDuplicates.cpp) groups the spells the tree could take (a tome teaches it, not in the voice slot,
with a name) by editor id (ASCII case ignored) and school. One record per group stays: the earliest plugin's
in the load order (`TESDataHandler::files` among the loaded files, the TomeReach pattern), unless it is
`tomeUnreachable` and another is not - then the earliest of those. The others get
`"duplicateOf": "<kept formId>"` in the scan, next to `tomeUnreachable`; they stay in the catalog and the perk
adapters, and `isTaughtByTome` leaves them out of the tree and the primed count. The log says
`SpellScanner: N duplicate spells merged into M`, and at debug level one line per group (kept and merged
records with their plugins).

- **Not the name:** a translation gives different spells one name (Korean: Calm `0x04DEE9` and Pacify
  `0x04DEED` are both 진정), so the key is the editor id. On the author's load order (3546 scanned, 1382 tree
  spells) it merges one group, the two Conjure Hungers; 25 names are shared in the tree, each by different
  editor ids. Name, school and an identical effect list (effect form, magnitude, duration, area) were
  measured too and found no pair with different editor ids, so they are not used.
- **Editor ids:** the engine keeps no editor id for spells; they come from po3's Tweaks (the scan dump has
  them). Without it no spell has one and nothing merges.
- **The copy's tome:** reading it reads the kept spell's tome (`SpellTomeHook::OnSpellTomeReadImpl` through
  `TreeSpellForTome`) when the tree in use has the kept spell as a node and not the copy (in general: the
  member that is a node, below): the same prerequisite and skill checks, learning target, tome XP (granted once per
  kept spell) and early learning for the kept spell, and ISL is handed the kept spell, so the copy never goes
  into the spellbook through the mod. The copy's tome in the inventory counts for the kept spell's inventory
  boost (`PlayerHasSpellTome`). In vanilla mode (the tome hook off, or the progression system off, with no
  ISL) a tome teaches what it names, copies included.
- **The tree in use decides:** the groups come from the spells loaded now, so a plugin added after the tree
  was built can name a record the tree does not hold. `TreeSpellForTome` asks the tree first
  (`ProgressionManager::IsTreeSpell`: every node the panel sent with `SetTreePrerequisites`, roots too,
  cleared with the tree): a node's own tome always teaches that node, and another group member's tome reads
  as the tome of the one member that is a node - the kept spell, or the copy a tree built before the marks
  (or edited by hand) holds. With no member a node, or two, the tome teaches its own spell. The groups do not
  depend on the player: the voice slot check reads the record's equip slot, not what is equipped. Until the panel sends a tree no spell is a node, so every tome reads
  as itself. Game thread (the node set has no lock, like the rest of ProgressionManager).
- **When the groups are worked out:** `PrepareTreeSpellForTome`, on the game thread once the tome reach pass
  is done (Main.cpp: a thread waits with `TomeReach::WaitUntilDone`, then queues it), so a cast's tome
  inventory check (`PlayerHasSpellTome`) never walks every form. A tree node's tome costs one set lookup. A
  tome read before then works them out on that call, and again once the pass is done.
- **The tome scan** (Papyrus `RunScan "tomes"`, `ScanSpellTomes`) marks `duplicateOf` the same way
  (`MarkDuplicateSpells`).
- **Saves and trees from before:** a tree built from an older scan still holds both nodes until it is built
  again; both are nodes, so each one's tome teaches that one. Copies a save already learned stay in the
  spellbook; nothing is removed.

**Tomes nothing hands out (2026-09-30):** a tome record can exist that no player ever finds: no copy is placed,
sold, dropped, crafted or given (a mod's cut or unfinished content, a patch that is not installed). The spell
it teaches would be a tree node nobody can learn. `src/tomereach/` finds them from the plugin files, since in
game the placed references of cells that are not loaded are not in memory:

- `TomeReach::StartAfterDataLoaded` (Main.cpp, kDataLoaded, game thread) takes the loaded plugins in load
  order (`TESDataHandler::files`, with each file's light flag) and every tome (`TESObjectBOOK` that teaches a
  spell, keyed by plugin name and local id), then hands them to a detached worker thread; no RE object
  crosses over.
- The worker (`FindReachedTomes`, TomeReachPass.cpp) reads every plugin (`ScanPlugin`, TomeReachRecords.cpp;
  compressed records through zlib). A tome counts as handed out when a record refers to it: a placed
  reference (`REFR` `NAME`), a leveled list (`LVLI` `LVLO`), a container, NPC or quest inventory (`CNTO`), a
  form list (`FLST` `LNAM`), a recipe (`COBJ` `CNAM`), a quest alias's created, forced or unique item
  (`ALCO`, `ALFR`, `ALUA`) or a script property (`VMAD`). Aliases and script properties are read loosely,
  every four bytes as a possible form id: a false hit only keeps a tome. Form ids resolve through each
  plugin's own master list, 12 bits for a light plugin. On the author's load order 55 tomes are reached only
  this way; every one of them is named by its own mod (a Creation Club pet quest's summon tome, Inigo's,
  spell packs whose scripts put their tomes on vendors), so the loose reading is kept (`tome-reach-test -v`
  lists them).
- A tome no record names can still come from an SKSE distribution framework: the worker reads the configs
  under `Data/` and `Data/SKSE/Plugins/` (`IsDistributionConfig`, TomeReachConfig.cpp: SPID `_DISTR.ini`, KID,
  FLM, BOS `_SWAP.ini`, CID, CDF, LLI, SkyPatcher and LLOS folders) and counts a tome they name by editor id
  (from the BOOK record) or as plugin and id (`0x800~Plugin.esp`, `Plugin.esp|0x800`). Only these files:
  inventory layout lists (GridInventory) and icon lists name every book.
- A debug tome counts as unreached wherever it is placed (2026-10-09): a BOOK whose winning editor id
  contains `debug` (case ignored). Mods keep these in a test cell reached only by `coc`: Sa'chil's
  `SU04SpellTomeDebug` ("사'칠 배치", which moves the follower home) sits in the `SachilDebug` cell. On the
  author's load order 3 BOOK records match and all 3 are debug books; the pass leaves out 1 tome
  (`byDebug`, in the `TomeReach:` log line and `tome-reach-test`'s first line). Spell editor ids are not
  used: the engine keeps none for spells, while the pass reads the BOOK's from the plugin file. Limit: it
  goes by the name, so a real tome whose editor id happens to contain `debug` is left out too; its spell
  stays in the tree only if another reached tome teaches it.
- A spell is `tomeUnreachable` when every tome that teaches it is unreached. The scan marks it
  (`UnreachableSpells`), and the tree filter (`isTaughtByTome`) leaves it out; it stays in the catalog and
  the perk adapters.
- A damaged plugin (a record or subrecord past its end, a compressed record that does not inflate, a file
  that ends inside a record header) was read only in part, and it could have named any form of itself or its
  masters in the rest: the tomes of those plugins count as handed out, the others keep their answer, and the
  log names the plugin. A plugin that cannot be opened, is not a plugin, or whose header (its master list) is
  cut leaves no spell marked: its form ids cannot be resolved.
- The pass takes a few seconds after the game starts. A full scan from the panel asked for in that time
  waits for it on its own thread, up to 30 seconds (`WaitUntilDone`, UIManagerScanner.cpp; the game goes
  on), then reads the forms on the game thread as always; if the pass is still not done, no spell is marked and the log
  says so. A tome scan does not wait for the pass (it does not use it) - but one asked for while a full scan
  runs, including while that one waits here, starts after it ("Where a scan runs"). Papyrus `RunScanToFile` does not wait either:
  a Papyrus full scan in those seconds carries no `tomeUnreachable` marks.
- On the author's load order (3,939 plugins, 0.94 GB) it leaves out 46 spells from 11 mods, among them 24
  summons of a daedra creature pack, Wish Magic's eight wishes and Novice Bolt Spells' three shards; the
  Python measurement it was checked against is `lab/tome-reach/` (`tome_reach.py`, `make_inputs.py`, the
  expected list), and `tools/tome-reach-test` runs the plugin's own pass on the same inputs.
- The file path is the engine's own folder for the plugin (`TESFile::path`), then `Data/<name>`; the log's
  `TomeReach: reading ...` line names the first one, and `TomeReach: N plugins, N tomes ...` the result.
- What it cannot see: a tome an SKSE plugin or a script hands out through `GetFormFromFile` with no
  property, and a config that names a tome by its runtime id (`0xFE000800~Plugin.esl`, a load order
  prefix) instead of its id in the plugin. Such a spell would leave the tree wrongly; none was found on the author's load order (no script
  there calls `GetFormFromFile` naming one of the 11 plugins).

**Spell tier from the half-cost perk (2026-09-30):** `DetermineSpellTier` (SpellScannerHelpers.cpp) asks
the spell's half-cost perk first, then its first effect's minimum skill. The perk step never worked: it
read the perk's name with `GetFormEditorID()`, which the engine leaves empty for perks, so every spell was
tiered by minimum skill, and a Master spell whose effect says 0 was Novice (Bane of the Undead, Summon
Ancient One; on the author's load order 276 of the 2290 spells with a vanilla half-cost perk got a tier
other than the perk's, most of them lower). The 25 vanilla half-cost perks (`AlterationNovice00` ...
`RestorationMaster100`) are now known by FormID - Skyrim.esm is always load index 0, and perk overhauls
edit these records in place - and a mod's own perk is read by name through `GetEditorId` (po3 Tweaks):
only the end of the name counts, a tier word followed by digits or nothing (`...Master100`) or a school followed by
00/25/50/75/100, so a word elsewhere in a name ("SpellmasterAdeptness") or an id merely ending in 00 is not
taken for a tier. Then minimum skill as before. The tier feeds the scan's `skillLevel`, the tree's depth,
the tier XP (`ProgressionManagerXP`) and passive learning's caps, so all of them follow the perk after a
rescan and rebuild; until the tree is rebuilt the panel's `node.level` (and the XP `RequiredXPSync` sends
for a learning target) keeps the old tier. A spell without a half-cost perk still falls back to minimum
skill. The 25 perks live in `include/librarian/VanillaCastingPerks.h`, which the perk adapters also read:
since 2026-10-08 they give a tome spell with no half-cost perk the vanilla one for its tier in memory
(docs/librarian/PERK_ADAPTERS.md, 7-2). Every reader that files a spell by its own record - this tier,
the scan's `castingPerk`, the copies check - asks `PerkAdapters::OriginalCastingPerk`, which hides a perk
the adapters gave.

**Field Config and the MGEF Structure Fields:**

`FieldConfig` decides which optional fields a scan emits. The panel scans with its
`state.fields` defaults (its minimal/balanced/full preset buttons went with `llmApiSettings.js` on
2026-09-28); `FieldsForPreset()` in SpellScannerJson.cpp keeps the three presets for the Papyrus
scan dump.

`effectDetails` (on in the `full` preset) adds the MGEF structure to every entry of
`effects[]`. That structure is the language-independent evidence the tag librarian
classifies on, and vanilla `Magic*` keywords live on the MGEF, not on the SPEL:

```json
"effects": [{
  "name": "Fire Damage", "magnitude": 60, "duration": 0, "area": 0,
  "keywords": ["MagicDamageFire"],
  "archetype": "ValueModifier",
  "primaryAV": "Health", "secondaryAV": "None", "resistance": "FireResist",
  "hostile": true, "detrimental": true,
  "castingType": "Fire and Forget", "delivery": "Aimed",
  "magicSkill": "Destruction",
  "associatedForm": "Skyrim.esm|0x01CB01"
}]
```

**Editor ids.** `editorId` is written for the spell and, with `effectDetails`, for every effect. The
engine only keeps editor ids for a few record types (keywords yes, spells and magic effects no), so
`GetEditorId` asks powerofthree's Tweaks (`Load EditorIDs`) through its exported `GetFormEditorID`.
Without po3 Tweaks the field is empty. Verified in game: 1440 / 1440 spells, 4246 / 4246 effects.

**Two keyword columns, not three.** `keywords` is the raw names as the plugins wrote them (kept
because icon lookup and other mods need the real names). `traits`, on the spell, is the one
normalised column: what the engine values and the base game's own keywords boil down to in a fixed
vocabulary - `element.fire`, `kind.summon`, `kind.undead`, `form.projectile`, `school.destruction` ...
A fire resist value and `MagicSummonFire` both come out as `element.fire`, so Flame Atronach reads
`element.fire` + `kind.summon`. Only keywords whose record a vanilla plugin defines are folded in
(Skyrim, Update, Dawnguard, HearthFires, Dragonborn - judged by the defining plugin,
`IsVanillaKeyword`, not by the name). Effects flagged Hide in UI are not read. `traits` is derived
rather than copied, which is why it has its own name; everything else in the dump stays as recorded.
Built by `BuildSpellTraits` (SpellScannerChips.cpp), the same list the spell card and the icon rules use.

**The librarian's elements replace the scanner's (2026-09-27).** Vanilla keywords only name fire,
frost, shock, poison and disease. Right after a scan, `Librarian::ClassifyScan` builds the tag
catalog (docs/librarian/LIBRARIAN.md) and rewrites every spell's `element.*` entries in `traits` and
`chips` from it, so the tree builder, the bridges and the card see blood, water, air, shadow ... as
the rule files tag them, and a tag a rule removes is gone there too. A tome scan (no effects) takes
the catalog the last full scan left. The held scan (`m_scanText`) and the result sent to the panel
are the merged text. The card icon rules still read the scanner's own traits.

**Perk adapters (2026-09-30).** The catalog's second consumer: `Librarian::PerkAdapters::Apply`
(`LibrarianPerkAdapterPatch.cpp`) adds the keywords perk overhauls look for (`MagicDamageFire`,
`MAG_MagicDamagePoison`, `IMP_K_MagicSummonDaedra` ...) to the effects of catalog spells, per adapter file in
`SKSE/Plugins/SpellLearning/librarian/adapters/`, at `kDataLoaded` and after a full scan (the panel's tree
scan and Papyrus `RunScan`; not the background tome scan). The planner (`LibrarianAdapters.cpp`) is pure and
shared with `librarian-test --adapters`; it builds each spell's JSON with `BuildSpellJson` so it reads the
scan's own fields, writes an effect only when every item using it passes (enchantments, scrolls, potions and
ingredients block), and skips conditioned effect items. A line with `keywordForm` names its keyword by persistent id; before planning, `ResolveKeywordForms` swaps in the editor id the loaded form carries (mods inject one FormID under different names). A `formList` line puts the spells it picks into a
perk mod's own FormList instead (appended to the list's `forms` array in memory, not through `AddForm`,
which is the script path a save keeps). In memory only; each run reconciles with its plan
(what an earlier run added and the plan no longer wants comes off, only new pairs go on; a pair a plugin
wrote is never touched). The scan leaves the added keywords out (`PerkAdapters::IsInjected` in `BuildBaseEffectJson`,
the chips and the card icon lookup), so a scan still records the plugins as written. Report:
`perk_adapters_report.json` next to the catalog; switch: `config.json` `perkAdapters.enabled` (default on).
Rules, measurements and what is left out: docs/librarian/PERK_ADAPTERS.md.
The planner's game-side source (`LoadOrderSource`, `LibrarianLoadOrderSource.cpp`) is shared with the
Spell Research registrations below; it indexes the effect users only when a keyword line asks.

**Spell Research (2026-10-03).** The catalog's third consumer, active only with `SpellResearch.esp` loaded:
`Librarian::SpellResearchPatch` (`LibrarianSpellResearchPatch.cpp`) registers catalog spells in Spell Research's
archetype FormLists when its Import ends (a `MessageBoxMenu` closing while `_SR_GlobalIsSetup` is 1 and the tier
lists changed, run as an SKSE task), at `kPostLoadGame` and after a full scan (`OnCatalogChanged`, beside the perk
adapters). Only spells a tome teaches (`SpellScanner::TomesBySpell`), and as in the tree not voice slot spells or
another plugin's copy (`SpellScanner::DuplicateGroupOf`: the kept record only, none of a group SR already holds a
member of); tomeUnreachable spells are registered (SR's research is a way to them). Spells already in an SR tier
list are left as SR's patches filed them. The lists per spell come from `LibrarianSpellResearch.cpp` (pure, shared with
`librarian-test --spellresearch`): tier, school, casting and targets from the catalog fields, elements and
techniques from `librarian/compat/spellresearch.json` planned with the perk adapter planner (formList lines,
`formListsNone`). Entries go in through `BGSListForm::AddForm`, the script path, so they live in the save and
SR's next Import clears them with its own; tome pairs only while a tier's two tome lists have equal counts.
Report: `spell_research_report.json`; switch: `config.json` `spellResearch.enabled` (default on).
docs/librarian/SPELL_RESEARCH.md.

`castByVampires: true` marks a spell a vampire NPC carries - an NPC whose record has the `Vampire`
keyword, or whose race has it or has an editor id holding "vamp" (case-insensitive: vanilla
FalmerFrozenVampRace, CC's HorkerRaceVampire and Vigilant's vampire races lack the keyword; the engine
keeps race editor ids without po3 Tweaks). An NPC that takes its traits from a template is judged by the
template NPCs' races (`CollectSummonedNPCs`, as for summons). The log line gives the count of vampire
races found each way. The spells come through its own or its (vampire) race's spell list and the leveled spell lists in them, plus
spells SPID hands to `Vampire` in any `Data/*_DISTR.ini` (SpellScannerCasters.cpp, read once). The tag
librarian's blood rules use it; it is absent, not false, for every other spell.

The walk over `Data` sees every file of every mod, so no file name may throw there: `path.string()` converts
to the system's ANSI code page on Windows and throws for a name that page cannot hold (a Korean or emoji name
on an English Windows: "No mapping for the Unicode character exists in the target multi-byte code page").
Names go through `PathText::Utf8` (`include/PathText.h`, `u8string`), a file that fails is skipped with a
log line, and the whole vampire read is optional - if it fails the scan goes on without the tag. The read is
retried only at the next game start (it runs once per session); a Data walk that stops early or cannot open
the directory is logged, not fatal.

A scan that throws still ends for the player: each of its three steps (see "Where a scan runs" below) runs inside a
try block and answers a failure with `onScanFailed({"mode":"all"|"tomes","reason":...})` (uiHelpers.js;
`ReportScanFailure` takes the mode from the request's `scanMode`, the same check `CollectScan` uses). A full scan (`all`) enables the Scan
button again (`restoreScanButton`), puts "Scan failed: <reason>" in the scan status bar (`status.scanFailed`, all
13 languages) and replaces edit mode's "Scanning game spells..." wait line with it. The panel disables the button
while a scan runs, so before this a failed scan left it on "Scanning..." for good. The panel's "Tomes only"
toggle and the background tome scan behind it are gone (2026-09-30): the tree itself leaves out what a
player does not learn (the scan keeps every spell and marks `taughtByTome`, `tomeUnreachable`, `voiceSlot`, `freeSpell` and `duplicateOf`), so a
`tomes` failure can only come from elsewhere and is just logged. An older plain
JSON-string payload is read as a full scan's reason. A throw that is not a `std::exception` sends an empty reason,
which the panel shows as `status.scanFailedUnknown` ("Scan failed (unknown error)", keyed so a language switch
relabels it). The scan does everything that can throw (the scan, the classification, allocating the
shared string for `m_scanText` in `StoreAndSendFullScan`) before `updateSpellData`, and moves that string into `m_scanText` after it (no copy); the `onScanStored` call after it catches and logs its own failure, so no
"Scan failed" can follow a scan the panel already has.

**Where a scan runs (2026-10-08, docs/librarian/ToDo.md S-3).** The panel's scan used to be one game thread
task: on the author's load order (9,765 spell forms, 3,776 scanned) the game stood still for about 4.8 s. It is
now three steps (UIManagerScanner.cpp):

| Step | Thread | What | Time before (same load order) |
|------|--------|------|------|
| `CollectScan` | game thread task | `ParseScanConfig`, the status line, `SpellScanner::CollectAllSpells` / `CollectSpellTomes`: every form read (spell and book arrays, effects, perks, the voice slot, the gate, tome reach and duplicate marks) | 0.42 s |
| `PrepareScanText` | worker (`std::thread`, detached) | `JsonText::Dump(output, 2)`, `Librarian::ClassifyScan` (parse, rules, catalog build and write, traits merge), the debug dump file | 3.1 s |
| `FinishScan` | game thread task | `PerkAdapters::Apply` and `SpellResearchPatch::OnCatalogChanged` (full scan only; they look up and write forms), `updateSpellData`, `m_scanText`, `onScanStored` | 0.94 s + 0.36 s send |

The worker reads no game form: it gets the collected `json` by value, and `ClassifyScan` is the code
`librarian-test` links without the game (`tools/CMakeLists.txt`: LibrarianRules, Classify, Catalog, Traits
against stub headers). The only librarian code that touches forms, `LoadOrderSource`, is used by
`PerkAdapters::Apply`, which stays on the game thread. The text is byte for byte the old one: the same calls
in the same order, only the dump moved thread (the timestamp is still taken while collecting).

- **One at a time.** `RequestScan` sets `m_scanRunning` (under `m_scanMutex`); a request while a scan runs is
  kept, the latest of each mode, and `EndScan` starts it when the running one ends, the full scan first (a
  tome scan asked for while a full scan still waits for the tome reach pass waits too). Each scan carries a
  `ScanTicket` (a `shared_ptr` the steps hand on by move); its destructor calls `EndScan`, so the scan ends
  however it ended - sent, failed in any step, or a task SKSE dropped (the dropped task's lambda is destroyed
  with the ticket). `EndScan`, `ReportScanFailure` and `FinishScan` are `noexcept` and catch their own
  failures; a queued scan that cannot start (`LaunchScan`) clears the flag and is logged. The end runs after
  `FinishScan` returns, outside its `try`, so trouble starting the next scan never reads "Scan failed" over
  one already sent. The flag always clears, but a few out-of-memory paths (a dropped task, a throw caught
  only by the worker's or the tome reach thread's last `catch`, a queued scan that cannot start) only log:
  the panel hears no `onScanFailed`, so after a full scan the Scan button can stay greyed.
- **Failures.** A throw in `CollectScan` or `FinishScan` is reported there; one on the worker is posted back
  as a game thread task that calls `ReportScanFailure` (`onScanFailed`). The worker's whole body sits in an
  outer `catch (...)`: a throw while reporting (say `bad_alloc`) is logged, never let out of the thread
  function, where it would end the game. When no thread can be made, the text work runs on the game thread
  as it did before.
- **Save, load or quit during a scan.** The form reads are one task, so a load cannot fall between them. The
  worker touches no game state; besides its own text it writes the catalog file and, through
  `RememberCatalog`, the spell card's catalog elements (`g_cardElements`, under `g_cardMutex`), and with a
  debug install `spell_scan_output.json` (`WriteScanOutput` takes a lock, shared with Papyrus `RunScan` and
  the Save button). `FinishScan` after a load applies the adapters to the loaded session as any scan would. Quitting while the worker runs ends the process with it, as with a tree build; the catalog is
  written beside `spell_catalog.json` and renamed over it, so a cut write leaves the old file.
- **Panel closed during a scan.** The scan finishes; a full scan is still kept in `m_scanText`, and every call
  into the panel goes through `CallView`, which drops it when the view is gone.
- **The catalog file.** The game thread reads `spell_catalog.json` (perk adapters, Spell Research at a save
  load, the first spell card) while the worker may write it, so `BuildAndWriteCatalog` writes
  `spell_catalog.json.part` and renames it over the catalog: a reader gets the old file or the new one. A
  reader holding the file open (`ifstream` does not share delete) or an antivirus scan makes the rename fail:
  it is tried 3 times 25 ms apart (`CATALOG_RENAME_TRIES`, `CATALOG_RENAME_RETRY_DELAY`), then the part file is
  removed, the old file stays and it is logged, and the scan reads the old file back (`BuildAndWriteCatalog`
  returns false): the perk adapters, Spell Research and the next start read the file, so every reader keeps
  the same catalog until a scan writes one (handing the built catalog on in memory is S-3 work). `ClassifyScan` takes a lock, so a Papyrus `RunScan` and the panel's worker never write two catalogs
  over each other. Papyrus `RunScanToFile` runs on the game thread and passes `wait = false`: while the
  worker classifies it does not wait (that would freeze the game for seconds) but merges the catalog file
  there is, as a tome scan does, and leaves the card's catalog to the worker.
- **What still stops the game:** `CollectScan` (0.42 s here) and `FinishScan` (about 1.3 s, mostly the perk
  adapters, whose planner looks up forms as it goes). Next steps are in ToDo S-3.

The way back from text to a path is `PathText::FromUtf8` (`std::u8string`): UTF-8 text from the panel, such as
a preset name in Korean, became a garbled file name or a throw through `path(std::string)`, which reads the
ANSI code page. `UIManagerIO.cpp` (save, delete, list presets) builds its paths with it, and reads names back
with `PathText::Utf8`, so the round trip matches. `SanitizeFilename` only replaces ASCII characters Windows
forbids, so non-ASCII letters stay in the name. The file work itself is `include/uimanager/PresetFiles.h`
(no logging, no game types, so an offline check compiles it as it is): a save writes the preset's own file and
removes nothing; a delete removes that file, or, only when it is missing and the name has a non-ASCII letter,
every file whose `name` inside matches (an older build's garbled file name). It reads each file into memory and
closes it before `remove()`: MSVC opens a stream without `FILE_SHARE_DELETE`, so removing a file still open throws.

**Game text that is not UTF-8 (1.1.0).** The game hands out names, editor ids and plugin file names in the
system's ANSI code page (Windows-1252 on a German or French game, 1251 on a Russian one, CP949 on a Korean one).
`nlohmann::json::dump()` throws `type_error.316` on such bytes, and whatever was being sent stopped. Two layers:

- Text that should read correctly goes through `EncodingUtils::SanitizeToUTF8` (already UTF-8 is kept as it is,
  anything else is converted from `CP_ACP`): spell and effect names, the spell name of `onLearningTargetSet`
  (the tome flow), editor ids (`GetEditorId`, keyword ids, the spell card's `editorId`) and plugin file names
  (`GetPluginName`, the plugin half of `GetPersistentFormId`). `ConvertFromCodePage(text, codePage)` is the
  conversion with an explicit code page (`ConvertToUTF8` is it with `CP_ACP`).
- Every `dump()` whose text leaves the plugin (the panel, a file) is `JsonText::Dump(value, indent)`
  (`include/JsonText.h`), `dump` with `error_handler_t::replace`: a byte that is still not UTF-8 becomes U+FFFD
  instead of a throw. There is no plain `.dump(` left in `plugins/`; keep it that way.

A persistentId's plugin half is the UTF-8 form of the file name. For an ASCII or already-UTF-8 name that is
the same key as before; only a name that was not valid UTF-8 changed, and such a plugin never produced a scan
or a tree (the dump threw), so nothing saved holds the old form. The co-save does not use persistentIds (it
stores FormIDs and resolves them with SKSE's `ResolveFormID`). The key is made and found by
`include/PluginNames.h` (no game types): `MakePersistentId` converts the name, and `PluginNames::Lookup` tries
`TESDataHandler::LookupModByName` with the text first (the game's own bytes, every ASCII name) and, for a name
with a non-ASCII byte that it misses, the converted names of the loaded files that have a non-ASCII byte
(`AsciiText::EqualsIgnoreCase`, A-Z folding only; `include/AsciiText.h` also holds the shared `HasNonAscii`).
A `Lookup` remembers every answer, misses included, and converts the file list at most once;
`ValidateAndFixTree` keeps one for its whole pass over the tree, so a tree full of one Korean-named plugin's
spells costs one walk of the file list, not one per node. The key depends on the ANSI code page: a player who
switches the Windows system locale gets other keys for plugins whose names are not UTF-8.

Reading `config.json` is `ConfigFile::Load` (`include/uimanager/ConfigFile.h`, no game types). A file that was
read but does not parse, or parses to something other than an object, is moved to `config.json.broken`
(`.broken-2`, ... up to 20; `FileUtils::MoveAside`). `config.json.bak` from the last good save is then read the
same way: when it is a JSON object (`LoadResult::fromBackup`) its settings are merged over the defaults and
written as the new `config.json` with `FileUtils::WriteAtomically`; the `.bak` itself stays, because nothing is
at the path for the write to move over it. When the `.bak` is missing or broken too, the new file holds only
the defaults. The legacy `settings.json` is merged only into a first `config.json` (`MergesLegacy`), never over
a broken one. A file that cannot be checked, opened or read (held by an antivirus or sync tool, blocked by its
ACL), or a broken one that cannot be moved, is left untouched and the defaults hold for the session only
(`WritesDefaults` is false); `user_locale.js` is not rewritten from those defaults either (they have no
`language`, and `''` would drop the player's panel language).

Every load also decides how the panel's saves treat the file until the next load (`ConfigFile::SaveModeFor`,
kept in `UIManager::ConfigSaveMode`, set and read under the config file lock). `Blocked` after `Unreadable` or
`BrokenKept`: the save worker writes nothing - the panel sends its whole config, the defaults plus the session's
changes, and merged over the file it would replace every setting in it - logs it and puts "changes are NOT
written" in the scan status bar (`UIManager::UpdateStatus`, from the game thread); the load already put a
message there. Neither text says "saved": `updateStatus` in cppCallbacks.js tests for "saved" before "Error"
and would show them as a success. The save's own check stays behind it: a file that no longer parses at save time is not written
either, with the same message. `NoBackup` after a `MovedAside` with only the defaults: saves write
`config.json` with `keepBackup` false, so the last good `.bak` is not pushed out by a defaults-only file.
`Normal` otherwise. Every load replaces the mode, so a block lasts until a load ends `Loaded`, `Missing` or
`MovedAside`, and `NoBackup` until one ends `Loaded` (from then on the defaults-based file is the one kept as
`.bak`). The panel only asks for its config when it starts, so in practice a block lasts until the next game
start. A blocked save still applies the panel's config to the game (`ApplySavedConfig` with what the panel
sent) - the player's changes work for the session, they are just not written - and skips `WritePanelLocale`.
`hotkeyCode` and `pauseGameOnFocus` are read with `SafeJsonValue` (a wrong type falls back to the default
from `GenerateDefaultConfig`) - a throw there used to skip every setting after it.
`hotkeyCode` 0 is "no hotkey" (Settings > Hotkey > None): no key has scancode 0, so the InputHandler's
`idCode != m_hotkeyCode` never matches it; `SafeJsonValue` returns the 0 (only a missing or null key gets
the default) and the save path's `is_number()` keeps it. With None no key opens the panel (Papyrus
`ToggleMenu` still does); nothing in the config locks None out.
**Risa's All In One Menu** (Nexus 183829, 5.4+): at `kPostLoad`, when `RisaAllInOneMenu.dll` is already loaded,
`RisaMenu::Register` (`src/RisaMenu.cpp`) registers the panel through Risa's C API (`plugins/RisaMenuIntegrationAPI.h`,
MIT, version 1) as `HeartOfMagic.Librarian` - a fixed id Risa keys the player's button and its own hotkey by, so it
never changes. Open/Close post `ShowPanel`/`HidePanel` to the game thread (Open answers false before the panel exists),
IsMenuOpen reads `IsPanelVisible`. While Risa manages the panel it calls `SetHotkeyEnabled(false)` and the
InputHandler skips our hotkey; that flag is runtime only, `config.json` keeps the key. A hotkey change
(`UpdateInputHandlerHotkey`, also at config load) unregisters and registers again so Risa shows the new key label:
at once while the panel is closed. The key is set in the panel's settings, so while the panel is open (perhaps as
Risa's open menu) the re-register waits: `HidePanel` calls `RisaMenu::OnPanelClosed`, which posts it to the game
thread. Risa's Open callback reads `UIManager::IsInitialized` off the game thread, so `m_isInitialized` is atomic,
as `m_isPanelVisible` is. Without Risa, or when Risa refuses, nothing changes. A `magicMenu` object left in a
config.json by a development build is read by nothing and does no harm.

The public API (`SpellLearningAPIImpl` in Main.cpp) runs each call that changes state through `GuardApiCall`:
an exception is logged and the caller gets `0`/`false`, it never crosses into the other plugin's DLL. The
calls in `OnExternalPluginMessage` (the AddXP and RegisterSource messages) are guarded the same way; that
handler is not registered as a listener at present.

`SetupLog` (`plugins/Common.h`, all three DLLs) never throws. CommonLib's `logger::init()` builds its file sink
from `path->string()`, which throws for a Documents path the ANSI code page cannot hold; the plugin then did
not load at all. On that throw the same log file is opened by its wide path (`WideFileSink`,
`plugins/WideFileSink.h`, `_wfsopen`; spdlog here has no `SPDLOG_WCHAR_FILENAMES`), and if that fails too the
plugin runs with the debugger-output sink only.

`archetype` and the actor value fields are always names, never raw numbers -
classification rules match on those strings, so they have to stay stable.
`associatedForm` only appears when the effect has one (summons, bound weapons).

Actor values come from the AVIF record's `enumName`, not `RE::ActorValueToString`. That
helper hands back the localized display name, so a translated load order emits `"체력"`
where an English one emits `"Health"` - and even the English display name
(`"Resist Fire"`) differs from the enum name (`"FireResist"`) the rules are written
against. The rule files ship once for every language, so these keys have to be language
independent.

**Structure evidence (SpellScannerEvidence.cpp):**

`effectDetails` also adds the fields below. Every one of them is copied out of the
record as it is; the scan never interprets a value. Deciding that something is "an
area spell" or "a fire spell" is the consumer's job, which is what keeps the dump the
same on every load order and in every language.

| Where | Field | Source |
|---|---|---|
| spell | `castingPerk` | `SpellItem::Data::castingPerk` - the half cost perk. Only when set |
| spell | `equipSlot`, `twoHanded` | `BGSEquipType::GetEquipSlot()`, `SpellItem::IsTwoHanded()` |
| spell | `castDuration`, `range` | `SpellItem::Data` |
| spell | `flags{}` | `SpellItem::SpellFlag`: costOverride, pcStartSpell, instantCast, ignoreLOSCheck, ignoreResistance, noAbsorb, noDualCastMods |
| spell (tome scan) | `tomePersistentId`, `tomeValue` | the teaching book and its gold value |
| effect | `form` | the effect record's persistent id - the identity; editor ids repeat and need po3 Tweaks |
| effect | `flags{}` | every `EffectSettingData::Flag` except hostile/detrimental, which stay top level |
| effect | `baseCost`, `minimumSkill` | `EffectSettingData` |
| effect | `projectile{form,type,speed,range,gravity,explodes}` | `projectileBase`. `type` is one of Missile, Lobber, Beam, Flame, Cone, Barrier, Arrow. Only when set |
| effect | `explosion{form,source,radius}` | the effect's own explosion, otherwise its projectile's. `source` says which. Only when set |
| effect | `hazard`, `hazardSource` | true/false, and where it was found. See below |
| effect | `perk`, `equipAbility` | `EffectSettingData`. Only when set |
| effect | `applies[]` | what the effect puts on others, one level: `{via, form, effects[]}` for a Cloak's spell (`associatedForm`, `via: cloak`), a hazard's spell (`BGSHazard::data.spell`: the effect's own hazard `hazard`, an explosion's placed hazard `explosionHazard`) and an explosion's enchantment (`explosion`, the effect's and its projectile's, both when both exist). Each applied effect: archetype, primaryAV, resistance, detrimental, hostile, magnitude, duration, flags.hideInUI, keywords. The librarian takes these as evidence, when the effect that sets them off counts itself (a hidden helper's explosion does not). Only when there are any |
| effect | `summonedKeywords` | a SummonCreature effect: the race keywords of the actor it calls up (`associatedForm` NPC, or every NPC of a leveled list, 4 levels deep; an NPC that takes its traits from a template counts as the template's NPCs, since 2026-10-10 - before that Mysticism's Dremora Lord, whose own race is a placeholder FoxRace, read as an animal) - `ActorTypeUndead`, `ActorTypeDaedra`, `ActorTypeAnimal` ... Only when there are any |
| effect | `targetKeywords[]` | `{keyword, has}` from the magic effect record's own conditions that ask `HasKeyword` of the subject (the actor the effect lands on): Turn Undead holds only on `ActorTypeUndead`. Conditions in an OR group, against a global or with another comparison are left out. The librarian's `targetRequires` reads it. Only when there are any |
| effect | `index`, `cost` | slot in the spell record, `Effect::cost` |
| effect | `conditions` | how many conditions this spell puts on the effect item (`Effect::conditions`) - a perk bonus rides as a conditioned item |

`hazard` is presence only - no radius, no lifetime. When it is true, `hazardSource`
says where the hazard hangs, strongest link first:

| `hazardSource` | Meaning | Seen in game (2026-09-21, 4246 effects) |
|---|---|---|
| `effect` | the MGEF's associated form is a hazard | 33 - Blizzard, Circle of Protection, Clairvoyance |
| `explosion` | the effect's explosion, or its projectile's, drops a hazard as placed object | 15 |
| `impact` | an impact data set (the MGEF's, or one of those explosions') carries a hazard | 735 |

The three are not the same kind of thing, which is why the scan names the source
instead of folding them into one flag. `effect` and `explosion` are hazards a spell is
built around. `impact` is whatever a hit leaves on a surface, and the engine uses
hazards for lingering visuals too: in vanilla it fires for Firebolt and Fireball, but
also for atronach summons, Banish and the mass illusion spells. A consumer that means
"leaves something dangerous on the ground" should not read `impact` that way. Runes are
not hazards at all - the rune is a Lobber projectile.

**Scanning from outside the UI:**

`SpellLearning.RunScan(mode, preset)` (Papyrus, see PapyrusAPI.cpp) runs a scan on the
game thread, writes `Data/SKSE/Plugins/SpellLearning/spell_scan_output.json` and returns
the path. `mode` is `"tomes"` or `"all"`, `preset` is `"minimal"`, `"balanced"` or
`"full"`. Unlike the UI's Save button it never routes the dump through the panel, so the
file is exactly the scan JSON.

It stays synchronous (`ScanAllSpells` / `ScanSpellTomes`, the same code the panel's scan runs in three
steps): its contract is the path of a written file. The panel's scan is the one that no longer stops the
game ("Where a scan runs" above). Papyrus calls it from the VM thread, so it submits the scan as a game thread task and
blocks until that task reports back, with a timeout so a dropped task cannot hang the
script forever. It also checks `IsOnGameThread()` (ThreadUtils.h) and runs the scan
inline when the caller is already on the game thread, where submitting a task and waiting
for it could not complete. `MessageHandler` in Main.cpp stamps the thread id, since SKSE
delivers those messages on the game thread.

Known limitation: the wait makes `RunScan` unusable from a caller that itself blocks the
game thread while waiting for the Papyrus result, which is how the DevBench harness
drives it. The scan task then cannot run until `RunScan` gives up, so the call takes the
full timeout and returns an empty string even though the file is written correctly a few
milliseconds later. A non-blocking variant that only queues the scan would avoid this.

**FormID Persistence:**
```
Runtime FormID (e.g. 0x02001234) → "Skyrim.esm|0x001234"
- Top byte = mod index (or 0xFE for ESL)
- ESL: bits [12:23] = light index, bits [0:11] = local ID
- Regular: bits [0:23] = local ID
- On tree load a node must still be a player's spell (`IsPlayerSpell`, SpellScannerFormId.cpp): a SpellItem
  with a name, not free, not in the voice slot. Anything else leaves the tree there and then (logged "not a
  player's spell now"), so a stale formId that names another record, or a spell a newer rule leaves out, never
  shows as an unnamed or code-like node and no rebuild is needed (2026-10-05).
- On tree load, every node with a persistentId takes the FormID it resolves to (since 2026-10-05 also when the
  saved FormID is still a valid form: after a load order change it can be another plugin's record, shown as an
  unnamed "Constant Effect" node; a persistentId that no longer resolves drops the node); `children`,
  `prerequisites`, `hardPrereqs`, `softPrereqs` (all schools, after every school is checked), the school
  `root` and the cross school `bridges` (both ends) follow the new FormIDs, and a bridge to a removed spell goes
```

### 2. **UIManager** (`plugins/spelllearning/src/uimanager/`, `plugins/spelllearning/include/uimanager/UIManager.h`)
Split across: UIManagerCore.cpp, UIManagerNotify.cpp, UIManagerScanner.cpp, UIManagerTree.cpp, UIManagerAutoBuild.cpp, UIManagerPreReqMaster.cpp, UIManagerDeclutter.cpp, UIManagerIO.cpp, UIManagerProgression.cpp, UIManagerConfig.cpp, UIManagerConfigSave.cpp, UIManagerLocale.cpp
**Status:** ✅ Implemented

**Responsibilities:**
- PrismaUI view registration and communication
- Hotkey handling (configurable, default F8; scancode 66)
- C++ ↔ JavaScript bridge
- Panel visibility management
- Unified config load/save (includes early learning settings)

**Key Functions:**
- `Initialize()` - Connect to PrismaUI
- `TogglePanel()` - Show/hide SpellLearningPanel
- `InteropCall(view, function, data)` - Send data to UI
- `OnLoadUnifiedConfig()` / `OnSaveUnifiedConfig()` - Settings persistence
- Various `On*` callback functions for UI interop

**Config save off the game thread** (2026-09-25): the panel saves its settings every time it closes. Reading `config.json`, merging the update into it and writing it back (temp file, move, one `.bak`) used to run as a game-thread task on the frame the game resumes. `OnSaveUnifiedConfig` now queues the text for one background worker (`UIManagerConfigSave.cpp`) that does the file work one save at a time, in the order they came in, so two saves never interleave their writes. The worker then posts `ApplyUnifiedConfig` to the game thread, which applies hotkey, pause, XP settings and `ApplySettingsFromConfig` exactly as before - those setters change state the game thread reads unlocked. The panel language file (`WritePanelLocale`) is written by the worker too. A config load (`LoadUnifiedConfig`) first waits, at most 2 s, for queued saves to reach the disk and holds the same file lock while it reads: a load that ran mid-write would find `config.json` moved aside and write the defaults over it. The worker thread is detached, not joined, because Windows ends it before static destructors run at exit; a save cut off there leaves the previous file in place. The repeated per-save log lines (XP caps, tier XP, each power step) are now debug level.

**PrismaUI View Path:**
```
CreateView("SpellLearning/SpellLearningPanel/index.html", ...)
```
**CRITICAL:** Deploy path must match exactly (project name = HeartOfMagic):
```
MO2/mods/HeartOfMagic_RELEASE/PrismaUI/views/SpellLearning/SpellLearningPanel/
```

### 3. **ProgressionManager** (`plugins/spelllearning/src/progressionmanager/`, `plugins/spelllearning/include/ProgressionManager.h`)
Split across: ProgressionManagerCore.cpp, ProgressionManagerSerialization.cpp, ProgressionManagerAPI.cpp, ProgressionManagerTargets.cpp, ProgressionManagerXP.cpp
**Status:** ✅ Implemented

**Responsibilities:**
- Track per-spell XP progress
- Manage learning targets (one per school in "perSchool" mode, one total in "single" mode)
- Calculate XP gains with multipliers (direct, school, any)
- **Track direct prerequisites per learning target** (from UI)
- XP caps per source type (any: 5%, school: 15%, direct: 50%)
- Progressive revelation thresholds
- Save/load progression via SKSE co-save
- **Early spell granting at unlock threshold**
- **Self-cast XP bonus after threshold**
- **Auto-mastery at 100% progress**

**Key Functions:**
- `SetLearningTarget(school, formId, prereqs)` - Set active target with prerequisites
- `SetTargetPrerequisites(targetId, prereqs)` - Update prerequisites for a target
- `IsDirectPrerequisite(targetId, castId)` - Check if cast spell is direct prereq (the list the UI sent, else the tree's hard/soft prerequisites - a target set from a spell tome comes with no list)
- `AddXP(formId, amount)` - Add XP to spell (triggers early grant/mastery)
- `OnSpellCast(school, castSpellId, baseXP)` - Handle cast event
- `GetProgress(formId)` - Get SpellProgress struct
- `IsSpellAvailableToLearn(formId)` - Check if spell can receive XP
- `ClearLearningTargetForSpell(formId)` - Clear every school whose target is this spell (found by id in the target map, not from the spell's effects) - on mastery, unlock and relock, and when the panel's clear carries the spell's formId
- `ClearLearningTarget(school)` - Clear that school's target, and the same spell under any other school key
- `TargetSchoolOf(spell)` - The one school key a spell is a learning target under: the tree node's school (first effect, `SpellScanner::GetSpellSchool`), which the panel keys by; the costliest effect's school only when the first effect has none. The tome path and the cast's school (`SpellCastHandler`, for the same-school rate) use it too
- `OnGameSaved/OnGameLoaded/OnRevert` - SKSE serialization

**Reverse unlock** (2026-09-23): prerequisites run from lower spells to higher ones, but a spell the
player already knows - a higher spell from a tome, a vendor, another mod - now opens its **direct**
prerequisites (hard, soft and PRM locks alike): they become learnable whatever their own prerequisites,
and cost a share of their XP set per tier of the opened spell - `reverseUnlockXPNovice` ...
`reverseUnlockXPMaster`, defaults 30 / 40 / 50 / 70 / 80%, so an Expert spell opened by a known Master
spell costs 70%. This applies only to spells opened from above; learning upward keeps the tier XP as it
is. "Opened from above" means a mastered spell lists it as a prerequisite, however that spell was
learned: a spell with soft prerequisites (need one of A1/A2/A3), once mastered through A1, opens A2 and A3
at their share too. By default only one step: the prerequisite's
own prerequisites open once it is learned in turn; `reverseUnlockToRoot` opens every spell below it
down to the root at once. One rule, three places that ask it:
- C++ `ProgressionManager::IsUnlockedByKnownChild` - `AreTreePrerequisitesMet` (the mod API, Papyrus,
  ISL; it checks the spell's own prerequisites first and walks up only when they are not met) and the
  spell tome hook's prerequisite check accept it. The walk looks children up in `m_requiredBy`
  (`GetRequiredBy`), the prerequisite links the other way round, kept in step as links are set
  (`SetPrereqRequirements` -> `LinkRequiredBy`) - not a scan of every spell per step. Both survive a
  save load (`ClearAllProgress` leaves them): they are tree data, sent only when a tree loads; `GetRequiredXP` applies the share to a
  spell that has no required XP from the panel yet (a tome read before the spell was ever a target).
- JS `recalculateNodeAvailability` (`cppCallbacks.js`) sets `node.openedByKnownChild` and opens the node.
- JS `getRequiredXPForNode` (`progressionUI.js`) - the one place the panel works out a spell's required
  XP: override, else tier, times `getReverseUnlockXPShare(tier)` when `openedByKnownChild`. C++ has the
  same lookup (`ProgressionManager::GetReverseUnlockXPShare`). C++ keeps the number it was sent with
  the target; `RequiredXPSync` (`modules/requiredXPSync.js`) sends it again through the `SetRequiredXP`
  listener whenever C++ reports another one - after a load (the co-save keeps only the percent, so C++
  starts from tier XP: `GetRequiredXP` with no stored value), or when a known higher spell or a share
  slider changes it mid-session. The Learn button, auto-advance,
  the progress read-out, the spell card and the tree's XP rings all use it. The Learn button used to
  send `node.requiredXP || 100`, and nothing ever set `node.requiredXP`, so every spell started from
  the button was a 100 XP target in C++ whatever its tier.

The spell card says why such a spell is open (`#reverse-unlock-note`). Config: `reverseUnlock` (default
true), `reverseUnlockToRoot` (default false) and `reverseUnlockXPNovice` ... `reverseUnlockXPMaster`
(0.1 - 1.0 in the panel; C++ clamps to 0.01 - 1),
read by both sides (`UIManagerConfig.cpp`, `settingsPanel.js`); `reverseUnlock: false` gives the old
behaviour. In the panel: *Settings > Progression > Known Higher Spells*, right under the XP per tier -
a switch for the rule, one for "down to the root" and a slider per tier for the XP share
(`modules/reverseUnlockSetting.js`, which also saves, loads, resets and puts in settings presets
every key below).

Everything else about learning works downward too:
- **XP gain rates.** With `reverseXpSeparate` on, spells learned downward (C++ `IsUnlockedByKnownChild`)
  gain XP at their own rates - `reverseXpGlobalMultiplier` (x1 - x1000), `reverseXpMultiplierDirect` /
  `School` / `Any` and `reverseXpCapAny` / `School` / `Direct` (percent, like the `xp*` keys; defaults
  the same as theirs). `ProgressionManager::GetGainRates(targetId)` picks the set, for spell casts
  (`OnSpellCast`) and for `AddSourcedXP` (the mod API, passive learning, BookXP), whose modded sources
  take the downward overall multiplier. Off (the default), both directions share the upward rates.
  Turning it on the first time starts the sliders from the upward values.
- **Direct source.** A cast counts as "direct" when the spell cast leads to the target
  (`IsDirectPrerequisite`, which also reads the tree's prerequisites, so targets set from a spell tome
  get it too) or - learning downward, with `reverseUnlock` on - is the spell above it that opened it
  (`IsDirectChild`: the cast spell lists the target as a hard or soft prerequisite).
- **Auto-advance, branch mode.** `_autoAdvanceBranchNext` (`progressionUI.js`) follows the direction
  the mastered spell was learned in: its children when learned upward, its own prerequisites (which it
  opens once mastered) when learned downward, the other direction when that one has nothing
  available. Random mode already picks from every available spell in the school.
- **Passive learning** reads the tier from the spell (`SpellScanner::DetermineSpellTier`) for its
  per-tier cap and its "novice" scope. It used to read it off the required XP, so a spell at a
  reverse-unlock share passed for a lower tier (an Adept spell at 50% = 200 XP took the Apprentice cap).

**XP Source Priority:**
1. **Self-cast** (casting the learning target itself) - 100% multiplier, no cap
2. **Direct prerequisite** (casting a direct prereq of target) - 100% multiplier, 50% cap
3. **Same school** (casting same school, not prereq) - 50% multiplier, 15% cap
4. **Any spell** (other schools) - 10% multiplier, 5% cap

**Data Structures:**
```cpp
struct SpellProgress {
    float progressPercent;  // 0.0 to 1.0
    float requiredXP;       // From tree data
    bool unlocked;          // Only TRUE at 100% mastery!
};

struct XPSettings {
    std::string learningMode;     // "perSchool" or "single"
    float globalMultiplier;       // Overall XP multiplier
    float multiplierDirect;       // Cast direct prerequisite (default 100%)
    float multiplierSchool;       // Cast same school (default 50%)
    float multiplierAny;          // Cast any spell (default 10%)
    float capDirect;              // Max XP from direct prereqs (default 50%)
    float capSchool;              // Max XP from school (default 15%)
    float capAny;                 // Max XP from any (default 5%)
    float xpNovice/Apprentice/Adept/Expert/Master;  // Tier XP requirements
};

// Direct prerequisites tracked per learning target
std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_targetPrerequisites;
```

### 4. **SpellEffectivenessHook** (`plugins/spelllearning/src/spelleffectiveness/`, `plugins/spelllearning/include/SpellEffectivenessHook.h`)
Split across: SpellEffectivenessHookCore.cpp, SpellEffectivenessHookDisplay.cpp, SpellEffectivenessHookLegacy.cpp, SpellEffectivenessHookGrant.cpp
**Status:** ✅ Implemented

**Responsibilities:**
- Hook `ActiveEffect::AdjustForPerks()` for runtime magnitude scaling
- Hook `SpellItem::GetFullName()` for display name modification (optional)
- Track early-learned spells that need nerfing
- **Stepped power system** - Discrete power levels at XP thresholds
- Handle binary effects (Paralysis, Invisibility) with threshold
- Persist early-learned spell list via SKSE co-save
- Display cache for modified spell names/descriptions

**Key Functions:**
- `Install()` / `InstallDisplayHooks()` - Install REL hooks
- `SetSettings(settings)` - Update from unified config
- `SetPowerSteps(steps)` - Configure discrete power steps
- `AddEarlyLearnedSpell(formId)` / `RemoveEarlyLearnedSpell(formId)`
- `IsEarlyLearnedSpell(formId)` - Check if spell is in nerfed state
- `NeedsNerfing(formId)` - Check if magnitude should be scaled
- `GetSteppedEffectiveness(formId)` - Get current power step effectiveness
- `GetCurrentPowerStep(formId)` - Get current step index
- `GetPowerStepLabel(step)` - Get label ("Budding", "Developing", etc.)
- `GrantEarlySpell(spell)` - Add spell to player, mark as early-learned
- `CheckAndRegrantSpell(formId)` - Regrant spell when setting learning target
- `RemoveEarlySpellFromPlayer(formId)` - Remove when switching targets
- `MarkMastered(formId)` - Remove from early-learned list (full power)
- `ApplyEffectivenessScaling(effect)` - Scale magnitude on ActiveEffect
- `GetModifiedSpellName(spell)` - Return "(Learning - X%)" name
- `UpdateSpellDisplayCache(formId, spell)` - Update cached display data
- `OnGameSaved/OnGameLoaded/OnRevert` - Serialization

**Power Step System:**
```cpp
struct PowerStep {
    float progressThreshold;  // XP % to reach this step
    float effectiveness;      // Power multiplier (0.0-1.0)
    std::string label;        // Display name
};

// Default steps:
// 25% XP -> 20% power (Budding)
// 40% XP -> 35% power (Developing)
// 55% XP -> 50% power (Practicing)
// 70% XP -> 65% power (Advancing)
// 85% XP -> 80% power (Refining)
// 100% XP -> 100% power (Mastered)
```

**Settings:**
```cpp
struct EarlyLearningSettings {
    bool enabled = true;
    float unlockThreshold = 20.0f;      // % to grant spell (early access)
    float minEffectiveness = 20.0f;     // Starting power %
    float maxEffectiveness = 70.0f;     // Max before mastery (legacy, now uses steps)
    float selfCastRequiredAt = 67.0f;   // % after which must cast spell itself
    float selfCastXPMultiplier = 1.5f;  // Bonus for casting the spell being learned
    float binaryEffectThreshold = 80.0f;// Binary effects don't work below this %
    bool modifyGameDisplay = true;      // Modify spell names in game menus
};
```

**Hook Targets:**
```cpp
// ActiveEffect::AdjustForPerks - Magnitude scaling
// SpellItem::GetFullName (vtable) - Display name modification
```

### 5. **SpellCastHandler** (`plugins/spelllearning/src/SpellCastHandler.cpp`, `plugins/spelllearning/src/SpellCastImmediateHook.cpp`, `plugins/spelllearning/include/SpellCastHandler.h`)
**Status:** ✅ Implemented

**Responsibilities:**
- Listen to spell cast events (`TESSpellCastEvent`)
- Catch casts that send no such event (2026-10-02): `MagicCaster::CastSpellImmediate` (vtable slot 0x01,
  hooked on `VTABLE_ActorMagicCaster[0]` at kPostLoad; ActorMagicCaster does not override it, so only
  actors' casters pass). SpellHotbar 2 casts every hotbar spell this way and Papyrus `Spell.Cast` does
  too; before, those casts gave no XP. Only the player's casts of a real spell (`kSpell`) the player has
  (`Actor::HasSpell`, so a mod's helper or proc spell does not count) are credited.
  **One cast, one credit (1.2.1).** Each report is stamped when the engine sends it (`ProcessEvent` /
  `OnImmediateCast`), not when the queued task runs. Per spell, `m_pendingCasts` holds the casts credited
  from each way (event, hook) that the other way may still report; a report of the other way within
  `kSameCastWindowSeconds` (0.1 s, either side - tasks run in queue order) takes one of them and earns
  nothing (`IsSecondReport`), so event, event, hook, hook is two credits, not three, and a slow game thread
  no longer opens the window. Only reports of different ways pair; two of the same way never do. The two
  reports of one cast need not come from one thread (the event comes from whichever thread cast the spell);
  the window assumes only that both are sent within a frame or so of the one engine cast, so 0.1 s (3 frames
  at 30 fps) is room for a slow frame while a genuine second cast the other way (SpellHotbar 2's 0.5 s cooldown, a hand cast's
  animation) falls outside it; at 0.3 s it could swallow one. The debug log's "duplicate skipped" line gives
  each pair's measured gap ("0.004s apart, window 0.10s"), to check that assumption in game. Repeats seen the same way still count each (a spell in each hand). Before,
  only the last credit was remembered and its time was the task's.
  A stream of immediate re-casts of one spell less than `kImmediateStreamGapSeconds` (0.45 s) apart -
  a mod refreshing a ward every 0.25 s - counts once, at its start, like a held concentration spell;
  SpellHotbar 2's own casts are at least its 0.5 s cooldown apart and each count. Two different casts of
  one spell under 0.1 s apart, one by hand and one from the hotbar, count once (rare; credited short, never twice).
- **XP log (1.2.1):** every grant is one line in SpellLearning.log, `XP: +<amount> to <spell> (<id>) from
  <path> - <detail>`, path `cast event`, `cast hook (immediate cast ...)`, `tome`, `tome (DEST)`, `study`,
  `book` (BookXP), `source <id>` (other mods, passive) or `raw` (`ProgressionManager::LogXPGrant`). Info
  level, except a cast's per-target lines, which are debug: a cast writes one info line,
  `XP: cast <spell> (<id>) from <path>: +<total> to <n> target(s), <k> duplicate key(s) skipped` (only when
  it earned something; the cast handler runs on every player cast). A report dropped as the other half of a
  credited cast logs `XP: duplicate skipped - ...` at debug level
- **One school per learning target (1.2.1):** the panel keys a target by the node's school (first effect),
  a tome read used the costliest effect's school, so one spell could be the target of two schools and
  `OnSpellCast` paid it twice per cast - or of one school the panel did not look at, so it could neither
  show nor clear it. Now there is one key, `TargetSchoolOf` (the node's school), for the panel, the tome path
  and the cast; `SetLearningTarget` drops the spell from any other school (`DropTargetFromOtherSchools`), a
  loaded save keeps each spell under one school (`CanonicalizeLearningTargets`: all targets collected first,
  then each moved to that key unless a target staying put holds it, in formId order, so two spells saved under
  each other's school swap and none is lost), and `OnSpellCast` credits a target spell at most once per cast.
  A target can therefore stay under an older key, so the panel looks targets up by formId
  (`LearningTargetKeys.schoolOf`), never by `node.school`. Every key dropped or moved is told to the panel
  (`UIManager::NotifyLearningTargetDropped` -> `onLearningTargetDropped`, which redraws the learning paths, the
  wheel marks and the open spell card; a key set by the move -> `onLearningTargetSet`) and fires
  `SpellLearning_TargetChanged` (0 for the key cleared, 1 for the key set), and the panel's clear sends the
  spell's formId too, so the plugin clears it under whatever key holds it
- Identify spell school from cast
- Route to ProgressionManager for XP calculation
- **Throttled notifications** (configurable interval, default 10 seconds)
- **Weakened spell notifications** (configurable on/off)
- Batch XP updates to UI

**Key Functions:**
- `Register()` - Register for SKSE events
- `ProcessEvent(event)` - Handle spell cast
- `SetNotificationInterval()` / `GetNotificationInterval()` - Notification throttling
- `SetWeakenedNotificationsEnabled()` / `GetWeakenedNotificationsEnabled()`
- The alert itself goes through `HudNotify::Show` (below), so XP Alerts off hides it too

**HudNotify** (`include/HudNotify.h`, `src/HudNotify.cpp`): every corner (HUD) message of the plugin. `Show(key,
english, {{name, value}...})` looks the key up in the panel's `lang/<locale>.json` (`hud.*` keys, read once per
language by `Configure`, which `ApplySettingsFromConfig` calls on config load and every save with
`notifications.enabled` and `ResolvePanelLocale` - config `language`, else `lang/locale.js`, else `en`), fills
the `{{name}}` placeholders and shows it, or shows nothing for an XP message (`IsXPAlert`: `hud.castPower*`,
`hud.powerIncreased`, `hud.beginStudy`, `hud.review`, `hud.beginGrasp`, `hud.studiedHour(s)`, `hud.bookXp*`) while
XP Alerts (`notifications.enabled`) is off; every other key always shows (`IsShown`). A missing file or key
falls back to the English text at the call. Papyrus reaches it through `SpellLearning.NotificationText` (with
`SpellLearning.NotificationsOff`, so that a script running with an older SpellLearning.pex, where the native is
missing and the call gives "", still shows its own English text - docs/PLAN-PUBLIC-MODDER-API.md), other
plugins through `ISpellLearningAPI::ShowNotification` (API v2). Safe from any thread.

### 6. **SpellTomeHook** (`plugins/spelllearning/src/SpellTomeHook.cpp`, `plugins/spelllearning/src/SpellTomeHookGate.cpp`, `plugins/spelllearning/src/SpellTomeHookInventory.cpp`, `plugins/spelllearning/include/SpellTomeHook.h`, `plugins/spelllearning/include/SpellTomeHookGate.h`, `plugins/spelllearning/include/SpellTomeHookSites.h`)
**Status:** ✅ Implemented

**Responsibilities:**
- Hook `TESObjectBOOK::ProcessBook` to intercept spell tome reading
- When spell is in learning system: grant XP, set learning target, keep book
- When spell is NOT in system: let vanilla proceed (teach + consume)
- Configurable XP grant per read (default 25% of required)
- **Tome inventory boost** - bonus XP while tome in inventory (25%)
- **Tome inventory cache** (2026-09-25) - the boost is checked once per learning target on every cast, and answering it meant walking the player's whole inventory each time. `SpellTomeHookInventory.cpp` keeps the answer per spell until the inventory changes: a `TESContainerChangedEvent` sink (registered at kDataLoaded) invalidates it when the player is the old or new container and the moved item is a spell tome (or cannot be looked up), and revert and post-load invalidate it too. The event can come from any thread, so the sink only bumps an atomic counter; the cache compares that number on its next lookup and starts over when it moved, and an answer computed while the counter moved is not kept
- Prerequisite checking before allowing tome XP
- Based on "Don't Eat Spell Tomes" pattern by Exit-9B
- **Game versions** - patch site `+0xE8` (SE) / `+0x11D` (AE); tested in game on SE 1.5.97 and AE 1.6.318 /
  1.6.1170, measured offline (not yet tested in game) on AE 1.7.104. `SpellTomeSites::Decide`
  (`include/SpellTomeHookSites.h`, no game types; the image range is passed in) lets the hook in only when
  `mov rcx,[rip+..]; call` is exactly at the site, the call stays inside SkyrimSE.exe's image (base .. base +
  SizeOfImage, read from the PE header by `src/SpellTomeHookGate.cpp`, which resolves the addresses and logs the
  result) and, on AE, the call goes to Actor::AddSpell (`RelocationID(37771, 38716)`), `41 F6 87` (test byte
  [r15+..]) starts at site + 0x56 (a layout-identity check: once patched, +0x173..+0x18F never runs) and `32 C0`
  (xor al, al) at the return site + 0x72; SE keeps the pattern check it was tested with, plus the in-image call.
  A call target outside the image is another SKSE mod's hook on the AddSpell call (`write_call` to its
  trampoline): the hook stays out on every build, tested ones included, rather than NOP over it, and the log says
  so. On the AE builds tested in game (`kInGameTestedAE`: 1.6.318, 1.6.1170) the site pattern and the in-image call
  decide; a failed AddSpell, block-end or return-site check there logs a WARNING with the check and what was read
  (another mod, or a layout that differs from the measured one), and the hook still goes in. Everywhere else a
  failed check leaves tomes the vanilla way and the log names the check that failed (another mod such as Don't Eat
  Spell Tomes got there first, or a different layout - 1.7.99's AddSpell sits elsewhere relative to 1.7.104, so it
  is unknown until its real call target is seen). Versions are logged as 1.6.1170.0, not 1-6-1170-0. Offsets, the
  1.7.104 measurement and why clearing esi keeps the book: docs/DEST-IMPROVEMENTS.md, "Key Offsets Reference"

**Settings:**
```cpp
bool enabled = true;
bool grantXPOnRead = true;
bool autoSetLearningTarget = true;
bool showNotifications = true;        // under Settings > XP Alerts for the study lines (HudNotify)
bool studySkillXP = true;             // ISL patch: a study session raises the school's skill
float xpPercentToGrant = 25.0;        // % of required XP per tome read
float tomeInventoryBoostPercent = 25.0; // Bonus while tome in inventory
```

**Key Functions:**
- `Install()` - Install vtable hook
- `OnSpellTomeRead()` - Internal hook handler
- `GetSettings()` / `SetSettings()` - Configuration

### 7. **ISLIntegration / DEST** (`plugins/spelllearning/src/ISLIntegration.cpp`, `plugins/spelllearning/include/ISLIntegration.h`)
**Status:** ✅ Implemented (Bundled as DEST)

**Note:** Renamed from ISL to DEST (Don't Eat Spell Tomes). DEST is now bundled with
the mod - always available. SpellTomeHook handles the core tome interception in C++.

**Responsibilities:**
- Detect ISL-DESTified mod (multiple plugin name variants)
- Register for OnSpellTomeRead events via Papyrus (`DEST_AliasExt` natives, `DispatchSpellTomeRead`)
- Convert ISL study hours to XP (`OnStudyProgress`): a session grants its share of the hours to master
  (hours studied / hours to master) of the spell's early-learning threshold XP (the unlock threshold %
  of its required XP); there is no XP-per-hour setting. `OnStudyComplete` applies the weakened name and
  descriptions once ISL has taught the spell
- The tome inventory bonus is `SpellTomeHook`'s (see above), not this module's

**Supported Plugin Names:**
```cpp
"DontEatSpellTomes.esp/esl"
"Don't Eat Spell Tomes.esp/esl"
"DEST_ISL.esp/esl"
"ISL-DESTified.esp/esl"
```

**Key Functions:**
- `Initialize()` - Detect DEST/ISL plugins in the load order
- `IsDESTInstalled()` / `IsISLInstalled()` / `IsActive()` - Status checks
- `GetDESTPluginName()` - Return detected plugin name
- `OnSpellTomeRead(book, spell, container)` - Legacy handler (non-ISL DEST setups)
- `RegisterDESTAliasExtFunctions(vm)` / `RegisterPapyrusFunctions(vm)` - Papyrus native bindings
  (`SpellLearning_ISL.OnStudyProgress` / `OnStudyComplete` compute and apply the study XP)

**Papyrus Scripts:**
- `SpellLearning_ISL.psc` - Native function stubs
- `SpellLearning_ISL_Handler.psc` - Event handler on player alias

### 8. **OpenRouterAPI** (removed 2026-09-28)
The LLM (OpenRouter) tree generation, colour suggestions and API settings were removed with
`UIManagerLLM.cpp`, their five listeners (`CheckLLM`, `LLMGenerate`, `PollLLMResponse`,
`LoadLLMConfig`, `SaveLLMConfig`) and the curl dependency. Players could no longer start it from
`index.html`. A saved config's `llm` section (it may hold an API key) is neither read nor written:
merge-saves leave it in the file, and `OnLoadUnifiedConfig` drops it before the config reaches the
panel. `openrouter_config.json` is no longer read or written.

### 9. **TreeNLP** (`plugins/spelllearning/src/treebuilder/TreeNLP.cpp`, `plugins/spelllearning/include/treebuilder/TreeNLP.h`)
**Status:** ✅ Implemented

**Responsibilities:**
- Core NLP algorithms for spell tree generation and PRM scoring
- TF-IDF vectorization with smoothed IDF and pre-computed L2 norms
- Cosine similarity between sparse TF-IDF vectors
- Character n-gram Jaccard similarity (morphological family detection)
- Fuzzy string matching (Levenshtein distance, partial ratio, token set ratio)
- Theme scoring for spell-to-theme assignment
- PRM candidate scoring with proximity blending

**Key Functions:**
- `Tokenize(text)` - Lowercase, strip non-alphanumeric, filter short words
- `BuildSpellText(spell)` / `BuildThemeText(spell)` - Extract weighted text from spell JSON
- `ComputeTfIdf(documents)` - TF-IDF vectorization with L2 norms
- `CosineSimilarity(a, b)` - Dot product of sparse vectors
- `CharNgramSimilarity(a, b)` - 3-char sliding window Jaccard
- `LevenshteinDistance(a, b)` / `FuzzyRatio()` / `FuzzyPartialRatio()` / `FuzzyTokenSetRatio()` - Fuzzy matching
- `CalculateThemeScore(spell, theme)` - Multi-strategy theme scoring (0-100)
- `ScorePRMCandidates(spell, candidates, settings)` - PRM lock candidate scoring
- `ProcessPRMRequest(request)` - Full PRM scoring request handler

**Data Types:**
```cpp
struct SparseVector {
    std::unordered_map<std::string, float> weights;
    float norm = 0.0f;
};
```

### 10. **TreeBuilder** (`plugins/spelllearning/src/treebuilder/`, `plugins/spelllearning/include/treebuilder/TreeBuilder.h`)
Split across: TreeBuilderCore.cpp, TreeBuilderSimilarity.cpp (similarity matrix), TreeBuilderClassic.cpp, TreeBuilderThemes.cpp, TreeBuilderReachability.cpp (unlock simulation and repair), TreeBuilderPlugins.cpp, TreeBuilderBridges.cpp, SimdKernels.cpp
**Status:** ✅ Implemented

**Responsibilities:**
- Spell tree construction engine: the Classic builder (Tree, Graph, Thematic and Oracle removed 2026-09-27, see TREE_BUILDING_SYSTEM.md)
- Theme discovery via TF-IDF keyword extraction
- Theme assignment by best-matching theme (trait themes first, then fuzzy scoring)
- Tree validation (reachability simulation, cycle detection)
- Unreachable node repair (multi-pass)
- Pre-computed pairwise similarity matrices

**Builder:**
| Mode | Function | Algorithm |
|------|----------|-----------|
| Classic | `BuildClassic()` | Tier-first: depth = tier index. NLP within-tier parent selection. |

**High-Level API:**
```cpp
// Called from UIManager::OnProceduralTreeGenerate
BuildResult Build(command, spells, configJson);
// Command: "build_tree_classic" (any other returns success=false, "Unknown build command")
```

**Architecture:**
```
C++ (UIManager)                    TreeBuilder
    │                                   │
    ├─OnProceduralTreeGenerate()─────►│
    │  (SKSE TaskInterface async)       │
    │                                   ├─Build(command, spells, config)
    │                                   │  ├─DiscoverThemesPerSchool()
    │                                   │  ├─ComputeSimilarityMatrix()
    │                                   │  ├─Per-school tree construction
    │                                   │  └─FixUnreachableNodes()
    │                                   │
    │◄─callback(BuildResult)────────────┤
    │  → InteropCall("onProceduralTreeComplete")
```

**Key Data Types:**
```cpp
struct TreeNode {
    std::string formId, name, tier, school, theme, section;
    std::vector<std::string> children, prerequisites;
    int depth = 0;
    bool isRoot = false;
};

struct BuildConfig {
    int seed, maxChildrenPerNode, topThemesPerSchool;
    float density, symmetry, commonThemeShare;
    bool autoFixUnreachable, preferVanillaRoots;
    std::unordered_map<std::string, std::string> selectedRoots;
    std::optional<GridHint> gridHint;
};

struct BuildResult {
    json treeData;       // Full tree JSON
    bool success;
    std::string error;
    float elapsedMs;
};
```

### 10a. **LayoutDeclutter** (`plugins/spelllearning/src/treebuilder/Layout*.cpp`, `plugins/spelllearning/include/treebuilder/LayoutDeclutter.h`)
Split across: LayoutDeclutter.cpp, LayoutLineClear.cpp, LayoutLineClearCost.cpp, LayoutLineGrid.cpp, LayoutMath.cpp (internal types in `LayoutDeclutterInternal.h`)
**Status:** ✅ Implemented

The native twin of the panel's tree declutter pass (`modules/layoutDeclutter.js`, `layoutLineClear.js`,
`layoutLineGrid.js`): spreads a built tree, moves spells off its lines, apart and off the heart, with the
same positions as the JavaScript (fdlibm `sin`/`cos`/`atan2` in `LayoutMath`, every sum in the same order).
`LayoutDeclutter::Run(request, cancel) -> reply` has no RE:: use and keeps all state per call; it throws
`LayoutDeclutter::Cancelled` once its optional cancel flag is set. Its line search leaves out lines past
`kMaxLine` and at spells with more than `kMaxSpellLines`, and stops at a work cap (`kMaxWork`, the same
count in both passes; the reply's `lineWork`, `lineCapped`): a few seconds native for any tree (the cap is checked once per
spell, and the push-apart rounds are not under it); the script's pass is bounded by the same work but
takes many times longer.

```
JS LayoutDeclutter.applyAsync ── callCpp("DeclutterTree", {id, schools, globe, layoutMode, noRotate})
  └─ UIManager::OnDeclutterTree (UIManagerDeclutter.cpp)
       └─ AddTaskToGameThread ─► std::thread (worker, detached, never joined; a newer
          request cancels the one before): parse + LayoutDeclutter::Run
            └─ AddTaskToGameThread ─► CallView("onDeclutterResult", {id, positions, moved, rounds, ...})
                 └─ JS writes x/y onto the nodes, onDone saves the tree
                    (error / unreadable reply / no reply in 30 s: the sliced JavaScript pass; another id: ignored)
JS giving a request up (timeout / unreadable reply / newer applyAsync)
  ── callCpp("DeclutterCancel", id) ─► UIManager::OnDeclutterCancel ─► game thread:
       that request's worker cancelled; if that stopped it ─► onDeclutterResult({id, cancelled: true})
       (nothing applied; ends the panel's 5-minute pause after a timeout)
```

Details, fallback and timings: [TREE_BUILDING_SYSTEM.md](TREE_BUILDING_SYSTEM.md#decluttering-before-save-layoutdeclutterjs-layoutlineclearjs-layoutlinegridjs-2026-09-26).
Offline check: `tools/declutter-test`.

### 11. **PapyrusAPI** (`plugins/spelllearning/src/PapyrusAPI.cpp`, `plugins/spelllearning/include/PapyrusAPI.h`)
**Status:** ✅ Implemented

**Responsibilities:**
- Expose 26 C++ functions to Papyrus scripts for inter-mod communication
- Menu control, XP granting, progress queries, learning target management, settings queries

**Papyrus Functions (26):**
```papyrus
; Menu
SpellLearning.OpenMenu()                              ; Show UI
SpellLearning.CloseMenu()                             ; Hide UI
SpellLearning.ToggleMenu()                            ; Toggle UI
SpellLearning.IsMenuOpen()                            ; Query state
SpellLearning.GetVersion()                            ; Version string

; XP (Modder API)
SpellLearning.RegisterXPSource(sourceId, displayName) ; Register source → creates UI controls
SpellLearning.AddSourcedXP(spell, amount, source)     ; Grant XP through cap system
SpellLearning.AddRawXP(spell, amount)                 ; Bypass all caps/multipliers
SpellLearning.SetSpellXP(spell, xp)                   ; Debug: set exact XP

; Progress Queries
SpellLearning.GetSpellProgress(spell)                 ; 0.0-100.0%
SpellLearning.GetSpellCurrentXP(spell)                ; Raw XP
SpellLearning.GetSpellRequiredXP(spell)               ; Required XP
SpellLearning.IsSpellMastered(spell)                  ; 100% + unlocked
SpellLearning.IsSpellUnlocked(spell)                  ; Granted to player
SpellLearning.IsSpellAvailableToLearn(spell)           ; In tree, prereqs met
SpellLearning.ArePrerequisitesMet(spell)               ; Tree prereqs check

; Learning Target Control
SpellLearning.GetLearningTarget(school)                ; Get target for school
SpellLearning.GetAllLearningTargets()                  ; All active targets
SpellLearning.GetLearningMode()                        ; "perSchool" or "single"
SpellLearning.SetLearningTarget(spell)                 ; Auto-determines school
SpellLearning.SetLearningTargetForSchool(school, spell)
SpellLearning.ClearLearningTarget(school)
SpellLearning.ClearAllLearningTargets()

; Settings Queries
SpellLearning.GetGlobalXPMultiplier()
SpellLearning.GetXPForTier(tier)                       ; "novice", "expert", etc.
SpellLearning.GetSourceCap(source)                     ; Cap % for any source
```

**ModEvents Sent (7):**
- `SpellLearning_MenuOpened` / `SpellLearning_MenuClosed`
- `SpellLearning_XPGained` — (strArg=source, numArg=amount, sender=Spell)
- `SpellLearning_SpellMastered` — (strArg=school, sender=Spell)
- `SpellLearning_SpellEarlyGranted` — (strArg=school, numArg=progress%, sender=Spell)
  (school: the key the spell is a target under, as `TargetChanged` named it - `ProgressionManager::EventSchoolOf`)
- `SpellLearning_TargetChanged` — (strArg=school, numArg=1.0/0.0, sender=Spell)
- `SpellLearning_SourceRegistered` — (strArg=sourceId)

**C++ API (for SKSE plugins):** See `SpellLearningAPI.h` — SKSE Messaging (fire-and-forget) or full `ISpellLearningAPI` interface.

<a id="modder-api-reference"></a>
### Modder API Reference

**For Papyrus modders** who want to add custom XP sources to SpellLearning:

```papyrus
; 1. Register your source on init (creates UI controls for users)
SpellLearning.RegisterXPSource("book_reading", "Book Reading")

; 2. Grant XP when your event fires
Spell[] targets = SpellLearning.GetAllLearningTargets()
int i = 0
while i < targets.Length
    SpellLearning.AddSourcedXP(targets[i], 15.0, "book_reading")
    i += 1
endwhile

; 3. Listen for SpellLearning events (optional)
RegisterForModEvent("SpellLearning_SpellMastered", "OnMastered")
```

**What happens when you register a source:**
1. C++ `ProgressionManager::RegisterModdedXPSource()` creates a `ModdedSourceConfig` (enabled=true, multiplier=100%, cap=25%)
2. `UIManager::NotifyModdedSourceRegistered()` sends JSON to PrismaUI
3. JS `onModdedXPSourceRegistered()` creates UI row with enable toggle + multiplier/cap sliders
4. User can adjust multiplier (0-200%) and cap (0-100%) per source
5. Settings persist via `saveUnifiedConfig()` → `config.json`

**XP flow through `AddSourcedXP()`:**
```
amount → × source multiplier (0-200%) → × global multiplier
       → clamped to: requiredXP × source cap %
       → minus already-tracked XP from this source
       → result added to spell progress
```

**For C++ plugins:** Use `SpellLearningAPI.h` — either SKSE messaging (fire-and-forget) or request the `ISpellLearningAPI` interface pointer.

---

## Performance Optimizations

### Threading Model

The plugin uses a game-thread-primary model with targeted background offloading. All game-thread dispatch goes through `AddTaskToGameThread()` (defined in `ThreadUtils.h`), which provides null-safety, exception handling, and named-task logging. A caller off the game thread that needs an answer back uses `RunOnGameThreadAndWait(name, work, timeout)`, which posts the work, waits up to `timeout` (normally `kPapyrusWait`, 2 s; the answer usually arrives within a frame) and returns `std::optional` - empty when the task was dropped, timed out, or threw. It runs the work inline when already on the game thread, so it cannot deadlock itself.

Note on "game thread": SKSE drains its task queue one task at a time, but not always on the thread that runs the main loop. In game the same queued task was seen running on four different thread ids. What the queue guarantees is order - no two tasks at once - and that is the property the lock-free state here relies on. Event sinks that the engine fires from worker threads (`SpellCastHandler`) therefore post their work into the queue instead of touching that state where they are called.

**Game thread (SKSE main thread):**
- All `RE::` engine calls (form lookups, spell add/remove, HUD messages)
- Event sinks (InputHandler, BookMenuWatcher). `SpellCastHandler` is called on the casting thread and posts its work to the task queue
- Hooks (SpellTomeHook, SpellEffectivenessHook) - both entry points are wrapped in try/catch: they sit above engine machine code with no unwind information, so an exception leaving them would end the process instead of reaching any handler
- Papyrus native functions (PapyrusAPI, ISLIntegration) - **not called there**: the script VM runs natives on its own worker threads. Natives that return nothing post their work with `AddTaskToGameThread()`; natives that return a value use `RunOnGameThreadAndWait()` and hand the script a fallback if the game thread does not answer. `IsMenuOpen` is the exception and reads an atomic flag directly
- SKSE serialization callbacks (co-save read/write)
- UIManager callbacks dispatch to game thread via `AddTaskToGameThread()`

**Background threads:**
- `PassiveLearningSource` — dedicated `std::thread` polling every 3s, dispatches XP grants back to game thread via `AddTaskToGameThread()`
- `TreeBuilder::Build()` — detached `std::thread` for NLP tree construction (TF-IDF, similarity matrices, Classic tree building). Uses OpenMP for inner-loop parallelism. No `RE::` dependencies. Result dispatched to game thread via `AddTaskToGameThread()`
- `TreeNLP::ProcessPRMRequest()` — detached `std::thread` for prerequisite-master scoring. No `RE::` dependencies. Result dispatched to game thread via `AddTaskToGameThread()`
- Config save worker (`UIManagerConfigSave.cpp`) — one detached `std::thread`, started on the first save, that reads, merges and writes `config.json` for queued saves in order and posts the settings back to the game thread via `AddTaskToGameThread()` (2026-09-25)

**Synchronization primitives:**
- `SpellEffectivenessHook` — `std::shared_mutex` (reader-writer) for hot-path spell data
- `SpellTomeHook` — `std::mutex` for tome XP tracking set; a second `std::mutex` plus an `std::atomic` generation counter for the tome inventory cache (the container event sink and a tree load or clear only touch the counter)
- `PassiveLearningSource` — `std::mutex` for settings, `std::atomic<bool>` for lifecycle
- `UIManager` — `std::atomic<bool>` guards for concurrent build/score prevention; `m_isPanelVisible` is atomic because Papyrus reads it off the game thread. Every call into the panel goes through `CallView()`, which drops the call with a warning when the PrismaUI view is gone instead of dereferencing it
- `ProgressionManager` — no mutex (game-thread-only invariant, documented in header)

### Logging (2026-09-25)

CommonLib's logger flushes on every info line (`flush_on(info)`), which made each info line a synchronous disk write on the thread that logged it - usually the game thread. `SetupLog()` (`plugins/Common.h`, shared by all three DLLs) now sets `spdlog::flush_on(warn)` (`kLogFlushImmediateLevel`). Warnings and errors still reach the file before the call returns, together with everything buffered before them. Info and lower lines wait in the file buffer until then, until `FlushLog()` runs (SpellLearning.dll calls it after every SKSE message - data loaded, new game, game loaded - and after the co-save is written) or until the buffer fills. So `SpellLearning.log` can be a few kilobytes behind while playing, and if the game crashes the last info lines may be missing - the warnings and errors are not. There is deliberately no flusher thread (`spdlog::flush_every`): its destructor runs when the DLL unloads and can hang the game's exit if Windows stopped the thread in the middle of a flush.

### Tree load (2026-09-25)
- `GetSpellInfoBatch` keeps each spell's info as JSON (`SpellScanner::GetSpellInfoJsonByFormId`) instead of building text and parsing it back once per spell; `GetSpellInfoByFormId` is the serialized wrapper for the single-spell path
- `ProgressionManager::LinkRequiredBy` checks a child list before adding to it instead of building a set on every call
- `GetPlayerKnownSpells` formats ids with `std::format` and logs each spell at trace level only

### Effect hook and XP notifications (2026-09-26)
- The effect hook (`ApplyEffectivenessScalingFast`, every effect of every player spell) reads the progress
  with `ProgressionManager::GetProgressPercent` (no copy of `SpellProgress`, whose modded-source map
  allocates) and takes one shared lock for the early-learned check, the power step and the binary threshold
  (`SpellEffectivenessHook::ScalingFor`); it took the lock four or five times and copied the progress twice.
  `GetCurrentPowerStep` reads the progress the same way.
- `UIManager::NotifyProgressUpdate` (every XP gain) returns for a hidden panel before anything else, and its
  "PrismaUI not valid" warning - written to disk at once - is logged once, not once per cast.
- `PapyrusAPI` `AddSourcedXP`/`AddRawXP` log at debug (another mod may call them on every hit).

### C++ Plugin Performance (Feb 2026)
- **`std::shared_mutex`** for read-heavy concurrent access (replaces `std::mutex`)
  - Read operations use `std::shared_lock` (non-blocking concurrent reads)
  - Write operations use `std::unique_lock` (exclusive access)
- **Cached player pointer** in AdjustForPerks hook (avoids `GetSingleton()` per-effect)
- **`std::call_once`** for thread-safe one-time debug logging
- **Static regex** compilation for description parsing
- **Early exit** in hot path for NPC casters (most common case)

---

## PrismaUI Frontend

### SpellLearningPanel (`PrismaUI/views/SpellLearning/SpellLearningPanel/`)

**Architecture:** Modular JavaScript (39 modules). See `index.html` for exact load order.

**Core Files:**
- `index.html` - UI structure, module load order
- `styles-skyrim.css` - Styling (Skyrim Edge, the one UI theme); designs lay `themes/design-*.css` over it
- `panel-chrome.css` - Chrome every design shares, loaded after `patch-ui.css` and before a design's sheet: the `.hom-icon` slots, line buttons instead of boxes, square switches, settings groups divided by rules (`#contentSettings .settings-block*`). Its rules use the same or low specificity so a design sheet can still restyle them
- `script.js` - Main initialization, tabs, button wiring, early learning helpers

**JavaScript Modules (`modules/`) – key ones:**

| Module | Purpose |
|--------|---------|
| **Configuration** | |
| `constants.js` | Default tree rules (scan export) and colour palette |
| `state.js` | `settings`, `state`, `customProfiles`, `xpOverrides`, `pluginWhitelist` |
| `config.js` | `TREE_CONFIG` layout and visual configuration, hotkey `KEY_CODES` |
| **Tree building (user-facing)** | |
| `classic/*.js`, `treeGrowth.js`, `treeGrowthStatus.js` | **Build Tree:** the Classic growth mode (sends `build_tree_classic`, lays out and saves the result), its orchestrator and the orchestrator's status line |
| `shapeProfiles.js` | 12 shape profiles (organic, explosion, tree, mountain, portals, spiky, radial, cloud, cascade, swords, grid, linear) read by WheelRenderer |
| `proceduralTreeBuilder.js` | Spell blacklist / plugin whitelist filters and `onProceduralTreeComplete` (hands the C++ result to Classic) |
| **Parsers & rendering** | |
| `treeParser.js` | Tree JSON → nodes/edges; validation, cycle detection, auto-fix |
| `orphanRepair.js` | Unreachable spells and missing prerequisite links in the loaded tree; the orphan repair button |
| `spellNames.js` | Names in the spell card's lists (`spellDisplayName`): the plugin after a name two spells share |
| `wheelRenderer.js` | Main 2D radial wheel rendering |
| `canvasRendererV2.js` | Canvas 2D tree renderer (`CanvasRenderer`); its methods are split over `canvasRenderer*.js` (colours, input, selection, data, frame and tree layer, moving parts, dividers, edges, nodes, a single spell, labels, learning paths), see `modules/README.md` |
| `editMode.js` | Tree editing (add/remove nodes, modify links) |
| **UI & callbacks** | |
| `settingsPanel.js` | Settings UI, config persistence, plugin whitelist modal |
| `treeViewerUI.js` | Tree viewer setup, node lookup, Import dialog (split 2026-09-30: `treeLoad.js` loading, `spellCardView.js` the card, `spellProgressView.js` its progress part, `findSpell.js` Find Spell) |
| `progressionUI.js` | How-to-Learn panel, learning status badges |
| `cppCallbacks.js` | C++ ↔ JS (e.g. ProceduralTreeGenerate, GetProgress); enables Complex/Simple buttons when spells loaded |
| `buttonHandlers.js` | Button click routing and UI state management |
| **Utilities & effects** | |
| `spellCache.js` | Spell data caching |
| `colorUtils.js` | Color manipulation utilities |
| `colorPicker.js` | Color picker UI component |
| `homIcons.js` | The panel's drawn icons: paints every empty `<span class="hom-icon" data-icon="<id>">` from the active design's icon folder (`icons` in a design preset, else `themes/icons-line/`), 19 ids; `slot()` for icons built in JS, `setLabel()` to change a button's text without losing its icon. Loaded before `uiHelpers.js` |
| `uiHelpers.js` | Shared UI helper functions, `saveTreeToFile` |
| `starfield.js` | Starfield background effect |
| `globe3D.js` | 3D globe visualization (experimental) |
| **Testing & entry** | |
| `unificationTest.js` | Shape profile / WheelRenderer tests (run by `run-tests.js` and `test-runner.html`, not loaded in game) |
| `main.js` | Entry point, initialization |

**Module load order:** See `index.html`. Order is: constants/state/config → shapeProfiles → spellCache/colorUtils/homIcons/uiHelpers → treeParser/orphanRepair/spellNames → wheel/starfield/globe/canvas/editMode → colorPicker/settingsPanel/treeViewerUI/… → treeCore/classic/treeGrowth/treeGrowthStatus → cppCallbacks/buildProgress/proceduralTreeBuilder → prereqMaster/treeAnimation → script.js → main.js. (The JS tree builders, generationModeUI, autoTest and the WebGL renderer were removed on 2026-09-27, llmIntegration, llmApiSettings and growthDSL on 2026-09-28; unificationTest is no longer loaded in game.)

**Tabs:**
1. **Spell Scan** - Scan spells, tree building, PreReq Master
2. **Spell Tree** - Interactive radial visualization with zoom/pan/rotate, How-to-Learn panel
3. **Settings** - Difficulty profiles, progression settings, display options, early learning, mod integrations

The Tree Rules tab (a prompt editor) is not in `index.html`; its save path (`SavePrompt`,
`onPromptSaved`) was removed on 2026-09-28. The saved rules (`tree_rules_prompt.txt`) are still sent
with `LoadPrompt` when the panel is ready and go into the scan export's `llmPrompt`. The Save by School,
Copy and Paste buttons of the old scan output (`SaveOutputBySchool`, `CopyToClipboard`,
`onCopyComplete`) went the same day; `GetClipboard` stays for the Import dialog's Paste.

**Key JavaScript Objects:**
- `TREE_CONFIG` - Layout and visual configuration (in `config.js`)
- `settings` - All user settings, persisted (in `state.js`)
- `state` - Runtime state: scan results, tree data, etc. (in `state.js`)
- `WheelRenderer` - SVG tree rendering engine (in `wheelRenderer.js`)
- `TreeParser` - Parse and validate tree JSON (in `treeParser.js`)

**Key Features:**
- Radial spell tree with school-based sectors
- Progressive node states: locked → available → learning → weakened → practicing → mastered
- Discovery Mode: hides locked nodes, shows "???" preview for upcoming spells
- XP-based name reveal: available node names hidden until XP threshold
- Preview nodes appear when parent has ≥20% XP progress
- Tier-based node sizing (novice → master = small → large)
- Collision resolution for dense trees
- Difficulty profiles: Easy, Normal, Hard, Brutal, True Master, Legendary
- Custom profile creation and persistence
- School color customization
- **Configurable divider colors** (school-based or custom)
- Configurable hotkey
- **How-to-Learn slide-out panel** with context-aware guidance
- **Learning status badges** (LOCKED, STUDYING, WEAKENED, PRACTICING, MASTERED)
- **Effectiveness percentage display** for early-learned spells
- **Per-school default shapes** (Destruction=explosion, Restoration=tree, Alteration=mountain, Conjuration=portals, Illusion=organic)
- **Plugin whitelist/blacklist** for controlling which plugins are included in tree generation

---

## Native C++ Tree Builder System

**Purpose:** Native C++ tree generation with the Classic builder (the Tree, Graph, Thematic and Oracle builders were removed on 2026-09-27). All algorithms run directly in the SKSE plugin via `plugins/spelllearning/src/treebuilder/TreeBuilder*.cpp` and `TreeNLP.cpp`. No external subprocess, no IPC, no Python dependency.

### Architecture

```
UIManagerTree.cpp
  └─ OnProceduralTreeGenerate()
       └─ SKSE TaskInterface (async)
            └─ TreeBuilder::Build(command, spells, config)
                 ├─ TreeNLP (TF-IDF, cosine sim, fuzzy matching)
                 ├─ Theme discovery + theme assignment
                 ├─ Per-school tree construction
                 └─ Validation + repair
                      └─ BuildResult → callback → InteropCall("onProceduralTreeComplete")
```

### Builder

| Mode | Command | Algorithm |
|------|---------|-----------|
| Classic | `build_tree_classic` | Tier-first: depth = tier index. NLP within-tier parent selection. |

A request without a command builds Classic; any other command (`build_tree`, `build_tree_graph`,
`build_tree_thematic`, `build_tree_oracle` before 2026-09-27) fails as unknown.

### Core Algorithms (TreeNLP)

| Algorithm | C++ Function | Description |
|-----------|-------------|-------------|
| Tokenization | `Tokenize()` | Lowercase, strip punctuation, filter words ≤ 2 chars |
| TF-IDF vectorization | `ComputeTfIdf()` | Sparse dict vectors, smoothed IDF, L2 norms |
| Cosine similarity | `CosineSimilarity()` | Dot product of sparse vectors / pre-computed norms |
| Char n-gram similarity | `CharNgramSimilarity()` | 3-char sliding window Jaccard (e.g. "Firebolt"/"Fireball" ≈ 0.45) |
| Levenshtein distance | `LevenshteinDistance()` | Single-row DP edit distance |
| Fuzzy matching | `FuzzyRatio()`, `FuzzyPartialRatio()`, `FuzzyTokenSetRatio()` | Replaces Python `thefuzz` library |
| Theme scoring | `CalculateThemeScore()` | Multi-strategy fuzzy scoring (0-100) |
| PRM scoring | `ScorePRMCandidates()`, `ProcessPRMRequest()` | TF-IDF + proximity blending for lock candidates |

### Theme Discovery

TF-IDF keyword extraction per school from spell text (names, descriptions, effects, keywords):
```
For each word across all spells in school:
    TF = term_count / total_tokens
    DF = documents_containing_word
    IDF = log((total_docs + 1) / (DF + 1)) + 1
    score = TF × IDF
Sort descending → take top N → merge with vanilla hints
```

Vanilla hints: Destruction → fire/frost/shock, Restoration → heal/cure/restore, etc.

### Per-School Default Shapes (removed)

Destruction → explosion, Restoration → tree, Alteration → mountain, Conjuration → portals,
Illusion → organic were the defaults of the removed builders (the C++ Tree/Graph/Thematic/Oracle
builders and the JS `SCHOOL_DEFAULT_SHAPES` in `shapeProfiles.js`, gone 2026-09-27). Classic lays every
school out tier-first (`config_used.shape` is `tier_first`).

---

## Data Flow

### Spell Scanning Flow
```
User clicks "Scan" → SpellScanner::ScanAllSpells()
  → Iterate all SpellItem forms
  → Extract properties → Generate JSON + LLM prompt
  → Send to UI → Display in text area
```

### Tree Generation Flow (user-facing modes, outside developer mode)

**BUILD TREE** — the Classic growth mode's build button → `TreeGrowthClassic.buildTree()` (`classic/classicMain.js`):
1. Filters the scan (`filterBlacklistedSpells` / `filterWhitelistedSpells`), sets `state._classicGrowthBuildPending`,
   disables the Build button (`TreeGrowth.setBuilding(true)`; the Easy page's button mirrors it) and calls
   C++ `ProceduralTreeGenerate` with `command: 'build_tree_classic'` (the request is put together and sent
   by `classic/classicBuildRequest.js`, which also lets go of the flag, buttons and modal if it cannot be sent).
2. C++ runs `TreeBuilder::Build(command, spells, config)` on a background thread. Executes native NLP
   algorithms (TF-IDF, fuzzy matching, tree construction).
3. `TreeBuilder` returns `BuildResult` with full tree JSON.
4. Callback fires on the game thread → `onProceduralTreeComplete` (`proceduralTreeBuilder.js`), which hands
   the tree to `TreeGrowthClassic.loadTreeData()` while the pending flag is set and drops any other result;
   the Build button is enabled again on success and failure. A request C++ turned away because a build was
   already running answers `{success: false, busy: true}`, which the callback ignores (the build in flight
   keeps its pending flag and delivers its own result).
5. `ClassicLayout` places the spells; on Apply `ClassicTreeOutput.build()` (`classic/classicTreeOutput.js`)
   turns result and layout into the tree JSON, `LayoutDeclutter` spaces the spells out, then `SaveSpellTree`.

A tree with no spell in it is never saved (1.5.0): every save in the panel - Apply, edit mode's Save Tree,
Import, orphan repair - goes through `SpellTreeSave.send()` (`modules/spellTreeSave.js`), which refuses one,
logs it and says so on the tree tab's status line (`status.treeNoSpellsNotSaved`; Import and edit mode show
their "saved" line only when the tree went out), and Apply says so on the builder's status line
(`buildProgress.statusNothingPlaced`); C++ `SaveSpellTreeFromPanel` (both save listeners) refuses one as well
(`UIManager::CheckTreeHasSpells`, "the tree has no spells" in the log; the check's time is in the
"Saved spell tree to ... (spell check N ms)" line). C++ does not parse the tree for it - the check runs on the
game thread for every save, and the panel checked already: `TreeSaveCheck::HasSpellNodes`
(`include/uimanager/TreeSaveCheck.h`) scans the text for a `"nodes"` key (its quote not escaped) whose array
starts with an object, whitespace allowed; offline check `tree-save-check --self-test`, or
`tree-save-check <tree.json>` against a full parse. A `schools` array counts no spells in the panel. Before, a
build whose layout had no preview grid to place on (`TreePreview.getOutput()` null) dropped every spell in
`ClassicTreeOutput.build` and Apply saved the empty tree over the player's file without a word.

### Tree Built at Game Start (1.5.0)

Settings > Hotkey > *Build Spell Tree at Start* (`autoBuild` in config.json: `"whenMissing"`, the default, or
`"off"`; `modules/autoBuildSetting.js`). With no saved tree the game start does what Scan, Build and Apply
would, in the closed panel (`modules/autoBuild.js`); a saved tree is never rebuilt by itself.

```
onPrismaReady → LoadUnifiedConfig, LoadSpellTree (as before)
  C++ OnLoadSpellTree, at every exit → onStartupTreeInfo { tree: missing | empty | present | unreadable, nodes,
        loadOrder, builtLoadOrder, noticedLoadOrder }                         (UIManagerAutoBuild.cpp)
  config (the value read first in onUnifiedConfigLoaded, noteConfigLoaded in its finally - after the saved
        scan fields) + scanner preset applied (onPresetsLoaded); each waited for 15 s at most
        (AutoBuild.WAIT_MS): no config → nothing is built (the player's Off is in it), no presets → current settings;
        a config.json C++ could not read (Unreadable, BrokenKept: its defaults sent, marked `configNotRead`)
        counts as no config
  → AutoBuild.decide(setting, info, saveLoaded)
      off                                   → nothing
      missing, or empty (no spell in it)    → build (also in game: its steps are then held, see below)
      present, built under another load order, not told yet → notice
      present otherwise (same, unknown, told) / unreadable → nothing
  build:  startScan({ auto }) → updateSpellData → AutoBuild.onSpellData (the first full scan after the run
            asked - `scanAsked`; a tome scan's scanMode "spell_tomes" is passed over)
          → TreePreviewHidden.ensure() (the preview grid without the preview on screen, at the size the preview last measured or 400 x 400
            when its box was never laid out - what TreePreview._render draws at then, TreePreviewUtils.fitCanvas;
            the Sun grid's point cache is dropped after, so the preview on screen works its own out)
          → TreeGrowthClassic.buildTree({ auto }) - BuildProgress silent: no modal, the same stages (PRM locks)
          → BuildProgress.complete → applyTree({ auto }): declutter, SpellTreeSave (SaveBuiltSpellTree),
            loadTreeData (tree tab only while the panel is closed, no 300 ms tab switch), scan status "Tree applied"
  notice: scan status line `status.loadOrderChanged`; C++ keeps the fingerprint in memory and shows
          `hud.loadOrderChanged` in the corner at the next save load (kPostLoadGame), or at once when a save
          has loaded already (`m_saveLoaded`), and only then records it as told (`noticedLoadOrder`)
```

- **When:** at the main menu - onPrismaReady asks for all three inputs - once per game start: only the
  first `onStartupTreeInfo` counts, so a later `LoadSpellTree` (Load Saved Tree on the tree tab, or a
  second onPrismaReady if PrismaUI reloaded the view) never starts a second run. spell_tree.json is one file
  for every save, so a save load or a new game (C++ `OnNewGame` → `onNewGameStarted`, sent only for this)
  does not stop a run, and a decision made in game (Continue pressed fast) still builds - but held, see
  "Held during play". Apply's `loadTreeData` makes the tree live for the loaded save: `SetTreePrerequisites`
  (clears and sets prerequisites only, not progress), then `GetProgress` / `GetPlayerKnownSpells` for that
  save's progress - what a manual Apply after a save load does. The scan may first wait for the tome reach
  pass (up to 30 s) as a Scan button press does; that pass reads the plugin files on a worker thread from
  kDataLoaded and needs no loaded game.
- **The player wins:** a scan that goes out (the Scan button past `startScan`'s 2 s debounce, edit mode's
  spawn list scan), a Build with spells to build, an Apply or Clear with a built tree, and every tree save of
  theirs (Import, edit mode's Save Tree, orphan repair: `SpellTreeSave.send` with another source) end a run
  first, quietly (`AutoBuild.yieldTo`: `yielded - <what>`, no status line); a press that does nothing leaves
  it. A build of the player's found under way when the scan comes back (a build request in C++, a progress
  modal, or a built tree not yet applied) ends it the same way. The run takes the first full scan that comes
  back after it asked: its own, or - scan results carry no request id, and C++ keeps only the latest queued
  full scan - one the panel had sent just before the run started, which is the same scan. The run's scan is
  not debounced (`startScan({ auto })`), and a scan that does not go out, or `startScan` throwing, fails the run
  at once. A run also
  ends by finishing, failing or `RUN_TIMEOUT_MS` (10 min). A step that comes back to an ended run is dropped
  (`_current`), an Apply still decluttering then does not save (`applyTree`'s `isCurrent`), and
  `BuildProgress.start` tells a silent build it replaces ("superseded by another build") before taking its
  callbacks.
- **Held during play:** each step starts work on the game thread - the scan (`CollectAllSpells`, about 1.7 s,
  after the tome reach wait on its own thread; then `FinishScan` with the perk adapters and the large
  InteropCall), the build (the `ProceduralTreeGenerate` request; then the tree reply's InteropCall and the
  lock scoring's request and reply - the build and the scoring themselves run on worker threads), and Apply
  (declutter's request, `SaveSpellTree`'s check and file write, then `SetTreePrerequisites` over the whole
  tree). While a game is played - a save loaded or a new game started (`AutoBuild._saveLoaded`), the panel
  closed - the next step does not start (`_step`): it is held (`held` in the log; the scan status line
  `status.autoBuildHeld`, relabelled on a language switch; C++ shows `hud.autoBuildHeld` in the corner once
  per session) until the panel opens (`onPanelShowing`, the resume a tick later, after the panel's own opening
  work; the panel pauses the game unless "Pause Game on Open" is off) or the main menu opens (C++
  `WatchMainMenu`: a `MenuOpenCloseEvent` sink for `MainMenu` → `onMainMenuShown`, which also ends "in game"),
  whichever comes first (`resumed` in the log). Held: before the scan (a decision made in game), before the
  build (the scan came back in game), before Apply (the build and its locks finished in game). A step already
  started finishes: C++ cannot stop a queued scan, a build or a lock scoring part way, and Apply's save and
  prerequisites go out together. The run's timeout is stopped while a step is held and runs a whole
  `RUN_TIMEOUT_MS` again from the resume. The player's Scan, Build, Apply or Clear while it waits ends the run
  (`yieldTo`) as at any other time; the held step then never runs. "Off" and the no-tree checks are as before.
- **The panel opened during the run's build:** `onPanelShowing` → `AutoBuild.onPanelShowing` →
  `BuildProgress.reveal()`: the silent build's progress modal shows from there on (`shown` in the log). The
  modal (`.modal`, styles-skyrim.css: fixed, the whole view, z-index 99999, with a backdrop) covers the panel,
  so Build, Apply and Clear cannot be pressed under it - the same as for a build from the buttons. Behind it,
  `TreeGrowthClassic.applyTree` and `clearTree` (not the run's own Apply) refuse while `BuildProgress` is
  active (`buildProgress.statusStillBuilding`): saving then would save the tree without its locks. And
  `onPreReqMasterComplete` drops lock scores that `autoApplyLocks` asked for a tree that is no longer
  `TreeGrowthClassic._treeData` (cleared or built again since): no locks, no progress step, a warning in
  the log. The progress is shown at once when the panel is already open as the run starts building. Every
  way out of the lock step ends the build's progress (a failed C++ scoring included), and a run that ends
  in its build step never leaves its progress up (`AutoBuild._dropBuild`): a silent one, or a shown one on a
  yield, is closed; a shown one on a failure (the timeout) is failed with the reason and gets its Close
  button; one that failed itself is left as it says. So neither can hold Apply and Clear back. A build
  request still in C++ then is the run's (a player's Build yields first): `_dropBuild` puts a discard in
  `state._classicGrowthBuildQueued`, so the late result is not loaded, runs no locks and writes no "Tree
  built" over the failure line; a Build of the player's queued meanwhile replaces the discard and goes out
  when that result comes. A lock scoring reply after a run ended in its lock step still lands on the tree it
  was asked for (that tree is built, not saved). **Known limit:** neither `ProceduralTreeGenerate` nor `PreReqMasterScore` replies carry a request
  id. A `busy` reply is told apart only by its flag, and `_prmTree` is one slot: a scoring request turned
  away as busy while another is in flight takes that slot, so the earlier reply is matched against the
  later request's tree. Ids in both replies (C++) would remove this; not reached from the panel today, as
  the buttons are disabled while a build runs.
- **One build in C++ at a time (defensive):** C++ turns a second `ProceduralTreeGenerate` away as `busy` and
  answers the first one later. The Build button is disabled while a build runs, so this is not reached from
  the panel today; should a Build come while one is in flight (`state._classicGrowthBuildPending`), it is
  not sent: it waits in `state._classicGrowthBuildQueued`, and when the earlier result comes back
  `onProceduralTreeComplete` drops it and sends the waiting request. The run never waits behind another
  build (`buildTree({ auto })` returns false).
- **The closed panel does no drawing for the run:** a tree loaded with the panel closed is not drawn
  (`CanvasRenderer.show` only marks a frame as needed, `loadTreeData` skips its `forceRender`), and the
  run's Apply with the panel closed does not ask for progress and known spells (`loadTreeData(..., {
  skipProgressFetch })`). Opening the panel starts the render loop, which draws the marked frame, and asks
  for both (`onPanelShowing`, since `state.treeData` is there), as after a save load.
- **The same tree as the buttons:** the active scanner preset is applied first (the start's own
  `onPresetsLoaded`), the build request, PRM and the save are the panel's own code with `{ auto: true }`.
  Progress is kept by formId in the co-save; the new tree's `requiredXP` reaches C++ at the next
  `LoadSpellTree`, as after a manual Apply.
- **Load order fingerprint:** FNV-1a 64 over the loaded plugin file names in load order (full, then light)
  and the count of SpellItem forms (a mod update that adds spells under the same file name changes it), 16 hex
  digits, worked out once per game start on the game thread. Limit: the count is taken when first asked for
  (LoadSpellTree at the main menu, normally); a mod that creates SpellItems at run time before that can give
  a notice for a load order that did not change. Kept in `SKSE/Plugins/SpellLearning/spell_tree_loadorder.json`
  beside the tree (`loadOrder` - the load order the tree was **built** under -, `plugins`, `spells`,
  `noticedLoadOrder`). Written only when a build's Apply saves (manual or at game start: `SpellTreeSave` sends
  those to the `SaveBuiltSpellTree` listener, the rest to `SaveSpellTree`; both run
  `SaveSpellTreeFromPanel(text, built)`); an edit mode save, an orphan
  repair and an **Import** leave the record as it was - an imported tree's load order is not known, so it keeps
  the notice state of the tree it replaced. A tree built before 1.5.0 has no record: no notice until it is
  built again. The notice is asked for once per session and shown once per load order; a build under the
  load order clears it, also a build under the load order the tree was already built for (the record is left
  alone only when neither field changes). A session that only starts new games (no save load) shows it only on the Scan tab's
  status line: the corner message waits for a kPostLoadGame (nothing shows that a message sent at kNewGame,
  during the loading screen, reaches the screen), and the load order is not recorded as told until it showed.
- **Log:** every step is one line `AutoBuild: <event> - <detail>` in SpellLearning.log (`AutoBuildEvent`,
  written whatever the developer mode; event and detail are cut at 512 bytes, back to the start of a UTF-8
  character, line breaks made spaces):
  `config` (a wait ran out, the config was applied only part way, or config.json could not be read), `presets` (a wait ran out), `decided`,
  `scan`, `build`, `shown`, `apply`, `done`, `failed`, `yielded`, `save loaded`, `new game`, `notice`, `held`,
  `resumed`. C++ adds `UIManager: startup tree info - ...`, `UIManager: spell tree built under
  load order ...` and `AutoBuild: load order notice shown for ...` (or `... not shown` / `... dropped`).
- A failed run says so on the scan status line (`status.autoBuildFailed`) and leaves spell_tree.json as it was.

The panel's older builds - Simple (`buildProceduralTrees`, JS only), Procedural+ and Visual-First
(`visualFirstBuilder` / `settingsAwareTreeBuilder`) - were removed on 2026-09-27; none had a button left
in `index.html`.

The developer-only AUTO AI flow (the LLM built each school over OpenRouter, with self-correction and
a Retry School control) was removed on 2026-09-28; its buttons had already left `index.html`.

### Tree Validation System

**Purpose:** Ensure all spells in an imported tree are reachable (can be learned by the player).

A tree pasted into the Import dialog is checked school by school: `TreeParser.detectAndFixCycles(school, rootId)`
simulates unlocking from the root and reports the spells that can never be unlocked. With
**Aggressive Path Validation** on (Settings > Tree Generation, developer mode; `settings.aggressivePathValidation`)
`treeViewerUI.js` repairs them before the tree is used. Built trees come from C++, which repairs
unreachable nodes itself (`FixUnreachableNodes`).

The LLM self-correction loop, the needs-attention tracking (`state.llmStats`) and the Retry School control
went with the LLM feature on 2026-09-28.

### XP Progression Flow (with Early Learning)
```
Player casts spell → SpellCastHandler::ProcessEvent()
  (or CastSpellImmediate hook → OnImmediateCast(), e.g. SpellHotbar 2; same task, known spells only)
  → Identify spell school → ProgressionManager::OnSpellCast()
  → Calculate XP (direct/school/any multipliers, self-cast bonus)
  → Update progress
  → If progress >= unlockThreshold (default 25%):
      → SpellEffectivenessHook::GrantEarlySpell() - Add spell to player (nerfed)
  → If progress == 100%:
      → SpellEffectivenessHook::MarkMastered() - Full power restored
      → ClearLearningTargetForSpell() - Auto-select next spell
  → Notify UI
```

### Spell Effectiveness Flow (Runtime)
```
Spell cast by player → ActiveEffect created
  → ActiveEffect::AdjustForPerks() called
  → SpellEffectivenessHook intercepts
  → Check if spell is early-learned
  → Calculate effectiveness based on XP progress
  → Scale magnitude (e.g., 20% → 70% → 100%)
  → Binary effects: blocked entirely below threshold
```

### Spell Tome Hook Flow
```
Player reads spell tome → TESObjectBOOK::ProcessBook hooked
  → SpellTomeHook intercepts BEFORE vanilla script
  → Check if spell is in learning tree
  → YES: Grant XP (25% of required), set learning target, keep book, DON'T teach
  → NO: Let vanilla proceed (teaches spell + consumes book)
  → Update UI with progress
```

### FormID Validation Flow (on tree load)
```
Load spell_tree.json → For each node:
  → persistentId field ("Plugin.esp|0x001234")? ResolvePersistentFormId() → the node takes that FormID
    (whether or not the saved one is still a valid form); none resolved → the node goes
  → No persistentId: the saved FormID stays if it is valid
  → Then, after every school: every node's link lists (children, prerequisites, hardPrereqs,
    softPrereqs), its Pre Req Master locks[].nodeId, the school root and the tree's bridges
    follow the new FormIDs; an entry naming a dropped node goes (softNeeded is held to the
    soft list by treeParser.clampSoftNeeded and UIManagerProgression, not here)
  → OnLoadSpellTree sends the fixed tree to the panel whenever a node moved or was dropped
    (resolvedFromPersistent or invalidNodes > 0); the file on disk is not rewritten, so this
    runs again on every load
```
Saved panel settings that name a spell follow the same idea: the blacklist matches by `plugin` + `localFormId`
(`filterBlacklistedSpells`), chosen roots go to the builder with the formId their spell has in the current scan
(`findScanSpellForEntry`, uiHelpers.js; docs/MODULE_CONTRACTS.md "selected_roots") and the root picker puts its
Current badge on that spell (`scanFormIdForEntry`), and `xpOverrides` are keyed
by the node's `persistentId` (`getXPOverride` / `setXPOverride`, uiHelpers.js). An override saved before that,
keyed by the runtime formId, is still read while the spell has none under its persistentId and is replaced by
the persistentId key the next time the player sets it; if the spell's formId moved before then, the old key
cannot be told apart from the spell that took that formId.

### ISL/DEST Integration Flow (Legacy)
```
Player reads spell tome → DEST fires OnSpellTomeRead
  → SpellLearning_DEST_Handler receives event
  → Call native DEST functions
  → ISLIntegration::OnSpellTomeRead()
  → Calculate XP → Grant via ProgressionManager
```

---

## Configuration

### Unified Config (JSON)

All settings stored in single config file, managed through UI:

```json
{
  "hotkey": "F8",
  "hotkeyCode": 66,
  "cheatMode": false,
  "activeProfile": "normal",
  
  "learningMode": "perSchool",
  "xpGlobalMultiplier": 1,
  "xpMultiplierDirect": 100,
  "xpMultiplierSchool": 50,
  "xpMultiplierAny": 10,
  
  "xpNovice": 100,
  "xpApprentice": 200,
  "xpAdept": 400,
  "xpExpert": 800,
  "xpMaster": 1500,

  "reverseUnlock": true,
  "reverseUnlockToRoot": false,
  "reverseUnlockXPNovice": 0.3,
  "reverseUnlockXPApprentice": 0.4,
  "reverseUnlockXPAdept": 0.5,
  "reverseUnlockXPExpert": 0.7,
  "reverseUnlockXPMaster": 0.8,
  "reverseXpSeparate": false,
  "reverseXpGlobalMultiplier": 1,
  "reverseXpMultiplierDirect": 100,
  "reverseXpMultiplierSchool": 50,
  "reverseXpMultiplierAny": 10,
  "reverseXpCapAny": 5,
  "reverseXpCapSchool": 15,
  "reverseXpCapDirect": 50,
  
  "revealName": 10,
  "revealEffects": 25,
  "revealDescription": 50,
  
  "discoveryMode": false,
  "nodeSizeScaling": true,
  
  "earlySpellLearning": {
    "enabled": true,
    "unlockThreshold": 30,
    "minEffectiveness": 20,
    "maxEffectiveness": 70,
    "selfCastRequiredAt": 67,
    "selfCastXPMultiplier": 1.5,
    "binaryEffectThreshold": 50
  },
  
  "dividerColorMode": "school",
  "dividerCustomColor": "#ffffff",

  "autoBuild": "whenMissing",

  "notifications": {
    "weakenedSpellNotifications": true,
    "weakenedSpellInterval": 10,
    "language": "panel"
  },

  "spellBlacklist": [],
  "pluginWhitelist": [],

  "schoolColors": {...},
  "customProfiles": {...}
}
```

---

## File Structure

```
HeartOfMagic/
├── CMakeLists.txt                 # Top-level super-build (shared config)
├── CMakePresets.json              # Build presets (VS2022, VS2026)
├── vcpkg.json                     # Shared vcpkg dependencies
├── BuildRelease.ps1               # Build script
├── plugins/
│   ├── cmake/
│   │   ├── CompilerFlags.cmake    # MSVC optimization flags (/GL, /LTCG, /AVX, etc.)
│   │   ├── commonlibsse.cmake     # CommonLibSSE-NG configuration
│   │   ├── Papyrus.cmake          # Papyrus script compilation
│   │   └── Spriggit.cmake         # Spriggit ESP serialization
│   ├── external/
│   │   └── commonlibsse-ng/       # Git submodule at tag v10.0.0 (built once, shared by all targets)
│   ├── SpellLearningAPI.h         ✅ Public C++ API header (shared across plugins)
│   ├── PrismaUI_API.h             ✅ PrismaUI modder interface (shared across plugins)
│   ├── Common.h                   ✅ PCH, logger alias, SetupLog (never throws; wide-path fallback)
│   ├── WideFileSink.h             ✅ spdlog file sink opened by a wide path (no game types)
│   ├── spelllearning/             # Main SpellLearning plugin
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── AsciiText.h                  ✅ HasNonAscii, A-Z-only case-insensitive compare (no game types)
│   │   │   ├── ISLIntegration.h             ✅ DEST mod integration header
│   │   │   ├── JsonText.h                   ✅ JsonText::Dump - dump that replaces bytes that are not UTF-8 (no game types)
│   │   │   ├── PapyrusAPI.h                 ✅ Papyrus native function header
│   │   │   ├── PassiveLearningSource.h      ✅ Passive learning source header
│   │   │   ├── PluginNames.h                ✅ persistentId plugin names: UTF-8 key, memoised lookup (no game types)
│   │   │   ├── ProgressionManager.h         ✅ XP tracking header
│   │   │   ├── SimdKernels.h                ✅ SIMD kernel header
│   │   │   ├── SpellCastHandler.h           ✅ Spell cast events header
│   │   │   ├── SpellCastXPSource.h          ✅ XP source implementation header
│   │   │   ├── SpellEffectivenessHook.h     ✅ Runtime magnitude scaling header
│   │   │   ├── SpellScanner.h               ✅ Spell enumeration header
│   │   │   ├── SpellTomeHook.h              ✅ Tome interception header
│   │   │   ├── SpellTomeHookGate.h          ✅ Tome hook gate (SiteIsKnownLayout)
│   │   │   ├── SpellTomeHookSites.h         ✅ Tome hook offsets and site checks (no game types)
│   │   │   ├── ThreadUtils.h                ✅ Game-thread dispatch utilities
│   │   │   ├── XPSource.h                   ✅ XP source interface
│   │   │   ├── treebuilder/
│   │   │   │   ├── TreeBuilder.h            ✅ Tree construction engine header
│   │   │   │   ├── TreeBuilderInternal.h    ✅ Internal tree builder helpers
│   │   │   │   ├── LayoutDeclutter.h        ✅ Native tree declutter (Run: request -> reply)
│   │   │   │   ├── LayoutDeclutterInternal.h ✅ Its internal types and constants
│   │   │   │   ├── LayoutMath.h             ✅ fdlibm sin/cos/atan2 (bit-identical with V8's Math)
│   │   │   │   └── TreeNLP.h                ✅ Core NLP header
│   │   │   └── uimanager/
│   │   │       ├── ConfigFile.h             ✅ config.json load: loaded / missing / unreadable / moved aside (no game types)
│   │   │       ├── PresetFiles.h            ✅ Preset file write/delete (no game types; closes a file before removing it)
│   │   │       ├── TreeSaveCheck.h          ✅ SaveSpellTree's no-spells guard: a text scan, no parse (no game types)
│   │   │       ├── UIManager.h              ✅ UI manager header
│   │   │       └── UIManagerInternal.h      ✅ Internal UI manager helpers
│   │   └── src/
│   │       ├── Main.cpp                     ✅ Entry point, event registration, serialization
│   │       ├── SpellCastHandler.cpp         ✅ Spell cast events, notification throttling
│   │       ├── SpellCastXPSource.cpp        ✅ XP source implementation
│   │       ├── SpellTomeHook.cpp            ✅ Tome interception, XP grant, keep book
│   │       ├── SpellTomeHookGate.cpp        ✅ Tome hook site check and its log (image range, AddSpell)
│   │       ├── SpellTomeHookInventory.cpp   ✅ Tome inventory boost and its cache
│   │       ├── PapyrusAPI.cpp               ✅ Papyrus native function bindings
│   │       ├── ISLIntegration.cpp           ✅ DEST mod integration (bundled)
│   │       ├── PassiveLearningSource.cpp    ✅ Passive learning source
│   │       ├── spellscanner/                ✅ Spell enumeration, FormID persistence
│   │       │   ├── SpellScannerScan.cpp         (main scan logic)
│   │       │   ├── SpellScannerJson.cpp         (scan JSON shape, MGEF fields, dump writing)
│   │       │   ├── SpellScannerFormId.cpp       (FormID persistence)
│   │       │   ├── SpellScannerHelpers.cpp      (utility helpers)
│   │       │   └── SpellScannerEncoding.cpp     (encoding/UTF-8)
│   │       ├── uimanager/                   ✅ PrismaUI bridge (12 files)
│   │       │   ├── UIManagerCore.cpp            (singleton, init, panel visibility, DOM bridge)
│   │       │   ├── UIManagerNotify.cpp          (C++→JS data push)
│   │       │   ├── UIManagerScanner.cpp         (scanner tab callbacks)
│   │       │   ├── UIManagerTree.cpp            (tree tab callbacks, procedural gen)
│   │       │   ├── UIManagerAutoBuild.cpp       (tree built at game start: load order fingerprint, startup tree info, notice, held message, main menu watch)
│   │       │   ├── UIManagerPreReqMaster.cpp    (PRM scoring on a worker thread, request parsed there)
│   │       │   ├── UIManagerDeclutter.cpp       (DeclutterTree: native tree declutter on a worker thread)
│   │       │   ├── UIManagerProgression.cpp     (progression system callbacks)
│   │       │   ├── UIManagerConfig.cpp          (unified config load/apply)
│   │       │   ├── UIManagerConfigSave.cpp      (unified config save worker)
│   │       │   ├── UIManagerLocale.cpp          (panel language file lang/user_locale.js)
│   │       │   └── UIManagerIO.cpp              (clipboard, presets, auto-test I/O)
│   │       ├── progressionmanager/          ✅ XP tracking, early grant/mastery, co-save (5 files)
│   │       │   ├── ProgressionManagerCore.cpp       (singleton, core logic)
│   │       │   ├── ProgressionManagerSerialization.cpp (co-save read/write)
│   │       │   ├── ProgressionManagerAPI.cpp        (public API methods)
│   │       │   ├── ProgressionManagerTargets.cpp    (learning target management)
│   │       │   └── ProgressionManagerXP.cpp         (XP calculation, grants)
│   │       ├── spelleffectiveness/          ✅ Runtime magnitude scaling (4 files)
│   │       │   ├── SpellEffectivenessHookCore.cpp   (hook install, settings, main scaling)
│   │       │   ├── SpellEffectivenessHookDisplay.cpp (display name/description modification)
│   │       │   ├── SpellEffectivenessHookLegacy.cpp (legacy compatibility)
│   │       │   └── SpellEffectivenessHookGrant.cpp  (early spell granting/removal)
│   │       └── treebuilder/                 ✅ Native NLP tree construction + tree declutter (11 files)
│   │           ├── TreeBuilderCore.cpp          (build dispatch, validation, repair)
│   │           ├── TreeBuilderClassic.cpp       (Classic builder: tier-first, the one builder)
│   │           ├── TreeBuilderThemes.cpp        (theme discovery + theme assignment)
│   │           ├── TreeBuilderReachability.cpp  (unlock simulation, unreachable repair)
│   │           ├── TreeBuilderSimilarity.cpp    (similarity matrix, spell keyword bag)
│   │           ├── TreeBuilderPlugins.cpp       (a spell's mod, same-mod parents)
│   │           ├── TreeBuilderBridges.cpp       (cross school bridges + school links; JS side: modules/schoolBridges.js)
│   │           ├── TreeNLP.cpp                  (TF-IDF, cosine sim, fuzzy matching, PRM scoring)
│   │           ├── LayoutDeclutter.cpp          (declutter: collect, spread, push apart, reply; JS twin: modules/layoutDeclutter.js)
│   │           ├── LayoutLineClear.cpp          (declutter line search: passes, one spell's search)
│   │           ├── LayoutLineClearCost.cpp      (declutter line search: the cost of a spot)
│   │           ├── LayoutLineGrid.cpp           (declutter line search: grids and fans)
│   │           ├── LayoutMath.cpp               (fdlibm sin/cos/atan2)
│   │           └── SimdKernels.cpp              (SIMD-optimized compute kernels)
│   ├── DummyDEST/                 # DEST compatibility shim
│   │   ├── CMakeLists.txt
│   │   └── src/
│   │       └── Main.cpp           ✅ Inert DontEatSpellTomes.dll replacement
│   ├── BookXP/                    # BookXP addon plugin
│   │   ├── CMakeLists.txt
│   │   └── src/
│   │       └── Main.cpp           ✅ BookMenu watcher + API integration
│   ├── Toxicology/                # Toxicology addon (own release; README.md, ESL.md, art/ICON.md)
│   │   ├── CMakeLists.txt
│   │   ├── data/                  ESL, settings, I4 rule, icon SWF (shipped); dev/ self test file (never shipped)
│   │   └── src/                   core/ (RE-free, tools/toxicology-test) + game/ (hits, apply, magicka, bottles, co-save, self test)
├── Scripts/Source/
│   ├── SpellLearning_DEST.psc         ✅ DEST native function stubs
│   ├── SpellLearning_DEST_Handler.psc ✅ DEST event handler
│   ├── SpellLearning_ISL.psc          ✅ ISL native function stubs
│   ├── SpellLearning_ISL_Handler.psc  ✅ ISL event handler
│   └── DEST_FormExt.psc               ✅ Form extension for DEST
├── PrismaUI/views/SpellLearning/
│   └── SpellLearningPanel/          ✅ Main UI (39 modules)
│       ├── index.html               ✅ UI structure + module loading
│       ├── themes/design-*.css      Designs laid over styles-skyrim.css (Arcane, Modern Dark)
│       ├── styles-skyrim.css        ✅ Skyrim-themed styling
│       ├── script.js                ✅ Main app logic
│       ├── themes/                  ✅ Theme definitions (default, skyrim)
│       └── modules/                 ✅ 39 JavaScript modules
├── SKSE/Plugins/SpellLearning/     Runtime data (presets, librarian, card icons)
└── docs/
    ├── ARCHITECTURE.md              ✅ This file
    ├── DESIGN.md                    ✅ Design patterns and UI documentation
    ├── MODULE_CONTRACTS.md          ✅ Module creation contracts
    ├── TREE_BUILDING_SYSTEM.md      ✅ Tree building algorithms
    ├── DEST-IMPROVEMENTS.md         ✅ DEST comparison
    ├── PRESETS.md                    ✅ Preset system documentation
    ├── TRANSLATING.md               ✅ Translation guide
    ├── PLAN-PUBLIC-MODDER-API.md    ✅ Public modder API spec
    └── research/
        └── TREE_GENERATION_RESEARCH.md ✅ NLP/ML research
```

---

## Deployment Structure

```
MO2/mods/HeartOfMagic_RELEASE/
├── PrismaUI/
│   └── views/
│       └── SpellLearning/              # Must match CreateView path!
│           └── SpellLearningPanel/
│               ├── index.html
│               ├── script.js
│               ├── styles-skyrim.css
│               ├── themes/
│               └── modules/            # JavaScript modules
├── Scripts/
│   ├── *.pex                           # Compiled Papyrus
│   └── Source/
│       └── *.psc
├── SKSE/
│   └── Plugins/
│       ├── SpellLearning.dll
│       └── SpellLearning/
│           └── presets/
└── (ESP if applicable)
```

---

## Implementation Status

### ✅ Completed
- PrismaUI panel with tabbed interface
- Spell scanning (all spells from plugins)
- LLM integration (OpenRouter API; removed 2026-09-28)
- Tree visualization (radial layout, canvas, 3D globe)
- Progression system (XP tracking, multipliers)
- SKSE co-save persistence
- DEST integration (bundled)
- Difficulty profile system (6 presets + custom)
- Progressive revelation (name/effects/description)
- Discovery Mode (hide locked, show ??? previews)
- Tier-based node sizing
- School color customization
- Configurable hotkey
- Early Spell Learning (grant at threshold)
- Progressive Effectiveness (runtime magnitude scaling)
- Self-cast XP bonus after threshold
- Binary effect threshold handling
- How-to-Learn info panel
- Learning status badges
- Divider color customization
- Multi-prerequisite preservation option
- Tree Generation Validation (reachability check, auto-fix, retry UI)
- Spell Tome Hook (intercepts tomes, grants XP, keeps book)
- Edit Mode (add/remove nodes, modify links)
- Complex Build (native C++ tree generation — 5 builder modes; only Classic remains since 2026-09-27)
- Native NLP engine (TF-IDF, cosine similarity, fuzzy matching)
- FormID persistence (survives load order changes)
- Performance optimizations (shared_mutex, cached player pointer)
- Notification throttling (configurable interval)
- Spell blacklist system
- SkyrimNet → LLM rename (independent AI integration)
- Papyrus API for inter-mod communication
- Theme system (default + Skyrim theme)
- Starfield background effect
- Per-school default shapes (5 schools × distinct visual shapes; removed 2026-09-27 with the builders that read them)
- LLM keyword classification (batched per-school, optional; removed 2026-09-27 with `llmTreeFeatures.js`)
- Plugin whitelist/blacklist filtering
- 12 visual shape profiles (organic, explosion, tree, mountain, portals, spiky, radial, cloud, cascade, swords, grid, linear)

### ✅ Recently Completed (Feb 14, 2026)

#### Native C++ Tree Builders (Python Eliminated)
- **`TreeNLP`** (`src/treebuilder/TreeNLP.cpp`, `include/treebuilder/TreeNLP.h`) — Core NLP engine: TF-IDF vectorization, cosine similarity, char n-gram similarity, Levenshtein distance, fuzzy matching (ratio, partial ratio, token set ratio), theme scoring, PRM candidate scoring
- **`TreeBuilder`** (`src/treebuilder/TreeBuilder*.cpp`, `include/treebuilder/TreeBuilder.h`) — Tree construction engine with 5 builder modes (Classic, Tree, Graph, Thematic, Oracle; all but Classic removed 2026-09-27), theme discovery, tree validation, unreachable node repair
- **Python completely eliminated** — No PythonBridge, no PythonInstaller, no embedded Python, no server.py, no subprocess IPC
- **All builder modes native** — TF-IDF, fuzzy matching, arborescence, LLM integration all in C++ (only Classic remains since 2026-09-27)
- **PRM scoring native** — `TreeNLP::ProcessPRMRequest()` replaces Python prereq_master_scorer.py
- **Wine/Proton compatibility** — No subprocess = no pipe/TCP IPC issues on Linux

### ✅ Recently Completed (Feb 11, 2026)

#### Passive Learning Feature
- **UI:** Toggle, scope dropdown (All/Root/Novice), XP-per-game-hour slider (1-50), per-tier max % caps
- **Settings:** `passiveLearning` object in `state.js` with `enabled`, `scope`, `xpPerGameHour`, `maxByTier`
- **Persistence:** Saved/loaded via `saveUnifiedConfig`/`onUnifiedConfigLoaded`
- **C++ side:** Not yet implemented (UI settings ready for timer-based XP granting)

#### Curved Edge Rendering
- **`_drawEdgePath()`** helper in `canvasRendererEdges.js` (was `canvasRendererV2.js`) — quadratic Bezier curves with 15% perpendicular offset
- **Settings toggle:** `edgeStyle: 'straight'|'curved'` in state.js + checkbox in settings panel
- Applied to all 3 edge passes (base connections, selected path, learning path)

#### themeColor Pipeline Fix
- **treeViewerUI.js** fast path (`_loadTrustedTree`): Added `theme`, `themeColor`, `skillLevel` to node construction
- **treeParser.js** slow path: Same fields added to node construction
- Fixes per-node theme colors being ignored (falling back to school color)

#### Import Modal Buttons
- Added `paste-tree-btn` and `import-cancel` buttons to import modal (HTML elements JS already referenced)

#### Modder API (Papyrus + C++)
- **`SpellLearningAPI.h`** — Public C++ header for SKSE plugins (messaging + full interface)
- **`SpellLearning.psc`** — Papyrus API with 26 native functions
- **`RegisterXPSource()`** — Creates UI controls (enable toggle, multiplier slider, cap slider)
- **`AddSourcedXP()`** — Grants XP through cap system with named source
- **`AddRawXP()`** — Bypasses all caps/multipliers
- **ModEvents** — 7 events for inter-mod communication
- See [Modder API Reference](#modder-api-reference) section below

### 🔄 Planned Improvements
- Passive learning C++ timer (game-hour XP granting)
- Viewport culling for large trees
- Level-of-detail rendering

### ✅ Previously Completed (Feb 9, 2026)

#### Modular Tree Builders (now native C++)
- **Classic builder** — Tier-first builder. Novice=depth 0, Master=depth 4. NLP similarity guides within-tier parent selection.
- **Tree builder** — NLP-based builder. TF-IDF similarity drives parent→child links. Round-robin theme interleaving. (Removed 2026-09-27.)
- **Shared error handler** — `_handleBuildFailure()` in `proceduralTreeBuilder.js` replaced the duplicate error handlers of the Classic and Tree modes; only Classic uses it now (its Retry reruns `TreeGrowthClassic.buildTree()` since 2026-09-28)
- **Classic `buildTree()` sends** `command: 'build_tree_classic'` and its config. It also named `tier_zones`, but read them from the wrong object, so the value was always undefined and dropped from the JSON, and C++ never read the key; the line went on 2026-09-28. Tier zones act in the panel: `ClassicLayout` places the returned tree by `TreeGrowthClassic.settings.tierZones`. A throw while the request is put together now releases the pending flag, the Build buttons and the progress modal (as a result parse error does), since no reply will come
- **Tree `buildTree()` sends** `command: 'build_tree'` explicitly (Tree mode removed 2026-09-27)
- **Fixes Classic tier zone controls** — Tier zone sliders now work because tree structure matches tier ordering (Novice near roots, Master at edges)

### ✅ Previously Completed (Feb 7, 2026)

#### Per-School Default Shapes
- **`SCHOOL_DEFAULT_SHAPES`** in both C++ (`plugins/spelllearning/src/treebuilder/`) and JS (`shapeProfiles.js`) (both removed 2026-09-27 with the builders that read them)
- Destruction=explosion, Restoration=tree, Alteration=mountain, Conjuration=portals, Illusion=organic
- Applied automatically when LLM auto-config is disabled

#### LLM Keyword Classification (removed 2026-09-27 with `llmTreeFeatures.js`; its button had already left `index.html`)
- **LLM Keyword Classification** — Optional batched LLM classification for spells with weak/missing keywords
- JS UI: toggle in LLM Features, `[K] Classify Keywords` button on Spell Scan tab (neither in `index.html`; the LLM Features settings code went on 2026-09-28)
- Default off — TF-IDF + fuzzy matching runs unchanged when disabled

#### Plugin Whitelist
- Per-plugin opt-in/out filtering for spell tree generation
- UI modal in Spell Scan tab for managing plugin list
- `pluginWhitelist` array in unified config (persistent)
- Base game plugins auto-detected and always included by default

#### 12 Visual Shape Profiles
- `shapeProfiles.js` expanded to 12 shapes with masks, conformity passes (masks removed 2026-09-27)
- `layoutEngine.js` BFS growth with density stretch (removed 2026-09-27)
- Shape-specific angular control and density multipliers

### ✅ Previous Updates (Feb 5, 2026)

#### Performance Optimizations
- **`std::shared_mutex`** replaces `std::mutex` in SpellEffectivenessHook for read-heavy access
- **Cached player pointer** in AdjustForPerks hook (avoids GetSingleton() per-effect)
- **`std::call_once`** for one-time debug logging
- **Static regex** for description parsing

#### FormID Persistence
- **Persistent FormID format:** `PluginName.esp|0x00123456` survives load order changes
- **Auto-validation on tree load:** Stale FormIDs resolved from persistentId field
- **ESL/light plugin support:** Correct handling of 0xFE prefix with 12-bit local IDs
- **No save after fix:** the corrected tree goes to the panel; the file keeps the old ids and is fixed again on every load

#### SkyrimNet → LLM Rename
- All callback names updated (C++ RegisterJSListener + InteropCall)
- All JS state variables, functions, callbacks renamed
- CSS classes renamed (`.btn-skyrimnet` → `.btn-llm`)
- `skyrimNetIntegration.js` → `llmIntegration.js`
- `SKSE/Plugins/SkyrimNet/` → `SKSE/Plugins/SpellLearning/`
- Documentation updated throughout

### ✅ Earlier Updates (Jan 2026)
- Tree Generation Validation (reachability check, LLM self-correction, gentle auto-fix)
- Code modularization (8000+ line script.js → 39 focused modules)
- Early-learned vs Mastered distinction
- Direct prerequisite XP tracking
- Power step system (6 discrete levels)
- Display hooks (optional "(Learning - X%)" in game menus)
- Progressive reveal improvements
- Auto-refresh on panel open

---

## Key APIs

### CommonLibSSE-NG
- `RE::TESSpellCastEvent` - Spell cast detection
- `RE::TESDataHandler` - Form enumeration
- `RE::PlayerCharacter` - Player reference
- `RE::SpellItem` - Spell data
- `RE::TESObjectBOOK` - Spell tome data
- `RE::ActiveEffect` - Runtime spell effect (magnitude scaling)
- `RE::EffectArchetype` - Effect type classification
- `RE::ActorHandle` / `RE::NiPointer` - Reference handling

### SKSE
- `SKSE::SerializationInterface` - Co-save persistence
- `SKSE::MessagingInterface` - Game lifecycle events
- `SKSE::PapyrusInterface` - Native function registration

### PrismaUI
- View registration and JS execution
- Hotkey handling
- C++ ↔ JS communication via `InteropCall` / `window.callCpp`

---

## Related Docs

| Doc | Purpose |
|-----|---------|
| **BUILD-COMPLEX-TRACE.md** | Full phase-by-phase trace of BUILD TREE (Complex) from click to rendered tree |
| **OVERVIEW.md** | High-level product summary, user-facing features |
| **QUICK_REFERENCE.md** | Quick lookup: components, data formats, modules, XP |
| **DESIGN.md** | Design intent and tier checklist (Tiers 1–5 implemented) |
| **PROGRESSION_DESIGN.md** | XP and learning flow from player perspective |
| **COMMON-ERRORS.md** | Troubleshooting (UTF-8, path mismatch, DLL, etc.) |
| **TREE_LAYOUT_DESIGN.md** | Layout options (zone reservation, round-robin) |
| **SHAPE-AND-GROWTH-ASSESSMENT.md** | Shape/growth assessment; backlog placement; spell-count–scaled shapes |
| **TECHNICAL_RESEARCH.md** | API and integration research |

---

## Notes for LLMs

- **The C++ Classic builder determines tree structure** - prerequisites from tier order and NLP similarity
- **Progressive revelation** - Spell details hidden until XP thresholds reached
- **Discovery Mode** - Enabled by default for Brutal+ difficulties

### Early Learning Flow (CRITICAL DISTINCTION)
1. Player selects spell as learning target
2. At `unlockThreshold` (default 25%), spell granted but **WEAKENED**
3. **IMPORTANT:** Early-learned spell does NOT unlock children!
   - Node state stays "available" (not "unlocked")
   - Children remain LOCKED until 100% mastery
4. Effectiveness follows **discrete power steps** (not linear):
   - 25% XP → 20% power (Budding)
   - 40% XP → 35% power (Developing)
   - 55% XP → 50% power (Practicing)
   - 70% XP → 65% power (Advancing)
   - 85% XP → 80% power (Refining)
5. After `selfCastRequiredAt` (67%), player must cast the spell itself
6. At **100% mastery:**
   - Spell gains full power (breakthrough moment)
   - Node state changes to "unlocked"
   - **NOW children become available**

### State Distinction
| XP Progress | Player Has Spell? | Node State | Children |
|-------------|-------------------|------------|----------|
| 0-19% | No | available/learning | Locked |
| 20-99% | Yes (weakened) | available | **Still Locked** |
| 100% | Yes (full power) | **unlocked** | **Available** |

### Other Notes
- **One target per school** - In "perSchool" mode, can learn multiple spells simultaneously
- **All progress saved** - Every spell tracks XP, not just active targets
- **Runtime magnitude scaling** - No save modification, pure runtime hooks
- **Binary effects** - Paralysis, Invisibility blocked entirely below 80% effectiveness
- **Direct prerequisite tracking** - UI sends prereq list to C++ for proper XP source detection
- **DEST integration bundled** - Always available (replaced ISL)
- **Spell Tome Hook** - Intercepts tome reading in C++, grants XP, keeps book
- **FormID persistence** - Trees survive load order changes via `PluginName.esp|0x123456` format
- **PrismaUI path critical** - CreateView path must exactly match deployment path
- **Panel auto-refresh** - GetPlayerKnownSpells called when panel opens (catches external spell learning)
- **LLM naming** - The AI integration was called "LLM" (not "SkyrimNet"); removed 2026-09-28
- **SkyrimNet bridge** - `SpellLearning_Bridge.psc`, `SpellLearning_QuestScript.psc`, the `SkyrimNetApi.psc` header and `custom_prompts/spell_tree_generator.prompt` sent tree requests to SkyrimNet from Papyrus; nothing had written their request file since the LLM rework and no plugin file ran the quest; removed 2026-09-28
- **Per-school shapes** - Each school had a distinct visual shape in `SCHOOL_DEFAULT_SHAPES` (C++ + JS); removed 2026-09-27 with the builders that read them
- **Plugin whitelist** - Users can filter which plugins contribute spells to tree generation
- **LLM keyword classification** - Optional batched classification for spells with weak keywords, off by default; removed 2026-09-27 with `llmTreeFeatures.js`