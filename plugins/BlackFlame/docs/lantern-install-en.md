# Saeel's Iron Cage Lantern — Black Flame

Equip the lantern to turn your fire magic black. Fire spells with the fire keyword cost 30% more Magicka. Eligible fire hits add damage equal to 2% of the target's maximum Health over 2 seconds, ignoring resistance. There is no flat damage. Repeated hits refresh the burn instead of stacking it.

## Requirements and installation

- Skyrim SE/AE, matching SKSE and Address Library. VR is not supported. The principal development runtime is AE 1.6.1170; some hooks are runtime-specific.
- Heart of Magic and its spell scan catalog. Immersive Equipment Displays is optional.
- Microsoft Visual C++ Redistributable 2015–2022, x64.

Install this ZIP through MO2 or Vortex and enable `HeartOfMagic_BlackFlame.esp` and `HeartOfMagic_BlackFlame_Slots.esp`. Choose either the English or Korean package. They use the same plugin filenames and FormIDs; only their displayed text differs.

On a new HoM installation, remain at the initial main menu while HoM's automatic scan creates its catalog and Black Flame generates private visual copies. Black Flame waits for a readable catalog before building its spell sets, copies and runtime hooks. An existing valid catalog is reused. If HoM automatic building is disabled, or a saved tree prevents a new automatic scan, run a full HoM scan manually.

If you load a save or start a new game before the catalog is ready, Black Flame cancels runtime initialization for that session. Complete the scan and restart the game. Copy publication never resumes during gameplay. Large mod lists may need another launch to finish applying generated copies. Do not import another player's catalog or generated copies. Rebuild any loose-file cache after installing or updating.

Automatic tests, automatic loading and automatic quitting are disabled in this package. The `source` directory contains the build's source snapshot and is not used by the game.

## Finding the lantern

The lantern is on an upper-level table in Ansilvund's inner burial chambers, next to smoke. The console command `help "Saeel's Iron Cage Lantern" 4` finds its FormID; use `player.additem <FormID> 1` to add it.

## Scope and verification

Visuals are selected from ordinary spells tagged as fire in HoM's catalog. Copies are private: NPCs and unrelated spells retain shared original assets. Ambiguous shared abilities can retain their original appearance. Removing the lantern restores the normal visuals and cost.

The extra burn excludes cloak hits, scrolls, staffs, shouts and powers. Catalog-only fire classification does not necessarily add the fire keyword required by the cost perk. The burn's IgnoreResistance and NoAbsorb flags do not make the original spell damage ignore resistance.

This build has offline startup, damage-rule and regression checks. The previous visual-scope build was confirmed working by the user; the new first-install scan timing, revised damage and localized tooltip still need in-game verification. Use a disposable test save. Compare equipping/removing the lantern, Fire Storm and nonfire shared effects, NPC casts, save/load, and the displayed description. Report the spell, runtime, IED usage and relevant BlackFlame log when something differs.
