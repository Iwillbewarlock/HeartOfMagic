# Spell gate labels, round 2026-10-05

Dump: TAKEALOOK dev install, `spell_scan_output.json` 2026-10-05 22:52 (3,775 spells). Cards: 1,517 tree candidates
(taught by a tome, not voice slot, not a copy; free spells and the blacklist kept in as known "out", 39).

## Confirmed
- Prompt v1 (qwen3.5:9b, batch 12, `verdicts-v1.json`): unusable. Known out recalled 22/39 (Witcher Horses, Inigo
  called "in"); 245 new "out", most of them ordinary spells (lockpicking, invisibility, night eye, cures, named boss
  spirit summons) - the prompt leaned on "combat".
- Prompt v2 (same model, 12-card probe): 9/12 as I would judge. Misses: a named boss spirit summon called a
  follower (hallucinated "Inigo"), an indrik "mount" called in, Stellaris "천체 유성" called out.

## Hypotheses
- A 9b local model alone is not a label source; local v2 + Gemini Pro second opinion on the local "out"/"unsure"
  and a 100-card sample of "in", with Claude deciding the disagreements, may be.

## Round result (2026-10-06)
- Local v2 full run: 1,502 judged (in 1,262 / out 239 / unsure 1); known out 21/39 - still unusable alone.
- Gemini 3.1 Pro on 358 cards (local out/unsure, the 39 known, 100 random local "in"): its "out" list reads right
  (mounts: Witcher Horses, indriks, nixads; pets; Inigo, Feris, Melana, Serano, Sa'chil; SexLab Eager NPCs 22;
  WishMagic and other convenience spawns; quest helpers). Local "in" sample agreed with Gemini almost everywhere.
- Labels (`labels.json`): known rule "out" > Gemini > local "in". in 1,411 / out 89 / unsure 17.
- Record features only, naive Bayes, leave-one-plugin-out: threshold 2 catches 42/89 with 23 false outs; the free
  rule alone catches 37 with 0. Structure alone barely beats the rule. Strongest single signals: free (37/89 vs 0),
  permanent summon (duration 0), summoned horse keyword (DummyHorseReanimate, RKF_ActorTypeHorse), DetectLife-only.

## Hypotheses (next)
- What the Gemini "out" set has in common is not in the dump's structure: the summoned actor is a mount or a
  follower (race/faction), the plugin is an adult or follower mod (masters), the description says "mount", "pet",
  "follower". Text tokens + those game facts should be measured next.
