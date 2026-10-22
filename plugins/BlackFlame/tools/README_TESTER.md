# Heart of Magic - Black Flame: tester build

Thank you for testing. This build turns the player's own fire spells grey-black while wearing the lantern
**Saeel's Iron Cage Lantern** (사엘의 철창 등불). For the Korean in-game checklist, see `lantern-tester-ko.md` in this
folder.

**Use a save you do not need to keep.** Do not continue your normal playthrough from a save made while testing this
build.

The main targets are AE 1.6.1170 and 1.7.104; some features are on only there. **On SE (1.5.97)** the hand art copy,
the hazard creation copy swap and the explosion creation swap are off (their SE addresses are not verified yet); grey
hazard models from the shipped grey map still apply, and the rest runs.

## What you need

- Skyrim SE 1.5.97 or AE 1.6.x / 1.7.x (Skyrim VR is not supported)
- SKSE for your game version and Address Library for SKSE Plugins
- For the builder (`tools\BlackFlame\blackflame-greybuild.exe`): the Microsoft Visual C++ Redistributable 2015-2022
  (x64), from Microsoft. Most Skyrim setups have it. Without it the builder does not start: the bat (or MO2) reports
  exit code -1073741515 (0xC0000135, "a DLL was not found"; the builder needs `VCOMP140.DLL`). Install the x64
  redistributable and run it again.

The licences of the libraries in the plugin and the builder are in `THIRD-PARTY-NOTICES.md` and `licenses\`. Both
binaries are GPL combined works; `THIRD-PARTY-NOTICES.md` names the commit they were built from, and when that commit
is not public yet its source is in the package as `source\HeartOfMagic-<version>-source.zip`.

## Install

**Mod Organizer 2**
1. Install the zip as a mod (Install a new mod from archive) and enable it with its three plugins
   (`HeartOfMagic_BlackFlame.esp`, `HeartOfMagic_BlackFlame_Copies.esp`, `HeartOfMagic_BlackFlame_Local.esp`).
2. Make an empty mod named `HeartOfMagic BlackFlame Copies` (right-click the mod list, Create empty mod) and place
   it **after** Heart of Magic - Black Flame.
3. Add `tools\BlackFlame\blackflame-greybuild.exe` from the Black Flame mod folder as an executable with the
   arguments `--from-game --out "<your MO2 mods folder>\HeartOfMagic BlackFlame Copies"`.

**Vortex**
1. Install the zip as a mod and enable its three plugins.
2. You will run `tools\BlackFlame\Build Black Flame copies.bat` from the mod's staging folder (step 2 of "Run"). With no
   argument it writes to `HeartOfMagic BlackFlame Copies` in your Documents folder; zip that folder and install the zip
   as a mod. Or give the bat a folder inside Vortex's staging folder as its argument.
3. The copies mod must **win the file conflicts** with Black Flame (it carries the same two plugin names and the grey
   map): in Vortex set it to load after Black Flame.

## Run

1. Start the game to the main menu, then quit. (The plugin writes what the builder needs into
   `Documents\My Games\Skyrim Special Edition\SKSE\HeartOfMagic_BlackFlame\`.)
2. Run the builder (MO2: the executable from step 3 of the MO2 install; Vortex: the bat). It ends with "next: enable the
   output mod". It also writes grey copies of the fire textures (a `textures  recoloured ...` line). If you built copies
   with an earlier tester build, do steps 1 and 2 again with this one: the game now also saves the textures the builder
   needs. Each run replaces its own copies in the output mod: copy files an earlier run made and this one did not are
   removed (only inside its `copies` folders), so do not keep your own files there. The run also writes
   `SKSE\Plugins\HeartOfMagic_BlackFlame_risk.txt` (its offline look check) and prints a `T0 risk:` line; send both back.
3. Enable the output mod (it replaces the two empty plugins). In MO2, press F5 (refresh) if its new files do not show.
   If you use a loose-file cache mod (CRDW and the like), rebuild its cache after running the builder: a cache made
   before the builder ran does not know the new files, and the game then cannot load those grey copies (the plugin says
   so on screen once).
4. Load a save, wear Saeel's Iron Cage Lantern (사엘의 철창 등불), and go through the checklist in
   `lantern-tester-ko.md`.

## Send back

All from `Documents\My Games\Skyrim Special Edition\SKSE\` (`Skyrim Special Edition GOG` for the GOG version) and your
test run:
- `HeartOfMagic_BlackFlame.log`
- `HeartOfMagic_BlackFlame\request.json` (in the same SKSE folder: what the game asked the builder for)
- from the copies output mod: `SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json` and
  `SKSE\Plugins\HeartOfMagic_BlackFlame_risk.txt` (with the builder's `T0 risk:` line)
- the builder's window text (copy it, or run it from a command prompt and save the output)
- a crash log if the game crashed (from your crash logger's folder)
- the `lantern-tester-ko.md` checklist results and screenshots

Also say which game version you use and whether you used MO2 or Vortex.
