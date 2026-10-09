# NPCs React To ... vs Heart of Magic (research, read-only)

Method: houseCARL record reads of the five ESPs (quest/INFO conditions), Papyrus source of the healing script, HoM adapters/vanilla.json. Nexus descriptions NOT fetched.

## 1. Detection per mod
- Necromancy (And More): all 465-record scan (212 INFOs) are dialogue lines (quest Reactions_ConjurationParty) with condition HasMagicEffectKeyword run on the player: MagicSummonUndead (194 uses), MagicSummonShock (66), MagicSummonFrost (66), MagicSummonFire (68), MagicSummonFamiliar (42). Quest also gates on Conjuration skill, factions, voice types, HasSpell MAG_ReanimateCorpseLeftHand (negated), Vampire keyword, WISpellDangerous keyword (13 uses). No script, no FormID of a spell.
- No Consequences Patch: 216-byte ESP, only masters Skyrim + the necro ESP; no records (a flag edit/empty override). Nothing to detect.
- Invisibility: quest alias conditions: GetDetected(player) + HasMagicEffectKeyword MagicInvisibility on the player, minus HasMagicEffect MAG_VoiceBecomeEtherealEffect01 and AbGhostPotionEffect; 166 INFO conditions use MagicInvisibility.
- Frenzy: INFO conditions (102): HasMagicEffectKeyword MagicInfluenceFrenzy on CombatTarget OR HasMagicEffect MAG_AlchFrenzy (the vanilla potion MGEF, a FormID); plus bandit-faction checks.
- Always React To Healing: no ESP. SayOnHitByMagicEffectScript (ActiveMagicEffect) makes the hit actor Say(topic) in OnEffectStart. It is a script attached to the magic effect record, not keyword based.
- Additional Healing Reactions: only new voice lines (GetIsVoiceType, health %, GetIsID) for the vanilla topic WICastMagicNonHostileSpellHealingTopic. Same trigger chain as vanilla, so still the MGEF script.

## 2. Do modded spells trigger?
- Necro/Invisibility/Frenzy: pure vanilla keyword checks, so a modded MGEF without the keyword is missed. Keyword on the MGEF is all that matters.
- Healing: depends on the MGEF having the script. A modded healing MGEF without it never triggers; a keyword cannot fix that (HoM adapters only add keywords).

## 3. HoM vanilla.json coverage
Covered: MagicSummonUndead (race rule, tag undead, plus Reanimate archetype), MagicSummonFire/Frost/Shock, MagicSummonFamiliar, MagicInvisibility (archetype Invisibility), MagicInfluenceFrenzy (archetype Frenzy).
Not covered: WISpellDangerous (13 necro lines; not in any adapter), MAG_AlchFrenzy / MAG_VoiceBecomeEtherealEffect01 / AbGhostPotionEffect (FormID checks on vanilla effects, not relevant to spells), the healing script (not a keyword).

## 4. Verdict
(a) already works for Necromancy, Invisibility, Frenzy: vanilla.json already attaches every keyword these mods read, to modded spells that load-time tagging/archetypes classify. Residual risks are classification misses, not missing entries.
Optional (b), minor: a WISpellDangerous line (e.g. hostile/offensive necromantic spells) would only add that keyword; low value, effect on 13 lines, uncertain semantics.
Healing: (c) is not a keyword job; a script/SKSE addon would be needed and is out of scope of HoM adapters. No addon justified.

## Not verified
- Nexus descriptions (not fetched). Dialogue text and what 0A9B1F (WISpellDangerous) conditions gate exactly. Whether the healing script is attached to vanilla restore MGEFs in this load order (assumed from vanilla). Whether HoM's keyword runs actually reach every modded summon/reanimate effect in-game (adapter rules not tested here). Two plugins failed to load in housecarl (unrelated).
