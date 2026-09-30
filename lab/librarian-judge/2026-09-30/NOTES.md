# Round 2026-09-30

- Dump: TAKEALOOK `spell_scan_output.json` of 2026-09-27 (3546 spells), catalog from the rules of that morning
- Sample: 120 mod spells, seed 20260930 (drawn by an earlier Python copy of `judge.js sample`; the Node
  sampler draws a different 120 for the same seed)
- Judges: two AI agents, 60 spells each, against TAGS.md and LIBRARIAN.md 3절 as they stood that morning
- Result then: precision 78.8% (271 / 344). Same verdicts on the rules of commit 62e0de8: 87.6% (268 / 306)
- Predates later decisions, so these verdicts count as wrong what is now intended:
  - `blood` on vampire drains and absorbs (7) - blood takes in vampiric magic since the same day
  - `creature` on summons was judged wrong already, as now decided
