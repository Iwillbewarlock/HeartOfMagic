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
| `SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json` | Dev only, never in the archive, deployed only with `DeployDev.ps1 -NpcSpellsOnly -WithSelfTest`: turns on the in-game self test (see "Self test") |
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
  limit); a tier with fewer spells than there are chunks is copied into every chunk first, and the other spells are
  dealt round-robin. The chunk count is the smallest from ceil(spells / 255) that holds those copies too (so 510
  spells with one master spell take 3 chunks, not 2); when even 3 chunks cannot hold them, the surplus is left out
  and counted as dropped over capacity. An NPC draws one chunk.
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
  nothing; other creatures get lists only with a mage class. The template
  flag for spells counts only with a template set: a record with the flag and no template keeps its own spell list
  and gets lists like any other, and so does such a record at the end of another NPC's template chain (it is that
  NPC's holder, below). Deleted or
  ignored NPC records, and character-creation face presets, are skipped.
- **NPCs that inherit their spell list from a template** (most bandit and warlock mages) get exactly the lists decided
  for their spell list holder: the first record of the template chain whose Spells template flag is off, the one that
  really holds the list (not the end of the chain: a holder can have a template of its own for other things), or the
  chain's last record when it has the flag but no template of its own (it keeps its own list; counted as
  `holderFlagNoTemplate`; a chain cut by a template cycle stays unresolvable). No dice
  of their own, so a template and all its children behave alike, and a holder that gets nothing (skipped, a non-mage
  whose 5 % roll failed) gives its children nothing. Only our lists are copied, never the holder's other entries. The
  engine copies the holder's spell list into each child while loading (`kCopiedTemplate`), before the addon runs, so
  lists put on the holder alone never reached a spawned child (in-game run 6, 2026-10-15). **Pending in-game check:**
  that a child now gets them is still to be confirmed in game (Known limits). The child's own race still counts: a
  child of a beast race gets nothing, and a creature child only with a mage class of its own (its race and class read
  from its own chain, as for an NPC that decides for itself; a child whose race or class cannot be read before spawn,
  or that has no race, keeps its holder's lists), unless it shares its spell list data with its template (below: the
  template's write reaches it, counted as `viaSharedDespiteOwnSkip`). Every holder is decided first and every record is written only after the whole scan (the form array is in
  load order, not template order); who is written and how is one plan (`PlanWrites`, `src/core/NpcSpellsPlan.h`,
  tested offline). A child with no spell list data of its own gets one made for it only when it has
  `kCopiedTemplate` (the engine copied an empty holder into it, so the holder's lists go into a new one); without the
  flag it is skipped. A child whose spell list data is the same as a chain record's (expected 0) is not written: the
  write to that record reaches it, and it is counted apart (`inheritedViaShared`). A chain with a leveled link before
  the holder cannot be resolved before spawn and is skipped ("template spell list, holder not resolvable"); the
  leveled NPC's end record is a record like any other and gets its own or its holder's lists.
- **Memory audit:** adding lists frees the record's old leveled-list array. Before anything is written, every NPC record
  of the load order (deleted ones, face presets and the player included) is read once, and a record whose array another
  record's distinct spell list data also points at is never written (`sharedLevSpells`, `blockedWrites`, expected 0;
  a warning names the first five); its template children are skipped with their own reason. Spell list data shared as
  a whole is memory-safe (one struct, one array), but each record the plan writes adds its lists to that one struct,
  so every record sharing it gets all of them: within one template chain a child sharing its template's data is not
  written itself (`inheritedViaShared`); records outside one template chain sharing one (two NPCs deciding for
  themselves would merge their lists there) are counted as `sharedSpellDataOutsideChain` (structs, expected 0). Race
  spell lists are not read: they are assumed not to share spell list data or arrays with NPCs.
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
- A broken file (bad JSON, a wrong-typed value such as a string where a number or a tag list belongs, a rung that is
  not an object, an unknown `startTier`, not 5 rungs) falls back to the built-in defaults; the report header says so.
  Tier names are read in any case (`"Novice"` is novice).
- Smaller problems keep the rest of the file: a section or list that is there but not the array or object it should
  be (`"keywords": [...]`, `"beastRaces": "..."`) keeps that part's defaults; an entry of a FormID list that is not a
  string or not a FormID drops that entry only; a `chance`, `share`, `preferShare`, `hybridScale`, `nonMageChance` or class share outside 0..1
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
the game got with its entry count and flag - a list skipped for a missing record, a prefilled record or no memory is
only counted); NPCs (scanned, eligible, got lists, unresolvable template, multi-group, by class tier, by group - these
count the NPCs that decided for themselves; an inheriting NPC, its holder resolved or not, is in none of them -,
skipped with reasons, a line for the template children: how many inherit their spell list (an unresolvable chain
included), how many got their holder's lists and how many lists, of them how many got new spell list data, how many
got them through shared spell list data (and of those, how many despite their own race rule), how many lack
`kCopiedTemplate` with and without a leveled link in their chain, how many came before their holder in the form array,
how many take class, race or factions from another record than the holder, how many share spell list data with a chain
record, how many have a holder at the chain's end with the Spells flag and no template; and the memory audit's line:
records whose leveled-list array another record's spell list data shares, the writes left out for it, and spell list
data shared outside one template chain, all expected 0);
NPC lists (one row for every NPC that decided for itself and got lists: persistent id `Plugin|0xLocal`, name, group,
class tier and undead policy, the attached list EditorIDs `HoMNS_...`); NPC lists inherited from a template (one short
row per child: `Plugin|0xLocal  Name  inherited from Plugin|0xHolder  (N lists)`; the holder's row above names the
lists); Timing. NPC names are converted to UTF-8 from the system code page.
Skip reasons for template children: `template spell list, holder not resolvable` (a leveled link before the holder),
`template spell list, holder got no lists` (skipped, no list, or skipped before its decision), `template spell list,
holder not written (shared leveled-list array)`, `template spell list, beast race`, `template spell list, creature not
a mage`, `template spell list, no spell list data and no kCopiedTemplate`, `template spell list, spell list data
shared with a chain record that gets no lists`, `template spell list, spell list data shared with a chain record not
written (shared leveled-list array)` (a chain record other than the holder), and for a child whose own leveled-list
array is shared, `template spell list, leveled-list array shared (not written)`; for a record deciding for itself,
`leveled-list array shared with another record's spell list data (not written)`. The log has the same counters on the
`NPCs in N ms:` line (`inheritedAttached= inheritedLists= inheritedCreated= inheritedViaShared=
viaSharedDespiteOwnSkip=`) and on `template spell list: inheriting= notCopiedTemplate leveledChain= plainChain=
beforeHolder= otherProvider= sharedSpellData= holderFlagNoTemplate= sharedLevSpells= blockedWrites=
sharedSpellDataOutsideChain=`, read at data load with no game needed; a `blockedWrites` warning (`NPCSpells: <FormID>
not given lists: its leveled-list array is shared with another record's spell list data`) names each of the first
five.
The Lists section also counts the variant lists topped up from All and `skippedPrefilled`: ESL records whose entry
count was not 0 when the addon came to fill them (another plugin's override). Those are left as they were and get no
NPC; the first one is named in the log. A record with count 0 is filled even when the engine left an entry pointer on
it (it does on these LLCT-0 records): the addon gives it a new entry array (headless, as the engine allocates a
array of plain entries) and never frees or reads the old pointer. If the game heap gives no memory for that array the
list is left empty and counted as `skippedNoMemory` (logged as an error), apart from the lists skipped for a
missing record.

## Known limits

- **Template children: the fix is not yet confirmed in game.** Lists on the holder alone never reached a spawned
  child (run 6: `039D37` FAIL, holder `039D31` with 9 lists, the child's base with none); children now get the holder's
  lists themselves. The self test's `039D37` must now show `actorBaseLists>0` and `PASS`. Not known from the headers,
  to be watched: whether the engine copies a template's list again later (a save load, a spawn), which would replace
  ours on the child, and how a leveled spawn's temporary base takes its list.
- **A child of a holder with no spell list** (`2750FE:3DNPC.esp`, holder `0728AD:Skyrim.esm`, the self test's fourth
  sample) gets spell list data made for it when it has `kCopiedTemplate`: whether the engine leaves it there or copies
  the empty holder again at spawn is the pending check (`actorBaseLists>0` and `PASS`). A child without the flag and
  without spell list data is skipped (counted).
- A child that shares its spell list data with a chain record gets whatever that record gets (counted as
  `inheritedViaShared`, no report row), even when its own race rule would give it nothing (`viaSharedDespiteOwnSkip`);
  expected 0 in practice.
- A child's own race rules read its Traits (race) and Stats (class) providers. When either is behind a leveled link
  (unresolvable before spawn), or the child has no race, its own rules are not applied and it keeps its holder's
  decision: the record's own race or class is not read in their place.
- **The leveled spawn path is checked from outside**, with the console through DevBench: the self test cannot spawn a
  leveled NPC (`PlaceObjectAtMe` gave no actor for `039D5F`, 2026-10-15), so it no longer has a leveled sample.
  Checklist item 10 stays the release condition.
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
.\DeployDev.ps1 -NpcSpellsOnly -WithSelfTest             # the same plus the dev self test file (every deploy without the switch removes it from the install)
```

## Self test (dev only)

An in-game check that needs no console work. The run:

1. `.\DeployDev.ps1 -NpcSpellsOnly -WithSelfTest` (it reminds you to take the file out afterwards).
2. Launch and load a **disposable** save (not a new game: the test never starts on one, it would spawn hostile NPCs
   into the opening scene).
3. Wait for the log's `SELFTEST DONE` line (about 10 to 20 seconds with the shipped file: repeated samples spawn one
   after another, the samples side by side; at worst `startDelayMs` plus the most repeated sample's repeat x
   `maxWaitMs`, 2 + 5 x 8 = 42 seconds of play with the shipped file, counted only while the game runs; a `SELFTEST RESTART ...` line instead
   means the test ended before its spawn and the next save load runs it). Until then **do not save, die,
   reload or start a new game**: a save holds the spawned actors (logged as `SELFTEST WARN save taken while sample
   actors exist`), and a load or a new game ends the test with its open samples failed.
4. Quit, read the log (or the report's `== Self test ==` section).
5. `.\DeployDev.ps1 -NpcSpellsOnly` without the switch removes the file from the install.

It runs only when `Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json` exists. That file is
`plugins/NPCSpells/data/dev/HeartOfMagic_NPCSpells_selftest.json` in the repo. It is opt-in: only `DeployDev.ps1
-NpcSpellsOnly -WithSelfTest` copies it into the dev install; every other deploy (`-NpcSpellsOnly` without the switch,
or the plain one) removes it from the NPC Spells install and says so, because the dev mod is enabled in a profile used
for real play; a deploy that stops before copying says whether the file is still installed. It is never in the release
zip (`DistributionNpcSpells.cmake` copies files by name and does not name it); the code is in the DLL but idle without
the file, which costs one file-exists check per load.

```json
{ "samples": ["039D37:Skyrim.esm", { "id": "013BBB:Skyrim.esm", "repeat": 5 }, "0551AD:Skyrim.esm", "2750FE:3DNPC.esp"], "startDelayMs": 2000, "maxWaitMs": 8000 }
```

Samples are base NPC records (NPC_) in the rules file's form (`XXXXXX:Plugin.ext`; the report's `Plugin|0x...` form
is refused and logged), each a string or an object `{"id": "...", "repeat": N}` (another key in the object is named in
the `SELFTEST config:` warning and ignored). The shipped four: `039D37` a bandit mage that inherits its spell list
from `039D31` (the template fix: it must now `PASS` with `actorBaseLists>0`), `013BBB` Farengar (own spell list, the
2026-10-10 control) spawned 5 times, `0551AD` a Necromancer-group mage with NecroPrefer lists, and `2750FE:3DNPC.esp`
(Interesting NPCs' Thalmor wizard, class `072887` ThalmorWizard): it inherits its spell list from
`0728AD:Skyrim.esm` `EncThalmor00WizardTemplate`, a mage-class Thalmor holder with no spells of its own (no SPLO or
LVSP; the report gives it Thalmor lists), so the child's copy of the holder's list is empty (the new-spell-list-data
case above; found with houseCARL on the TAKEALOOK load order, 2026-10-15). `startDelayMs` (wait after the player's 3D
and cell are in) and `maxWaitMs` (per spawned actor) are 0..60000; `repeat` is 1..10 (default 1); a bad value goes
back to its default, a bad sample is dropped alone, all logged.

`repeat` spawns the sample again after each run's check and cleanup, one actor of it at a time, to measure how often
each list gives a spell (the open "some R1 lists give nothing" question). That each new actor rolls its leveled lists
anew is an assumption the repeated runs check (`pickedRuns` short of all or none). Each run has its own lines and
`RESULT` (named `<sample> run=k/N`; a sample run once keeps its plain name) and counts in `DONE`; a failure before the
spawn (record not found, holder without lists, no actor from the spawn) ends the sample's runs with a `FAIL` for the
run it happened in. A sample whose plugin is not loaded ends the same way with `INCONCLUSIVE (plugin <name> not
loaded)`: nothing could be checked, which is not a failure of the addon. `DONE` then adds `notRun=N`: the runs that
never started and have no `RESULT` line - a sample stopped in run k of N, or a test that ended in it, adds N - k (run
k itself has its `RESULT` and is counted in pass, fail or inconclusive). A sample whose holder is found only after the spawn (leveled, or a leveled link
before the holder) finds it again from each run's actor. After the last run the sample's `SELFTEST lists` line gives
each list's `pickedRuns=K/C`: in K of the C checked runs the actor's leveled spell cache held one of the list's spells.

A leveled NPC record (LVLN) is accepted but cannot be spawned this way: `PlaceObjectAtMe` gave no actor for
`039D5F` (2026-10-15), so such a sample FAILs as `PlaceObjectAtMe gave no actor`. The leveled spawn path is checked
from outside, with the console through DevBench.

What it does, once per session, at the first save load (`kPostLoadGame`):

1. Finds each sample's record. For an NPC whose template chain resolves, the record that really holds its spell list
   (`SpellListHolder`, the template walk `AttachToNpcs` uses; the Spells flag without a template link is the record
   itself, as `AttachToNpcs` reads it); a holder with none of our lists fails without a spawn. A leveled sample, or an
   NPC whose chain has a leveled link before its holder, is spawned first: its holder is found from the spawned
   actor's real records (step 4).
2. Spawns each sample at the player (`PlaceObjectAtMe`, which takes an NPC or a leveled NPC). **Side effect: for up
   to `maxWaitMs` hostile NPCs (bandits, necromancers) stand next to the player and may attack.** Use a disposable
   save. They are disabled and deleted right after the check (`Disable`, `SetDelete(true)`), also on a timeout or an
   error; only a ref that still has the FormID it was spawned with and is an actor is touched. After another load, a
   new game the refs are left alone (that world is gone with them).
3. Waits until the actor has its 3D and a high process (the leveled spell cache needs the high process; before 3D
   every `HasSpell` was false on 10-10). A worker thread queues one main-thread tick every 100 ms, and a new one only
   after the last has run; every wait (the player's 3D and cell: 60 s, `startDelayMs`, `maxWaitMs`) counts ticks that
   ran, not wall time. Ticks stop only while the game window is in the background (alt-tab).
4. Logs the spawned actor's records: its base (`0xFF...` when the engine made one at the spawn), `GetTemplateBase()`
   (the `ExtraLeveledCreature` template when the actor has that extra, else the base), whether it has
   `ExtraLeveledCreature` and its original base, and the base's `kCopiedTemplate` flag. For a leveled or unresolvable
   sample the holder is the first of template base, original base and actor base (records from a plugin) whose
   template walk resolves. The lists checked are the actor base's (the engine builds the leveled spell cache from
   it), or the holder's when the base has none.
5. Walks the actor's spells once with `Actor::VisitSpells` and a visitor that never stops (it builds the leveled
   spell cache when missing), then reads the cache itself, `HighProcessData::leveledSpellList` (`picked`: the list's
   spells in it). Per list it also asks `Actor::HasSpell` for each witness spell: the list's spells minus every spell
   the NPC can have another way (the static spells and the other, vanilla, leveled lists' entries of the sample, its
   holder, its original base and every record of the template chains of the template base and the actor base, the
   race's spells and leveled lists, and the actor's added spells). So a spell vanilla also hands out (Flames in
   `OREO_LSpellBandit01FireFrostShock`) never counts as proof.

Ending early: a load before the spawn starts the wait again in the loaded save. A load after the spawn or a new game
(`kNewGame`) ends the test: the open samples fail with the reason and no ref is touched. A test that ends before its
spawn (a new game, no player 3D within 60 s, an error) checked nothing: no `RESULT` line and no `DONE` line, only
`SELFTEST RESTART <reason>: the next save load in this session starts it again`, and the next save load in the same
session runs it again; one that spawned ends with `DONE` and does not. There is no main menu check: the test starts
only on `kPostLoadGame`, and on 1.6.1170 `UI::IsMenuOpen(MainMenu)` stayed true for at least 10 ticks (about 1 s)
after a load from the main menu (2026-10-15), so such a check ended every test.

`Actor::HasSpell` covers leveled results: on 1.6.1170 it (ID 38782) is a visitor over `Actor::VisitSpells` (38781),
which walks the added spells, the actor's base record's spells, the race's spells and then, for a non-player actor
with an AI process, `GetLeveledSpells` (39443) - the high process's leveled spell cache, built once from the
**actor's own base record's** leveled lists (schools rolled by current skill). Disassembly notes: `lab/addon-research/
npc-spells-ledger.md` (main checkout, not in git) and the 10-10 Case 161 check.

Log lines (`HeartOfMagic_NPCSpells.log`, all start with `SELFTEST`, the values below are an example). Every line,
warnings and errors included, is also appended to the report under `== Self test ==`:

```
SELFTEST start samples=4 startDelayMs=2000 maxWaitMs=8000 pollMs=100 esl=loaded runs=8
SELFTEST spawn sample 039D37:Skyrim.esm ref 0xFF0012AB
SELFTEST sample 039D37:Skyrim.esm kind=npc placed Skyrim.esm|0x039D37 actorBase Skyrim.esm|0x039D37 templateBase Skyrim.esm|0x039D37 extraLeveled=no originalBase none copiedTemplate=yes holder Skyrim.esm|0x039D31 holderFrom=record inherited=yes chain=resolved holderLists=9 actorBaseLists=9 listsFrom=actorBase 3D=loaded high=yes waitedMs=300
SELFTEST cache 039D37:Skyrim.esm leveledSpellList=7 spells
SELFTEST list 039D37:Skyrim.esm HoMNS_Destruction_None_All_R1_C0 school=destruction skill=35 entries=120 levels=highest minLevel=0 witnessMinLevel=0 topLevel=25 witnessReachable=yes excluded=2 witnesses=118 shared=90 hits=1 picked=1 pickedWitnesses=1 hitSpells: 0x... Name picked: 0x... Name
SELFTEST school 039D37:Skyrim.esm destruction lists=2 distinctHits=1 distinctPicked=1
SELFTEST RESULT sample 039D37:Skyrim.esm PASS (HoMNS-only witnesses found: 4)
SELFTEST spawn sample 013BBB:Skyrim.esm run=1/5 ref 0xFF0012AC
SELFTEST RESULT sample 013BBB:Skyrim.esm run=1/5 PASS (HoMNS-only witnesses found: 3)
SELFTEST lists 013BBB:Skyrim.esm runs=5 checked=5 HoMNS_Alteration_None_All_R1_C1 pickedRuns=2/5, HoMNS_Destruction_None_All_R1_C0 pickedRuns=5/5
SELFTEST DONE pass=8 fail=0 inconclusive=0
```

Other lines: `SELFTEST config: ...` (a warning: a dropped sample or a reset value), `SELFTEST cache <sample>
leveledSpellList=null` (no cache after the walk), `SELFTEST another save is loading before the spawn: the wait starts
again`, the warnings `SELFTEST WARN another save is loading during the test: ...`, `SELFTEST WARN a new game was
started during the test: ...` and `SELFTEST WARN save taken while sample actors exist (actors=N)`, the error `SELFTEST
stopped by an error: ...`, `SELFTEST RESTART <reason>: the next save load in this session starts it again` (the last
line of a test that ended before its spawn, in place of `DONE`; e.g. `SELFTEST RESTART the player's 3D or cell never
came: ...`), and `SELFTEST DONE pass=0 fail=0 inconclusive=0 (not started: ...)` for a config that cannot be read
(that one is final: nothing starts it again in the session). A watcher waits for `SELFTEST DONE`.

How to read them:

- `RESULT ... PASS` needs at least one witness spell found, by `HasSpell` or in the cache. Otherwise, in this order:
  `FAIL (holder has N HoMNS lists, the spawned actor's base has none)` - the lists did not reach the record the engine
  rolls from; `INCONCLUSIVE (reason)` - no list could give a witness: no witnesses, no witness reachable at the
  actor's skill, or the cache shows the list's roll landed on non-witness spells (`picked>0`, `pickedWitnesses=0`);
  counted apart in `DONE`; `FAIL (reason)` - nothing found where a list could give one. Other FAILs mean nothing was
  checked (record not found, no list on the holder nor the actor base, "template chain has a leveled link before the
  spell list holder", no 3D or high process in time, another load or a new game after the spawn, an error).
- `picked` (from the cache) is the primary evidence: the spells the engine rolled from this actor's leveled lists that
  are in this list; `pickedWitnesses` of them are witnesses. `hits` (`HasSpell` on witnesses) is the second line; the
  two should agree. `excluded` is how many of the list's spells were taken out of its witnesses.
- `actorBaseLists` is the discriminator for the template question: the engine reads the leveled lists of the spawned
  actor's own base. `holderLists>0` with `actorBaseLists=0` means the inheriting NPC's copy did not get our lists, so
  no roll can ever give them (the FAIL above). Before the template fix that was every template child (run 6); now
  every child gets its holder's lists at data load, so it means that write did not happen or did not last (look for
  the child in the report's inherited rows and in the skip reasons; a copy from the template after data load would
  also show as this).
  `actorBaseLists` equal to `holderLists` with no pick means the lists are there and the roll gave nothing.
  `actorBase`, `templateBase`, `extraLeveled`, `originalBase` and `copiedTemplate` say which records the engine used.
- `hits` and `picked` per list cannot be read as "this list gave one": lists of one school overlap (`shared` =
  witnesses another list of ours on the NPC also holds - always one of the same school, as a spell has one school),
  and the engine merges the picks. The one-spell-per-list check is the `school` line: `distinctPicked` (and
  `distinctHits`) against `lists`.
- `skill` is the actor's current skill in the list's school (what the engine rolls with). `levels=all` is a list with
  the all-levels flag (any entry at or below the skill can be picked), `levels=highest` one without it (only the
  entries of `topLevel`, the highest level at or below the skill). `witnessMinLevel` is the lowest level of a witness
  entry; `witnessReachable=no` marks a list whose roll at this skill cannot land on a witness, so it can legitimately
  give no proof. An R1 list (minLevel 0) giving 0 is not such a case (it is the open "Alteration lists give nothing"
  question when it is an Alteration list).

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
   10. **Release condition.** A leveled bandit mage (its end record inherits its spell list from a template, as most
       leveled bandit mages do): `HasSpell` on the spawned actor must be true for at least one spell only our lists
       hold (the report's inherited row of the end record names its holder, whose row names the lists); the leveled
       record itself gets no list. Not yet done. The self test cannot spawn a leveled record (`PlaceObjectAtMe` gave no
       actor for `039D5F`, `OREO_SubCharBandit01Magic`), so this is checked from outside with the console through
       DevBench: `player.placeatme` a `LvlBanditWizard...` record and check `HasSpell` as above. `PASS` on the direct
       sample `039D37` is necessary but not sufficient: placing the end record itself skips the leveled spawn path the
       game uses.
   11. Non-mage sanity: among about 20 guards or bandits (`player.placeatme` a few `EncBandit01Melee...`), about one
       has a `..._NonMage_...` list.
   12. Save and reload: the lists are still on the base records (re-added at load), no crash. Disable the addon and
       load the same save: no leftover spells on NPCs you did not meet, no missing-form warnings beyond the ESL itself.
4. Pin the one-spell-per-list assumption: a bandit mage with N attached lists in a school should show at most N spells
   of that school after entering high process. The self test's `SELFTEST school ... lists=N distinctHits=D distinctPicked=P` lines
   give this per school and sample (P and D at most N), with the actor's skill on each list line.
5. Write the numbers (NPCs given lists, lists, biggest list, ms) into spec sections 8 and 9.
