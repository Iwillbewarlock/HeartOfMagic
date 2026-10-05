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

## Measurement 2 (2026-10-06, `score_proto.py`, leave-one-plugin-out)
- + long summon (over an hour: Witcher Horses 99 days, nixads 300 days), horse summon, banish-only, effect/name
  words: at most 37-40/89 caught with 3-16 false outs. Words do not generalize to an unseen plugin.
- Plain rule set (free, long summon, horse summon, banish-only): 42/89, 13 false - the horse summons that fight
  (skeletal horse, phantom mare...) and two long-lived summons. Some of those "in" labels are questionable (Witcher
  Horses' "황혼 소환" judged in) - horse summons need a human decision.
- The 47 the record cannot reach: SexLab Eager NPCs' 20 detect/inject spells (no shared structure; its master is
  the only tie), script-only conveniences (food, chest, bread, ore, book), follower calls (Feris, Melana), quest
  helpers. Their difference lives in the attached Papyrus script and the plugin's masters, not in any field the
  scan reads. 89 "out" from ~15 plugins is too few for a learner to generalize to an unseen mod family.

## 2026-10-06 rules.json `adultFramework` 측정

- Confirmed: 마스터(SexLab.esm, SexLabAroused.esm, OSLAroused.esm, OStim.esp)에 기대는 카드의 플러그인은
  `SexLab Eager NPCs.esp` 하나(플러그인 헤더 MAST 를 직접 읽음, scratchpad `rule_measure.py`). 효과 원형이 전부
  {Script, DetectLife, ValueModifier} 안인 주문 20개, labels.json 에서 20개 모두 out. 오탐 0.
- 표본이 한 플러그인뿐이라 ValueModifier 만 있는 주문(양성구유 오라 1개)이 다른 SexLab 의존 모드의 진짜 버프를
  빼는지는 이 로드 오더로는 모른다.
