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
4. Log: `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_BlackFlame.log` (every line is
   written at once).

## Config (`SKSE/Plugins/HeartOfMagic_BlackFlame.json`)

A missing file, bad JSON or a bad value keeps the default for what was wrong and logs why.

| Key | Default | What |
|---|---|---|
| `burnShader` | `true` | Layer 1: grey burn shader on targets of the player's fire spells |
| `burnMode` | `"replace"` | `"replace"`: finish the engine's hit shader, then apply the grey one. `"overlay"`: leave the engine's hit shader and apply the grey one on top. Anything else: `"replace"` with a warning |
| `projectile` | `true` | Layer 2: recolour the player's fire projectiles |
| `explosion` | `true` | Layer 3: recolour the player's fire explosions |
| `handArt` | `true` | Layer 4: recolour the casting art in the player's hands |
| `light` | `true` | Tint projectile, explosion and casting lights to `lightColor` |
| `forcePalette` | `true` | Effect shapes without the grey-to-palette colour flag get the flag, so their own texture brightness is mapped through the grey ramp. Off: such shapes only get the desaturated base colour |
| `particleColors` | `false` | Also desaturate `BSPSysSimpleColorModifier` colour keys in the player's recoloured 3D (layout not in CommonLib, from the Jonahex fork, unverified on AE; see the self-test step T7) |
| `selfTest` | `false` | Run the scripted self-test once after a save is loaded (section "Self-test") |
| `tintScale` | `0.6` | Base colour RGB = luminance x tintScale (0..2), alpha kept |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |

## How it works

**Fire spells** (built once at kDataLoaded): a spell is fire when Heart of Magic's
`Data/SKSE/Plugins/SpellLearning/spell_catalog.json` lists `"fire"` in its `elements`; a spell the
catalog does not list is fire when one of its effects has the vanilla `MagicDamageFire` keyword
(Skyrim.esm 0x01CEAD). Without a catalog it is keyword only, with a warning. Only `SpellType::kSpell`
counts. The fire explosions are those named by fire spells' effects and their projectiles. The log
gives the counts. No mesh, node, mod, plugin or FormID is special-cased: vanilla meshes, VFX replacers,
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
| 1 burn shader | `TESActiveEffectApplyRemoveEvent` -> game-thread task: finish the MGEF's hit shader in the effect's `hitEffects` (replace mode), apply `HoMBF_BurnShader` for the remaining time (1.5 s for an instant hit), not stacked on a live grey shader |
| 2 projectile | `Handle3DLoaded` (vfunc 0xC0) on the Missile, Flame, Cone, Beam, Grenade and Barrier vtables; shooter the player, spell fire |
| 3 explosion | `Set3D` (vfunc 0x6C) on the Explosion vtable; base form in the fire explosion set, owner or cause actor the player |
| 4 hand art | ticker task every 100 ms while stoked: per hand casting a fire spell, the caster's `magicNode`, the hand's magic node by name in the 1st and 3rd person skeletons, the player's casting-art `ModelReferenceEffect`s; the caster's light tinted |

## Known limits

- Grey-black, not pitch black: the blending stays additive.
- Death flames (an actor burning to death) stay orange: hardcoded in the engine (Black Flame author's note).
- Scorch decals, impact data sets and hazards placed by spells or explosions (e.g. Flare Storm) are unchanged.
- Hit-effect art (e.g. Flame Cloak's `FXFireCloak01`) is not recoloured; only the hit shader is (layer 1).
- Glow-map cores on lighting shaders (e.g. Vulcano's Heartstone) are not touched: `BSLightingShaderProperty` emissive colour may be shared.
- Replace mode swaps the burn shader after the engine applied its own: concentration spells (Flames) may flicker orange for a frame.
- A `BSEffectShaderPropertyColorController` animates a property's colour every frame and overrides the base colour change; the log counts such shapes. Particle colour modifiers multiply the palette, so some orange can stay unless `particleColors` is on.
- Shapes without the palette flag, with `forcePalette` off, keep their texture's colour (examples from vanilla paths: `PCoreCircularFlare`, `RedGlowAdd` on bolt/ball projectiles, `pGlow`/`ppLight` on hand effects).
- Staffs, scrolls, enchantments and powers are excluded (`SpellType::kSpell` only).
- Heart of Magic's tree leaves the Stoke spell out (no tome teaches it); the prototype gives it by console.
- The recolour does not call `SetupGeometry`. If a recoloured shape does not show the palette in game, a missing `SetupGeometry`/`FinishSetupGeometry` (render pass state) is the first suspect.
- Which thread calls `Handle3DLoaded` and `Explosion::Set3D` was not known at build time: off the game thread the recolour is deferred by handle, and the log says which it was.

## Self-test

The DLL can check most of itself in game, from the engine's own data, and write the results to a file.

1. Use a **throwaway save** in an open exterior area: the test casts spells (Heart of Magic may grant XP
   for them), spawns two vanilla actors in front of the player and deletes them at the end.
2. Set `"selfTest": true` in `HeartOfMagic_BlackFlame.json` (the Stoke spell does not need to be known).
3. Load the save and wait about 30 s without opening a menu. It starts 3 s after the player is in a loaded
   cell with the game unpaused, and runs once per game session.
4. Read `Documents/My Games/Skyrim Special Edition/SKSE/HeartOfMagic_BlackFlame_selftest.txt` (the same
   lines are in `HeartOfMagic_BlackFlame.log`): `[SELFTEST] PASS|FAIL|INFO <id> <detail>`, ending with
   `[SELFTEST] DONE pass=N fail=M info=K`. A step that times out is a FAIL that says what it waited for.
5. Set `"selfTest": false` again.

Vanilla forms it uses (Skyrim.esm, read with houseCARL): Firebolt `012FD0`, Fireball `01C789`, target
`EncDeer 0CF89D` (unaggressive, no crime faction, so hitting it is no crime), NPC caster `WEFarmerFemale
1034E6` (unaggressive). Both get their AI switched off; the target's health is raised to 100000.

| Step | Checks |
|---|---|
| T0 | game thread id, palette loaded (which path), ESP forms, fire spell counts, config; Firebolt and Fireball count as fire |
| T1 | the player casts the Stoke spell on itself (`CastSpellImmediate`): the buff flag is on within 1 s |
| T2 | the two actors spawn (`PlaceObjectAtMe`), their 3D loads, AI off, target health raised |
| T3 | the player's Firebolt: the projectile layer recoloured at least one effect shader and every recoloured material carries the palette pointer and path; counts, hook thread, recolour time in us |
| T5 | after T3: a live grey shader on the target; in replace mode no live engine hit shader of Firebolt's effect; what the burn layer recorded |
| T4 | the NPC and the player cast Firebolt in the same frame: the NPC's projectile carries no palette material and shares no effect property with the player's live one; whether their materials were pooled before any recolour (INFO) |
| T5-npc | the target's shaders after T4 (INFO) |
| T6 / T6-npc | the player's Fireball explosion is attributed to the player and recoloured (owner, cause actor, runtime magicCaster->currentSpell, light at Set3D reported); the NPC's is not recoloured |
| T7 | the first BSPSysSimpleColorModifier's raw bytes 0x30-0x7F (layout sanity, INFO); with `particleColors` on, the NPC's modifiers are not the player's and not desaturated |
| T8 / T8-cast | dispelling the Stoke effect turns the flag off within 1 s; the next Firebolt is not recoloured |
| T9 | the two actors are disabled and marked deleted |

## In-game checks the self-test cannot do

Only what needs eyes or hands. Steps 1-3 set up a manual session.

1. `help 흑염 4` (or the spell's runtime id from the log line `spell HoMBF_StokeSpell ... = FExxx803`), then
   `player.addspell <id>`, and cast it.
2. **The on-screen look** of each layer (the self-test sees materials and shaders, not pixels): grey burn,
   grey Firebolt, grey Fireball explosion; flame bodies forced onto the palette (`forcePalette`) keep their
   shape (a blank or white-box shape: set `forcePalette` false and report the mesh); flame edges do not turn
   hard or blocky (the palette's alpha ramp drives shapes counted `with palette alpha`).
3. **Hand art in 1st and 3rd person**: `CastSpellImmediate` has no charge animation, so only a real charge
   shows the casting art. The log line `hand art: ...` says which places had effect geometry. A `(more
   geometry)` line repeating every 100 ms means the "already grey" check does not hold: report it.
4. **Concentration flicker** (Flames, replace mode): a one-frame orange flash is a look, not data.
5. **Overlay mode look** (`burnMode` `"overlay"`): does the membrane show black (the engine may draw only
   the top-most membrane), and do the original shader's orange particles stay visible?
6. **Casting light**: a 10 Hz orange/tint flicker (the engine rewriting the light every frame) shows only on
   screen.
7. **Frame time feel** while casting with the buff on: no hitch on cast or impact.
8. **Save and load with the buff active**: the self-test runs on load and dispels the buff itself, so this
   stays manual: log `buff flag ON (game loaded)` or `(tick)`.
