# Black Flame - "흑염 달구기" prototype

An SKSE plugin plus a small ESL. The player casts the self buff **흑염 달구기** ("Stoke the Black Flame",
60 s). While it lasts, the visuals of every fire spell the **player** casts turn grey-black: the burn
shader on targets, the projectile, the explosion, the casting art in the player's hands and the casting
lights. NPC fire spells, projectile physics and damage do not change. The look follows the Black Flame
mod's burn shader: the engine's additive blending stays, only the colour palette becomes a colourless
grey ramp, so the result is grey-black rather than pitch black.

This is a **separate prototype**. It is built by the Heart of Magic super-build (`BuildRelease.ps1`)
but is not in the Heart of Magic release: it is not in `plugins/cmake/Distribution.cmake` or the FOMOD.
It does not link SpellLearning; it only reads Heart of Magic's `spell_catalog.json` when present.

Design and decisions: [docs/superpowers/specs/2026-10-15-black-flame-proto-design.md](../../docs/superpowers/specs/2026-10-15-black-flame-proto-design.md).

## Files

| File | What |
|---|---|
| `data/HeartOfMagic_BlackFlame.esp` | ESL (master Skyrim.esm; houseCARL dropped the unused Update.esm master when it added the ADDN records): EFSH `HoMBF_BurnShader` 0x800 (vanilla FireFXShader with both palettes grey), KYWD `HoMBF_StokedKeyword` 0x801, MGEF `HoMBF_StokeEffect` 0x802, SPEL `HoMBF_StokeSpell` "흑염 달구기" 0x803, ADDN `HoMBF_GreyAddon_*` 0x804-0x80C (section "Grey duplicates") |
| `data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json` | the grey duplicate map (section "Grey duplicates") |
| `data/HeartOfMagic_BlackFlame_Copies.esp`, `data/HeartOfMagic_BlackFlame_Local.esp` | empty ESLs (no masters, no records, HEDR 1.70; `blackflame-greybuild --write-empty-esls`): enabled from the start, so the builder's output - the same two file names in a mod placed after this one - takes over without changing the plugin list |
| `data/HeartOfMagic_BlackFlame_Slots.esp` | single launch (work in progress): the slot pool ESL, 2000 placeholder records at 0x800-0xFCF (`blackflame-greybuild --write-slot-pool`; section "Single launch: the builder (P1)"). Not loaded by anything yet |
| `data/meshes/heartofmagic/blackflame/` | grey master particle systems and remapped impact / hazard copies, made from the vanilla BSAs by `tools/blackflame-greybuild` |
| `data/textures/HeartOfMagic/BlackFlame/GradBlackFlame.dds` | the grey palette (256x64) of normal-blend palette shaders and of the runtime recolour, made by `tools/make-gradient.js` |
| `data/textures/HeartOfMagic/BlackFlame/GradBlackFlameAdd.dds` | the bright-edge palette of additive palette shaders in the copies (Godskin; `make-gradient.js <out> godskin`). The path is fixed: the colours are whichever DDS sits there |
| `data/optional/palettes/GradBlackFlameAdd_blackblade.dds`, `_messmer.dds` | other bright-edge palettes, not loaded: copy one over `GradBlackFlameAdd.dds` (a later FOMOD option) - no rebuild of the copies needed |
| `data/SKSE/Plugins/HeartOfMagic_BlackFlame.json` | config (below) |
| `build/plugins/BlackFlame/Release/HeartOfMagic_BlackFlame.dll` | the plugin (build output) |
| `src/` | the plugin source, one file per layer |

## Install by hand

1. Build: `.\BuildRelease.ps1 -preset Release-2026` (this machine) from the repository root.
2. Make one new MO2 mod folder and copy into it, keeping the folders:
   - `plugins/BlackFlame/data/*` (the ESP, `textures/`, `SKSE/Plugins/HeartOfMagic_BlackFlame.json`)
   - `build/plugins/BlackFlame/Release/HeartOfMagic_BlackFlame.dll` to `SKSE/Plugins/`
3. Enable the mod and the ESP. Needs SKSE and Address Library (SE 1.5.97 or AE 1.6.x; VR not built).
4. Log: `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_BlackFlame.log` (info lines are
   buffered: they reach the file on a warning, a game load, a save, a new game and at the end of the
   self-test). Per-recolour and per-swap lines are limited to one per spell and kind per second (the next
   line says how many were held back).

## Config (`SKSE/Plugins/HeartOfMagic_BlackFlame.json`)

A missing file, bad JSON or a bad value keeps the default for what was wrong and logs why.

| Key | Default | What |
|---|---|---|
| `burnShader` | `true` | Layer 1: the hit shader of the player's fire effects is the grey burn shader (on targets, and on the player for cloaks) |
| `hitArt` | `false` | Layer 1: runtime recolour of the hit art of the player's fire effects (e.g. a cloak's flames). Kept after C6, off by default, until `copyHitArt` is proven in game (E6 and E6-npc PASS); it and `handArt`, `boundWeapon` are what the `Recolor.cpp` material code still serves, and that code goes when they go |
| `handArt` | `false` | Layer 4: runtime recolour of the casting art in the player's hands. Kept after C6, off by default, until `copyCastingArt` is proven in game (SetCastingArt calls on a real charge; on SE the SE ids first). The casting light is tinted with `light` |
| `boundWeapon` | `false` | Layer 4: runtime recolour of the player's bound fire weapons (their arrows count as fire projectiles whatever this says). Kept after C6, off by default, until the bound-weapon copies replace it |
| `scriptPlaced` | `true` | Wrap the Papyrus natives `ObjectReference.PlaceAtMe`, `Spell.Cast` and `Spell.RemoteCast` so what scripts of the player's fire effects place or cast from is tagged as the player's: its projectiles take their copies, `copyPlaced` hands PlaceAtMe the copy, and a placed fire explosion or hazard gets its light tinted (section "Script-placed fire"). `false`: the natives are left untouched |
| `impactSwap` | `true` | The player's fire impacts load the grey copies named by the grey map (section "Grey swap") |
| `hazardSwap` | `true` | The player's fire hazards load the grey copies named by the grey map |
| `light` | `true` | Tint projectile, explosion, hazard, casting, light-effect and script-placed lights to `lightColor` (no LIGH copy exists: the per-instance light tint stays in v2) |
| `copyLaunch` | `true` | v2: the player's fire launch takes its projectile copy (`Projectile::Launch` detour, section "Black Flame v2") |
| `copyLaunchExplosion` | `true` | v2: after that launch the projectile's explosion is set to its copy |
| `copyExplosionCreate` | `true` | v2: explosion creation hands the explosion copy when the source projectile is a copy or the player's self-area fire cast made it |
| `copyImpacts` | `true` | v2: impacts inside a copy window take the impact copies |
| `writeRequest` | `false` | v2 expansion: write the copy request and the NIF dump for the builder at kDataLoaded (section "Copy request"). Off for players: then no NIF is read at startup; the tester package turns it on |
| `testerMode` | `false` | the tester package: `selfTest` and `writeRequest` forced on, every log line written through at once (section "Runtimes and Address Library ids") |
| `copyCastingArt` | `true` | v2 expansion C4: SetCastingArt's call gives the player's fire hand art its copy (section "Swap points") |
| `refreshCastingArt` | `true` | when the buff turns on or off, the player's drawn hands that hold a fire spell get the copy or the original casting art at the next tick (~100 ms), without a re-ready (section "Swap points", hand art refresh). AE 1.6.1170 and 1.7.104 only (1.7.104 checked only by the install-time byte patterns), with `copyCastingArt`; off for the session without the main-thread queue |
| `copyHazardCreate` | `true` | v2 expansion C4: hazard creation gives the hazard its copy inside a copy window or from the player's fire spawn-hazard effect |
| `copyPlaced` | `true` | v2 expansion C4: PlaceAtMe called by the player's fire effect script places the copy of a HAZD, EXPL or PROJ |
| `copyHitArt` | `true` | v2 (ARTO spike): the hit art of the player's fire effects is the art's ARTO copy when it has one - `GetHitEffectArt` returns the copy, so the engine builds the art's 3D from the copy's grey NIF; an art without a copy keeps the engine's art (recoloured only under `hitArt`) |
| `copyModelProbe` | `"log"` | v2i: does the game load every copy NIF? `"log"` (default; `true` means the same): probe and log only, no copy is ever taken out - the in-game run of 46855dd reported 347 of 356 copy models failed (`error code 1, node NULL, 0 ms`) although the copies load in play, so the probe's way of asking is in doubt (being investigated). `"drop"`: a copy whose model fails while its source's loads is left out of the swaps for the session (the form stays original), a failed grey muzzle goes back to the source's, and links other copies hold to a dropped copy return to the source at once (T0-v2 FAIL lines); in play only those links are stored, one pointer each (W11), and the rest of such a refill waits for the next load's refresh - the tester file lists it as deferred (INFO, W26). `"off"` / `false`: no probe. Every failure line quotes the exact string handed to `BSModelDB::Demand` for the copy and for its source (the control). Cost: a normal start probes nothing - the last complete probe's results are cached in `<SKSE log folder>\HeartOfMagic_BlackFlame\HeartOfMagic_BlackFlame_probe.json` (`My Games\Skyrim Special Edition\SKSE\...`), keyed by the copies map's bytes, its write time (a builder run rewrites it), the plugin version and the file-index state (W20: CRDW.dll's size and time, every copy NIF's size and time; Y8, Z14: every derived palette file's
too, all as UTF-8 text and sorted), and reused. Only a complete probe without failures is cached (W14), so the cache's effect is skipping the probe; a cache that does not read is ignored and the probe runs (W23). A stale file index is reported only when the control (the source's own model) loaded, and its corner notice only with CRDW.dll loaded (W13, W25). It probes again when that key changed, there is no cache, `selfTest` / `testerMode` is on, or `copyModelProbeAlways` is true. A probe demands each distinct copy model path (record models and PROJ muzzles) in 8 ms slices, one per frame (no visible hitch at 60 fps; a single NIF that takes longer still loads whole, and the log names the largest slice), finishing in the background in the main menu or on into play (a copy not probed yet stays enabled); 20 s of probing at most. The log gives the probing time, the frames used and the largest slice. To force a re-probe: delete the cache file, or set `copyModelProbeAlways` for one start |
| `copyModelProbeAlways` | `false` | Probe at every start instead of reusing the cached result |
| `selfTest` | `false` | Run the scripted self-test once after a save is loaded (section "Self-test") |
| `devAutoLoad` | absent | **Dev only** (not in the shipped json or the tester package; ignored unless `selfTest` or `testerMode` is on): a save name without extension, loaded once, 5 s after the main menu first opens in a session, unless a load has started by then (`DevAutomation.h`); with `singleLaunch` it also waits until the slot publication settled, 5 minutes at most |
| `devAutoE3` | `false` | **Dev only** (same rules): when E3 asks for a save and reload, the plugin waits 3 s, saves `HoMBF_E3`, waits for SKSE's save message plus 3 s (10 s at most without it) and loads `HoMBF_E3`; E3 judges that load as it judges a player's |
| `devAutoQuit` | `false` | **Dev only** (same rules): the game quits (`Main::quitGame`) 10 s after the self-test's DONE, or after DONE E3 when E3 armed; 12 minutes (6 before 2026-10-18: the main run's step caps alone add up to 270 s of play) after the last load (or the auto load's request, or the main menu's first opening when `devAutoLoad` is set: the kDataLoaded-to-menu time does not count; 20 minutes after kDataLoaded when the main menu never opens) without that, it quits anyway and logs TIMEOUT |
| `selfTestDelaySeconds` | `30` | Seconds of unpaused play (player in a loaded cell, no menu, not loading) after the load before the self-test's first step (0..600); E3 judges 1.5 s of ready play after its reload instead (fix H5) |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |
| `singleLaunch` | `false` | The single-launch route (section "Single launch: the DLL side"): the slot pool, the ledger, the builder started by the game, publication at kDataLoaded and at the main menu. Off: the copies map route as above. Off by default because the route has not run in game and needs P1's slot ESL and builder |
| `launchBuilder` | `true` | With `singleLaunch`: start the builder at kDataLoaded (`false`: only copies already built are published) |
| `slotPreflight` | `false` | With `singleLaunch`: at kPreLoadGame read the save's co-save from disk and refill the slots whose pairs differ (their data only, no SetModel) before the engine reads the save. Off: only logged (the message's save name format and refilling at that point are unverified in game). On or off, a co-save that is there but cannot be read, or whose BFSL record does not read, closes every slot gate for the session (log and HUD line); a save without a Black Flame block (made before the mod or the slot route) is normal |
| `slotTxst` | `false` | With `singleLaunch`: compute TXST keys, ask the builder for TXST copies in request v5, and publish the TXST slots like the other members of their groups (W1 from the ledger, W2 as their lines come): TX00 / TX03 set to the builder's grey copies (`SlotTxst.h`), the gate opened, and the PROJ / IPCT copies' decal links then point at the slot. Off, no TXST build is requested and the decals keep the source's sets |
| `selfTestSlots` | `false` | With `selfTest` and `singleLaunch`: the self-test steps T0-slots and T0-W2 |
| `tint`, `tintAdd` | `0.6`, `0.9` | With `singleLaunch`: the copies' base colour scales (0..2; another value keeps the default, logged), quantised to 6 decimals into the keys and request.json v5 (the builder takes them only from there) |
| `paletteMode` | `"derived"` | With `singleLaunch`: `"derived"` or `"godskin"`, into the keys and request.json v5 |
| `builderJobMemoryMiB` | `1536` | With `singleLaunch`: the builder's job memory cap (256..16384), also passed as `--job-memory` |

Retired keys (C6): `projectile`, `explosion`, `hazard`, `forcePalette`, `particleColors`, `tintScale` (and the older
`burnMode`). An old json keeps loading; each is logged once as `retired, ignored` with the reason, never as an unknown key.

## How it works

**Fire spells and fire effects** (built once at kDataLoaded; v2 rule, user decision 2026-10-16): a spell
is fire when one of its effects is a fire MGEF - the vanilla `MagicDamageFire` keyword (Skyrim.esm 0x01CEAD)
or fire resistance as its resist value - OR when Heart of Magic's `spell_catalog.json` tags it "fire". Only
`SpellType::kSpell` counts. A fire-MGEF spell's fire effects are its fire MGEFs; a catalog-only fire spell's
fire effects are all of its effects (it has no fire MGEF to pick; e.g. Vigilant's Script-archetype Blinding
Fire). The v2 copies change only visuals, so a catalog false positive costs only a grey look (this reverses
round 8b, where the catalog tag alone did nothing). The log says how many spells are fire by the tag alone. A fire
Cloak-archetype MGEF's associated spell (the damage spell the cloak casts on whatever comes near) is a fire spell
whatever its SpellType, with every effect of it a fire effect (v2 expansion R3; repeated until nothing is added; the
log counts them), so what a player's cloak puts on a target counts as the player's fire. Those effects add no fire
explosions or hazards (v2 fix J4: a frost sub-spell's shared explosion must not turn grey while stoked). The
fire explosions are those the fire effects and their projectiles name; the fire hazards are those the fire
effects can leave (their impact data sets' hazards, their explosions' impact hazards and placed
objects, a spawn-hazard archetype's form) plus any hazard whose own spell carries a fire effect. The
log gives the counts.

**Whose it is** (projectiles, explosions, hazards): the player's when the owner or shooter is the
player, OR a NonActorMagicCaster's `blameActor` is the player (an explosion's own caster, or the
ExtraMagicCaster of a non-actor shooter: `Spell.RemoteCast` with the player as blame), OR a script
of the player's fire effect placed it or cast from it (script tag; actors are never tagged). The
actor cause is NOT a rule: a fire trap the player set off may name the player as its cause, and
vanilla fire traps are Spell-type spells with fire effects. A cause naming the player with no rule
matched is logged once per layer. Which rule matched is logged once per layer. The hit shader and hit art use the
active effect's caster handle. No mesh, node, mod, plugin or FormID is special-cased: vanilla meshes, VFX replacers,
legacy NiTriShape meshes and modded spells all go through the same rules.

**Buff flag**: one atomic, written on the game thread when an effect is applied to or removed from
the player, at game load and new game, and by a ticker (every 100 ms while on, every 1 s while off).
Hooks only read it. Every change is logged.

**Recolour** (C6: only `hitArt`, `handArt` and `boundWeapon`, all off by default): every `BSEffectShaderProperty` found by walking the 3D gets its own copy of
its material (materials are pooled; `SetMaterial(copy, true)` gives the property an unshared one),
with the grey palette as greyscale texture and the base colour desaturated (x 0.6). No palette flag is added to a shape
without one (the builder's v2i rule). Already grey materials are skipped, so it is safe to repeat. Projectiles, explosions,
hazards and script-placed fire are never recoloured any more: their copies are grey, a form without a copy stays orange.

| Layer | Where |
|---|---|
| 1 hit shader and hit art | `ActiveEffectReferenceEffectController` vtable: `GetHitEffectShader` (0x0D) and `GetHitEffectArt` (0x0C), where the engine asks an active effect which hit shader and art to put on its target. While stoked, for an effect the player cast (caster handle) whose MGEF is a fire effect of a fire spell (scrolls, staffs and enchantments sharing the MGEF do not count): the shader call returns `HoMBF_BurnShader` instead of the engine's, so the engine makes the hit shader from ours (instant hits, concentration and cloaks alike; no event, no record changed, nothing to finish); the art call returns the engine's art, and with `hitArt` on the art's 3D (`ModelReferenceEffect::artObject3D`) is recoloured on a later tick once it exists (up to 2 s; matched on controller and art only - the engine's first art call comes before it sets the target - with the live controller re-checked as the player's fire effect, and the target read from the live effect). Both slots are hooked whatever the config says (v2 S3); `burnShader` and `hitArt` decide only the swap and the recolour. Logged per slot, with two caps that cannot use each other up: the first 20 calls whose caster is not the player and that have something to show (the engine's answer or the MGEF's own field not null), and the first 60 calls whose caster is the player, whatever the outcome - each with the controller and its call count, the active effect's flags (+0x7C, raw, and bit 1: the engine's "no hit shader" bit, set only by the cloak callback), the MGEF and its own field, the spell, the target, the caster, whether stoked, the burn shader form, the engine's answer, the outcome with its reason (swapped, engine answered null, not stoked, not a fire effect, config off...) and the thread. Per-outcome counters of the player's calls feed the self-test (E4). The engine reads MGEF `data.effectShader` only through slot 0x0D (AE 34846 / SE 34049, called from `ReferenceEffectController::Start` 34761 and the `ShaderReferenceEffect` constructor 34910; research 2026-10-16), so no other swap point exists; enchantment shaders go through `ApplyEffectShader` (19872) and are out of scope |
| 2 projectile | `Handle3DLoaded` (vfunc 0xC0) on the Missile, Flame, Cone, Beam, Grenade, Barrier and Arrow vtables; the player's (see "Whose it is") and a fire spell's, or an arrow of the player's bound fire weapon: its light tinted (C6: no 3D recolour), the self-test record and the material check |
| 3 explosion | `Set3D` (vfunc 0x6C) on the Explosion vtable; base form in the fire explosion set, the player's (see "Whose it is"), decided only once owner, blame or a script tag (also on the explosion caster's object reference, `GetCasterObjectReference`) is there, given up after 2 s; the explosion's own magicCaster (Explosion.h:87) must name nothing, a fire spell, or an enchantment (the explosion's own; a weapon enchantment cannot be told apart) - a scroll, staff, power, potion and so on are left; a light missing at Set3D is tinted on the next tick. Initialize (0xA2) and Update (0xA3) are hooked as a second route (Update does nothing unless stoked or the self-test records); each explosion is decided once, and the first 20 hook calls on any explosion are logged |
| 3 hazard | `Set3D` (0x6C) and `Initialize` (0xA2) on the Hazard vtable: a hazard made inside the player's impact or copy window, or whose base is a copy, is tagged; while stoked (and `light` on), a hazard with 3D is queued once (by handle) and handled by a task: the player's (owner or script tag waited for up to 2 s) and a fire hazard (see above) gets its light tinted (light type checked through RTTI; C6: no 3D recolour); a pass without a light lets a later Set3D queue it again. The first 20 hook calls on any hazard are logged |
| 4 hand art (`handArt`, off by default; kept until `copyCastingArt` is proven) | ticker task every 100 ms while stoked, and at once when the buff turns on: per hand casting a fire spell OR holding one readied (Actor::GetEquippedObject), the casting art itself (`ActorMagicCaster::castingArtData.attachedArt`); only when that is null or holds no effect geometry (logged as FALLBACK) the caster's `magicNode`, the hand's magic node by name in the 1st and 3rd person skeletons and, last, the player's casting-art `ModelReferenceEffect`s; the caster's light tinted. Every recoloured hand-art property is remembered with its original greyscale texture, path, base colour and palette flags, and put back when the buff turns off and on a load (a fresh material copy, SetMaterial(copy, true)); the casting light's old colour too. Entries only we still hold are dropped each tick |
| 4 bound weapon (`boundWeapon`, off by default), light effect (`light`) | ticker while stoked: a bound fire weapon (bound flag, named by an active Bound Weapon-archetype effect the player cast on themselves, that effect a fire effect of a fire spell or the weapon's own enchantment carrying a fire MGEF, and the weapon one of the static bound fire weapon seeds; the list is kept by the ticker and the arrow hook only compares FormIDs) is recoloured in the player's 1st and 3rd person bipeds (`BIPOBJECT::partClone`); a Light-archetype fire effect the player cast on themselves has its light tinted (`LightEffect::light`). Both are put back with the hand art |

## Grey duplicates

**Why.** Fire impacts and hazards (e.g. `Effects\ImpactEffects\FireballImpact01.nif`, `Magic\FireWallFX01.nif`)
hold AddOnNodes: `BSValueNode` blocks whose integer `value` is an ADDN index. The engine does not draw their
particles in the impact's own 3D: it emits them into one shared `BSMasterParticleSystem` per index, built from
the ADDN record's model (`MPS\MPSFireBoltImpact01.nif` for index 9). One emitter serves every caster's impacts,
so recolouring it for the player's hit would recolour everyone's. Instead the addon ships grey copies of those
master particle systems under new ADDN indices, and copies of the impact / hazard NIFs whose AddOnNodes name
the grey indices; the DLL swaps the player's impact and hazard models to the copies while stoked (section
"Grey swap"). Precedent in the TAKEALOOK order: mihailskeletalrevenants.esp, Desecration.esp and Flames of
Coldharbour.esp ship recoloured MPS copies under their own ADDN indices.

**What ships (the vanilla core).**

| Original ADDN (Skyrim.esm) | Index | Grey index | Grey record (this ESL) | Used by |
|---|---|---|---|---|
| MPSFireSmall01 01A71D | 0 | 9100 | 0x804 | FXFire02New |
| MPSFireWallBase 01C2B0 | 3 | 9103 | 0x805 | FireWallFX01 |
| MPSFireWallSparks 01C2B1 | 4 | 9104 | 0x806 | FireWallFX01 |
| MPSFireBoltImpact01 02B95B (DNAM flags 3, always loaded) | 9 | 9109 | 0x807 (flags 3 too) | FireballImpact01 (20 fire impacts) |
| MPSDragonSmoke 0211D4 | 12 | 9112 | 0x808 | FXDragonFireImpact |
| MPSFireSmSmoke01 03E072 | 23 | 9123 | 0x809 | FXFire02New |
| MPSFireLg01 03E071 | 24 | 9124 | 0x80A | FXFire01New, Embers XD's FireWallFX01 |
| MPSFireLgSmoke01 03E070 | 25 | 9125 | 0x80B | FXFire01New, Embers XD's FireWallFX01 |
| MPSFireWaveFlames 0647BC | 64 | 9164 | 0x80C | Embers XD's FireWallFX01 |

Each record: EditorID `HoMBF_GreyAddon_<original>`, DATA the grey index, MODL
`HeartOfMagic\BlackFlame\MPS\<original>.nif`, DNAM copied (cap 0, flags 1 = master particle system, 3 = always
loaded, as Skyrim.esm stores them), OBND as Skyrim.esm's. No ADDN index above 8444 is used in the TAKEALOOK
order (houseCARL scan); the grey range starts at 9100.

**The map**, `data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json` (version 1): `tint`, optional `tintAdd` (v2i, default 0.9), `addons` (per
original index: `grey`, `model` and `greyModel`, `editorId`, `source`, `masterParticleCap`, `dnamFlags`,
`greyEditorId`, `greyForm` as `Plugin|localId`) and `models` (impact / hazard NIF -> its grey copy). Paths are
Data-relative, lower case, backslashes (`meshes\mps\mpsfireboltimpact01.nif`); a record's Model field is the same
path without `meshes\`.

**The grey copies.** Only a *palette shader* - a `BSEffectShaderProperty` whose source already maps its texture
through a palette (greyscale-to-palette colour flag, SLSF1 bit 4, on and a non-empty source texture) - gets one of our
palettes as its greyscale texture, chosen by the blend of the shape that uses it (its `NiAlphaProperty` flags:
blending on and destination blend ONE, i.e. SRC_ALPHA->ONE or ONE->ONE, is additive):
`textures\HeartOfMagic\BlackFlame\GradBlackFlameAdd.dds` for additive (an additive shape cannot draw dark, so this
palette has bright edges), `GradBlackFlame.dds` for normal blend. A palette shader whose shape has no alpha property
counts as normal blend and the build prints a `NOTE` line for it. Every other shader (textureless glows, FXWhite tint
spheres, lens flares, palette-alpha-only shaders) keeps its flags and textures (v2i: forcing the flag and the palette on
them made flat grey shapes; since Y1 the palette colour flag alone makes a palette shader, see "Glow rules"). Every
shader's base colour RGB = luminance x `tint` (Rec. 709 weights and default 0.6, the DLL's `Tint.h` and the 0.6 of its
kept runtime recolour), or x `tintAdd` (default 0.9, `--tint-add`) on an additive palette shader - on a palette shader
that only moves the palette row (Effect.hlsl); the glow rules then write the colours again and put `tintAdd` on a derived
additive palette's emissive multiple. Alpha kept; the palette alpha flag stays as it was. `BSPSysSimpleColorModifier` keys and `BSEffectShaderPropertyColorController`
keys (direct `NiPoint3Interpolator` only; the build counts any it cannot reach) go to luminance, alpha kept, the
controller's keys scaled like the base colour of the shader it targets. The palettes are made by
`node tools/make-gradient.js <out.dds> [default|godskin|blackblade|messmer]` (no name: the grey palette, byte-identical
to the shipped one); the bright-edge ones are smoothstep curves between stops with wide edge plateaus (narrow peaks drew
contour lines).
Lighting-shader shapes are left alone. An impact / hazard copy changes only its AddOnNode values (names kept);
its own inline particles (e.g. FireWallFX01's flames) stay as they are, for the DLL's per-instance recolour.
The rest is re-saved unchanged. The header string table is kept exactly as the source has it (v2i): nifly's own Save
rebuilds it from the string references it knows (`NiHeader::UpdateHeaderStrings`), dropping strings no known reference
names and renumbering the rest - Dragonfire's copy lost 'AutoPlay', 'GenericBehaviors\Autoplay.hkx' and
'Magic\SprayRange.hkx' - so the builder puts the source's table back after nifly's Save (same strings, same order,
duplicates included), points every known reference at the slot it named before (a new string goes at the end), and writes
the file as nifly's Save does but without the rebuild; every output is checked to start with the source's table. On the
TAKEALOOK request 489 of 513 unchanged re-saves are now byte-identical (was 156); the other 24 are 4-48 bytes shorter
in node blocks (the file in total), consistent with nifly removing empty child references (`--roundtrip` lists what changed).

**The builder**, `tools/blackflame-greybuild` (C++, built by `BuildRelease.ps1` into `build/tools/Release/`):

```
blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>
blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]
blackflame-greybuild --dump <file.nif> [...]        (AddOnNodes, effect shaders, colour keys)
blackflame-greybuild --roundtrip <file.nif> [...]   (does an unchanged re-save match?)
blackflame-greybuild --self-test [--golden-dir <dir>] [--print-golden]
                                                    (made-up NIF, no game files, and the golden test with
                                                    --golden-dir tools\blackflame-greybuild\golden; exit 0 = pass)
blackflame-greybuild --make-golden-fixtures <dir>   (the golden test's input files, written once and committed)
blackflame-greybuild --identity                     (the output identity this exe keys with: <n> or <n>+dev<hex>)
```

`--in` folders hold loose files laid out as Data (`meshes\...`), searched in the order given: the first that has
a file wins, so list them in MO2 priority order, highest first, then extracted BSAs, then the vanilla
extraction (the builder reads loose files only; extract BSAs first, e.g. houseCARL `housecarl_bsa_extract`).
`--out` must not overlap any `--in`. Every output is read back and checked (each palette shader on the palette its
blend takes, grey colours, no original index left); each NIF's line and the run's summary count the effect shaders as
palette normal (and how many of those without an alpha property), palette additive and left with a grey base colour; an AddOnNode value the map does not know fails that file and nothing is written for it.
The shipped meshes were made with `--map` on Skyrim - Meshes0/1.bsa extracted to a scratch folder.

**Grey duplicates for any setup (`--request`).** VFX replacers (Embers XD, Inferno, Particle Patch, Dragon
Breath VFX Edit, ...) replace the MPS files and impact meshes and add their own ADDN indices (Embers XD.esp: 18 records,
224-242 without 226), so a fixed core cannot cover them. A request lists what one load order has:

```json
{ "version": 1,
  "addons": [ { "index": 9, "model": "MPS\\MPSFireBoltImpact01.nif", "editorId": "MPSFireBoltImpact01",
                "source": "Skyrim.esm|02B95B", "masterParticleCap": 0, "dnamFlags": 3 } ],
  "models": [ "Effects\\ImpactEffects\\FireballImpact01.nif" ] }
```

`addons` is the winning ADDN record per index (all of them, so taken indices are known; the shipped grey records
included); `models` the fire impact / hazard NIFs. The builder reads each model's AddOnNodes from the `--in`
folders, and for each index used picks a grey index: 9100 + original when free, the shipped record at that index
when it already names the same grey copy, otherwise the next free index above all used ones. It writes the grey
MPS and copies, the full map (`SKSE\Plugins\HeartOfMagic_BlackFlame_greymap.json`, core entries included) and,
when indices beyond the shipped records are needed, `HeartOfMagic_BlackFlame_Local.esp`: an ESL with no masters
holding one ADDN record per extra index (FormIDs 0x800 up, HEDR version 1.70; the written file is read back and compared). A re-run keeps the Local.esp records the request already lists with their FormIDs (new ones take the ids after them, at most 0xFFF), finds an existing grey record by the copy it names at any index, and removes a stale Local.esp in `--out` when no extra index is needed (or stops with exit 1 if it cannot). Two different MPS models writing one grey copy are refused; bad sizes in an ESL it reads are errors. Installed as a mod placed after this one (and the ESL
enabled), its files win over the shipped map and meshes. Who writes the request: planned, the DLL at kDataLoaded
(every `BGSAddonNode` with index, model and DNAM, plus the fire set's impact and hazard models); until then it
can be made by hand or from a houseCARL / xEdit ADDN listing. Embers XD's meshes are never redistributed: the
patch is made on the user's machine from their installed files.

Tested on the TAKEALOOK order (2026-10-15, request from a houseCARL ADDN dump, 329 indices; 8 indices are
defined twice there, DragonWar.esp and mihailskeletalrevenants.esp, and the request keeps the later):
`--in` Dragon Breath VFX Edit, Embers XD - CS, Embers XD, then the Inferno, Particle Patch and CS Particle Patch
BSAs and the vanilla BSAs extracted to scratch. 13 files built: Embers XD's MPS for 0, 9, 23, 24, 25, 64, 225, CS
Particle Patch's for 12, Embers XD - CS's FXFire01New / 02New, Dragon Breath VFX Edit's FXDragonFireImpact, Embers
XD's FireWallFX01 (values 64, 25, 24, 225 to 9164, 9125, 9124, 9325). Every index but 225 reused a shipped
record; 225 got 9325 in a generated ESL (`HoMBF_GreyAddon_MPSLightGlow02`, read back by the builder and by a
separate byte reader).

**Licence.** nifly (GPL-3.0) is linked into the builder only, never into a DLL (`THIRD-PARTY-NOTICES.md.in`,
"Build tools"). The shipped grey meshes are derived from Bethesda's vanilla meshes, as mod meshes normally are.

**Not verified in game:** that a grey MPS draws grey (palette on baked-colour replacer textures included); that
the always-loaded grey 9109 behaves like 9; that an extra ADDN record from the local ESL works.

## Grey swap (the DLL side)

Only for the player's fire while stoked, the engine loads the grey copy instead of the original impact or hazard
mesh. No form is changed and no form is made; NPC fire never takes the swap.

- **Map**: `Data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json` (version 1) read once at kDataLoaded. One
  file name only: `blackflame-greybuild --request` writes its full map under the same name, so a generated map in a
  later mod replaces the shipped one. Keys and values are normalised (lower case, backslashes, no leading
  `meshes\`) so they match `TESModel::GetModel` paths; entries outside `meshes\heartofmagic\blackflame\` or not
  `.nif` are skipped and logged. The strings live for the session (the engine keeps the returned pointer for an
  async clone).
- **Impacts**: the three calls of `Projectile::SpawnCollisionEffects` (found by scanning their callers for an E8 to
  it; chained when another plugin redirected one first) go through the SKSE trampoline; around each call a
  thread-local window opens when the projectile is the player's fire and the buff is on. The explosion side opens
  the same window around `Explosion::Update` for the player's fire explosions (with `impactSwap` on, the same switch as the projectile side). While a
  window is open, BGSImpactData's `GetModel` returns the grey copy when the map has one.
- **Hazards**: `Load3D` on the Hazard vtable: the player's fire hazard (owner, blame or script tag; one made inside
  an impact window is tagged first) whose base model has a grey copy gets it through BGSHazard's `GetModel` during
  that load only.
- **Address Library**: all eight function ids are present in the 1.6.1170 and 1.5.97 Address Library (checked with the id audit reader, 2026-10-16). Every function id is also checked in the running game's database first (`REL::Offset2ID`); a
  missing id turns the impact swap off with a log line instead of a failed lookup.

| What | SE / AE | Status |
|---|---|---|
| `Projectile::SpawnCollisionEffects` | 43022 / 44213 | present in the 1.6.1170 and 1.5.97 Address Library (AE 0x7ecd80, SE 0x7531c0); code: 1.7.104, 1.6.1170 by id alignment, SE matched by algorithm (po3 uses the pair) |
| `Projectile::ProcessImpacts` (caller 1) | 42943 / 44123 | present in the 1.6.1170 and 1.5.97 Address Library (AE 0x7e5cd0, SE 0x74c710); code: 1.7.104 and 1.6.1170 vtable slot 0xAC; call offset +0x407 AE / +0x410 SE from MuIF only (fallback) |
| caller 2 (from `HandleHits`) | 43014 / 44205 | present in the 1.6.1170 and 1.5.97 Address Library (AE 0x7ec130, SE 0x752620); code: 1.7.104, SE matched by algorithm |
| caller 3 (from `ConeProjectile::UpdateImpl`) | 42638 / 43806 | present in the 1.6.1170 and 1.5.97 Address Library (AE 0x7ce260, SE 0x7369b0); code: 1.7.104, SE matched by algorithm |
| `VTABLE_BGSImpactData[1]` slot 4, `VTABLE_BGSHazard[2]` slot 4 (`TESModel::GetModel`) | CommonLib ids (AE 190905 / 189083) | present in the 1.6.1170 Address Library (0x17b7e18 / 0x1793ca8); slots verified on 1.6.1170 .rdata (research notes) |
| Hazard vtable slot 0x6A (`TESObjectREFR::Load3D`) | CommonLib id | verified on 1.6.1170 .rdata; not overridden by Hazard |
| Explosion vtable slot 0xA3 (`Update`) | CommonLib id | verified on 1.6.1170 .rdata |

The kDataLoaded grey check logs `grey check PASS/FAIL` lines (each grey ADDN record found by its plugin and local
id and sitting in `TESDataHandler::addonNodes` at its grey index, every grey model file opening, the call sites
and slots hooked); the self-test repeats them as T0-grey and counts swaps at T6-swap / T6-npc-swap.

## Black Flame v2 (copy framework spike)

Branch `feature/black-flame-v2`. The runtime material recolour leaked into NPC fire through shared materials and
relied on a main-thread assumption that run 2 disproved, so v2 makes "the player's only" structural: the player's
fire launches with **grey copies** of its projectile, explosion, impact and hazard forms, whose NIFs have the grey
palette baked in. NPC fire keeps the original forms. Runtime recolour is off by default (config keys above); a form
without a copy stays orange and is logged once. The per-instance light tint stays. Design and decisions:
[docs/superpowers/specs/2026-10-15-black-flame-proto-design.md](../../docs/superpowers/specs/2026-10-15-black-flame-proto-design.md), "v2 스파이크".

### Copies (S1): `blackflame-greybuild --forms`

```
blackflame-greybuild --forms <forms.json> --request <addons.json> --in <dir> [--in <dir> ...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>] [--no-textures]
```

The forms request (version 2) lists source forms, each with `source` (`Plugin|localId`, the defining plugin), `type`
(`PROJ`, `EXPL`, `IPCT`, `HAZD`, `ARTO`; no IPDS or LIGH copies), `editorId`, `model` (as the record stores it), `bounds`
(6 integers; not on IPCT) and `data` (the scalar DATA values; unknown keys are refused, missing ones are 0; PROJ
`projectileType` takes a name or the raw bit, PROJ `soundLevel` goes to VNAM; ARTO takes only `artType` 0 casting,
1 hit effect, 2 enchant effect, written as its DNAM: an ARTO record has no DATA). `--request` is the ADDN request
of "Grey duplicates": the copies' models count for the AddOnNode values that need a grey ADDN index (no remapped
copy of them is made), so one run writes everything a setup needs into `--out`:

| Output | What |
|---|---|
| `HeartOfMagic_BlackFlame_Copies.esp` | ESL, no masters, HEDR 1.70, form version 44, FormIDs 0x800 up: one placeholder per source - EDID `HoMBF_Copy_<editorId>_<localId>`, OBND (PROJ, HAZD, EXPL), MODL = the grey copy, DATA in Skyrim.esm's size for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24) with the scalars and every form link 0, VNAM on PROJ. Groups PROJ, HAZD, EXPL, IPCT. Read back and compared after writing |
| `meshes\heartofmagic\blackflame\copies\<model path>` | the full-grey copy of each distinct source NIF: effect shaders as in the grey MPS (palette shaders on the palette of their blend, the rest with a grey base colour) and every AddOnNode on its grey ADDN index |
| `SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json` | the copies map (version 1): source -> copy id, type, models. The next run reads it back from `--out`: a source keeps its id, a new one takes the id after every id used, a source no longer requested is kept as `retired` and its id is never given to another (a saved projectile or hazard of that copy must not turn into another form) |
| grey MPS, `HeartOfMagic_BlackFlame_greymap.json`, `HeartOfMagic_BlackFlame_Local.esp` | as `--request` writes them (the copies' AddOnNode indices included) |

Ids are assigned and every record value is validated before anything is written; all outputs are then written and
read back in a staging folder beside `--out` (`<out>.staging`) and only then moved into `--out`, so a failed run
leaves the previous outputs unchanged. The copies map refuses a source listed twice (in copies, or in copies and
retired), and a new source takes the id after the highest id the map holds, retired ones included.

Install the output folder as a mod after this one and enable both ESLs. The spike's requests are hand-made from
read-only houseCARL reads of the TAKEALOOK profile: [requests/forms-takealook.json](requests/forms-takealook.json)
(33 forms: the winning PROJ, EXPL, IPCT and HAZD reached from vanilla Firebolt, Fireball, Flames and Fire Rune, plus
the two ARTOs of the art spike: Flame Cloak's hit art `FXFireCloak01` 02ACD7 and casting art `FireCloakHandEffects`
036342) and
[requests/addons-takealook.json](requests/addons-takealook.json) (the profile's 329 ADDN records). Their NIFs are
other mods' files (Flame VFX Edit, Embers XD - CS, CS Particle Patch), so the output is built on the user's machine
and not committed; the spike run used `--in` Flame VFX Edit, Dragon Breath VFX Edit, Embers XD - CS, Embers XD, then
the CS Particle Patch, Inferno, Particle Patch and vanilla Meshes0/1 BSAs extracted to scratch (33 copies, 9 copy
NIFs, 7 extra grey ADDN records for indices 5, 6, 34, 35, 80, 81 and Embers XD's 225). The art spike re-ran it into a
copy of that output with `KittyVFX - ENBHands` first (the winning `fxfirecloakhandeffects.nif`): the 33 copies kept
their ids 0x800-0x820, the two ARTOs took 0x821 and 0x822 (35 copies, 11 copy NIFs).

### Copy request (v2 expansion C1, DLL): `FireClosure.h`, `CopyRequest.h`

At kDataLoaded, after the fire sets, the DLL walks the **fire closure** from the original forms: for every fire spell its
fire MGEFs and its costliest effect's MGEF (`MagicItem::GetAVEffect`, where the engine takes the projectile and the
casting art; fire or not), then to a fixed point MGEF projectile, explosion, impact data set, casting art, dual cast
data and a spawn-hazard archetype's hazard (and for a fire MGEF its hit art and its RFCT's art), DUAL projectile,
explosion, impact data set and hit art, PROJ explosion, EXPL impact data set, placed object and spawn projectile, every
impact of an IPDS, IPCT hazard, HAZD impact data set and its spell's MGEFs. Copy types: PROJ, EXPL, IPCT, HAZD, ARTO;
IPDS, DUAL, RFCT, SPEL and MGEF are walked through; a copied PROJ's decal texture set and a copied IPCT's two make TXST
copies (section "Decal texture set copies"). Excluded, each with its reason: a form of our own plugins
(`HeartOfMagic_BlackFlame*`), a placed object that is not a copy type, an art with alternate textures. Bound fire
weapons (bound-weapon stage 1): every Bound Weapon-archetype MGEF whose associated form is a bound WEAP that is fire by the
runtime rule's two branches - a fire effect of a fire spell, or the weapon's enchantment carrying a fire MGEF (the MGEF
then need be in no fire spell) - is a seed (`FireSet.h` BoundFireSeeds, logged at kDataLoaded); the closure takes its
MGEF's projectile (bound bow MGEFs carry the arrow's PROJ there) and, for a bow or crossbow, the PROJ of every AMMO with
the `WeapTypeBoundArrow` default-object keyword (the arrows bound-bow scripts hand out: an over-approximation, harmless
because the Launch swap needs the player's bound fire weapon as the weapon source). The request's `viaSpell` is then the
seed's MGEF. No new copy type: the arrows are PROJ copies. The runtime bound fire list (`PlayerArt.cpp`) now also requires
a seed's weapon, so it stays inside the closure. Then, in
`<SKSE log folder>\HeartOfMagic_BlackFlame\` (`My Games\Skyrim Special Edition\SKSE\...`: outside the VFS, so MO2's
overwrite stays clean):

| File | What |
|---|---|
| `request.json` | version 4 (AD1; the builder still reads 3, which had no `dumpComplete`): `game`, `fingerprint`, `copiesPlugin`, `dumpComplete` (false when a dump file could not be written, with `dumpWriteFailures`, or `dumpError` when the dump stopped part way); `forms` (source `Plugin|localId` in UTF-8, type, editorId, model and muzzleModel as the record stores them, bounds, the scalars the builder writes into a placeholder, `viaSpell`, `costliest`); `excluded` (source, type, reason); `addons` (every ADDN record: index, model, cap, DNAM flags, `own` for ours); `models` (the closure's impact and hazard models); `nifs` (each NIF read: Data-relative path, size, fnv64, ok, error); `textures` (T1: each texture those NIFs name - path, size, ok = in the dump, error) and `textureCaps`. Written through a temporary file and a rename, after the dump |
| `data\meshes\...` | the winning bytes of every NIF the request names (form models, PROJ muzzles, every ADDN model not ours), read through the engine's own file system (`BSResourceNiBinaryStream`: archives, loose files and MO2's VFS as the game loads them). Rewritten only when the fingerprint differs from `dump-manifest.json`'s; a NIF that fails to read is removed from the dump; one that cannot be written, or whose path the dump refuses, is removed and listed as not read, and the request then says `"dumpComplete": false` with `dumpWriteFailures` (the fingerprints stay this load order's); the builder copies it into the map and the DLL reads such a map as stale ("built from an incomplete dump") whatever the fingerprint says (AC1; a map without the field counts as complete) |
| `data\textures\...` | T1: the textures the dumped NIFs name, read the same way within the caps (Texture copies below) |
| `dump-manifest.json` | the fingerprint the dump was made for, the textures' own `textureFingerprint`, and `textureReadFailures` (textures that opened but could not be read) |

Model paths are normalised (ASCII lower case, `/` as `\`, repeated separators folded, a leading `data\`/`meshes\` dropped,
one `meshes\` put in front); a path with a drive letter, `:` or a `.` or `..` token is refused and never read or written
(AC2: a `.` token is refused, not folded - `meshes\.\fx.nif` is never read, a known limit). Each
NIF is read in 64 KiB pieces with every return code checked and must give exactly the stream's `totalSize`.

**Fingerprint** (one formula for the request, which the builder copies into the copies map, and for the freshness
check at every start): FNV-1a 64 over the sorted lines of every closure form (type, source, model, muzzle), every ADDN
not ours (index, source, model) and every NIF (path, size, fnv64). Scalars, EditorIDs and bounds are left out (the DLL
fills them from the live forms), and so are our own plugins' forms and ADDNs (a built copies plugin must not make its
own request stale). The NIF bytes are read at every start on the kDataLoaded thread, so a same-size replacer (a
recolour) is caught; the log line gives the time (`copy request: N forms (PROJ a, EXPL b, IPCT c, HAZD d, ARTO e) from S
fire spells, X excluded, M NIFs (K failed) read in T ms, fingerprint F, request written, dump written|unchanged`). The
fingerprint, the request and the dump each have their own try/catch: a failure is logged and the hooks are installed
anyway. `writeRequest` false (the default for players) reads no NIF and makes no fingerprint, request or dump (v2 fix
J7; the freshness check of C3 makes its own cheap size form). The request also carries `fingerprintSizes`, the same
formula without the byte hashes (path and size), which the builder copies into the map for that check. A NIF is read
once (its bytes are kept for the dump); a busy open or read is tried once more and a stream that cannot seek is read from
where it opened (warned once), so a transient error does not change the fingerprint. "Unchanged" also needs every dumped
NIF to be there with its size, and dump paths past MAX_PATH are opened through `\\?\`. Model paths with `<>"|?*` or a
control character are refused; a double-byte character of the system code page is copied as it is (its trail byte is never
lowered or taken for a separator), and every leading `data\` and `meshes\` is dropped. The closure never walks an excluded
form (a costliest-effect seed reaching it again included, v2 fix J1); a copy-type form with no defining file (made at
runtime) is excluded, and a hazard's spell seeds only its fire effects and its costliest effect (J8).

### Builder from the game (v2 expansion C2): `--from-game`

```
blackflame-greybuild --from-game [<request folder>] --out <output mod folder> [--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired] [--no-textures] [--retire-left-out]
```

The request folder defaults to `<Documents>\My Games\Skyrim Special Edition\SKSE\HeartOfMagic_BlackFlame` (SE and AE)
or `...\Skyrim Special Edition GOG\...`, whichever `request.json` is newer (Documents through `SHGetKnownFolderPath`, so a
redirected or OneDrive Documents works). Skyrim VR is not supported. The run reads `request.json` (version 3) and the
NIF dump beside it - no `--in`, `--map`, `--request` or `--forms` - and writes everything into `--out`, which must be an
existing folder and **the same folder every time** (its copies map gives the stable ids). Every refusal names the next
step (no request: start the game to the main menu once; no `--out`: make an empty mod first).

- **Warn and continue**: a model the game could not read (`nifs` ok false), a NIF missing from the dump or one nifly
  cannot parse gives a copy that keeps the **source's** model (`greyModel` "", a `note` in the map, a `WARN` line); a grey
  MPS that cannot be made drops its grey index (those AddOnNodes keep the original particles); an AddOnNode value in no
  ADDN record (e.g. Flamearrowflight.nif's 99) is kept as it is and warned. With `--forms` (hand-made requests) these
  stay failures, as before: the behaviour change is `--from-game` only. A request listing one ADDN index twice keeps the
  later record (warned).
- **Copies map version 2** (`SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json`): `requestFingerprint` (the request's,
  compared by the DLL at every start), `dumpComplete` (the request's; false or not a bool: the DLL reads the map as
  stale), `copiesPlugin`, per copy also `sourceMuzzleModel`, `greyMuzzleModel` (PROJ: the
  grey copy of its muzzle flash model, built like the form models) and `note`; `retired`; `excluded` (the request's);
  `shaders` (v2i, information only, no reader takes it: `tint`, `tintAdd` and the grey NIFs' effect shader counts
  `paletteNormal`, `paletteNormalNoAlpha`, `paletteAdditive`, `left`; the run prints the same as its `shaders` line).
  Version 1 maps are still read. Copy EditorIDs are `HoMBF_C_<TYPE>_<local id>` (the runtime gives no source EditorID).
- **Ids**: kept and new as before; when no id above every used one is left, the run writes nothing and the message gives
  the requested, earlier and retired counts per type. `--reclaim-retired` then gives retired ids to new forms, lowest
  first - only for a new save (a saved projectile or hazard of a retired copy would read as the new form).
- **Commit**: every output is staged and read back beside `--out` first, then each file is moved in on its own
  (`MoveFileExW`, replace, write through). `--out` itself is never removed or renamed (MO2's `meta.ini` stays); a failed
  move names how many files were moved, and running again converges. `HeartOfMagic_BlackFlame_Local.esp` is always
  written, empty when no local grey ADDN record is needed (the old run removed it).
- **Install**: MO2 - add the builder as an executable with the arguments
  `--from-game --out "<MO2 mods>\HeartOfMagic BlackFlame Copies"`, make that empty mod first and place it after Heart of
  Magic - Black Flame; Vortex - point `--out` at a mod folder in Vortex's staging folder, or zip the output folder and
  install it as a mod. The builder needs no VFS (it reads only the dump). Then start the game again.

Checked offline (2026-10-16): the builder self-test (v3 parsing, map v2, the 2049th form, `--reclaim-retired`, warn and
continue on a missing, an unparsable and an unreadable model and an AddOnNode with no ADDN, `meta.ini` kept, the empty ESL);
a simulated game request made from the spike's hand-made requests with the same NIFs as a dump gave the same 30 grey NIFs
byte for byte, the same local ESL and grey map as the `--forms` run, and a second run kept every id; `--roundtrip` of the LE
streams `bluefiresprayfx01.nif` (Skeletal Revenants, Vigilant) and Vigilant's `flamering.nif`: string table reordered only.
Not checked: a request written by the game (C1 needs the in-game run).

### Texture copies (T1): `GreyTexture.h`, `--no-textures`

`GreyEffects` changes shader values only, so colour **painted into a texture** stays: an orange diffuse or glow map on a
lighting-shaded mesh, or the source texture of an effect shader the builder leaves off the palette. `--forms` and
`--from-game` (not `--map` / `--request`) therefore also write grey copies of those textures; `--no-textures` turns it
off (the copy NIFs then keep naming the source textures). An output built before this release has no texture copies:
start the game once to the main menu with this release (the DLL then dumps the textures), quit and run the builder again.

- **Which textures** (NIF data only, any load order): in every full-grey copy NIF and grey MPS, `BSLightingShaderProperty`
  texture set slot 0 (diffuse) and slot 2 only on a **glow shader** (shader type `BSLSP_GLOWMAP` or the SLSF2 glow flag,
  bit 6; on a skin, face or multi-layer shader slot 2 is another map and stays), and the source texture of every
  `BSEffectShaderProperty` that `UsesPalette` rejects (a palette shader's texture is a greyscale map through our palette).
  Our own textures (`textures\heartofmagic\blackflame\...`) are skipped. A texture is **already grey** when the mean chroma
  (brightest minus darkest channel) of the mip 0 pixels that are visible (alpha) and lit (brightest channel at
  `kLitFloor` 0.1 or above) is under `kGreyscaleChroma` (0.05) and no more than `kColouredShare` (0.5%) of them have a
  chroma of `kColouredChroma` (0.15) or more, or when none is lit: small orange specks on black count fully, a few orange
  embers in grey smoke count through the share, the 5:6:5 noise of a dark grey BC1 does not. When no pixel is both visible
  and lit (an additive texture whose alpha is zero), the lit pixels are measured whatever their alpha. An already grey
  texture is left only at a tint of 1: a diffuse at `--tint` 0.6 gets a darkened copy even when grey, so the diffuse
  always carries the darkening. One-channel formats (BC4, R8, A8) and legacy luminance DDS (L8,
  A8L8, L16, read expanded to RGBA) are grey too. A two-channel format (BC5, R8G8) cannot hold grey (no blue) and is
  left as a failure.
- **Glow shaders' emissive colour**: set to its luminance (x 1) in the copy NIF, since a grey glow map under an orange
  emissive colour still glows orange; the same for a lighting shader with the SLSF1 `Own_Emit` flag (bit 22), which adds
  its emissive colour without a glow map. Other lighting shaders keep their emissive colour. A
  `BSLightingShaderPropertyColorController` that animates the emissive colour is not greyed (known limit).
- **Recolour** (DirectXTex, MIT, linked into the builder only): decode (BCn to 8-bit RGBA in the source's colour space, so
  sRGB stays sRGB), each pixel set to its Rec. 709 luminance (the builder's own `Luminance`) x the role's tint, alpha kept,
  encoded again in the **source format** (BC1/BC2/BC3/BC7 or the uncompressed format; BC7 with `TEX_COMPRESS_BC7_QUICK`)
  with the same size, mip count and array size. In key mode a BC7 target (BC7_UNORM, BC7_UNORM_SRGB) goes to the GPU
  first (DirectXTex's DirectCompute encoder, "GPU BC7 encode" below); its result is decoded and checked on every
  subresource, and anything over a limit, a refusal or a failure is encoded on the CPU from the same grey image. The other
  routes (`--from-game --out`, `--forms`) encode on the CPU only. Tints: diffuse = the run's `--tint` (default 0.6; lighting shaders are
  otherwise left alone, so the diffuse carries the darkening), glow `kTextureTintGlow` 1.0 (a brightness mask), effect
  source `kTextureTintEffect` 1.0 (the base colour already carries the tint). HDR (BC6H), float, typeless, planar,
  palettized and video formats are refused with a reason (a float texture is not clamped).
- **Where**: `textures\heartofmagic\blackflame\copies\_tint\<tint x 100>\<path after textures\>` for every tint (`_tint\100\`
  for 1, `_tint\60\` for the default diffuse tint), in `--out` (staged and committed with the rest). The fixed `_tint\`
  segment keeps a source path from landing on another source's copy (a texture under `textures\t60\` once did when tint 1
  had no folder). After a successful run the builder removes from `--out` every file under its own copy folders
  (`textures\heartofmagic\blackflame\copies\`, `meshes\heartofmagic\blackflame\copies\`) that this run did not write - an
  earlier run's copy of a source that left the load order, or of a texture now grey; nothing outside those folders, and
  nothing through a symbolic link or junction (W17, X13). One texture used at two tints gets two copies; each texture and tint is made once per run. The slot is
  rewritten as that path with `textures\` in front, as `kPalettePath` is. The source file is never written. A texture that
  is missing, unreadable or fails leaves its slot as it was; it never fails the NIF.
- **Paths**: the builder reads a slot with the DLL's own rule (`plugins/BlackFlame/src/DataPathRule.h`, included by both,
  not copied): lower case, `/` as `\`, leading and repeated separators folded, every leading `data\` and `textures\`
  dropped and one `textures\` put in front, a double-byte character of the system code page kept as it is; a drive, `:`,
  a `.` or `..` token (a `.` token is refused, not folded: `textures\.\fx.dds` is a known limit), a trailing separator or
  a character no file name has is refused. A double-byte character counts as one
  character of its path token, so a trail byte 0x5C is never taken for a separator (the separator after it is kept, a
  path ending in it is a file, it splits no `..` off its token). One table of cases runs in the self-test against that
  header (the double-byte cases only on a double-byte system code page). The same header's `LowerSlashes` (case and
  separators, a double-byte character kept) is the key rule of the builder's model paths (`NormalizeDataPath`;
  `MeshDataPath` is `Normalize` under `meshes\`) and of the DLL's grey map lookups, so model and texture keys follow
  one rule.
- **Output**: one `TEX OK` line per copy (texture and tint) and one `GREY|FAIL|MISS` line per texture path, the first
  time it is met (a texture that failed or is missing is not tried again at another tint), and at the end
  `textures  recoloured N, skipped greyscale G, failed F, not in the dump M; R of S slots now name a grey copy; glow shader
  emissive colours greyed E; copies X MiB`. `MISS` names the game's reason when the request lists the texture as left out
  of the dump.
- **Where the textures come from** (`--from-game`): the DLL's dump (`data\textures\...`, Copy request above). A request
  without a `textures` list (an older DLL, or a listing that failed) builds without texture copies and says so. `--forms`
  reads them from the `--in` folders.
- **The DLL side**: with `writeRequest` on, every NIF that read ok is scanned for inline texture strings
  (`DataPathRule::TexturePathsIn`: each `.dds`, any case, whose run of printable bytes before it is exactly as long as the 4
  length bytes in front say - no NIF parser, so every block type's slots are found), normalised with `textures\` as the
  root, opened for their size (`ProbeModel`) and planned in path order within `kMaxTextureBytes` (64 MiB each) and
  `kMaxTextureDumpBytes` (768 MiB in all; a texture that no longer fits is left out, smaller ones after it still go in).
  They are read and written only when the dump is (re)written. `request.json` lists them (`textures`: path, size, ok,
  error) with `textureCaps`; it is written after the dump, so a read failure is named there. The request fingerprint is
  unchanged (the players' freshness check shares it), so a texture-only change does not make the copies stale;
  `dump-manifest.json` carries its own `textureFingerprint` (path, size, planned) and `textureReadFailures`: a texture
  that opens but cannot be read is listed there; the next start with an unchanged dump reads only those again (a read
  failure may pass, e.g. a busy archive) and rewrites the manifest when one reads, without redoing the dump; a texture
  that cannot be written leaves the manifest as it was, so the next start dumps again. A manifest
  without a texture fingerprint - a dump made before T1 - is written again once. A listing that throws writes `error` as
  the texture fingerprint (never equal to a real one) and leaves `textures` out of the request.
- **Our palettes**: the two fixed palettes (`textures\heartofmagic\blackflame\gradblackflame.dds`, `...add.dds`) are
  planned first (`own` in the request's `textures`), read whole at every start with `writeRequest` on for their FNV,
  which goes into the texture fingerprint: replacing one, even with a file of the same size, dumps again.
- **Same-size replacements**: the texture fingerprint holds sizes only (no byte is read at a start), so a texture mod
  swapped for one of the same size is not dumped again. After changing texture mods, delete `dump-manifest.json` in the
  request folder, start the game to the main menu once and run the builder again.
- **Cost** (writeRequest on only; players' default `false` opens no texture): on the 662-NIF dump of 2026-10-16 the scan
  finds 411 distinct texture paths (216 under `textures\effects\`, 23 normal maps, the palette gradients too: the DLL takes
  every path, the builder picks the slots; a run over that dump used 90 of them). Opening them for their size at every
  start: about 80-100 ms (677 NIFs took 136 ms in the tester run). Reading and writing them when the dump is written: not
  measured; estimated 300-600 MB and 2-6 s (677 NIFs, 20 MB, read in 289 ms), once per load order change.
- **Not done** (fix list T10, T12): the dump does not put the textures of the NIFs the builder greys first, so under the
  total cap a needed texture can lose to an unneeded one (the cap is far above the 411-path estimate); copies a run no
  longer makes stay in `--out`, a failed NIF's texture copies are committed with the rest, and the dump keeps textures
  that are no longer named (none of these is read by anything).

Checked offline (2026-10-17): the builder self-test (the shared path table against the DLL's header - the DBCS case runs
only on a double-byte system code page; the texture scan cases; a coloured BC3, BC7 and B8G8R8A8 become grey with alpha,
format and 7 mips kept and the source unchanged; a grey BC1, a dark grey BC1 and a legacy A8L8 are skipped; black with
small orange specks is recoloured; a broken DDS, a BC5 and a float texture fail with a reason; the glow shader rule, its
emissive colour, per-tint copies, and the slots that must stay). Not checked: the DLL's texture dump in the game, and how
the grey copies look.

### Decal texture set copies (TXST): `GreyFormsTxst.h`, `FormCopiesFillTexture.cpp`

Fire Rune's glyph and the scorch a fire impact leaves are **decals**: texture sets (TXST) the projectile
(`BGSProjectileData::decalData`) and the impact (`BGSImpactData::decalTextureSet`, `decalTextureSet2`) name, drawn
apart from their NIFs. Measured 2026-10-17: Fire Rune's `MAG_RuneFireProjectile02` names `DecalRuneFire` (Skyrim.esm
06051D: TX00 and TX03 `Effects\RuneFire01.dds`, an orange DXT5), the fire impacts `DecalFlameBurn01` (02B95A); the copy
NIFs were grey, the decals stayed orange. The player's copies now point at copies of those texture sets; NPCs keep the
originals, and a texture set the player's and an NPC's forms share is never written (only the copies point at its copy).

- **Closure and request**: the closure takes a copied PROJ's decal set and a copied IPCT's two sets (edge `kEdgeDecal`).
  A texture set an explosion places (`impactPlacedObject`) is neither walked nor excluded for that (AB2: excluding it
  would take away its decal copy); the copy EXPL keeps the source's placed set (T0-decal checks it).
  A TXST form in `request.json` gives `textures` (TX00..TX07 for people, UTF-8, `""` for an empty slot), `texturesRaw`,
  `hasDecal` (the source has DODT), `data.flags` (DNAM) and `bounds`; the request says `"decalTextureSets": true`. Its
  TX00 and TX03 textures are dumped with the NIFs' (a path both name, once). The fingerprint's line of a TXST is its
  eight slots and whether it has DODT, so a change of either makes the copies stale.
- **Slots** (CommonLibSSE-NG `RE/B/BSTextureSet.h`, TX00..TX07): diffuse TX00 is recoloured at the run's `--tint` (a
  decal is drawn on a lit surface like a mesh diffuse), glow / detail TX03 at 1 (a brightness mask), both through the
  same texture step as the NIFs (the same chroma judgement, the same `copies\_tint\<pct>\` folders; RuneFire01 gets a
  `_tint\60\` and a `_tint\100\` copy). Normal, masks, environment and the rest keep the source's text. Not checked against
  the game: how the decal shader uses each slot.
- **Record** (in the copies ESL, the TXST group first): EDID `HoMBF_C_TXST_<id>`, OBND, each slot the source has (its grey
  copy when one was made, else the source's text, without `textures\`), a **DODT stub** (36 zero bytes) exactly when
  the source has DODT, DNAM flags; no DATA, no MODL. `decalData` is a pointer (`RE/B/BGSTextureSet.h` 0x0C0) the engine
  allocates for a record with DODT, which the DLL then fills; it never allocates one. Read back subrecord by subrecord
  (presence kept apart from content, a subrecord twice is a fault).
- **Text** (AB1): a slot's text never goes through a code page. The DLL sends each slot's own bytes (`texturesRaw`, hex
  of the record string) beside its text for people (`textures`); the builder writes those bytes unchanged into the copy
  record for every slot it does not recolour, the map keeps them (`sourceTexturesRaw`, hex; `sourceTextures` the text),
  and the DLL compares bytes with bytes (ASCII case and `/` folded, a double-byte character of the system code page
  kept). A double-byte path, or bytes that are no text in any code page, pass through as they are. A colour slot
  (TX00 / TX03) whose bytes are not plain ASCII is not recoloured (a `NOTE`; the risk check's `DECAL_NOT_JUDGED` WARN),
  since its dump file's name depends on a conversion of those bytes. Only a hand-made `--forms` request without
  `texturesRaw` converts its UTF-8 text to the code page (exactly: no best-fit character, read back to the same text;
  otherwise that texture set is left out with a warning). A texture set left out while reading, or one the DLL excluded
  (the request's `excluded`, type TXST), whose copy `--out` still holds refuses the whole build, like an older request
  over TXST copies: run again with `--retire-left-out` to retire it on purpose (its record goes: only for a new save),
  or build into an empty `--out`. `--forms` refuses a game (version 3) request: it carries the dump, the decal flag and
  the left-out forms only `--from-game` reads.
- **Fill (DLL)**: `boundData`, `flags` and `*decalData` (its colour's RGB set to its Rec. 709 luminance, alpha kept)
  from the live source; never `textures[]` or `textureFileIDs`, nor `SetTexturePath`: the copy keeps its record's
  paths. An IPCT copy's own `dData` colour is greyed the same way, and its two texture sets, like a PROJ copy's
  `decalData`, point at the TXST copies (the second pass, the compare and the refresh, in play too). A TXST copy is
  left out (the decal stays the source's, orange) when its DODT presence differs from the live source's, a live source
  slot is not the one it was built for, its slots are not the map's (the ESL and the map from different runs), or a
  grey texture does not open through the game's file system (opens only: whether it decodes is the builder's risk
  check, which decodes every copy it judges). The reasons are counted apart: an out-of-step copy makes freshness `stale`
  ("run the builder") even with a matching fingerprint; a grey texture that does not open makes it `texturesMissing`
  (its own corner notice: the output mod's textures missing or overwritten, or a loose-file cache such as CRDW out of
  date - the copy's line says "on disk under Data" when the file is there but the game's file system does not open it;
  both counts in the detail when both occur); the tester file and CheckCopies give the two counts. The grey decal
  textures are part of the copy model probe's file-index state, so a changed one probes again. A
  slot that changes after the start cannot be refilled (the record's string): CheckCopies reports it and the next start
  leaves the copy out.
- **Risk check**: `DECAL_COLOUR` (FAIL) when a TXST copy's TX00 or TX03 still names a coloured texture (the same
  `IsColouredTexture` judgement as `TEX_COLOUR`); UNREAD / MISSING as for the NIFs.
- **Old and new**: an old copies ESL and map with this DLL: no TXST entries, the decals stay the source's, the fingerprint
  differs (stale: "run the builder"), and the log says how many texture sets the closure has without a copy. A new
  output with an older DLL: one "bad entry" warning per TXST entry (T0-v2 FAIL lines), every other copy works and the
  decals stay the source's. An older builder refuses a new request (`type 'TXST' is not ...`) and a new map. This builder
  refuses a request from an older DLL when `--out` already holds TXST copies (building would retire their records),
  and builds without decals, saying so, otherwise. A source keeps its type with its id: one that comes back as another
  type, active or retired, is refused before anything is written (retired entries now keep their `type`).
- **Saves**: the copies ESL holds only active entries, so a saved reference to a TXST copy resolves only while its source
  is still copied; retiring a source only reserves its id (it is never given to another source or type), its record is
  gone. Whether decals are saved at all is not checked (`BSTempEffect` has save and load virtuals; the save format has a
  temp-effects section, UESP); uninstalling the copies, or a rebuild that drops a texture set, with saved decals is a
  known limit.
- **Known limits**: TX02 (environment mask / subsurface tint), TX06 (multilayer) and TX07 are not dumped or judged; the
  decal on screen is an eye check (self-test T0-decal judges the forms by data).

### Glow rules: `GreyGlow.h`

After `GreyEffects` the builder writes a full-grey copy's colours again from the source NIF's own values (the same block
numbers: the builder changes values only), so nothing is greyed twice:

- **G, glow classes** (each effect shader without the palette flag, by its shapes; the measures are the risk check's):
  G1, an additive soft shape (no texture, or texture detail under 0.06) whose source colour - base colour and keys x
  texture mean x vertex colour - is warm (S >= 0.35): RGB 0 for its base colour and every key, alpha and flags kept (in
  the source it was an orange haze; grey it floats a grey disc or ball over the black body, and additive cannot draw
  dark). When G1 would leave a NIF nothing drawn (on this load order `impactfrost01`, `impactfrostsmall01` and `mpsfrostmist01`), those
  shaders take G2 instead and the risk check lists them as HALO_KEPT (WARN). G2, additive with detail (flames, sparks):
  luminance x `tintAdd` (0.9), the silver edge. G3, additive soft neutral (white in the source): luminance x `--tint`
  (WARN NEUTRAL_HALO). G5, normal blend: luminance x `--tint`. G0, not drawn (hidden, no blending, a blank texture,
  colour 0): luminance x `--tint`. A texture that cannot be read makes its shape G2 (soft or not cannot be told) and is
  noted.
- **P, palettes** (`GreyPalette.h`, `--palette-mode derived`, the default). A palette shader is one with the palette colour
  flag on, with or without a source texture (Y1: Community Shaders' `Effect.hlsl` samples the palette whenever the flag is on;
  13 flag-only copy shapes on the scratch run - firestorm and rune `Glow:0`, Vulcan, `arcwallfx01 Cylinder10:0` - had kept
  their source's orange palette: PAL_FOREIGN 13). A flag-only shape's empty source texture slot is taken as one white
  texel (texture green and alpha 1: the palette's right-hand column) - an assumption about the engine's default texture,
  not checked in the shader code; the risk report's header says so and the in-game check below compares one such shape.
  It gets a palette derived from its
  own source palette, one file per source palette and blend under
  `textures\heartofmagic\blackflame\copies\palettes\{add,normal}\<path after textures\>` (8-bit RGBA, one mip, sRGB as the
  source). Additive (P2): the source palette with every texel at its luminance and its alpha exactly kept, so the shape
  keeps the outline and brightness the source palette gave it (our one-row `GradBlackFlameAdd.dds` lit palette-alpha
  shapes up whole - Firebolt's `Cylinder02:0`, Fire Rune's ground planes - or sank them into its dark band). Normal blend
  (P1): `GradBlackFlame.dds`'s colour resampled to the source palette's size with the source's alpha. The shader's base
  colour and keys keep their red as grey (r, r, r): `Effect.hlsl` takes the palette colour as `BaseColorScale` (the emissive
  multiple) x palette(texture green, base red x vertex red) - the base colour only picks the row - so a derived palette's
  rows are read as the source's were. `--tint-add` scales a derived additive palette's output through that emissive
  multiple (the shader's `baseColorScale` and every emissive-multiple controller value and key), so `--tint-add 0`
  really darkens it (Y2). The keys are found as the colour keys are (K): a float controller's own `NiFloatInterpolator`
  and its `NiFloatData`, every controlled block of a sequence that names the controller, and a controller's own
  `NiBlendFloatInterpolator` value (Z1, Z2). A key block also used by another variable (the alpha transparency, 5) or by
  another shader is left as it is, noted in the log and counted on the `glow` line ("left as shared"). The multiple
  scales a palette shape only: a shape without the flag shows base colour x texture x vertex colour (`Effect.hlsl` reads
  `BaseColorScale` in the palette branch alone), so the risk check's measures for plain shapes leave it out (Z5). The vanilla shader may differ (NifSkope's reading multiplies the base colour in): the risk
  check warns (PAL_MODEL) where the two readings differ more than 2x, and the in-game check is Fire Rune's ground planes
  and Firebolt's `Cylinder02:0` / `Wisps02:0` against the original. A source palette that cannot be read keeps `GradBlackFlameAdd.dds` /
  `GradBlackFlame.dds` and the risk check reports it (UNREAD, or MISSING when the game lacks it).
  `--palette-mode godskin` keeps the earlier rule: `GradBlackFlameAdd.dds` (the Godskin gradient) on every additive palette
  shader and `GradBlackFlame.dds` on every normal-blend one. One rule says which greyscale paths are ours, for the DLL and
  the builder alike (Z6, `DataPathRule::IsOurPalettePath`): the two fixed palettes and every file under
  `textures\heartofmagic\blackflame\copies\palettes\{add,normal}\`, after the shared path folding. The DLL counts such a
  path as ours when it is loaded (`IsAnyPalettePath`: the self-test's grey rule, the runtime recolour's "already ours");
  the builder's own check (`VerifyGrey`) accepts one of them that fits the shader's blend; the risk check fails any other
  palette left on a copy's palette shape, hidden shapes included (PAL_FOREIGN). The runtime recolour's terminal guard
  (Y9): a property that shows one of our palette paths with no texture behind it has its palette flags turned off and is
  not recoloured again (the shader would read a palette it does not have).
- **K, colour keys**: every key a shader's `BSEffectShaderPropertyColorController`s reach - the controller's own
  `NiPoint3Interpolator`, and every `NiControllerSequence` controlled block that names the controller (by reference, or
  for a block without one by node name, property type `BSEffectShaderProperty` and controller type) - gets the shader's
  colour rule; a controller's own `NiBlendPoint3Interpolator` value too. Before, only the controller's own interpolator was
  greyed, and keys played through a sequence kept their orange (Fire Rune's `Glow:0`).
- **V, vertex colours**: on a shape without the palette flag that has the `Vertex_Colors` flag (SLSF2 bit 5), every vertex
  colour goes to its luminance, alpha kept (`BSTriShape` vertex data and `NiTriShapeData`); a palette shape's are left (its
  vertex red selects the palette row), and so is geometry a palette shape shares.

The run ends with `glow  shaders: G1 RGB 0 N (N NIF(s) kept at G2: nothing else drawn), G2 x tintAdd N; shapes: G3 ...;
colour keys reached through controlled blocks greyed N; ...; vertex colours to luminance on N shape(s) (...); shaders shared
by a soft warm glow and another kind of shape (no G1) N; emissive-multiple controller blocks x tintAdd N, left as shared
with another variable or shader N`.

### Visual-risk check: `GreyRisk.h`

Each full-grey copy NIF and grey MPS is judged against its source shape by shape after it is written, by data alone
(rules and constants from the 2026-10-17 prototype, `glow-rule.md` section 4). The report goes to
`SKSE\Plugins\HeartOfMagic_BlackFlame_risk.txt` in the output (tab separated: SUMMARY, then FAIL, WARN, UNREAD, MISSING
lines with the NIF, block and shape name) and the builder prints `T0 risk: PASS fail=0 unread=0 (...)` or
`T0 risk: FAIL fail=<n> unread=<m> (see HeartOfMagic_BlackFlame_risk.txt)`. The gate is FAIL 0 and UNREAD 0; on a real
run it is reported, not fatal.

| Rule | Level | When |
|---|---|---|
| KEYS_LEFT | FAIL | a copy colour (base colour, any reached key) more saturated than `kRiskMaxKeySat` 0.05 |
| VC_LEFT | FAIL | a shape without the palette flag whose copy vertex colours' alpha-weighted mean is more saturated than 0.10 |
| PSYS_LEFT | FAIL | a copy `BSPSysSimpleColorModifier` or `NiColorData` key more saturated than 0.05 |
| GREY_HALO | FAIL | a soft (no texture, or texture detail under 0.06) additive shape whose source colour (base x keys x texture mean x vertex colour) is warm (S >= 0.35) and whose copy shows luminance x emissive multiple above 0.01 |
| TEX_COLOUR | FAIL | a coloured texture (20% of lit texels with S > 0.25, or a mean colour with S >= 0.30) the copy still names |
| DECAL_COLOUR | FAIL | a decal texture set copy (TXST) whose TX00 or TX03 still names a coloured texture (the same judgement) |
| PAL_SHEET | FAIL | a palette shape whose energy (additive: each side's palette x its own emissive multiple) or coverage (normal blend; alpha test: the samples passing the threshold, GEQUAL) grows over 4x while the copy covers at least half the texture |
| PAL_VANISH | FAIL | an additive palette shape that drew at least 0.01 whose energy falls under a quarter |
| EFFECT_EMPTY | FAIL | a NIF whose source draws a shape and whose copy draws none |
| PAL_BRIGHTER, NEUTRAL_HALO, PAL_FLAT | WARN | energy over 4x on a small area; a soft additive neutral glow still visible; a palette texture with one flat green |
| HALO_KEPT | WARN | a GREY_HALO in a NIF where the glow rules kept the soft warm glows visible because RGB 0 would have emptied it (Y7: the glow step says so) |
| PAL_FOREIGN | FAIL | Y1: a copy shape with the palette colour flag whose greyscale texture is not one of ours (it shows the source's palette) |
| PAL_MODEL | WARN | Y3: an additive palette shape whose energy ratio under the other reading of the shader (base colour multiplies the palette) differs more than 2x |
| EFFECT_DIM | WARN | Y5: a NIF whose drawn shapes give under a quarter of its source's light (G1's RGB 0 included) |
| KEYS_UNREACHED | WARN | Y6: a colour controller none of whose keys can be reached (nothing to grey or judge) |
| UNREAD | gate | a texture the rule needs is in no folder read (and the game did not say it is absent) or does not decode; one of our two fixed palettes is one line for the run with the number of shapes that needed it (still one UNREAD for the gate) |
| MISSING | INFO | the game itself does not have the texture (the request lists it as not opening): the source has the same hole |

Textures (mip 0, 8-bit RGBA) are read from the `--in` folders (`--from-game`: the game's dump), the output being written
and our own shipped ones (this run's copies first, Y14). Our two fixed palettes (`GradBlackFlame.dds`,
`GradBlackFlameAdd.dds`) are in the dump since this release (the DLL reads them through the game's file system and
hashes them at every start, so a same-size variant is dumped again); the dump's copy then wins over the builder's own
folders, which are read only for a request that does not list the palettes (an older DLL, said once at the start of the
run; before this a run from outside the tester package or the repository found neither and gave UNREAD per shape). The palette rules follow Community Shaders' `Effect.hlsl`
(2026-10-17; the vanilla shader may differ): colour = emissive multiple x palette(texture green, base red x vertex red),
palette alpha = palette(texture alpha, base alpha x vertex alpha); an empty source texture slot is taken as one white
texel (assumed). Each side is read from its own NIF values (Y2: the copy's row and emissive multiple, not the run's
settings). A shape is drawn when it is shown, blended, its texture not blank, its colour not black and its alpha
(alpha-transparency controller keys included) x emissive multiple above 0.
Measured on the 2026-10-16 load order (the dump plus the prototype's textures, `--no-textures`), the v2i copies gave
FAIL 823 and WARN 225, every count equal to the prototype's; with K and V, KEYS_LEFT 70 -> 0 and VC_LEFT 103 -> 0; with
G, GREY_HALO 90 -> 0 (HALO_KEPT 2); with the derived palettes, PAL_SHEET 423 -> 0, PAL_VANISH 78 -> 0 and PAL_BRIGHTER 132 -> 0
(138 P2 and 59 P1 files; 2 shaders keep today's palette: `gradpurple05_blur.dds` is not in the load order). With the
texture step on as well, the whole run gives `T0 risk: PASS fail=0 unread=0` (WARN 95: NEUTRAL_HALO 56, PAL_FLAT 37,
HALO_KEPT 2; MISSING 2). The texture step never leaves as grey a texture this check calls coloured (`IsColouredTexture`),
and a mean colour's saturation needs a channel of 0.02 (the design's S(c); the prototype's statistics had no floor).
Alpha-test-only shapes (no blending) draw an opaque cut-out: the colour rules and the coverage rule apply, the coverage
counted as the samples whose alpha passes the property's threshold (Y4), and the energy rules do not. After Y1-Y7 the
scratch run gives PAL_FOREIGN 13 -> 0 and `T0 risk: PASS fail=0 unread=0` (WARN 122: PAL_FLAT 37, NEUTRAL_HALO 33,
EFFECT_DIM 30, PAL_MODEL 18, HALO_KEPT 2, KEYS_UNREACHED 2; MISSING 2). After Z (the glow classes and the risk check on
one measure, Z3; plain shapes without the emissive multiple, Z5): WARN 125 (PAL_FLAT 37, NEUTRAL_HALO 33, EFFECT_DIM 32,
PAL_MODEL 18, HALO_KEPT 3 - `mpsfrostmist01`'s `PCloud07`, which the glow rules already kept at G2, is now measured warm
by the check too -, KEYS_UNREACHED 2; MISSING 2), PAL_FOREIGN 0; 813 emissive-multiple key blocks scaled, none shared. Source and copy shapes are paired by block and name (BLOCK_LAYOUT otherwise), and the copy side is judged with
the texture the copy names.

### Freshness and the copies map v2 (v2 expansion C3, DLL): `CopyFreshness.h`

The DLL reads copies map version 2 (and version 1, which counts as stale). A PROJ copy's muzzle flash model is the map's
`greyMuzzleModel` when the builder made one, else the source's own: the fill, the compare and the refresh all hold the
copy to that same value (otherwise every load would put the source's back and T0-v2 would fail). At kDataLoaded, after
the copy request, the copies are judged:

| State | When |
|---|---|
| `none` | no copies map, or one with no entry (only the empty ESLs the mod ships) |
| `pluginMissing` | the map was read but `HeartOfMagic_BlackFlame_Copies.esp` is not loaded, or at least half of its entries (and at least one) lost their copy form (the output mod placed before Black Flame, or not enabled) |
| `texturesMissing` | AB4: the fingerprint matches, but a decal texture set copy names a grey texture that does not open: "Black Flame: some grey decal textures of the copies do not load. Check that the copies output mod is enabled with its textures and nothing overwrites it; with a loose-file cache such as CRDW, rebuild the cache." |
| `notApplied` | Y11: the map was there but filling the copies stopped part way: every copy is left out this session (the log says why) |
| `stale` | a version 1 map, or the map's request fingerprint differs from this start's: the byte form when `writeRequest` computed it, else the size form (each NIF only opened for its size, no byte read; the request carries both forms); or a decal texture set copy was left out as out of step with the load order (section "Decal texture set copies"; it wins over texturesMissing, both counts in the detail); or the map was built from an incomplete dump (`dumpComplete` false) |
| `fresh` | otherwise |

The log line gives the state, why, how many closure forms have no copy and how many map entries lost their source. Once
per session, at the first save load or new game, the player sees a corner notice through the main-thread queue (no
queue: the log only): stale - "Black Flame: your fire spells changed. Run the Black Flame builder, then restart the
game."; none - "Black Flame: the grey copies are not built yet. Run the Black Flame builder, then restart the game.";
pluginMissing - "Black Flame: the generated copies are not enabled. Enable HeartOfMagic_BlackFlame_Copies.esp and put
the copies output mod after Black Flame."; notApplied - "Black Flame: the grey copies could not be applied this session
(see HeartOfMagic_BlackFlame.log); fire spells stay as they were." The notice counts as shown only once the main-thread queue took it, and none
is shown when kDataLoaded did not get as far as the check (the ESP's forms missing, the closure failing: the log says
why; fix F10). English only (as the rest of this prototype's HUD lines); no id goes on screen. A
player's fire form without a copy stays orange, logged once per form with the freshness state. Do not deploy a C2
builder output with a DLL older than C3 (it reads only map version 1).

### Fill (S1, DLL): `FormCopies.h`

At kDataLoaded only, before any hook reads them, each copy is filled from the LIVE winning source (so SkyPatcher and
other runtime edits made before then are included), data members only, never the form header, flags or the copy's
own model: PROJ `data` (0x80-0x108), `soundLevel`, `fullName`, `muzzleFlashModel` through `TESModel::SetModel`,
`boundData`; EXPL `data` (0x98-0xE8), the enchantment members, `imageSpaceModifying`, `fullName`, `boundData`; IPCT
`data`, both decal texture sets, both sounds, `hazard`, `dData` (its colour greyed); HAZD `data`, `imageSpaceModifying`,
`fullName`, `boundData`; ARTO `data` (the DNAM art type) and `boundData` - never its model component; TXST `boundData`,
`flags`, `*decalData` (section "Decal texture set copies"); a source art with alternate
textures (MODS) is refused and left out (`alternate textures not copied`; no fire art in the measured order has any). Data structs are copied byte for byte (their layouts are asserted by CommonLib). A second pass points
only the links that have a copy at it: PROJ `explosionType` and `decalData`, EXPL `impactPlacedObject` and
`spawnProjectile`, IPCT `hazard`, `decalTextureSet` and `decalTextureSet2`; every other link (light, sounds, image space,
enchantment, hazard spell, impact data sets) stays the source's. A copy whose source or copy form is missing, or whose types differ, or whose own model does not
open through the game's file system (v2 fix J5: the engine then keeps the original and its runtime fallback), is left
out of the map and logged. The fire set treats a copy as its source (`IsFireExplosion`, `IsFireHazard`). Right after the fill every copy
is compared with its source (a difference there is a fill fault, logged as a warning). At every save load and new game
(`RefreshCopies`, v2 fix C5) every copy is compared again; one that no longer matches its live source is logged field
by field (`data.radius: source 320 copy 300`; a link with a copy prints the copy it should hold and the source's own; a
form pointer is named from the game's form map, not by dereferencing it), then only what differs is written - each data
struct as one write with its links already pointing at copies, so a copy never points at an original in between - and
compared once more. Self-test T0-v2 lists what that refresh re-filled (INFO
`refresh:`) and then compares every copy with its live source again, field by field.

### Swap points (S2, DLL): `CopySwap.h`

Only while stoked, only for a launch that is the player's (shooter, blame actor or confirmed script tag, as
"Whose it is") and fire, and only where a copy exists. **Fire at Launch** (`IsFireLaunch`): `LaunchData.spell`
(+0x70) decides alone by the fire rule (so a non-fire spell sharing a copied PROJ is never swapped); with no spell,
a weapon source (+0x38) on the player's bound fire weapon list counts; an enchantment (+0x80) alone does not
(staffs, scrolls and enchantments are excluded by the kSpell rule). NPC launches keep their forms.

| Device | Config key | What |
|---|---|---|
| `Projectile::Launch` (SE 42928 / AE 44108) prologue detour | `copyLaunch` | `LaunchData.projectileBase` set to its copy for the call and restored after. A Flame or Beam stream started with the copy is a new stream (Launch reuses a stream only with the same base) |
| the same detour, after the call | `copyLaunchExplosion` | the new projectile's `GetProjectileRuntimeData().explosion` (Launch takes it from the MGEF when the area is above 0 - Fireball - else from the base) set to its copy |
| explosion creation (AE 43829; SE: off) prologue detour | `copyExplosionCreate` | on every AE runtime whose prologue the replay decoder takes (layout read on 1.7.104, every pointer checked elsewhere), and only when explosion copies exist; **off on SE** (fix F1: SE 42657 is an attempted AE->SE match, never derived from SE code - the hook list says so, and in testerMode the candidate's first 32 bytes go to the tester file for an offline check; Launch's post-write still gives MGEF explosions their copies on SE). The create data's base (+0x00) set to its copy when its source projectile (+0x20, a projectile reference by form type) has a copy as its base, or - no projectile, the self-area cast - when its caster (+0x38, an ActorMagicCaster by vtable) is the player's and its spell (+0x28, a SpellItem by form type) is fire. Put back by a scope guard however the original returns (fix F3). A base that is already a copy is counted apart, so the self-test tells which device made a Fireball's explosion grey |
| `GetImpactData` (SE 20408 / AE 20860) calls in `SpawnCollisionEffects` (43022 / 44213) and the explosion's impact function (42679 / 43851) | `copyImpacts` | inside a copy window the impact copy is returned for the impact the set would return (no IPDS copy, no hash map insert). The window opens on the impact call sites (section "Grey swap") when the projectile's base is a copy, and in `Explosion::Update` when the explosion's base is a copy. The round-10 model window (GetModel swap) stays for projectiles and explosions without a copy |
| SetCastingArt (AE 34150) call in AE 38784; SE: off | `copyCastingArt` | v2 expansion C4. On 1.7.104 it is the only E8 to SetCastingArt in the executable (no pointer to it either), inside a function (0xDA bytes in three pdata chunks) that reads `selectedSpells[slot]`, takes that spell's costliest effect (`GetAVEffect`) and its `castingArt`. The site is found on every AE runtime by scanning the caller's first 0x200 bytes for E8s to SetCastingArt: exactly one must be there, and (fix F4) the 7 bytes before it must be `48 8B 90 E8 00 00 00` (`mov rdx, [rax+0xE8]`, the castingArt read), else it is left alone with that note; only on 1.6.1170 / 1.7.104 may +0x91 stand in for a call another plugin rewrote. The thunk forwards rcx, rdx, r8, r9, checks the caster (ActorMagicCaster vtable) and the art (BGSArtObject vtable), then `PickCastingArtImpl`: the copy when the caster's actor is the player, its casting source a hand, the hand's selected spell fire, the art that spell's costliest effect's `castingArt` (read from FireSet's kDataLoaded cache, fix F5: no GetAVEffect on a job thread), while stoked. The first 40 player calls are logged. **Off on SE** (fix F1: 33369 / 37830 unverified matches) |
| hand art refresh: DetachCastingArt (AE 34152) then SetCastingArt (AE 34150), called by us; SE: off | `refreshCastingArt` | SetCastingArt only stores an art while none is attached: it returns at once when the caster's flag `kCastingArtAttached` (1 << 3, ActorMagicCaster.h) is set (1.6.1170 code read 2026-10-17). So the site above alone gives a hand its copy only when it readies a spell, and a hand readied before the buff kept the original for the whole buff (and one readied during it the copy after it). When the buff flag changes (RecomputeBuff), both hands become pending, and the main-thread queue (after the frame's job wait) gives each hand the art `PickCastingArtImpl` picks now, by the steps of 38784's own unready and ready branches: DetachCastingArt(caster, true) as the unready branch calls it, whatever the flags (with the art attached it releases the light and detaches the art; always it clears `castingArtData` and the art's graph, releases the enchantment controller and cancels and releases the clone task), then SetCastingArt(caster, art), which with no art attached stores it with `kStartCloneTask`. `ActorMagicCaster::Update` calls CheckAttachCastingArt (34185) first every update; it clones the new art and attaches it a few frames later (the hand is bare until then; the casting light is made again in the effect's colour and tinted on the next stoked tick when `light` is on). The first try is the tick after the change. Guards, per hand: the player's; weapon state drawn (being drawn: tried again, at most 50 unpaused ticks; sheathed or sheathing: left to the next ready); the hand's selected spell a fire spell; its `castingArt` that spell's costliest effect's art or a copy of it (any other art is left alone); caster state none (fix HB1: ready waits too, until a log shows which state an idle drawn hand is in - neither 38784, DeselectSpellImpl (34158) nor 34152 reads the caster's state, so the engine unreadies a hand in any state, but that does not show ready is safe here) - else, ready, charging or casting (a concentration spell held), tried again every tick for at most 600 unpaused ticks (about 60 s; a `gave up` line with caster state 3 means an idle drawn hand is in ready); the player without 3D: at most 50 ticks; past a limit the hand keeps its art until its next ready (logged); a paused game waits without counting; nothing pending after a load. Installed only on 1.6.1170 and 1.7.104 (34152 and 38784 were read on 1.6.1170; 1.7.104 is checked only by the install-time byte patterns that follow) with the site above hooked, `mov rcx, rbx` before its castingArt load (the caster is in rbx), the id present and exactly one E8 to 34152 in 38784's first 0x200 bytes preceded by `B2 01 48 8B CB` (`mov dl, 1; mov rcx, rbx`, the unready branch, the same caster register); nothing is written into the code. The refresh calls SetCastingArt itself, so when the +0x91 call is chained onto another plugin's redirect, that redirect does not see the refresh's call. Each refresh logs `hand art refresh (buff on|off): hand N, spell X, art A -> B, was attached yes/no, caster state S`, and a hand given up `gave up after N ticks: ...` (60 lines a session at most) |
| hazard creation (AE 43954; SE: off) prologue detour | `copyHazardCreate` | v2 expansion C4, on every AE runtime whose prologue the decoder takes (1.7.104: `48 8B C4` and pushes), four argument registers forwarded (1.7.104: one argument, no float or stack argument). The create data's base (+0x00, BGSHazard by vtable; +0x08 a cell by vtable when set) becomes its copy inside a copy window, or when SpawnHazardEffect's own start (fix F2: AE 36885, 1.7.104 0x666ed0, prologue `48 89 5C 24 18`, reached from SpawnHazardEffect::Start 36879 directly and from slot 0xF HandleQueuedStart through a je, both with rcx = the effect; it calls 43954 at 0x66700b) is on the stack for a fire effect the player cast while stoked and the base is that effect's MGEF associated hazard. 36885's prologue is detoured and opens a thread-local window naming the effect when it carries the SpawnHazardEffect vtable (the earlier window on 34360 was GuideEffect's path-trail spawner and never saw one). Put back by scope guards (fix F3). **Off on SE** (fix F1: 42786 unverified, no SE id of 36885) |
| PlaceAtMe proxy (`PapyrusWrap.h`) | `copyPlaced` | v2 expansion C4: when the calling script is settled as the player's fire right now (a confirmed tagged reference, or on the game thread an ActiveEffect the player cast), PlaceAtMe's `akFormToPlace` - a HAZD, EXPL or PROJ with a copy - becomes the copy for that call (the native's own argument; nothing else changes). A caller still pending on a VM thread places the original (logged once) |

The grey map's model swaps (ImpactSwap, HazardSwap) never touch a copy's own grey model (a copy's path is under
`copies\`, which the grey map does not list); a copy that kept its source's model under warn-and-continue still gets the
grey map's model - the intended fallback, so they are not told to skip copies.

**Detours.** The first 16 bytes are read at runtime. An `E9` there (another plugin's detour, e.g. IDRC hooks
Launch's prologue) or an `FF 25` absolute jump is chained onto: our thunk calls its target. Otherwise the leading
instructions are replayed in a gateway only when every one is a position-independent form the decoder knows (push,
REX push, `mov [rsp+disp8], r64`, `sub rsp, imm`, `mov rax/r11, rsp`) covering 5 bytes or more; anything else is
left alone and logged with its bytes. 1.7.104 code (readable): Launch starts `48 89 4C 24 08` (one 5-byte
instruction), explosion creation `40 53 55 56 57` (four pushes). The GetImpactData sites are found by scanning each
caller's function-table range for its one `E8`; only SpawnCollisionEffects has a fallback offset (+0xBC, AE, for a
call another plugin rewrote): the explosion function differs between 1.7.104 and 1.6.1170 (see the ids table).
Trampoline: 512 bytes (158 at most used). Counters per device are in `GetCopySwapCounters` and the self-test.

### Ids and how each was checked (v2)

Every id below is present in CommonLib's `tests/REL/versionlib-1-6-1170-0.bin` and `version-1-5-97-0.bin` (id
reader `iddb.js`, 2026-10-16) and is checked again in the running game's database before use. 1.6.1170's code is
encrypted on disk, so code facts come from the readable 1.7.104 exe; "1170 equal" means the function has the same
pdata size and the same UNWIND_INFO bytes in 1.6.1170 and 1.7.104 (strong evidence that the code matches, not
proof). No 1.5.97 exe is on disk: nothing inside an SE function is verified.

| What | SE / AE | 1170 / 1597 offset | Checked |
|---|---|---|---|
| `Projectile::Launch` | 42928 / 44108 | 0x7e46c0 / 0x74b170 | CommonLib `RELOCATION_ID(42928, 44108)`; 1.7.104 prologue `48 89 4C 24 08`; 1170 equal (size 0x7DA). The detour reads the bytes at runtime |
| explosion creation | 42657 / 43829 | 0x7cf6a0 / 0x737bf0 | 1.7.104: `(createData*)`, data[0] BGSExplosion (flags read at +0xDC), prologue `40 53 55 56 57`; 1170 equal (size 0x2FA). Create data layout from its callers 44216 / 43025 (+0x00 explosion, +0x08 cell, +0x20 projectile, +0x28 spell, +0x30 effect; 1170 equal, size 0x632) and 34413 / 33635 (+0x38 MagicCaster; 1170 equal, size 0x7AE); SE layout unverified (checked per pointer at runtime) |
| `BGSImpactDataSet` GetImpactData | 20408 / 20860 | 0x318020 / 0x2c3490 | 1.7.104: `(BGSImpactDataSet*, BGSMaterialType*) -> BGSImpactData*` (CRC32 of the key, impactMap walk); 1170 equal (size 0x80). Called through its E8 sites only |
| `Projectile::SpawnCollisionEffects` | 43022 / 44213 | 0x7ecd80 / 0x7531c0 | one E8 to 20860 at +0xBC (1.7.104); 1170 equal (size 0x7DA): the fallback offset is used only on AE and only when the site is an E8 into a trampoline stub |
| the explosion's impact function | 42679 / 43851 | 0x7d1c40 / 0x739ee0 | one E8 to 20860 at +0x7FC on 1.7.104, but **not** 1170 equal (size 0x135D vs 0x139D, other unwind codes): found by the scan only, no fallback |
| `VTABLE_ActorMagicCaster[0]` | 257613 / 205828 | 0x187b8b0 / 0x1637490 | CommonLib; compared with the create data's caster vtable |
| impact window call sites (round 10, now also the copy window) | 42943 / 44123, 43014 / 44205, 42638 / 43806 | present | 1170 equal: 44123 (0x4B1), 44205 (0x2AE), 43806 (0x49F) |
| hit shader slot (S3, unchanged hook) | vtable slot 0x0D of `ActiveEffectReferenceEffectController` (function SE 34049 / AE 34846) | - | research 2026-10-16: the slot's only function, 1170 equal |

### Spike limits and costs (v2)

- Copies are not purely visual: a script's OnHit `akProjectile` is the copy's FormID while stoked (a script comparing
  projectile ids sees another form); hazard count limits may count copy hazards apart from the originals'.
- A hit art made while stoked is saved with the copy's FormID (a ModelReferenceEffect saves its `artObject`): a save made
  then and loaded without the copies plugin drops or mis-reads that art. Not checked in game (an E3-style check could be
  added).
- Hit art: the ARTO spike returns the copy from `GetHitEffectArt` (0x0C) for the player's fire effects while stoked
  (`copyHitArt`); E6 settles in one run whether the engine takes it. Casting (hand) art takes its copy at SetCastingArt's
  call (C4), which the engine makes when a hand readies a spell; a hand already drawn when the buff turns on or off gets
  its art from the hand art refresh (`refreshCastingArt`, E5b, AE 1.6.1170 and 1.7.104), except while it charges or casts
  for more than about 60 s. Spawn-hazard
  archetype hazards take their copy only when the spawn-hazard effect's MGEF is itself a fire effect. Impacts
  of a copy hazard (`HAZD.impactDataSet`) and every impact data set stay the originals (no IPDS copies).
- The grey NIFs are built from the user's winning files; a replacer installed or updated later needs a new
  `--forms` run (a form whose copy is missing stays orange, logged once). The DLL fills copies at kDataLoaded only:
  a source changed later in the session is not followed (the self-test's T0-v2 compare shows it).
- Unverified: the whole spike in game (copies accepted by the engine, the stream holding, the rune across a reload,
  E3 needs the player's quicksave and quickload); 1.6.1170 code inside the functions (pdata/unwind equality only); every SE offset and
  layout; whether another plugin's detour of Launch's prologue (IDRC) chains as expected.
- The explosion creation detour reads its create data under an SEH guard: a caller with another layout (a pointer
  that is not a form at +0x00 or +0x20) trips the guard once and stops the creation swap decision for the session (the
  detour stays and calls the original; log
  `hardware exception` naming `copy explosion create`); `createLayoutRejected` counts checks that failed cleanly.
- Script-placed fire takes a copy only when the Launch detour already sees the player link: the shooter, a blame actor,
  or a confirmed script tag. A projectile launched while its source's tag is still pending (a wrapped call on a VM
  thread, judged by a later task) keeps the original form and stays orange (the copy cannot be swapped in afterwards).
- E3 is semi-manual without `devAutoE3`: it asks for a quicksave and a quickload (with `devAutoE3` the DLL saves
  `HoMBF_E3` and loads it itself, `DevAutomation.h`; which folder the engine's save and load use under MO2 is not
  checked by the DLL) and judges only a load that matches its
  marker (same session nonce, same cell, within a game day after the request, within 10 minutes). A quicksave made
  for E3 holds the test rune and buff: do not keep playing from it.
- A re-run of `--forms` with a hand-made ADDN request gives the local ADDN records ids from 0x800 in index order
  again (the request does not list the local records a DLL-written request would): replace the whole output mod,
  never layer two outputs (the same greymap and Local ESL names, different ids).

### Single launch: the copy contract (P0): `CopyContract.h`, `SlotCoSave.h`

Work in progress (branch feature/black-flame-single): grey copies built while the game runs, published at kDataLoaded
and at the main menu before the first load, into a fixed slot pool. P0 is the contract both sides build against
(contract version 5); nothing in game uses it yet. Header-only, C++17, no game types, included by the DLL and the
builder like `DataPathRule.h`:

| File | What |
|---|---|
| `src/CopyContract.h` | contract version; the slot pool (ESL local ids: PROJ 480 at 0x800, IPCT 480 at 0x9E0, EXPL 320 at 0xBC0, ARTO 224 at 0xD00, HAZD 192 at 0xDE0, TXST 96 at 0xEA0 - the first 72 with a DODT stub -, ADDN 208 at 0xF00 with AddOnNode index 40000 + place; 0xFD0-0xFFF reserved; every entry pinned by a static_assert), FNV-1a 64, the tint text (`TintText`, `QuantizedTint`: to_chars fixed 6 decimals, 0..2 for tint and tintAdd as the builder takes them, -0 as 0), file states (an unset one makes every key builder refuse), request.json v5 field names (with `textures[].state` - ok, notdumped, missing, unread - so no side reads the error text, and `addnMps[].index`, the source ADDN's index, both required) |
| `src/CopyContractKeys.h` | the key string of every model object (form NIF, PROJ muzzle, ADDN MPS, TXST, the builder's texture and palette copies) and the ADDN table part; key16 = the string's FNV-1a 64 as 16 lower-case hex digits. A not-dumped file keys as `notdumped:<size>` (content-free: the builder never reads it); a non-ASCII path as hex. The prefix's `out=` field is the builder's output identity (`kBuilderOutputVersion`: `<n>` for a release build, `<n>+dev<16 hex>` for a build of any configuration but Release or one whose builder sources differ from git HEAD; key format BFK4, where BFK3 had `exe=<the exe's fnv64>`). `VerifyKeyPrefix`: the builder rebuilds the DLL's `keyPrefix` from its compiled identity and refuses the run on a difference. The exact format is in the header and frozen by golden strings in the test table; the texture child key16 anchor (`kGoldenTextureKey16`) is asserted by that table (the builder's --self-test and cosave-check) and by cosave-check's txst paths test through the DLL's own `SlotTxstPaths::CopyKey16`, so a builder and DLL that key a texture copy differently fail offline |
| `src/CopyContractIdentity.h` | the builder's output identity in its exe (RCDATA 4101, the text's bytes, no NUL) and its read: `ReadIdentityResource` loads the exe as a data file (never run; kernel32 only) and takes the resource's bytes, `BuilderIdentityFault` refuses a malformed identity or another output version than the DLL's own. rc.exe includes it too (only the resource id's `#define` outside `RC_INVOKED`). The builder's `--self-test` reads its own exe through it; cosave-check runs the failure paths (no file, no PE image, an exe without the resource) and reads the builder built beside it |
| `src/CopyContractProgress.h` | the builder's `progress.jsonl` line: outcomes ok, fallback, failed (retryable), too_large and not_dumped (terminal); the writer and a parser that ignores a last line without `\n`, refuses any v but 1, a key given twice and any path that is not a copy path, and reports a bad line by number; a too_large line carries its memory cap as a `cap` field; the last line for a key wins; the DLL copies staging to Data and never moves a file; `ProgressValidPrefix` for the builder before it appends |
| `src/CopyContractTest.h`, `CopyContractTestPaths.h`, `CopyContractTestProgress.h` | the shared test table: `blackflame-cosave-check --self-test` and `blackflame-greybuild --self-test` both run it (the double-byte path cases with a fixed code page 932 rule, so they run on any system; a case the system cannot run is SKIP, not PASS) |
| `src/SlotCoSave.h` | co-save record BFSL v1 (unique id `HBFS`): epoch, ledger version, plugin table, 9 bytes per slot; one byte sequence per snapshot (names ASCII-lower-cased outside double-byte characters, whose trail bytes are kept as they are; table and slots sorted); a read cut short, out of limits or not in that form invalidates the whole record; Write validates before it opens the record |
| `src/DataPathRule.h` | `CopyPath` / `ParseCopyPath`: a copy lives at `meshes\|textures\heartofmagic\blackflame\copies\<key16>\copy.<ext>`, a derived palette at `textures\heartofmagic\blackflame\copies\<key16>\palette.dds` (`PaletteCopyPath`; `IsOurPalettePath` takes it, not a texture copy) (ASCII only; the source's own name appears only in its key string); the staging folder mirrors Data |

The files P1 (builder) and P2 (DLL) fill in exist as stubs with their CMake entries already in place, among them
`tools/blackflame-cosave-check/SkseFile.cpp` for P2's .skse file reader.

```
blackflame-cosave-check --self-test     (BFSL on byte buffers and the contract table; exit 0 = pass)
```

### Single launch: the builder (P1): `GreySlotPool.h`, `GreyKeyed.h`, `GreyProgress.h`, `GreyBudget.h`

Work in progress like P0 (built against contract version 4): the builder side of the single launch. Nothing in game
runs it until P2's DLL launches it.

```
blackflame-greybuild --write-slot-pool <file.esp>
blackflame-greybuild --collisions <request.json>
blackflame-greybuild --from-game [<request folder>] --staging <dir> [--job-memory <MiB>] [--commit-reserve <MiB>] [--parent-pid <pid>]
```

**Slot pool ESL** (`--write-slot-pool`, committed as `data/HeartOfMagic_BlackFlame_Slots.esp`): ESL, no masters, HEDR
1.70, one placeholder per slot of `CopyContract.h`'s pool table, FormIDs 0x800-0xFCF (next id 0xFD0), written through
the copies ESL's record writer (`GreyFormsEsl.cpp`) and the ADDN record writer (`GreyEsl.cpp`), groups in Skyrim.esm's
order TXST, PROJ, HAZD, EXPL, ADDN, IPCT, ARTO. Written beside the target, read back with both byte readers and compared
slot by slot, then moved into place. EditorIDs `HoMBF_S_<TYPE>_<local id>`. The builder self-test fails when the
committed file is not what the builder writes.

| Slots | Record |
|---|---|
| PROJ, HAZD, EXPL, IPCT | EDID, OBND 0 (not IPCT), DATA in Skyrim.esm's size with every value and form link 0; PROJ VNAM 0 |
| ARTO | EDID, OBND 0, DNAM art type 0 |
| TXST 0xEA0-0xEE7 / 0xEE8-0xEFF | EDID, OBND 0, no texture slot, the 36-byte DODT stub on the first 72 only, DNAM 0 |
| ADDN 0xF00-0xFCF | EDID, OBND as Skyrim.esm's MPS records, MODL `HeartOfMagic\BlackFlame\Slots\Unset.nif` (no file has it), DATA index 40000 + place, DNAM cap 0, flags 1 (on demand) |

No record but the ADDNs has a MODL: the DLL sets every slot's model (the source's, or the grey copy) and fills the
scalars, links, the DODT data and the ADDN cap and flags from the slot's source.

**`--collisions <request>`**: the request's ADDN records (`addons`, a `--request` file or the game's request.json) whose
index is in 40000-40207, our own (`"own": true`) left out, one `COLLIDE` line each with the slot it takes. Exit 0 none,
1 some, 2 the request does not read. It runs alone (no other mode's option).

**Key mode** (`--from-game ... --staging`): reads request.json **version 5** and the dump beside it, and builds every
object at the path its key gives - `DataPathRule::CopyPath(<root>, key16, <source>)`, `...\copies\<key16>\copy.<ext>` -
inside `--staging`, which mirrors Data. The builder never makes a key for an object the DLL keyed; it makes only its child
objects' keys (texture and palette copies): the contract's `TextureKeyString` / `PaletteKeyString` over the verified prefix
and the source file's own bytes. At the start it rebuilds the prefix from the options, its compiled output identity and the dumped
palettes (`GradBlackFlame.dds` / `GradBlackFlameAdd.dds`: hashed, else `notdumped:<size>` when the prefix says so, else
`missing`) and refuses the run (exit 1, nothing written) when `VerifyKeyPrefix` says it is not `options.keyPrefix`.
`--tint`, `--tint-add`, `--palette-mode`, `--no-textures`, `--out`, `--in` and the other run options are refused (they
would change an output without changing its key); `--job-memory`, `--commit-reserve` and `--parent-pid` exist only here.
Only files the request lists are read from the dump: a NIF with its fnv64 in `nifs[]` (built only when the dump's bytes
have that fnv64), a texture `textures[]` lists as dumped (`ok`). Any other file in the dump folder (a stale one the DLL
could not delete) counts as not there.

**Output identity and the golden test** (proposal (b), 2026-10-18). A key's prefix carries `out=<identity>` instead of
the exe's hash, so a builder rebuilt from the same committed sources keys every object as before, and a mod update that
does not change the builder's output rebuilds nothing. `tools/blackflame-greybuild/GreyIdentity.cmake` writes the
identity at build time (a custom target the builder depends on; it rewrites its two files, the builder's
`GreyIdentityText.h` and the RCDATA `GreyIdentity.rc`, only when the text changes, in a folder per configuration -
`build/tools/blackflame-greybuild-identity/<config>/` - so a Debug and a Release build at once each embed their own):
`<n>` = `kBuilderOutputVersion` when the build is Release and the output-affecting sources
(`tools/blackflame-greybuild/**` with the golden fixtures, `plugins/BlackFlame/src/CopyContract*.h`, `DataPathRule.h`,
and what sets the builder's compile options and libraries: `tools/CMakeLists.txt`, the root `CMakeLists.txt`,
`plugins/cmake/*.cmake`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`; only the folder pattern recurses)
are git HEAD's (`git --no-optional-locks status`, so a build never takes the index lock); else `<n>+dev<16 hex>` (any
configuration but Release, a modified, staged or untracked source, or git not there), the hex the first 16 digits of a
SHA-256 over the configuration name and those files (not FNV-1a: a CMake script has no unsigned 64-bit arithmetic; no
file matched is an error, not an empty digest). A dev build so gets keys of its own and never writes into a release's. The release identity is a promise
that the output did not change, kept by hand (bump `kBuilderOutputVersion` whenever any output byte or progress line
changes) and checked by the golden test in `--self-test` (`GreySelfTestGolden.cpp`). It runs as a POST_BUILD step of
`blackflame-greybuild` on every build of it (`GreySelfTestGate.cmake`: the whole `--self-test`, about 3 s, its output in
`build/tools/Release/blackflame-greybuild-selftest.log`, the summary and FAIL lines in the build output, its stderr too
when it fails, a clear line when the exe did not run at all); a failure fails the build and moves the exe to
`blackflame-greybuild.gate-failed.exe`, which still runs `--print-golden`; a passing gate removes a `.gate-failed.exe` an
earlier one left. The test runs the
key mode with the release identity and the CPU encoder forced (`GpuUse::kOff`, checked: no crash marker in its staging;
GPU bytes are not fixed, guard 5 checks the GPU by tolerance) on committed inputs (`tools/blackflame-greybuild/golden/input`,
about 180 KB, written once by `--make-golden-fixtures`): BC7, sRGB BC7, BC3, BC1, B8G8R8A8, a 16 x 4 BC7 down to 1 x 1,
an already grey BC1, a B8G8R8A8 texture one step to either side of `kGreyscaleChroma`, `kColouredShare`,
`kStatColouredFrac` and `kStatMeanSat`, texels exactly on `kStatColouredSat` (saturation 0.25: not coloured) and an
alpha-weighted mean channel exactly on `kStatMinChannel` (0.02: counted), texels at alpha x max 6501 and 6507 on either
side of the lit test (each checked to land on its side), a BC5, a palette shader NIF, glow shapes and a
glow map shader, an MPS with AddOnNode 9, a TXST, and an additive palette shader on a B8G8R8A8_UNORM_SRGB source palette
(the derived palette's sRGB branch; checked to come out sRGB). The sRGB palette and the texel-test fixtures were added
to version 1 before its release (2026-10-18 and -19), every row they do not reach unchanged. It compares every output file's size and fnv64, every progress
line's kind, key, outcome and deps (not its reason) and a digest of the output-affecting constants the headers expose
(hex-float text, `DIRECTX_TEX_VERSION`, the DirectXTex and nifly vcpkg port versions) with the table of that version in
`GreyGoldenTable.h`; a difference fails with "bump kBuilderOutputVersion in CopyContractKeys.h and regenerate the table
(--self-test --print-golden)". A released table is never edited: a new version adds one. A released version is pinned:
its row in `kReleasedPins` (`GreyGoldenTable.h`, apart from the tables, so a table pasted from `--print-golden` never
overwrites a pin; that option prints no pin) holds the fnv64 of its table's text, and a pinned version whose table is
missing or no longer hashes to its pin fails the self-test. `Make-TesterPackage.ps1` refuses a builder whose
`kBuilderOutputVersion` has no pin and prints the row to add. Procedure: run `--self-test`, add the row its `NOTE golden
table v<n> fnv64 <hex>` line gives (`{ <n>, "<hex>" },`), commit, build Release again, package. Version 1 is not pinned
yet: it is pinned before the first tester package with BFK4 is made. The builder turns FMA3 off in the CRT's maths functions
(`_set_FMA3_enable(0)`) at the start of every mode, so a shipped run gives the bytes the table holds. Version 1's table was
made on one machine and has not been confirmed on a second one yet; it is the build's gate all the same (the POST_BUILD
step above). Without `--golden-dir` the golden part is skipped (no developer's folder is compiled into the exe; a tester
package has no fixtures); the POST_BUILD step and `Make-TesterPackage.ps1` pass the repository's
`tools/blackflame-greybuild/golden`, so there a missing folder fails. `--identity` prints the exe's output identity; the
package script refuses a `+dev` one, one whose number is not `kBuilderOutputVersion`, and a builder whose `--self-test` has
a FAIL. Not in the digest: constants inside `.cpp`
files' unnamed namespaces (their changes show only as changed bytes where the fixture reaches them). Stated limit: a
forgotten bump whose output change the fixture does not reach gives stale copies, which the exe hash never did. The
golden gate checks CPU outputs only (the test forces the CPU encoder): a change to the GPU encode settings changes no
byte it sees, so it must bump `kBuilderOutputVersion` by hand (review round 1; the builder's code says the same). Stated
limit: the BFK3 generation is never pruned. After the switch to BFK4 every BFK3 key's staging files and progress lines stay
(`GreyProgress.cpp`'s compaction keeps the last line of every key, whatever its format, so those lines are parsed again,
and their staged files hashed again, at every compaction: part of the `fnv64 checks N ms` time), and so do the Data
copies published under BFK3 keys. Those Data copies stay in use until the slot's BFK4 generation is published: a slot's
last published generation is applied at kDataLoaded while its files and deps are intact (`ApplyLastPublished`, W1),
whatever its key; once the BFK4 generation of that slot is published they are unused disk space (about 250 MiB on the
measured load order, proposal section 4). Exception: a txst object whose ledger entry has no stored strings (a pre-B2
ledger) is made again from this session's keys (TXST slots below), which name BFK4 copies, so it is lost at the
BFK3 -> BFK4 switch and the slot keeps its source until its BFK4 generation is published.

Request v5 as this builder reads it (P2 writes it; every point below is a builder assumption):
- every v4 field, read as `--from-game --out` reads it (so the dev mode builds a v5 request too), but no `models`: v5
  has none (the contract names no such key, `CopyRequestV5.cpp` writes none); in key mode only, an ADDN in `addons[]`
  whose model is no mesh path keeps its index with a warning, and a null `forms[].data` value (a NaN or infinite float
  as nlohmann writes it) is warned and left out - the key mode reads neither, `--out` still refuses both;
- `contractVersion` (top level) = `CopyContract::kContractVersion`, else refused;
- `options {tint, tintAdd, paletteMode, keyPrefix}`: tints in 0..2 (QuantizedTint values);
- `forms[].key`: a form with a model is a `nif` object at that key; a form with a model and no key (the DLL keys no NIF
  it could not read and no form outside a usable slot) is warned, worded by the reason, and not built: its slot stays
  closed. `forms[].slot`: a TXST form's slot (a TXST slot, one form each, else refused); a TXST form carries no key;
- `muzzles[] {path, key}`: `nif` objects;
- `addnMps[] {slot, path, key, index}`: an `mps` object, and its slot (an ADDN slot's local id) bound to the original
  ADDN index `index` (required since contract 4: an entry without it refuses the request);
- `textures[].state` (required): `ok` (in the dump), `notdumped` (over a dump cap: the object is `not_dumped`), `missing`
  (does not open: a slot left, fallback), `unread` (the game opened it and its read failed: the object is `failed`, so the
  next start, which reads it again under a new key, builds it); the error text is never read;
- `txst[] {slot, key, dodt}`: the TXST form whose `forms[].slot` is `slot` (a key on that form, if any, must be `key`);
  `slot` must be a TXST slot and `dodt` its DODT split.
- A NIF object `nifs[]` does not list (with its fnv64, or ok false; paths compared after `MeshDataPath`) is warned and that
  object alone gets a `failed` line, so one slip of the DLL does not block every build.
- Refused as the DLL's mistakes (exit 2): a txst slot no TXST form has, a TXST form key that is not its entry's, two
  TXST forms on one slot, one key for two objects, an ADDN index or slot bound twice or in no `addons[]` record, an `ok`
  `nifs[]` entry whose fnv64 does not parse.

Checked offline (2026-10-18, after in-game run 2): run 2's builder refused the request the DLL really wrote three times
in a row (`'models' must be an array`; then a form with a model and no key, 4 IPCT whose NIF is missing from the load
order; then `no TXST form has the key`, the DLL writing only `slot` on TXST forms) - the self-test's hand-written v5
fixture had all three. The fixture now has the DLL's shape (slots on every form, a mixed-case model path, a TXST with
`model ""`, `texturesRaw` and `data.flags`, a keyless IPCT whose NIF is ok false, an IPCT with no model, an `excluded`
entry with no source) and every rule above has a test (builder self-test 381 pass, cosave-check 222). The real run 2 request, built by a
throwaway builder carrying the old exe's fnv64 (so the keys match), ran end to end: 495 objects, 485 ok, 10 fallback,
0 failed, 259 s; a checker compiled against `CopyContractProgress.h` read all 775 progress lines and matched every
staged file and dep (size and fnv64), and every key the request asked for has a line.

Objects and their lines (`progress.jsonl`, the contract's writer only):

| Kind | Object | Line |
|---|---|---|
| `nif` | a form's model or a PROJ muzzle: the full-grey copy (GreyEffects, the glow rules, texture and palette copies), AddOnNode values on the slot indices | path, bytes, fnv64; deps: each texture / palette copy it names (path, fnv64) and `{"addn": n}` for every slot index the remap gave it (from the source's AddOnNode values the map binds) |
| `mps` | an ADDN slot's grey master particle system, the same build | the same (`{"addn"}` deps too, since contract 4) |
| `txst` | a TXST's TX00 (diffuse, `tint`) and TX03 (glow, tint 1) copies | no path; deps: those copies. A TXST with no colour slot copied (both left or empty) is `fallback` with no dep, never ok (contract 3) |
| `dds` | a texture copy (role diffuse / glow / effect, `copies\<key16>\copy.dds`) or a derived palette (add / normal, `copies\<key16>\palette.dds` from `PaletteCopyPath`), made once per run and named by every object that uses it | path, bytes, fnv64 |

An AddOnNode value goes to `40000 + place` when an `addnMps` entry binds its original index; a value bound to no slot
stays (fallback), and an index of the request inside the pool's own range is never bound (warned). A source value
that already lies in 40000-40207 is left as it is and warned; it gives no `{"addn"}` dep (a source with 9 and 40012 and 9
bound to 40012 still lists 40012, through 9).

| Outcome | When |
|---|---|
| `ok` | everything applied (an already grey texture at tint 1 left as it is counts as applied) |
| `fallback` | a deterministic slot left: a texture not in the dump (the game: does not open), a format that cannot be recoloured (BC5, HDR, float), a DDS that does not decode, a source palette or our palette that does not read (today's palette kept), a TXST slot that is not ASCII, an AddOnNode value bound to no slot |
| `failed` | retryable: an I/O error, out of memory (`bad_alloc`, `E_OUTOFMEMORY`), a locked, vanished or unreadable file (`IsTransientResult`: too many open files, a pending delete, a lost network share, a device or CRC error and a quota too)
on any read - the glow rules' and palettes' too -, a texture copy's write that fails for any reason but `E_INVALIDARG` or
not supported (a short write, a full disk, a locked target), over the cap with what the process holds, a 30-minute wait for commit, a source NIF not listed, not in the dump or that nifly cannot read, the dump's NIF bytes not the request's, a texture the request lists as dumped that is not in the dump, a texture whose `state` is `unread` |
| `too_large` | a texture whose estimate alone is over the job memory cap: a `too_large` dds line for it (the cap in its `cap` field), and every object naming it `too_large` (with `cap` too); also an object whose glow rules or palettes would read a texture larger than the cap |
| `not_dumped` | a texture the rule recolours that the request gives `state` `notdumped`: the object `not_dumped` |

A failure wins over a terminal state, a terminal state over a fallback. Failed and terminal objects leave no file. The
key's last line is the one that counts (compaction keeps it).

**progress.jsonl and staging** (`GreyProgress.h`): at the start (the DLL has read the file to its end before it launched
the builder) leftover `*.tmp` files are removed - in staging's two copies folders and `progress.jsonl.tmp` only - and the
file is compacted: per key its last line - dropped when it is `failed` (no older line of the key comes back), an ok or
fallback line only while its file is in staging with its size and fnv64; bad lines and a cut last line dropped; written to `progress.jsonl.tmp` and renamed over it (a POSIX-semantics rename, so a reader that
holds the file open with FILE_SHARE_DELETE does not block it; MoveFileExW when the file system has none). A progress
file that is there but does not read stops the run and is left as it is. Then it is opened for appending (shared read,
write and delete) and each finished object adds one line in one write, after its file was written as `<path>.tmp` and
renamed into place. Appended lines are not flushed one by one (a line lost to a crash only rebuilds its object: a kept line
is trusted only after the compaction's fnv64 check of its file); the compacted file is flushed before its rename. The
staging line of the log gives the time the compaction's fnv64 checks took (`fnv64 checks N ms`). Once an object has
failed, its remaining texture slots, palettes and reads are not tried. A write that fails (a full disk) latches the log and
stops the run (exit 1); a line the contract's writer refuses is written as a failed line instead and the run goes on. A reason's bytes outside ASCII
are written as `?`. An object is skipped (reused, nothing rewritten) when its key's last line is ok or fallback, its file
is in staging with the line's bytes and fnv64 and every path dep with its fnv64; or the line is terminal - `too_large`
only while its `cap` field is not below this run's (the cap is no part of the key, so a larger cap tries again). Staging
is the builder's cache: the DLL's copy worker must copy into Data, never move, or every start rebuilds.

**Memory** (`GreyBudget.h`): before a texture is recoloured its peak is estimated from the DDS header (the decoded RGBA
chain x 5: RecolourDds holds the source, the decoded chain, the risk check's copy of mip 0, the grey chain and the
encoded chain at once, plus, for a GPU result, one decoded subresource for its check); before the glow rules or a palette
read one, the read's own peak (the file and mip 0 decoded x 2).
Too large alone: `too_large` (the estimate alone: the GPU device never makes one). The system's available commit (commit
limit - commit total, not the job's own remainder) under `--commit-reserve` (default 4096 MiB) plus the estimate: the
texture cache is cleared once, then a GPU device the run holds is released, each followed by a new measure; still short,
the builder waits, a line a minute, at most 30 minutes. The process's committed bytes plus the estimate over `--job-memory`
(default 1536 MiB, the job object's cap the DLL sets): a GPU device the run holds is released with the texture cache, else
before a recolour the texture cache the glow rules keep is cleared once (never during the glow rules' reads, while a
texture of that cache is in use), and it is measured again; still over: `failed` (retryable). The cache is also cleared
between objects past 256 MiB. One worker builds one object at a time (the texture, palette and progress state is not
thread-safe). Inside an object OpenMP runs on every logical CPU: the builder links `OpenMP::OpenMP_CXX` itself (its own
`#pragma omp` loops; vcpkg's DirectXTex links it too), so it is built with `/openmp` and imports `vcomp140.dll`; `TEX_COMPRESS_PARALLEL` (the CPU encoder) and `ComputeStats`'s row loops use
it. The process runs at `BELOW_NORMAL_PRIORITY_CLASS`.

**GPU BC7 encode** (`GreyGpuEncoder.h`, proposal Step 1, 2026-10-18; DirectXTex's `dx11` feature, `d3d11` and `dxgi`
from the Windows SDK, the builder only). In key mode a BC7 target is encoded by DirectXTex's DirectCompute encoder
(`DEFAULT | BC7_QUICK`, alpha weight 1.0; the CPU overload's float in the same place is BC1's threshold 0.5) when:
- an adapter: `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` (else `EnumAdapters1`; each list ends at its
  first failed call), the first that is not a software adapter, logged once at the start (`GPU   adapter <description> (vendor, device, driver <version>), LUID,
  dedicated memory`); none: the CPU;
- a device: `D3D11CreateDevice` on that adapter (`D3D_DRIVER_TYPE_UNKNOWN`, feature level 11_0), made at the first BC7
  texture only while the job memory cap minus the process's committed bytes is at least 448 MiB (`kGpuDeviceShareBytes`:
  the GPU path's peak addition on the real request, 979 MiB against 563 MiB forced to the CPU, one exe) plus that
  texture's encoded chain; `SetGPUThreadPriority(-7)` (whether WDDM honours it is not known). After each GPU encode the
  context is cleared and flushed (DirectXTex's buffers of that call are freed before the output check). It is released,
  with its context, at the first failure (the rest of the run is CPU), before an object would fail over the cap or wait
  for commit (made again once a run when there is room, never a third time), and for the rest of the run on the throttle
  event;
- the crash guard allows it: `staging\gpu.inflight` (`<strikes>` and the adapter) exists only while device creation or a
  GPU encode runs; a start that finds it keeps the GPU and carries the strike, a marker of strike 2 (two starts in a row
  ended inside a GPU call) writes `staging\gpu.disabled` (adapter with its driver version, output identity) and from then
  BC7 is encoded on the CPU; the marker stays until that write succeeds (a failed write leaves both strikes for the next
  start). A marker naming another adapter or driver is dropped with its strikes; one that does not read is one strike and
  stays. `gpu.disabled` is cleared, with any marker, only at a start whose adapter, driver or output identity differs, and
  only when it is whole (both lines there and non-empty, the output a valid identity); one that does not read, or is
  malformed, keeps the GPU off and is never deleted, and a stale one that cannot be removed keeps the GPU off for that run.
  Both files are written to `<file>.tmp`, flushed and renamed over the file (`MOVEFILE_REPLACE_EXISTING |
  MOVEFILE_WRITE_THROUGH`), so a failed write never leaves a partial marker. The output check also refuses a result in
  another format than the target. A
  CPU run (`BLACKFLAME_GREYBUILD_GPU=off`, or the golden test) neither reads nor writes either file. The DLL never walks
  staging (it copies what progress lines name), so the two files reach nothing else.
- Output check (`CheckGpuOutput`): the result is decoded one subresource at a time and compared with the grey image:
  every subresource at 24 dB PSNR or more (all four channels) and no texel's alpha off by more than 96 steps. The real run-2
  request's 52 BC7 textures gave 30.1 dB at worst (mostly on small mips; mip 0 at 37.6 or more) and 50 steps of alpha on
  this machine (RTX 3070 Laptop, driver 32.0.16.1664). Over a limit: that texture is encoded on the CPU and a `GPU   CPU`
  line says why; a passed one gets a `GPU   BC7` line with its worst subresource. A small local defect under the limits is
  not caught. The run ends with a `gpu    ...` line (textures on the GPU, refused by the check, devices made, memory
  releases). GPU bytes are not the CPU's and can change with the driver. Stated limit (proposal 3.1), same key, other
  bytes: any rebuild of a key whose Data file already exists can hit it, whenever the rebuild's GPU bytes differ from the
  file in Data. A key is rebuilt when its staging file is gone, its last line is failed or transient (not reusable), a
  damaged progress.jsonl lost its line, or its staged copy (or a dep's) changed (`ProgressLog::Reusable`). The DLL keeps
  the Data file when a published object of the ledger stands on it (its own file or a dep, with the Data file's fnv64:
  `SlotLedgerModel::PublishedHolds`; a copy path names its key, so that file is a build of the same key): the new file is
  not deferred, the line counts as in Data for the objects that need it, `BuildProgress::Find` names the file by the Data
  file's size and fnv64 (so what is verified and recorded in the ledger is what is in Data), and the log says
  "'<path>': kept the published copy (another build of the same key; ...)" once a session per path (review round 1 D1:
  `BuildProgress.cpp` `KeepPublished`). Only a file that is there and that no ledger object stands on (a damaged or cut
  one, or a build copied in but never published) still goes the old way: launch N defers the new file as `.next`
  (a slot that already had a published generation keeps it, one that had none stays on its source); launch N+1:
  `ApplyDeferred` renames it in over the file there and marks it replaced this session, so `PlanObject` refuses it and
  `ApplyLastPublished` finds other bytes for any generation on the old file: those slots show their source colour for
  that session; launch N+2: published. A `.next` an earlier build of the DLL deferred before this fix is still renamed in
  once that way. The other way out (let the builder take the Data file's bytes for a key that has one) is not built.

**Throttle** (`plugins/BlackFlame/src/BuilderSignal.h`): with `--parent-pid <pid>` the builder opens the named,
manual-reset event `Local\HoMBlackFlame_Throttle_<pid>` (SYNCHRONIZE only; not there: never throttled, one `THROTTLE`
line) and reads it between objects. Once the DLL sets it (when the publish window closes: `BuilderLauncher.h`
`ThrottleAfterWindow`, which also puts the job at idle priority and a CPU hard cap of one logical CPU), the GPU device is
released for the rest of the run and OpenMP drops to one thread (`omp_set_num_threads(1)`), logged once.

**Order**: objects whose NIF names no texture the rule recolours (that the dump has) first, MPS before the rest, then by
the texture estimate. The visual-risk check does not run in key mode (it is the dev mode's report).

**`--parent-pid <pid>`**: a thread waits on that process; when it ends the run stops after the object in hand (exit 3)
and is terminated 30 s later if it is still going. A pid that names no process stops the run at once (exit 3); one that
does not open for another reason is exit 1. Known limit: a pid Windows gave to another process before the builder opened
it is watched instead (the job object's kill-on-close is the first guard). The same pid names the throttle event
("Throttle" above).

Exit codes: 0 every object got a line (whatever its outcome), 1 the run could not go on (staging, the key prefix, a line
not written), 2 bad arguments or a request that is not version 5, 3 the parent is gone.

Checked offline: the builder self-test - the pool ESL (counts per type, the DODT split, ADDN 0xF00 = 40000 .. 0xFCF =
40207 with flags 1, the group order, a damaged and a cut copy refused, the committed file the same bytes), `--collisions`,
the progress file (compaction, append, reuse, a changed file not reused, temp files only in our folders), the memory rule
(waits, cache clears, too large, the estimate of a 64 x 64 BC3), the parent watch, the object guard and outcome order,
`IsTransientResult`, `VerifyKeyPrefix` and the palettes' notdumped state, and key runs on made-up NIFs and DDS files
(outcomes, CopyPath files with their fnv64, deps, order, the AddOnNode remap, a second run reusing everything, a changed
copy rebuilt to the same bytes, a tiny cap, a missing palette and a BC5 decal, a texture outside the dump caps, refused
requests and prefixes, the CLI refusals, and `--from-game --out` on the same v5 request); the GPU encoder's guards (the
crash marker: fresh, one marker, two starts in a row, another driver or output identity, a CPU run leaving both files; the
output check: a good result, one texel's alpha off, a zeroed mip, alpha lost, another mip count; the device released
before an over-cap failure, none to release, too large untouched; the throttle event absent, unset, set; the marker writes cut part way, a failed rename, a leftover temp file, a malformed
or an undeletable stale `gpu.disabled`; on WARP the encoder's state and the wiring, `RecolourDds` reaching the encoder)
and guard 5 (with a hardware device: the BC7, sRGB BC7 and 16 x 4 BC7 fixtures on the GPU against the CPU's result -
format, mips, size, the check's limits; a hardware adapter that gives no device, or none at all, an INFO line); the
throttle's wiring in a key mode run (a real event for the self-test's pid, set before a run whose BC7 texture otherwise
gets a WARP device: the THROTTLE line, no device, OpenMP on one thread); the golden pins (a pinned version without its
table, or with another text, fails). Not checked: a request written by the game (P2), the job object's
own limit, a real pause for commit, a transient read failure, a driver that kills the process or resets (TDR), another
GPU, an iGPU-only machine, the throttle event set by the real DLL.


### Single launch: the DLL side (P2): `SingleLaunch.h`

Config `singleLaunch` (off by default: the slot ESL and the builder's key mode come from P1, and nothing of this route
has run in game yet). With it on, the copies map route (Copies.esp, `LoadAndFillCopies`, the freshness check, the copy
model probe, the v4 request) does not run; with it off nothing below runs except the menu sink.

**kDataLoaded, in order** (Main.cpp, `SingleLaunch.cpp`):

| Step | File | What |
|---|---|---|
| menu watch | `MenuWatch.h` | the `MenuOpenCloseEvent` sink, always registered (DevAutomation reads the main menu from it). The main-thread proof (`MainThreadProof.h`): the drains may publish only when the main-thread queue is installed and its first drain ran on `RE::Main`'s thread, decided only by a caller on that drain's thread before any publish step (a caller on another thread, or before the drain stored its thread, leaves it pending; a pending proof re-queues the stepper); otherwise only kDataLoaded itself publishes (logged with both thread ids). kDataLoaded's own thread is not part of it: on 1.6.1170 under MO2 (2026-10-18) kDataLoaded came on another thread than `Main::threadID`, while every drain and menu event ran on Main's |
| file index | `FileIndex.h` (`FileIndexInit.cpp`, `FileIndexGuardCheck.cpp`) | lifted from the spike's FileProbeEngine: PathToID, IndexGuard, FindRecord, RegisterLoose, GlobalLocations. Used only when every structural check passed and a FindRecord of a shipped path found it; logs whether `usvfs_x64.dll` is loaded and the Data folder (`<exe folder>\Data`). Register: FindRecord first - a loose record is kept, an archive record is a name clash (logged, never registered). The checks also want a path nothing ships not found; the first sane GlobalLocations in `.data` is used (more than one is logged). The stream count offset must be 0x10 on 1.6.1170 (probe run 3); on another AE build 0x0C or 0x10 is taken and the first registration proves it. Any fault in a lookup or a registration, a new stream whose count reads 0, or a RegisterLoose that does not raise the count by exactly one turns the index off for the rest of the session (the stream is then leaked, never released under the index); without it the copies wait for the next start's startup index |
| ledger | `SlotLedger.h` (`SlotLedgerModel.h`, `SlotLedgerJson.h`, `SlotLedgerFiles.h`) | `HoMBF_slots.json` in the folder of `PrepareFileSavePath("HoMBF_slots.json")`'s path (the folder the game saves to: `__MO_Saves` for MO2's profile-local saves). The engine appends `.ess` (1.6.1170: `...\__MO_Saves\HoMBF_slots.json.ess`), so only the folder is taken (`SaveFolderFromPrepared`: the path must be absolute and its file name exactly the name or the name + `.ess`, normalised, no `..` left) and our own name joined to it; the folder is logged, 5 backups beside it (rolled once a session, and only from a main that reads; backup 1 copied beside it and renamed in), a mirror per save folder in `<SKSE log folder>\HeartOfMagic_BlackFlame\HoMBF_slots.<8 hex>.json` (the low 32 bits of the FNV-1a 64 of the save folder's path, lower-cased), merged only when its epoch is the save folder's own files' epoch (the single `HoMBF_slots.json` mirror of earlier builds is no longer read); every write flushed to disk and renamed write-through. All of them are read and merged into the newest, the others newest first (the union of reservations; an older copy of a binding of the same epoch loses to the newer one, logged as stale - a save's rebind survives its backups; one slot bound to two sources across epochs is a conflict, never published again). JSON fields that are there but damaged refuse the file (a backup is read instead); a published path must be a copy path. Main not readable: the BFSL of every `.skse` in the save folder is merged in too (a backup may lack the last session's reservations); no file at all: that union alone. The co-saves of the ledger's epoch are merged newest first, and a newer one's binding replaces an older ledger's (what that published goes with it). When a co-save cannot be read whole (or listed), no new slot is assigned that session; one over the 256 MiB cap is logged as such. Each object a slot published keeps its key, its key string (an FNV-1a 64 collision guard), path, size, fnv64 and deps; parse failures are counted per key (a second session's makes it terminal `parse`) |
| key inputs | `SlotKeys.h` | the closure's NIFs, PROJ muzzles and every ADDN model read through the engine's file system; the dump's texture plan, each planned texture read whole for its fnv64 (outside the caps: `notdumped:<size>`); the builder exe (`<DLL folder>\HeartOfMagic_BlackFlame\blackflame-greybuild.exe`): it must open, and its output identity (RCDATA, `CopyContractIdentity.h`, logged with the key inputs) must be of the DLL's `kBuilderOutputVersion`; it goes into the key prefix verbatim (missing, unreadable or another version: no key this session); the AddOnNode indices the closure's NIFs name, read from each NIF header's block table (`NifValueNodes.h`: a BSValueNode's value is the int32 five bytes before its block's end; a NIF without the Bethesda header, user version 3+, is not parsed). A NIF that is there but could not be read gives no key; an ADDN MPS's own ADDN deps are followed transitively. TXST keys only with `slotTxst` |
| assign and fill | `SlotAssign.h`, `FormCopiesFillModel.h` | wants: every closure form of a copy type (a TXST by its DODT: the stubbed or unstubbed part of its pool) and every ADDN record `addonNodes` gives for a named index. Kept, re-activated or a never-used id; pool full and refused ones stay orange. Every ledger entry's pool form in `HeartOfMagic_BlackFlame_Slots.esp` is filled from its source (the per-type data fill; an ADDN its cap and sound, its DNAM flags left at 1) or, the source gone, from a vanilla template, and its model set to the source's. One gate per slot, closed; `CopyOf` hands a slot out only while its gate is open, so a closed slot's links go to the source. The copies table (`FormCopiesState.h`) holds the active usable slots, so every hook finds them as it found the copies-map copies. A slot ADDN whose index another record claims, or whose `addonNodes` entry is not the slot, is collided. The slot ESL must be loaded (its compile index); a form of the slot ESL itself never gets a slot |
| last published | `FormCopiesFillModel.h` `ApplyLastPublished` | W1: a slot's last published generation whose files and deps are still what the ledger recorded (copies in Data by size and fnv64, each Data file hashed once a session - `DataFiles.h` caches by path, size and write time; source files by the key inputs' hashes) is registered and set as its model; an ADDN slot's MPS first (and its source's DNAM flags, kAlwaysLoaded included). The ADDN slots an object names (its muzzle's too) must be open before any of its files is hashed or registered; an object without a file, off a copy path, or whose form takes no model is not used |
| request v5 | `CopyRequestV5.h` | the dump (the very bytes the keys hashed) and `request.json` v5: contractVersion, options (quantised tints, palette mode, key prefix), per form its key and slot, `muzzles`, `addnMps` (with the source ADDN's `index`), `txst` |
| progress | `BuildProgress.h` | `staging\progress.jsonl` read to its end before the builder starts (the builder compacts it only at its own start); what earlier sessions copied into Data counts now, but no file is copied on this thread (the worker does, after W1) |
| groups | `CopyGroups.h`, `CopyModelProbeVerify.h` | the connected components of the sources' copy links. Groups whose members all show a verified generation open now (W1). The rest, while the publish window is open and the proof holds: every new object registered, its NIF Demanded one at a time in 8 ms main-thread slices, and only when all parse SetModel (model, muzzle), the gates, the links (`Refill` pointers only) and the ledger's lastPublished. A member whose object failed, is terminal, has no key, or got no line (once the builder ended and every line is read and copied: `CaughtUp`), or whose files did not all reach Data by then, shows its older generation (or, without one, leaves: gated, links to the source) and never keeps its group waiting; TXST members (with `slotTxst`) are registered, not Demanded, and written before the gates open (off: they leave). Without the file index nothing that entered Data this session is a candidate (the next start's index has it). A NIF naming an ADDN slot index waits for that slot's MPS; an ADDN slot that will not open counts as ready with the model it has, a collided one makes the member leave; ADDN slots whose MPS only wait for each other are dropped after a full pass. An open ADDN slot takes a newer complete MPS of its key in W1/W2: it stays in the ADDN queue while its current key may still come (its NIFs use the older MPS meanwhile), and a pass over the queue goes on where the last slice stopped. An ADDN slot a NIF names that no source holds this session, or that cannot be published, makes the member leave (W1 and W2 alike); one not open yet only keeps the NIF on the model it has. A parse failure (an ADDN MPS's too) counts the key and leaves the group closed this session; a registration failure leaves it closed without counting; a Demand that faults stops publication for the session; there is no re-close. Registration and the re-plan run inside the 8 ms slice; ledger writes at most once a second while publishing, and once more when publication settles or the window closes; a step that throws stops publication with a log line. If the file index turns off during the session, an object that entered Data this session is not Demanded (the group stays closed, no parse failure counted). The kDataLoaded log line gives the Data files hashed, their size and the time. RefreshCopies and CheckCopies skip closed slots; `ExpectedMuzzleFor` gives a slot's grey muzzle only while it is open |
| builder | `BuilderLauncher.h` | `"<exe>" --from-game "<request folder>" --staging "<staging>" --job-memory <MiB> --parent-pid <pid>` (quoted the way `CommandLineToArgvW` reads it back): created suspended, below normal priority, only `builder.log` inherited, assigned to a job with kill-on-close and `JOB_OBJECT_LIMIT_JOB_MEMORY` (`builderJobMemoryMiB`), resumed; the job handle is held for the session and never closed by us. Before it: the request folder's named mutex `Local\HoMBlackFlame_Builder_<FNV-1a 64 of the normalised, lower-case folder path>` (`BuilderFolderMutex.h`) is made and held for the game's lifetime, at the slot route's start before the request, the dump or a deferred copy is touched (`BuilderLauncher::ClaimFolder`, also with `launchBuilder` off; review round 1 D2); one that is there already (another game process on the same folder) leaves `builder.log` alone and starts no builder, and this game writes no request or dump, renames no deferred copy and runs no copy worker (the start's read of progress.jsonl counts as caught up: `BuildProgress::SkipWorker`), one warning `the builder was not started: another game process holds this request folder ...; no request is written, no deferred copy is renamed in and no copy worker runs this session` (not made for another reason: a warning, the builder starts unexcluded). Then the last launch's `builder.log` is renamed to `builder.prev.log` (an older one replaced; not renamed: a warning, and it is overwritten) and the throttle event `Local\HoMBlackFlame_Throttle_<game pid>` (`BuilderSignal.h`, manual reset, unset) is made (not made: a warning, the builder starts without it). The process handle is kept for the session: when the builder ends (a thread-pool wait on it, so also in play), one line `builder (pid N) ended: exit code <code> (<meaning>)`: 0 ok, 1 failed (the run could not go on), 2 refused (bad arguments or not request v5), 3 stopped (the parent process is gone), anything else `0x........ (terminated or crashed)` (`BuilderControl.h`); the progress worker's "caught up" line carries it too |
| throttle | `BuilderLauncher.h` `ThrottleAfterWindow` | when the publish window closes (`MenuWatch::Close`, any of its reasons; a few job and event calls, no wait): the job's extended limits read and `JOB_OBJECT_LIMIT_PRIORITY_CLASS` with `IDLE_PRIORITY_CLASS` added (kill-on-close and the memory cap kept, read back), a CPU hard cap (`JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | HARD_CAP`, `CpuRate` 10000 / logical CPUs, at least 1: one logical CPU of the whole machine), and the throttle event set; one line `builder (pid N) throttled after the publish window closed: ...` with what was read back (a warning when a step failed, with the Windows error). What the builder finishes after that is used only at the next launch. A builder that already ended, or none this session: nothing (debug level, not in the log at the default info level) |
| copy worker | `BuildProgress.h`, `ProgressTail.h` | a thread tails progress.jsonl (shared read, write and delete; read again from its start when its file id changes - the builder's compaction by a POSIX rename - or it is shorter than the offset; a cut last line is read again whole, never fused with a later one; the last line of a key wins, a line that differs in any field starts its object over) and copies (never moves) each ok or fallback file staging -> Data through a temporary file and a replacing rename, fnv64 checked on both sides, only while the publish window is open (checked again right before the rename). A failed copy leaves no temporary file and fails every object that is that file or needs it (an older generation stands in). Another file already at a copy's path (a damaged or cut one: P1's builder gives one file a key) is never replaced while the game runs: the copy waits beside it as `<path>.next` (listed in `<SKSE log folder>\HeartOfMagic_BlackFlame\deferred-copies.txt`), the next start renames it in at kDataLoaded before anything uses the path, and it is used from the start after (the index may hold the old file's size). An object counts as in Data once its file and its copy deps are. `CaughtUp`: a pass that began with the builder ended has read the file to its end (or found none - an I/O failure is tried again at the next poll and never counts) and copied everything |

**The publish window** closes for the session on the first of kPreLoadGame, kNewGame, and (once the main menu opened)
the loading menu opening or the main menu closing; each publish step re-checks it on the main thread right before
SetModel and RegisterLoose. Unfinished groups stay as they were. The drains publish only once the main-thread proof is
made in the first of them; without the main-thread queue nothing steps after kDataLoaded and the progress counts as
settled at once, and a proof refused in the first drain stops the stepper there (settled); a pending one (`Main::threadID` still 0)
re-queues it. W1's steps (and its `RegisterLoose` calls) run on the kDataLoaded thread, which on 1.6.1170 under MO2 is
the loader thread, not Main's: they rely on the engine's IndexGuard lock, and T0-W2 judges only the drain steps (W1's are
counted, INFO). The dev auto-load waits
for "settled" (nothing more can open this session) or every slot open, and logs it when its 5-minute wait ran out first.

**The player is never asked to wait** (bf-no-wait-proposal.md section 5): the progress is in the log only, at the main
menu and when the count moves (10 s at most) `slots: progress: N of M grey copies ready (log only)`, and
`slots: progress: all M grey copies are ready (log only)` once; nothing on screen, and after a load with copies missing
nothing either (what was not open when the window closed opens at the next launch).

**HUD** (English), faults only, one of three lines (`BuilderControl.h` `BuilderNotice`): "Black Flame: the grey copy
builder was not found; only copies built before are shown" when the exe does not open; "Black Flame: the grey copy
builder could not be read; only copies built before are shown" when it does not load as a data file or its output
identity is malformed or does not read; "Black Flame: the grey copy builder is from another version; only copies built
before are shown" when its identity is of another output version or it has none (a builder from before BFK4: review
round 1 D3) (the details logged). When the slot
route does not start, one line at the main menu: "Black Flame: grey copies are off this session (<why>)", the why in
plain words (no ledger beside the saves, the slot ESL not enabled; the builder's absence added), logged too. Whether a
HUD line shows at the main menu is not checked in game.

**Saves.** Every save holds the ledger's pairs (BFSL, unique id `HBFS`). A loaded save whose pairs differ from what the
slots were filled from closes those slots' gates ("Black Flame: grey copies are off for some spells in this save until
you restart the game."), and its pairs win in the ledger (the current sources get new slots at the next start). Refs
that already resolved cannot be repaired: the slot route is for throwaway saves. With `slotPreflight` the save's co-save
is read from disk at kPreLoadGame and the differing slots' data are refilled from the save's sources before the engine
reads the save, their models left as they are (no SetModel at kPreLoadGame; unverified: the message's name format and
refilling at that point); a refill of another type is refused. Off, the preflight only logs what it found. Either way
absent and corrupt differ: no co-save, no HBFS block or a block without a BFSL record (a save made before the mod or the
slot route) is normal; a co-save that is there but cannot be read, a BFSL record that does not read (at kPreLoadGame or
in the load callback), or a preflight that throws closes every slot gate for the session ("this save's slot list could
not be read; grey is off for N spells"). A slot a save binds otherwise stays unusable for the session.

**TXST slots** (`slotTxst`, `SlotTxst.h`, `SlotTxstPaths.h`): new code - the copies-map route never wrote a texture set
at runtime (the builder baked the grey paths into its records), so this is the design's open point v2 §1, answered by
the log. A txst line's deps (no slot per dep) are matched to TX00 (role diffuse) and TX03 (role glow) by the key the
builder gives each copy (the contract's `TextureKeyString` over the prefix, the slot's normalised texture and its
fnv64); a colour slot no dep matches keeps the source's bytes, and a dep that matches neither, an ADDN dep, or no dep
at all (a fallback with both slots kept) publishes nothing for that slot. The write sets `textures[i].textureName`
(TESTexture, record strings without "textures\", `TESTexture::GetDefaultPath`) and `textureFileIDs[i]`
(`RE/B/BGSTextureSet.h` 0x040 / 0x0CC); `SetTexturePath` (vfunc 27) is not called. A copy slot's ID is made with
`BSResource::ID::GenerateFromPath` by the rule the engine's own IDs follow, found once a session on the TXST sources slot
by slot (`SlotTxstRule.h`, logged as "TXST textureFileIDs rule"): a slot whose stored ID is non-zero and equals the hash of
the name normalised under textures\ votes "prefixed", of the bare name "bare"; any other slot does not vote. Run 3
(2026-10-18) showed why: of 92 slots, 78 matched "textures\" + name; the old all-slots rule gave NONE and wrote no TXST.
The other 14 are, as run 3's counts read (its log has only counts; the rule line's `Tally::Text` names them by name at
the next run), 10 TX03 'black.dds' that no record has (3 of them seen holding an
earlier override's TX03 ID, 2 zero) and 4 PSI.esp TX05 'cubemap\shinydull_e.dds', a missing file left at zero. A slot both
forms match (a rooted name) does not vote. The rule is the form with votes when the other has none; with both or
neither, no TXST slot is written. The rule line groups the unexplained slots by name. An unchanged slot keeps the
source's ID when it is zero, the rule's ID of its string, or Normalize changes the string's shape (data\, a root, a
doubled or '/' separator); otherwise it takes the rule's (the stale ID names another file; the builder recoloured that
slot by its string); an empty slot takes zero; `Write` logs each ID not taken from the source. The DDS deps are
registered like every copy file (a DDS loads without it, probe run 3). A publish stores the eight strings it wrote and
the ID rule's name ("prefixed", "bare"; "zero" from an older build)
with the txst object in the ledger (`textures`, `idsRule`). W1 in a later session restores the set from them
(`SlotTxstPaths::RestoredStrings`), never from that session's key prefix, so a changed builder or tint, or no builder,
keeps an intact generation while this session's textureFileIDs rule is usable (`SlotTxst::Write` writes nothing without
it; the stored key string's prefix, BFK3 or BFK4, is never compared: cosave-check's txst restore test): the source's key fields now (its eight strings, TX00 / TX03 hashes, DODT;
`CopyContract::TxstSourceFields`) must end the stored key string, each changed colour slot must name one of the deps
(whose files `ObjectIntact` checks), every other slot must be the source's, and every dep must be named. The IDs are
made by this session's rule (a different stored rule is logged). A txst object whose ledger entry has no stored strings
(a ledger written before they were kept, pre-B2) makes them again from this session's keys, as before
(`SlotTxst::RestoredFor` -> `StringsFor`): its deps must be the copies this session's prefix names, so such a generation
is lost at the BFK3 -> BFK4 switch (and with a changed builder or tint, or no builder) and the slot keeps its source until
its BFK4 generation is published.
Whether the decal on screen takes the grey copies is the eye check (T0-decal and E3-slots judge the forms).
Staging cleanup is the builder's; no GC.

**Offline checks:** `blackflame-cosave-check --self-test` also runs the co-save reader on synthetic files, the ledger
(assignment, merge, JSON, the save compare, the files with backups and the mirror in a temporary folder, and one
regression test for each ledger finding of the P2 reviews: a rebind surviving its backups, the mirror's epoch, an
unreadable main not rolled, newest-first merging, non-ASCII conflict text, damaged fields refused), the progress tail
(`ProgressTail.h`: cut lines, a new file id, a shorter file), the BSValueNode reader, and the route start (the save folder from `PrepareFileSavePath`'s return with `.ess` appended or not, relative and empty refused; the main-thread proof's verdicts), and the builder launcher's pure parts (`BuilderControl.h`: the throttle's `CpuRate` for 1, 3, 4, 16, 64 logical CPUs, 0 and 20000 never 0; the exit code text for 0-3 and crash codes, the codes pinned to the builder's by `static_assert`; the throttle's hand-off; `BuilderFolderMutex.h`: the name's normalisation pinned, the decision, real named mutexes made twice); `--cosave <file.skse>...` walks real co-saves (the spike's two 1.6.1170 saves walk to their end, no
Black Flame block); `--nif-values <file.nif>...` prints the AddOnNode values (the sample request's 364 closure NIFs all
parse and name every one of the 97 indices the builder's run made grey ADDN records for, plus 17 others).

**Self-test** (`selfTest` and `selfTestSlots`, both off in the shipped config): T0-slots and T0-W2 (section "Self-test").

## Runtimes and Address Library ids

One DLL for **SE 1.5.97 and AE 1.6.x / 1.7.x**: the build sets CommonLibSSE-NG's `ENABLE_SKYRIM_SE` and
`ENABLE_SKYRIM_AE` on (plugins/cmake/commonlibsse.cmake), so every engine address comes from the running game's
Address Library and every member whose offset differs between SE and AE goes through CommonLib's runtime accessors (a
mixed build does not compile direct access to such a member). **VR is not supported** (`ENABLE_SKYRIM_VR` is off: no
VR build, no VR ids). Every id below is also checked in the running game's database before use (`Patch::HasIds`): a
missing id is a log line and that device stays off, never a failed lookup.

Table: every Address Library id Black Flame uses - its own and the CommonLib functions it reaches - looked up in every
database CommonLib ships in `tests/REL` (made with [tools/idaudit/table.js](tools/idaudit/table.js), which reads formats
1, 2 and 5; 1.7.99 is format 5). The SE column looks up the SE id, every AE column the AE id. **"yes" means the id exists
in that database, not that the code there is what we expect**: the hook sites are checked at runtime (bytes, call
targets, vtables) on every runtime. No 1.6.640 or 1.7.104 database is in `tests/REL`, so those two are not in the
table. The SE ids of the C4 items (SetCastingArt, its caller, hazard creation, the spawn-hazard effect) are the SE
counterparts the research notes name; no SE executable was read, so their SE function identity is unverified.

| What | SE id | AE id | 1.5.97 | 1.6.353 | 1.6.629 | 1.6.1130 | 1.6.1170 | 1.6.1179 | 1.7.99 |
|---|---|---|---|---|---|---|---|---|---|
| Projectile::Launch (copy swap detour) | 42928 | 44108 | yes | yes | yes | yes | yes | yes | yes |
| explosion creation (copy swap detour; SE id an unverified match, not used on SE) | 42657 | 43829 | yes | yes | yes | yes | yes | yes | yes |
| hazard creation (C4 detour; SE id an unverified match, not used on SE) | 42786 | 43954 | yes | yes | yes | yes | yes | yes | yes |
| BGSImpactDataSet::GetImpactData (callee) | 20408 | 20860 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::SpawnCollisionEffects (GetImpactData site, impact window) | 43022 | 44213 | yes | yes | yes | yes | yes | yes | yes |
| explosion impact function (GetImpactData site) | 42679 | 43851 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::ProcessImpacts (impact window site) | 42943 | 44123 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::HandleHits caller (impact window site) | 43014 | 44205 | yes | yes | yes | yes | yes | yes | yes |
| ConeProjectile::UpdateImpl (impact window site) | 42638 | 43806 | yes | yes | yes | yes | yes | yes | yes |
| Main::Update (main-thread queue call site) | 35565 | 36564 | yes | yes | yes | yes | yes | yes | yes |
| ActorMagicCaster::SetCastingArt (C4 callee; SE id unverified, not used on SE) | 33369 | 34150 | yes | yes | yes | yes | yes | yes | yes |
| SetCastingArt caller (C4 site; SE id unverified, not used on SE) | 37830 | 38784 | yes | yes | yes | yes | yes | yes | yes |
| ActorMagicCaster::DetachCastingArt (hand art refresh; no SE id, not used on SE) | - | 34152 | - | yes | yes | yes | yes | yes | yes |
| SpawnHazardEffect start, the spawn window (fix F2; no SE id) | - | 36885 | - | yes | yes | yes | yes | yes | yes |
| VTABLE_ActorMagicCaster[0] | 257613 | 205828 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Explosion[0] | 263870 | 209940 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Hazard[0] | 263922 | 209971 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSHazard[2] (TESModel) | 233489 | 189083 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSImpactData[1] (TESModel) | 236357 | 190905 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_ActiveEffectReferenceEffectController[0] | 257577 | 205809 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSExplosion[0] (explosion creation base check) | 233467 | 189059 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSArtObject[0] (casting art check) | 233406 | 189017 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BGSHazard[0] (hazard creation base check) | 233487 | 189079 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_TESObjectCELL[0] (hazard creation cell check) | 235197 | 190143 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_SpawnHazardEffect[0] (spawn-hazard window check) | 259802 | 207209 | yes | yes | yes | yes | yes | yes | yes |
| PlayerCharacter::GetSingleton (singleton ptr) (CommonLib) | 517014 | 403521 | yes | yes | yes | yes | yes | yes | yes |
| ProcessLists::GetSingleton (CommonLib) | 514167 | 400315 | yes | yes | yes | yes | yes | yes | yes |
| TESDataHandler::GetSingleton (CommonLib) | 514141 | 400269 | yes | yes | yes | yes | yes | yes | yes |
| ScriptEventSourceHolder::GetSingleton (fn) (CommonLib) | 14108 | 14298 | yes | yes | yes | yes | yes | yes | yes |
| UI::GetSingleton (CommonLib) | 514178 | 400327 | yes | yes | yes | yes | yes | yes | yes |
| TESForm::GetAllForms map (LookupForm->LookupByID) (CommonLib) | 514351 | 400507 | yes | yes | yes | yes | yes | yes | yes |
| TESForm::GetAllForms lock (CommonLib) | 514360 | 400517 | yes | yes | yes | yes | yes | yes | yes |
| BSReadWriteLock::LockForRead (CommonLib) | 66976 | 68233 | yes | yes | yes | yes | yes | yes | yes |
| BSReadWriteLock::UnlockForRead (CommonLib) | 66982 | 68239 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::GetSingleton (RE::free, BSTArray growth) (CommonLib) | 11045 | 11141 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::Allocate (CommonLib) | 66859 | 68115 | yes | yes | yes | yes | yes | yes | yes |
| MemoryManager::Deallocate (CommonLib) | 66861 | 68117 | yes | yes | yes | yes | yes | yes | yes |
| BSFixedString::ctor8 (CommonLib) | 67819 | 69161 | yes | yes | yes | yes | yes | yes | yes |
| BSStringPool::Entry::release8 (~BSFixedString) (CommonLib) | 67847 | 69192 | yes | yes | yes | yes | yes | yes | yes |
| BSPointerHandleManagerInterface::GetHandle (TESObjectREFR::GetHandle) (CommonLib) | 15967 | 16212 | yes | yes | yes | yes | yes | yes | yes |
| GetSmartPointer non-const (handle.get()) (CommonLib) | 12785 | 12922 | yes | yes | yes | yes | yes | yes | yes |
| GetSmartPointer const (const handle.get()) (CommonLib) | 12204 | 12332 | yes | yes | yes | yes | yes | yes | yes |
| TESObjectREFR::ApplyEffectShader (CommonLib) | 19446 | 19872 | yes | yes | yes | yes | yes | yes | yes |
| TESObjectREFR::SetPosition(NiPoint3) (CommonLib) | 19363 | 19790 | yes | yes | yes | yes | yes | yes | yes |
| TESDataHandler::CreateReferenceAtLocation (PlaceObjectAtMe) (CommonLib) | 13625 | 13723 | yes | yes | yes | yes | yes | yes | yes |
| Actor::SetHeading (CommonLib) | 36248 | 37230 | yes | yes | yes | yes | yes | yes | yes |
| ActiveEffect::Dispel (CommonLib) | 33286 | 34061 | yes | yes | yes | yes | yes | yes | yes |
| MagicItem::GetAVEffect (CommonLib) | 11194 | 11302 | yes | yes | yes | yes | yes | yes | yes |
| MagicItem::GetCostliestEffectItem (CommonLib) | 11216 | 11335 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderProperty::SetMaterial (CommonLib) | 98897 | 105544 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderProperty::SetFlags (CommonLib) | 98893 | 105540 | yes | yes | yes | yes | yes | yes | yes |
| BSShaderManager::GetTexture (CommonLib) | 98986 | 105640 | yes | yes | yes | yes | yes | yes | yes |
| Projectile::Launch (CommonLib) | 42928 | 44108 | yes | yes | yes | yes | yes | yes | yes |
| RTDynamicCast (skyrim_cast) (CommonLib) | 102238 | 109689 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_TESObjectREFR (CommonLib) | 513899 | 392437 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_Projectile (CommonLib) | 514046 | 394480 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_MagicCaster (CommonLib) | 513999 | 393096 | yes | yes | yes | yes | yes | yes | yes |
| RTTI_ActorMagicCaster (CommonLib) | 686479 | 394312 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSEffectShaderProperty (CommonLib) | 527834 | 414781 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSEffectShaderPropertyColorController (CommonLib) | 528413 | 415359 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_BSPSysSimpleColorModifier (CommonLib) | 528415 | 415361 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiGeometry (CommonLib) | 523978 | 410558 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiParticleSystem (CommonLib) | 524465 | 411046 | yes | yes | yes | yes | yes | yes | yes |
| NiRTTI_NiSourceTexture (CommonLib) | 523939 | 410519 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_MissileProjectile[0] (CommonLib) | 263942 | 209980 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_FlameProjectile[0] (CommonLib) | 263884 | 209953 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_ConeProjectile[0] (CommonLib) | 263822 | 209925 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BeamProjectile[0] (CommonLib) | 263808 | 209914 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_GrenadeProjectile[0] (CommonLib) | 263895 | 209962 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_BarrierProjectile[0] (CommonLib) | 263790 | 209901 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Explosion[0] (CommonLib) | 263870 | 209940 | yes | yes | yes | yes | yes | yes | yes |
| VTABLE_Projectile__LaunchData[0] (LaunchData ctor emplace_vtable) (CommonLib) | 230860 | 187478 | yes | yes | yes | yes | yes | yes | yes |
| MagicTarget::DispelEffect (CommonLib) | 33721 | 34505 | yes | yes | yes | yes | yes | yes | yes |
| BSResourceNiBinaryStream ctor (CommonLib) | 69636 | 71014 | yes | yes | yes | yes | yes | yes | yes |
| BSResourceNiBinaryStream dtor (CommonLib) | 69638 | 71016 | yes | yes | yes | yes | yes | yes | yes |


**Hook sites on every runtime** (2026-10-16): no in-function offset measured only on 1.6.1170 is trusted elsewhere.

| Site | How it is found and checked |
|---|---|
| main-thread queue | `Main::Update` (35565 / 36564) + 0x748 on SE, + 0xC26 on AE before 1.7.99, + 0xC38 from 1.7.99 (the offsets OAR, Precision and TDM use for every runtime); the site must be an E8 into the game image or a trampoline stub. The AE tail anchor (pdata end - site = 0x37) is enforced only on 1.6.1170 and 1.7.104, where it was measured; other AE builds log their distance. SE's SKSE task drain sits in the same function (+0x6B8), so the queue's reasoning holds there too |
| Launch, explosion creation, hazard creation, SpawnHazardEffect start | prologue detours on every runtime whose first bytes the replay decoder takes (else left alone, logged, and the self-test says INFO). Explosion creation, hazard creation and the spawn window run on AE only (fix F1: their SE ids are unverified matches); their thunks forward all four argument registers and return rax (verified on 1.7.104: the functions take one or two pointer arguments and no float or stack argument); every pointer is checked by vtable or form type; a fault trips the SEH guard once and stops that swap decision for the session (the detour stays and calls the original). The SetCastingArt site is AE only too |
| GetImpactData sites, impact window sites | scan of the caller's function-table range for its one E8 to the callee; a fixed offset only for a call another plugin already rewrote (an E8 into a trampoline stub), and only where the offset was confirmed: SE 1.5.97 (MuIF, KillFeed) and AE 1.6.1170 / 1.7.104 |
| vtable slots | CommonLib's vtable ids, checked in the database |

Our jumps never use a hand-written rel32 into the DLL: call redirects and the prologue jump go through the SKSE trampoline
(within 2 GB of the game), and the gateway returns with an absolute `FF 25` jump.

**Tester diagnostics.** `HeartOfMagic_BlackFlame_tester.txt` next to the log, written at kDataLoaded and again when the
self-test ends: the runtime, every hook INSTALLED or SKIPPED with its address (image offset), the bytes it found and why,
when and why it was written (kDataLoaded, every save load and save, the self-test's end; fix G5), the copies map and fill counts with any copy problem, the copy request, a counters line (Launch, explosion creation,
hazard creation and the spawn window, the SetCastingArt site, PlaceAtMe copies, the native wrappers bound or not; also
an INFO line of T9, fix F6), and the self-test's DONE lines. `testerMode` (config, default false) turns on `selfTest`
and `writeRequest` and writes every log line through at once (a crash keeps it), during and after the self-test (fix F9);
the self-test starts after `selfTestDelaySeconds` (30) of unpaused play.

**Tester package.** `plugins\BlackFlame\tools\Make-TesterPackage.ps1` (after `BuildRelease.ps1`) writes
`dist\HeartOfMagic-BlackFlame-Tester_<VERSION>-<commit>.zip`: the DLL and PDB, the ESP and the two empty ESLs, the shipped
vanilla-derived grey meshes and palette, the grey map, the config with `testerMode` true, the builder with
`Build Black Flame copies.bat` (no argument: `Documents\HeartOfMagic BlackFlame Copies`) under `tools\BlackFlame`, and
[tools/README_TESTER.md](tools/README_TESTER.md) (MO2 and Vortex steps, the files to send back, a throwaway save). The
script refuses to package generated copies (a `copies` mesh folder, a copies map, a non-empty copies or local ESL).
The package's `THIRD-PARTY-NOTICES.md` is its own (`tools/THIRD-PARTY-NOTICES-TESTER.md.in`, not the Librarian
release's): the DLL (CommonLibSSE-NG) and the builder (nifly) as GPL combined works, each one's libraries with the
version vcpkg installed, and the exact commit as the Corresponding Source. The licence texts are copied from the
CommonLib submodule and vcpkg's installed copyright files when packaging (and checked byte for byte). The commit counts as
published only when a branch or tag origin has now reaches it (`git fetch origin`, then `git ls-remote` and
`merge-base --is-ancestor`; local tags and other remotes do not count, offline counts as unpublished). An unpublished
commit is refused unless `-AllowUnpublished`, which puts `source\HeartOfMagic-<version>-source.zip` in the package:
`git archive` of the commit (with `-AllowDirty`, which needs it, the tracked files' uncommitted changes too) and of the
CommonLibSSE-NG submodule's commit at `plugins/external/commonlibsse-ng/`, in one zip; the notice then points into the
zip. The zip is written with .NET's ZipArchive, which marks entries as made on Windows: the submodule's few symbolic
links (documentation files) come out as small files holding their target (Y20). An untracked file under the DLL's or
the builder's sources is refused always. The DLL's library table has no xbyak
and no zlib: neither one's strings are in `HeartOfMagic_BlackFlame.dll` (both are in `SpellLearning.dll`, whose
release notice lists them).

## Known limits

- The copy model probe's cache key (W20) stamps CRDW.dll and every copy NIF, not CRDW's own index file (its path is not
  known here): a loose-file index rebuilt while the copies mod was off, with the copies unchanged since, reuses the cache
  and is not probed again; delete `HeartOfMagic_BlackFlame_probe.json` (or set `copyModelProbeAlways` once) after such a
  rebuild (X17).
- Non-ASCII model and texture paths (W10): the builder finds a dumped file whether the path came from a NIF (system code
  page bytes) or from `request.json` (UTF-8), and the DLL and the builder fold keys with one rule (`DataPathRule.h`),
  but the copies ESL records and the copies map carry a request path as the request has it (UTF-8); a model path with
  non-ASCII characters may then not load in a game whose code page is not UTF-8. Vanilla and nearly all mods use ASCII
  paths.

- Grey-black, not pitch black: the blending stays additive.
- Death flames (an actor burning to death) stay orange: hardcoded in the engine (Black Flame author's note).
- Impact data sets are unchanged; scorch decals and Fire Rune's glyph are grey through TXST copies (section "Decal texture set copies"; their look is an eye check). Of hazards (ground fire) the copy or the grey map's model is grey and the light is tinted (C6: no runtime recolour); vanilla flames that emit into a shared `BSMasterParticleSystem` (MPSFireWallBase and the like) stay orange, because recolouring them would turn every NPC's fire of that kind grey. Grey copies of the shared-particle impacts and hazards exist (section "Grey duplicates"), and the DLL swaps the player's to them while stoked (section "Grey swap").
- A hit shader or hit art made while stoked stays grey until it ends, also after the buff ends; one made before the buff stays as it was (expected: the engine asks when it makes it; the `GetHitEffectShader` lines show the call count per controller).
- Hit art that is a shared master particle system (`BSMasterParticleSystem`) is not recoloured.
- A fire effect whose MGEF names no hit shader gets none (the engine is asked, we only swap what it would show; the old layer applied grey anyway).
- A fire trap the player set off stays orange (the actor cause is not a rule, see "Whose it is"); so does a RemoteCast whose blame the engine does not keep on the caster (T3-blame reports it).
- Summoned creatures' own fire (a flame atronach's flames and spells) stays as it is: summons are not part of this (user decision).
- A catalog-fire spell without a fire effect gets nothing (see "Fire spells and fire effects").
- Explosions of a projectile shot by a script-tagged source are only the player's when the explosion itself carries the cause or blame; light effects cast on others than the player are not tinted.
- A reference placed initially disabled by a script has no 3D within the 5 s tag window and stays as it is.
- Glow-map cores on lighting shaders (e.g. Vulcano's Heartstone) are not touched by the kept runtime recolours: `BSLightingShaderProperty` emissive colour may be shared. The builder's copies grey their glow map and emissive colour (Texture copies), so a form the copies cover is grey there.
- A `BSEffectShaderPropertyColorController` animates a property's colour every frame and overrides the base colour change of the kept runtime recolours (`hitArt`, `handArt`, `boundWeapon`); the log counts such shapes. The copies have their controller keys greyed by the builder.
- Effect shapes without the palette flag (examples from vanilla paths: `PCoreCircularFlare`, `RedGlowAdd` on bolt/ball projectiles, `pGlow`/`ppLight` on hand effects) keep their own texture with a grey base colour in the copies (the builder's v2i rule adds no palette flag); the builder's copies give such shapes grey copies of their source textures (Texture copies).
- Staffs, scrolls, enchantments and powers are excluded (`SpellType::kSpell` only).
- A loose-file cache mod (CRDW AE and the like) answers the game's loose-file lookups from an index it built earlier; if that index was made before the builder ran, the model database does not find the new copy NIFs (BSModelDB::Demand: error 1, "does not exist", although the file is there) and those copies show nothing in game (seen 2026-10-17: Dragonfire, Incinerate, the Flame Cloak art). If you use one, rebuild its cache after running the builder. The copy model probe tells this case apart (the plain stream opens the file, Demand does not) and says it once on screen and in the log and the tester file; the builder's last line says it too.
- The runtime bound fire weapon list only takes the static seeds' weapons (built at kDataLoaded from the records): a mod that changes a Bound Weapon effect's associated form at runtime is not covered.
- Bound fire weapons (stage 1): only their arrows take copies. The weapon and arrow meshes (WEAP, its first-person STAT, the AMMO model), the enchantment glow (EFSH `enchantShader`, ARTO `enchantEffectArt`) and the enchantment's hit shader and art on a target stay orange (stage 2 waits for an in-game diagnostic run; the glow is out of scope, README layer table). A bow seed takes every bound-arrow AMMO's PROJ: a fire bound bow shooting the vanilla bound arrow turns that arrow's explosion grey too.
- Heart of Magic's tree leaves the Stoke spell out (no tome teaches it); the prototype gives it by console.
- The kept runtime recolours do not call `SetupGeometry`. If a recoloured shape does not show the palette in game, a missing `SetupGeometry`/`FinishSetupGeometry` (render pass state) is the first suspect.
- Threads (v2 fix D1): SKSE's `AddTask` is drained in two places: in Main::Update on the main thread, but only while the game is paused, and inside the BSJobs "Post process" job (39092 -> 36588) on job worker threads during normal play, alongside the main thread (run 11:11: 409 of 415 task bodies off the main thread). So every task of ours goes to our own main-thread queue (`MainThreadQueue.h`): a call redirect at Main::Update (SE 35565 +0x748, AE 36564 +0xC26 before 1.7.99, +0xC38 from 1.7.99; the site Open Animation Replacer, Precision and True Directional Movement use), after the frame's job wait, on every Main::Update, paused or not. It is written only when the byte is E8, its target is in the game's code or a trampoline stub, and on AE the site is 0x37 bytes before the function's end; otherwise nothing is written and tasks fall back to `AddTask` (logged). The hooks only record and post; every engine change of the hooks, the ticker and the self-test (recolours, light writes, 3D walks, dispels, the self-test's spawns, casts and moves, the buff's effect list walk) runs from the queue. The SKSE message handler is the other main-thread entry: its load and new-game work (the restores at kPreLoadGame, `RefreshCopies`, arming the self-test) runs there, not through the queue. The SE site (0x748) has no tail anchor (only the E8 and target checks) and is unverified on 1.5.97. When the queue is not installed, the kept runtime recolours (`hitArt`, `handArt`, `boundWeapon`) and the light writes are switched off for the session and the self-test is not armed (v2 fixes E6, F2; the copies and the per-call swaps stay; the ticker's art, light and hazard work is gated by those same settings). The build uses /EHa: a task runs under the SEH guard (`SehGuard.h`), so a hardware fault in it is logged with its code and module+offset, and that task kind - like one that throws an exception of an unknown type - is not run or queued again that session; when that is the ticker the line says so (E9, F3, G4); the poster then clears its own queued flag (G3); the queue warns once above 10,000 waiting tasks, and a queue that has run no frame 60 s after the watchdog started gets a STALL block (E5). The queue's first frame stamps the game thread and logs it against `Main::threadID`; it logs that it runs on paused frames, and a heartbeat line every 5 s (frame, queue size, stoked, paused, thread, and "ours this window": the drain and each hot hook's calls and ms of our own part, from QPC in relaxed atomics, `Perf.h`). `RefreshCopies` logs compared / differed and its compare, pointer naming, refill and total ms; the tester file write logs its ms and lines; the builder prints `time  <stage>: N ms` per stage. kDataLoaded runs on an init thread. Log lines that print a stored thread label it (`hookThread=`, `callerThread=`, `gameThread=`), so the log prefix's own thread id is not confused with it. Lists shared between tasks, the message handler and hooks are under mutexes. Since v2 a pending script tag is judged (`ResolvePending`) from any task or ticker body whatever thread runs it - run 2 showed SKSE task bodies on several threads - under the same VM handle policy checks; its first runs log their thread.
- A pending script tag (a wrapped call on a VM thread) is judged by a task right after the call; if the calling effect has already ended by then, it leaves no tag. A second pending caller on a live pending tag is dropped (the first is kept until judged), so the player's tag can be lost when another effect's pending call on the same reference came first. A projectile decided by the deferred path (only a pending tag matched at its hook) does not log the material check. The explosion side of the impact swap lives in the Explosion Update hook (installed whatever `explosion` says since v2). The hazard swap has no PASS/FAIL step: T6-hazard counts, the log lines tell.
- The grey swap is not run in game yet: whether the grey copies draw grey, whether an impact hazard is created inside the impact window (log `loaded inside the player's impact window`), and the 1.6.1170 code offsets are open.
- Hangs (v2 fix C6, measurement only): a watchdog thread flushes the log every second (every info line at once
  while the self-test records) and logs every SKSE message with its thread. Every traced body (`ThreadTrace.h`)
  writes a START and an END entry and holds a slot in an open-body table; our mutexes are `TrackedMutex`
  (`TrackedMutex.h`), which keep their owner thread. When the main-thread queue has not finished a frame for 10 s
  (120 s while a load is under way; the queue runs on paused frames too, and the watchdog reads no engine data -
  v2 fix D2; a minimized or unfocused game that stops Main::Update, or a new-game load longer than 10 s - it sends no
  kPreLoadGame - shows up as a STALL too, which is noise, not harm), the log gets one `STALL` block: the last 64 trace entries, every body
  started and not ended (kind, thread, since when) and every held mutex with its owner and declaration; `stall
  over` when ticks come back. A STALL with no open body and no held mutex means the hang is outside our traced
  code. For the stacks of a hung game, leave it running, set `_NT_SYMBOL_PATH` to the folder holding the DLL's PDB
  (the DebugSymbols zip, `SKSE/Plugins`) plus `srv*` for Microsoft's, and run `cdbX64 -pv -p <pid> -c "~*kb 30;q"`
  (`cdbX64` is the WinDbg (Store) alias; from the Windows SDK Debugging Tools the same is `cdb.exe`; `-pv` attaches without stopping the process for long, `~*kb 30` prints
  30 frames of every thread); the thread ids in the STALL block are the ones to look for.
- Diagnostic build: every step runs under an SEH guard. A fault (access violation and the like) logs the step, code and module+offset and switches that step off for the session (later calls counted); restores are the exception, guarded item by item and never switched off. A fault inside engine code that holds an engine lock may hang the game rather than crash it; the first `hardware exception` line names the step.

## Self-test

The DLL can check most of itself in game, from the engine's own data, and write the results to a file.

1. Use a **throwaway save** on flat open ground outside (the actors are placed at the player's height): the
   test casts the Stoke spell on the player (Heart of Magic may count that cast) and launches Firebolt and Fireball projectiles (no cast: no magicka, no cast event, no XP), spawns three vanilla deer (the third only for E2) and a marker for E3 and deletes them at
   the end.
2. Set `"selfTest": true` in `HeartOfMagic_BlackFlame.json` (the Stoke spell does not need to be known).
3. Load the save and wait about 90 s without opening a menu, and **do not save while the test runs yourself**. It
   starts after `selfTestDelaySeconds` (default 30) of unpaused play with the player in a loaded cell - time in
   menus, loading screens or with the game paused is not counted, and the arm line in the log says the delay -
   runs once per game session, and is disarmed if it could not start within 5 minutes plus that delay. Time in menus or with the game paused does not count
   against a step's timeout. Loading another save or starting a new game stops it (`ABORT`), leaving its
   actors and buff to the load.
4. Read `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_BlackFlame_selftest.txt` (the same
   lines are in `HeartOfMagic_BlackFlame.log`): `[SELFTEST] PASS|FAIL|INFO <id> <detail>`, ending with
   `[SELFTEST] DONE pass=N fail=M skipped=S info=K` (SKIP lines - a step that could not be judged this run - are counted
   apart, v2 fix J2). A step that times out is a FAIL that says what it waited for.
   The last step, E3 (v2), launches a Fire Rune with the buff on and then writes one line: `E3: quicksave now, then
   quickload that save`. Do that within 10 minutes and in the same cell (F5, then F9): the test resumes after the
   load, adds its E3 lines and `[SELFTEST] DONE E3 ...` to the same file, and cleans up (buff dispelled, rune
   removed). Do not keep playing from that quicksave afterwards: it holds the test rune and the Stoke buff. Skipping
   it is fine: another load, a new game or 10 minutes end E3 as INFO "not run (manual step skipped)"; it cleans up
   only while its own world is still the live one (otherwise the line says what may be left). The DLL itself never
   saves or loads.
5. Set `"selfTest": false` again.

Vanilla forms it uses (Skyrim.esm, read with houseCARL): Firebolt `012FD0`, Fireball `01C789` (its explosion
radius is read at runtime and logged, 320 units in Skyrim.esm), and `EncDeer 0CF89D` twice: the target, 550
units ahead (AI off, health raised to 100000), and the caster, 700 units to the player's right (AI kept, so it
behaves like a normal creature; the test turns it to the target and re-checks distances before each launch). A deer is unaggressive and in no crime faction, so hitting either is
no crime. Every projectile re-finds its target first and nothing is launched when it is gone. Projectiles are launched with `Projectile::Launch` (LaunchData from the shooter's chest straight at the centre of the target's 3D world bound - its torso - or, before the 3D has a bound, 60% of its height, aimed higher by the projectile's estimated gravity drop; no engine angle solve, whose AE address is unverified). Never switch this to `Projectile::LaunchSpell`: its CommonLib implementation reaches `RELOCATION_ID(46022, 0)` and aborts on AE (Address Library audit, 2026-10-15) rather than `CastSpellImmediate`, which aims at the target's feet and misses beyond about 300 units (skyrim-headless-mods headless-findings s24); each launch logs origin, aim, the drop and the angles, and any height fallback for a fresh spawn. Every run ends in the cleanup
(T9): the Stoke buff dispelled, then, 1.5 s after the last cast, both deer deleted.

| Step | PASS when |
|---|---|
| T0 | the palette loaded (path logged), the ESP's forms and the vanilla forms exist, Firebolt and Fireball count as fire (game thread, fire counts, config and Fireball's explosion radius logged). It first waits for the copy model probe to end (W21, X18: the probe always ends by its 20 s budget of 8 ms slices, one per frame, about 170 s at 15 fps; a cap of 600 s of play guards a stuck one and is logged), so T0-cover counts every drop |
| T0-wrap | each wrapped native: the script type holds our proxy (the real check, "bound"); the proxy's parameter count and types, static, latent and tasklet flags are the values copied from the original in its constructor (no game script needed) |
| T0-grey | the kDataLoaded grey check lines (see "Grey swap") |
| T0-v2 | the copies map read (no map = FAIL), fill counts per type, and per copy: its fields equal its live source's now and its links point to copies where one exists, else to the source's own (`FormCopies.h` CheckCopies); v2i: a FAIL line per copy whose NIF the game could not load (`CopyModelProbe.h`, with the path and the error code) - that copy is out of the swaps for the session |
| T1 | after the Stoke cast (`CastSpellImmediate`) the player has one more Stoke effect and the flag is on. T1 has 6 s of unpaused play (v2 fix I2); its PASS line gives the dispel-to-flag-off and cast times when it dispelled first. A Stoke buff the player already had is dispelled first and not given back; when it is still on 3 s after that dispel, T1 goes on with it (SKIP "cast not judged", v2 fix J2) so the later steps still run stoked and T8 dispels it (v2 fix H2) |
| T2 | both deer spawned (`PlaceObjectAtMe`) with 3D; the target's AI is off and its health at least 99% of 100000; the target (AI off as soon as its 3D is loaded) has come to rest (v2 fix H1: for 0.5 s it moved at most 5 units on the ground plane and 5 in height, each measured from where it last moved on that axis; it is spawned at the player's height and may fall first). After 8 s without that, a target whose height held for 0.5 s goes on with an INFO line (it may slide on a slope with its AI off); otherwise T2 fails. The T2 line says how far it moved during the wait and rests on the ground plane between Fireball's explosion radius + 100 (the runtime radius, v2 fix J2) and 1200 units from the player wherever the terrain put it (the line gives that distance; later steps read the actual positions). T6, T6-npc, E1b and E1c also check the actual 3D distances before casting (target to player, and to the caster for the caster's casts, at least the explosion radius + 100) and are SKIPped when one is short; T6-swap and T6-npc-swap then SKIP too. The caster's home is its planned spot, at the height it stands at in T2 while it is within 200 units of the spot, else at the target's rest height (the line says how far it had already wandered): before each of its casts it is moved back there when it wandered more than 200 units on the ground plane, and the cast waits for the next tick (v2 fix G2) |
| T3 | launched with a copy (v2 fix C2): the copy is grey by construction, so PASS when every effect shape of its 3D at the hook is grey by the copies' rule (already grey is the expected state; no 3D = FAIL). Without a copy: INFO "not judged" (C6: no runtime recolour; T3-pointer and T3-tint went with it) |
| T4-light | read on the live player projectile of T4, a tick or more after the tint: its light diffuse equals `lightColor` (INFO when it has no light or is gone) |
| T5 | judged only after the Firebolt's hit is confirmed by a TESHitEvent (the player and the target, with Firebolt as the source or its projectile - original or copy - as the projectile; v2 fixes C3, D3): T3's cast first, then after its flight time + 0.8 s without a hit the player casts again (not once our shader is seen), up to 3 casts, else FAIL `aim: no hit after 3 casts` listing every hit event on the target since the first cast and the geometry (player, target, its 3D bound, distance). Then: our shader appears on the target and the `GetHitEffectShader` hook recorded swapping the player's hit there; Firebolt's engine hit shaders are known and none of them is unfinished at 0.5 s or older in the second after (the line lists each engine shader with its age, and how long it checked) |
| T5-second | a second Firebolt, launched by T5 the moment our shader shows, is swapped too: a swap record for the target naming a NEW hit (controller and ActiveEffect address - v2 fix H3: not its `usUniqueID`, which is 0 on most effects; a hit recorded before the second cast asked again does not count); 800 ms later at least one of our shaders is alight, Firebolt's engine shaders are known and none is alight |
| T4 | the caster launches first, then the player, in the same frame (the caster turned to the target first; no explosion margin for this Firebolt, v2 fix I1; the player's projectile matched after T5-second's is on record): the copies are judged (C6: no runtime recolour): the caster's projectile has its original base and, when the player's launch should take a copy (copyLaunch on, Launch hooked, a copy of Firebolt's projectile), the player's has the copy base; judged on the tick after both records |
| T4-shared | while both projectiles still exist, inspected then: no effect property shared, none grey on the caster's (INFO "not comparable" when one was gone) |
| T5-npc | once the caster's projectile can have arrived (distance / projectile speed): the hook recorded a KEPT call (a fire effect someone else cast) for the target, and an engine Firebolt hit shader unfinished at 0.5 s or older that was not already on the target before T4 shows within 1.5 s (a sighting is the same shader only if its age grew with the unpaused clock, and a shader recorded finished never matches an unfinished one); INFO "cannot judge" without a kept record |
| T6 | Fireball's explosion from the player: when it is the copy (v2 fix C2), PASS when every effect shape of its 3D is grey by the copies' rule; else INFO "not judged" (C6; owner, cause, magicCaster current spell, light at Set3D logged). T6-npc needs effect shapes in the caster's explosion 3D, none grey |
| T6-npc | Fireball's explosion from the caster (owner or cause actor) has effect shapes, none grey |
| T3-swap | after T3 and T5-second, for the player's Firebolts: every mapped impact model requested inside an open window was handed out grey; INFO "no mapped model requested inside the window" (with the call sites hooked and chained) when none was asked for, or when `impactSwap` is off, no call site is hooked, or Firebolt's impact set has no model in the map |
| T6-swap | 1.2 s after the player's Fireball: the same judgement for the explosion's impacts |
| T6-hazard | INFO: grey hazard models handed out since the player's Fireball (no vanilla spell is known to leave a mapped hazard reliably, so the hazard swap is judged by this count and the log lines) |
| T6-npc-swap | the caster casts only 2.5 s after the player's Fireball (its explosion has ended); 1.2 s after the caster's Fireball: no grey impact model handed out (INFO when the player's side would not swap either) |
| T3-remote | the script-tag rule: a placed vanilla XMarker (a non-actor reference, only the test's prop), tagged as the native wrapper tags a Cast's akSource, casts Firebolt at the target with no blame actor; PASS only when the projectile it shot was the player's through the script tag and grey as T3 judges it (a copy: the copies' rule on every effect shape of its inspected 3D; no copy: not judged, C6). It tags the marker itself: the wrapper is not exercised here (T0-wrap checks it). Also logs each explosion's caster object reference |
| T3-blame | INFO: a second marker casts with the player as blame actor and no tag; reports whether the engine kept that blame on the marker's NonActorMagicCaster (the blame rule matched) |
| T-placed | INFO "manual": no test script ships for script-placed fire |
| T8 / T8-cast | after T1 turned the buff on, one dispel (`MagicTarget::DispelEffect` only, from the main-thread queue; each effect's flags before and after logged) turns the flag off within 1 s. When it does not hold, a second dispel at 0.5 s restores the state for the later steps, but T8 is then a FAIL (v2 fix D3; E2 likewise). Then (skipped if the buff never came on or is still on) the next Firebolt has effect shapes, none grey, and has no copy, and T3 was judged grey (its copy) The self-test's dispel (`SelfTestDispel.cpp`) scans the player's effect list again by the Stoke keyword for each dispel (v2 fix H3). Before H3 it found the effects again by `usUniqueID`, which is 0 on most effects, so every earlier self-test run dispelled one or two unrelated player effects and left the Stoke effect live (the "dispel did not hold" lines): do not keep saves made after those runs |
| T-threads | v2 fixes D1, E7: PASS only when the main-thread queue is installed, ran task bodies, all of them on `Main::threadID`, no two drains overlapped, nothing fell back to `AddTask`, and no job-side hook body (Launch, explosion creation, GetImpactData in a copy window, Explosion::Update in a copy window) was entered on a worker thread while a drain was running (v2 fix G1: counted online at each hook's entry over the whole session; only drains that ran tasks go into the trace ring) |
| T9 | cleanup (INFO) |
| T9-loadout | W4: 1 s after the bound weapon's spell is dispelled and the loadout put back, the player's right and left hands, ammo and shield are read again: PASS when each holds what it held before (the same instance - enchantment, tempering - when the inventory still has it). The left hand is put back on its own unless the right hand held a two-handed weapon, bow or crossbow (W1); a scroll is re-equipped as an item (W19) |
| T0-hooks | v2: the Launch and explosion creation detours hooked (or chained) with the bytes they found, and both GetImpactData call sites hooked; FAIL for a device that is wanted (config on, copies of that kind filled; the creation detour on AE only) but not hooked; INFO otherwise. Also the round-10 impact call sites, and the prologue replay decoder on fixed byte cases (Launch's and the creation's 1.7.104 prologues = 5, sub rsp imm8/imm32, mov rax/r11 rsp accepted; `48 8B C4 48 89 58 08` and an E9 refused) |
| T0-request | v2 expansion C1: with `writeRequest` on, PASS when closure forms are above 0, the fingerprint is computed, `request.json` is written, no closure form's or muzzle's NIF failed to read and the dump is unchanged or written in full (else FAIL); an ADDN record's NIF that failed (often another mod's missing mesh) is an INFO line with the count and the first five paths, not a FAIL; INFO when `writeRequest` is off; the line gives the counts, the NIF read time, the fingerprint and the dump state. Whether the closure is the right set is judged from `request.json` against the census, not here |
| T0-fresh | slot route: INFO (no copies map to be stale; the slots are filled from the live closure). v2 expansion C5: PASS when the copies are fresh (section "Freshness"); FAIL for stale, none, pluginMissing, notApplied or texturesMissing, with why and how many closure forms have no copy |
| T0-cover | PASS when every copy-type form of this session's fire closure has a filled copy or was excluded with a reason (slot route: a form whose slot's group is closed this session is counted apart, INFO, not missing); FAIL lists the first 20 without one (type, FormID, plugin, the spell that reached it and its plugin). INFO: per fire spell its costliest effect's projectile, dual cast projectile, explosion and casting art, each as copied of present; copies without a grey model (the builder kept the source's); copies no swap point uses (a casting art reached only from an effect that is not a costliest one, a hazard reached only as a non-fire spawn-hazard effect's form). T0-hooks also judges explosion creation, the SetCastingArt call site, the hazard creation detour and the SpawnHazardEffect window: PASS hooked; INFO when left off on purpose (the replay decoder refused the prologue, SE where the function is not verified, the SetCastingArt scan or its byte check) - except on 1.6.1170 / 1.7.104, where a wanted device that is not hooked is a FAIL whatever the note; a full trampoline, a jump into memory that is not executable or a missing id is a FAIL everywhere (fix G1) |
| T0-decal | slot route (with `selfTestSlots`, `SelfTestSlotDecal.h`): every open PROJ / IPCT slot's decal links and IPCT hazard are LinkFor of the source's (an open TXST slot, or the source's set while that slot is closed), never a closed slot; every open TXST slot holds the strings it was written, each TX00 / TX03 it changed is a grey texture copy path, each `textureFileIDs` entry the one the write computed, DODT as the source's - consistency checks of our own writes read back, not proof the engine draws them; with `slotTxst`, a usable TXST slot closed inside an open group with no stated reason = FAIL, every other closed one INFO by its state (`CopyGroups::SlotStateOf`: waiting, failed, left, not usable, "not written: why"). INFO: TXST slots assigned / usable / open and the ID rule. Copies-map route: by data, no live decal read: every PROJ / IPCT copy's decal links hold the TXST copy when one is filled (else the source's set); every TXST copy's DODT presence equals the source's, its slots the map's, its non-empty slots' `textureFileIDs` set wherever the source's are; no closure texture set without a copy. INFO: one line per decal-bearing copy, the closure's sets, the `textureFileIDs` count |
| T0-bound | per bound fire weapon seed (weapon, MGEF, which branch made it fire, the spell carrying it): its MGEF's projectile and, for a bow or crossbow, every bound-arrow AMMO's projectile has a copy (PASS), is excluded from the closure or has no source mesh (listed with the reason), or has no copy (FAIL: run the builder again); a seed without an arrow PROJ (a melee weapon) is INFO. T0-cover adds an INFO line: bound fire weapon arrows copied of those in the closure |
| T0-slots | single launch, with `selfTestSlots`: slots are wanted and active (FAIL with none); the assignment is stable over two starts (SKIP on the ledger's first session); the backups, read now without main and the mirror, hold every reservation of earlier sessions (SKIP with no backup yet: they roll once a session); request v5 was written and its dump is complete (T0-request's rule; T0-request hands this to T0-slots on this route); the BFSL record round-trips in memory and the loaded save's pairs match this session's; no Demand of a gated slot's model. INFO: the file index counters |
| T0-W2 | single launch, with `selfTestSlots`: every publish step of the main-thread drains on Main's thread and none during a load (SKIP when no drain step ran; W1's steps at kDataLoaded run on the kDataLoaded thread and are only counted, INFO); every open slot's model and muzzle as published (compared case-blind, as the model pool hands back a path) and recorded in the ledger, a published path the ledger lacks counted (SKIP with no slot open); no Demand of a gated slot. INFO: publish steps with the main menu open (the grey look is the eye check) |
| E-bound-launch / E-bound-npc | while stoked, the first bow or crossbow seed with a spell: the player's hands and ammo recorded, the spell cast on the player, then (once the weapon is on the runtime bound fire list, 3 s at most) an arrow shot at the self-test's target deer (T3's aim; no target: SKIP, nothing launched - only the test's own actors are shot) through the AMMO `LaunchData` constructor (projectile base = the ammo's PROJ, weapon and ammo source set): PASS when the Launch record has that PROJ's copy. E-bound-npc: the same ammo and weapon from the deer caster is not swapped. SKIP without a bow seed with a spell, or without bound-arrow AMMO |
| E-bound-off | after T8 (the buff off): the same launch from the player is not swapped; then the spell is dispelled and the player's hands and ammo put back (T9 and a late cleanup do it again whatever path the steps took). Every test arrow (player, deer, buff off) is deleted after its judgement (W3: its handle is taken at the launch, so an abort or a failed later step still deletes it), so the save E3 asks for holds no copy-based arrow; as with E3, use a throwaway save: a bound weapon or arrow left by a crash mid-step would be in it |
| T0-pred | v2: `IsFireLaunch` on vanilla props: Firebolt true, no spell/enchantment/weapon false, Sparks (`02DD2A`) false (INFO instead when Sparks counts as fire in the load order) |
| T0-v2 | (also) the synthetic GetImpactData call: the thunk of a hooked site called through the engine's lookup for the first impact of Firebolt's impact set that has a copy - the engine's impact with the window closed, the copy with it open |
| E4 | v2 S3: FAIL `aim: no hit after 3 casts` when T5 neither confirmed a hit nor saw our shader (v2 fixes C3, D3). PASS only when our EFSH was alight on the target after the player's Firebolt (T5's sighting). INFO lines: how often the Firebolt hit the target (TESHitEvent) and the 0x0D outcome counters for the player's effects (calls, swapped, engine answered null and with flags bit 1, not stoked, not a fire effect); the FAIL names which of the three cases it was (no hit, the thunk never reached, the engine answered null, or our own check kept the engine's shader) |
| E1a | v2: the player's Firebolt was launched with its projectile copy (launch record), its 3D at the hook carries the grey palette path, every AddOnNode value the hook recorded with that 3D inspection is a grey ADDN index (no 3D at the hook = FAIL; no AddOnNode = INFO, not judged), and its impact took at least one impact copy (counter) within 1.5 s of the hit. The hit is confirmed first like T5's (TESHitEvent, up to 3 casts, else FAIL `aim: no hit after 3 casts`); after a recast the launch, projectile and impact counters are taken from that cast, so the cast that hit is judged. A missing record is a FAIL |
| E1b | v2: the player's Fireball explosion is the explosion copy with the palette path; INFO which device did it from the counters (Launch post-write, the creation detour seeing a copy already, or the creation detour swapping), with the self-area swaps, rejected create data, non-spell self-area slots and player launches without a copy |
| E1c | v2: the caster's Fireball before the player's (A), 2.5 s after it (B) and in the same frame (C): each phase needs the caster's launch, projectile and explosion each seen at least once, with original bases, effect shapes in their 3D (observed) and none of them grey or with the palette path; in C the player's explosion is the copy. The caster is moved back home before each of its casts (v2 fix C3) |
| E1m | v2 expansion C5: a modded fire spell picked by data - aimed delivery, its costliest effect's projectile has a copy, its plugin last in the load order (a plugin not in the order is skipped; INFO when only the official masters qualify; target-location spells such as runes are left out, their projectile lands and no hit on the target confirms it). After an earlier explosion has settled (2.5 s), the player launches it at the target and the hit is confirmed as in E1a (up to three casts, clocks shifted by paused time). PASS when the launch took the projectile copy and, when the engine's explosion (the costliest effect's MGEF explosion when that effect has an area, else the projectile's) has a copy, that very copy was seen within 1.5 s of the hit. Step limit 12 s (fix F11) |
| E2 | v2: a Flames-type spell (MAG_Flames when its projectile is a flamethrower, else the Flame Atronach's Flames `07E881`) launched by the player every tick at a deer 400 units ahead: 1 s stoked, the buff dispelled mid-stream, 0.6 s more, the buff cast again. PASS: every stoked launch took the copy, launches after the flag went off took the original, and a TESHitEvent from the player with that spell came while stoked (the damage path runs with the copy). INFO: distinct streams before and after (one each = the stream held) |
| E6 | v2 ARTO spike: a fire spell picked by data - cast fire and forget, with a Cloak-archetype fire effect whose hit art (the MGEF's RFCT art when set, else its `hitEffectArt`) has an ARTO copy; of those the spell whose file comes last in the load order (SKIP when there is none, or `copyHitArt` is off). The player casts it on itself while stoked; PASS when within 10 s (was 2 s; the line gives the time from the cast to the 3D) a live ModelReferenceEffect on the player has the copy as its art, its 3D is loaded with every effect shape grey by the copies' rule, no new effect has the original art, and `GetHitEffectArt` returned the copy since the cast. INFO, not FAIL: the original art already on the player before the cast (another cloak), and a copy NIF with no effect shape (its grey not judged). That one line settles both spike questions: the engine takes the art the hook returns, and loads an ARTO record without DATA. The cloak is dispelled right after. Step limit 23 s |
| E6-drop | after that dispel the copy art is gone from the player within 10 s |
| E6-npc | the same spell cast by the deer caster on itself: PASS when its art effect has the original art and none has the copy; the step waits up to 10 s for the original art's 3D and logs its time from the cast as INFO (the control for E6's copy); the cloak is dispelled right after. T9 dispels E6's cloak and its associated spell on every actor in high process again, whatever path E6 took |
| E5 | v2 expansion C4/C5: `PickCastingArtImpl` called directly for a fire spell whose costliest effect's casting art has a copy: player right hand stoked -> copy, not stoked -> original, the power slot -> original, the deer -> original, another effect's casting art of the same spell -> unchanged; a case that cannot run (no deer, no other casting art) says "not run" and stays out of the PASS (fix F12). SKIP without such a spell. INFO first: whether the SetCastingArt site is hooked and this session's calls, swaps and rejections. E5 checks the rule only; the hand art in game is judged from the SetCastingArt log lines |
| E5b | the hand art refresh (`refreshCastingArt`; SKIP when it is off, also when switched off for the session), stoked, on a hand of the player that holds a fire spell whose costliest effect's casting art has a copy (right hand first; nothing is equipped: SKIP without one, SKIP when the refresh is not installed on SE or a build whose code was not read, FAIL on 1.6.1170 / 1.7.104 when the SetCastingArt site is hooked and the refresh is not installed). The hands are drawn when they are not (and sheathed again at the end when the step drew them). Judged by data on that hand's ActorMagicCaster: 1. stoked, `castingArt` is the copy and attached; 2. after the buff is dispelled, the original and attached again with a refresh counted since; 3. after the buff is cast again, the copy and attached again with a refresh counted since. Each phase has 50 self-test ticks (about 5 s), then FAIL with the hand's art, attached flag, caster state, weapon state and refresh count; the buff is on again afterwards (T8 dispels it); a FAIL, or the harness's timeout (the step's timeout cleanup), also sheathes the hands the step drew and casts the buff again when the step dispelled it. Step limit 30 s |
| slot use | slot route (T3, E1a, E1b, E2, E3): a player launch that did not take its copy is judged by its slot: assigned, usable and open but not taken = FAIL; no slot assigned = FAIL (the closure should have assigned one); a slot closed inside an open group with no stated reason = FAIL (`CopyGroups::ClosedUnexplained`); a slot closed this session (still building, failed, terminal, collided, not usable, not written, pool full, refused) = INFO naming the state (`CopyGroups::SlotNote`). Before the first launch is judged (T3) one INFO line: N of M assigned slots open and the closed ones by state (`CopyGroups::SlotSummary`) |
| E3-slots | slot route, with `selfTestSlots`, when E3 starts (by data, before the launch): the Fire Rune's PROJ copy is an open slot; the rune's chain (PROJ decal and explosion, the MGEF's explosion, their impact sets' IPCTs with their decal sets and hazard): every open copy's link is LinkFor of the source's, every open TXST slot reached judged as in T0-decal; the source forms hold no slot (the NPC's and every unswapped path keep the original) - these are consistency checks read back from the forms; with `slotTxst` and a decal set in the chain, each set's slot is judged as in "slot use" (no slot, or closed in an open group with no reason = FAIL; closed with a state = INFO), every set closed = INFO, otherwise a decal link reaches an open TXST slot with grey copies. An NPC rune launch is not part of it (E1c and T4 judge the NPC path) |
| E3 | v2, after T9, semi-manual: the buff cast again, a vanilla Fire Rune (`05DB90`) launched at a marker 300 units ahead; before the save its projectile's base and explosion must be copies; then a marker file `HeartOfMagic_BlackFlame_e3.json` (rune, copy base, cell, game days, session nonce) next to the self-test file and the line asking for a quicksave and a quickload. Judged only on a kPostLoadGame that succeeded, came after its kPreLoadGame and within 10 minutes, with the marker's nonce, the same cell and a game time from the request to one game day after it (another load before the judgement ends E3 as not run): the rune is found again (by FormID with the copy base, else a projectile of the copy base in the cell), alive, base the copy and explosion still a copy (PostLoad3D resets it from the copy PROJ's link). Every other outcome is INFO "E3 not run (manual step skipped)". The cleanup (rune and marker removed; the Stoke buff dispelled only where E3 cast it or its rune was found by FormID in its own save) runs only in the world E3 ran in or in a save shown to be its own; an E3 step ended by its timeout or an exception is cleaned up when the run finishes; every wait has a deadline (10 minutes for the reload, 5 for a started load to finish, 5 for the player to be ready); the marker file is always deleted. E3-esl (INFO, manual): removing the copies ESL with such a save |

The log's `material check` lines (first 5 player and NPC fire projectiles, first 5 NPC fire explosions) list per effect property `prop=`, `mat=`, `grey=` and `paletteFlag=`: `prop=` must differ between player and NPC instances; an equal `mat=` is normal (materials are pooled); any `grey=yes` on an NPC line is contamination.

## Script-placed fire

With `scriptPlaced` on, `ObjectReference.PlaceAtMe`, `Spell.Cast` and `Spell.RemoteCast` are wrapped,
only when the game's own function is an `NF_util::NativeFunctionBase`: a proxy of that class copies
its name, parameters and flags and is bound in its place; the engine's own `NativeFunctionBase::Call`
dispatches to the proxy's `MarshallAndDispatch`, which runs the original's and looks around it (the
log says `replaced by the wrapper` only when the script type then holds the proxy; a target not
found is retried after a load or new game). Only while the buff is on, when the script that called
the native (the frame before the native's own) is a fire effect the player cast or a reference
already tagged: what PlaceAtMe returned is tagged placed, and the akSource of Cast/RemoteCast (when
the spell carries a fire effect) is tagged a source for 5 s, so its projectiles count as the
player's. The active effect is read only on the game thread: a call on a VM thread stores the tag
at once as pending with the caller's VM handle, and a task or the ticker judges it - never a hook
(hooks count confirmed tags only; a projectile matched only by a pending tag is decided by a task
after the judgement, an explosion waits, the impact window and the hazard model swap treat it as not
the player's). Actors are never tagged. C6: nothing placed is recoloured any more (`copyPlaced` hands the script the copy
of a HAZD, EXPL or PROJ); once its 3D exists (within 5 s) a placed fire explosion or hazard gets its light tinted with
`light`; other kinds are left (logged once per form
type). No script, mod or FormID is named. The first native calls log the frames they saw
(`native wrapper frames`).

## In-game checks the self-test cannot do

Only what needs eyes or hands. Steps 1-3 set up a manual session.

1. `help 흑염 4` (or the spell's runtime id from the log line `spell HoMBF_StokeSpell ... = FExxx803`), then
   `player.addspell <id>`, and cast it.
2. **The on-screen look** of each layer (the self-test sees materials and shaders, not pixels): grey burn,
   grey Firebolt, grey Fireball explosion; flame bodies without a palette flag (Embers XD style) keep only a grey
   base colour on their own texture in the copies (the builder's v2i rule): report one that still looks orange; flame edges do not turn
   hard or blocky (the palette's alpha ramp drives shapes counted `with palette alpha`).
3. **Hand art in 1st and 3rd person** (readied flames turn grey when the buff starts and go back when it ends, without sheathing; E5b checks the caster's art by data when a fire spell is equipped, the look is the eye check): `CastSpellImmediate` has no charge animation, so only a real charge
   shows the casting art. The log line `hand art: ...` says which places had effect geometry. A `(more
   geometry)` line repeating every 100 ms means the "already grey" check does not hold: report it.
4. **Concentration and cloaks**: Flames on a target and Flame Cloak on the player show the grey shader with no
   orange frame, and the cloak's flames (hit art) turn grey; the log's `GetHitEffectShader` /
   `GetHitEffectArt` lines say whether the engine asks once per effect or again and again.
5. **Fire Rune**: the resting rune (a grenade projectile) and its explosion turn grey (log `projectile:` with
   kind grenade, then `explosion:`).
6. **Casting light**: a 10 Hz orange/tint flicker (the engine rewriting the light every frame) shows only on
   screen.
7. **Frame time feel** while casting with the buff on: no hitch on cast or impact.
8. **Save and load with the buff active**: the self-test runs on load and dispels the buff itself, so this
   stays manual: log `buff flag ON (game loaded)` or `(tick)`.
9. **Bound fire weapon and script-made fire** (any mod that has them): its bow and arrows turn grey
   (log `bound weapon` and `projectile: ... (arrow)`), and fire a fire spell's script places or remote-casts
   turns grey (log `placed by a script of the player's fire effect`, `first one attributed to the player through
   its script tag / caster's blame actor`). A RemoteCast hit shader turns grey only if the engine gives the
   effect the blame actor as caster: the `GetHitEffectShader` lines say `caster player` or `other`.
10. **A flag-only palette shape against the original** (Z17): the builder takes an empty source texture slot as one white
   texel (the palette's right-hand column), not checked in the shader code. Cast Firestorm (or set a Fire Rune) with the
   copies and once without Black Flame, and compare `firestormareaeffect.nif`'s `Glow:0`: in the copy it should glow grey
   about as bright and as wide as the orange one did. Not automated: what the engine binds to an empty slot shows only on
   screen. Report a copy that is clearly brighter, dimmer or missing.
