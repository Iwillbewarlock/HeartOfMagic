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
| `forcePalette` | `true` | Effect shapes without the grey-to-palette colour flag get the flag, so their own texture brightness is mapped through the grey ramp. Off: such shapes only get the desaturated base colour |
| `particleColors` | `false` | Also desaturate `BSPSysSimpleColorModifier` colour keys in the player's recoloured 3D (layout not in CommonLib, from the Jonahex fork, unverified on AE; see the self-test step T7) |
| `selfTest` | `false` | Run the scripted self-test once after a save is loaded (section "Self-test") |
| `tintScale` | `0.6` | Base colour RGB = luminance x tintScale (0..2), alpha kept |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |

## How it works

**Fire spells and fire effects** (built once at kDataLoaded; v2 rule, user decision 2026-10-16): a spell
is fire when one of its effects is a fire MGEF - the vanilla `MagicDamageFire` keyword (Skyrim.esm 0x01CEAD)
or fire resistance as its resist value - OR when Heart of Magic's `spell_catalog.json` tags it "fire". Only
`SpellType::kSpell` counts. A fire-MGEF spell's fire effects are its fire MGEFs; a catalog-only fire spell's
fire effects are all of its effects (it has no fire MGEF to pick; e.g. Vigilant's Script-archetype Blinding
Fire). The v2 copies change only visuals, so a catalog false positive costs only a grey look (this reverses
round 8b, where the catalog tag alone did nothing). The log says how many spells are fire by the tag alone. The
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
| 1 hit shader and hit art | `ActiveEffectReferenceEffectController` vtable: `GetHitEffectShader` (0x0D) and `GetHitEffectArt` (0x0C), where the engine asks an active effect which hit shader and art to put on its target. While stoked, for an effect the player cast (caster handle) whose MGEF is a fire effect of a fire spell (scrolls, staffs and enchantments sharing the MGEF do not count): the shader call returns `HoMBF_BurnShader` instead of the engine's, so the engine makes the hit shader from ours (instant hits, concentration and cloaks alike; no event, no record changed, nothing to finish); the art call returns the engine's art, and with `hitArt` on the art's 3D (`ModelReferenceEffect::artObject3D`) is recoloured on a later tick once it exists (up to 2 s; matched on controller and art only - the engine's first art call comes before it sets the target - with the live controller re-checked as the player's fire effect, and the target read from the live effect). The first 20 calls of each slot, on any effect and on a stoked player's fire effect, are logged with the controller, how many times it was asked, the effect, the target and the thread |
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
(`PROJ`, `EXPL`, `IPCT`, `HAZD`; no IPDS or LIGH copies), `editorId`, `model` (as the record stores it), `bounds`
(6 integers; not on IPCT) and `data` (the scalar DATA values; unknown keys are refused, missing ones are 0; PROJ
`projectileType` takes a name or the raw bit, PROJ `soundLevel` goes to VNAM). `--request` is the ADDN request
of "Grey duplicates": the copies' models count for the AddOnNode values that need a grey ADDN index (no remapped
copy of them is made), so one run writes everything a setup needs into `--out`:

| Output | What |
|---|---|
| `HeartOfMagic_BlackFlame_Copies.esp` | ESL, no masters, HEDR 1.70, form version 44, FormIDs 0x800 up: one placeholder per source - EDID `HoMBF_Copy_<editorId>_<localId>`, OBND (PROJ, HAZD, EXPL), MODL = the grey copy, DATA in Skyrim.esm's size for the type (PROJ 92, HAZD 40, EXPL 52, IPCT 24) with the scalars and every form link 0, VNAM on PROJ. Groups PROJ, HAZD, EXPL, IPCT. Read back and compared after writing |
| `meshes\heartofmagic\blackflame\copies\<model path>` | the full-grey copy of each distinct source NIF: every effect shader on the grey palette (as the grey MPS) and every AddOnNode on its grey ADDN index |
| `SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json` | the copies map (version 1): source -> copy id, type, models. The next run reads it back from `--out`: a source keeps its id, a new one takes the id after every id used, a source no longer requested is kept as `retired` and its id is never given to another (a saved projectile or hazard of that copy must not turn into another form) |
| grey MPS, `HeartOfMagic_BlackFlame_greymap.json`, `HeartOfMagic_BlackFlame_Local.esp` | as `--request` writes them (the copies' AddOnNode indices included) |

Install the output folder as a mod after this one and enable both ESLs. The spike's requests are hand-made from
read-only houseCARL reads of the TAKEALOOK profile: [requests/forms-takealook.json](requests/forms-takealook.json)
(33 forms: the winning PROJ, EXPL, IPCT and HAZD reached from vanilla Firebolt, Fireball, Flames and Fire Rune) and
[requests/addons-takealook.json](requests/addons-takealook.json) (the profile's 329 ADDN records). Their NIFs are
other mods' files (Flame VFX Edit, Embers XD - CS, CS Particle Patch), so the output is built on the user's machine
and not committed; the spike run used `--in` Flame VFX Edit, Dragon Breath VFX Edit, Embers XD - CS, Embers XD, then
the CS Particle Patch, Inferno, Particle Patch and vanilla Meshes0/1 BSAs extracted to scratch (33 copies, 9 copy
NIFs, 7 extra grey ADDN records for indices 5, 6, 34, 35, 80, 81 and Embers XD's 225). Planned: the DLL writes the
forms request at kDataLoaded from the fire set's closure.

### Fill (S1, DLL): `FormCopies.h`

At kDataLoaded only, before any hook reads them, each copy is filled from the LIVE winning source (so SkyPatcher and
other runtime edits made before then are included), data members only, never the form header, flags or the copy's
own model: PROJ `data` (0x80-0x108), `soundLevel`, `fullName`, `muzzleFlashModel` through `TESModel::SetModel`,
`boundData`; EXPL `data` (0x98-0xE8), the enchantment members, `imageSpaceModifying`, `fullName`, `boundData`; IPCT
`data`, both decal texture sets, both sounds, `hazard`, `dData`; HAZD `data`, `imageSpaceModifying`, `fullName`,
`boundData`. Data structs are copied byte for byte (their layouts are asserted by CommonLib). A second pass points
only the links that have a copy at it: PROJ `explosionType`, EXPL `impactPlacedObject` and `spawnProjectile`, IPCT
`hazard`; every other link (light, sounds, texture sets, image space, enchantment, hazard spell, impact data sets)
stays the source's. A copy whose source or copy form is missing, or whose types differ, is left out of the map and
logged. The fire set treats a copy as its source (`IsFireExplosion`, `IsFireHazard`). Self-test T0-v2 compares every
copy with its live source again.

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
| explosion creation (SE 42657 / AE 43829) prologue detour | `copyExplosionCreate` | the create data's base (+0x00) set to its copy when its source projectile (+0x20, a projectile reference by form type) has a copy as its base, or - no projectile, the self-area cast - when its caster (+0x38, an ActorMagicCaster by vtable) is the player's and its spell (+0x28, a SpellItem by form type) is fire. Restored after the call. A base that is already a copy is counted apart, so the self-test tells which device made a Fireball's explosion grey |
| `GetImpactData` (SE 20408 / AE 20860) calls in `SpawnCollisionEffects` (43022 / 44213) and the explosion's impact function (42679 / 43851) | `copyImpacts` | inside a copy window the impact copy is returned for the impact the set would return (no IPDS copy, no hash map insert). The window opens on the impact call sites (section "Grey swap") when the projectile's base is a copy, and in `Explosion::Update` when the explosion's base is a copy. The round-10 model window (GetModel swap) stays for projectiles and explosions without a copy |

**Detours.** The first 16 bytes are read at runtime. An `E9` there (another plugin's detour, e.g. IDRC hooks
Launch's prologue) or an `FF 25` absolute jump is chained onto: our thunk calls its target. Otherwise the leading
instructions are replayed in a gateway only when every one is a position-independent form the decoder knows (push,
REX push, `mov [rsp+disp8], r64`, `sub rsp, imm`, `mov rax/r11, rsp`) covering 5 bytes or more; anything else is
left alone and logged with its bytes. 1.7.104 code (readable): Launch starts `48 89 4C 24 08` (one 5-byte
instruction), explosion creation `40 53 55 56 57` (four pushes). The GetImpactData sites are found by scanning each
caller's function-table range for its one `E8`; only SpawnCollisionEffects has a fallback offset (+0xBC, AE, for a
call another plugin rewrote): the explosion function differs between 1.7.104 and 1.6.1170 (see the ids table).
Trampoline: 512 bytes (158 at most used). Counters per device are in `GetCopySwapCounters` and the self-test.

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
- Threads: SKSE tasks run in the SKSE task phase of Main::Update (main thread, per SKSE 2.2.8 Hooks_Threads.cpp); kDataLoaded runs on an init thread. The game thread is stamped by the first SKSE task (queued at `SKSEPlugin_Load`), which logs `taskThread=`, `loadThread=` and `Main::threadID=` in one line. Log lines that print a stored thread label it (`hookThread=`, `callerThread=`, `gameThread=`), so the log prefix's own thread id is not confused with it. Lists shared between tasks, the message handler and hooks are under mutexes. Since v2 a pending script tag is judged (`ResolvePending`) from any task or ticker body whatever thread runs it - run 2 showed SKSE task bodies on several threads - under the same VM handle policy checks; its first runs log their thread.
- A pending script tag (a wrapped call on a VM thread) is judged by a task right after the call; if the calling effect has already ended by then, it leaves no tag. A second pending caller on a live pending tag is dropped (the first is kept until judged), so the player's tag can be lost when another effect's pending call on the same reference came first. A projectile decided by the deferred path (only a pending tag matched at its hook) does not log the material check. The explosion side of the impact swap lives in the Explosion Update hook (installed whatever `explosion` says since v2). The hazard swap has no PASS/FAIL step: T6-hazard counts, the log lines tell.
- The grey swap is not run in game yet: whether the grey copies draw grey, whether an impact hazard is created inside the impact window (log `loaded inside the player's impact window`), and the 1.6.1170 code offsets are open.
- Diagnostic build: every step runs under an SEH guard. A fault (access violation and the like) logs the step, code and module+offset and switches that step off for the session (later calls counted); restores are the exception, guarded item by item and never switched off. A fault inside engine code that holds an engine lock may hang the game rather than crash it; the first `hardware exception` line names the step.

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
| T0-wrap | each wrapped native: the script type holds our proxy (the real check, "bound"); the proxy's parameter count and types, static, latent and tasklet flags are the values copied from the original in its constructor (no game script needed) |
| T0-grey | the kDataLoaded grey check lines (see "Grey swap") |
| T0-v2 | the copies map read (no map = FAIL), fill counts per type, and per copy: its fields equal its live source's now and its links point to copies where one exists, else to the source's own (`FormCopies.h` CheckCopies) |
| T1 | after the Stoke cast (`CastSpellImmediate`) the player has one more Stoke effect and the flag is on. A Stoke buff the player already had is dispelled first and not given back |
| T2 | both deer spawned (`PlaceObjectAtMe`) with 3D; the target's AI is off and its health at least 99% of 100000; target-to-player and target-to-caster distances are beyond the explosion radius + 100 |
| T3 | the player's Firebolt was recoloured and at least as many effect shapes carry the palette path as were recoloured (ratio logged; INFO "partial" when forcePalette left some out). With `projectile` false (the v2 default) T3, T3-pointer and T3-tint are INFO "not judged": the grey copy is judged by E1a |
| T3-pointer | every palette material also kept our texture pointer (the property holds the engine's copy) |
| T3-tint | every recoloured base colour is grey (R=G=B) and, for at least one shape seen before the recolour and without a colour controller, equals its old luminance x tintScale; INFO instead when shapes were recoloured but every comparable one has a colour controller |
| T4-light | read on the live player projectile of T4, a tick or more after the tint: its light diffuse equals `lightColor` (INFO when it has no light or is gone) |
| T5 | our shader appears on the target and the `GetHitEffectShader` hook recorded swapping the player's hit there; Firebolt's engine hit shaders are known and none of them is unfinished at 0.5 s or older in the second after (the line lists each engine shader with its age, and how long it checked) |
| T5-second | a second Firebolt, launched by T5 the moment our shader shows, is swapped too: a swap record for the target naming a NEW hit (controller and active effect id; a hit recorded before the second cast asked again does not count); 800 ms later at least one of our shaders is alight, Firebolt's engine shaders are known and none is alight |
| T4 | the caster launches first, then the player, in the same frame (the caster turned to the target and every distance re-checked first; the player's projectile matched after T5-second's is on record): the caster's has effect shapes, none grey, and the player's was recoloured (not required with `projectile` false); judged on the tick after both records |
| T4-shared | while both projectiles still exist, inspected then: no effect property shared, none grey on the caster's (INFO "not comparable" when one was gone) |
| T5-npc | once the caster's projectile can have arrived (distance / projectile speed): the hook recorded a KEPT call (a fire effect someone else cast) for the target, and an engine Firebolt hit shader unfinished at 0.5 s or older that was not already on the target before T4 shows within 1.5 s (a sighting is the same shader only if its age grew with the unpaused clock, and a shader recorded finished never matches an unfinished one); INFO "cannot judge" without a kept record |
| T6 | Fireball's explosion from the player is recoloured with the palette path on the recoloured shapes (owner, cause, magicCaster current spell, light at Set3D logged); INFO "not judged" with `explosion` false (the v2 default) |
| T6-npc | Fireball's explosion from the caster (owner or cause actor) has effect shapes, none grey, not recoloured |
| T3-swap | after T3 and T5-second, for the player's Firebolts: every mapped impact model requested inside an open window was handed out grey; INFO "no mapped model requested inside the window" (with the call sites hooked and chained) when none was asked for, or when `impactSwap` is off, no call site is hooked, or Firebolt's impact set has no model in the map |
| T6-swap | 1.2 s after the player's Fireball: the same judgement for the explosion's impacts |
| T6-hazard | INFO: grey hazard models handed out since the player's Fireball (no vanilla spell is known to leave a mapped hazard reliably, so the hazard swap is judged by this count and the log lines) |
| T6-npc-swap | the caster casts only 2.5 s after the player's Fireball (its explosion has ended); 1.2 s after the caster's Fireball: no grey impact model handed out (INFO when the player's side would not swap either) |
| T3-remote | the script-tag rule: a placed vanilla XMarker (a non-actor reference, only the test's prop), tagged as the native wrapper tags a Cast's akSource, casts Firebolt at the target with no blame actor; PASS only when the projectile it shot was the player's through the script tag and recoloured (recolour not required with `projectile` false). It tags the marker itself: the wrapper is not exercised here (T0-wrap checks it). Also logs each explosion's caster object reference |
| T3-blame | INFO: a second marker casts with the player as blame actor and no tag; reports whether the engine kept that blame on the marker's NonActorMagicCaster (the blame rule matched) |
| T-bound / T-placed | INFO "manual": vanilla has no bound weapon with a fire effect; no test script ships for script-placed fire |
| T7 / T7-player | INFO: the first colour modifier's keys (0x30-0x5F) and whether the layout looks plausible (all in 0..2, one above 0); with `particleColors`: every player modifier desaturated (T7-player); and, only when the caster's projectile was recorded before the player's, its modifiers neither shared, nor changed between its hook and T4, nor already equal to the desaturated keys of the player's T3 projectile before its recolour (T7; "not compared" when those had no colour, INFO when not comparable) |
| T8 / T8-cast | after T1 turned the buff on, dispelling it turns the flag off within 1 s; then (skipped if the buff never came on or is still on) the next Firebolt has effect shapes, none grey, not recoloured, and T3 had recoloured (not required with `projectile` false) |
| T-threads | v2 S4, measurement only: every QueueTask body and every v2 hook body records (kind, thread, start, end) in a 4096-entry ring (`ThreadTrace.h`); the step lists per kind the threads seen (and how many on the stamped game thread) and the overlapping intervals of different threads per kind pair. PASS when at least one body was recorded (0 = FAIL: nothing was measured). No gate depends on it |
| T9 | cleanup (INFO) |

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
