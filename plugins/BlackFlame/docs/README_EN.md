# Heart of Magic - Black Flame 1.0.0

**Korean name:** Heart of Magic - 흑염 달구기  
**Author:** Iwillbewarlock  
**Base mod:** Heart of Magic Librarian (by Iwillbewarlock; a rebuild of Heart of Magic - Spell Learning System by Dinkel Zombie)

## What it does

Heart of Magic - Black Flame is an add-on for Heart of Magic Librarian. It adds one wearable item: "Saeel's Iron Cage Lantern" (Korean: "사엘의 철창 등불"). You wear it at the waist on the left front of the hip. It is an iron cage with a grey-black flame floating inside, and it gives no light.

While you wear the lantern, you get the "Longing" (Korean: "갈망") worn effect:

1. **Grey-black fire visuals:** Your own fire spells look grey-black. This changes the projectile, the explosion, the casting effect in your hands, the body aura, the fire on targets you hit, and the casting light. "Your fire spells" means ordinary spells that Heart of Magic's spell catalog marks as fire. It is a glowing grey-black flame, not pitch black.
2. **Higher Magicka cost:** Fire spells that have the fire keyword cost 30% more Magicka. A spell that counts as fire only in Heart of Magic's catalog may not get this cost increase.
3. **Black Flame burn damage:** Your fire hits add the "Black Flame" (Korean: "흑염") effect. It deals 2% of the target's maximum Health over 2 seconds and ignores resistance. Hitting the target again restarts the 2 seconds; it does not stack. The spell's own damage is still resisted as usual. This effect does not apply to cloak spells, scrolls, staffs, shouts, or powers.
4. **Grey corpse fire:** On AE 1.6.1170, the corpse fire of Frozen Electrocuted Combustion (FEC) on enemies your fire killed while you wore the lantern is grey too.

**What stays unchanged:**
- NPCs' fire spells, a torch you hold, summoned creatures' own fire, and fire traps keep their normal look.
- Projectile speed, range, and the spells' own damage are unchanged.
- Take the lantern off and your fire spells go back to their normal look and cost.

**How it works:**  
When the game starts, the add-on makes private grey copies of the fire visuals from your own load order, vanilla and mods alike, with a small helper program included in the download. Only your own fire spells use the copies; NPCs and other spells keep the original files.

## Requirements

- **Skyrim Special Edition / Anniversary Edition:** AE 1.6.1170 is the only version tested in game (see the version table below). Skyrim VR is not supported.
- **SKSE** for your game version.
- **Address Library for SKSE Plugins**.
- **Heart of Magic Librarian 1.5.0 or newer** (tested with 1.6.0). Black Flame reads the spell catalog Heart of Magic makes when it scans your spells. From 1.5.0 on, Heart of Magic makes it by itself on the first start.
- **Microsoft Visual C++ Redistributable 2015-2022 (x64):** The helper program needs it. Most Skyrim setups already have it.
- **Optional - Immersive Equipment Displays (IED):** With IED, IED hangs the lantern at your hip. Without IED, the lantern's own worn model is used. With IED you can move it by adding `"lanternIedPreset": "frontLeft"`, `"frontRight"`, `"backLeft"` or `"backRight"` to `SKSE\Plugins\HeartOfMagic_BlackFlame.json` (default `frontLeft`; restart the game after a change).
- **Disk space:** The generated copies and work files depend on your load order. On the author's test setup (about 4,000 mods) they took about 2.6 GB in total.

## Installation

1. Install the archive with Mod Organizer 2 or Vortex.
2. There is a Korean and an English download. Install only one. They have the same files and FormIDs; only the in-game text differs.
3. Enable both plugins: `HeartOfMagic_BlackFlame.esp` and `HeartOfMagic_BlackFlame_Slots.esp`.
4. Keep `SKSE\Plugins\HeartOfMagic_BlackFlame.json`: without it the plugin falls back to built-in settings that are not the release ones. If it is lost, reinstall the mod.
5. If you use a loose-file cache mod (such as CRDW), rebuild its cache (`Force rebuild`) after installing or updating. An old cache does not know the new files.

## First launch

- Start the game and wait at the main menu. The first start takes longer than usual because the add-on makes the grey copies then. Wait at the main menu before you load a save: about 1-2 minutes on the author's PC (RTX 3070); a slower PC or a larger load order takes longer. Nothing on screen says when it is done; spells that still look orange after loading turn grey from the next start.
- If Heart of Magic has no spell catalog yet, its automatic first-start scan makes one first, and Black Flame waits for it.
- If Heart of Magic's automatic scan is turned off, or you already have a saved tree so it does not scan by itself, run a full scan in Heart of Magic.
- If you load a save or start a new game before the catalog is ready, Black Flame does not set itself up for that session. Finish the scan and restart the game.
- On AE 1.6.1170, the copies are used in the same launch. On other game versions they may be used only from the next launch: restart the game once.
- With a large load order, some spells may still look orange after the first launch. Restart the game once more.
- Later launches reuse the copies that are still valid.
- Do not share your generated copies or your catalog with other players. They are made from the files of your own mods.

## Finding the lantern

- **In game:** You can find "Saeel's Iron Cage Lantern" (Korean: "사엘의 철창 등불") on a table on the upper level of Ansilvund Burial Chambers (Korean: 안질푼트 매장묘실), with smoke rising next to it. A save that has already visited that place gets the lantern on the next visit.
- **Console (English):** Run `help Saeel 4` to see its FormID, then run `player.additem <FormID> 1`.
- **FormID format:** Its FormID is `FE` + the 3-digit ESL number your mod manager shows for `HeartOfMagic_BlackFlame.esp` + `80D` (example: ESL number `FD3` gives `FEFD380D`).

## Game versions

| Version | Status | Notes |
| --- | --- | --- |
| AE 1.6.1170 (Steam) | Tested in game | Copies are used in the same launch. |
| AE 1.7.104 | Not tested in game | The code addresses were checked by reading the game code, but it was never run. Off: the instant aura refresh of spells that give an aura while equipped, the grey FEC corpse fire, the grey Fire Rune glyph. Scorch marks were checked only on 1.6.1170. Copies may need one restart. |
| Other AE 1.6 versions (incl. GOG 1.6.1179) | Not tested | Same as 1.7.104, and also off: the instant hand casting effect refresh (your hands change on the next draw or cast instead). Copies may need one restart. |
| SE 1.5.97 | Not tested, never run | Copies are used from the next launch (restart once). Everything that is off on other AE versions is off, and also: the grey hand casting effect, and the grey ground fire and explosions created by the game's own hazard/explosion code. |
| Skyrim VR | Not supported | |

## Known limits

- The flame is grey-black, not pitch black.
- Death flames (an enemy burning to death) stay orange; that is hard-coded in the game engine.
- The rune glyph on the ground (Fire Rune) turns grey only on AE 1.6.1170. Scorch marks were checked only on AE 1.6.1170.
- The instant aura refresh of spells that give an aura while equipped (such as Fire Storm) when you put the lantern on or take it off works only on AE 1.6.1170.
- The FEC corpse fire may stay orange in some cases (for example when your own frost or shock spell hits that corpse afterwards). The grey corpse fire is AE 1.6.1170 only.
- With the lantern on, if you switch from a fire spell to a non-fire spell that shares the same equipped effect, that spell's light can stay tinted. It is harmless and goes back to normal when you take the lantern off.
- Spells that are not in Heart of Magic's catalog keep their normal look, even if they look like fire. An effect shared by a fire spell and a non-fire spell may keep its original look.
- A burn effect that started while you wore the lantern stays grey until it ends, even if you take the lantern off.
- The inventory preview may not show the flame inside the lantern (the preview does not draw particles). This is normal.
- With IED, a follower wearing the lantern does not show it (only the player is set up).

## Uninstall

1. If you use IED: take the lantern off and save before you remove the mod. Otherwise IED keeps showing the lantern's last state.
2. Disable and remove the mod. Removing plugins in the middle of a playthrough can leave traces in the save; keep a save from before you installed it.
3. Delete the generated files:
   - In MO2's Overwrite folder (or in the game's Data folder without MO2): `meshes\heartofmagic\blackflame\copies` and `textures\heartofmagic\blackflame\copies`
   - The work folder: `Documents\My Games\Skyrim Special Edition\SKSE\HeartOfMagic_BlackFlame` (GOG: `Skyrim Special Edition GOG`)
   - `HoMBF_slots*.json` in your saves folder (with MO2 profile-local saves: that profile's saves folder)
   - `HeartOfMagic_BlackFlame.log` and `HeartOfMagic_BlackFlame_tester.txt` in the SKSE folder above (logs; attach them to bug reports)

## Credits and thanks

- **Dinkel Zombie** - the original Heart of Magic (Heart of Magic - Spell Learning System).
- **HDharder** - Temp File Framework (GPL-3.0): the add-on uses its findings about the game's file index (function addresses, signatures and data layouts) to make new files visible while the game runs. None of its code is used.
- **SlavicPotato** - Immersive Equipment Displays: its Papyrus API shows the lantern at the hip.
- **GiraPomba** - Lights On - Wearable Lanterns for IED: its IED hip positions are the lantern's position presets.
- **powerofthree** - address references, and Frozen Electrocuted Combustion, whose corpse effects this add-on turns grey.
- **Ershin** - the main-thread hook location also used by Open Animation Replacer, Precision and True Directional Movement.
- **Khajiitistan** - Black Flame (Nexus mod 192777): the colour reference for the look. No files from it are used.
- **The authors of MuIF and KillFeed** - their call offsets for the SE 1.5.97 impact sites.
- **Community Shaders** - its effect shader was the reference for the palette rules.
- **Bethesda** - the grey effect meshes are made from Skyrim's own files.
- **Libraries:** CommonLibSSE-NG (GPL-3.0 with exceptions), nifly (GPL-3.0), DirectXTex, DirectX Tool Kit, DirectXMath, spdlog, {fmt}, JSON for Modern C++ (all MIT).

## License and source

- The SKSE plugin (`HeartOfMagic_BlackFlame.dll`) and the helper program (`blackflame-greybuild.exe`) are GPL-3.0 combined works. Heart of Magic's own source files are MIT.
- Source code: https://github.com/Iwillbewarlock/HeartOfMagic ; `THIRD-PARTY-NOTICES.md` in the download names the exact commit and the licenses.
