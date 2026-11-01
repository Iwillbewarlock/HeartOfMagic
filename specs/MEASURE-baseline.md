# Baseline measurement session (specs 001, 002, 003)

One game session, about 15 minutes, gives the numbers all three specs start from.
Claude reads `SpellLearning.log` afterwards and writes the tables; nothing needs copying by hand.

## Before the game (Claude)

1. Build with spec 003 Phases 0-1 (parity dump and stage timers, no behaviour change), so this
   one session also gives the load-time split.
2. Skyrim closed. `.\DeployDev.ps1 -WithSelfTest` puts the current build and `selftest.json` into
   the dev install (the auto scan then runs at the main menu whatever its record says, and logs
   `AutoScan SELFTEST:` lines with the HITCH check).

## In the game (user)

1. Start the game and wait at the main menu until the auto scan is done (about 10-30 s; nothing to
   press). This gives spec 003 (game start) and spec 002 (auto scan, HITCH).
2. Load the usual save. This gives spec 003 (save load).
3. Open the panel. Settings > Advanced > Developer & Debug: turn **Developer Mode** on.
4. In each of the six designs (Skald, Classic, Modern Dark, Arcane, Night Grimoire, Chalkboard),
   about one minute each, in this order:
   - 10 s without touching anything (idle)
   - 15 s dragging the tree around (drag)
   - 15 s zooming in and out with the wheel (zoom)
   - 15 s moving the mouse over nodes, clicking two or three spells (hover, click)
   This gives spec 001.
5. Press **Scan** in the panel once and build the tree once. This gives spec 002 (full scan, build).
6. Quit the game normally (the log is flushed on quit).

## After the game (Claude)

1. Read `Documents/My Games/Skyrim Special Edition/SKSE/SpellLearning.log`.
2. Fill the baseline tables in each spec; note what is ours and what is PrismaUI's or the game's.
3. Deploy again without `-WithSelfTest` (removes `selftest.json`) when the measurements are done.

Only step "In the game" needs the user; every number is read from the log.
