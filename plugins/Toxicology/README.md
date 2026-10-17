# Heart of Magic Librarian - Toxicology

## What it does

An addon to Heart of Magic Librarian. Once you have learned the ability **Toxicology**, every poison spell you cast
also delivers one of your own poisons: when a poison spell hits a living target, the poison you have favorited
first is applied to that target, at the poison's own strength and duration, with you as the caster (intended;
pending in-game self test, see "Not yet confirmed in game" below).

- **Poison spells** are the spells the Librarian tags `poison` in `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`
  (its scan writes the file), kept only when the spell has at least one hostile effect. The Librarian also tags
  Cure Poison, poison resistance buffs and similar helpful spells `poison` for tree grouping; those are skipped, so
  healing a follower does not poison them. For cloak spells the sub-spell the cloak casts on enemies is
  meant to count too (pending in-game self test).
- **Which poison**: among the poisons in your inventory that are favorited, the first by name (the name shown in
  the inventory, a poison you named yourself by that name; case is ignored). That is the order SkyUI's favorites
  menu sorts by. The vanilla favorites menu's order was not checked; the same rule is used.
- **Once per target**: each poison is applied to a given target only once. Later hits on that target with the same
  poison do nothing - they neither stack nor renew it, so a paralysis poison cannot keep a target held forever. If
  your first favorited poison changes, the new poison can be applied to that target once too. The record is kept in
  memory only, so it starts over after a save is loaded (see Known limits).
- **Bottles**: hits use up no bottles. Instead, every `magickaPerDose` Magicka (default 100) you spend casting poison spells from your hands
  (hit or miss; meant to be the cost after perks and gear, dual casting and concentration included - pending in-game
  self test) uses one bottle of the poison being mimicked. A poison cloak costs what you pay for the cloak; the
  sub-spell it casts on enemies costs nothing. When that poison runs out the next favorited poison takes over.
  Bottles without a favorite mark are used before the favorited stack. The running total (with any concentration
  Magicka not yet counted) is kept in the SKSE co-save.
- **Icon**: with Inventory Interface Information Injector (I4) installed, the poison being mimicked shows a
  "flask before a magic circle" icon in green (`#62A636`) in the inventory and favorites menus.

With no favorited poison, with the ability not learned, or with `enabled` off, nothing happens and no Magicka is
counted. The addon writes nothing to the game's own save; only the Magicka total goes into the co-save.

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
| `HeartOfMagic_Toxicology.esp` | ESL-flagged plugin: the keyword, the ability's effect, the ability, the tome (`ESL.md`) |
| `SKSE/Plugins/HeartOfMagic_Toxicology.dll` | The addon |
| `SKSE/Plugins/HeartOfMagic_Toxicology.json` | Settings |
| `SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json` | I4 rule for the icon |
| `Interface/HeartOfMagicToxicology/icons.swf` | The icon (`art/ICON.md`) |
| `SKSE/Plugins/HeartOfMagic_Toxicology/` | `LICENSE`, `THIRD-PARTY-NOTICES.md`, `licenses/` (nothing reads this folder) |

The log is `HeartOfMagic_Toxicology.log` in the SKSE log folder (`Documents/My Games/Skyrim Special Edition/SKSE/`).

## Getting the ability

Buy **Spell Tome: Toxicology** (350 gold) from Elgrim's Elixirs in Riften. The DLL puts one tome into that shop's
merchant chest in memory at game start; no vanilla record is overridden. Reading it teaches the ability at once and
the tome is used up, as with any vanilla tome that teaches an ability (Heart of Magic's tome study does not apply
to abilities, and the ability is not in the spell tree). The ability shows under **Active Effects** in the magic
menu, not in the spell list.

## Settings

`SKSE/Plugins/HeartOfMagic_Toxicology.json`, read once at game start (change it with the game closed):

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Off: no poison is applied, no icon, no Magicka counted |
| `showIcon` | `true` | Off: the mimic keyword is not put on the poison, so I4 shows its normal icon |
| `magickaPerDose` | `100` | Magicka spent on poison spells per bottle used (more than 0) |

A missing key keeps its default. A value of the wrong type, a `magickaPerDose` of 0 or less, or a file that is not
valid JSON keeps the default and is logged. `minIntervalSeconds`, a setting of earlier test builds, is no longer
used; a settings file that still has it loads without a warning.

## Load order

`HeartOfMagic_Toxicology.esp` must load **after `I4IconAddon.esp`** (when installed) so the addon's icon rule wins
over I4's default poison icon. A newly added plugin loads last by default, so this is normally already true.

## Known limits

### Not yet confirmed in game

TODO (pending in-game self test): the points below are what the addon is built to do, but none has been confirmed
in game yet. Update this list from the self test's `SELFTEST` lines and move each confirmed point out of it.

- **Deliveries without hit events**: the addon reacts to the engine's hit events (`TESHitEvent`) from your poison
  spells. Which of runes, explosions, concentration spells and cloaks send those events, and how often for
  concentration, is not confirmed; list here any delivery that does not trigger.
- **Cloaks**: the cloak's sub-spell (the one it casts on enemies) is in the poison spell set; whether its hits
  apply the poison is not confirmed.
- **Where the poison lands and who cast it**: the poison is cast on the hit target with you as the caster; that it
  lands on the target (not on you) and carries you as its caster is not confirmed.
- **Magicka count**: the counted cost is meant to match the Magicka the engine takes (after perks and gear, dual
  casting and concentration included); that it does, within 5 %, is not confirmed. The addon reads the hand
  caster's own cost; which of its two cost values tracks the engine's charge is decided by this check. The check
  is manual: the self test casts through the instant caster, which the addon does not watch (see Development).

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
- **Saves that already traded with Elgrim** may show the tome only after the merchant restocks (about 48 hours of
  game time).
- **Once per target resets after a load**: which targets already got which poison is not saved. After a save is
  loaded (or a new game started) every target can get each poison once more, even one that still carries it from
  before the load.
- **Once per target counts the cast, not the effect**: a target counts as poisoned as soon as the addon casts the
  poison on it, even if the target resisted it or the poison has only instant effects. The same poison from a coated
  weapon does not count: the addon may still apply its own copy once.
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
- **Perks on the applied poison**: whether the engine applies alchemy perks to a poison the addon applies is not
  checked (the in-game self test logs it); the addon uses the poison's values as they are.
- **Crime**: a poison spell hit already counts as an attack; whether the applied poison adds anything on a
  non-hostile target is not checked.
- If the first favorited poison changes while an effect is running, that effect runs to its end and the next hit
  applies the new poison (once per target, as above).

## Uninstalling

Remove the mod. The ability record disappears with the ESL (a save that had it loses it, like any removed plugin's
record), and the tome placed in Elgrim's chest was only in memory. The co-save record (the Magicka total, ID
`HMTX`) is ignored by SKSE without the DLL. As always, test removal on a separate save.

## Development

- **Build**: `.\BuildRelease.ps1 -preset Release-2026` from the repository root. It builds
  `HeartOfMagic_Toxicology.dll` (`plugins/Toxicology/CMakeLists.txt`, version from `plugins/Toxicology/VERSION`) and
  assembles `dist/HeartOfMagic-Librarian-Toxicology_<version>.zip`, rooted at Data, with its PDB in
  `dist/HeartOfMagic-Librarian-Toxicology_<version>_DebugSymbols.zip` (`plugins/cmake/DistributionToxicology.cmake`;
  the notice comes from `plugins/Toxicology/THIRD-PARTY-NOTICES.md.in`, source tag `toxicology-v<version>`). The dev
  self test file and `art/` never go into the zip.
- **Offline harness**: `build/tools/Release/toxicology-test.exe --self-test` checks the pure core
  (`src/core/ToxCore.*`: settings, catalog parsing, catalog keys, the hit and cost spell sets, the poison choice,
  the bottle arithmetic and which stack loses a bottle, the interval check of the Magicka drain queue); every
  check prints PASS or FAIL, exit 1 on a failure.
- **Dev deploy** (game closed; the dev mod folder must exist, default `D:\TAKEALOOK\mods\HeartOfMagic-Toxicology-Dev`):

  ```powershell
  .\DeployDev.ps1 -ToxicologyOnly                       # DLL, ESL, I4 rule, SWF; the settings JSON only if the install has none
  .\DeployDev.ps1 -ToxicologyOnly -Force                # the same, the install's settings JSON replaced
  .\DeployDev.ps1 -ToxicologyOnly -ToxicologySelfTest   # plus the dev self test file
  .\DeployDev.ps1 -ToxicologyOnly -ToxicologyModPath <folder>
  ```

  Every deploy without `-ToxicologySelfTest` (the plain Heart of Magic deploy too) removes the self test file from
  the Toxicology install and says so. Every copy is checked by hash.
- **In-game self test** (dev only): with `data/dev/HeartOfMagic_Toxicology_selftest.json` installed as
  `SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json`, the DLL runs a test once per session, `startDelayMs`
  (default 30 s) after a save load settles: it checks the tome in Elgrim's chest (read only), gives the ability and two poisons, spawns targets, casts the load
  order's poison spells at them (one per delivery), checks what the addon did, then the bottles and the saved
  Magicka total (it asks for a save and a reload for that last check), and takes back what it added. Results are
  `SELFTEST PASS|FAIL|INFO|DONE` lines in `HeartOfMagic_Toxicology.log`. Use a disposable save. Details:
  `src/game/ToxSelfTest.h`.
- **Magicka accuracy is checked by hand**: the caster hook only sees the player's hand casters, and the self test
  casts through the instant caster, so it cannot measure the count. With the ability and a favorited poison, stand
  still with no regeneration or potion effects, cast poison spells from your hands (single, dual, concentration)
  for 5 seconds at a time, and read the `magicka counted X ... / player magicka drop Y` lines (the first 3 after each
  load are logged at info) and the `casting starts` / `fire-and-forget release` lines beside them, which give both
  cost values the caster holds. X and Y should agree within 5 %.
- The ESL and how it was made: `ESL.md`. The icon and the SWF: `art/ICON.md`.
