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
| `data/meshes/heartofmagic/blackflame/` | grey master particle systems and remapped impact / hazard copies, made from the vanilla BSAs by `tools/blackflame-greybuild` |
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
| `hitArt` | `false` | Layer 1: runtime recolour of the hit art of the player's fire effects (e.g. a cloak's flames). Off by default since v2 (runtime recolour retired, see "Black Flame v2") |
| `projectile` | `false` | Layer 2: runtime recolour of the player's fire projectiles' 3D. Off by default since v2; the projectile hook stays installed (light tint, self-test records) |
| `explosion` | `false` | Layer 3: runtime recolour of the player's fire explosions' 3D. Off by default since v2; the explosion hooks stay installed (light tint, the explosion side of the impact window, self-test records) |
| `hazard` | `false` | Layer 3: runtime recolour of the player's fire hazards (ground fire): geometry that is not a shared particle system. Off by default since v2; the hazard's light is tinted with `light` |
| `handArt` | `false` | Layer 4: runtime recolour of the casting art in the player's hands. Off by default since v2 (the casting light is tinted with `light`) |
| `boundWeapon` | `false` | Layer 4: runtime recolour of the player's bound fire weapons (their arrows count as fire projectiles whatever this says). Off by default since v2 |
| `scriptPlaced` | `true` | Wrap the Papyrus natives `ObjectReference.PlaceAtMe`, `Spell.Cast` and `Spell.RemoteCast` so what scripts of the player's fire effects place or cast from is recoloured (section "Script-placed fire"). `false`: the natives are left untouched |
| `impactSwap` | `true` | The player's fire impacts load the grey copies named by the grey map (section "Grey swap") |
| `hazardSwap` | `true` | The player's fire hazards load the grey copies named by the grey map |
| `light` | `true` | Tint projectile, explosion, hazard, casting and light-effect lights to `lightColor` (independent of the recolour keys above: the per-instance light tint stays in v2) |
| `copyLaunch` | `true` | v2: the player's fire launch takes its projectile copy (`Projectile::Launch` detour, section "Black Flame v2") |
| `copyLaunchExplosion` | `true` | v2: after that launch the projectile's explosion is set to its copy |
| `copyExplosionCreate` | `true` | v2: explosion creation hands the explosion copy when the source projectile is a copy or the player's self-area fire cast made it |
| `copyImpacts` | `true` | v2: impacts inside a copy window take the impact copies |
| `writeRequest` | `false` | v2 expansion: write the copy request and the NIF dump for the builder at kDataLoaded (section "Copy request"). Off for players: then no NIF is read at startup; the tester package turns it on |
| `testerMode` | `false` | the tester package: `selfTest` and `writeRequest` forced on, every log line written through at once (section "Runtimes and Address Library ids") |
| `copyCastingArt` | `true` | v2 expansion C4: SetCastingArt's call gives the player's fire hand art its copy (section "Swap points") |
| `copyHazardCreate` | `true` | v2 expansion C4: hazard creation gives the hazard its copy inside a copy window or from the player's fire spawn-hazard effect |
| `copyPlaced` | `true` | v2 expansion C4: PlaceAtMe called by the player's fire effect script places the copy of a HAZD, EXPL or PROJ |
| `copyHitArt` | `true` | v2 (ARTO spike): the hit art of the player's fire effects is the art's ARTO copy when it has one - `GetHitEffectArt` returns the copy, so the engine builds the art's 3D from the copy's grey NIF; an art without a copy keeps the engine's art (recoloured only under `hitArt`) |
| `forcePalette` | `true` | Effect shapes without the grey-to-palette colour flag get the flag, so their own texture brightness is mapped through the grey ramp. Off: such shapes only get the desaturated base colour |
| `particleColors` | `false` | Also desaturate `BSPSysSimpleColorModifier` colour keys in the player's recoloured 3D (layout not in CommonLib, from the Jonahex fork, unverified on AE; see the self-test step T7) |
| `selfTest` | `false` | Run the scripted self-test once after a save is loaded (section "Self-test") |
| `devAutoLoad` | absent | **Dev only** (not in the shipped json or the tester package; ignored unless `selfTest` or `testerMode` is on): a save name without extension, loaded once, 5 s after the main menu first opens in a session, unless a load has started by then (`DevAutomation.h`) |
| `devAutoE3` | `false` | **Dev only** (same rules): when E3 asks for a save and reload, the plugin waits 3 s, saves `HoMBF_E3`, waits for SKSE's save message plus 3 s (10 s at most without it) and loads `HoMBF_E3`; E3 judges that load as it judges a player's |
| `devAutoQuit` | `false` | **Dev only** (same rules): the game quits (`Main::quitGame`) 10 s after the self-test's DONE, or after DONE E3 when E3 armed; 6 minutes after the last load (or the auto load's request, or the game start when `devAutoLoad` is set) without that, it quits anyway and logs TIMEOUT |
| `selfTestDelaySeconds` | `30` | Seconds of unpaused play (player in a loaded cell, no menu, not loading) after the load before the self-test's first step, and before E3 judges after its quickload (0..600) |
| `tintScale` | `0.6` | Base colour RGB = luminance x tintScale (0..2), alpha kept |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |

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

**Recolour** (layers 2-4): every `BSEffectShaderProperty` found by walking the 3D gets its own copy of
its material (materials are pooled; `SetMaterial(copy, true)` gives the property an unshared one),
with the grey palette as greyscale texture and the base colour desaturated. Already grey materials are
skipped, so it is safe to repeat.

| Layer | Where |
|---|---|
| 1 hit shader and hit art | `ActiveEffectReferenceEffectController` vtable: `GetHitEffectShader` (0x0D) and `GetHitEffectArt` (0x0C), where the engine asks an active effect which hit shader and art to put on its target. While stoked, for an effect the player cast (caster handle) whose MGEF is a fire effect of a fire spell (scrolls, staffs and enchantments sharing the MGEF do not count): the shader call returns `HoMBF_BurnShader` instead of the engine's, so the engine makes the hit shader from ours (instant hits, concentration and cloaks alike; no event, no record changed, nothing to finish); the art call returns the engine's art, and with `hitArt` on the art's 3D (`ModelReferenceEffect::artObject3D`) is recoloured on a later tick once it exists (up to 2 s; matched on controller and art only - the engine's first art call comes before it sets the target - with the live controller re-checked as the player's fire effect, and the target read from the live effect). Both slots are hooked whatever the config says (v2 S3); `burnShader` and `hitArt` decide only the swap and the recolour. Logged per slot, with two caps that cannot use each other up: the first 20 calls whose caster is not the player and that have something to show (the engine's answer or the MGEF's own field not null), and the first 60 calls whose caster is the player, whatever the outcome - each with the controller and its call count, the active effect's flags (+0x7C, raw, and bit 1: the engine's "no hit shader" bit, set only by the cloak callback), the MGEF and its own field, the spell, the target, the caster, whether stoked, the burn shader form, the engine's answer, the outcome with its reason (swapped, engine answered null, not stoked, not a fire effect, config off...) and the thread. Per-outcome counters of the player's calls feed the self-test (E4). The engine reads MGEF `data.effectShader` only through slot 0x0D (AE 34846 / SE 34049, called from `ReferenceEffectController::Start` 34761 and the `ShaderReferenceEffect` constructor 34910; research 2026-10-16), so no other swap point exists; enchantment shaders go through `ApplyEffectShader` (19872) and are out of scope |
| 2 projectile | `Handle3DLoaded` (vfunc 0xC0) on the Missile, Flame, Cone, Beam, Grenade, Barrier and Arrow vtables; the player's (see "Whose it is") and a fire spell's, or an arrow of the player's bound fire weapon |
| 3 explosion | `Set3D` (vfunc 0x6C) on the Explosion vtable; base form in the fire explosion set, the player's (see "Whose it is"), decided only once owner, blame or a script tag (also on the explosion caster's object reference, `GetCasterObjectReference`) is there, given up after 2 s; the explosion's own magicCaster (Explosion.h:87) must name nothing, a fire spell, or an enchantment (the explosion's own; a weapon enchantment cannot be told apart) - a scroll, staff, power, potion and so on are left; a light missing at Set3D is tinted on the next tick. Initialize (0xA2) and Update (0xA3) are hooked as a second route (Update does nothing unless stoked or the self-test records); each explosion is decided once, and the first 20 hook calls on any explosion are logged |
| 3 hazard | `Set3D` (0x6C) and `Initialize` (0xA2) on the Hazard vtable: while stoked (and `hazard` on), a hazard with 3D is queued once (by handle) and handled by a task: the player's (owner or script tag waited for up to 2 s; a hazard made inside the player's impact window is tagged) and a fire hazard (see above); its 3D recoloured and its light tinted (light type checked through RTTI); a pass that changed nothing lets a later Set3D queue it again. Geometry under a `BSMasterParticleSystem` is skipped: vanilla fire hazards are mostly AddOn nodes emitting into such a shared system, so expect `0 effect geometries ... N AddOn nodes` on them. The first 20 hook calls on any hazard are logged |
| 4 hand art | ticker task every 100 ms while stoked, and at once when the buff turns on: per hand casting a fire spell OR holding one readied (Actor::GetEquippedObject), the casting art itself (`ActorMagicCaster::castingArtData.attachedArt`); only when that is null or holds no effect geometry (logged as FALLBACK) the caster's `magicNode`, the hand's magic node by name in the 1st and 3rd person skeletons and, last, the player's casting-art `ModelReferenceEffect`s; the caster's light tinted. Every recoloured hand-art property is remembered with its original greyscale texture, path, base colour and palette flags, and put back when the buff turns off and on a load (a fresh material copy, SetMaterial(copy, true)); the casting light's old colour too. Entries only we still hold are dropped each tick |
| 4 bound weapon, light effect | ticker while stoked: a bound fire weapon (bound flag, named by an active Bound Weapon-archetype effect the player cast on themselves, that effect a fire effect of a fire spell or the weapon's own enchantment carrying a fire MGEF; the list is kept by the ticker and the arrow hook only compares FormIDs) is recoloured in the player's 1st and 3rd person bipeds (`BIPOBJECT::partClone`); a Light-archetype fire effect the player cast on themselves has its light tinted (`LightEffect::light`). Both are put back with the hand art |

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

**The map**, `data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json` (version 1): `tint`, `addons` (per
original index: `grey`, `model` and `greyModel`, `editorId`, `source`, `masterParticleCap`, `dnamFlags`,
`greyEditorId`, `greyForm` as `Plugin|localId`) and `models` (impact / hazard NIF -> its grey copy). Paths are
Data-relative, lower case, backslashes (`meshes\mps\mpsfireboltimpact01.nif`); a record's Model field is the same
path without `meshes\`.

**The grey copies.** In a grey MPS every `BSEffectShaderProperty` gets the greyscale-to-palette colour flag
(SLSF1 bit 4) on, `textures\HeartOfMagic\BlackFlame\GradBlackFlame.dds` as greyscale texture and base colour RGB =
luminance x `tint` (Rec. 709 weights and default 0.6, the DLL's `Tint.h` and `tintScale`), alpha kept; the
palette alpha flag stays as it was. `BSPSysSimpleColorModifier` keys and `BSEffectShaderPropertyColorController`
keys (direct `NiPoint3Interpolator` only; the build counts any it cannot reach) go to luminance, alpha kept.
Lighting-shader shapes are left alone. An impact / hazard copy changes only its AddOnNode values (names kept);
its own inline particles (e.g. FireWallFX01's flames) stay as they are, for the DLL's per-instance recolour.
nifly re-saves the rest unchanged: byte-identical, or with the header string table reordered, or (seen on two
replacer meshes) with unused header strings dropped and one node block 4-8 bytes shorter, consistent with nifly removing empty child references (`--roundtrip` lists what changed).

**The builder**, `tools/blackflame-greybuild` (C++, built by `BuildRelease.ps1` into `build/tools/Release/`):

```
blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>
blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>]
blackflame-greybuild --dump <file.nif> [...]        (AddOnNodes, effect shaders, colour keys)
blackflame-greybuild --roundtrip <file.nif> [...]   (does an unchanged re-save match?)
blackflame-greybuild --self-test                    (made-up NIF, no game files; exit 0 = pass)
```

`--in` folders hold loose files laid out as Data (`meshes\...`), searched in the order given: the first that has
a file wins, so list them in MO2 priority order, highest first, then extracted BSAs, then the vanilla
extraction (the builder reads loose files only; extract BSAs first, e.g. houseCARL `housecarl_bsa_extract`).
`--out` must not overlap any `--in`. Every output is read back and checked (flag, texture, grey colours,
no original index left); an AddOnNode value the map does not know fails that file and nothing is written for it.
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
  the same window around `Explosion::Update` for the player's fire explosions (needs `explosion` on). While a
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
blackflame-greybuild --forms <forms.json> --request <addons.json> --in <dir> [--in <dir> ...] --out <mod root> [--tint <0..2>]
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
| `meshes\heartofmagic\blackflame\copies\<model path>` | the full-grey copy of each distinct source NIF: every effect shader on the grey palette (as the grey MPS) and every AddOnNode on its grey ADDN index |
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
IPDS, DUAL, RFCT, SPEL and MGEF are walked through. Excluded, each with its reason: a form of our own plugins
(`HeartOfMagic_BlackFlame*`), a placed object that is not a copy type, an art with alternate textures. Then, in
`<SKSE log folder>\HeartOfMagic_BlackFlame\` (`My Games\Skyrim Special Edition\SKSE\...`: outside the VFS, so MO2's
overwrite stays clean):

| File | What |
|---|---|
| `request.json` | version 3: `game`, `fingerprint`, `copiesPlugin`; `forms` (source `Plugin|localId` in UTF-8, type, editorId, model and muzzleModel as the record stores them, bounds, the scalars the builder writes into a placeholder, `viaSpell`, `costliest`); `excluded` (source, type, reason); `addons` (every ADDN record: index, model, cap, DNAM flags, `own` for ours); `models` (the closure's impact and hazard models); `nifs` (each NIF read: Data-relative path, size, fnv64, ok, error). Written through a temporary file and a rename |
| `data\meshes\...` | the winning bytes of every NIF the request names (form models, PROJ muzzles, every ADDN model not ours), read through the engine's own file system (`BSResourceNiBinaryStream`: archives, loose files and MO2's VFS as the game loads them). Rewritten only when the fingerprint differs from `dump-manifest.json`'s; a NIF that fails to read is removed from the dump |
| `dump-manifest.json` | the fingerprint the dump was made for |

Model paths are normalised (ASCII lower case, `/` as `\`, repeated separators folded, a leading `data\`/`meshes\` dropped,
one `meshes\` put in front); a path with a drive letter, `:` or a `..` token is refused and never read or written. Each
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
blackflame-greybuild --from-game [<request folder>] --out <output mod folder> [--tint <0..2>] [--reclaim-retired]
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
  compared by the DLL at every start), `copiesPlugin`, per copy also `sourceMuzzleModel`, `greyMuzzleModel` (PROJ: the
  grey copy of its muzzle flash model, built like the form models) and `note`; `retired`; `excluded` (the request's).
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

### Freshness and the copies map v2 (v2 expansion C3, DLL): `CopyFreshness.h`

The DLL reads copies map version 2 (and version 1, which counts as stale). A PROJ copy's muzzle flash model is the map's
`greyMuzzleModel` when the builder made one, else the source's own: the fill, the compare and the refresh all hold the
copy to that same value (otherwise every load would put the source's back and T0-v2 would fail). At kDataLoaded, after
the copy request, the copies are judged:

| State | When |
|---|---|
| `none` | no copies map, or one with no entry (only the empty ESLs the mod ships) |
| `pluginMissing` | the map was read but `HeartOfMagic_BlackFlame_Copies.esp` is not loaded, or at least half of its entries (and at least one) lost their copy form (the output mod placed before Black Flame, or not enabled) |
| `stale` | a version 1 map, or the map's request fingerprint differs from this start's: the byte form when `writeRequest` computed it, else the size form (each NIF only opened for its size, no byte read; the request carries both forms) |
| `fresh` | otherwise |

The log line gives the state, why, how many closure forms have no copy and how many map entries lost their source. Once
per session, at the first save load or new game, the player sees a corner notice through the main-thread queue (no
queue: the log only): stale - "Black Flame: your fire spells changed. Run the Black Flame builder, then restart the
game."; none - "Black Flame: the grey copies are not built yet. Run the Black Flame builder, then restart the game.";
pluginMissing - "Black Flame: the generated copies are not enabled. Enable HeartOfMagic_BlackFlame_Copies.esp and put
the copies output mod after Black Flame." The notice counts as shown only once the main-thread queue took it, and none
is shown when kDataLoaded did not get as far as the check (the ESP's forms missing, the closure failing: the log says
why; fix F10). English only (as the rest of this prototype's HUD lines); no id goes on screen. A
player's fire form without a copy stays orange, logged once per form with the freshness state. Do not deploy a C2
builder output with a DLL older than C3 (it reads only map version 1).

### Fill (S1, DLL): `FormCopies.h`

At kDataLoaded only, before any hook reads them, each copy is filled from the LIVE winning source (so SkyPatcher and
other runtime edits made before then are included), data members only, never the form header, flags or the copy's
own model: PROJ `data` (0x80-0x108), `soundLevel`, `fullName`, `muzzleFlashModel` through `TESModel::SetModel`,
`boundData`; EXPL `data` (0x98-0xE8), the enchantment members, `imageSpaceModifying`, `fullName`, `boundData`; IPCT
`data`, both decal texture sets, both sounds, `hazard`, `dData`; HAZD `data`, `imageSpaceModifying`, `fullName`,
`boundData`; ARTO `data` (the DNAM art type) and `boundData` - never its model component; a source art with alternate
textures (MODS) is refused and left out (`alternate textures not copied`; no fire art in the measured order has any). Data structs are copied byte for byte (their layouts are asserted by CommonLib). A second pass points
only the links that have a copy at it: PROJ `explosionType`, EXPL `impactPlacedObject` and `spawnProjectile`, IPCT
`hazard`; every other link (light, sounds, texture sets, image space, enchantment, hazard spell, impact data sets)
stays the source's. A copy whose source or copy form is missing, or whose types differ, or whose own model does not
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
  call (C4); when the engine calls it (equip/ready or every cast) is settled by its log lines and E5's INFO. Spawn-hazard
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
- E3 is semi-manual: the DLL never saves or loads (MO2 local saves go to another folder, and the engine's save and
  load functions were not read). It asks for a quicksave and a quickload and judges only a load that matches its
  marker (same session nonce, same cell, within a game day after the request, within 10 minutes). A quicksave made
  for E3 holds the test rune and buff: do not keep playing from it.
- A re-run of `--forms` with a hand-made ADDN request gives the local ADDN records ids from 0x800 in index order
  again (the request does not list the local records a DLL-written request would): replace the whole output mod,
  never layer two outputs (the same greymap and Local ESL names, different ids).


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

## Known limits

- Grey-black, not pitch black: the blending stays additive.
- Death flames (an actor burning to death) stay orange: hardcoded in the engine (Black Flame author's note).
- Scorch decals and impact data sets are unchanged. Of hazards (ground fire) only geometry that is not a shared master particle system and the light change; vanilla flames that emit into a shared `BSMasterParticleSystem` (MPSFireWallBase and the like) stay orange, because recolouring them would turn every NPC's fire of that kind grey. Grey copies of the shared-particle impacts and hazards exist (section "Grey duplicates"), and the DLL swaps the player's to them while stoked (section "Grey swap").
- A hit shader or hit art made while stoked stays grey until it ends, also after the buff ends; one made before the buff stays as it was (expected: the engine asks when it makes it; the `GetHitEffectShader` lines show the call count per controller).
- Hit art that is a shared master particle system (`BSMasterParticleSystem`) is not recoloured.
- A fire effect whose MGEF names no hit shader gets none (the engine is asked, we only swap what it would show; the old layer applied grey anyway).
- A fire trap the player set off stays orange (the actor cause is not a rule, see "Whose it is"); so does a RemoteCast whose blame the engine does not keep on the caster (T3-blame reports it).
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
- Threads (v2 fix D1): SKSE's `AddTask` is drained in two places: in Main::Update on the main thread, but only while the game is paused, and inside the BSJobs "Post process" job (39092 -> 36588) on job worker threads during normal play, alongside the main thread (run 11:11: 409 of 415 task bodies off the main thread). So every task of ours goes to our own main-thread queue (`MainThreadQueue.h`): a call redirect at Main::Update (SE 35565 +0x748, AE 36564 +0xC26 before 1.7.99, +0xC38 from 1.7.99; the site Open Animation Replacer, Precision and True Directional Movement use), after the frame's job wait, on every Main::Update, paused or not. It is written only when the byte is E8, its target is in the game's code or a trampoline stub, and on AE the site is 0x37 bytes before the function's end; otherwise nothing is written and tasks fall back to `AddTask` (logged). The hooks only record and post; every engine change of the hooks, the ticker and the self-test (recolours, light writes, 3D walks, dispels, the self-test's spawns, casts and moves, the buff's effect list walk) runs from the queue. The SKSE message handler is the other main-thread entry: its load and new-game work (the restores at kPreLoadGame, `RefreshCopies`, arming the self-test) runs there, not through the queue. The SE site (0x748) has no tail anchor (only the E8 and target checks) and is unverified on 1.5.97. When the queue is not installed, the runtime recolour layers and light writes are switched off for the session and the self-test is not armed (v2 fixes E6, F2; the copies and the per-call swaps stay; the ticker's art, light and hazard work is gated by those same settings). The build uses /EHa: a task runs under the SEH guard (`SehGuard.h`), so a hardware fault in it is logged with its code and module+offset, and that task kind - like one that throws an exception of an unknown type - is not run or queued again that session; when that is the ticker the line says so (E9, F3, G4); the poster then clears its own queued flag (G3); the queue warns once above 10,000 waiting tasks, and a queue that has run no frame 60 s after the watchdog started gets a STALL block (E5). The queue's first frame stamps the game thread and logs it against `Main::threadID`; it logs that it runs on paused frames, and a heartbeat line every 5 s (frame, queue size, stoked, paused, thread). kDataLoaded runs on an init thread. Log lines that print a stored thread label it (`hookThread=`, `callerThread=`, `gameThread=`), so the log prefix's own thread id is not confused with it. Lists shared between tasks, the message handler and hooks are under mutexes. Since v2 a pending script tag is judged (`ResolvePending`) from any task or ticker body whatever thread runs it - run 2 showed SKSE task bodies on several threads - under the same VM handle policy checks; its first runs log their thread.
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
| T0 | the palette loaded (path logged), the ESP's forms and the vanilla forms exist, Firebolt and Fireball count as fire (game thread, fire counts, config and Fireball's explosion radius logged) |
| T0-wrap | each wrapped native: the script type holds our proxy (the real check, "bound"); the proxy's parameter count and types, static, latent and tasklet flags are the values copied from the original in its constructor (no game script needed) |
| T0-grey | the kDataLoaded grey check lines (see "Grey swap") |
| T0-v2 | the copies map read (no map = FAIL), fill counts per type, and per copy: its fields equal its live source's now and its links point to copies where one exists, else to the source's own (`FormCopies.h` CheckCopies) |
| T1 | after the Stoke cast (`CastSpellImmediate`) the player has one more Stoke effect and the flag is on. T1 has 6 s of unpaused play (v2 fix I2); its PASS line gives the dispel-to-flag-off and cast times when it dispelled first. A Stoke buff the player already had is dispelled first and not given back; when it is still on 3 s after that dispel, T1 goes on with it (SKIP "cast not judged", v2 fix J2) so the later steps still run stoked and T8 dispels it (v2 fix H2) |
| T2 | both deer spawned (`PlaceObjectAtMe`) with 3D; the target's AI is off and its health at least 99% of 100000; the target (AI off as soon as its 3D is loaded) has come to rest (v2 fix H1: for 0.5 s it moved at most 5 units on the ground plane and 5 in height, each measured from where it last moved on that axis; it is spawned at the player's height and may fall first). After 8 s without that, a target whose height held for 0.5 s goes on with an INFO line (it may slide on a slope with its AI off); otherwise T2 fails. The T2 line says how far it moved during the wait and rests on the ground plane between Fireball's explosion radius + 100 (the runtime radius, v2 fix J2) and 1200 units from the player wherever the terrain put it (the line gives that distance; later steps read the actual positions). T6, T6-npc, E1b and E1c also check the actual 3D distances before casting (target to player, and to the caster for the caster's casts, at least the explosion radius + 100) and are SKIPped when one is short; T6-swap and T6-npc-swap then SKIP too. The caster's home is its planned spot, at the height it stands at in T2 while it is within 200 units of the spot, else at the target's rest height (the line says how far it had already wandered): before each of its casts it is moved back there when it wandered more than 200 units on the ground plane, and the cast waits for the next tick (v2 fix G2) |
| T3 | launched with a copy (v2 fix C2): the copy is grey by construction, so PASS when every effect shape of its 3D at the hook carries the grey palette path (already grey is the expected state; no 3D = FAIL), and T3-pointer and T3-tint are INFO "not judged: a copy". Without a copy: the player's Firebolt was recoloured and at least as many effect shapes carry the palette path as were recoloured (ratio logged; INFO "partial" when forcePalette left some out); with `projectile` false as well, all three are INFO "not judged" |
| T3-pointer | every palette material also kept our texture pointer (the property holds the engine's copy) |
| T3-tint | every recoloured base colour is grey (R=G=B) and, for at least one shape seen before the recolour and without a colour controller, equals its old luminance x tintScale; INFO instead when shapes were recoloured but every comparable one has a colour controller |
| T4-light | read on the live player projectile of T4, a tick or more after the tint: its light diffuse equals `lightColor` (INFO when it has no light or is gone) |
| T5 | judged only after the Firebolt's hit is confirmed by a TESHitEvent (the player and the target, with Firebolt as the source or its projectile - original or copy - as the projectile; v2 fixes C3, D3): T3's cast first, then after its flight time + 0.8 s without a hit the player casts again (not once our shader is seen), up to 3 casts, else FAIL `aim: no hit after 3 casts` listing every hit event on the target since the first cast and the geometry (player, target, its 3D bound, distance). Then: our shader appears on the target and the `GetHitEffectShader` hook recorded swapping the player's hit there; Firebolt's engine hit shaders are known and none of them is unfinished at 0.5 s or older in the second after (the line lists each engine shader with its age, and how long it checked) |
| T5-second | a second Firebolt, launched by T5 the moment our shader shows, is swapped too: a swap record for the target naming a NEW hit (controller and ActiveEffect address - v2 fix H3: not its `usUniqueID`, which is 0 on most effects; a hit recorded before the second cast asked again does not count); 800 ms later at least one of our shaders is alight, Firebolt's engine shaders are known and none is alight |
| T4 | the caster launches first, then the player, in the same frame (the caster turned to the target first; no explosion margin for this Firebolt, v2 fix I1; the player's projectile matched after T5-second's is on record): the caster's has effect shapes, none grey, and the player's was recoloured; with `projectile` false (the v2 default) the copies are judged instead: the caster's projectile has its original base and, when the player's launch should take a copy (copyLaunch on, Launch hooked, a copy of Firebolt's projectile), the player's has the copy base; judged on the tick after both records |
| T4-shared | while both projectiles still exist, inspected then: no effect property shared, none grey on the caster's (INFO "not comparable" when one was gone) |
| T5-npc | once the caster's projectile can have arrived (distance / projectile speed): the hook recorded a KEPT call (a fire effect someone else cast) for the target, and an engine Firebolt hit shader unfinished at 0.5 s or older that was not already on the target before T4 shows within 1.5 s (a sighting is the same shader only if its age grew with the unpaused clock, and a shader recorded finished never matches an unfinished one); INFO "cannot judge" without a kept record |
| T6 | Fireball's explosion from the player: when it is the copy (v2 fix C2), PASS when every effect shape of its 3D carries the grey palette path; else it is recoloured with the palette path on the recoloured shapes, and INFO "not judged" with `explosion` false (owner, cause, magicCaster current spell, light at Set3D logged). T6-npc needs effect shapes in the caster's explosion 3D, none grey |
| T6-npc | Fireball's explosion from the caster (owner or cause actor) has effect shapes, none grey, not recoloured |
| T3-swap | after T3 and T5-second, for the player's Firebolts: every mapped impact model requested inside an open window was handed out grey; INFO "no mapped model requested inside the window" (with the call sites hooked and chained) when none was asked for, or when `impactSwap` is off, no call site is hooked, or Firebolt's impact set has no model in the map |
| T6-swap | 1.2 s after the player's Fireball: the same judgement for the explosion's impacts |
| T6-hazard | INFO: grey hazard models handed out since the player's Fireball (no vanilla spell is known to leave a mapped hazard reliably, so the hazard swap is judged by this count and the log lines) |
| T6-npc-swap | the caster casts only 2.5 s after the player's Fireball (its explosion has ended); 1.2 s after the caster's Fireball: no grey impact model handed out (INFO when the player's side would not swap either) |
| T3-remote | the script-tag rule: a placed vanilla XMarker (a non-actor reference, only the test's prop), tagged as the native wrapper tags a Cast's akSource, casts Firebolt at the target with no blame actor; PASS only when the projectile it shot was the player's through the script tag and grey as T3 judges it (a copy: the palette path on every effect shape of its inspected 3D; no copy: recoloured). It tags the marker itself: the wrapper is not exercised here (T0-wrap checks it). Also logs each explosion's caster object reference |
| T3-blame | INFO: a second marker casts with the player as blame actor and no tag; reports whether the engine kept that blame on the marker's NonActorMagicCaster (the blame rule matched) |
| T-bound / T-placed | INFO "manual": vanilla has no bound weapon with a fire effect; no test script ships for script-placed fire |
| T7 / T7-player | INFO: the first colour modifier's keys (0x30-0x5F) and whether the layout looks plausible (all in 0..2, one above 0); with `particleColors`: every player modifier desaturated (T7-player); and, only when the caster's projectile was recorded before the player's, its modifiers neither shared, nor changed between its hook and T4, nor already equal to the desaturated keys of the player's T3 projectile before its recolour (T7; "not compared" when those had no colour, INFO when not comparable) |
| T8 / T8-cast | after T1 turned the buff on, one dispel (`MagicTarget::DispelEffect` only, from the main-thread queue; each effect's flags before and after logged) turns the flag off within 1 s. When it does not hold, a second dispel at 0.5 s restores the state for the later steps, but T8 is then a FAIL (v2 fix D3; E2 likewise). Then (skipped if the buff never came on or is still on) the next Firebolt has effect shapes, none grey, is not recoloured and has no copy, and T3 was judged grey (copy or recolour) The self-test's dispel (`SelfTestDispel.cpp`) scans the player's effect list again by the Stoke keyword for each dispel (v2 fix H3). Before H3 it found the effects again by `usUniqueID`, which is 0 on most effects, so every earlier self-test run dispelled one or two unrelated player effects and left the Stoke effect live (the "dispel did not hold" lines): do not keep saves made after those runs |
| T-threads | v2 fixes D1, E7: PASS only when the main-thread queue is installed, ran task bodies, all of them on `Main::threadID`, no two drains overlapped, nothing fell back to `AddTask`, and no job-side hook body (Launch, explosion creation, GetImpactData in a copy window, Explosion::Update in a copy window) was entered on a worker thread while a drain was running (v2 fix G1: counted online at each hook's entry over the whole session; only drains that ran tasks go into the trace ring) |
| T9 | cleanup (INFO) |
| T0-hooks | v2: the Launch and explosion creation detours hooked (or chained) with the bytes they found, and both GetImpactData call sites hooked; FAIL for a device that is wanted (config on, copies of that kind filled; the creation detour on AE only) but not hooked; INFO otherwise. Also the round-10 impact call sites, and the prologue replay decoder on fixed byte cases (Launch's and the creation's 1.7.104 prologues = 5, sub rsp imm8/imm32, mov rax/r11 rsp accepted; `48 8B C4 48 89 58 08` and an E9 refused) |
| T0-request | v2 expansion C1: with `writeRequest` on, PASS when closure forms are above 0, the fingerprint is computed, `request.json` is written, no closure form's or muzzle's NIF failed to read and the dump is unchanged or written in full (else FAIL); an ADDN record's NIF that failed (often another mod's missing mesh) is an INFO line with the count and the first five paths, not a FAIL; INFO when `writeRequest` is off; the line gives the counts, the NIF read time, the fingerprint and the dump state. Whether the closure is the right set is judged from `request.json` against the census, not here |
| T0-fresh | v2 expansion C5: PASS when the copies are fresh (section "Freshness"); FAIL for stale, none or pluginMissing, with why and how many closure forms have no copy |
| T0-cover | PASS when every copy-type form of this session's fire closure has a filled copy or was excluded with a reason; FAIL lists the first 20 without one (type, FormID, plugin, the spell that reached it and its plugin). INFO: per fire spell its costliest effect's projectile, dual cast projectile, explosion and casting art, each as copied of present; copies without a grey model (the builder kept the source's); copies no swap point uses (a casting art reached only from an effect that is not a costliest one, a hazard reached only as a non-fire spawn-hazard effect's form). T0-hooks also judges explosion creation, the SetCastingArt call site, the hazard creation detour and the SpawnHazardEffect window: PASS hooked; INFO when left off on purpose (the replay decoder refused the prologue, SE where the function is not verified, the SetCastingArt scan or its byte check) - except on 1.6.1170 / 1.7.104, where a wanted device that is not hooked is a FAIL whatever the note; a full trampoline, a jump into memory that is not executable or a missing id is a FAIL everywhere (fix G1) |
| T0-pred | v2: `IsFireLaunch` on vanilla props: Firebolt true, no spell/enchantment/weapon false, Sparks (`02DD2A`) false (INFO instead when Sparks counts as fire in the load order) |
| T0-v2 | (also) the synthetic GetImpactData call: the thunk of a hooked site called through the engine's lookup for the first impact of Firebolt's impact set that has a copy - the engine's impact with the window closed, the copy with it open |
| E4 | v2 S3: FAIL `aim: no hit after 3 casts` when T5 neither confirmed a hit nor saw our shader (v2 fixes C3, D3). PASS only when our EFSH was alight on the target after the player's Firebolt (T5's sighting). INFO lines: how often the Firebolt hit the target (TESHitEvent) and the 0x0D outcome counters for the player's effects (calls, swapped, engine answered null and with flags bit 1, not stoked, not a fire effect); the FAIL names which of the three cases it was (no hit, the thunk never reached, the engine answered null, or our own check kept the engine's shader) |
| E1a | v2: the player's Firebolt was launched with its projectile copy (launch record), its 3D at the hook carries the grey palette path, every AddOnNode value the hook recorded with that 3D inspection is a grey ADDN index (no 3D at the hook = FAIL; no AddOnNode = INFO, not judged), and its impact took at least one impact copy (counter) within 1.5 s of the hit. The hit is confirmed first like T5's (TESHitEvent, up to 3 casts, else FAIL `aim: no hit after 3 casts`); after a recast the launch, projectile and impact counters are taken from that cast, so the cast that hit is judged. A missing record is a FAIL |
| E1b | v2: the player's Fireball explosion is the explosion copy with the palette path; INFO which device did it from the counters (Launch post-write, the creation detour seeing a copy already, or the creation detour swapping), with the self-area swaps, rejected create data, non-spell self-area slots and player launches without a copy |
| E1c | v2: the caster's Fireball before the player's (A), 2.5 s after it (B) and in the same frame (C): each phase needs the caster's launch, projectile and explosion each seen at least once, with original bases, effect shapes in their 3D (observed) and none of them grey or with the palette path; in C the player's explosion is the copy. The caster is moved back home before each of its casts (v2 fix C3) |
| E1m | v2 expansion C5: a modded fire spell picked by data - aimed delivery, its costliest effect's projectile has a copy, its plugin last in the load order (a plugin not in the order is skipped; INFO when only the official masters qualify; target-location spells such as runes are left out, their projectile lands and no hit on the target confirms it). After an earlier explosion has settled (2.5 s), the player launches it at the target and the hit is confirmed as in E1a (up to three casts, clocks shifted by paused time). PASS when the launch took the projectile copy and, when the engine's explosion (the costliest effect's MGEF explosion when that effect has an area, else the projectile's) has a copy, that very copy was seen within 1.5 s of the hit. Step limit 12 s (fix F11) |
| E2 | v2: a Flames-type spell (MAG_Flames when its projectile is a flamethrower, else the Flame Atronach's Flames `07E881`) launched by the player every tick at a deer 400 units ahead: 1 s stoked, the buff dispelled mid-stream, 0.6 s more, the buff cast again. PASS: every stoked launch took the copy, launches after the flag went off took the original, and a TESHitEvent from the player with that spell came while stoked (the damage path runs with the copy). INFO: distinct streams before and after (one each = the stream held) |
| E6 | v2 ARTO spike: a fire spell picked by data - cast fire and forget, with a Cloak-archetype fire effect whose hit art (the MGEF's RFCT art when set, else its `hitEffectArt`) has an ARTO copy; of those the spell whose file comes last in the load order (SKIP when there is none, or `copyHitArt` is off). The player casts it on itself while stoked; PASS when within 2 s a live ModelReferenceEffect on the player has the copy as its art, its 3D is loaded with every effect shape on the grey palette path, no new effect has the original art, and `GetHitEffectArt` returned the copy since the cast. INFO, not FAIL: the original art already on the player before the cast (another cloak), and a copy NIF with no effect shape (its grey not judged). That one line settles both spike questions: the engine takes the art the hook returns, and loads an ARTO record without DATA. The cloak is dispelled right after. Step limit 6 s |
| E6-drop | after that dispel the copy art is gone from the player within 2 s |
| E6-npc | the same spell cast by the deer caster on itself: PASS when its art effect has the original art and none has the copy; the cloak is dispelled right after. T9 dispels E6's cloak and its associated spell on every actor in high process again, whatever path E6 took |
| E5 | v2 expansion C4/C5: `PickCastingArtImpl` called directly for a fire spell whose costliest effect's casting art has a copy: player right hand stoked -> copy, not stoked -> original, the power slot -> original, the deer -> original, another effect's casting art of the same spell -> unchanged; a case that cannot run (no deer, no other casting art) says "not run" and stays out of the PASS (fix F12). SKIP without such a spell. INFO first: whether the SetCastingArt site is hooked and this session's calls, swaps and rejections. E5 checks the rule only; the hand art in game is judged from the SetCastingArt log lines |
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
the player's). Actors are never tagged. With `explosion` or `hazard` on (both off by default since v2), a placed reference is recoloured
once its 3D exists (within 5 s) only when its base is a fire explosion or hazard, or a visual-only
kind (activator, moveable static, static, art object); other kinds are left (logged once per form
type). No script, mod or FormID is named. The first native calls log the frames they saw
(`native wrapper frames`).

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
