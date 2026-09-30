# Round 2026-09-30b

- Dump: TAKEALOOK `spell_scan_output.json` of 2026-09-30 15:30 (3155 spells; first dump with
  summonedKeywords, targetKeywords, applies), catalog from commit ff22d11 (race and target rules)
- Sample: `node judge.js sample` with seed 20260931, 120 mod spells
- Judges: two AI agents, 60 each, told the settled decisions (creature/human = target, blood takes in
  vampiric magic, summons by race, soul not on daedra or machines)
- Result on ff22d11: 85.4% (270 / 316). One spell, Mysticism's Dispel, carried 15 of the 46 wrong tags
- After the fixes of this round (Dispel/Dismiss in 80_manual, atronach hook on undead summons, KIT resist
  debuffs only on their resist value): 90.6% (270 / 298)
- Left, mostly narrative: soul on daedra summons whose race is not marked daedra (4), bound weapons of
  holy make read as daedra, SpeedMult buffs as stamina, walls and storms as trap
