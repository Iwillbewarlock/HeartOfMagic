# Quickstart: verifying 003 Game Loading Time

## Offline (Claude)

1. Skyrim closed; `.\BuildRelease.ps1` passes; tools self-tests pass; panel tests pass;
   `node tools/librarian-check.js` passes (scanner fields and perk adapters changed).
2. Parity: the dev-only parity file (Phase 0: sorted `(effect, keyword)`, `(list, spell)`,
   `(spell, perk)` sets and the Spell Research registrations) from the baseline launch and from the
   after launch are identical. Counts alone are not enough; the report's `elapsedMs` is ignored.

## In game (user, two launches: baseline with Phase 0+1, after with Phases 2-3)

1. Start the game, wait for the main menu, load the usual save, quit.
2. Claude reads `SpellLearning.log`:
   - The OnDataLoaded summary line (and `Apply` timed from outside): total ≤ 500 ms (SC-002).
   - `PerkAdapters: data loaded - 399 keywords on effects ...` with the same counts as before.
   - `Startup tree: 1365 of 1365 entries applied` (SC-004).
   - Revert, cosave load and the PostLoadGame summary sum ≤ 100 ms (SC-003).
   - Time from the handler's end to "Panel DOM ready" (User Story 3, reported, PrismaUI's share named).

## Feature list to confirm unchanged

- Keywords on effects, FormList entries, casting perks (parity file).
- Spell Research registrations (parity file, same `LoadOrderSource`).
- `Reassert` after save load (re-adds removed keywords).
- Full scan still marks `castByVampires` (scan output of a full scan contains the flag).
- Librarian classify rules that use `castByVampires` still match (librarian-test, librarian-check).
