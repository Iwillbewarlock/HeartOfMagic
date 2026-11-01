# Implementation Plan: Game Loading Time

**Branch**: `speckit-perf` | **Date**: 2026-11-01 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-load-time/spec.md`

## Summary

Cut the plugin's game-thread time at data load from about 1.7 s to at most 0.5 s with the same
results. Perk adapters take 1,420 ms of it (research.md). First add stage timers, then two fixes
that keep results identical by construction: reuse one plugin lookup when resolving catalog ids,
and skip the vampire caster flag where the adapters do not read it.

## Technical Context

**Language/Version**: C++23 (MSVC, VS 2022), CommonLibSSE-NG; PrismaUI panel in ES5 JavaScript

**Primary Dependencies**: CommonLibSSE-NG, nlohmann/json, spdlog (SKSE log)

**Storage**: JSON files under `Data/SKSE/Plugins/SpellLearning/` (catalog, adapters, report)

**Testing**: `.\BuildRelease.ps1`; tools self-tests (`librarian-test`, `progression-check`, ...);
perk adapter report parity (before/after `WriteReport` JSON); one in-game launch for timing

**Target Platform**: Skyrim SE/AE (Windows x64)

**Project Type**: SKSE plugin (DLL)

**Performance Goals**: data-loaded handler ≤ 500 ms game thread; save load ≤ 100 ms

**Constraints**: form writes stay on the game thread; results identical (399 keywords on effects,
1,365 of 1,365 tree entries); 600-line file limit

**Scale/Scope**: 4,329 plugins, 10,001 spell forms, ~10k catalog spells, 6 adapter files

## Constitution Check

| Principle | Status |
|---|---|
| I. CLAUDE.md is the rulebook | Pass: headers checked before RE calls; docs updated (ARCHITECTURE.md timing lines; PERK_ADAPTERS.md if behaviour notes change) |
| II. Measure in game first | Pass: baseline from the 2026-11-01 log; Phase 1 adds stage timers before any fix; after-run on the same load order |
| III. Every feature survives | Pass: report parity check and the feature list in quickstart.md |
| IV. Build, test, review gates | Pass: build, tools self-tests, panel tests, `node tools/librarian-check.js` (CLAUDE.md, after scanner/adapter changes), reviewer agent + Codex review. CLAUDE.md names OpenCodeReview as the second reviewer; the user's global rule (2026-10-18) bans OCR, so Codex takes that place |
| V. Smallest change | Pass: two local fixes; cached plan only if the timers show it is needed |

Re-check after design: no violations.

## Design

### Phase 0 - Parity dump (dev only, before any change)

- The adapter report holds counts and at most 8 examples per rule (`LibrarianPerkAdapterPatch.cpp:388-402`,
  `AdapterMatch.h:22`), so equal counts do not prove equal writes. With `debug.json` present, write
  the full sorted sets `(effect, keyword)`, `(list, spell)`, `(spell, perk)` and the Spell Research
  registrations (same `LoadOrderSource`) to a parity file. Baseline and after files must match.

### Phase 1 - Stage timers (no behaviour change)

- `LibrarianPerkAdapterPatch.cpp` ApplyImpl: time catalog load, adapter files, keyword map,
  `LoadOrderSource` construction, `BuildPlan` (with spell JSON build and effect-user indexing
  apart), reconcile (keywords, lists, perks) and report write; one log line
  `PerkAdapters: stages - catalog N ms, files N, keywords N, source N, plan N (spell json N, users
  N), reconcile N, report N`. The existing 1,420 ms stops before the report write
  (`:386`), so `Main.cpp` also times `Apply` from outside.
- Vampire collection already logs its own ms (`SpellScannerCasters.cpp:309-314`); reuse it.
- `Main.cpp`: one summary line per event with each step's time - OnDataLoaded (including
  `Environment::Initialize`, `SyncRequiredXPFromTree`, `AutoScan::Install`, the tome prep),
  `OnNewGame`, `OnPostLoadGame`; cosave load and revert (`CoSave.cpp:44`, `:80`) log their ms.
- Panel ready: log the time from the data-loaded handler's end to "Panel DOM ready".
- Uses the existing `MsSince()` (`ElapsedMs.h:7`).

### Phase 2 - One plugin lookup per pass

- `SpellScannerFormId.cpp` already has `ResolveWith(id, PluginLookup&, ...)` in an anonymous
  namespace (`PluginLookup` = `PluginNames::Lookup<const RE::TESFile*>`, line 52). Expose a small
  resolver in `SpellScanner.h` that owns one lookup and resolves many ids; the existing
  `ResolvePersistentFormId(id)` stays as it is for every other caller.
- One resolver per `Apply` pass, used by every resolve in it: the `LoadOrderSource` constructor
  loop, effect resolution (`LibrarianLoadOrderSource.cpp:102`), the `inFormList` callback
  (`LibrarianPerkAdapterPatch.cpp:340-345`, keeps `HasForm` and the `g_listed` exclusion), list
  writes (`:365`) and casting perk resolution (`:378-379`). Spell Research's own `inFormList`
  (`LibrarianSpellResearchPatch.cpp:388`) gets the same treatment in its pass.
- Same ids by construction (same resolve code; the not-loaded filter and the light/full checks
  stay; plugin-by-name answers cached within one pass; the plugin list does not change during data
  load). GPT checked this against the code (확실); a run comparison is still to do.

### Phase 2b - Per-pass reverse lookup, if Phase 1 shows it

- `GetPersistentFormId` (`SpellScannerFormId.cpp:21-40`) looks the plugin up by load index for
  every spell and associated form; Phase 2 does not remove that. If the spell JSON stage stays
  large, cache index → plugin name for the pass.

### Phase 3 - Skip the vampire flag in the adapter source

- Add `castByVampires` to `SpellScanner::FieldConfig` (`SpellScanner.h:~20-34`), default `true`,
  and have `BuildSpellJson` check it before `IsCastByVampires` (`SpellScannerJson.cpp:271`).
  `LoadOrderSource` sets it `false`. Full scan and librarian classify keep the default, so the
  collection still runs (once) when they need it.

### Phase 2c - Planner visits candidate spells only (user's choice 2026-11-01, replaces Phases 2-4)

Why: research.md "Findings after the Phase 1 timers". About 110 lines each walk all 3,773 catalog
spells; Phase 2 saves under 50 ms and Phase 3 moves a 507 ms freeze into the next scan.

- `BuildPlan` (`LibrarianAdapters.cpp`) walks the source once at its start and keeps a snapshot:
  `{ id (copied), const json* spell, const json* entry }` in catalog order. `PlanSource` gains one
  sentence of contract: the refs a visit hands over stay valid for the whole `BuildPlan` call
  (true for `LoadOrderSource`, whose JSON sits in `unique_ptr`s, and for the offline harness).
- From the snapshot it builds position lists (ascending, so visits keep catalog order):
  catalog tag -> spells whose entry's `elements` or `techniques` holds it; school -> spells;
  archetype -> spells with a VISIBLE effect of that archetype.
- Each keyword, formList and castingPerk line visits a candidate list instead of every spell:
  `tags.all` non-empty -> the list of `all[0]`; else `tags.any` non-empty -> the sorted union of its
  tags' lists; else `spell.school` -> that school's list; else `effect.archetype` (keyword and
  formList lines only, whose PickTarget needs a visible effect) -> that archetype's list; else all.
  Damage lines keep their own full walk (none in the shipped files).
- Why the result is identical, not just close:
  - A skipped spell would have failed the same check with no side effect. `HasTag` sets
    `withoutSources` only when it FINDS the tag; a spell missing `all[0]` (or every `any` tag) fails
    `TagsMatch` before any tag is found. A line with no tag condition never sets it (the `none` check
    uses a throwaway flag), so the school and archetype filters skip nothing that counts.
  - `carriedBy` fills a spell's entry from its record keywords on first ask. Every write into
    `carriedCache` (`[id].insert`, lines 470, 544, Damage 236) follows a `carriedBy` of the same id,
    so filling later gives the same set it would have had.
  - Candidates are visited in catalog order, so stats, examples and write order are unchanged.
- Gate: the offline report (`librarian-test --adapters ... -v` stdout and `-o` report) byte-identical
  before and after on this load order's dump; librarian self-tests; then one game start
  (main menu, quit) for the new `plan` time and the two parity files against `build/parity-baseline/`.
- Not changed: the vampire read stays at data load (Phase 3 dropped: research.md); Phase 2 and 4
  are not done.

### Phase 4 - Only if timers show > 500 ms left

- Cache the adapter plan keyed on the load order fingerprint and the adapter, catalog and config
  hashes; re-validate live form state at apply time. Planned separately if reached.

### Phase 5 - Saved plan (reviewed, NOT taken; 2026-11-01)

GPT design review (codex task, 5 findings, 3 checked in code): the key would have to cover far more
than plugins and keywords - `LoadOrderFingerprint::Current().hash` has no plugin sizes or times
(LoadOrderFingerprint.h:18, use `.scan`), the planner also reads keyword definitions, non-spell
effect users, taught tomes, live FormLists and DLL presence; a plan built after a scan differs from
one built at start (adapters run before SpellTomes, Main.cpp:334; AutoScan never runs Apply); a
start after a change stays near 980 ms. User chose the fresh path below; the saved plan is weighed
again only if Phase 6 leaves a gap.

### Phase 6 - Lighter fresh build (user's choice 2026-11-01)

Same plan every start, built fresh; only work the planner does not read is removed or moved.

6a. Vampire walk off the start path (about -400 ms)
- `FieldConfig::castByVampires` (default true) checked before `IsCastByVampires` in `BuildSpellJson`;
  `LoadOrderSource` sets it false. No adapter or Spell Research rule reads the mark (only
  LibrarianClassify and LibrarianRules, the scan's tag rules).
- The walk itself becomes a stepped object in `SpellScannerCasters.cpp`: the NPC array in slices
  under a time budget, keeping its race, template and list caches between slices; the SPID read as
  the last step. `IsCastByVampires` finishes whatever is left at once (budget unlimited) before it
  answers, so every consumer (AutoScan, panel scans, tome scan, Papyrus) sees a complete set.
- Prefetch at the main menu: after data load a worker posts 4 ms slices to the game thread (as
  AutoScan's PostStep does) until the walk is done, so it is normally finished seconds before any
  scan. Game thread only: other plugins may still change NPC records, and the reads are not locked.
- A scan that starts before the prefetch ends pays the rest in its first spell build (AutoScan: in
  its own slices, the walk stepped under its budget).

6b. Release off the game thread (about -110 ms)
- The spell JSON map and the parsed catalog are moved to a worker thread and freed there after
  Apply. Pure memory, no game forms touched.

6c. Files read before data load (about -78 ms)
- At kPostLoad a worker reads and parses `spell_catalog.json` and the adapter JSON files (no game
  forms). At data load Apply takes them if each file's size and write time are unchanged, otherwise
  reads them as today.

6d. Measure the spell JSON, then trim it
- Stage timers inside the spell JSON build (name, tier, effects, effect evidence) in the same build
  as 6a-c. One game start shows which part of the remaining ~365 ms to cut; that cut is planned from
  the numbers, not now.

Gate for 6a-c: offline report byte-identical (the planner is untouched); librarian-test,
progression-check; in game one start: perk adapter and Spell Research parity files byte-identical
to `build/parity-baseline/`, AutoScan SELFTEST all PASS (HITCH, COMPLETE), and a self-test that
compares the stepped vampire set with a one-go walk (same spells). Expected start: about 790 ms
before 6d.

6e. More before the in-game run (user 2026-11-01: "fix as much as possible before testing")
- Which spell JSON fields the planner reads: each field left out of the scan dump in turn, offline
  report compared, and the planner's field reads grepped. Unread and costly: traits (now off in
  LoadOrderSource). Base effect JSON built once per MGEF per pass and copied (FieldConfig::baseEffects).
- `FindPlugin` keeps each found plugin per thread (the game's lookup walks all 4,330 files).
- `ReadJsonFile` reads whole and parses from memory; strict parse failures fall back to the stream
  parse, so accepted files and values are unchanged.
- Stage timers in the startup tree (read, validate, apply) and in Spell Research's reconcile.
- GPT review: 1 finding (allocation outside the try in ReadJsonFile), fixed.

## Risks

- The 1,420 ms split is inferred until Phase 1 runs. After the vampire flag (~394 ms) the rest
  (~1,026 ms) must fall by about 776 ms (76%) to reach the target; Phases 2-3 may not be enough
  alone, and Phase 2b or 4 is decided from the timers, not now.
- If any adapter rule turns out to depend on `castByVampires`, the parity check fails and Phase 3
  is dropped.

## Project Structure

### Documentation (this feature)

```text
specs/003-load-time/
├── spec.md
├── plan.md
├── research.md
├── quickstart.md
└── checklists/requirements.md
```

No data model or contracts: no new data, and the only new interface is the resolver in
`SpellScanner.h`.

### Source Code (repository root)

```text
plugins/spelllearning/
├── include/SpellScanner.h                     # FieldConfig.castByVampires, resolver declaration
└── src/
    ├── Main.cpp                               # OnDataLoaded step timings
    ├── librarian/LibrarianPerkAdapterPatch.cpp    # stage timers
    ├── librarian/LibrarianLoadOrderSource.cpp/.h  # shared resolver, vampire flag off
    ├── spellscanner/SpellScannerFormId.cpp    # resolver over ResolveWith
    ├── spellscanner/SpellScannerJson.cpp      # honour FieldConfig.castByVampires
    └── spellscanner/SpellScannerCasters.cpp   # time in the summary line
docs/ARCHITECTURE.md                           # new timing lines
```
