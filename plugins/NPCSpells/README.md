# Heart of Magic - NPC Spells

## What it does

An optional addon to Heart of Magic. At game start it takes the spells that are in your Heart of Magic tree and hands
them to magic-using NPCs, by school and by skill: a novice bandit mage gets a novice spell, a master-level court mage
gets a spread of everything up to master. Factions have tastes (Thalmor lean to shock and fire and never get undead
spells, necromancers lean to undead conjuration, Vigilants lean to sun and light), warriors almost never get any
spell, beasts get none. Nothing is removed from an NPC and nothing is saved in your game: disable the addon and the
spells are gone at the next start. A report file says who got what.

The design and its reasons are in `docs/superpowers/specs/2026-10-07-npc-spells-addon-design.md` (Korean).

## Requirements

- Skyrim SE 1.5.97 and AE (1.6.x / 1.7.x) through Address Library, with SKSE; VR is not supported (the DLL is
  built without VR support, like Heart of Magic's).
- Heart of Magic Librarian 1.5.0 or later, with a scan done and a tree built (the addon reads `spell_catalog.json`
  and `spell_tree.json` from `Data/SKSE/Plugins/SpellLearning/`; with either missing, or a catalog whose `version`
  is not 1, it gives nothing and the report says why). The addon does not check Heart of Magic's version itself:
  1.5.0 is the release its file reading was checked against.
- `HeartOfMagic_NPCSpells.esp` enabled (an ESL-flagged plugin that only holds empty leveled spell lists).
- Not needed: SPID, SkyPatcher. Heart of Magic itself is not changed.

## Files

| File | Role |
|---|---|
| `HeartOfMagic_NPCSpells.esp` (ESL flag) | 921 empty leveled spell lists (LVSP) with fixed FormIDs; the DLL fills them |
| `SKSE/Plugins/HeartOfMagic_NPCSpells.dll` | The distributor |
| `SKSE/Plugins/HeartOfMagic_NPCSpells.json` | The rules file (editable, see below) |
| `SKSE/Plugins/HeartOfMagic_NPCSpells_report.txt` | Written on each run: counts, every list, every NPC that got lists, timings. Not in the archive |
| `HeartOfMagic_NPCSpells.log` (SKSE log folder) | The addon's log |
| `SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json` | Dev only, never in the archive: turns on the in-game self test (see "Self test") |
| `SKSE/Plugins/HeartOfMagic_NPCSpells/` | `LICENSE`, `THIRD-PARTY-NOTICES.md` (the addon's own, from `plugins/NPCSpells/THIRD-PARTY-NOTICES.md.in`; source tag `npcspells-v<version>`) and `licenses/` (only the libraries this DLL links: CommonLibSSE-NG with its GPL and exceptions, fmt, spdlog, nlohmann-json, zlib, Xbyak, DirectXMath, DirectXTK). Kept out of Data's top level; nothing reads this folder |
| `dist/HeartOfMagic-NPCSpells_<NPCSPELLS_VERSION>.zip` | The release archive (separate from the Heart of Magic zip, `plugins/cmake/DistributionNpcSpells.cmake`). No FOMOD: the zip's root is the Data folder (the rows above, no wrapping folder), so a mod manager installs it as it is. The PDB goes to `..._DebugSymbols.zip`, rooted at Data the same way (`SKSE/Plugins/HeartOfMagic_NPCSpells.pdb`) |
| `plugins/NPCSpells/VERSION` | The addon's own version (1.0.0), apart from Heart of Magic's `VERSION`: the DLL's version, the report's `Version:` line and the zip name |

## How lists are made

The engine, not the addon, picks the spells. A leveled spell list (LVSP) on an NPC's spell list is resolved when the
actor enters the high process: for each of the five schools the engine takes the NPC's current skill in that school as
the "level" and picks one spell among the entries whose level is at most that skill (the highest level group). The
result is not saved; the engine rolls again when the actor enters high process again. So the addon builds the lists
ahead of time (at data load) and attaches them to NPC base records.

- **Lists:** one per school x group x variant x rung (a "slot", 307 in all); the ESL has room for 3 chunks per slot.
  A list holds one school only, entries sorted by level; a spell's level is the skill floor of its tier (novice 0,
  apprentice 25, adept 50, expert 75, master 100). A list is split into chunks only past 255 entries (the engine's
  limit); a tier with fewer spells than there are chunks is copied into every chunk. An NPC draws one chunk.
- **One spell per attached list.** More spells for a higher skill therefore means more lists. The ladder, per school,
  for a mage-class NPC:

  | Rung | Starts at tier | Chance | "below" list |
  |---|---|---|---|
  | R1 | novice | 1.0 | no |
  | R2 | apprentice | 0.25 | yes |
  | R3 | adept | 0.25 | yes |
  | R4 | expert | 0.5 | yes |
  | R5 | master | 0.5 | no |

  Expected spells per school by the NPC's skill tier: novice 1.0, apprentice 1.25, adept 1.5, expert 2.0, master 2.5.
  Hybrid classes (spell-sword and the like) get half, in the schools their class weights only.
- **"Below" lists** carry the engine's "all levels" flag. In them a spell of tier t sits at the floor of tier
  `max(t + 1, rung start tier)`, and master spells are left out. The engine stops at an entry above the NPC's skill,
  so an NPC of the rung's start tier draws only spells of lower tiers. The spread over lower tiers is per spell, not
  per tier (a tier with more spells is drawn more often). Other lists (flag 0) give the NPC's own tier or lower. As
  one list gives one spell, the own-tier share is approximate: 1.0 / .8 / .67 / .5 / .6 for novice to master, not an
  exact half.
- **Variants:** All (every spell of the school), Prefer (the group's `prefer` tags, `never` tags removed), NecroPrefer
  (undead-rule spells only: element `undead` with technique summoning or control), NoUndeadAll (no undead-rule spell).
  A faction NPC gets Prefer lists with a 3/4 dice and All lists otherwise; the `never` tags apply to both. A variant
  list (Prefer, NecroPrefer, NoUndeadAll) that has no spell of a tier its All list (same school, group and rung) holds
  takes the All list's spells of that tier, so no rung gives nothing to an NPC the All list would serve. What the
  variant never holds stays out: NoUndeadAll, and a Mixed group's conjuration Prefer, get no undead-rule spell from it.
  The report counts these lists ("variant lists topped up from All").
- **Dice:** deterministic per NPC and salt: FNV-1a 64 plus the fmix64 finalizer, so the same NPC gets the same lists
  on every start. The seed is the defining plugin name and the local FormID, not the runtime FormID. Deterministic per
  machine: a non-ASCII plugin name is hashed in the bytes the game gives, which follow the system code page, so another
  machine with another code page can roll differently for that plugin's NPCs (ASCII names roll the same everywhere).
- **Who gets what:** the NPC's class (from its skill weights) decides: mage (magic weights at least 0.6 of all),
  hybrid (at least 0.3), non-mage. A non-mage gets, with 5 %, a list of novice curing/armor spells. Beast races get
  nothing; other creatures get lists only with a mage class. NPCs that inherit their spell list from a template are
  skipped (the lists go on the record that really uses it); leveled NPCs resolve to their end record. Deleted or
  ignored NPC records, and character-creation face presets, are skipped.
- **Conjuration:** a necromancer (Necromancer or Vampire group, or class `CombatMageNecro` and not a Redguard) mostly
  gets NecroPrefer lists (3/4), All lists otherwise. A conjurer (conjuration weight 3 or more and not below any other
  school, not a Redguard) in no group gets NoUndeadAll mostly, All otherwise; if its NoUndeadAll list is missing it
  falls back to All. Any other NPC outside the Thalmor and Vigilant groups, in no group (for example a Redguard mage),
  gets NoUndeadAll only. Such an NPC in a Mixed group (Forsworn) gets the group's conjuration Prefer list (undead-rule
  spells removed) with the 3/4 dice and NoUndeadAll otherwise, never All; a missing Prefer list falls back to
  NoUndeadAll, and with both missing that rung gives nothing (counted). Thalmor and Vigilant NPCs get the group's
  Prefer lists plus its All lists; their undead spells are removed by the group's `never: undead` tag, not by a list
  variant, and the report shows them as "no undead" whatever their class.
- **Redguards:** a Redguard whose class is below hybrid (non-mage tier) gets nothing, not even the 5 % list. A hybrid Redguard in the Vampire or
  Necromancer group keeps only its conjuration share (undead allowed through the group); a hybrid Redguard in no such
  group gets nothing. A mage Redguard gets lists, and no undead spells unless the Vampire or Necromancer group allows them.
- **Undead rule:** undead-rule spells go to necromancers (above), to conjurers that are not Redguards, and to Falion
  (`0135E9:Skyrim.esm`, a Redguard conjuration trainer and the one named exception).
- **Group priority:** an NPC in several groups gets the first group's lists in the order Forsworn, Vampire,
  Necromancer, Thalmor, Vigilant; the other groups' `never` tags are not applied (counted as "multi-group" in the report).
- **Excluded:** only tree nodes are used (one representative per node, not its merged copies); tags `cloak`, `trap`,
  `sense`; spells in `excludeSpells`; spells another mod's `*_DISTR.ini` names.

## Rules file

`HeartOfMagic_NPCSpells.json` ships these keys, and no others are read: `ladder` (exactly 5 rungs, each
with `startTier`, `below`, `chance`; the expected spells per tier follow from it, there is no separate target),
`hybridScale`, `nonMageChance`, `nonMageTags`, `classTiers`, `conjuration`
(`necromancerClasses`, `share`), `excludeTags`, `excludeSpells`, `beastRaces`, `groups` (`name`, `factions`, `prefer`,
`preferShare`, `never`), `undead.exceptNpcs`, `redguard.race`, `keywords` (`npc`, `creature`, `animal`).

- Every record is a FormID written `XXXXXX:Plugin.ext`, mod records too. A bare EditorID is rejected and logged: the
  game gives no EditorID without powerofthree's Tweaks, and mods rename them. A ref the load order does not have
  drops that ref only (warned once in the log); the rest of its group stays. Plugin names are handled as UTF-8.
- `classTiers` shares are compared in tenths: `mageMinMagicShare` and `hybridMinMagicShare` are rounded to the nearest
  0.1 (0.64 reads as 0.6), then the class's magic weight sum x 10 is compared with its total weight x that many tenths.
- `excludeSpells` uses the catalog's exact form `Plugin.esp|0xNNNN` (exact string match).
- Group names are fixed (Forsworn, Vampire, Necromancer, Thalmor, Vigilant); a group with another name is ignored.
- A broken file (bad JSON, wrong types, not 5 rungs) falls back to the built-in defaults; the report header says so.
- Smaller problems keep the rest of the file: an entry of a FormID list that is not a string or not a FormID drops
  that entry only; a `chance`, `share`, `preferShare`, `hybridScale`, `nonMageChance` or class share outside 0..1
  goes back to its default (not clamped), and so do both class shares when `hybridMinMagicShare` is above
  `mageMinMagicShare`. Every such problem is named, `; ` between them, in the log (a warning) and on the report's
  `Rules error:` line.
- Fixed values, not in the file: skill bands 0/25/50/75/100 (the engine's), the own-tier share (it follows from the
  ladder), which groups count as necromancers (Vampire, Necromancer), the conjurer's non-undead preference, the
  conjurer classes (found from class weights), and the Redguard rule.

## Report

`HeartOfMagic_NPCSpells_report.txt` is always written, also when the run stops (then with a `Stopped:` line and the
reason; a missing or unreadable `spell_tree.json` or `spell_catalog.json` is named with its path). Sections: header
(version, time, rules source); Spells (pool counts: tree nodes, duplicate persistentId - a spell named by more than
one node, taken once so its weight is not doubled -, excluded by tag, kept...);
Lists (lists built, empty slots, dropped over capacity, ESL records found/missing/wrong type, per school, every list
with its entry count and flag); NPCs (scanned, eligible, got lists, by class tier, by group, skipped with reasons);
NPC lists (one row for every NPC that got lists: persistent id `Plugin|0xLocal`, name, group, class tier and undead
policy, the attached list EditorIDs `HoMNS_...`); Timing. NPC names are converted to UTF-8 from the system code page.
The Lists section also counts the variant lists topped up from All and `skippedPrefilled`: ESL records whose entry
count was not 0 when the addon came to fill them (another plugin's override). Those are left as they were and get no
NPC; the first one is named in the log. A record with count 0 is filled even when the engine left an entry pointer on
it (it does on these LLCT-0 records): the addon gives it a new entry array (headless, as the engine allocates a
array of plain entries) and never frees or reads the old pointer. If the game heap gives no memory for that array the
list is left empty and counted as `skippedNoMemory` (logged as an error), apart from the lists skipped for a
missing record.

## Known limits

- **Not yet checked in game: NPCs that inherit their spell list from a template**, which is most leveled bandit
  mages. The addon puts the lists on the record that really holds the spell list (the template's end), never on
  the inheriting NPC; whether the spawned actor then has the spells is not confirmed. The dev self test below
  checks it on `039D37:Skyrim.esm` (a bandit mage inheriting from `039D31`); it, or checklist item 10, must pass
  before release.
- The same spell can be given again when the NPC already knows it; harmless.
- An NPC in two groups gets the first group's lists (see Group priority).
- Vigilant `never: undead` also removes Turn Undead (the `undead` tag covers repelling). A spell that commands undead
  but is an Illusion spell is treated as an ordinary Illusion spell (undead-rule spells are `undead` with
  summoning or control).
- A spell another mod's `*_DISTR.ini` names is left out wherever the name appears in the file, also in a filter, so a
  few too many are skipped. Only files named `*_DISTR.ini` at Data's top level are read (where SPID reads them;
  `TomeReach::TopLevelConfigs`, the shared tome-reach config code); names are read as UTF-8 and a bad entry is
  skipped alone.
- Another plugin that overrides this ESL's leveled lists is not supported.
- Each filled list leaves the small block the engine had pointed it at (about 338 blocks, once per game start): its
  origin is unknown, so freeing it could damage the game heap.
- The engine re-rolls each time an actor enters high process, so the same NPC can show different spells over a session.
- Measured numbers (offline on the 1.5.0 files, and the game's report): the spec, section 8. The in-game checklist below is only partly done.

## Development

```powershell
.\BuildRelease.ps1 -preset Release-2026                  # DLL, npcspells-test, npcspells-esl and the ESL
.\build\tools\Release\npcspells-test.exe                 # all suites, exit 1 on a failure
.\build\tools\Release\npcspells-test.exe --tree <spell_tree.json> --catalog <spell_catalog.json>   # statistics on real files, lowest entry level per variant slot vs its All slot
.\build\tools\Release\npcspells-esl.exe -o <path>        # write the ESL from the slot table
.\DeployDev.ps1 -NpcSpellsOnly                           # DLL, ESL, rules JSON -> D:\TAKEALOOK\mods\HeartOfMagic-NPCSpells-Dev (folder must exist; stale build refused; an existing rules JSON is kept)
.\DeployDev.ps1 -NpcSpellsOnly -Force                    # the same, the install's rules JSON replaced with the shipped one
.\DeployDev.ps1 -NpcSpellsOnly -NoSelfTest               # the same without the dev self test (its file removed from the install)
```

## Self test (dev only)

An in-game check that needs no console work: launch, load a **disposable** save (or start a new game), wait about
10 seconds, quit, then read the log. It runs only when `Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json`
exists. That file is `plugins/NPCSpells/data/dev/HeartOfMagic_NPCSpells_selftest.json` in the repo; `DeployDev.ps1
-NpcSpellsOnly` copies it into the dev install (and says so), `-NoSelfTest` removes it. It is never in the release
zip (`DistributionNpcSpells.cmake` copies files by name and does not name it). Without the file the DLL does one
file-exists check per load and nothing else.

```json
{ "samples": ["039D37:Skyrim.esm", "013BBB:Skyrim.esm", "0551AD:Skyrim.esm"], "startDelayMs": 2000, "maxWaitMs": 8000 }
```

Samples are base NPC records in the rules file's form (`XXXXXX:Plugin.ext`; the report's `Plugin|0x...` form is
refused and logged). The shipped three: `039D37` a bandit mage that inherits its spell list from `039D31` (the ship
gate), `013BBB` Farengar (own spell list, the 2026-10-10 control), `0551AD` a Necromancer-group mage with
NecroPrefer lists. `startDelayMs` (wait after the player's 3D and cell are in) and `maxWaitMs` (per spawned actor)
are 0..60000; a bad value goes back to its default, a bad sample is dropped alone, both logged.

What it does, once per session, at the first save load or new game (`kPostLoadGame` / `kNewGame`):

1. Finds each sample's record and the record that really holds its spell list (`SpellListHolder`, the template
   walk `AttachToNpcs` uses). A sample whose holder has none of our lists fails without a spawn.
2. Spawns each sample at the player (`PlaceObjectAtMe`). **Side effect: for about a second hostile NPCs (bandits,
   necromancers) stand next to the player and may attack.** Use a disposable save. They are disabled and deleted
   right after the check (`Disable`, `SetDelete(true)`), also on a timeout or an error.
3. Waits until the actor has its 3D and a high process (the leveled spell cache needs the high process; before 3D
   every `HasSpell` was false on 10-10), polled every 100 ms by queued main-thread tasks.
4. Per list of ours on the holder: the witness spells are the list's spells minus every spell the NPC can have
   another way: the static spells and the other (vanilla) leveled lists' entries of the sample, its holder and the
   spawned actor's base, the race's spells and the actor's added spells. So a spell vanilla also hands out (Flames in
   `OREO_LSpellBandit01FireFrostShock`) never counts as proof. It asks `Actor::HasSpell` for each witness.

`Actor::HasSpell` covers leveled results: on 1.6.1170 it (ID 38782) is a visitor over `Actor::VisitSpells` (38781),
which walks the added spells, the actor's base record's spells, the race's spells and then, for a non-player actor
with an AI process, `GetLeveledSpells` (39443) - the high process's leveled spell cache, built once from the
**actor's own base record's** leveled lists (schools rolled by current skill). Disassembly notes: `lab/addon-research/
npc-spells-ledger.md` (main checkout, not in git) and the 10-10 Case 161 check.

Log lines (`HeartOfMagic_NPCSpells.log`, all start with `SELFTEST`, the values below are an example; the same lines are appended to the report under
`== Self test ==`):

```
SELFTEST start samples=3 startDelayMs=2000 maxWaitMs=8000 esl=loaded
SELFTEST spawn sample 039D37:Skyrim.esm ref 0xFF0012AB
SELFTEST sample 039D37:Skyrim.esm base Skyrim.esm|0x039D37 holder Skyrim.esm|0x039D31 inherited=yes actorBase 0xFF0012AC holderLists=9 actorBaseLists=9 3D=loaded high=yes waitedMs=300
SELFTEST list 039D37:Skyrim.esm HoMNS_Destruction_None_All_R1_C0 school=destruction skill=35 entries=120 minLevel=0 witnesses=118 shared=90 hits=1 hitSpells: 0x... Name
SELFTEST school 039D37:Skyrim.esm destruction lists=2 distinctHits=1
SELFTEST RESULT sample 039D37:Skyrim.esm PASS (hits on HoMNS-only witnesses: 4)
SELFTEST DONE pass=3 fail=0
```

How to read them:

- `RESULT ... PASS` needs at least one hit on a witness spell. `FAIL (reason)` names why when nothing was checked
  (record not found, no list on the holder, no 3D or high process in time, an error).
- `actorBaseLists` is the discriminator for the ship gate: the engine reads the leveled lists of the spawned actor's
  own base. `holderLists>0` with `actorBaseLists=0` means the inheriting NPC's copy did not get our lists (they were
  attached to the template after the engine copied it), so no roll can ever give them. `actorBaseLists` equal to
  `holderLists` with no hit means the lists are there and the roll gave nothing.
- `hits` per list cannot be read as "this list gave one": lists of one school overlap (`shared` = witnesses another
  list of ours on the NPC also holds), and the engine merges the picks. The one-spell-per-list check is the
  `school` line: `distinctHits` against `lists`.
- `skill` is the actor's current skill in the list's school (what the engine rolls with); `(skill below every
  entry)` after `minLevel` marks a list that can legitimately give nothing. An R1 list (minLevel 0) giving 0 is not
  such a case (it is the open "Alteration lists give nothing" question when it is an Alteration list).

**FormID stability:** the ESL FormID of a list is `0x800 + slotIndex * kMaxChunks + chunk`. `kMaxChunks` and the slot
order are frozen: a new slot may only be appended. Changing them renumbers every list; that is harmless only because
the ESL and the DLL ship together and nothing saved refers to the lists.

## In-game check (TAKEALOOK dev install, More Informative Console installed)

Partly done (2026-10-10, the pre-1.5.0 build of the addon on TAKEALOOK): 338 lists filled, 2,468 NPCs given lists,
`skippedPrefilled 0`; Farengar's `Actor.HasSpell` was true for spells only our lists hold, so the engine does resolve
them (the console and MIC spell lists do not show leveled results). Still open: an Alteration R1 list on Farengar
gave no `HasSpell` hit in two sweeps (107 of 118 spells checked), and one spell per list is not pinned (step 4).
The offline pool on the 1.5.0 files gives Alteration lists normally (`npcspells-test --tree`), so that doubt is on
the engine side. Enable `HeartOfMagic_NPCSpells.esp` in MO2, start to the main menu, then:

1. Open `HeartOfMagic_NPCSpells.log`: expect "ESL records loaded=true found=921 missing=0 wrongType=0", the pool
   line, the list count, the NPC counts and "done in N ms total" (expected well under 500 ms).
   The list line must say `skippedPrefilled=0` (the first run said 338: every list was skipped, fixed since). The
   `input steps:` line times each input step (tree and catalog read and parse, rules, SPID config search with the
   files found in Data and under SKSE/Plugins, SPID config reading): the first run took 2847 ms reading inputs,
   2926 ms of it the SPID config search listing Data's whole USVFS-merged top level. The top level is now read
   with an OS wildcard (`*_distr.ini` only, `TomeReach::TopLevelConfigs`), so expect that step well under 100 ms;
   the next line splits the search into "Data top level" and "SKSE/Plugins" times. SKSE/Plugins is no longer
   searched (SPID reads `_DISTR.ini` only from Data's top level; the walk took 1400 ms under USVFS and found none),
   so its figure is always "0 ms (skipped)" and "0 under SKSE/Plugins".
   Then confirm that a `HoMNS_` list really gives spells. With the dev self test on (above) this is its
   `SELFTEST RESULT` lines and needs no console; by hand: after loading a save (step 3), select a bandit mage and
   check `Actor.HasSpell` (as the 2026-10-10 Farengar check did) for a spell only our lists hold (one
   the report lists for that NPC's lists and its vanilla spell list lacks). The console's and MIC's spell lists do
   not show what a leveled list gave, so they are not the check; a false result for every such spell means the
   fill did not land.
2. Open the report: no `Stopped:` line, dropped over capacity 0, `skippedPrefilled 0`.
3. Load a save. For each NPC below open the console on the actor and, in More Informative Console, the base record's
   Spells: it must list leveled lists named `HoMNS_<School>_<Group>_<Variant>_R<n>_C<n>` from our ESL (the report's
   NPC lists row names them too). The spells they resolve to must be of the NPC's tier in that school or lower
   (compare with its skills in MIC) and match the group rule; check them with `HasSpell` on the selected actor, not
   with MIC's spell list, which does not show leveled results.
   How to find and inspect actors: `help "<name>" 4` in the console lists the matching records with their base IDs;
   click an actor in the world to select it (its name shows in the console) and inspect its base record in More
   Informative Console; `player.placeatme <baseID>` spawns a test actor next to you. For a leveled NPC (item 10) and for
   bandit mages, `LvlBanditWizard` (base `01E79F:Skyrim.esm` is the leveled record to spawn; load-order prefix in the
   console differs, so confirm with `help "LvlBanditWizard" 4`) is a good start: spawn it and inspect the spawned
   actor's base record, which is the end record, not the leveled one.
   1. Bandit mage (`EncBandit01MagicF` etc., class `EncClassBanditWizard`): R1 lists for every school, group None;
      conjuration NoUndeadAll or NecroPrefer/All per its class; no `cloak`, `trap` or `sense` spell.
   2. Thalmor wizard: `..._Thalmor_...` lists; no undead spell, no conjured undead.
   3. Necromancer (Necromancer faction, e.g. a warlock in Rannveig's Fast): `..._Necromancer_...`; conjuration from
      NecroPrefer/All; no `sun` spell.
   4. Vampire (e.g. Movarth's lair): `..._Vampire_...`; no `sun`, `light` or `curing` spell.
   5. Redguard warrior (a Redguard mercenary, an Alik'r warrior): no `HoMNS_` list at all.
   6. Redguard mage (not a vampire): lists present; conjuration only NoUndeadAll.
   7. Falion (Morthal): conjuration NoUndeadAll/All (conjurer), undead allowed.
   8. Hagraven: lists only if its class is a mage tier (the report row says; no row means no lists).
   9. Wolf: no lists.
   10. **Release condition.** A `LvlBanditWizard...` leveled NPC placed with `player.placeatme` (its end record
       inherits its spell list from a template, as most leveled bandit mages do): `HasSpell` on the spawned actor must
       be true for at least one spell only our lists hold (the report row of the template record that holds the
       lists names them); the leveled record itself gets no list. Not yet done. The self test's `039D37` sample
       checks the same thing on the end record a `LvlBanditWizard` draws (`PASS` there, with `actorBaseLists` above
       0, closes this item).
   11. Non-mage sanity: among about 20 guards or bandits (`player.placeatme` a few `EncBandit01Melee...`), about one
       has a `..._NonMage_...` list.
   12. Save and reload: the lists are still on the base records (re-added at load), no crash. Disable the addon and
       load the same save: no leftover spells on NPCs you did not meet, no missing-form warnings beyond the ESL itself.
4. Pin the one-spell-per-list assumption: a bandit mage with N attached lists in a school should show at most N spells
   of that school after entering high process. The self test's `SELFTEST school ... lists=N distinctHits=D` lines
   give this per school and sample (D at most N), with the actor's skill on each list line.
5. Write the numbers (NPCs given lists, lists, biggest list, ms) into spec sections 8 and 9.
