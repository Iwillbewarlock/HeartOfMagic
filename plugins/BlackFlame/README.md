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
| `data/HeartOfMagic_BlackFlame.esp` | ESL (masters Skyrim.esm, Update.esm): EFSH `HoMBF_BurnShader` 0x800 (vanilla FireFXShader with both palettes grey), KYWD `HoMBF_StokedKeyword` 0x801, MGEF `HoMBF_StokeEffect` 0x802, SPEL `HoMBF_StokeSpell` "흑염 달구기" 0x803 |
| `data/textures/HeartOfMagic/BlackFlame/GradBlackFlame.dds` | the grey palette (256x64), made by `tools/make-gradient.js` |
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
| `hitArt` | `true` | Layer 1: recolour the hit art of the player's fire effects (e.g. a cloak's flames) |
| `projectile` | `true` | Layer 2: recolour the player's fire projectiles |
| `explosion` | `true` | Layer 3: recolour the player's fire explosions |
| `hazard` | `true` | Layer 3: recolour the player's fire hazards (ground fire): geometry that is not a shared particle system, and the hazard's light |
| `handArt` | `true` | Layer 4: recolour the casting art in the player's hands |
| `boundWeapon` | `true` | Layer 4: recolour the player's bound fire weapons (their arrows count as fire projectiles whatever this says) |
| `scriptPlaced` | `true` | Wrap the Papyrus natives `ObjectReference.PlaceAtMe`, `Spell.Cast` and `Spell.RemoteCast` so what scripts of the player's fire effects place or cast from is recoloured (section "Script-placed fire"). `false`: the natives are left untouched |
| `light` | `true` | Tint projectile, explosion, hazard, casting and light-effect lights to `lightColor` |
| `forcePalette` | `true` | Effect shapes without the grey-to-palette colour flag get the flag, so their own texture brightness is mapped through the grey ramp. Off: such shapes only get the desaturated base colour |
| `particleColors` | `false` | Also desaturate `BSPSysSimpleColorModifier` colour keys in the player's recoloured 3D (layout not in CommonLib, from the Jonahex fork, unverified on AE; see the self-test step T7) |
| `selfTest` | `false` | Run the scripted self-test once after a save is loaded (section "Self-test") |
| `tintScale` | `0.6` | Base colour RGB = luminance x tintScale (0..2), alpha kept |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |

## How it works

**Fire spells and fire effects** (built once at kDataLoaded): only effects that are actually fire
change visuals - an MGEF with the vanilla `MagicDamageFire` keyword (Skyrim.esm 0x01CEAD) or fire
resistance as its resist value. A spell is fire when one of its effects is; only `SpellType::kSpell`
counts. Heart of Magic's `spell_catalog.json` "fire" tag alone does nothing (a summon with red art is
not fire); it is read only to log how many catalog-fire spells have no fire effect. The fire
explosions are those the fire effects and their projectiles name; the fire hazards are those the fire
effects can leave (their impact data sets' hazards, their explosions' impact hazards and placed
objects, a spawn-hazard archetype's form) plus any hazard whose own spell carries a fire effect. The
log gives the counts.

**Whose it is** (projectiles, explosions, hazards): the player's when the owner or shooter is the
player, OR the reference's ActorCause (`TESObjectREFR::GetActorCause`) names the player, OR its
NonActorMagicCaster's `blameActor` is the player (the last two cover `Spell.RemoteCast` from an
activator with the player as blame), OR a script of the player's fire effect placed it or cast from
it (script tag). Which field matched is logged once per layer. The hit shader and hit art use the
active effect's caster handle. No mesh, node, mod, plugin or FormID is special-cased: vanilla meshes, VFX replacers,
legacy NiTriShape meshes and modded spells all go through the same rules.

**Buff flag**: one atomic, written on the game thread when an effect is applied to or removed from
the player, at game load and new game, and by a ticker (every 100 ms while on, every 1 s while off).
Hooks only read it. Every change is logged.

**Recolour** (layers 2-4): every `BSEffectShaderProperty` found by walking the 3D gets its own copy of
its material (materials are pooled; `SetMaterial(copy, true)` gives the property an unshared one),
with the grey palette as greyscale texture and the base colour desaturated. Already grey materials are
skipped, so it is safe to repeat.

| Layer | Where |
|---|---|
| 1 hit shader and hit art | `ActiveEffectReferenceEffectController` vtable: `GetHitEffectShader` (0x0D) and `GetHitEffectArt` (0x0C), where the engine asks an active effect which hit shader and art to put on its target. While stoked, for an effect the player cast (caster handle) whose MGEF is a fire effect of a fire spell (scrolls, staffs and enchantments sharing the MGEF do not count): the shader call returns `HoMBF_BurnShader` instead of the engine's, so the engine makes the hit shader from ours (instant hits, concentration and cloaks alike; no event, no record changed, nothing to finish); the art call returns the engine's art, and the art's 3D (`ModelReferenceEffect::artObject3D`) is recoloured on a later tick once it exists (up to 2 s). The first 20 calls of each slot, on any effect and on a stoked player's fire effect, are logged with the controller, how many times it was asked, the effect, the target and the thread |
| 2 projectile | `Handle3DLoaded` (vfunc 0xC0) on the Missile, Flame, Cone, Beam, Grenade, Barrier and Arrow vtables; the player's (see "Whose it is") and a fire spell's, or an arrow of the player's bound fire weapon |
| 3 explosion | `Set3D` (vfunc 0x6C) on the Explosion vtable; base form in the fire explosion set, the player's (see "Whose it is"), decided only once owner, cause or blame is set; the explosion's own magicCaster (Explosion.h:87) must name nothing, a fire spell, or an enchantment (the explosion's own; a weapon enchantment cannot be told apart) - a scroll, staff, power, potion and so on are left; a light missing at Set3D is tinted on the next tick. Initialize (0xA2) and Update (0xA3) are hooked as a second route (Update does nothing unless stoked or the self-test records); each explosion is decided once, and the first 20 hook calls on any explosion are logged |
| 3 hazard | `Set3D` (0x6C) and `Initialize` (0xA2) on the Hazard vtable: while stoked (and `hazard` on), a hazard with 3D is queued once (by handle) and handled by a task: the player's (owner or cause waited for up to 2 s when neither is set yet) and a fire hazard (see above); its 3D recoloured and its light tinted (light type checked through RTTI); a pass that changed nothing lets a later Set3D queue it again. Geometry under a `BSMasterParticleSystem` is skipped: vanilla fire hazards are mostly AddOn nodes emitting into such a shared system, so expect `0 effect geometries ... N AddOn nodes` on them. The first 20 hook calls on any hazard are logged |
| 4 hand art | ticker task every 100 ms while stoked, and at once when the buff turns on: per hand casting a fire spell OR holding one readied (Actor::GetEquippedObject), the casting art itself (`ActorMagicCaster::castingArtData.attachedArt`); only when that is null or holds no effect geometry (logged as FALLBACK) the caster's `magicNode`, the hand's magic node by name in the 1st and 3rd person skeletons and, last, the player's casting-art `ModelReferenceEffect`s; the caster's light tinted. Every recoloured hand-art property is remembered with its original greyscale texture, path, base colour and palette flags, and put back when the buff turns off and on a load (a fresh material copy, SetMaterial(copy, true)); the casting light's old colour too. Entries only we still hold are dropped each tick |
| 4 bound weapon, light effect | ticker while stoked: a bound fire weapon (bound flag, named by an active Bound Weapon-archetype effect on the player that is a fire effect the player cast) is recoloured in the player's 1st and 3rd person bipeds (`BIPOBJECT::partClone`); a Light-archetype fire effect the player cast on themselves has its light tinted (`LightEffect::light`). Both are put back with the hand art |

## Known limits

- Grey-black, not pitch black: the blending stays additive.
- Death flames (an actor burning to death) stay orange: hardcoded in the engine (Black Flame author's note).
- Scorch decals and impact data sets are unchanged. Of hazards (ground fire) only geometry that is not a shared master particle system and the light change; vanilla flames that emit into a shared `BSMasterParticleSystem` (MPSFireWallBase and the like) stay orange, because recolouring them would turn every NPC's fire of that kind grey.
- A hit shader or hit art made while stoked stays grey until it ends, also after the buff ends; one made before the buff stays as it was (expected: the engine asks when it makes it; the `GetHitEffectShader` lines show the call count per controller).
- Hit art that is a shared master particle system (`BSMasterParticleSystem`) is not recoloured.
- A fire effect whose MGEF names no hit shader gets none (the engine is asked, we only swap what it would show; the old layer applied grey anyway).
- Summoned creatures' own fire (a flame atronach's flames and spells) stays as it is: summons are not part of this (user decision).
- A catalog-fire spell without a fire effect gets nothing (see "Fire spells and fire effects").
- Explosions of a projectile shot by a script-tagged source are only the player's when the explosion itself carries the cause or blame; light effects cast on others than the player are not tinted.
- A reference placed initially disabled by a script has no 3D within the 5 s tag window and stays as it is.
- Glow-map cores on lighting shaders (e.g. Vulcano's Heartstone) are not touched: `BSLightingShaderProperty` emissive colour may be shared.
- Explosions: a weapon enchantment's explosion cannot be told apart from the explosion's own enchantment, so a player-owned one in the fire explosion set is recoloured; with no magicCaster spell, a player-owned fire explosion is recoloured whatever made it.
- A `BSEffectShaderPropertyColorController` animates a property's colour every frame and overrides the base colour change; the log counts such shapes. Particle colour modifiers multiply the palette, so some orange can stay unless `particleColors` is on.
- Shapes without the palette flag, with `forcePalette` off, keep their texture's colour (examples from vanilla paths: `PCoreCircularFlare`, `RedGlowAdd` on bolt/ball projectiles, `pGlow`/`ppLight` on hand effects).
- Staffs, scrolls, enchantments and powers are excluded (`SpellType::kSpell` only).
- Heart of Magic's tree leaves the Stoke spell out (no tome teaches it); the prototype gives it by console.
- The recolour does not call `SetupGeometry`. If a recoloured shape does not show the palette in game, a missing `SetupGeometry`/`FinishSetupGeometry` (render pass state) is the first suspect.
- Threads: SKSE tasks run in the SKSE task phase of Main::Update (main thread, per SKSE 2.2.8 Hooks_Threads.cpp); kDataLoaded runs on an init thread. The game thread is stamped by the first SKSE task (queued at `SKSEPlugin_Load`), which logs `taskThread=`, `loadThread=` and `Main::threadID=` in one line. Log lines that print a stored thread label it (`hookThread=`, `callerThread=`, `gameThread=`), so the log prefix's own thread id is not confused with it. Lists shared between tasks, the message handler and hooks are under mutexes.
- Diagnostic build: every step runs under an SEH guard. A fault (access violation and the like) logs the step, code and module+offset once and switches that step off for the session (later calls counted). A fault inside engine code that holds an engine lock may hang the game rather than crash it; the first `hardware exception` line names the step.

## Self-test

The DLL can check most of itself in game, from the engine's own data, and write the results to a file.

1. Use a **throwaway save** on flat open ground outside (the actors are placed at the player's height): the
   test casts the Stoke spell on the player (Heart of Magic may count that cast) and launches Firebolt and Fireball projectiles (no cast: no magicka, no cast event, no XP), spawns two vanilla deer and deletes them at
   the end.
2. Set `"selfTest": true` in `HeartOfMagic_BlackFlame.json` (the Stoke spell does not need to be known).
3. Load the save and wait about 30 s without opening a menu, and **do not save while the test runs**. It
   starts 3 s after the player is in a loaded cell with the game unpaused, runs once per game session, and is
   disarmed if it could not start within 5 minutes. Time in menus or with the game paused does not count
   against a step's timeout. Loading another save or starting a new game stops it (`ABORT`), leaving its
   actors and buff to the load.
4. Read `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_BlackFlame_selftest.txt` (the same
   lines are in `HeartOfMagic_BlackFlame.log`): `[SELFTEST] PASS|FAIL|INFO <id> <detail>`, ending with
   `[SELFTEST] DONE pass=N fail=M info=K`. A step that times out is a FAIL that says what it waited for.
5. Set `"selfTest": false` again.

Vanilla forms it uses (Skyrim.esm, read with houseCARL): Firebolt `012FD0`, Fireball `01C789` (its explosion
radius is read at runtime and logged, 320 units in Skyrim.esm), and `EncDeer 0CF89D` twice: the target, 1000
units ahead (AI off, health raised to 100000), and the caster, 700 units to the player's right (AI kept, so it
behaves like a normal creature; the test turns it to the target and re-checks distances before each launch). A deer is unaggressive and in no crime faction, so hitting either is
no crime. Every projectile re-finds its target first and nothing is launched when it is gone. Projectiles are launched with `Projectile::Launch` (LaunchData from the shooter's chest straight at the target's torso, aimed higher by the projectile's estimated gravity drop; no engine angle solve, whose AE address is unverified). Never switch this to `Projectile::LaunchSpell`: its CommonLib implementation reaches `RELOCATION_ID(46022, 0)` and aborts on AE (Address Library audit, 2026-10-15) rather than `CastSpellImmediate`, which aims at the target's feet and misses beyond about 300 units (skyrim-headless-mods headless-findings s24); each launch logs origin, aim, the drop and the angles, and any height fallback for a fresh spawn. Every run ends in the cleanup
(T9): the Stoke buff dispelled, then, 1.5 s after the last cast, both deer deleted.

| Step | PASS when |
|---|---|
| T0 | the palette loaded (path logged), the ESP's forms and the vanilla forms exist, Firebolt and Fireball count as fire (game thread, fire counts, config and Fireball's explosion radius logged) |
| T1 | after the Stoke cast (`CastSpellImmediate`) the player has one more Stoke effect and the flag is on. A Stoke buff the player already had is dispelled first and not given back |
| T2 | both deer spawned (`PlaceObjectAtMe`) with 3D; the target's AI is off and its health at least 99% of 100000; target-to-player and target-to-caster distances are beyond the explosion radius + 100 |
| T3 | the player's Firebolt was recoloured and at least as many effect shapes carry the palette path as were recoloured (ratio logged; INFO "partial" when forcePalette left some out) |
| T3-pointer | every palette material also kept our texture pointer (the property holds the engine's copy) |
| T3-tint | every recoloured base colour is grey (R=G=B) and, for at least one shape seen before the recolour and without a colour controller, equals its old luminance x tintScale; INFO instead when shapes were recoloured but every comparable one has a colour controller |
| T4-light | read on the live player projectile of T4, a tick or more after the tint: its light diffuse equals `lightColor` (INFO when it has no light or is gone) |
| T5 | our shader appears on the target and the `GetHitEffectShader` hook recorded swapping the player's hit there; Firebolt's engine hit shaders are known and none of them is unfinished at 0.5 s or older in the second after (the line lists each engine shader with its age, and how long it checked) |
| T5-second | a second Firebolt, launched by T5 the moment our shader shows, is swapped too: a swap record for the target naming a NEW controller (the first hit's controller asked again does not count); 800 ms later at least one of our shaders is alight, Firebolt's engine shaders are known and none is alight |
| T4 | the caster launches first, then the player, in the same frame (the caster turned to the target and every distance re-checked first; the player's projectile matched after T5-second's is on record): the caster's has effect shapes, none grey, and the player's was recoloured; judged on the tick after both records |
| T4-shared | while both projectiles still exist, inspected then: no effect property shared, none grey on the caster's (INFO "not comparable" when one was gone) |
| T5-npc | once the caster's projectile can have arrived (distance / projectile speed): the hook recorded a KEPT call (a fire effect someone else cast) for the target, and an engine Firebolt hit shader unfinished at 0.5 s or older that was not already on the target before T4 shows within 1.5 s (a sighting is the same shader only if its age grew with the unpaused clock, and a shader recorded finished never matches an unfinished one); INFO "cannot judge" without a kept record |
| T6 | Fireball's explosion from the player is recoloured with the palette path on the recoloured shapes (owner, cause, magicCaster current spell, light at Set3D logged) |
| T6-npc | Fireball's explosion from the caster (owner or cause actor) has effect shapes, none grey, not recoloured |
| T3-remote | RemoteCast attribution: a placed vanilla XMarker (a non-actor reference, only the test's prop) casts Firebolt at the target through its NonActorMagicCaster with the player as blame actor; the projectile it shot counts as the player's and is recoloured |
| T-bound / T-placed | INFO "manual": vanilla has no bound weapon with a fire effect; no test script ships for script-placed fire |
| T7 / T7-player | INFO: the first colour modifier's keys (0x30-0x5F) and whether the layout looks plausible (all in 0..2, one above 0); with `particleColors`: every player modifier desaturated (T7-player); and, only when the caster's projectile was recorded before the player's, its modifiers neither shared, nor changed between its hook and T4, nor already equal to the desaturated keys of the player's T3 projectile before its recolour (T7; "not compared" when those had no colour, INFO when not comparable) |
| T8 / T8-cast | after T1 turned the buff on, dispelling it turns the flag off within 1 s; then (skipped if the buff never came on or is still on) the next Firebolt has effect shapes, none grey, not recoloured, and T3 had recoloured |
| T9 | cleanup (INFO) |

The log's `material check` lines (first 5 player and NPC fire projectiles, first 5 NPC fire explosions) list per effect property `prop=`, `mat=`, `grey=` and `paletteFlag=`: `prop=` must differ between player and NPC instances; an equal `mat=` is normal (materials are pooled); any `grey=yes` on an NPC line is contamination.

## Script-placed fire

With `scriptPlaced` on, `ObjectReference.PlaceAtMe`, `Spell.Cast` and `Spell.RemoteCast` are wrapped: a
proxy function that forwards every call to the game's own native is bound in their place (the log
says `... wrapped: yes`; a target not found at Papyrus registration is retried after a load). Only
while the buff is on, when the script that called the native is a fire effect the player cast (its
ActiveMagicEffect resolves to the active effect) or a reference already tagged: what PlaceAtMe returned
is recoloured once its 3D exists (within 5 s), and the akSource of Cast/RemoteCast (when the spell
carries a fire effect) counts as the player's for 5 s, so its projectiles are recoloured. No script,
mod or FormID is named. The first native calls log the frames they saw (`native wrapper frames`).

## In-game checks the self-test cannot do

Only what needs eyes or hands. Steps 1-3 set up a manual session.

1. `help 흑염 4` (or the spell's runtime id from the log line `spell HoMBF_StokeSpell ... = FExxx803`), then
   `player.addspell <id>`, and cast it.
2. **The on-screen look** of each layer (the self-test sees materials and shaders, not pixels): grey burn,
   grey Firebolt, grey Fireball explosion; flame bodies forced onto the palette (`forcePalette`) keep their
   shape (a blank or white-box shape: set `forcePalette` false and report the mesh); flame edges do not turn
   hard or blocky (the palette's alpha ramp drives shapes counted `with palette alpha`).
3. **Hand art in 1st and 3rd person** (readied flames turn grey when the buff starts and go back when it ends; not automated: equipping and readying a spell from the test is not done): `CastSpellImmediate` has no charge animation, so only a real charge
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
