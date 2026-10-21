# Black Flame lantern (흑염 등불) - design

Status: draft v2 after review round 1 (reviewer agent + Codex gpt-6-astra, 2026-10-21; findings checked against
the code, all taken), 2026-10-21. Branch `feature/black-flame-lantern` (from `feature/black-flame-single` 377c32b5).

## 1. What and why

A wearable belt lantern: the empty iron cage of an Imperial soldier's lantern with a black flame floating in it. It is
not a light source. While it is worn:

1. the player's fire spells take the existing Black Flame look (the whole addon: grey copies, burn shader, hands,
   runes, corpse fire). No visual work is new: the lantern only switches the existing pipeline on;
2. fire spells cost 30% more magicka;
3. a player's fire spell hit adds damage that ignores resistance: 2% of the target's maximum health per hit for
   fire-and-forget spells (0.5 s per target), 0.4% per hit for concentration spells and their hazards (0.25 s per
   target, so at most 1.6% per second). Cloak hits, scrolls, staffs, shouts and powers get nothing.

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
| 0x812 | SPEL | `HoMBF_LanternBurnSpell` | Spell, Fire and Forget, Target Actor; flags Manual Cost Calc (cost 0), Ignore Resistance, No Absorb/Reflect, No Dual Cast Modification; effect 0x813 magnitude 1 (overridden), duration 0, area 0 |
| 0x813 | MGEF | `HoMBF_LanternBurnEffect` | Value Modifier, Health; Hostile + Detrimental, Recover off; No Duration, No Area, No Hit Effect, Painless, Hide In UI; **No Hit Event off** (the hit event is what registers the attack; the sink skips our own spell by FormID instead - Codex 2026-10-21); Power Affects Magnitude off; magic skill None, resist value None; no keywords (so no Augmented Flames, no "fire" in FireSet), no shaders, no art |
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

## 5. DLL: the extra damage (new `LanternDamage.cpp/.h`, pure rules in `LanternDamageRules.h`)

The always-registered `HitEventSink` (`HitEvents.cpp`) calls `LanternDamage::OnHit`. In the sink (any thread) only
FormID work and a mutex-guarded ICD check happen; everything that reads the world runs in the queued task.

1. Sink: `cause` is the player and `source` is not our 0x812; the source FormID is classified once per id and cached:
   a `SpellItem` that `IsFireSpell` accepts, minus cloak secondaries (the set `AddCloakSpells` already walks,
   `FireSet.cpp:274-304`, kept and exposed instead of a second walk), plus hazard spells (`BGSHazard::data.spell` of
   fire hazards, put in the concentration bucket so a Wall of Flames hazard cannot reach 4%/s; review R5).
2. Rule (pure, `LanternDamageRules.h`): casting type -> bucket (fire and forget -> single, 2%, 0.5 s; concentration or
   hazard -> 0.4%, 0.25 s; anything else -> none). ICD keyed by (target FormID, bucket) on `steady_clock`, entries
   older than 5 s dropped on each event.
3. Task (game thread, same world generation): target is an `Actor`, alive, not the player; `IsStoked()` and the player
   has an active `HoMBF_LanternEffect` (0x810), so the lantern is the reason, not the console stoke spell. Read health,
   `CastSpellImmediate(burnSpell, true, target, 1.0f, false, GetActorValueMax(kHealth) * pct, player)`, read health
   again in the same task.
4. Log, not rate-limited while `lanternLog` is true (default true for the first run): `lantern hit: src=<id> proj=<id>
   tgt=<id> bucket=<b> t=<ms>` for every accepted event and `lantern hit: rejected src=<id> type=<t> (<why>)` once
   per source id; `lantern damage: tgt=<id> max=<m> intended=<x> before=<a> after=<b> difficulty=<d>`; a
   `TESDeathEvent` whose victim took lantern damage in the last 2 s logs `lantern kill: tgt=<id> killer=<id>`.
   `after` read in the same task isolates the hidden spell only if the engine applies it inside the call; if
   `after == before` on every line, the damage lands later and the separate check in section 7 is used instead.

Config keys (`HeartOfMagic_BlackFlame.json`, documented in the README config table): `lanternPlace` (true),
`lanternDamage` (true), `lanternLog` (true), `lanternSinglePct` (0.02), `lanternSingleIcd` (0.5), `lanternConcPct`
(0.004), `lanternConcIcd` (0.25), `lanternOffset` [x,y,z,yaw], `journalOffset` [x,y,z,yaw], `smokeOffset` [x,y,z].
The +30% cost lives in the perk.

Cost line: on every buff flag change `RecomputeBuff` logs `lantern cost: <spell id> <cost>` for the player's known
fire spells via `MagicItem::CalculateMagickaCost`, off and on, so the log holds both values (review R7). The buff
reason gets `lantern` when the 0x810 effect is the one found (review R3).

## 6. Meshes and text

- `lantern_gnd.nif`: `candlelantern01.nif` frame (one BSTriShape, collision kept); diffuse swapped to a darkened copy
  of `LampGeneric01.dds` made by a script (normal map kept); a child `NiNode` `AddOnNode9100` (grey ADDN 0x804
  `MPSFireSmall01`) at the cage centre.
- `lantern_worn.nif`: the frame skinned 100% to `NPC Pelvis [Pelv]` with a `BSDismemberSkinInstance` partition 60 at
  the left hip, built by a new `blackflame-greybuild` step with nifly. The flame: the same `AddOnNode9100`. Whether an
  addon node in a worn armor follows the body is not known (review R9): before writing the step, look for a worn
  armor in the load order that does this (`nif_inspect` over ARMA models holding `AddOnNode`); if none, the DLL
  attaches the flame on equip as an art object at the pelvis instead.
- Text: `plugins/BlackFlame/docs/lantern-text.md` (Gemini Argon draft from the fact sheet, checked line by line).
  Names in Korean follow the installed translation: 루'아 알-스케이븐, 사엘, 피우스 브룩시우스, 《매니마코, 벌레들의 왕》.

## 7. Checks

Offline: `LanternDamageRules.h` tests in `blackflame-cosave-check` (bucket per casting type and hazard, ICD edges at
exactly 0.25/0.5 s, cloak and own-spell exclusion, pct maths, map pruning); houseCARL read-back of every new record;
`nif_inspect` of the three NIFs (shape list, skin partition 60, addon node names, texture paths). Reviews: reviewer
agent + Codex, twice.

One bundled in-game run by the user, judged from `HeartOfMagic_BlackFlame.log`:

| What | Pass |
|---|---|
| Enter Ansilvund02 | `lantern place: ok`, then `keyframed ... ok` twice; lab and lantern coordinates as configured |
| Pick it up | `lantern smoke: disabled 1` |
| Equip / unequip | `buff flag ON (... lantern)` / `buff flag off`; `lantern cost` lines: worn = 1.3 x off for keyword fire spells |
| Firebolt, Fireball (direct and splash), Fire Rune, Incinerate | one `lantern damage` per 0.5 s per target, intended = 2% of max |
| Flames held 5 s on one target | `lantern hit` count over 5 s, damage lines >= 0.25 s apart, sum <= 8% of max |
| Wall of Flames | hazard hits in the 0.4% bucket |
| Flame Cloak | only `rejected ... (cloak)` |
| Hidden spell vs resistance | `after - before == intended` on a fire-resistant target (if the same-task read works); otherwise one cast on a resistant target with nothing else hitting it, step written into the run sheet |
| A kill | `lantern kill: killer=00000014` |

Left to the eye (no log can judge them): the lantern sitting on the lab (the log proves the coordinates, not that it
looks right), the worn lantern's place on the hip, the flame inside the cage.
