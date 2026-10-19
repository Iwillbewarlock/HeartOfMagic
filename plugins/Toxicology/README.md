# Heart of Magic Librarian - Toxicology

## What it does

An addon to Heart of Magic Librarian. Once you have learned the ability **Toxicology**, every poison spell you cast
also delivers one of your own poisons: when a poison spell hits a living target, the poison you have favorited
first is applied to that target, at the poison's own strength and duration, with you as the caster. The poison is
cast by your instant magic caster onto the target and lands on the target, not on you (confirmed in game; what was
tested and what was not is under "Tested in game" and "Not yet confirmed in game" below).

- **Poison spells** are the spells the Librarian tags `poison` in `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`
  (its scan writes the file), kept only when the spell has at least one hostile effect. The Librarian also tags
  Cure Poison, poison resistance buffs and similar helpful spells `poison` for tree grouping; those are skipped, so
  healing a follower does not poison them. For cloak spells the sub-spell the cloak casts on enemies is
  in the set too (not yet confirmed in game). Aimed bolts and aimed concentration sprays were confirmed in game
  to apply the poison on hit; other deliveries are in "Not yet confirmed in game".
- **Which poison**: among the poisons in your inventory that are favorited, the first by name (the name shown in
  the inventory, a poison you named yourself by that name; case is ignored). That is the order SkyUI's favorites
  menu sorts by. The vanilla favorites menu's order was not checked; the same rule is used.
- **Immunity after the poison wears off**: once a poison is applied to a target, that target is immune to the same
  poison until the poison's duration has run out and an immunity time after it has passed: the immunity is the
  duration times `immunityMultiplier` (default 1.0), and at least `minImmunitySeconds` (default 5). A 10 second
  poison can therefore be applied again about 20 seconds after it was applied (game time, see Known limits); an
  instant poison after 5 seconds. Hits in
  between do nothing - they neither stack nor renew it, so a paralysis poison cannot keep a target held forever. The
  duration is the one the poison actually got on the target (alchemy perks included). Time is game time: it stands
  still while a menu pauses the game. Each poison has its own window, so a newly favorited poison can be applied at
  once. The record is kept in memory only, so it starts over after a save is loaded (see Known limits).
- **Bottles**: hits use up no bottles. Instead, every `magickaPerDose` Magicka (default 100) you spend casting poison spells from your hands
  (hit or miss) uses one bottle of the poison being mimicked. A fire-and-forget spell counts the cost its hand caster
  holds for the cast (the cost after perks and gear); a concentration spell counts that cost per second while the
  hand caster is casting. A dual cast is meant to count once (not yet confirmed in game). A poison cloak costs what you pay for the cloak; the
  sub-spell it casts on enemies costs nothing. When that poison runs out the next favorited poison takes over.
  Bottles without a favorite mark are used before the favorited stack. The running total (with any concentration
  Magicka not yet counted) is kept in the SKSE co-save.
- **Icon**: with Inventory Interface Information Injector (I4) installed, the poison being mimicked shows a
  "flask before a magic circle" icon in green (`#62A636`) in the inventory and favorites menus.

With no favorited poison, with the ability not learned, or with `enabled` off, nothing happens and no Magicka is
counted. The addon writes nothing to the game's own save; the SKSE co-save (ID `HMTX`) holds the Magicka total
(record `ACCM`) and whether the tome reward has been given (record `TOMG`, see "Getting the ability").

The design and its reasons are in `docs/superpowers/specs/2026-10-17-toxicology-addon-design.md` (Korean).

## Requirements

- Skyrim SE 1.5.97 and AE (1.6.x / 1.7.x) with SKSE and Address Library for SKSE Plugins. VR is not supported
  (the DLL is built without VR support, like Heart of Magic's).
- **Heart of Magic Librarian 1.5.0 or later, with one full scan done.** The addon reads the Librarian's
  `spell_catalog.json` (catalog `version` 1); without it, or with another version, there are no poison spells and
  nothing happens (the log says why).
- **Not compatible with the original Heart of Magic**: it does not write `spell_catalog.json` or its tags.
- Optional: Inventory Interface Information Injector (I4), for the icon. Without it the feature works, only the
  icon is missing.

## Files

| File | Role |
|---|---|
| `HeartOfMagic_Toxicology.esp` | ESL-flagged plugin: the keyword, the ability's effect, the ability, the tome (`ESL.md`); the `_KO` archive has the Korean one under the same name |
| `SKSE/Plugins/HeartOfMagic_Toxicology.dll` | The addon |
| `SKSE/Plugins/HeartOfMagic_Toxicology.json` | Settings |
| `SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json` | I4 rule for the icon |
| `Interface/HeartOfMagicToxicology/icons.swf` | The icon (`art/ICON.md`) |
| `SKSE/Plugins/HeartOfMagic_Toxicology/` | `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/` (nothing reads this folder) |

The log is `HeartOfMagic_Toxicology.log` in the SKSE log folder (`Documents/My Games/Skyrim Special Edition/SKSE/`).

## Getting the ability

**Spell Tome: Toxicology** is Ingun Black-Briar's reward for her quest **Few and Far Between** (Riften): the DLL
gives it when the quest reaches its success stage, 200, with the game's "added" notice on screen; no vanilla record
is overridden. A save that finished the quest before the addon was installed gets the tome at its next load. It is
given even when the ability is already known or a tome is already carried (it is the quest's reward). Once
given it is never given again (the co-save keeps that), so a tome sold or dropped unread is gone; the console
(`player.additem` with the tome's form ID) is the way back. With `enabled` off (read at game start) nothing is
given and the reward stays open, so turning it on later gives the tome at the next load. Reading the tome teaches
the ability at once and the tome is used up, as with any vanilla tome that teaches an ability (Heart of Magic's tome study does not apply
to abilities, and the ability is not in the spell tree). The ability shows under **Active Effects** in the magic
menu, not in the spell list.

## Settings

`SKSE/Plugins/HeartOfMagic_Toxicology.json`, read once at game start (change it with the game closed):

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Off: no poison is applied, no icon, no Magicka counted |
| `showIcon` | `true` | Off: the mimic keyword is not put on the poison, so I4 shows its normal icon |
| `magickaPerDose` | `100` | Magicka spent on poison spells per bottle used (more than 0) |
| `immunityMultiplier` | `1.0` | Immunity after a poison wears off, as a multiple of its duration (0 or more; 0: only `minImmunitySeconds`) |
| `minImmunitySeconds` | `5.0` | The shortest immunity, in seconds (0 or more); also the whole immunity for an instant poison |

A missing key keeps its default. A value of the wrong type, a `magickaPerDose` of 0 or less, a negative
`immunityMultiplier` or `minImmunitySeconds`, or a file that is not valid JSON keeps the default and is logged. `minIntervalSeconds`, a setting of earlier test builds, is no longer
used; a settings file that still has it loads without a warning.

## Load order

`HeartOfMagic_Toxicology.esp` must load **after `I4IconAddon.esp`** (when installed) so the addon's icon rule wins
over I4's default poison icon. A newly added plugin loads last by default, so this is normally already true.

## Known limits

### Tested in game

Skyrim AE 1.6.1170, the developer's load order (with Mysticism and Apothecary), 2026-10-17/18, from the log and by eye:

- **Hits**: aimed fire-and-forget bolts (Mysticism's Poison Spray) and aimed concentration sprays (Mysticism's
  Deadly Poison Spray) raise hit events, and the poison is applied on hit for both.
- **Where it lands**: the poison lands on the target, cast by the player's instant caster with the player as
  caster (`(player's caster): 1 new effect(s)` on the target); nothing landed on the player.
- **Immunity window**: a 10 s poison got 10 s of immunity and a reapply time 20 s after the cast (`D 10.00 s ... immunity
  10.00 s, reapply in 20.00 s`); a paralysis poison was not renewed while the target was immune.
- **Magicka count**: a fire-and-forget cast was charged the hand caster's cost (17.15 per cast, the spell's own
  cost); concentration was counted in caster state 6 (`kCasting`). One 5 s concentration window counted 28.28
  against a player Magicka drop of 26.67 (about 6 % high; regeneration was not ruled out). Bottles were used up
  one per 100 Magicka.
- **Alchemy perks**: for the test character the applied magnitude and duration equalled the poison record's
  (50 / 10 s), so no perk that acts when a poison is applied changed it. See "Perks on the applied poison" below.
- **Mod-added poisons**: vanilla poisons overridden by Apothecary work. **Icon**: shows in the inventory.
- **Runtime 1.6.629 and later**: the crash when reading a target's active effects was fixed (the list is now
  reached through `Actor::AsMagicTarget()`).

### Not yet confirmed in game

- **Other deliveries**: runes and other target-location spells, explosions and cloak sub-spells - whether they raise
  hit events (`TESHitEvent`, which the addon reacts to) and so apply the poison. A delivery that does not trigger
  applies nothing.
- **Dual casting**: counted once per cast (fire-and-forget: one release; concentration: only the right hand while
  both hands cast the same spell, `kCountOneHandWhenDual`); not measured.
- **The tome reward** given in a real playthrough of Few and Far Between (the dev self test sets the stage through
  Papyrus instead), and the look of its on-screen "added" line; the I5 inventory icon refresh after the favorite
  changes.
- **The tome for a save that finished the quest earlier**: the load check relies on the stopped quest still reading
  stage 200 after a save and load, and on SKSE's kPostLoadGame passing the load's success as its data pointer. The
  self test's reload check (step 4a) measures the first; neither has run in game yet.
- **Text**: the ability's description under Active Effects and the tome's text (both rewritten for the immunity
  window) have not been looked at in game.

### Other limits

- **I5 (InventoryInjectorImproved)** caches inventory icons per item, so the icon can stay on the old poison in the
  inventory and barter menus after the favorite changes. The console command `i5 purge` (I5 0.4.0, the release in
  use when this was written) or `i5 cache purge` (newer I5) clears the cache; the favorites menu is always accurate. The addon cannot clear that cache itself (it logs a line when I5 is loaded).
- Changing favorites with the inventory or favorites menu open moves the icon when that menu closes, so the open
  menu still shows it on the old poison. Poisons taken from or put into a container are picked up when the container
  menu closes.
- **Player-made poisons keep the icon in the save**: a poison you brewed is saved with the mimic keyword if it was
  being mimicked then. After every load the addon takes the keyword off every poison in your inventory except the one
  being mimicked; such a poison left in a chest or with a follower keeps the icon until it is back in your inventory
  at a later load.
- **A save from an earlier test build that sold the tome at Elgrim's Elixirs**: a tome bought there and left unread
  in a chest (not in your inventory) does not count as the reward, so finishing Few and Far Between (or loading a
  save that finished it) gives a second one.
- **A quest stage set after 200 by a mod or the console** (for example 250, the failure stage, after 200) hides a
  finished quest from the load check, so a save that finished it before the addon was installed gets no tome.
- **Immunity resets after a load**: which targets are immune to which poison is not saved. After a save is loaded
  (or a new game started) every target can get each poison again at once, even one that still carries it from
  before the load.
- **Immunity counts the cast, not the effect**: the window starts as soon as the addon casts the poison, even if
  the target resisted it (then the poison record's duration is used). A cure or dispel that ends the poison early
  does not shorten the window. The same poison from a coated weapon does not count: the addon may still apply its
  own copy.
- **Waiting moves the clock**: the window runs on game time, so waiting, sleeping or fast travel ends it early
  (game time runs much faster then). A timescale change by a mod stretches what is left of a window with it. The
  window is therefore about its length in real seconds, not exact: in testing one reapply came 18.7 s of real time
  after a 20 s window. Whether a timescale change or the measurement caused that is still open; the `cast #N` line
  carries the game hour and timescale at the cast and the immune line the timescale at the cast, to tell.
- **The icon is on the poison, not on your bottles**: the mimic icon marks the base poison, so every copy of it shows
  the icon while it is being mimicked - at vendors, in containers and in loot too.
- **Leftover Magicka**: when the last favorited poison runs out, the part of the count under one dose stays and goes
  toward the next poison you favorite; whole doses with no bottle left to take are dropped. Casting with no
  favorited poison counts nothing.
- **Casts from your hands only**: a poison spell a script or another mod casts for you does not count toward
  bottles; the addon takes the cost from your left and right hand casters.
- **Only your own casts**: poison spells cast by NPCs, followers included, do not apply poisons.
- **A rescan in game** (the Librarian's full scan rewrites the catalog) is picked up at the first spell you cast, or
  spell hit of yours, at least 5 seconds after the previous check of the catalog file, or at the next save load. A spell from a mod installed after the last scan counts only after the next scan.
- **Cost-reducing mods** that lower spell cost outside the engine's own spell cost (for example by script) may not
  be reflected in the Magicka count.
- **Perks on the applied poison**: the poison is applied at its record's strength and duration. Perks that act at
  brewing are already in a brewed poison's record; the dose perks (Concentrated Poison, Bottomless Cup) do not
  apply, since no weapon is coated. In testing no perk changed the applied values. Perks that change a spell's
  magnitude or duration when it is applied (for example Ordinator's Doctor Death or Elemental Oil) were not tested.
  The log shows it: for the first 10 casts after each load, each new effect's actual magnitude and duration next to
  the poison record's (`Toxicology: cast #N ...` and the `effect` lines under it; later casts at debug level).
- **Crime**: a poison spell hit already counts as an attack; whether the applied poison adds anything on a
  non-hostile target is not checked.
- If the first favorited poison changes while an effect is running, that effect runs to its end and the next hit
  applies the new poison (with its own immunity window, as above).

## Uninstalling

Remove the mod. The ability record disappears with the ESL (a save that had it loses it, like any removed plugin's
record). The co-save records (ID `HMTX`: the Magicka total and the tome reward flag) are ignored by SKSE without the
DLL. As always, test removal on a separate save.

## Development

- **Build**: `.\BuildRelease.ps1 -preset Release-2026` from the repository root. It builds
  `HeartOfMagic_Toxicology.dll` (`plugins/Toxicology/CMakeLists.txt`, version from `plugins/Toxicology/VERSION`) and
  assembles `dist/HeartOfMagic-Librarian-Toxicology_<version>.zip`, rooted at Data, with its PDB in
  `dist/HeartOfMagic-Librarian-Toxicology_<version>_DebugSymbols.zip` (`plugins/cmake/DistributionToxicology.cmake`;
  the notice comes from `plugins/Toxicology/THIRD-PARTY-NOTICES.md.in`, source tag `toxicology-v<version>`). The dev
  self test file and `art/` never go into the zip. `dist/HeartOfMagic-Librarian-Toxicology_<version>_KO.zip` is the
  same files with the Korean ESL (`data/ko/`, `ESL.md` section "Korean ESL").
- **Offline harness**: `build/tools/Release/toxicology-test.exe --self-test` checks the pure core
  (`src/core/ToxCore.*`: settings, catalog parsing, catalog keys, the hit and cost spell sets, the poison choice,
  the bottle arithmetic and which stack loses a bottle, the immunity window, the interval check of the Magicka
  drain queue, whether the tome reward is given and the co-save record of that flag) and the English / Korean ESL
  check (`ESL.md`, "Korean ESL"); every check prints PASS or FAIL, exit 1 on a failure.
- **Dev deploy** (game closed; the dev mod folder must exist, default `D:\TAKEALOOK\mods\HeartOfMagic-Toxicology-Dev`):

  ```powershell
  .\DeployDev.ps1 -ToxicologyOnly                       # DLL, ESL, I4 rule, SWF; the settings JSON only if the install has none
  .\DeployDev.ps1 -ToxicologyOnly -Force                # the same, the install's settings JSON replaced
  .\DeployDev.ps1 -ToxicologyOnly -ToxicologySelfTest   # plus the dev self test file
  .\DeployDev.ps1 -ToxicologyOnly -ToxicologyKorean     # the Korean ESL instead of the English one
  .\DeployDev.ps1 -ToxicologyOnly -ToxicologyModPath <folder>
  ```

  Every deploy without `-ToxicologySelfTest` (the plain Heart of Magic deploy too) removes the self test file from
  the Toxicology install and says so. Every copy is checked by hash.
  `-ToxicologyKorean` first runs `build/tools/Release/toxicology-esl-check.exe` on the English and Korean ESLs, as
  the release build does before the zips (`ESL.md`, "Korean ESL"): a mismatch, or a check that is missing or older
  than its source, stops the deploy with its output and nothing copied (build first with
  `.\BuildRelease.ps1 -preset Release-2026`).
- **In-game self test** (dev only): with `data/dev/HeartOfMagic_Toxicology_selftest.json` installed as
  `SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json`, the DLL runs a test once per session, `startDelayMs`
  (default 30 s) after a save load settles: it checks the tome record and the tome reward (the save's ability and
  tomes are taken, Few and Far Between is set to stage 200 through Papyrus - or, when it is already there, the load
  check runs - the tome must be given once and not a second time, then the ability and tomes are put back; the
  quest stays at stage 200 and the reward stays given, so the save is changed for good), gives the ability and two
  poisons, spawns targets, casts the load
  order's poison spells at them (one per delivery), checks what the addon did, hits one target again inside its
  immunity window and once after it (it waits up to about 90 s for that and compares the window's game clock with
  the poison effect's own timer), then the bottles and the saved
  Magicka total and the reward flag (it asks for a save and a reload for that last check; after the load the flag
  must be set and the quest at stage 200 again), and takes back what it added. Results are
  `SELFTEST PASS|FAIL|INFO|DONE` lines in `HeartOfMagic_Toxicology.log`. Use a disposable save. Details:
  `src/game/ToxSelfTest.h`.
- **Magicka accuracy is checked by hand**: the caster hook only sees the player's hand casters, and the self test
  casts through the instant caster, so it cannot measure the count. With the ability and a favorited poison, stand
  still with no regeneration or potion effects, cast poison spells from your hands (single, dual, concentration)
  for 5 seconds at a time, and read the `magicka counted X ... / player magicka drop Y` lines (the first 3 after each
  load are logged at info) and the `casting starts` / `fire-and-forget release` lines beside them, which give both
  cost values the caster holds. X and Y should agree within about 5 %; the one window measured so far was about 6 %
  high, with regeneration not ruled out (see "Tested in game").
- The ESL and how it was made: `ESL.md`. The icon and the SWF: `art/ICON.md`.
