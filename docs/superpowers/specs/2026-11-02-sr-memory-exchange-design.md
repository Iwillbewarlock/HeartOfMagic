# Practice and memory: HoM learning XP <-> Spell Research archetype XP - design

Date 2026-11-02. Branch: feature/sr-memory-exchange (from speckit-perf after merging sr-import-signal).
Status: design, approved by the author 2026-11-02; built 2026-11-02, not run in game.

## Idea (the author's words, 2026-11-02)

"HoM-style XP is experience; SR's is memory. They give and take." HoM learning XP earned by casting is
practice. Spell Research's archetype XP (60 archetypes x 5 ranks of GlobalVariable) is what the character
remembers. The two feed each other:

- **A. Practice becomes memory.** Casting XP a learning target earns in HoM adds SR archetype XP to that
  spell's archetypes, the way researching the spell does in SR.
- **B. Memory guides practice.** Casting XP for a learning target is raised by the share of the spell's
  archetypes the character already remembers at the spell's rank, up to +25%.

Already there, unchanged: SR -> HoM through the SR Prisma UI bridge (`_SRO_HOML.psc`: research hour, analysis,
thesis give a share of the target's required XP as source `spell_research`; compose offers the spell as a goal
with a 35% head start through `AddRawXP`).

Main counterargument (accepted): archetype XP has one gameplay consumer, the compose success chance
(`_SR_EffectComposeSpellScript`). Feeding practice into it makes composing easier for a player who never
researches. Kept small, both halves can be turned off.

## Spell Research facts this rests on (SR 2.2.3, Upgrade and Fixes 1.4.3, Prisma UI scripts)

- Memory store: `_SR_ListGlobalsArchetypes` (`SpellResearch.esp|0x133D`), 60 FormLists in archetype order, each
  holding 5 TESGlobal: index 0 Novice ... 4 Master. All 300 default 0.0. "Known" in SR's MCM = value > 0.
- Spell -> archetypes: `_SR_ListSpellsArchetypes` (`SpellResearch.esp|0xD9E`), 60 FormLists in the same order;
  membership by `HasForm` (plugin entries and `scriptAddedTempForms`, which is where SR's Import and HoM's
  registrations live). Rank: which `_SR_ListAllSpells<tier>` holds the spell (compat json `allSpells`).
- Research gain (`_SR_EffectResearchScript.UpdateArchetypeList` + `_SRU_Utility.AdvanceSkillsEx`): for a spell
  of rank R and value v, for every archetype the spell has and slot s = 0..6, rank t = R + 2 - s, if 0 <= t <= 4:
  `global[a][t] += v ^ ((s + R + 1) / 3)`; `ModGlobalVariable` adds only while the value is below 10,000,000 and
  clamps to it. One research hour: v = `_SR_GlobalResearchTimeBase` (`SpellResearch.esp|0x2B82`, 15.0) x hours.
  SR also calls `Game.AdvanceSkill`; HoM does not (casting already trains the school).

## A. Practice becomes memory

- Where: `ProgressionManager::OnSpellCast`, around its `AddXP(targetId, actualXPGain)`: progress before and after
  the cast's XP. Only casting counts. Tome reading, passive, ISL study, DEST, BookXP, other mods' sources and
  `AddRawXP`/`SetSpellXP` do not: they are not practice, and SR's own share (`spell_research`) and compose's
  head start would otherwise come back as memory.
- Chunks, no state: progress is cut at every 15% (`kChunkPercent`: 15, 30, ... 90) and at 100%. Each boundary a
  cast crosses pays one fixed payment, as if the spell were researched once for `memoryMinutes` (default 25):
  `v = timeBase x memoryMinutes / 60` (6.25 at defaults), put through SR's gain table once per boundary. The
  payment is fixed, not the span: SR's gain `v ^ ((s + R + 1) / 3)` is not linear, so paying a summed span
  would make the total depend on how the progress was cut (Codex design review 2026-11-02: 15% twice 172.2 vs
  30% once 434.0 for a Master spell's Master global). A spell practised from 0 to mastery crosses 7 boundaries:
  7 payments of v. SR's own research is not linear in session length either, so this is not "N research hours";
  it is "a short research session per 15% of practice". SR's curve on small v (< 1) shrinks the higher-rank
  gains, which is why it is not paid per cast.
- LIMIT (in code): no per-spell record of what was paid. Progress from other sources inside a chunk is paid
  with the next cast that crosses its boundary (a tome read to 25%, then a cast crossing 30%, pays 15%), and a
  boundary crossed by a non-cast source pays nothing. A progress reset pays again: `RelockSpell` (cheat mode) or
  `SetSpellXP(spell, 0)` (Papyrus/C++ API, the panel's XP edit) puts the spell below its boundaries, and practising
  it again pays its 7 payments a second time (review round 1, 2026-11-02). Upgrade path: a per-spell "highest
  boundary paid" byte in the co-save progress record (a version bump), checked before paying.
- Skip silently (debug log) when: SR not loaded, `_SR_GlobalIsSetup` != 1 or `_SR_GlobalImporting` != 0, the
  feature is off, the panel's cheat mode is on (top-level `cheatMode`; both halves, author's decision 2026-11-02:
  unlocking, relocking and XP edits are not practice), the spell is in no tier list or no archetype list.
- One info log line per payout: spell, boundaries crossed, v, rank, archetypes paid.
- Stacks with SR's own research-mode cast reward (`_SR_EffectResearchScript.OnSpellCast`, v = 1.5 per cast of
  the equipped spell while SR research runs): allowed on purpose, both are practice.
- LIMIT (in code): SR's Papyrus `GlobalVariable.Mod` is GetValue then SetValue; a C++ write landing between the
  two would be lost (or SR's would). Only possible while SR research runs and the cast crosses a boundary; one
  payment at most. Not guarded.
- Where it lands in OnSpellCast: inside the duplicate-key check, the progress read before `AddXP` by value and
  after through `GetProgress`, before the single-mode `break`. Mastery inside `AddXP` clears the target but keeps
  the progress record, so the last boundary (100%) is paid.

## B. Memory guides practice

- `known = average over a in archetypes(spell) of min(1, global[a][R] / KnownFull(R))`, with
  `KnownFull(R) = ((R + 1) x d)^2` and d = `_SR_GlobalCraftSpellDifficulty` (`SpellResearch.esp|0x9EB3`, 3.0 in the
  ESP; read at call time, 3.0 when missing, not positive or not finite): SR's own compose scale
  (`_SR_EffectComposeSpellScript.psc:249`, MinChance x `Math.Pow((SpellRank + 1) x difficulty, 2)`), 9 for Novice
  and 225 for Master. Proportional, not "value > 0" (the first design, review round 1 2026-11-02): A's slot 2
  payment (t = R) puts something into every global[a][R] of the spell, so a "> 0" share was 1 after the first 15%
  and B a flat bonus. `multiplier = 1 + maxPracticeBoost% x known`, `maxPracticeBoost` default 25% (the size of the
  existing tome inventory boost). Applied to `xpGain` right after the tome inventory boost, before the source caps, so the caps
  still bound it. 1.0 whenever A's skip conditions hold.
- No loop growth: B is capped, A pays only from cast XP, a mastered spell takes no more XP.
- Cost: no cache. Each cast reads the spell's rank (5 tier lists) and archetypes (60 lists, `HasForm` over
  plugin and script-added entries) for each credited target: a few thousand comparisons, tens of microseconds.
  A cache keyed on list sizes would miss an Import that changes membership with equal counts (Codex design
  review 2026-11-02); reading every time has nothing to invalidate.

## Settings (config.json, section `spellResearch`, beside `enabled`)

```json
"memoryExchange": { "practiceToMemory": true, "memoryMinutes": 25, "memoryToPractice": true, "maxPracticeBoost": 25 }
```

Missing keys = the defaults. Clamped: `memoryMinutes` 0..240, `maxPracticeBoost` 0..100 (percent).
Panel: Settings > Spell Tome Learning, under the Spell Research line: the two switches and the two numbers.

## Code layout

- `include/librarian/SpellResearchMemoryMath.h`: pure (no game types): `BoundariesCrossed(old, new)`, the research
  gain table for (R, v) -> per target rank gains, `ApplyGain(value, gain)` with SR's cap rule, `KnownShare`,
  `PracticeMultiplier`. Checked by `progression-check --self-test` (normal, boundary, failure cases).
- `include/librarian/SpellResearchMemory.h`, `src/librarian/LibrarianSpellResearchMemory.cpp`: the game side
  (form lookups through the compat json persistent ids, the global writes, settings). Game thread only.
- `compat/spellresearch.json`: `lists.archetypeSpells` (0xD9E), `lists.archetypeGlobals` (0x133D),
  `researchTimeBase` (0x2B82), `craftSpellDifficulty` (0x9EB3). The outer lists are read by index (`forms[i]`,
  then the script-added part as stored), a null entry keeping its slot, so archetype a stays paired with its globals.
- Dev self-test `sr-memory` (EnvironmentSelfTest): with SR set up, one spell in SR's lists: snapshot its globals,
  pay a chunk through the game-side function, compare every changed global with the math header, restore the
  snapshot through an RAII guard (also on an exception or early return; the test leaves the save as it was); then
  B's multiplier against the formula. The rank mapping is checked by name (each global's editor id ends with
  `<t+1><Rank>`). `sr-memory-hook`: `OnPracticeProgress(spell, 10%, 32%)`, the hook's own entry, must pay two
  payments (SKIP with practiceToMemory off). The panel shows its rows only when the status says `memoryReady`.
  PASS/FAIL/SKIP.
- Save persistence of direct `TESGlobal::value` writes: the save's global variable table holds every global, but
  that the engine collects a native write was not proven (Codex: 추정). At `kPostLoadGame`, with SR set up, one
  info line: the sum of the 300 memory globals and how many are above 0. The author's normal save and reload
  shows it in the log.

## Not done

- Writing memory through SR's Papyrus (`AdvanceSingleSkill`): its signature differs between SR builds and it shows
  SR's pop-up; direct writes work with every build. The Prisma UI knowledge-change table does not see them.
- Changing `_SRO_HOML`'s direct research share; looked at again after B's effect shows in logs.
- Sleep as the moment practice turns into memory: a later option.
