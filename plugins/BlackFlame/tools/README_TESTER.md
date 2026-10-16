# Heart of Magic - Black Flame: tester build

Thank you for testing. This build turns the player's own fire spells grey-black while the Stoke buff is on. It
runs a scripted self-test and writes extra logs, so it is **for testing only**.

**Use a throwaway save.** The self-test spawns two deer next to you, casts spells, toggles the buff and asks for a
quicksave and quickload at the end. Do not keep playing from a save made during the test.

**On SE and on AE builds other than 1.6.1170 / 1.7.104** this is a diagnostic run: SKIPPED hooks in the tester file
and INFO / SKIP lines for devices in T0-hooks are expected there. FAIL lines are what to report. On SE the hand art copy,
the hazard creation copy swap and the explosion creation swap are off (their SE addresses are not verified yet); grey
hazard models from the shipped grey map still apply, and the rest runs.

## What you need

- Skyrim SE 1.5.97 or AE 1.6.x / 1.7.x (Skyrim VR is not supported)
- SKSE for your game version and Address Library for SKSE Plugins

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
   output mod".
3. Enable the output mod (it replaces the two empty plugins). In MO2, press F5 (refresh) if its new files do not show.
   If you use a loose-file cache mod (CRDW and the like), rebuild its cache after running the builder: a cache made
   before the builder ran does not know the new files, and the game then cannot load those grey copies (the plugin says
   so on screen once, and in the tester file's "file index" line).
   Start the game and load your throwaway save.
4. Wait about 30 seconds without pausing. The self-test runs by itself; at the end it asks you to quicksave and
   quickload once.

## Send back

All from `Documents\My Games\Skyrim Special Edition\SKSE\` (`Skyrim Special Edition GOG` for the GOG version):
- `HeartOfMagic_BlackFlame.log`
- `HeartOfMagic_BlackFlame_selftest.txt`
- `HeartOfMagic_BlackFlame_tester.txt`
- `HeartOfMagic_BlackFlame\request.json` (in the same SKSE folder: what the game asked the builder for)
- from the copies output mod: `SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json`
- a crash log if the game crashed (from your crash logger's folder)
- the builder's window text (copy it, or run it from a command prompt and save the output)

Also say which game version you use and whether you used MO2 or Vortex.
