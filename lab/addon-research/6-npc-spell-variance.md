# NPC Spell Variance (Nexus 132097) vs a Heart of Magic perk adapter

Research only. Nothing under D:/TAKEALOOK or the repo was modified. Source of the plugin logic: `hook.cpp` fetched from the
author's GitHub (LeoneKingzz/NPC-Spell-Varaince-Unified, HEAD, saved next to this file as `hook.cpp`), cross-checked against the
installed 2.7.2 DLL/ESP strings. The GitHub HEAD may be newer than the installed 2.7.2 (not verified).

## 1. Keywords: what they are, where defined, what the plugin does without them

Defined in `NPC Spell Variance.esp` (D:/TAKEALOOK/mods/NPC Spell Variance - Spell Variety AI/), 389 KYWD records, load index 03,
FormIDs 0x03000800+ (parsed from the ESP). The ones a spell-pack author must set (input keywords):

| Keyword (EditorID, FormID low 24) | Meaning (from hook.cpp use) |
|---|---|
| NSV_Magic_Damage 0x825 | attack spell. Gives the spell a "damage" AI effect (5 random tiers when the spell's castingPerk is in `NSV_Perks_FormList`) |
| NSV_Magic_Heal 0x81C | heal spell; AI skips it at full health, targets hurt allies |
| NSV_Magic_Cloak 0x81A | cloak (self, concentration/fire-and-forget) |
| NSV_Magic_Armour 0x81E | armor buff spell (oakflesh type) |
| NSV_Magic_Ward 0x823 | ward |
| NSV_Magic_Invisibility 0x81F | invisibility |
| NSV_Magic_Summon 0x822 | any summon (with NSV_Tag_Weapon = bound weapon, NSV_Tag_Ally = summon-for-ally) |
| NSV_Magic_Trap 0x827 | rune/trap (placed at target location) |
| NSV_Magic_Fear 0x821, NSV_Magic_CalmSeduce 0x820, NSV_Magic_Control 0x81D | influence spells |
| NSV_Magic_Absorb 0x824 | drain/absorb |
| NSV_Magic_Teleport 0x826 | teleport (+ NSV_Tag_Enemy = teleports the enemy away) |
| NSV_Generic_Buff 0x829, NSV_Generic_Debuff 0x82A | other buff / debuff |
| NSV_Survival_Lethal 0x828 | "survival" spell used when near death |
| NSV_Tag_Undead 0x82B, NSV_Tag_Summoned 0x82D | spell is meant for undead / summoned targets (picks an alternate AI effect) |
| NSV_Tag_Ally 0x82E, NSV_Tag_Enemy 0x82F, NSV_Tag_Weapon 0x830 | target / sub-kind selectors |
| NSV_Tag_Area 0x831, NSV_Tag_Vampiric 0x82C, NSV_Resource_Convert, NSV_Tag_Health_to/Magicka_to/Stamina_to, NSV_Tag_pHealth/pMagicka/pStamina | present in the ESP and used by patch authors (148 / 44 uses) but **no `HasKeyword(NSV_Tag_Area)` etc. exists in hook.cpp** (only looked up). Effectively dead in the shipped code; do not map them |
| NSV_CActorSpell_Exclude, NSV_BaseWeaponTypes, NSV_SpellSword/Healer/Support/VampireLord/ModCombatInvent_Exclude | exclusion/actor flags, not spell classification. Out of scope |

The ~330 other keywords (`NSV_Aimed_*`, `NSV_Self_*`, `NSV_TA_*`, `NSV_TL_*`, `NSVTACC...`) are the plugin's own output markers
on its 351 MGEFs in the ESP (`NPC Spell Variance Assets.esp` holds 84 more).

**What it does by itself.** At the KID event `KID_KeywordDistributionDone` (hook.cpp ~l.721 "recieved KID finished event") it runs
`Patch_Spell_List()`:
1. takes every SPEL not in `exc_mods` / carrying `exc_keywords` (`SKSE/Plugins/NPCSpellVariance.ini`: `exc_mods = Heroes of Yore.esp|TheBeastWithin.esp|...`, `exc_keywords = HoY_MagicShoutSpell|LDP_MagicShoutSpell|NSV_CActorSpell_Exclude`),
2. drops anything that is not `SpellType::kSpell`, is Touch, Scroll or ConstantEffect, or has an effect with archetype
   ValueAndParts/Telekinesis/Concussion/SlowTime/Disguise/VampireLord/GrabActor/WerewolfFeed/CureAddiction/Lock/Open/Werewolf/SpawnScriptedRef/CureDisease/NightEye/Guide/Light/Darkness/DetectLife,
3. appends one of its own MGEFs to the spell's effect list, chosen by `GetEffect_{FF|CC}_{Aimed|Self|TA|TL}(spell)`, which looks only at the **NSV_* keywords** (HasKeyword chain, e.g. l.3142 `if (NSV_Magic_Damage && a_spell->HasKeyword(NSV_Magic_Damage))`). When no keyword matches, `unrecognized_spell = true; continue;` (hook.cpp l.381): **the spell gets no AI effect**.

So the "scan" in the description is only the filter above. Classification is entirely keyword-driven; a spell without keywords is just
skipped (the old structure heuristics, hostile/fire/frost/heal flags, are commented out in the source, l.100-170). It also reads three
MGEF keywords directly: `MagicRune` (Trap sound, l.1835, l.1994), `MagicRestoreHealth` (l.2092), `MagicWard` (l.2123) - all of which
HoM's `vanilla.json` already writes.

Order of events: the patch step runs on the KID done event, i.e. at data load. HoM's `PerkAdapters::Apply(kDataLoadedReason)` runs in HoM's own
kDataLoaded handler (Main.cpp l.405). Which comes first is **not verified** (see risks).

## 2. What the per-pack patches contain

All are KID `Keyword = <NSV keyword>|Spell|<FormID>~<plugin>` (or `|Spell|<EditorID>`) lines: **target type is Spell, not MGEF.**
Count over the 5 installed packs (D:/TAKEALOOK/mods/NPC Spell Variance*/):
Darenii AIO 13 files (1896 lines total; Natura 562), Kittytail 4 files + a SPID file, Lost Grimoire 74 lines, Mysticism 1134 lines (includes
229 Skyrim.esm vanilla entries), Spell Variety AI base: `NPCSpellVarience - CommandedActorSpells_KID.ini` (17 lines). In total 1250 per-spell lines.
Per keyword: Magic_Damage 301, Tag_Area 148, Magic_Summon 148, Tag_Ally 146, Magic_Trap 73, Tag_Undead 49, Tag_Enemy 47, Magic_Heal 46, Tag_Vampiric 44,
Generic_Buff 40, Tag_Weapon 30, CActorSpell_Exclude 28, Magic_Cloak 27, Generic_Debuff 26, CalmSeduce 20, Absorb 17, Fear 14, Teleport 11, Armour 10, Ward 9, Invisibility 8.

Examples (Arcane_KID.ini, Mysticism_KID.ini, LostGrimoire_KID.ini):
```
;Arcane Bolt - Deals 10 magic damage to health and twice as much to magicka...
Keyword = NSV_Magic_Damage|Spell|0x810~Arcane.esp
Keyword = NSV_Magic_Damage|Spell|0xB1C~Arcane.esp          <- second record = the NPC / LeftHand copy
Keyword = NSV_Magic_Trap|Spell|0x82A~Arcane.esp
;Ash Cloud ...
Keyword = NSV_Tag_Area|Spell|0x1901E5~MysticismMagic.esp
Keyword = NSV_Tag_Enemy|Spell|0x1901E5~MysticismMagic.esp
Keyword = NSV_Generic_Debuff|Spell|0x1901E5~MysticismMagic.esp
Keyword = NSV_Magic_Summon|Spell|GRIM_SPELL_CON00_FamiliarWolf,...   (EditorID lists)
Keyword = NSV_Magic_Armour|Spell|0x5AD5C~Skyrim.esm
```
Each pack is per spell (often both the player spell and its NPC/LeftHand twin), hand-written from the spell descriptions.

## 3. Mapping to HoM tags / fields

Measured, not guessed: I joined the 1206 FormID-style lines of all 5 packs to `D:/TAKEALOOK/overwrite/SKSE/Plugins/SpellLearning/spell_catalog.json`
(3776 spells; 1204 of the 1206 are in the catalog) and compared the pack author's keyword with catalog techniques/elements/targeting/school.
Rough rule test (technique/element only), tp / fp / fn over all patched spells (fp = rule-true, patched with another keyword or not at all, so it
over-counts):

| NSV keyword | Candidate HoM condition | tp / fp / fn | Verdict |
|---|---|---|---|
| Magic_Summon | technique `summoning` (+ `effect.archetype` SummonCreature) | 144 / 4 / 0 | clean |
| Tag_Weapon | elements `weapon`+ technique `summoning` | 29 / 1 / 0 | clean |
| Magic_Trap | element `trap` (same as vanilla.json MagicRune rule) | 70 / 5 / 2 | clean |
| Magic_Ward | element `shield` (WardPower AV) | 9 / 1 / 0 | clean |
| Magic_Cloak | effect archetype Cloak (technique `cloak`) | 25 / 6 / 0 | clean via archetype |
| Magic_Invisibility | archetype Invisibility | 7 / 2 / 1 | clean |
| Magic_Armour | `armor`+`infuse`, self | 10 / 5 / 0 | clean, needs ward exclusion |
| Magic_Fear | technique `fear` / archetype Demoralize | 14 / 2 / 0 | clean |
| Magic_CalmSeduce | technique `pacify` / archetype Calm | 12 / 1 / 8 | medium (fn: frenzy/charm variants) |
| Magic_Heal | `curing`+`health`+self-target / MagicRestoreHealth structure | 40 / 3 / 5 | medium |
| Magic_Absorb | `siphon` + destruction + archetype Absorb | 17 / 22 / 0 | needs judgment (fp: Vampiric-type damage packs tag as Damage not Absorb) |
| Magic_Damage | detrimental health value-mod effects, school destruction/restoration(sun/necrotic) | 284 in packs; no single tag | needs structure rule, hardest, biggest keyword |
| Magic_Teleport | none: catalog has no teleport tag assigned (vocab lists it; 0 tp, 11 fn: tagged telekinesis/control/arcane) | 0 / 0 / 11 | cannot map yet; HoM rule needed first |
| Tag_Undead | element `undead`/`sun` | 44 / 69 / 2 | judgment: author means "use this vs undead", not "is undead"; LIBRARIAN.md l.100 already treats it as an NPC-AI marker |
| Tag_Summoned | n/a (2 uses) | | skip |
| Tag_Ally | summon whose target is an ally/familiar vs conjured hostile minion; 142 uses, 118 are `summoning` | | judgment (summons: Skyrim familiars vs atronachs) |
| Tag_Enemy | debuff on enemies (illusion/alteration `creature`/`human` targets) | | judgment; only read for Teleport (and l.3561 region) |
| Generic_Buff | `strengthen`+self | 10 / 15 / 28 | poor, author-intent |
| Generic_Debuff | `curse` | 14 / 18 / 9 | poor |
| Tag_Area, Tag_Vampiric, Resource_Convert, Tag_*_to, Tag_p* | not read by the plugin | | skip |
| NSV_CActorSpell_Exclude, actor keywords | not classification | | out of scope |

HoM fields used: elements (`trap`, `shield`, `weapon`, `armor`, `undead`, `sun`, `health`), techniques (`summoning`, `cloak`, `fear`, `pacify`, `curing`,
`siphon`, `infuse`, `strengthen`, `curse`), targeting (`self`/`actor`/`location`/`aoe`), school; casting is not needed (the plugin chooses FF vs CC from the spell's own
casting type). Targeting only decides Tag_Ally/Tag_Enemy-like intent.

## 4. Draft adapter JSON (existing format, no code extension for the keywords above)

`requires` gate: plugin file name in the load order is `NPC Spell Variance.esp` (the keywords live there; `NPC Spell Variance Assets.esp` holds MGEFs, not keywords).
`keyword` EditorIDs exist in the load order only when that plugin is loaded, so lines go silent otherwise (also by `requires`).
Lines are ordered so `keywordsNone` stays meaningful. NOT measured offline with `librarian-test`, a first draft:

```json
{
  "perkMod": "NPC Spell Variance - Spell Variety AI 2.7.2",
  "measured": "2026-10-06 (draft, tp/fp from catalog join, not from librarian-test)",
  "requires": { "any": ["NPC Spell Variance.esp"] },
  "adapters": [
    { "keyword": "NSV_Magic_Summon", "why": "any summon: conjure creature, reanimate",
      "effect": { "archetypeAny": ["SummonCreature", "Reanimate"] } },
    { "keyword": "NSV_Tag_Weapon", "why": "bound weapon summons",
      "tags": { "all": ["weapon", "summoning"] }, "effect": { "visible": true } },
    { "keyword": "NSV_Magic_Trap", "why": "runes, same structure as MagicRune",
      "tags": { "all": ["trap", "construct"] },
      "effect": { "detrimental": true, "delivery": "Target Location", "explodes": true } },
    { "keyword": "NSV_Magic_Cloak", "why": "cloaks", "effect": { "archetype": "Cloak" } },
    { "keyword": "NSV_Magic_Ward", "why": "wards",
      "effect": { "detrimental": false, "primaryAV": "WardPower" } },
    { "keyword": "NSV_Magic_Invisibility", "why": "invisibility", "effect": { "archetype": "Invisibility" } },
    { "keyword": "NSV_Magic_Fear", "why": "fear (illusion demoralize)",
      "spell": { "school": "Illusion" }, "effect": { "archetype": "Demoralize" } },
    { "keyword": "NSV_Magic_CalmSeduce", "why": "calm / charm",
      "effect": { "archetype": "Calm" } },
    { "keyword": "NSV_Magic_Heal", "why": "healing restore on self or allies",
      "tags": { "all": ["health", "curing"] },
      "effect": { "detrimental": false, "primaryAV": "Health", "archetype": "ValueModifier" } },
    { "keyword": "NSV_Magic_Armour", "why": "armor flesh buffs, not wards",
      "tags": { "all": ["armor"] },
      "effect": { "detrimental": false, "primaryAV": "DamageResist", "archetypeAny": ["ValueModifier", "PeakValueModifier"] },
      "spellLacks": [ { "primaryAV": "WardPower" } ], "keywordsNone": ["NSV_Magic_Ward"] },
    { "keyword": "NSV_Magic_Absorb", "why": "destruction health absorb",
      "tags": { "all": ["siphon"] }, "spell": { "school": "Destruction" },
      "effect": { "archetype": "Absorb", "primaryAV": "Health" } },
    { "keyword": "NSV_Magic_Damage", "why": "hostile health damage the AI should attack with (needs measuring)",
      "spell": { "notSchool": "Conjuration" },
      "effect": { "detrimental": true, "primaryAV": "Health", "archetypeAny": ["ValueModifier", "PeakValueModifier", "DualValueModifier"] },
      "keywordsNone": ["NSV_Magic_Trap", "NSV_Magic_Absorb", "NSV_Magic_Heal"] }
  ]
}
```
Open items for the draft: archetype spelling for Demoralize/Calm/Reanimate/DualValueModifier follows `vanilla.json`/scan names but DualValueModifier was not
checked against the scanner enum; `Magic_Damage` needs an offline `librarian-test` run; `Tag_Undead`, `Tag_Ally`, `Tag_Enemy`, `Generic_*`, `Teleport` are left out on purpose (author-intent or no HoM tag).

**Does the format need extending?** For the keywords above the written target is the first visible MGEF, and `MagicItem::HasKeyword` is what the plugin calls on the SPELL
(`a_spell->HasKeyword`). The adapter doc (PERK_ADAPTERS.md section 8, in-game test) states "the engine reads effect keywords when answering spell-side perk conditions", and
`PerkAdapters.h` CarriedKeywords counts "its own and every effect's". If that holds for MagicItem::HasKeyword (the CommonLib header only shows `HasKeyword` as an override at
MagicItem.h:78; I did not find the implementation), **no extension is needed**. If it did not, a spell-level write (keyword onto the SPEL's BGSKeywordForm, which is what KID does) would
need a new `target: "spell"` field plus a spell-level leak check (no shared-effect problem, simpler). Not verified in game.

## 5. Overlap and value

- **Double keyword harm.** Adapter skips a spell that already carries the keyword (spell record or any effect), so after KID writes the SPELL first nothing is added. If the adapter ran first
  and KID adds later, KID `AddKeyword` is idempotent for the same BGSKeyword (a keyword array holds each form once; the usual SKSE keyword form). No double-AI risk: the plugin only checks presence,
  so a duplicate is invisible. Not tested in game.
- **Order risk (the real one).** NSV builds its AI effect list once, at `KID_KeywordDistributionDone`. If HoM's kDataLoaded `Apply` runs later than that event, newly tagged spells get no
  AI effect until the next restart cycle (it does not re-patch). I could not determine the order. KID and HoM are separate DLLs on the same SKSE message. Fix if needed: apply the adapters (or re-run) on the KID event, or on `kPostLoad`
  ordering; to be tested with the log (`NPCSpellVariance.log` "recieved KID finished event" versus HoM `PerkAdapters:` line).
- **Shared effects.** Adapter writes MGEF keywords; the leak check blocks a write when an enchantment/scroll/potion shares the effect. Most NSV spell packs reuse MGEFs between player and NPC twins (fine,
  both are catalog spells).
- **Coverage gain.** Plugin's own auto-scan adds nothing without keywords (section 1), so an unlisted modded spell is invisible to NSV. The five packs cover 1250 spell-lines. The catalog has 3776 spells; the keywords
  are the whole value. HoM already writes `MagicRune/RestoreHealth/Ward/Invisibility/Cloak` etc, which NSV reads in three places, but its main flow reads NSV_* only.
- **What the adapter is worth.** Clean keywords (Summon, Weapon, Trap, Ward, Cloak, Invisibility, Fear, Armour) can replace patches almost fully. Damage (284 uses, the biggest) and Heal/Absorb/Calm need judgment and an offline measure. Tag_Undead/Ally/Enemy/Generic_*/Teleport cannot be derived cleanly; their per-pack lines would still need to stay (or an author-intent mapping is accepted).

## Could not verify
- whether `MagicItem::HasKeyword` includes effect keywords in this game version (the doc says yes by in-game test, header does not prove it)
- load/event order between HoM's kDataLoaded `PerkAdapters::Apply` and KID's done event
- hook.cpp HEAD equals installed 2.7.2
- the draft adapter was not run through `librarian-test` and effect field names (archetype spellings) were only checked against `vanilla.json`
- true recall/precision: the join treats unpatched spells as negatives; the packs are hand-written and incomplete, so fp is overstated
- how vanilla Skyrim.esm spells get NSV keywords outside the Mysticism pack (no SPEL in `NPC Spell Variance.esp`)
- KID 1.x `Spell` type targets the SPEL record (inferred from the `|Spell|` form, not run)
