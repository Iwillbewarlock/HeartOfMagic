# Black Flame lantern (흑염 등불) - design

Status: draft v3 (Elden Ring black flame rules, user 2026-10-21) after review round 1 (reviewer agent + Codex gpt-6-astra, 2026-10-21; findings checked against
the code, all taken), 2026-10-21. Branch `feature/black-flame-lantern` (from `feature/black-flame-single` 377c32b5).

## 1. What and why

A wearable belt lantern: the empty iron cage of an Imperial soldier's lantern with a black flame floating in it. It is
not a light source. While it is worn:

1. the player's fire spells take the existing Black Flame look (the whole addon: grey copies, burn shader, hands,
   runes, corpse fire). No visual work is new: the lantern only switches the existing pipeline on;
2. fire spells cost 30% more magicka;
3. a player's fire spell hit sets the target on black flame, as Elden Ring's black flame does (the user's model,
   2026-10-21): damage over 2 s equal to 2% of the target's maximum health + 20, ignoring resistance. It does not
   stack: a further hit while it burns only restarts the 2 s. So a single hit gives 2% + 20, and any stream of hits
   (Flames held, Firebolt spam, Wall of Flames) burns at most 1% + 10 per second. Every casting type is treated the
   same; the cast-type split and its ICDs (draft v2) are gone. Cloak hits, scrolls, staffs, shouts and powers get
   nothing.

   Elden Ring facts (checked 2026-10-21 on the Fandom wiki through its API, pages `Black_Flame` and `Blackflame`):
   "20 + 2.0% of the target's maximum HP over 2 seconds"; "The effect does not stack, though further hits refresh its
   2-second timer"; Shadow of the Erdtree bosses take 10 + 1.0% (Messmer, Bayle, Radahn 7 + 0.7%). Gemini also cited
   SpEffectParam values (0.1% + 1 HP every 0.1 s for 2 s) and "ignores fire negation" from Reddit and soulsmodding
   pages behind redirect links; not opened, not relied on. Skyrim has no boss tier, so no boss reduction here.

User decisions (2026-10-21): built into this addon (same ESL, same DLL); the lantern is the only player-facing trigger;
both combat effects in; the extra damage is a hidden spell the player casts (kill credit kept), not a direct actor
value change and not a Papyrus cloak.

The pasted spec's "black/purple" recolour and its EffectShader guide are superseded by the existing look (bright core
with dark parts, user design rule 2026-10-20).

## 2. Facts this rests on (checked 2026-10-21)

| Fact | Evidence |
|---|---|
| The flame is on while the player has an active effect whose MGEF has `HoMBF_StokedKeyword` (0x801); duration 0 passes | `src/BuffState.cpp:38-67` |
| Unequip is caught within 1 s even if no effect event fires (the idle tick recomputes) | `src/BuffState.cpp:79,100` |
| `HoMBF_StokeEffect`/`HoMBF_StokeSpell` are only read by self tests and the "not a fire spell" comparison; a missing form logs an error and nothing else | explorer report; `src/Forms.cpp:16,36`, `src/FireSet.cpp:159,284` |
| A worn item's MGEF can carry a perk (`Perk to Apply`) | vanilla `dunHunterQSTAmuletEffect` 107E2F (archetype Script, perk 107E2C) |
| Perk entry point Mod Spell Cost exists | CommonLib `BGSEntryPoint.h:53` `kModSpellCost = 38` |
| SPEL flag Ignore Resistance exists and is what `IgnoresResistance()` returns | CommonLib `SpellItem.h:31` (`1 << 20`), `:74` |
| `CastSpellImmediate(spell, noHitEffectArt, target, effectiveness, hostileEffectivenessOnly, magnitudeOverride, blameActor)` | CommonLib `MagicCaster.h:62` |
| `TESHitEvent` carries `source` and `projectile` as FormIDs | CommonLib `TESHitEvent.h:9-35` |
| Casting types: constant 0, fire and forget 1, concentration 2, scroll 3 | CommonLib `MagicSystem.h:36-42` |
| Max health = permanent + temporary modifiers | CommonLib `Actor::GetActorValueMax`, `src/RE/A/Actor.cpp:358-362` |
| Cloak secondaries are found as the `associatedForm` of `kCloak` (35) MGEFs; FireSet already does it | `src/FireSet.cpp:272-304` |
| `Ansilvund02` (01F358) is "안질푼트 매장묘실"; it holds the Alchemy Lab ref 0C0090 (4628.6, 100.2, 8192, rot z 3.483) and *Imperial Condolences* ref 09020E (4456.2, 199.6, 8251.1) | houseCARL |
| `Ansilvund02` is overridden by 13 plugins in TAKEALOOK (winner Lux.esp) | houseCARL `override_depth=13` |
| `meshes/clutter/common/glazedcandlelantern01.nif` does not exist; `candlelantern01.nif` is one BSTriShape (frame only, `LampGeneric01.dds`), no glass, no candle | houseCARL asset_status, nif_inspect |
| Grey `mps/mpsfiresmall01.nif` and `mps/mpsfiresmsmoke01.nif` already ship in `data/meshes/heartofmagic/blackflame/` | file list |
| Slot use in TAKEALOOK (ARMA count): 60: 115, 58: 176, 56: 212, 55: 214, 48: 218, 49: 368, 57: 405, 52: 434 | houseCARL |
| Lore: husband Saeel died 4E 175 retaking the Imperial City (letter `dunAnsilvundLetter01`); "dark fire" in *Mannimarco, King of Worms* (`SkillAlchemy2`) is one verse about his eyes, not an art | houseCARL book text |

Not verified (checked by the log of the first run, section 7): whether an equip/unequip raises
`TESActiveEffectApplyRemoveEvent`; how often a concentration spell raises `TESHitEvent`; whether `kIgnoreResistance`
also skips ResistMagic (expected: yes, `CheckResistance` is not consulted for such spells - Codex asked);
whether perk magnitude perks (Augmented Flames) scale a `magnitudeOverride`.

## 3. Records (ESL `HeartOfMagic_BlackFlame.esp`, new ids from 0x80D)

| Id | Type | EditorID | Settings |
|---|---|---|---|
| 0x80D | ARMO | `HoMBF_Lantern` | name from section 6; slot 60; Clothing, weight 2, value 1200; keywords `ArmorClothing` 06BBE8, `MagicDisallowEnchanting` 0C27BD; enchantment 0x80F; world model `heartofmagic/blackflame/lantern/lantern_gnd.nif`; armature 0x80E; no ARMO template, not playable-hidden |
| 0x80E | ARMA | `HoMBF_LanternAA` | slot 60; race `DefaultRace` 000019 + additional races = the playable races and their vampire races (as vanilla jewellery ARMAs list them); weight slider off; male and female 3rd person model `.../lantern_worn.nif` |
| 0x80F | ENCH | `HoMBF_LanternEnch` | Enchantment, Constant Effect, delivery Self, no charge; effect 0x810 magnitude 0 duration 0 area 0 |
| 0x810 | MGEF | `HoMBF_LanternEffect` | archetype Script (as the vanilla amulet effect), Constant Effect, Self; keywords `HoMBF_StokedKeyword` 0x801; Perk to Apply 0x811; not hostile, not detrimental; magic skill None; description = the item text |
| 0x811 | PERK | `HoMBF_LanternPerk` | hidden, not playable; one entry point Mod Spell Cost, Multiply Value 1.3; spell condition `EPMagic_SpellHasKeyword MagicDamageFire` (01CEAD) |
| 0x812 | SPEL | `HoMBF_LanternBurnSpell` | Spell, Fire and Forget, Target Actor; flags Manual Cost Calc (cost 0), Ignore Resistance, No Absorb/Reflect, No Dual Cast Modification; effect 0x813 magnitude 1 (overridden per cast with the per-second rate), **duration 2** (v3: the black flame lasts 2 s), area 0 |
| 0x813 | MGEF | `HoMBF_LanternBurnEffect` | Value Modifier, Health, a damage over time (v3: No Duration off); Hostile + Detrimental, Recover off; No Area, FX Persist, Painless, Hide In UI, No Hit Effect off (the burn shader must show; the DLL passes noHitEffectArt=false); **No Hit Event off** (the hit event is what registers the attack; the sink skips our own spell by FormID instead - Codex 2026-10-21); Power Affects Magnitude off; magic skill None, resist value None; no keywords (so no Augmented Flames, no "fire" in FireSet); hit shader `HoMBF_BurnShader` 0x800 with FX Persist, so the target burns grey for the 2 s (a load order with Glow Be Gone nulls it at kDataLoaded; respected, user 2026-10-20) |
| 0x814 | BOOK | `HoMBF_LuahJournal` | note model (vanilla `clutter/books/...` note, chosen at build), text from section 6, no skill, value 5 |
| 0x815 | MSTT | `HoMBF_LanternSmoke` | model `heartofmagic/blackflame/lantern/lantern_smoke.nif`: an empty `BSFadeNode` with one child `NiNode` named `AddOnNode9123` (the node index of the existing grey ADDN 0x809 `HoMBF_GreyAddon_MPSFireSmSmoke01`) - the MPS copies are `BSMasterParticleSystem` roots and only emit through an addon node, as the vanilla `candlelanternwithcandle01.nif` does with `AddOnNode49` (review R3) |
| 0x816 | GLOB | `HoMBF_LanternPlaced` | short, 0, **Constant off** (a constant global is not saved); 1 once every placed object is ready |

The stoke SPEL/MGEF (0x802/0x803) stay in the ESP with the keyword, because the self tests cast them; nothing in the
game hands them out (console only), so the lantern is the only player-facing trigger. README/docs say so.

Conflict scope: no vanilla record is overridden.

Fire-spell scope (review R10): the extra damage uses the addon's own `IsFireSpell` (keyword, ResistFire or catalog tag,
spell type Spell) so it matches what turns grey; the +30% perk can only see keywords (`MagicDamageFire`), so a spell
that is fire only by catalog tag turns grey and gets the damage but not the cost. Scrolls are out of both damage and
look (they do not turn grey, `FireSet.cpp:158`). README states both limits. A follower wearing the lantern: the perk
entry carries a `GetIsID Player` owner condition, so followers get nothing; the flame and the damage are player-only.

## 4. Placement in Ansilvund02 (DLL, no cell edit)

A CELL override from this ESL would carry its own copy of the cell header and fight the 13 plugins that already
override `Ansilvund02` (lighting template, music, water, image space). So the DLL places the objects at run time.

1. Trigger: `TESCellFullyLoadedEvent` for cell 01F358, and the player's cell after a game load. The sink only queues
   a task (`QueueTask`, which carries `WorldGeneration()`; a task from an older world does nothing).
2. Guard (game thread): the feature is off for the session when `MainQueue::Installed()` is false (the SKSE fallback
   runs tasks on worker threads, `MainThreadQueue.cpp:325-338`) - logged once; `IsOnGameThread()` is checked before
   every engine write below. `DisableEngineWrites` (`Config.cpp:389`) also turns `lanternPlace` and `lanternDamage` off.
3. `HoMBF_LanternPlaced == 0`; lab ref 0C0090 found, 3D loaded, not disabled, not deleted, parent cell 01F358 - else
   one warning naming which check failed, nothing placed, the global stays 0 (retry on the next load of the cell).
4. Create the lantern, journal and smoke with `TESDataHandler::CreateReferenceAtLocation` (forcePersist) at the final
   position: lab position + config offset rotated by the lab's z angle; angle = lab angle + config yaw (review R2:
   placing at the lab origin and moving later lets Havok throw the object out of the lab mesh).
5. Wait for 3D (on the tick, like `TickHitArt`), then `SetMotionType(kKeyframed)` on the lantern and journal; when
   both succeed, set the global to 1. A ref whose 3D does not come within 10 s: log it, delete what was made, retry on
   the next cell load.
6. Every later load of the cell: find the placed lantern/journal by base object in the cell and set keyframed again
   (the motion type is not known to survive a save; review R1).
7. Pickup: `TESContainerChangedEvent` (old container 0, new container the player, base `HoMBF_Lantern`) queues a task
   that disables every `HoMBF_LanternSmoke` ref in the player's cell. A console `AddItem` also passes; harmless.

Log lines (exact prefixes): `lantern place: ok lantern=<id> journal=<id> smoke=<id> at (x,y,z) lab=(x,y,z)`,
`lantern place: skipped (<reason>)`, `lantern place: keyframed <id> ok|failed`, `lantern smoke: disabled <n>`.

Limit: an existing save that already visited Ansilvund02 gets the lantern on the next visit (the cell must reload).

## 5. DLL: the black flame (new `LanternDamage.cpp/.h`, pure rules in `LanternDamageRules.h`)

v3 (2026-10-21, the user's Elden Ring model, section 1): one rule for every casting type.

The always-registered `HitEventSink` (`HitEvents.cpp`) calls `LanternDamage::OnHit`. In the sink (any thread) only
FormID work and a mutex-guarded re-apply check happen; everything that reads the world runs in the queued task.

1. Sink: `cause` is the player and `source` is not our 0x812; the source FormID is classified once per id and cached:
   a `SpellItem` that `IsFireSpell` accepts or a fire hazard's spell (`BGSHazard::data.spell`), minus cloak
   secondaries (`IsCloakSpell`). Casting type does not matter any more.
2. Rule (pure, `LanternDamageRules.h`): per-second rate = (max health x `lanternBurnPct` + `lanternBurnFlat`) /
   burn seconds, where burn seconds is read from the 0x812 effect's duration at kDataLoaded (2 in the ESP; not a
   config key, so the record and the maths cannot disagree). Re-apply throttle per target: `lanternReapply` (0.1 s)
   - only to keep a 60-events-per-second Flames from casting 60 times a second; it changes nothing in the damage,
   because a re-cast only restarts the 2 s.
3. Task (game thread, same world generation): target is an `Actor`, alive, not the player; the player has an active
   `HoMBF_LanternEffect` (0x810). `CastSpellImmediate(burnSpell, false, target, 1.0f, false, rate, player)`.
   Not stacking relies on the engine replacing the same spell from the same caster on a target (vanilla behaviour,
   not proven here): the log line below counts our effects on the target after the cast.
4. Log, not rate-limited while `lanternLog` is true (default true for the first run): `lantern hit: src=<id> proj=<id>
   tgt=<id> t=<ms>` for every accepted event and `lantern hit: rejected src=<id> type=<t> (<why>)` once per source
   id; `lantern burn: tgt=<id> max=<m> rate=<r>/s for <s> s effects=<n> magnitude=<x> difficulty=<d>`, where `n` is
   the number of active 0x813 effects on the target after the cast (expected 1) and `magnitude` is that effect's
   magnitude as the engine stored it (expected = rate: a resistance or a magnitude perk would show as a smaller or
   larger number - this replaces the old before/after health read, which a damage over time cannot use); a
   `TESDeathEvent` whose victim burned in the last 2 s logs `lantern kill: tgt=<id> killer=<id>` (the burn is noted
   before the cast, so a kill inside the cast is caught; review 1 R5).

Config keys (`HeartOfMagic_BlackFlame.json`, documented in the README config table): `lanternPlace` (true),
`lanternDamage` (true), `lanternLog` (true), `lanternBurnPct` (0.02), `lanternBurnFlat` (20), `lanternReapply`
(0.1), `lanternOffset` [x,y,z,yaw], `journalOffset` [x,y,z,yaw], `smokeOffset` [x,y,z]. `lanternSinglePct`,
`lanternSingleIcd`, `lanternConcPct`, `lanternConcIcd` (v2) are removed; an old JSON that still has them logs
"unknown key" once and goes on. The +30% cost lives in the perk.

Cost line: on every buff flag change `RecomputeBuff` logs `lantern cost: <spell id> <cost>` for the player's known
fire spells via `MagicItem::CalculateMagickaCost`, off and on, so the log holds both values (review R7). The buff
reason gets `lantern` when the 0x810 effect is the one found (review R3).

## 6. Meshes and text

- `lantern_gnd.nif`: `candlelantern01.nif` frame (one BSTriShape, collision kept); diffuse swapped to a darkened copy
  of `LampGeneric01.dds` made by a script (normal map kept); a child `NiNode` `AddOnNode9100` (grey ADDN 0x804
  `MPSFireSmall01`) at the cage centre.
- `lantern_worn.nif`: the frame skinned 100% to `NPC Pelvis [Pelv]` with a `BSDismemberSkinInstance` partition 60 at
  the left hip, built by a new `blackflame-greybuild` step with nifly. The flame (review R9, checked 2026-10-21): no
  worn armor in the load order carries particles or an addon node (the 8 worn lanterns found - Armors of the Velothi,
  evgnn SMP backpacks - are skinned shapes with a glow map), and the vanilla Flame Atronach body
  (`Actors/AtronachFlame/Character Assets/AtronachFlame.nif`) puts its fire on the body as skinned
  `BSEffectShaderProperty` strips (`FireStrip08`, `FireStripWarp`) with `BSEffectShaderPropertyFloatController` UV
  scroll. So the worn flame is two such strips copied from that NIF, skinned to the pelvis inside the cage, with the
  addon's grey palette (`GradBlackFlame.dds`) - no particles, no addon node, no DLL art object.
- Buff name (user 2026-10-21): MGEF 0x802, SPEL 0x803 and MGEF 0x810 are named "갈망" in the ESP (English "Longing");
  "흑염 달구기" is retired.
- Text: `plugins/BlackFlame/docs/lantern-text.md` (Gemini Argon draft from the fact sheet, checked line by line).
  Names in Korean follow the installed translation: 루'아 알-스케이븐, 사엘, 피우스 브룩시우스, 《매니마코, 벌레들의 왕》.

## 7. Checks

Offline: `LanternDamageRules.h` tests in `blackflame-cosave-check` (rate maths for 2% + 20 over 2 s, edge max health 0
and huge, re-apply throttle edges at exactly 0.1 s, cloak / own-spell / non-fire exclusion, map pruning past the
longest throttle); houseCARL read-back of every new record;
`nif_inspect` of the three NIFs (shape list, skin partition 60, addon node names, texture paths). Reviews: reviewer
agent + Codex, twice.

One bundled in-game run by the user, judged from `HeartOfMagic_BlackFlame.log`:

| What | Pass |
|---|---|
| Enter Ansilvund02 | `lantern place: ok`, then `keyframed ... ok` twice; lab and lantern coordinates as configured |
| Pick it up | `lantern smoke: disabled 1` |
| Equip / unequip | `buff flag ON (... lantern)` / `buff flag off`; `lantern cost` lines: worn = 1.3 x off for keyword fire spells |
| Firebolt on a target, then nothing for 3 s | one `lantern burn`, rate = (2% of max + 20) / 2 per second, `effects=1`, `magnitude` = rate |
| Firebolt twice within 1 s on one target | two `lantern burn` lines, `effects=1` after the second (no stacking) |
| Flames held 5 s on one target | `lantern burn` lines about 0.1 s apart, `effects=1` on every line |
| Fireball (direct and splash), Fire Rune, Wall of Flames | `lantern hit` lines name their source; Wall of Flames: none at all means its hazard hits do not carry the player as cause |
| Flame Cloak | only `rejected ... (cloak)` |
| Resistance and magnitude perks | `magnitude` = rate on a fire-resistant target and with Augmented Flames |
| A kill | `lantern kill: killer=00000014` |

Left to the eye (no log can judge them): the lantern sitting on the lab (the log proves the coordinates, not that it
looks right), the worn lantern's place on the hip, the flame inside the cage.
