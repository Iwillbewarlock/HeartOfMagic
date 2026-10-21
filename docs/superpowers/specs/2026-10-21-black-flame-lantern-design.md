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
| 0x814 | - | (removed) | was BOOK `HoMBF_LuahJournal`; removed 2026-10-21 by the user ("가끔은 유저의 상상이 더 좋은 이야기야"): nothing placed beside the lantern, the DLL no longer looks it up |
| 0x815 | MSTT | `HoMBF_LanternSmoke` | model `heartofmagic/blackflame/lantern/lantern_smoke.nif`: an empty `BSFadeNode` with one child `NiNode` named `AddOnNode9123` (the node index of the existing grey ADDN 0x809 `HoMBF_GreyAddon_MPSFireSmSmoke01`) - the MPS copies are `BSMasterParticleSystem` roots and only emit through an addon node, as the vanilla `candlelanternwithcandle01.nif` does with `AddOnNode49` (review R3) |
| 0x816 | GLOB | `HoMBF_LanternPlaced` | short, 0, **Constant off** (a constant global is not saved); 1 as soon as the lantern, journal and smoke refs are created (section 4; the keyframe is best effort and does not gate it) |

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
5. Done at creation (coordinator decision 2026-10-21, review 2): as soon as the three `CreateReferenceAtLocation`
   calls have returned, in the same task, `HoMBF_LanternPlaced` becomes 1 and the `ok` line is written. Nothing that
   happens later (3D, Havok, a save) can undo it, so no later load places a second lantern and no leftover ever has
   to be removed or deleted (the old leftover removal and the 10 s delete-on-timeout are gone, and with them review 2
   B1, R2, R3). A creation that fails part-way (one or two of the three refs null): the `partial` warning names which,
   what was made is kept, and the global is still set to 1 - a retry could double the refs that were made, and a
   second lantern is the worse outcome (a missing journal or smoke is only cosmetic; a missing lantern means that save
   gets none). Only when none of the three was made does the global stay 0 (nothing exists that a retry could double).
6. Keyframe, best effort (a failure only logs): the lantern and the journal are set keyframed through
   `TESObjectREFR::SetMotionType(kKeyframed)` (CommonLib: it calls the node's `SetMotionType(type, true, false, true)`
   and adds the `kHavokMoved` change flag, so the state is saved with the ref; review 2 I1), once the ref's 3D is there
   (on the tick, 10 s window from the last ref queued). A false result is tried again on the next ticks inside the window
   (the Havok body can come a tick after the 3D root) and its last value is logged as is. Which refs: right after
   creation, the handles just made (not a scan of the cell: whether a new ref is in the cell's reference list when
   `CreateReferenceAtLocation` returns is not known); at every later load of the cell (global 1), each lantern or journal
   in the cell with our base object, not disabled, parent cell 01F358 and within 64 units of its configured place next to
   lab 0C0090 (review 1 code R4: only the refs this addon placed, not one the player dropped elsewhere). Each candidate
   gets one `candidate` line (first-run diagnostics). A handle is queued once even when the post-load check and the
   cell event both fire for the same load (review 2 R4).
7. Pickup: `TESContainerChangedEvent` (old container 0, new container the player, base `HoMBF_Lantern`) queues a task
   that disables every `HoMBF_LanternSmoke` ref in the player's cell. It also sets `HoMBF_LanternPlaced` to 1: for a
   placed lantern the global is already 1; the line only matters for a lantern from a console `AddItem` before any
   visit, which then never gets the placed one - it already has one.

Log lines (exact prefixes, every `lantern place` line `LanternPlace.cpp` writes):
`lantern place on (cell <id>, lab ref <id> of <plugin>)` and `lantern place off (<reason>)` (kDataLoaded),
`lantern place: ok lantern=<id> journal=<id> smoke=<id> at (x,y,z) lab=(x,y,z)` (all three made, global set to 1),
`lantern place: partial lantern=<id> journal=<id> smoke=<id> at (x,y,z) lab=(x,y,z) (could not create the <names>; ...)`
(warning, a null ref shows as 00000000, global set to 1),
`lantern place: skipped (<reason>)` (a lab check failed, or none of the three could be created; global stays 0),
`lantern place: candidate <id> base=<id> disabled=<b> cell=<id> has3D=<b> dist=<d>` (each ref looked at for a keyframe,
`dist` from its configured place),
`lantern place: keyframed <id> ok|failed` (the `SetMotionType` result as is),
`lantern place: keyframed <id> failed (no 3D within 10 s)`,
`lantern place: keyframe again skipped (lab ref 0C0090 not found|not in cell 01F358)` (a later load),
`lantern place: nothing of ours to keyframe again (...)` (a later load found none within 64 units),
`lantern place: a task ran off the main thread - nothing placed or changed` (once), `lantern smoke: disabled <n>`.

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
   tgt=<id> t=<ms>` for every accepted event that burns - written by the task after its checks, just before the cast
   (review 1 item 7b: a hit with only the console stoke spell, a dead target or a non-actor passes the sink but must not
   count as a lantern hit; `t` is still the sink's time), and `lantern hit: rejected src=<id> type=<t> (<why>)` once per
   source id, written whatever `lanternLog` says (review 2 I4: one line per source id for the session, cheap, and the
   only record of why a hit did not burn); `lantern burn: tgt=<id> max=<m> rate=<r>/s for <s> s effects=<n> magnitude=<x> difficulty=<d>`, where `n` is
   the number of active 0x813 effects on the target after the cast (expected 1) and `magnitude` is that effect's
   magnitude as the engine stored it (expected = rate: a resistance or a magnitude perk would show as a smaller or
   larger number - this replaces the old before/after health read, which a damage over time cannot use); a
   `TESDeathEvent` whose victim burned in the last burn seconds + 1 s (3 s: the 2 s burn, plus a second for a kill at
   its end to reach the death sink) logs `lantern kill: tgt=<id> killer=<id>` (the burn is noted
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
  of `LampGeneric01.dds` made by a script (normal map kept); the same two flame layers as the worn model (below) in the
  cage, BSXFlags Animated for their controllers. No addon node (round 6: the grey ADDN 0x804 `MPSFireSmall01` copy is
  all additive, without the dark layer).
- `lantern_worn.nif`: the frame skinned 100% to `NPC Pelvis [Pelv]` with a `BSDismemberSkinInstance` partition 60 at
  the left hip, built by a new `blackflame-greybuild` step with nifly. The flame (review R9, checked 2026-10-21): no
  worn armor in the load order carries particles or an addon node (the 8 worn lanterns found - Armors of the Velothi,
  evgnn SMP backpacks - are skinned shapes with a glow map), and the vanilla Flame Atronach body
  (`Actors/AtronachFlame/Character Assets/AtronachFlame.nif`) puts its fire on the body as skinned
  `BSEffectShaderProperty` strips (`FireStrip08`, `FireStripWarp`) with `BSEffectShaderPropertyFloatController` UV
  scroll. Round 7 (2026-10-21; round 6's cut strips, about 20 vertices each, did not show in game): the flame is two
  closed spindles (lathe shapes, 12 segments, 6 rings, open at the bottom) made by the builder on the cage axis, the same
  in the ground model, the worn model and the inventory, skinned to the pelvis in the worn one - `HoMBF_FlameOuter`
  0.40 x the frame's height tall and 0.55 x the smaller cage wall size wide, its base a third of the frame's height up,
  and `HoMBF_FlameCore` 0.55 x as tall and 0.35 x as wide (round 9), 0.05 x the outer height above the outer base; vertex alpha
  0 at the bottom ring, 0.7 at the 0.15 ring, 1 at the widest, 0 at the tip. Each takes a copy of `FireStrip08`'s effect
  shader and its UV scroll controller (the atronach NIF is read for nothing else; `FireStripWarp` is dropped) with
  Greyscale_To_PaletteAlpha and the Decal bits cleared and Greyscale_To_PaletteColor, Vertex_Alpha, Use_Falloff,
  Double_Sided and Vertex_Colors on: with palette alpha on, the opacity is the palette's alpha alone and our palettes' 64
  rows are alike, so vertex and base alpha did nothing; off, it is texture alpha x vertex alpha x base alpha x falloff.
  Round 8 (2026-10-21, user: "a flame trapped inside a spindle-shaped mesh", the hard lathe outline read as glass): the
  bottom ring's vertex alpha 0.5 -> 0 and the 0.15 ring's 0.75 -> 0.7 (the open rim showed as a line); falloff on, start
  angle cos 1.0 opacity 1.0 to stop angle cos 0.0 opacity 0.0 on the outer layer and cos 1.0 / 1.0 to cos 0.2 / 0.0 on the
  core, so the outline fades edge-on; U texture scale 2 on both (the scrolling tile twice round, more and narrower
  tongues; the template's clamp mode 0x0000FF03 wraps, so the U 0..1 seam still meets). Round 9 (2026-10-22, matched in
  a Blender preview of Community Shaders' effect maths: colour = palette.rgb(texture green) x base colour scale, alpha =
  texture alpha x vertex alpha x falloff): base colour scale 0.6 on the outer layer and 1.6 on the core (the template's
  2.0 on both read as a white marbled blob; now a dark smoky envelope round a brighter core); UV scale 2 x 2.5 on both
  (V 2.5 for finer tongues, the V offset scroll unchanged); falloff cos 0.6 / 1.0 to cos 0.0 / 0.0 on the outer layer
  (more body before the edge fades) and cos 0.8 / 1.0 to cos 0.2 / 0.0 on the core; the core 0.55 x 0.35 (was 0.65 x
  0.45); a jagged top, every odd segment's vertex alpha x 0.3 on the 0.60 and 0.85 rings and the tip (the seam column
  copies segment 0, both even, so the seam stays continuous; the read-back checks the 0.60 ring's alternate segments at
  0.3 of the others). The outer layer blends normally (0x10ED, base alpha 1.0) on `GradBlackFlame.dds`, the core
  additively (0x100D, base alpha 1) on `GradBlackFlameAdd.dds` - no particles, no addon node, no DLL art object.
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
| Enter Ansilvund02 | `lantern place: ok`, then a `candidate` line and `keyframed ... ok` for the lantern and the journal; lab and lantern coordinates as configured |
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

## 8. Worn lantern on Immersive Equipment Displays (2026-10-22, user decision of 2026-10-21)

IED (1.7.4, SlavicPotato, optional) shows the worn lantern at the hip through its Papyrus API, with no setup by the
player; without IED the skinned `lantern_worn.nif` above stays. `LanternIed.cpp/.h`, the transform rules in
`LanternIedRules.h` (tested in `blackflame-cosave-check`), README "Lantern on IED".

- Detection at kDataLoaded: `ImmersiveEquipmentDisplays.dll` loaded, the VM knows the script type `IED`
  (`TypeIsValid`, else `GetScriptObjectType`) and `IED.GetVersion()` can be sent (its result logged).
- With IED and `lanternIed`: ARMA 0x80E's biped models (first-person ones when set) swapped to `lantern_empty.nif` (a root
  BSFadeNode only, written by `--write-lantern`) before any save loads, with a PASS/FAIL line that both are (again at the first game load). Without IED or
  with `lanternIed` false: no swap; with IED and `lanternIed` false the player's entry is deleted at each load
  (`DeleteItemActor`, which has no sex argument).
- At each successful load and new game, one entry `Lantern` under the plugin's key: `CreateItemActor` once (male; IED
  refuses a second create of the name before it sets the other sex's form, inventory flag and node, so the setters do
  both sexes), both sexes disabled, then for each sex form ARMO 0x80D, inventory form, node NPC Pelvis [Pelv], parent
  attachment (mode 1, no reference sync: Lights On's flags 0x900010), world model (no effect for a non-shield ARMO, kept
  as harmless), Havok off, position, rotation (degrees), scale, then both enabled from the worn state at that moment, then
  the check: `ItemExistsActor` and `ItemEnabledActor` per sex, a PASS/FAIL line each (`sent=`: IED's config against what
  was sent, not proof of the attached model). The calls are chained (each
  `DispatchStaticCall` sent on the main thread from the task its predecessor's result queued; dispatch acceptance is not
  the native's success, so every result is logged); a call without a result for 5 s is given up on from the ticker, and
  each call carries a sequence number so a late result is ignored.
- `TESEquipEvent` of the player and ARMO 0x80D: both sexes enabled or disabled and the check (after the setup's enable
  step).
- Fallback, on received results only (a timeout is unknown, never a false): `CreateItemActor` and `SetItemFormActor male`
  both returned false, the setup's `ItemExistsActor` returned false, or `GetVersion` without an int: the ARMA's original
  paths back, `setup failed`, no entry set up for the session and the entry deleted at once and at each load. The setup's
  `ItemExistsActor` without a result: a warning, IED kept.
- Config `lanternIed` (true), `lanternIedPreset` (`frontLeft` | `frontRight` | `backLeft` | `backRight`), optional
  `lanternIedTransform` `[px, py, pz, rx, ry, rz, scale]` (+-1000, +-360 degrees, 0.05..5), read once at start (a change
  needs a restart). The presets are the IED profiles of GiraPomba's Lights On - Wearable Lanterns for IED (Nexus SSE
  85575, asset use allowed with credit): frontLeft pos (-18, 13, -8.5) rot (15, 31, 23.5); frontRight (13.5, 13.5, -8.5)
  (-14.5, -29.5, 10); backLeft (-14.5, -10, -12) (-7.5, 136.5, -8.5); backRight (13, -14, -12) (4.5, 215, -5); scale 0.6.
- Limits: a follower wearing the lantern shows nothing while IED is on (empty ARMA, only the player registered); the entry
  is an inventory form (to IED it shows while carried) and only this DLL ties it to the worn state, so removing only the
  DLL keeps its last state (unequip, save, then remove; removing the ESP makes IED purge it); without the main-thread
  queue no call is made, so a save with an enabled entry can show both copies until a normal session; a short off/on of
  the worn lantern at each load while the setup runs.
- In game, left to the eye: the place on both bodies, the world model's flame with Havok off; judged from the log: the
  ARMA check, every setter `-> true`, the `lantern ied check PASS` lines after the setup and each equip toggle, the delete
  with `lanternIed` false.
