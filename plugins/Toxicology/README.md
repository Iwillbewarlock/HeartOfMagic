# Heart of Magic Librarian - Toxicology

## What it does

An addon to Heart of Magic Librarian. Once you have learned the ability **Toxicology**, every poison spell you cast
also delivers one of your own poisons: when a poison spell hits a living target, the poison you have favorited
first is applied to that target, at the poison's own strength and duration, with you as the caster.

- **Poison spells** are the spells the Librarian tags `poison` in `Data/SKSE/Plugins/SpellLearning/spell_catalog.json`
  (its scan writes the file), kept only when the spell has at least one hostile effect. The Librarian also tags
  Cure Poison, poison resistance buffs and similar helpful spells `poison` for tree grouping; those are skipped, so
  healing a follower does not poison them. For cloak spells the sub-spell the cloak casts on enemies counts too.
- **Which poison**: among the poisons in your inventory that are favorited, the first by name (the name shown in
  the inventory, a poison you named yourself by that name; case is ignored). That is the order SkyUI's favorites
  menu sorts by. The vanilla favorites menu's order was not checked; the same rule is used.
- **Repeat hits** do not stack: while the effect the addon applied is still on the target, a new hit only renews
  its duration. Per target and poison, hits closer together than `minIntervalSeconds` (default 1 s) are ignored.
  The addon never renews an effect it did not apply itself: if the target already carries the same poison from a
  coated weapon, or from before the save was loaded, nothing happens until that effect ends.
- **Bottles**: hits use up no bottles. Instead, every 100 Magicka you spend casting poison spells (hit or miss;
  the cost after perks and gear, dual casting and concentration included) uses one bottle of the poison being
  mimicked. When that poison runs out the next favorited poison takes over. The running total is kept in the SKSE
  co-save. Bottles without a favorite mark are used before the favorited stack.
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
| `minIntervalSeconds` | `1.0` | Least time between two applications or renewals of the same poison on the same target (0 or more) |
| `magickaPerDose` | `100` | Magicka spent on poison spells per bottle used (more than 0) |

A missing key keeps its default. A value of the wrong type, a negative `minIntervalSeconds`, a `magickaPerDose` of 0
or less, or a file that is not valid JSON keeps the default and is logged.

## Load order

`HeartOfMagic_Toxicology.esp` must load **after `I4IconAddon.esp`** (when installed) so the addon's icon rule wins
over I4's default poison icon. A newly added plugin loads last by default, so this is normally already true.

## Known limits

- **Deliveries without hit events**: the addon reacts to the engine's hit events (`TESHitEvent`) from your poison
  spells. TODO (pending in-game self test): which of runes, explosions, concentration spells and cloaks send those
  events, and how often for concentration, is not confirmed yet; list here any delivery that does not trigger.
- **I5 (InventoryInjectorImproved)** caches inventory icons per item, so the icon can stay on the old poison in the
  inventory and barter menus after the favorite changes. The console command `i5 purge` clears the cache; the
  favorites menu is always accurate. The addon cannot clear that cache itself (it logs a line when I5 is loaded).
- Changing favorites with a menu open changes the icon the next time the menu is opened.
- **Saves that already traded with Elgrim** may show the tome only after the merchant restocks (about 48 hours of
  game time).
- **Poisons with an instant and a lasting effect**: while the lasting effect is still on the target, hits only
  renew it, so the instant damage lands once per application, not once per hit.
- **Only your own casts**: poison spells cast by NPCs, followers included, do not apply poisons.
- **A rescan in game** (the Librarian's full scan rewrites the catalog) is picked up within about 5 seconds, at the
  next poison spell hit. A spell from a mod installed after the last scan counts only after the next scan.
- **Cost-reducing mods** that lower spell cost outside the engine's own spell cost (for example by script) may not
  be reflected in the Magicka count.
- **Perks on the applied poison**: whether the engine applies alchemy perks to a poison the addon applies is not
  checked (the in-game self test logs it); the addon uses the poison's values as they are.
- **Crime**: a poison spell hit already counts as an attack; whether the applied poison adds anything on a
  non-hostile target is not checked.
- If the first favorited poison changes while an effect is running, that effect runs to its end and the next hit
  uses the new poison.

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
  (`src/core/ToxCore.*`: settings, catalog parsing, catalog keys, the poison choice, the bottle arithmetic and
  which stack loses a bottle, the hit interval); every
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
  (default 30 s) after a save load settles: it gives the ability and two poisons, spawns targets, casts the load
  order's poison spells at them (one per delivery), checks what the addon did, then the bottles and the saved
  Magicka total (it asks for a save and a reload for that last check), and takes back what it added. Results are
  `SELFTEST PASS|FAIL|INFO|DONE` lines in `HeartOfMagic_Toxicology.log`. Use a disposable save. Details:
  `src/game/ToxSelfTest.h`.
- The ESL and how it was made: `ESL.md`. The icon and the SWF: `art/ICON.md`.
