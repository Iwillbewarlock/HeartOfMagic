# HeartOfMagic.esp and explosion twins

Heart of Magic's plugin, `HeartOfMagic.esp`, exists for one job (1.6): give hidden, harmless spell effects an
explosion that mods reading explosions by name do not take for an element. A second, optional plugin,
`HeartOfMagic_Mysticism.esp`, makes Mysticism's screen-shake script use it (see "The Mysticism patch"). This page
holds the plugins' record values, how the files were made, the twin map, the offline check, what the DLL does with
them, and the in-game self-test.

## Why

Frostwalker (Nexus 184628, version 2.2 in TAKEALOOK) freezes water under cold explosions. It decides an explosion's
element from text alone (`src/Utils.cpp`, `GetExplosionType`): the explosion's EditorID, then its name, then its model
path, each lower-cased and tested against its pattern files (`SKSE/Plugins/Frostwalker/<folder>/`, every regular file
in the folder - `Settings.cpp` `LoadAllPatterns` does not look at the extension; a missing folder is an empty list) in
the order NeutralSources, FrostSources, FireSources, WaterSources; the first substring hit wins. (Text alone is for
explosions and projectiles; for walking on water and cloaks Frostwalker reads keywords, listed in its `*Keywords`
folders.) The 2.2 lists:

| Folder | Patterns |
|---|---|
| NeutralSources | `practice` |
| FrostSources | `cold` `frost` `ice` `blizzard` `freeze` `freezing` (`ice` does not count right after `vo`, as in "voice") |
| FireSources | `fire` `flame` `heat` |
| WaterSources | `water` `steam` `river` `ocaen` `sea` |

**Premise: EditorIDs.** The engine drops the EditorID of an explosion after loading. Frostwalker gets it the way
CommonLib plugins usually do, from powerofthree's Tweaks (`po3_Tweaks.dll`, export `GetFormEditorID`, which answers
only with Tweaks' "Load EditorIDs" on; Frostwalker 2.2's DLL holds the `po3_Tweaks` / `GetFormEditorID` strings).
Without it Frostwalker sees no EditorID: the original then has no EditorID, no name and the model
`Effects\FXEmptyObject.nif`, reads Unknown, and the shake does not freeze water - the twin changes nothing there. TAKEALOOK
loads EditorIDs, which is how the bug showed.

Mysticism's `MAG_ScreenShakeEffect` (`MysticismMagic.esp|0xBDE626`) is a hidden Script effect that only shakes the
screen. Its explosion is `MAG_BlizzardForceExplosion` (`Skyrim.esm|0x0B7A05`), and "blizzard" reads as cold, so every
spell carrying the shake froze water. In `MysticismMagic.esp` alone 22 items carry it (11 spells, 11 scrolls): the
spells Fire Storm, Hysteria, Harmony, Mayhem, Debilitation (`Skyrim.esm|0x0B62E6`; Vokriinator Black renames its
EditorID `MAG_Paralysis`), Bane of the Undead, Quietude, Stormblast, Charged Nova, Black Marsh and Blizzard, and the
scrolls of Fire Storm, Hysteria, Harmony, Mayhem, Debilitation, Bane of the Undead, Quietude, Domination, Stormblast,
Charged Nova and Black Marsh. Mysticism's Domination spell does not carry it; in TAKEALOOK
`Mysticism - Vokrii Compatibility Patch.esp` adds it there, the 23rd (all 23 magnitude 0, area 0, duration 1, read
with houseCARL 2026-10-20; Hysteria and Harmony confirmed freezing water in game 2026-10-20). Real Blizzard's
hazard effect `MAG_FrostDamageFFHazardArea` (`Skyrim.esm|0x0B79FE`, SpawnHazard) uses the same explosion and must keep
freezing water. (`MAG_Blizzard` also carries the shake effect; that entry moves to the twin, its hazard does not.)

**What places the explosion: the script, not the Explosion field** (found 2026-10-20). The first fix repointed the
effect's own Explosion field (`data.explosion`) to the twin. In game, with `HeartOfMagic.esp` enabled and the DLL of
`0c2ec7fc`, Hysteria, Harmony and Fire Storm still froze water, although `SpellLearning.log` showed that side working
as designed (`MysticismMagic.esp|0xBDE626` took the twin, self-test pass=9 fail=0). The effect runs the vanilla script
`magicPlaceActivatorScript` (VMAD) with the property `ExplosionRef = Skyrim.esm|0x0B7A05` `MAG_BlizzardForceExplosion`.
The script (UOSC's source, `scripts\source\magicplaceactivatorscript.psc`) does `akCaster.PlaceAtMe(PlacedActivator)`,
then `ActivatorRef.PlaceAtMe(ExplosionRef)` at `OnEffectStart` and again every 0.65 s (`OnUpdate`) until the effect
ends. Frostwalker's explosion hook classifies every placed explosion by its base EXPL, so the property's Blizzard
explosion still reads cold. The field repoint alone does not stop the freeze; the script property has to name the
twin, which the optional Mysticism patch does (user decision: a patch plugin, not a runtime edit of script
properties). Of the 15 records that reference `0x0B7A05` (houseCARL reverse references), the shake effect is the only
one that is not a frost hazard; the others keep the original. The DLL's field repoint stays (harmless; the twin is now
also what the patch names).

Runtime forms were ruled out: `BGSExplosion` has no `Copy()`, and a form made at runtime does not survive in a save.
A record in a light plugin is save-safe and costs no load order slot.

## The plugin

`HeartOfMagic.esp` at the repo root (the root mirrors Data). The release puts it at the Data root
(`plugins/cmake/Distribution.cmake`, step 4b), the FOMOD installs it in its "Core Files" step, `DeployDev.ps1` copies
it to the dev mod folder's root. 334 bytes, SHA-256 `c6d636e0b5ce11e0737a588eb05842ad68a8713a5ee8f4e5b7084fa18f2a4b0f`
(HEDR 1.70; the first build, HEDR 1.71, was `5cfefc2f...`).

### Header

| Field | Value |
|---|---|
| Flags | `0x200` (ESL / light), nothing else |
| HEDR | version 1.70 (bytes `9A 99 D9 3F`), 2 records (one group, one record), next object ID `0x801` |
| Masters | `Skyrim.esm` only |
| Author (CNAM) | Iwillbewarlock |
| Description (SNAM) | Heart of Magic Librarian |

### Records

| Local FormID | Type | EditorID | persistentId |
|---|---|---|---|
| `0x800` | EXPL | `HoM_ShakeBlastTwin` | `HeartOfMagic.esp\|0x000800` |

The EditorID was chosen to contain no Frostwalker pattern (case-insensitive). The twin has no name (FULL), like the
original.

### `0x800` HoM_ShakeBlastTwin: field values

Every field is the load-order winner of `Skyrim.esm|0x0B7A05`, which is `MysticismMagic.esp` from
**Mysticism - A Magic Overhaul 2.4.2.0** (Nexus 27839, file `Mysticism - A Magic Overhaul-27839-2-4-2-1712790317.7z`,
`MysticismMagic.esp` SHA-256 `41b9309e7970059895498081457309ab3691d4d2ae462fe8b1064b15ec8bdf5c`), read 2026-10-20.
Only the EditorID differs; the subrecords are byte-identical to Mysticism's otherwise (compared byte for byte).

| Field | Value |
|---|---|
| EditorID (EDID) | `HoM_ShakeBlastTwin` (source: `MAG_BlizzardForceExplosion`) |
| Name (FULL) | none |
| Object bounds (OBND) | (0,0,0) to (0,0,0) |
| Model (MODL) | `Effects\FXEmptyObject.nif` (no pattern) |
| Model data (MODT) | `02 00 00 00 00 00 00 00 00 00 00 00` (12 bytes, as the source) |
| Image space modifier (MNAM) | `Skyrim.esm|0x0B7A06` `MAGBlizzardForceExplosionImod` |
| Object effect (EITM) | none (the source has no EITM) |
| Light | none |
| Sound 1 | `Skyrim.esm|0x03E5E9` `XXXPlaceholderSilenceSD` |
| Sound 2 | none |
| Impact data set | none |
| Placed object | none |
| Spawn projectile | none |
| Force | 100 |
| Damage | 0 |
| Radius | 1000 |
| IS radius | 1760 |
| Vertical offset mult | 0.5 |
| Flags | 339 = `0x153`: bit `0x1` (unnamed), AlwaysUsesWorldOrientation, IgnoreLosCheck, IgnoreImageSpaceSwap, NoControllerVibration |
| Sound level | 1 (Normal) |
| Record header | flags 0, form version 44, version-control 0, Version2 2 (as the source) |

Raw DATA (52 bytes): `00000000 e9e50300 00000000 00000000 00000000 00000000 0000c842 00000000 00007a44 0000dc44
0000003f 53010000 01000000`.

For reference, `Skyrim.esm`'s own version differs from Mysticism's in Damage 1 (Mysticism 0), EditorID
`BlizzardForceExplosion`, form version 40. These values are only the file's defaults: at `kDataLoaded` the DLL copies
the live original's values into the twin (see "What the DLL does"), so a later Mysticism or another mod's change to
the original reaches the twin without remaking the file. The EditorID and the (empty) name stay the file's.

### How it was made

1. `housecarl_create_plugin` `patch="HeartOfMagic"`, `esl=true`, author and description as above. houseCARL put it in
   `D:\TAKEALOOK\mods\houseCARL - HeartOfMagic`; the folder was renamed to `[HoM-build] HeartOfMagic ESL` before any
   record was written. It was never enabled in MO2 (no modlist.txt or plugins.txt edit).
2. `housecarl_create` `into="HeartOfMagic.esp"`: one bare `Explosion` with EditorID `HoM_ShakeBlastTwin` (`0x800`).
3. `housecarl_apply` `into="HeartOfMagic.esp"`, `op=CopyFrom`, `from="0B7A05:Skyrim.esm"`,
   `from_source="MysticismMagic.esp"` for Model, ObjectBounds, Damage, Flags, Force, ISRadius, Radius,
   ImageSpaceModifier, Sound1, Sound2, SoundLevel, VerticalOffsetMult, ImpactDataSet, Light, ObjectEffect,
   PlacedObject, SpawnProjectile (dry run first).
4. Byte edit (node, in the session's scratchpad): houseCARL writes `Update.esm` as a second master; the
   `MAST Update.esm` + `DATA` pair (31 bytes) was cut from the TES4 header and the header size lowered by 31; the
   record's own FormID re-indexed `0x02000800` to `0x01000800` (the file was checked to hold no other FormID with a
   non-zero index: MNAM and the six DATA links are all `Skyrim.esm`); the null `EITM` houseCARL wrote for the empty
   object effect (the source has none) dropped, with the record and group sizes lowered by 10; the record header's
   Version2 set to 2 as in the source. **Extending the plugin with houseCARL adds `Update.esm` back: repeat this.**
5. Read back with `housecarl_records` `source={"file":"HeartOfMagic.esp","mod":"[HoM-build] HeartOfMagic ESL"}`
   (form `everything`): every field equal to the winner's dump, Name absent. `housecarl_check plugins=["HeartOfMagic.esp"]`
   (errors family, off-order): 0 dangling references, 0 missing masters, 0 unscannable records.
6. Copied to the repo root (same SHA-256 as the build folder's copy), then the build folder was deleted.
7. HEDR version lowered from 1.71 (`48 E1 DA 3F`, what houseCARL writes) to 1.70 (`9A 99 D9 3F`) by patching the four
   bytes at file offset 30 (node, 2026-10-20): SE 1.5.97 and AE before 1.6.1130 refuse a 1.71 plugin, and a 1.70
   light plugin's object IDs from `0x800` are all this file needs. Read back by parsing (HEDR 1.70, size 334) and with
   `tools/frostwalker-twin-check.js` (header and field checks below). `.gitattributes` marks `*.esp binary`.

## The Mysticism patch

`HeartOfMagic_Mysticism.esp` at the repo root: optional, for load orders with Mysticism - A Magic Overhaul. 817 bytes,
SHA-256 `2f3c12915807944e4fa6f10028ff8f0d91e96dfcbbdcb6170da06502a955e995`. The release ships it under
`optional/MysticismPatch/` (`Distribution.cmake`, step 4b; not at the Data root, so a zip extracted by hand without
Mysticism does not bring a plugin with a missing master); the FOMOD's "Compatibility Patches" step installs it to the
Data root, Recommended when `MysticismMagic.esp` is active and not installable otherwise; `DeployDev.ps1` always
copies it (hash-checked), since the dev install has Mysticism.

| Field | Value |
|---|---|
| Flags | `0x200` (ESL / light) |
| HEDR | version 1.70 (bytes `9A 99 D9 3F`) |
| Masters | `Skyrim.esm`, `Update.esm`, `MysticismMagic.esp`, `HeartOfMagic.esp` |
| Records | one: an override of `MysticismMagic.esp|0xBDE626` `MAG_ScreenShakeEffect` (MGEF, FormID `0x02BDE626` in this file) |

The record is Mysticism's own (2.4.2.0, `MysticismMagic.esp` SHA-256 `41b9309e...`, as above) with one change: the VMAD
property `magicPlaceActivatorScript.ExplosionRef` names `HeartOfMagic.esp|0x000800` (`0x03000800` here) instead of
`Skyrim.esm|0x0B7A05`. Byte compare with Mysticism's record (both 372 bytes, record flags 0, form version 44,
version-control 0, Version2 0; subrecords EDID VMAD FULL MDOB KSIZ KWDA DATA SNDD DNAM): exactly two byte differences,
`ExplosionRef` `0x000B7A05` -> `0x03000800`, and DATA's casting art master index `05` -> `02` (the same
`MysticismMagic.esp` form under this file's master list). The Explosion field in DATA keeps `Skyrim.esm|0x0B7A05`; the
DLL repoints it at runtime as before.

How it was made (2026-10-20, houseCARL, build folder in the session scratchpad, never enabled in MO2):

1. `housecarl_create_plugin` `esl=true`.
2. `housecarl_forward` `source=MysticismMagic.esp` for `MysticismMagic.esp|0xBDE626` (an override copied from
   Mysticism's version, not from the TAKEALOOK winner).
3. `housecarl_apply` `Set VirtualMachineAdapter.Scripts[0].Properties[0].Object = 000800:HeartOfMagic.esp`.
4. HEDR lowered from 1.71 to 1.70 at file offset 30, as for `HeartOfMagic.esp`. `Update.esm` stays a master (houseCARL
   adds it; it is always present, and removing it would mean re-indexing every FormID).

**Load order: it must be the effect's last override.** A plugin loaded after it that also edits `MAG_ScreenShakeEffect`
brings back its own copy of the VMAD and with it the Blizzard explosion. In TAKEALOOK the winner before the patch is
`Synthesis - Sound and Combat.esp` (it changed only the projectile, to an Audio Overhaul one; the effect is Self, so the
projectile is not used): the patch goes after it. The self-test's `mysticism-patch` line names the file that wins.

## The twin map

`SKSE/Plugins/SpellLearning/librarian/environment/twins.json` (shipped with the rest of `SKSE/Plugins/SpellLearning`;
`DeployDev.ps1` copies `librarian/environment/*.json`). The librarian's rule loader reads only the files directly in
`librarian/`, so the subfolder is not taken for a rule file.

```json
{ "explosions": [ { "original": "Skyrim.esm|0x0B7A05", "twin": "HeartOfMagic.esp|0x000800", "why": "..." } ] }
```

Ids are persistentIds (`PluginNames::MakePersistentId`, `"Plugin|0x%06X"`). An entry means: the twin is a neutral copy
of the original, and the original classifies cold (or another element) to Frostwalker. That judgement is made here,
offline, not by the DLL.

## Offline check

```powershell
node tools/frostwalker-twin-check.js --patterns "<Data>\SKSE\Plugins\Frostwalker"   # not a folder: exit 2
node tools/frostwalker-twin-check.js            # Frostwalker 2.2's Default.ini lists, built in
```

It reimplements Frostwalker's matcher (order, substring, the `vo` + `ice` case, the pattern file rules: trimmed lines,
blank and `#` lines skipped), checks it on seven fixed cases (the original's EditorIDs `MAG_BlizzardForceExplosion` and
`BlizzardForceExplosion` read cold, `VoiceUnrelentingForce` does not, ...), then reads each twin's EDID, FULL and MODL
from the plugin file and fails (exit 1) unless the record is an EXPL and each string is Unknown. Run 2026-10-20
against TAKEALOOK's Frostwalker 2.2 folder and the built-in lists: PASS (EDID `HoM_ShakeBlastTwin` unknown, no FULL,
MODL `Effects\FXEmptyObject.nif` unknown). A copy with the EditorID changed to `HoM_ShakeFrostTwin` fails, as it should.

It also checks the file itself (added after the ESL review, 2026-10-20): the TES4 flags carry `0x200`, HEDR is 1.70
(`9a99d93f`), the masters are exactly `["Skyrim.esm"]`, each twin's FormID top byte equals the master count (a record
of this plugin, not an override), and every subrecord after the EDID equals, in order, the fixed bytes in the script's
`TWIN_FIELDS` (OBND, MODL, MODT, MNAM, DATA as in the table above). Run 2026-10-20: PASS; copies with the light flag
cleared, HEDR 1.71, the record re-indexed to `0x00000800`, one DATA byte changed, or the master renamed each fail with
their own line. The C++ copy of the matcher (`include/environment/EnvironmentMatch.h`) is checked on the same cases by
`librarian-test --self-test`.

Then the Mysticism patch (`tools/mysticism-patch-check.js`, both scripts reading with `tools/plugin-reader.js`, which
throws on a file that is not a plugin or whose sizes run past their ends):

```powershell
node tools/frostwalker-twin-check.js --mysticism "<Mysticism mod folder>\MysticismMagic.esp"   # also compare with the source
node tools/frostwalker-twin-check.js --patch <a copy of HeartOfMagic_Mysticism.esp>
```

Without `--mysticism`: the file exists and parses, light flag, HEDR 1.70, masters exactly the four above, exactly one
record, an uncompressed MGEF with FormID `MysticismMagic.esp|0xBDE626` under the master list (an override, not a new
record), EditorID `MAG_ScreenShakeEffect`, one VMAD with `magicPlaceActivatorScript` whose `ExplosionRef` is a single
object resolving to the twin `twins.json` gives `Skyrim.esm|0x0B7A05`. With `--mysticism`: also the source record
(`MysticismMagic.esp`'s own `0xBDE626`): same record flags and form version, same subrecords in the same order, and
each subrecord byte for byte equal after mapping its FormIDs from the source's master list (and the source itself) to
the patch's - MGEF DATA's 16 FormID fields, MDOB, KWDA, ESCE, SNDD's sound FormIDs and every VMAD object property -
with one allowed difference: the source's `ExplosionRef` must be `Skyrim.esm|0x0B7A05` and the patch's the twin. Any
other difference fails with the subrecord, the byte offset and both values. Run 2026-10-20 with TAKEALOOK's Mysticism
2.4.2.0: PASS (9 subrecords, 14 FormIDs mapped, 0 differing). Copies with the light flag cleared, HEDR 1.71,
`ExplosionRef` back on the original, DATA's casting art left on index `05`, one DATA or KWDA byte changed, a master
renamed, the record made a new record (index `04`), a truncated file, a non-plugin file and a missing file each fail
with their own line. Not a C++ harness case: the only C++ plugin reader in `tools/` is tome-reach-test's, built for book and
container records, so the file check is JavaScript only; the self-test's verdict logic is in `librarian-test --self-test`.

## What the DLL does (1.6, `src/environment/EnvironmentTwins.cpp`)

At `kDataLoaded`, after the perk adapters (`Main.cpp` `OnDataLoaded`), `Environment::Initialize()`:

1. Reads `twins.json`. Each entry's ids go through `SpellScanner::ResolvePersistentFormId` and
   `RE::TESForm::LookupByID<RE::BGSExplosion>(formId)`; a null result for either (HeartOfMagic.esp not enabled, the
   original's plugin absent, not an explosion) is logged once at info and the entry skipped - nothing else changes.
2. Copies the live original into the twin: `data` (the whole `BGSExplosionData`: light, sounds, impact data set,
   placed object, spawn projectile, force, damage, radius, IS radius, vertical offset, flags, sound level),
   `boundData`, `formEnchanting` / `castingType` / `amountofEnchantment`, `imageSpaceModifying`, and the model path
   (`TESModel::SetModel`). Not copied: the name and EditorID (Frostwalker reads them; the twin stays nameless), and
   the `TESModel` texture and addon arrays (the form's own heap buffers - copying the pointers would share and later
   double-free them; the twin keeps the file's MODT, which is the same). The model path is copied only while
   Frostwalker's installed lists read it as no element (read as Frostwalker reads them; without its folder the
   lists are empty, as in Frostwalker, and nothing reads the twin anyway); otherwise the twin keeps its own and a
   warning says so.
3. Records, once, every `EffectSetting` whose `data.explosion` is an original and that passes all of these, by
   record (`Environment::TwinRefusal`, `include/environment/EnvironmentMatch.h`):
   - archetype `kScript` and `data.primaryAV == kNone`;
   - `kHideInUI` set, `kHostile` and `kDetrimental` clear;
   - every entry in every spell, scroll, enchantment, potion and ingredient that carries the effect has magnitude 0
     (one pass over those five form arrays; an effect no item carries passes). The authored `effectItem.magnitude`,
     not `Effect::GetMagnitude()` (which answers 0 for any `kNoMagnitude` effect): a hidden Script effect with that
     flag and a magnitude written into its carriers keeps the original - the stricter reading.
   Keywords are not read: `MAG_ScreenShakeEffect` carries `MagicDamageFrost` and ResistFrost though it does no damage.
   Each refused candidate is logged with its first failed condition (up to 40 lines).
4. Points every recorded effect at its twin while `config.json` `environment.enabled` is not false (read like the
   adapters' `requires.config`: missing or unreadable means on), and logs the count and the persistentIds. The table is
   in place, and the scanner's map ready, only after this step.

The twin's values are copied this once, at `kDataLoaded`: a mod that changed the original later in the session (none
is known to) would not reach the twin until the next game start.

The switch is read again whenever the panel saves its settings (`UIManagerConfigSave.cpp` `ApplySavedConfig`, a
game-thread task, `Environment::OnConfigApplied`): turned off, every recorded effect gets its original back; turned on,
the twin again. Each recorded effect remembers what HoM last wrote into it, and a switch writes only while the effect
still holds that (or, before the first write, the original): an explosion another mod set since - a third one, none, or
the original put back while the twin was on - is left alone and named in a warning (`DecideSwitch`,
`EnvironmentMatch.h`; on, off, on again, the same setting twice, a third explosion and none are checked by
`librarian-test --self-test`). There is no panel toggle yet: a hand edit of config.json takes effect at the next game
start or the next settings save. A settings save while config.json could not be read at load (the panel's "not
written" state) sends the panel's own settings, which have no `environment` key, so the switch reads on again until the
next game start. Repointing only happens there - on the game thread, never inside a scan.

The scanner records the original: every read of an effect's own explosion in `SpellScannerEvidence.cpp`
(`HazardSource`, `BuildAppliesJson`, the `explosion` field) and `SpellScannerChips.cpp` (the blast chip) goes through
`Environment::ScannedExplosion(effect)`, which maps a recorded effect's twin back to its original, whatever the switch.
Contract: game thread only. Every caller runs there - the panel scan's form reads (`CollectScan`, a game-thread task;
its worker thread, `PrepareScanText`, reads no form) and Papyrus `RunScan` (it hops to the game thread).

Frostwalker caches an explosion's element by the explosion's base FormID, so the twin gets its own (Unknown) entry and
the original's cold entry stays for the hazard.

### What it does in TAKEALOOK (expected; read with houseCARL 2026-10-20)

Nine magic effects have `Explosion = Skyrim.esm|0x0B7A05` at the winner: `MAG_ScreenShakeEffect`
(`MysticismMagic.esp|0xBDE626`: Script, primary AV None, HideInUI, not hostile or detrimental, 23 carriers - 12 spells,
11 scrolls, see "Why" - all magnitude 0), and eight that keep the original: `MAG_FrostDamageFFHazardArea` (Skyrim.esm),
`MAG_FrostDamageFFHazardAreaDualCast` (MysticismMagic.esp), `SPEPSGrandBlizzardEffect` and `...New` (SPERG-SSE.esp),
`VoiceAlduinMQ206FrostBlizzardEffectSelf` (DragonsUseThuum.esp), `zDcdMgECallWinter` (DAc0da.esm),
`DLC2FrostDamageHazardArea40` (Dragonborn.esm) - all SpawnHazard - and `MAG_FrostDamageFFHazardAreaDummy`
(MysticismMagic.esp, hidden but a ValueModifier on DamageResist). So the log at game start should read, with
HeartOfMagic.esp enabled:

```
EnvironmentTwins: HeartOfMagic.esp|0x000800 (HoM_ShakeBlastTwin) has Skyrim.esm|0x0B7A05's values (Frostwalker lists: installed)
EnvironmentTwins: MysticismMagic.esp|0xBDE626 takes HeartOfMagic.esp|0x000800 (23 carriers, all magnitude 0)
EnvironmentTwins: 9 effects carry an original explosion: 1 take its twin, 8 keep it
EnvironmentTwins: data loaded - environment.enabled on: 1 effects on their twin (1 changed now, 0 were): MysticismMagic.esp|0xBDE626
```

plus eight `... keeps Skyrim.esm|0x0B7A05: archetype is not Script` lines (the Dummy's says the same: it is a
ValueModifier).

## Self-test (dev only)

`src/environment/EnvironmentSelfTest.cpp`, `EnvironmentSelfTestChecks.cpp` and `EnvironmentSelfTestTomes.cpp`. Off unless
`Data/SKSE/Plugins/SpellLearning/selftest.json` holds `{"enabled": true}` (optional `"delaySeconds"`, default 30,
5-600). The file is not shipped: `debug/selftest.json` in the repo, put into the dev install by
`.\DeployDev.ps1 -WithSelfTest` and removed by every deploy without the switch (`-PanelOnly` included). Armed at a save load or a new game,
it starts once per game session after `delaySeconds` of unpaused play with the player in a loaded cell (menus, loading
and pauses are not counted; a second load before the start restarts the wait; given up after 15 minutes). Every line
goes to `SpellLearning.log` (flushed at once) with the prefix `[selftest-1.6]`: `armed`, `START`, one
`<id> PASS|FAIL|SKIP|ANSWER <observed values>` per check, `INFO` lines, and `DONE pass=N fail=N skip=N answer=N`.
FAIL means a defect; the H2 lines are an experiment and write `ANSWER yes|no` instead (counted in `answer=`, not
`fail=`). The H2 check briefly edits Flames' fire effect's keyword array on the game thread - the same kind of
in-memory edit the perk adapters make - and puts it back at once.

| Id | Checks | PASS when |
|---|---|---|
| `H2-record` | Flames (`Skyrim.esm|0x012FCD`): which effect carries `MagicDamageFire` (`Skyrim.esm|0x01CEAD`), whether the SPEL record's own list has it (non-virtual `HasKeywordID`), and the spell's virtual `HasKeyword` | ANSWER yes or no: does the virtual call see it while only an effect carries it (SKIP when the record itself carries it - then only `H2-runtime` decides) |
| `H2-runtime` | adds a throwaway keyword (`ActorTypeDwarven`, else `ActorTypeGiant`, `ActorTypeHorse`: the first on neither Flames nor its effects) to Flames' fire effect, reads the spell's virtual `HasKeyword`, non-virtual `HasKeywordID` and `GetNumKeywords`, takes it off again (also on an exception) | ANSWER yes or no: the virtual call sees it while it is on the effect. FAIL only if the edit did not take or did not come off; SKIP if the spell saw it without the edit. An `H2-answer` INFO line states what was observed, and that whether OIF, Dynamic Candles and Put Out The Fires call the virtual `HasKeyword` on the spell is not observed here (unverified) |
| `twins-shake` | `MAG_ScreenShakeEffect`'s explosion, and what the scan would record | at least one effect recorded; the twin while the switch the DLL applied (game start or last settings save, not config.json as edited since) is on, the original while off; the scan records the original |
| `twins-hazard` | `MAG_FrostDamageFFHazardArea`'s explosion | the original |
| `twins-values` | twin vs original: `BGSExplosionData` (all but the pad), bounds, enchantment, image space modifier, model path, twin name | all equal, name empty |
| `mysticism-patch` | whether `MysticismMagic.esp` and `HeartOfMagic_Mysticism.esp` are loaded, and `MAG_ScreenShakeEffect`'s source files in load order (`TESForm::sourceFiles`; the last, `GetFile(-1)`, is the version the game uses). An INFO line gives the effect's Explosion field | PASS: the last file is `HeartOfMagic_Mysticism.esp` (name compared case-insensitively). FAIL: Mysticism loaded without the patch ("install the optional Mysticism patch"), or another file after it ("load HeartOfMagic_Mysticism.esp after <file>"). SKIP: no `MysticismMagic.esp`. The verdict is `Environment::JudgeMysticismPatch` (`EnvironmentMatch.h`), checked by `librarian-test --self-test` |
| `frostwalker` | Frostwalker's matcher (`EnvironmentMatch.h`) on the installed `Data/SKSE/Plugins/Frostwalker/*Sources/` (every file, as Frostwalker reads them), with EditorIDs from `SpellScanner::GetEditorId` (po3 Tweaks' `GetFormEditorID`, as Frostwalker) | PASS: twin unknown and the original cold. FAIL: the twin reads as an element. SKIP: no Frostwalker folder, or the original is not cold with the installed lists (a user list that makes "blizzard" neutral, or no EditorIDs) - then the twin is not needed. The original's class is always in the line |
| `adapters-<file>` | for `env-elements.json`, `env-oif.json`, `env-candles.json`, `frostwalker.json`: their `requires` against the live config, load order and `Data/SKSE/Plugins` (`requires.dlls`, through `FileApplies`' default `DllInstalled`), and `perk_adapters_report.json` | listed under `files` when they apply, under `filesNotApplying` when not. The config gate is taken as the report's run judged it (`configJudged` in the report, written by the adapters for every `requires.config` path), so a config.json edited since the last game start or full scan is named in the line, not counted as a fault |
| `adapters-desecration` | every effect of every `DAR_Necrotic*` spell (Desecration.esp) | none got `MagicDamageFire` from the adapters (`PerkAdapters::IsInjected`). SKIP without Desecration.esp or EditorIDs |
| `sr-paired-tomes` | the tome pairs for books that teach nothing (Spell Research Synthesizer, `SpellTomes::CurrentStatus`; docs/ARCHITECTURE.md, "Spell tomes that teach nothing"): the counts per source and per skip reason; for every pair `SpellOf(book)`, `TomesBySpell` and the tome reach pass's tome count of this generation (`TomeReach::TomeCount`) | PASS: no JSON id unresolved, every pair a BOOK that teaches nothing and a SPEL, no book twice in the JSON, a SynthesizedPatch.json that exists was usable, `SpellOf` answers every pair's book with its spell, every pair's spell has a tome in `TomesBySpell`, and the pass got every tome `ForEachTome` gives (runtime forms aside). FAIL otherwise (the counts say which). SKIP: no SpellResearch.esp and no JSON. Not a defect of HoM when the JSON names a plugin no longer loaded (`unresolved`): run the patcher again |
| `game-thread` | an INFO line with the first thread id seen in each kind of entry (a task, a Papyrus native, an SKSE message, the inventory event sink) and `RE::Main::threadID` then, plus the ids now (data to judge: which entries the engine runs on the main thread); `IsOnGameThread()` (ThreadUtils.h: the main thread's id or a GameThreadFlag scope) in the self-test's own task and on a new thread | PASS: true in the task, false on the new thread |

Without HeartOfMagic.esp the twin checks write one `twins SKIP` line ("the explosion twin fix is inactive, nothing
checked"); without MysticismMagic.esp `twins-shake` and `mysticism-patch` are a SKIP.

The two H2 lines are an experiment, not a check of HoM's code: `ANSWER no` means "the spell's virtual `HasKeyword`
does not see its effects' keywords" (then env-oif.json / env-candles.json would need spell-record targets, if those
mods call that function - not observed here). Read the `H2-answer` line.

### In-game test for the user (TAKEALOOK)

What the self-test cannot judge is what the water looks like. Steps:

1. Close the game. `.\BuildRelease.ps1 -preset Release-2026`, then `.\DeployDev.ps1 -WithSelfTest`.
2. In MO2, enable `HeartOfMagic.esp` in the dev mod (`HeartOfMagic-Librarian-Dev`; a new plugin starts unchecked). It is
   light: no slot, anywhere in the order. Enable `HeartOfMagic_Mysticism.esp` too and move it below
   `Synthesis - Sound and Combat.esp` (and below any other plugin that edits `MAG_ScreenShakeEffect`).
3. Start the game, load a save near open water (Riverwood's river, or the lake by Helgen), play for 30 seconds
   unpaused, then quit (or keep playing).
4. Read `SpellLearning.log` (SKSE's log folder: `Documents\My Games\Skyrim Special Edition\SKSE\` on the Steam
   runtime): the `EnvironmentTwins:` lines from the game start (expected above) and the `[selftest-1.6]` lines;
   The pass is `DONE ... fail=0` **and** no `twins`, `twins-shake`, `twins-hazard`, `twins-values` or `mysticism-patch`
   line is a SKIP (a SKIP there means HeartOfMagic.esp or Mysticism was not loaded and nothing was checked); the two H2 `ANSWER` lines
   are the finding to report, not a pass or a fail. Claude can read the log and judge it.
5. Only then, by eye (console `player.addspell <id>`, Skyrim.esm ids): cast Hysteria (`7E8DE`) and Harmony (`7E8DB`)
   standing at the water's edge - the water must **not** freeze; cast Blizzard (`7E8E4`) over water - it **must**
   still freeze. Fire Storm (`7A82B`) should not freeze it either. This is the one thing left to
   the eye: whether Frostwalker draws ice is not in any log HoM can read.
6. Deploy again without `-WithSelfTest` (`.\DeployDev.ps1`) to take the self-test file out.

To check the switch: set `"environment": { "enabled": false }` in the install's `config.json`, start the game: the log
says `environment.enabled off: 1 effects on their original`, and Hysteria freezes water again.

## Not verified

- In game: the 2026-10-20 run (DLL of `0c2ec7fc`, HeartOfMagic.esp enabled, no patch) showed the DLL side as designed
  (self-test pass=9 fail=0) and Hysteria, Harmony and Fire Storm still freezing water, which led to the patch. The
  patch itself and the `mysticism-patch` line have not run in game yet: whether the water stays unfrozen with the
  patch loaded last is the user's step 5 above. The repoint count above is read from the records with houseCARL.
- The model path guard (the twin keeps its own model when the live original's path reads as an element) only acts
  when a mod changes the original's model; in TAKEALOOK both are `Effects\FXEmptyObject.nif`, so it is untested there.
- That nothing else in the effect changes with the twin (screen shake, the image space modifier within 1760 units,
  the silent sound): the values are copied from the live original, so none is expected.
- The twin's values are copied from the original once, at `kDataLoaded`; a change to the original later in the
  session would not follow (no mod is known to make one).
- Skyrim VR: not a target. The DLL is built without VR support (`plugins/cmake/commonlibsse.cmake`,
  `ENABLE_SKYRIM_VR OFF`) and neither README nor FOMOD claims VR, so how VR treats an ESL-flagged plugin does not
  arise.
- Frostwalker's projectile path (`GetProjectileType`, which reads the spell and its effects' names and the projectile
  `FrostStormProjectile_AOS`) is not touched by the twin; the shake effect is a Self effect, which launches no
  projectile, so it is not expected to matter.
- The FOMOD (with the Mysticism patch group) was checked as well-formed XML and against the ModConfig 5.0 element order
  by hand, not with the XSD, and not run in a mod manager.

## Adapters: keywords for environment mods (1.6 step 2)

Besides the explosion twin, Heart of Magic gives environment mods the keywords they read, through the perk adapter
engine (rules and measurements: docs/librarian/PERK_ADAPTERS.md, section 6-5). Four adapter files in
`SKSE/Plugins/SpellLearning/librarian/adapters/`, all switched off together by `config.json`
`"environment": { "enabled": false }` (missing: on; no panel toggle yet):

| File | Runs when | Gives | To |
|---|---|---|---|
| `env-elements.json` | one of `RainExtinguishesFires.esp`, `Water Douses Fire.esp`, `Sneak Tools.esp`, `JBMod.esp` (DISDD), `ElementallyReactiveWaters.esp`, `SIMP.esp` loaded, or `IceIsSlick.dll` / `RainExtinguishesFires.dll` in `Data/SKSE/Plugins` (`requires.dlls`: Ice is Slick has no plugin) - not always, since perk mods read the same keywords and the extra keywords change their balance | `MagicDamageFire/Frost/Shock` | every effect of a spell whose own record says it deals that damage (Rain Extinguishes Fires, Water Douses Fire, Sneak Tools, DISDD, Elementally Reactive Waters, Ice is Slick, SIM; perk mods read the same keywords) |
| `env-oif.json` | any of 14 OIF rule-pack plugins | the packs' own keywords (fire, dousing, frost, shock) | the same damage effects |
| `env-candles.json` | Dynamic Candles or Put Out The Fires | `DC_Fire/DC_Frost`, `POF_Fire/POF_Frost` | the same damage effects |
| `frostwalker.json` | `Frostwalker.esp` | `Frostwalker_FrostCloak_Keyword`, `Frostwalker_FireCloak_Keyword` | a frost or fire cloak's own Cloak effect (Frostwalker reads active effects on the actor) |

Shared rules: the spell's element must rest on the record (`tierMin: "manual"`, mgef or manual evidence) - an element
a framework keyword gave by name alone is not spread; fire, frost and shock need the effect's own resistance;
dousing (`waterbearing*`, `DC_Frost`, `POF_Frost`) takes frost, water and air spells. Keywords are only added, in memory,
and taken back on the next run when no longer planned; nothing another mod gave is removed.

What the next steps have to check in game:

- **H2** - the OIF packs ask the hit spell (`attackSource`) `HasKeyword`. CommonLib's `MagicItem` overrides
  `BGSKeywordForm::HasKeyword`, so the spell likely answers for its effects' keywords too, but whether a keyword added
  to an effect at runtime shows there is not confirmed. If it does not, `env-oif.json` needs a spell-record target.
  The self-test's `H2-record` / `H2-runtime` lines and the `H2-answer` line decide it (not run yet).
- That Frostwalker freezes water under a modded frost cloak that got the keyword (and melts ice under a fire cloak):
  `Frostwalker_FrostCloak_Keyword` on Dragonborn's Kastav's ice cloak and CC Unbound Freeze in TAKEALOOK.
- `perk_adapters_report.json` after a game start: the `env-*` files under `files`, or under `filesNotApplying` when
  their plugins are missing or `environment.enabled` is false (the self-test's `adapters-*` lines check this); leaks
  blocked by scrolls and staves (the offline count cannot see them).
