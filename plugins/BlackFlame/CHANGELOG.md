# Heart of Magic - Black Flame: changelog

The add-on's own version (`plugins/BlackFlame/VERSION`). Heart of Magic Librarian's version (`VERSION` at the
repository root) is separate.

## Unreleased

- AE 1.7.104: the five features that ran only on AE 1.6.1170 now run on 1.7.104 too - the hit path observation
  (corpse mark, magic effect apply sink, SRE probes), the grey FEC corpse fire (corpse grey hooks A/B with the corpse
  colours), the Fire Rune glyph's copy window, the instant aura refresh of spells that give an aura while equipped
  (equip ability visual restart), and the file index's 0x10-only stream count rule (copies used in the same session
  when its checks pass). The game code they use was read on 1.7.104 and found the same as on 1.6.1170; not run in
  game. Other AE builds and SE 1.5.97 are unchanged. The version rule moved to `src/CodeReadBuilds.h`, which the
  equip refresh check compiles as it is.
- Less disk: the work folder (`<SKSE log folder>\HeartOfMagic_BlackFlame`) no longer holds a second copy of the grey
  copies and of their sources. A copy Data already holds is deleted from `staging` (the builder now reads the list of
  published copies, `staging\published.tsv`, and does not build them again), and the dump in `data` keeps only the
  sources of what is still to be built. Copy contract 9 (an older builder refuses the request); keys and copies are
  unchanged, so nothing is rebuilt on update. The first start after the update deletes the old duplicates itself. On the
  author's ~4,000-mod setup this is expected to take the work folder from about 1.7 GB to under 0.1 GB (not yet run in
  game). Design and limits: `docs/DISK-TRIM-DESIGN.md`.
- Docs: the player guides and Nexus pages list 1.7.104 with every feature on (not tested in game) and name what is
  off on other AE builds and on SE 1.5.97 without referring to other rows; Korean and English wording fixes from a
  naturalness review; developer notes and code comments brought in line with the code.

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
