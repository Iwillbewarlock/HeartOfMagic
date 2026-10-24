# Heart of Magic - Black Flame: changelog

The add-on's own version (`plugins/BlackFlame/VERSION`). Heart of Magic Librarian's version (`VERSION` at the
repository root) is separate.

## 1.0.0 (first public release)

- New item: Saeel's Iron Cage Lantern (사엘의 철창 등불), worn at the hip, found on a table on the upper level of
  Ansilvund Burial Chambers. With Immersive Equipment Displays installed it is shown through IED (four hip presets,
  `lanternIedPreset`); without IED its own worn model is used.
- While it is worn, the player's own fire spells (ordinary spells Heart of Magic's catalog marks as fire) look
  grey-black: projectile, explosion, hand casting art, body aura, burn shader on targets, casting light. NPC fire,
  torches and summons are unchanged.
- While it is worn, fire-keyword spells cost 30% more Magicka, and the player's eligible fire hits add 2% of the
  target's maximum Health over 2 seconds, ignoring resistance; a new hit restarts the burn instead of stacking it.
  Cloaks, scrolls, staffs, shouts and powers add no burn.
- On AE 1.6.1170, the Frozen Electrocuted Combustion corpse fire on enemies the black flame killed is grey.
- The grey copies are built from the player's own load order by the included builder at game start (used in the same
  launch on AE 1.6.1170; other runtimes may need one restart, SE always does), after Heart of Magic's spell catalog is ready.
- Runtimes: tested in game on AE 1.6.1170 only. AE 1.7.104 is code-read, not run; other AE builds and SE 1.5.97 run
  with the runtime-specific parts off (`docs/README_EN.md`, "Game versions"). No VR build.
- The release package ships `lanternLog` off.
