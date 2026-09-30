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
CommonLibSSE-NG v10.0.0 (`plugins/external/commonlibsse-ng`, tag `v10.0.0`). All three DLLs
(`SpellLearning`, `DontEatSpellTomes`, `SL_BookXP`) use `add_commonlibsse_plugin(... USE_ADDRESS_LIBRARY)`
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
- `RunScanToFile(mode, preset)` - Scan and write the dump in one call, for callers outside the UI
- `BuildSpellJson(spell, formId, fields)` - The single source of the scan JSON shape (SpellScannerJson.cpp)
- `BuildEffectJson(effect, fields)` - One effect, including MGEF structure when `effectDetails` is on
- `WriteScanOutput(content)` - Write `spell_scan_output.json`, return its path
- `DebugScanDumpEnabled()` - `debug.json` next to it says `writeScanDump: true` (debug installs: the debug symbols archive and `DeployDev.ps1` ship `debug/debug.json`); then the panel's full scan (`UIManager::RunScan`) writes the dump too, after the perk adapters
- `GetSpellInfoByFormId(formId)` - Lookup spell details
- `GetSystemInstructions()` - LLM output format spec
- `GetPersistentFormId(formId)` - Convert runtime FormID to `PluginName.esp|0x00123456` format (plugin name in UTF-8, see below)
- `ResolvePersistentFormId(persistentId)` - Resolve persistent ID back to runtime FormID
- `ValidateAndFixTree(treeData)` - Validate all FormIDs in tree, resolve from persistentId if stale
- `IsFormIdValid(formId)` - Check if a FormID resolves to a valid form
- `SpellsTaughtByTomes()` - The spells some tome teaches; the scan marks each spell `taughtByTome` from it
- `IsEquippedInVoiceSlot(spell)` - The spell is equipped in the voice slot (the default object `kVoiceEquip`); the scan marks `voiceSlot` from it and the tome hook hands such a spell to the vanilla path
- `LogSpellsOutOfTree(spells, taught, unreachable)` - Logs, by kind, the scanned spells the tree leaves out (SpellScannerCopies.cpp, below)

**The scan keeps every spell; the tree takes what a tome teaches (2026-09-30):** the panel's "Tomes only"
switch was on by default and filtered at tree build time only: Classic's build request kept the scanned
spells a tome teaches, while the scan, the librarian's catalog and the perk adapters saw every spell. When
the switch went, that filter went with it, and about 1,100 spells no tome teaches came into the tree (2,542
spells against 1,428). Now the scan keeps every spell that passes its other filters, so the catalog and the
perk adapters see them all (a perk overhaul's keyword reaches a quest or perk spell too, and an effect shared
with a spell outside the tree is still checked against that spell's tags), and marks facts from the
record on each: `taughtByTome` (from `SpellsTaughtByTomes`), `voiceSlot` (`IsEquippedInVoiceSlot`) and
`tomeUnreachable` (below, "Tomes nothing hands out"). The tree takes a spell a tome teaches, whose tome
something hands out, that is not in the voice slot: `isTaughtByTome` / `filterTomeSpells`
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
  at once, no learning target, early learning, weakening or ISL study.
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
  plugin's own master list, 12 bits for a light plugin.
- A tome no record names can still come from an SKSE distribution framework: the worker reads the configs
  under `Data/` and `Data/SKSE/Plugins/` (`IsDistributionConfig`, TomeReachConfig.cpp: SPID `_DISTR.ini`, KID,
  FLM, BOS `_SWAP.ini`, CID, CDF, LLI, SkyPatcher and LLOS folders) and counts a tome they name by editor id
  (from the BOOK record) or as plugin and id (`0x800~Plugin.esp`, `Plugin.esp|0x800`). Only these files:
  inventory layout lists (GridInventory) and icon lists name every book.
- A spell is `tomeUnreachable` when every tome that teaches it is unreached. The scan marks it
  (`UnreachableSpells`), and the tree filter (`isTaughtByTome`) leaves it out; it stays in the catalog and
  the perk adapters. When any plugin cannot be read or is damaged (a record past the end of the file, a
  compressed record that does not inflate), or the pass has not finished when the scan runs (the
  first seconds after the game starts), no spell is marked and the log says so.
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
skill.

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

`castByVampires: true` marks a spell a vampire NPC carries - an NPC whose race or record has the
`Vampire` keyword, through its own or its race's spell list and the leveled spell lists in them, plus
spells SPID hands to `Vampire` in any `Data/*_DISTR.ini` (SpellScannerCasters.cpp, read once). The tag
librarian's blood rules use it; it is absent, not false, for every other spell.

The walk over `Data` sees every file of every mod, so no file name may throw there: `path.string()` converts
to the system's ANSI code page on Windows and throws for a name that page cannot hold (a Korean or emoji name
on an English Windows: "No mapping for the Unicode character exists in the target multi-byte code page").
Names go through `PathText::Utf8` (`include/PathText.h`, `u8string`), a file that fails is skipped with a
log line, and the whole vampire read is optional - if it fails the scan goes on without the tag. The read is
retried only at the next game start (it runs once per session); a Data walk that stops early or cannot open
the directory is logged, not fatal.

A scan that throws still ends for the player: `UIManager::OnScanSpells` runs `RunScan` inside a try block and
answers a failure with `onScanFailed({"mode":"all"|"tomes","reason":...})` (uiHelpers.js; `ReportScanFailure`
takes the mode from the request's `scanMode`, the same check `RunScan` uses). A full scan (`all`) enables the Scan
button again (`restoreScanButton`), puts "Scan failed: <reason>" in the scan status bar (`status.scanFailed`, all
13 languages) and replaces edit mode's "Scanning game spells..." wait line with it. The panel disables the button
while a scan runs, so before this a failed scan left it on "Scanning..." for good. The panel's "Tomes only"
toggle and the background tome scan behind it are gone (2026-09-30): the tree itself leaves out what a
player does not learn (the scan keeps every spell and marks `taughtByTome`, `tomeUnreachable` and `voiceSlot`), so a
`tomes` failure can only come from elsewhere and is just logged. An older plain
JSON-string payload is read as a full scan's reason. A throw that is not a `std::exception` sends an empty reason,
which the panel shows as `status.scanFailedUnknown` ("Scan failed (unknown error)", keyed so a language switch
relabels it). `RunScan` does everything that can throw (the scan, the classification, allocating the
shared string for `m_scanText`) before `updateSpellData`, and moves that string into `m_scanText` after it (no copy); the `onScanStored` call after it catches and logs its own failure, so no
"Scan failed" can follow a scan the panel already has.

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
| effect | `summonedKeywords` | a SummonCreature effect: the race keywords of the actor it calls up (`associatedForm` NPC, or every NPC of a leveled list, 4 levels deep) - `ActorTypeUndead`, `ActorTypeDaedra`, `ActorTypeAnimal` ... Only when there are any |
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

Papyrus calls it from the VM thread, so it submits the scan as a game thread task and
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
- On tree load, stale FormIDs auto-resolved from persistentId field
```

### 2. **UIManager** (`plugins/spelllearning/src/uimanager/`, `plugins/spelllearning/include/uimanager/UIManager.h`)
Split across: UIManagerCore.cpp, UIManagerNotify.cpp, UIManagerScanner.cpp, UIManagerTree.cpp, UIManagerDeclutter.cpp, UIManagerIO.cpp, UIManagerProgression.cpp, UIManagerConfig.cpp, UIManagerConfigSave.cpp, UIManagerLocale.cpp
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
- `ClearLearningTargetForSpell(formId)` - Clear every school whose target is this spell (found by id in the target map, not from the spell's effects: the panel keys a target by the tree node's school, which comes from the first effect and can differ from the costliest effect's) - on mastery, unlock and relock
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

### 5. **SpellCastHandler** (`plugins/spelllearning/src/SpellCastHandler.cpp`, `plugins/spelllearning/include/SpellCastHandler.h`)
**Status:** ✅ Implemented

**Responsibilities:**
- Listen to spell cast events (`TESSpellCastEvent`)
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
bool showNotifications = true;
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
- `SpellTomeHook` — `std::mutex` for tome XP tracking set; a second `std::mutex` plus an `std::atomic` generation counter for the tome inventory cache (the container event sink only touches the counter)
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
| `uiHelpers.js` | Shared UI helper functions, `saveTreeToFile` |
| `starfield.js` | Starfield background effect |
| `globe3D.js` | 3D globe visualization (experimental) |
| **Testing & entry** | |
| `unificationTest.js` | Shape profile / WheelRenderer tests (run by `run-tests.js` and `test-runner.html`, not loaded in game) |
| `main.js` | Entry point, initialization |

**Module load order:** See `index.html`. Order is: constants/state/config → shapeProfiles → spellCache/colorUtils/uiHelpers → treeParser/orphanRepair/spellNames → wheel/starfield/globe/canvas/editMode → colorPicker/settingsPanel/treeViewerUI/… → treeCore/classic/treeGrowth/treeGrowthStatus → cppCallbacks/buildProgress/proceduralTreeBuilder → prereqMaster/treeAnimation → script.js → main.js. (The JS tree builders, generationModeUI, autoTest and the WebGL renderer were removed on 2026-09-27, llmIntegration, llmApiSettings and growthDSL on 2026-09-28; unificationTest is no longer loaded in game.)

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
  → Try to resolve runtime FormID
  → If invalid (load order changed):
    → Check for persistentId field ("Plugin.esp|0x001234")
    → ResolvePersistentFormId() → Get new runtime FormID
    → Update node with resolved FormID
  → Auto-save corrected tree
  → Report validation results to UI
```

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

  "notifications": {
    "weakenedSpellNotifications": true,
    "weakenedSpellInterval": 10
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
│   │       ├── uimanager/                   ✅ PrismaUI bridge (10 files)
│   │       │   ├── UIManagerCore.cpp            (singleton, init, panel visibility, DOM bridge)
│   │       │   ├── UIManagerNotify.cpp          (C++→JS data push)
│   │       │   ├── UIManagerScanner.cpp         (scanner tab callbacks)
│   │       │   ├── UIManagerTree.cpp            (tree tab callbacks, procedural gen, PRM scoring)
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
│   └── BookXP/                    # BookXP addon plugin
│       ├── CMakeLists.txt
│       └── src/
│           └── Main.cpp           ✅ BookMenu watcher + API integration
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
- **Auto-save after fix:** Corrected tree saved immediately

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