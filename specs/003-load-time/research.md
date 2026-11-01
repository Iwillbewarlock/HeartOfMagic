# Research: Game Loading Time

Sources: `SpellLearning.log` 2026-11-01 11:55-11:58 and the code, read 2026-11-01. "Inferred" marks
what was reasoned from code or timestamps, not timed.

## Where the 1.7 s goes (data loaded, game thread 16896)

| Step | Time | Evidence |
|---|---|---|
| UIManager::Initialize | 5 ms | log 71-74 |
| Event and source registration | 2 ms | log 80-91 |
| **PerkAdapters::Apply** | **1,420 ms** | log 101, `LibrarianPerkAdapterPatch.cpp:405` |
| of which: vampire caster collection | 394 ms | log 92-100 (18.557-18.951, inside Apply's span) |
| Environment::Initialize | ~113 ms | timestamps 19.872-19.985 (inferred; no timer) |
| SpellResearch install, tome rebuild | 2 + 2 ms | log 112-113 |
| TomeReach start (game-thread part) | ~17 ms | log 113-114 |
| Gameplay config | ~5 ms | log 115-125 |
| Startup tree | 102 ms | log 565 (validation 19 ms, 62 ms unlogged) |
| SyncRequiredXP, AutoScan::Install, tome prep | unknown | no log lines (`Main.cpp:358-363`) |

Outside the plugin: the 2.2 s to "Panel DOM ready" is PrismaUI loading the view (no plugin lines
in that span; inferred). On save load the plugin's own work is ~1 ms of cosave plus 79 ms
SpellResearch; the 18.9 s and 2.9 s gaps carry no plugin lines (engine, inferred).

## R1. Why perk adapters take 1,420 ms

- Decision: two loops are the candidates; time them before changing them.
  1. `LoadOrderSource` constructor (`LibrarianLoadOrderSource.cpp:21`) calls
     `SpellScanner::ResolvePersistentFormId` once per catalog spell (~10k). That function builds a
     fresh `PluginLookup` per call (`SpellScannerFormId.cpp:152-157`, read), so every call scans the
     4,329 loaded plugin files by name again. The same happens per spell in `inFormList`.
  2. `BuildPlan` builds the full scan-style JSON of each catalog spell once (`SpellJson` →
     `BuildSpellJson`, `SpellScannerJson.cpp:261`), and the first such call triggers the vampire
     caster collection (`IsCastByVampires` `call_once`, `SpellScannerJson.cpp:271`, read).
  3. Also in the spell JSON stage: `GetPersistentFormId` looks plugins up by load index per spell
     and per associated form (`SpellScannerFormId.cpp:21-40`); not removed by a name cache
     (GPT review, 추정 until timed).
- Rationale: Apply has one timer, which stops before the report write (`:386`); the split is
  unknown.
- Alternatives considered: caching the whole plan on the load order fingerprint
  (`LoadOrderFingerprint`, `ScanRecord::CatalogMatches`) - larger change, kept as a fallback if the
  two fixes below do not reach the target.

## R2. Is the vampire collection needed by perk adapters

- Decision: no, as far as read. `castByVampires` is read only by the librarian's classify rules
  (`LibrarianClassify.cpp:196`) and the full scan (`SpellScannerScan.cpp:487`); the adapter matcher
  does not read it (grep), and the one adapter file that names vampires (`ascension.json`) only
  uses tags and the `MagicVampireDrain` keyword, not the flag (read).
- To confirm in implementation: the plan written by `WriteReport` is identical with the flag
  skipped (parity check, below).

## R3. Can work move off the game thread

- Decision: no, not in this feature. Apply writes forms (keywords on effects, form lists, casting
  perks); the planner reads forms that other plugins may still change during data load.
  `LoadOrderSource` is documented game-thread only (`LoadOrderSource.h:21`).

## R4. The 866 "Resolved ... persistent ID" lines

- Decision: low priority. They cost ~10 ms each time (two times); saving re-resolved ids would
  remove the repeat but is outside the time budget's main cost.

## Budget check

Target 0.5 s. Non-adapter steps ~250 ms today. So perk adapters must reach ~250 ms (from 1,420).
Skipping the vampire flag removes ~394 ms (measured span). The plugin-lookup fix removes an
unknown share of the remaining ~1,026 ms (inferred 0.6-2 s worth of lookups at ~10k calls, so
probably most of it). If the timers show more left, the fallback is the cached plan.

## Findings after the Phase 1 timers (2026-11-01 19:21 run)

Numbers: spec.md "Baseline with stage timers". What they change in the plan:

- **Phase 2 is small.** Catalog ids take 34 ms. Only 5 adapter lines are formList lines
  (adamant 4, vokrii 1), and `inFormList` runs only for spells a line picked. At most about 50 ms.
- **Phase 3 moves the cost, it does not remove it.** `IsCastByVampires` collects on first use
  (`std::call_once`, SpellScannerCasters.cpp). Today the adapters' first `BuildSpellJson` at data load
  is that first use. Without it, the first use is the next full scan's first step, on the game thread
  in 4 ms steps: a single step of about 507 ms, which breaks spec 002 (≤ 0.5 s, HITCH budget). On a
  launch with no scan the 507 ms is gone; on a launch that scans it becomes a visible freeze.
- **The planner's ~605 ms is lines × spells.** About 110 live lines each walk all 3,773 catalog
  spells and read the scan JSON (std::map finds) per spell: about 1.5 µs per spell per line.
  Taking string copies out of the matching checks (`LibrarianAdapterMatch.cpp`, FieldView) left the
  offline report byte-identical and cut the offline planner from 2.24 s to 2.04 s (3 runs each,
  `librarian-test --adapters` on this load order's dump; the offline source copies ids and does a
  catalog find per spell per line, so offline is slower than the game and is used for parity and
  relative change only).
- Per-file offline planner time: vanilla 547 ms (26 lines), env-oif 527 (22), adamant 346 (18);
  a one-line file costs about 75-90 ms (the carried-keyword cache for every spell plus one walk).

Consequence: Phases 2-3 together reach about 1.3 s at best, against 500 ms. Something structural is
needed; the choice is the user's (cache the plan across launches, or restructure the planner to visit
only candidate spells).
