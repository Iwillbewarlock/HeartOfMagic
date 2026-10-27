# Spell tomes that teach nothing: find their spell through Spell Research's tome pairs - design

Date 2026-10-27. Branch: release/1.6 (then merged forward). Status: built 2026-10-27 (release/1.6), reading option (a); see docs/ARCHITECTURE.md, "Spell tomes that teach nothing".

## Problem

A Nexus reporter's tree was empty: the log said `TomeReach: reading 4332 plugins for 0 tomes`. They run Spell
Research with Spell Research Synthesizer (github.com/audriuska12/SpellResearchSynthesizer, a fork of
Synthesis-Collective/SpellResearchAlternateTomePatcher). For each tier whose "Process" option is on (all but Novice
by default), the patcher overrides every spell tome it knows in SpellResearchSynthesizer.esp:

- `bookOverride.Teaches = new BookTeachesNothing()` (or `Teaches.Clear()` with its experimental option) and
  `BookText` = the spell's archetypes (Program.cs:722-732). The name is kept; an optional script grants archetype XP
  on first read.
- The tome-spell pair goes to Spell Research's per-tier lists `_SR_ListTomes<tier>` / `_SR_ListTomeSpells<tier>`
  (same index = one pair): through a generated FormList Manipulator ini (`SpellResearchSynthesizer_FLM.ini`, option
  Generate FLM Ini, Program.cs:313-360, 435) and/or Spell Research's own Import of the JSON patch it writes,
  `Data/SKSE/Plugins/SpellResearchSynthesizer/SynthesizedPatch.json` (Program.cs:279-290; entries carry
  `"spellId"` and `"tomeId"` as `__formData|<plugin>|0x<id>`).

Every place HoM decides "this book is a spell tome, and teaches spell S" asks `TESObjectBOOK::TeachesSpell()` /
`GetSpell()`, so all of those tomes vanish.

## Where HoM asks (release/1.6)

| Site | What it decides |
|---|---|
| `spellscanner/SpellScannerScan.cpp:355-357` | the scan's tome list: which spells enter the tree |
| `spellscanner/SpellScannerCopies.cpp:85-86` | `TomesBySpell()` (copies, the Librarian's Spell Research registrations) |
| `tomereach/TomeReach.cpp:136-137` | the seed set for the "is this tome handed out anywhere" check |
| `SpellTomeHookInventory.cpp:27` | whether a moved item could be a tome (cache invalidation) |
| `SpellTomeHookInventory.cpp:105-107` | `PlayerHasSpellTome` (the carried-tome XP bonus) |
| `tomereach/TomeReachRecords.cpp:192` | raw BOOK DATA read on the worker thread (spell only when the flag is set) |

Not a site: the read hook (`SpellTomeHook.cpp`) patches the AddSpell call inside `TESObjectBOOK::Read`, which a book
that teaches nothing never reaches; DEST/ISL events likewise.

## Design

### One answer for "which spell does this tome stand for"

New `SpellTomes` unit (game thread writes, any thread reads an immutable snapshot swapped under a mutex):

```cpp
namespace SpellTomes {
    RE::SpellItem* SpellOf(const RE::TESObjectBOOK* book);   // Teaches first, then the pair map; nullptr = not a tome
    void ForEachTome(const std::function<void(RE::TESObjectBOOK*, RE::SpellItem*)>&);  // every tome, both kinds
    void Rebuild(std::string_view reason);                   // game thread
    std::string StatusLine();                                // counts per source, for the log and the panel
}
```

The five call sites above use it instead of `TeachesSpell()/GetSpell()`. A book that teaches a spell keeps that
spell even if a pair names another (the record wins; the conflict is logged once).

### Where the pairs come from, in order

1. **Spell Research's live lists** (SpellResearch.esp loaded): for each tier t, `_SR_ListTomes<t>` and
   `_SR_ListTomeSpells<t>` read as pairs index by index, the record part (`forms`) and the script-added part
   (`scriptAddedTempForms`) each on its own, a part used only when both lists' parts have the same length (the
   Librarian already applies this rule, `LibrarianSpellResearchPatch.cpp` tomesPaired). Covers FLM (applied at data
   load) and SR's Import (script-added, in the save).
2. **SynthesizedPatch.json** when present: the `tomeId` / `spellId` pairs, resolved against the load order. Covers
   the main-menu scan before any save is loaded (SR's script-added part is empty then) and players who did not use
   the FLM option. Read only; unknown or unresolved ids are counted and skipped.

A pair is used only when the book resolves to a BOOK, the spell to a SPEL, and the book does not teach a spell.

### When it is rebuilt

kDataLoaded (after SpellResearch.esp and the JSON are readable; FLM's own kDataLoaded order is not guaranteed, so
also) right before each scan, kPostLoadGame, and after SR's Import finishes (the Librarian already listens for that
message box). Rebuild cost: O(pairs), a few thousand at most.

### Reading such a tome

Out of scope for this fix (decision for the author): HoM's read hook never sees these books, so reading one does
what Spell Research's script does (archetype XP), not HoM's tome learning. Options for later: (a) leave it - the
tree and its gates work, spells are learned through HoM's XP and Spell Research; (b) a Book Menu open listener that
treats the first read of a paired tome as HoM's tome read (XP / learning target, never AddSpell).

### Log and tests

- Startup line: `SpellTomes: N tomes - T by the book's own spell, P by Spell Research pairs (records R, script S),
  J by SynthesizedPatch.json; X pairs skipped (reasons)`. TomeReach's "for 0 tomes" then counts all kinds.
- Offline test (tools/, no game types): the pairing rule (equal parts pair, unequal part skipped, record and script
  parts kept apart, a teaching book not overridden), and the JSON reader (good entries, `__formData` with a missing
  plugin, malformed id, non-UTF-8 text) - normal / edge / failure cases.
- Self-test (dev selftest.json): on a load with SpellResearch.esp, the counts above logged PASS when every source's
  pair resolves, SKIP when SR is not loaded.

## Rulings after the GPT design review (2026-10-27; five findings, code claims checked and accepted)

1. Equal lengths prove nothing (FLM adds with HasForm per list and re-applies at every load, so a re-run patch on an
   old save can shift both lists into wrong but equal-length pairs). The source order is turned round:
   **SynthesizedPatch.json is the primary source** (explicit tome-spell pairs from the same run as the plugin), and
   the live SR lists are only a fallback when no JSON is found, logged as "unverified". Where both exist, a live
   pair that disagrees with the JSON is not used and is counted as a conflict.
2. (Revised 2026-10-27 after implementation.) The JSON is trusted for a book whose winning override teaches
   nothing, in whatever plugin that override is: the patcher names `SpellResearchSynthesizer.esp` only when run
   standalone (`SetTypicalOpen`, its Program.cs:31-32); inside the Synthesis app it writes the group's plugin
   (`Synthesis.esp` by default). A book that teaches a spell keeps its own. The pairs used are logged per winning
   plugin ("JSON tomes by winning plugin: Synthesis.esp 412, ..."). An unreadable JSON leaves the fallback, never
   a silent accept. (The first text tied the JSON to SpellResearchSynthesizer.esp alone; that would have rejected
   every pair of a patch run through Synthesis.)
3. Live lists are read only when Spell Research is set up and not importing (`_SR_GlobalIsSetup` == 1,
   `_SR_GlobalImporting` == 0, the Librarian's Reconcile rule, LibrarianSpellResearchPatch.cpp:366-370); otherwise
   the last good snapshot stays. An Import finishing triggers a rebuild even when the entry count is unchanged.
4. The map has a generation number. A new generation re-runs TomeReach with value inputs of that generation (a
   result of an older one is dropped), clears `g_duplicatesSettled` and the duplicate groups
   (SpellScannerDuplicates.cpp:162-185) and the carried-tome cache (SpellTomeHookInventory.cpp).
5. "No pairs yet" is not "zero tomes": when the generation changes after a scan was made with fewer tomes (main menu
   without the JSON, then a save with SR's script-added pairs), the saved scan is marked stale and the auto build
   (autoBuild.js, which keeps `_decided` and does not retry after a failed run, :195-217) gets one retry at the next
   safe point (panel open or main menu). The reporter's case (JSON present) is covered at the first main-menu scan.

Tests added to the plan: re-run patch on an old save (shifted lists vs JSON), JSON present but the book overridden
by another plugin, scan during Import (snapshot kept), generation change after a scan (stale + one retry), duplicate
copy tome carried after a rebuild.

## Not in scope

Spell Research's scrolls, artifacts and alchemy lists; changing Spell Research's or the patcher's data.
