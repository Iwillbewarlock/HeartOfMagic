# Black Flame - "갈망" (Longing) prototype

An SKSE plugin plus a small ESL. The player casts the self buff **갈망** ("Longing",
60 s), or wears the lantern (section "Lantern"). While it lasts, the visuals of every fire spell the **player** casts turn grey-black: the burn
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
| `data/HeartOfMagic_BlackFlame.esp` | ESL (master Skyrim.esm; houseCARL dropped the unused Update.esm master when it added the ADDN records): EFSH `HoMBF_BurnShader` 0x800 (vanilla FireFXShader with both palettes grey; its edge effect colour, DATA +0x38, was the vanilla orange 146,65,5 and is grey 89,89,89 since 2026-10-19 - the grey of the same relative luminance, sRGB linear light; its other colour keys were already grey or white), KYWD `HoMBF_StokedKeyword` 0x801, MGEF `HoMBF_StokeEffect` 0x802, SPEL `HoMBF_StokeSpell` "갈망" 0x803, ADDN `HoMBF_GreyAddon_*` 0x804-0x80C (section "Grey duplicates") |
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
| `devAutoLoad` | absent | **Dev only** (not in the shipped json or the tester package; ignored unless `selfTest` or `testerMode` is on): a save name without extension, loaded once, 5 s after the main menu first opens in a session, unless a load has started by then (`DevAutomation.h`); with `singleLaunch` it also waits until the slot publication settled (the open groups' upgrades recorded too), 5 minutes at most |
| `devAutoE3` | `false` | **Dev only** (same rules): when E3 asks for a save and reload, the plugin waits 3 s, saves `HoMBF_E3`, waits for SKSE's save message plus 3 s (10 s at most without it) and loads `HoMBF_E3`; E3 judges that load as it judges a player's |
| `devAutoQuit` | `false` | **Dev only** (same rules): the game quits (`Main::quitGame`) 10 s after the self-test's DONE, or after DONE E3 when E3 armed; 12 minutes (6 before 2026-10-18: the main run's step caps alone add up to 270 s of play) after the last load (or the auto load's request, or the main menu's first opening when `devAutoLoad` is set: the kDataLoaded-to-menu time does not count; 20 minutes after kDataLoaded when the main menu never opens) without that, it quits anyway and logs TIMEOUT |
| `selfTestDelaySeconds` | `30` | Seconds of unpaused play (player in a loaded cell, no menu, not loading) after the load before the self-test's first step (0..600); E3 judges 1.5 s of ready play after its reload instead (fix H5) |
| `lightColor` | `[0.25, 0.22, 0.28]` | RGB 0..1 for the tinted lights |
| `singleLaunch` | `false` | The single-launch route (section "Single launch: the DLL side"): the slot pool, the ledger, the builder started by the game, publication at kDataLoaded and at the main menu. Off: the copies map route as above. Off by default because the route has not run in game and needs P1's slot ESL and builder |
| `launchBuilder` | `true` | With `singleLaunch`: start the builder at kDataLoaded (`false`: only copies already built are published) |
| `slotPreflight` | `false` | With `singleLaunch`: at kPreLoadGame read the save's co-save from disk and refill the slots whose pairs differ (their data only, no SetModel) before the engine reads the save. Off: only logged (the message's save name format and refilling at that point are unverified in game). On or off, a co-save that is there but cannot be read, or whose BFSL record does not read, closes every slot gate for the session (log and HUD line); a save without a Black Flame block (made before the mod or the slot route) is normal |
| `slotTxst` | `false` | With `singleLaunch`: compute TXST keys, ask the builder for TXST copies in request v5, and publish the TXST slots like the other members of their groups (W1 from the ledger, W2 as their lines come): TX00 / TX03 set to the builder's grey copies (`SlotTxst.h`), the gate opened, and the PROJ / IPCT copies' decal links then point at the slot. Off, no TXST build is requested and the decals keep the source's sets |
| `cleanOldCopies` | `true` | With `singleLaunch`: once a session, at the main menu after publication settled (the ledger read whole, no ledger write of the session failed on main or the mirror, the builder ended and its lines read, the ledger written, no load yet), remove the `copies\<key16>\` folders under Data's and staging's `meshes|textures\heartofmagic\blackflame\copies\` that nothing keeps: kept are every key a ledger object (published or previous, its file deps, a TXST's texture strings) of this ledger or of any ledger mirror in the log folder stands on, every key of every ledger epoch's keep set file (`staging\keepsets\<epoch>.json`, so MO2 profiles that share one mirror name still keep theirs; a registered epoch without one stops the cleanup), what the slots were published with, and every kept key and this session's request keys with what their progress lines need (`OldCopies.h`, `OldCopiesRule.h`, `OldCopiesKeepsets.h`). Only 16-lower-case-hex folders holding plain files, no links on the way down from Data or staging, older than `oldCopiesGraceDays`, the staging folder before the Data folder, at most 2000 a session, stopped by a load; under MO2 a Data folder only when its files' real paths are under the overwrite folder (else counted only). One summary line `old copies: ...`. `false`: the same counts logged as a dry run, nothing removed |
| `oldCopiesGraceDays` | `14` | With `cleanOldCopies`: a key folder whose newest file (Data or staging) was written within this many days is kept (1..3650; 0 or another value keeps the default, logged: a grace of at least a day) |
| `selfTestSlots` | `false` | With `selfTest` and `singleLaunch`: the self-test steps T0-slots and T0-W2 |
| `tint`, `tintAdd` | `0.6`, `0.9` | With `singleLaunch`: the copies' base colour scales (0..2; another value keeps the default, logged), quantised to 6 decimals into the keys and request.json v5 (the builder takes them only from there) |
| `tintAddHands` | `1.4` | With `singleLaunch`: casting-art objects only (ARTO artType 0), clamped to 0..2 and quantised to 6 decimals. Palette copies get a dark-low-index / white-core ramp (clipped at 1); non-palette additive shapes keep the ordinary glow rules. The per-object request field and NIF/palette keys carry it; other keys stay unchanged. See [casting-art hand flames](docs/casting-art-hands.md). |
| `handMarbleOpacity` | `0.9` | Casting-art dark flame layer peak opacity, also capping dark ember opacity at min(0.9, this value); clamped to 0..1 and quantised to 6 decimals. Zero leaves the hand ramp but disables marble/dark ember systems and the bright ember multiplier. Included in that object's request and NIF key only. |
| `handMarbleScale` | `1.15` | Casting-art dark flame particle size relative to its additive source; clamped to 0.1..4 and quantised to 6 decimals. Included in that object's request and NIF key only. |
| `paletteMode` | `"derived"` | With `singleLaunch`: `"derived"` or `"godskin"`, into the keys and request.json v5 |
| `greyCorpseShaders` | `true` | Run 8 probe (`CorpseMark.h`, AE 1.6.1170 only): a Papyrus-applied effect shader (`EffectShader.Play`, an OwnedController: FEC's, but any other mod's or script's too) started on a dying or dead actor the player killed, whose last elemental hit (fire, frost or shock) was the player's stoked fire within `corpseWindowGameSeconds`, gets ShaderReferenceEffect flags bit 24, and at attach its NAM9 / NAM8 palettes are swapped for the builder's grey ones (`CorpseGrey.h`) - only the palettes that passed the builder's fire test have a grey copy, so a non-fire shader on that corpse keeps its own. `false`: the decision is logged only, no bit, and the attach hooks forward unchanged (a bit saved earlier is ignored) |
| `corpseWindowGameSeconds` | `45` | The corpse mark's window (1..3600): unpaused game seconds (`GameClock.h`) after that last stoked hit; each mark keeps the target's window open this long again |
| `builderJobMemoryMiB` | `1536` | With `singleLaunch`: the builder's job memory cap (256..16384), also passed as `--job-memory` |
| `lanternPlace` | `true` | Place the lantern and the smoke on the table (UpperTable01) of Ansilvund02 (section "Lantern"). Off for the session without the main-thread queue |
| `lanternDamage` | `true` | The worn lantern's black flame on the player's fire hits (section "Lantern"). Off for the session without the main-thread queue |
| `lanternLog` | `true` | One line per hit that burns (`lantern hit:`), per burn cast (`lantern burn:`) and per kill of a burning target (`lantern kill:`), not rate-limited (on for the first run). The `rejected` line (once per source) and the placement lines are written either way |
| `lanternBurnPct`, `lanternBurnFlat` | `0.02`, `20` | One burn's damage: the share of the target's maximum health (0..1) plus a flat amount (0..10000), dealt over the burn spell's duration (2 s, from the ESP record, not a key) |
| `lanternReapply` | `0.1` | Seconds between two burn casts on one target (0.02..2). Changes nothing in the damage (a re-cast only restarts the burn); it keeps a 60-hits-a-second Flames from casting 60 times a second |
| `lanternOffset` | `[0, 0, 59.5, 0]` | Where the lantern stands: `[x, y, z, yaw]` in game units from the table ref (x right, y forward, z up, turned with the table; -1000..1000) and a yaw in degrees added to the table's (-360..360). The default is the table top's centre (its bounds reach z 59) |
| `smokeOffset` | `[0, 0, 71.5]` | The smoke's `[x, y, z]`, the same way |

Retired keys (C6): `projectile`, `explosion`, `hazard`, `forcePalette`, `particleColors`, `tintScale` (and the older
`burnMode`). An old json keeps loading; each is logged once as `retired, ignored` with the reason, never as an unknown key.

**Old copies and other MO2 profiles** (`cleanOldCopies`). Under MO2 every profile with its own saves writes one ledger
mirror name (`HoMBF_slots.<8 hex>.json` hashes the virtual `__MO_Saves` path), so a profile's cleanup cannot see another's
keys from the mirror alone; each ledger epoch therefore keeps `staging\keepsets\<epoch>.json`, listed in
`staging\keepsets\registry.json`. A profile that ran an older build has neither: at each start, before the session's
first ledger or mirror write, every mirror in the log folder whose epoch is not this ledger's is registered and its keys
merged into that epoch's keep set file (`OldCopies::NoteSessionStart`, `OldCopiesKeepsets.h` `AdoptMirrors`), so its
copies are protected from then on. A profile that never ran with a mirror visible to this one (its mirror was replaced
before any session of this build read it) is protected only by `oldCopiesGraceDays`. What the cleanup then removes of its
copies is rebuilt, never broken: at that profile's next start a slot whose ledger object is not in Data with its
recorded size and fnv64 is not used (`ObjectIntact`, `src/FormCopiesFillModel.cpp:86`, the Data check at `:91`; the
caller at `:173` logs "is not used": that object is not published), and the builder makes the copy again.

**A cleanup stopped by a keep set** (`old copies: not cleaned this session: ledger epoch <hex> is registered but its keep
set file is missing`, or `... the keep set '...' does not read`, or `the registry '...' does not read`). Nothing is
removed while it says so. To recover (the game closed): delete `staging\keepsets\registry.json`, and the named
`<epoch>.json` when that one does not read. What stays protected: every other keep set file (each is read whether or not
the registry lists it), every ledger mirror in the log folder, this profile's ledger, and the grace. This session's epoch
is registered again at its next ledger write, and any epoch whose mirror is still in the log folder is adopted again at
the next start. Only an epoch with no mirror left and its keep set file deleted loses its keys; its copies are then
rebuilt as above.

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
| 1 hit shader and hit art | `ActiveEffectReferenceEffectController` vtable: `GetHitEffectShader` (0x0D) and `GetHitEffectArt` (0x0C), where the engine asks an active effect which hit shader and art to put on its target. While stoked, for an effect the player cast (caster handle) whose MGEF is a fire effect of a fire spell (scrolls, staffs and enchantments sharing the MGEF do not count): the shader call returns `HoMBF_BurnShader` instead of the engine's, so the engine makes the hit shader from ours (instant hits, concentration and cloaks alike; no event, no record changed, nothing to finish); the art call returns the engine's art, and with `hitArt` on the art's 3D (`ModelReferenceEffect::artObject3D`) is recoloured on a later tick once it exists (up to 2 s; matched on controller and art only - the engine's first art call comes before it sets the target - with the live controller re-checked as the player's fire effect, and the target read from the live effect). Both slots are hooked whatever the config says (v2 S3); `burnShader` and `hitArt` decide only the swap and the recolour. Logged per slot, with two caps that cannot use each other up, both only for calls with something to show (the engine's answer or the MGEF's own field not null; since Phase 0, 2026-10-19, also for the player's calls: an always-on ability with no shader, MGEF FE2D6812 in the live log, filled the player cap at load): the first 20 calls whose caster is not the player and the first 60 calls whose caster is the player - each with the controller and its call count, the active effect's flags (+0x7C, raw, and bit 1: the engine's "no hit shader" bit, set only by the cloak callback), the MGEF and its own field, the spell, the target, the caster, whether stoked, the burn shader form, the engine's answer, the outcome with its reason (swapped, engine answered null, not stoked, not a fire effect, config off...) and the thread. Per-outcome counters of the player's calls feed the self-test (E4). Phase 0 (2026-10-19, normal log, no self-test): when the stoke buff flag goes on, both caps and a second set of counts by outcome (every caster) start again (`hit shader/art diagnostics: stoke window started (<reason>): line caps and window counts reset`); when it goes off (or a load drops it), and every 30 s while it is on, one line per slot: `hit shader 0x0D stats (stoke window ended after|running for <s> s, <reason>; per call, two per shader effect): player calls N: swapped, copied, engine null (flags bit 1), not stoked, not fire, config off, already ours, no burn shader; not the player's` and the same `hit art 0x0C stats` line (its "swapped" reads "kept for the recolour"); the self-test's own counters are not reset. The engine reads MGEF `data.effectShader` only through slot 0x0D (AE 34846 / SE 34049), reached only through the vtable: `ReferenceEffectController::Start` 34761 and the `ShaderReferenceEffect` constructor 34910 both `call [rax+68h]`, no direct call (research 2026-10-16; decrypted 1.6.1170 disassembly, 2026-10-19), so every count is per call, normally two per shader hit, and no other swap point exists; enchantment shaders go through `ApplyEffectShader` (19872) and are out of scope |
| 2 projectile | `Handle3DLoaded` (vfunc 0xC0) on the Missile, Flame, Cone, Beam, Grenade, Barrier and Arrow vtables; the player's (see "Whose it is") and a fire spell's, or an arrow of the player's bound fire weapon: its light tinted (C6: no 3D recolour), the self-test record and the material check |
| 3 explosion | `Set3D` (vfunc 0x6C) on the Explosion vtable; base form in the fire explosion set, the player's (see "Whose it is"), decided only once owner, blame or a script tag (also on the explosion caster's object reference, `GetCasterObjectReference`) is there, given up after 2 s; the explosion's own magicCaster (Explosion.h:87) must name nothing, a fire spell, or an enchantment (the explosion's own; a weapon enchantment cannot be told apart) - a scroll, staff, power, potion and so on are left; a light missing at Set3D is tinted on the next tick. Initialize (0xA2) and Update (0xA3) are hooked as a second route (Update does nothing unless stoked or the self-test records); each explosion is decided once, and the first 20 hook calls on any explosion are logged |
| 3 hazard | `Set3D` (0x6C) and `Initialize` (0xA2) on the Hazard vtable: a hazard made inside the player's impact or copy window, or whose base is a copy, is tagged; while stoked (and `light` on), a hazard with 3D is queued once (by handle) and handled by a task: the player's (owner or script tag waited for up to 2 s) and a fire hazard (see above) gets its light tinted (light type checked through RTTI; C6: no 3D recolour); a pass without a light lets a later Set3D queue it again. The first 20 hook calls on any hazard are logged |
| 4 hand art (`handArt`, off by default; kept until `copyCastingArt` is proven) | ticker task every 100 ms while stoked, and at once when the buff turns on: per hand casting a fire spell OR holding one readied (Actor::GetEquippedObject), the casting art itself (`ActorMagicCaster::castingArtData.attachedArt`); only when that is null or holds no effect geometry (logged as FALLBACK) the caster's `magicNode`, the hand's magic node by name in the 1st and 3rd person skeletons and, last, the player's casting-art `ModelReferenceEffect`s; the caster's light tinted. Every recoloured hand-art property is remembered with its original greyscale texture, path, base colour and palette flags, and put back when the buff turns off and on a load (a fresh material copy, SetMaterial(copy, true)); the casting light's old colour too. Entries only we still hold are dropped each tick |
| 4 bound weapon (`boundWeapon`, off by default), light effect (`light`) | ticker while stoked: a bound fire weapon (bound flag, named by an active Bound Weapon-archetype effect the player cast on themselves, that effect a fire effect of a fire spell or the weapon's own enchantment carrying a fire MGEF, and the weapon one of the static bound fire weapon seeds; the list is kept by the ticker and the arrow hook only compares FormIDs) is recoloured in the player's 1st and 3rd person bipeds (`BIPOBJECT::partClone`); a Light-archetype fire effect the player cast on themselves has its light tinted (`LightEffect::light`). Both are put back with the hand art |

Hit path observation (2026-10-19, `HitPathObserve.h`, log only, AE 1.6.1170 only; any other runtime logs `hit path observation off (runtime ...)` and installs nothing). In run 5 the player's fire spells damaged NPCs yet no 0x0D / 0x0C call carried a fire MGEF, and the decrypted image showed another plugin's `FF 25` jump at the entry of 40508. At kDataLoaded one line per function of the hit path (40508, 34092, 34094, 34761, 34910, 19872, 34833, 34154): `hit path foreign hooks: <id> <name> at SkyrimSE.exe+0x<rva>: <first 16 bytes>: not hooked`, or `hooked by <module> at <module>+0x<offset> (chain -> <addr> (<module+offset or no module>) ...)` (E9, EB, FF 25 [rip], mov rax/r11 imm64 + jmp, push + mov [rsp+4] + ret followed up to 8 hops; every read checked with VirtualQuery and under SEH). Then three call sites, each checked first as an E8 to the expected callee (`hit path observation: <site> at SkyrimSE.exe+0x<rva> verified (bytes ...)`, or `... - skipped` with the reason, the bytes and where the call goes instead), are redirected through `write_call<5>` to thunks that call the original with the same arguments and return its result: site A (34092 +0x23E) and site B (+0x4CD), both calls to 34094, and ApplyEffectShader's call of the `ShaderReferenceEffect` constructor (19872 +0x1AC -> 34910). Lines, capped per stoke window (the caps come back with the 0x0D / 0x0C caps, the first 3 windows of a session): `hit path effect start [A|B n]` for the first 40 calls whose effect the player cast or that targets the player and whose MGEF has a `data.effectShader` (MGEF and editor id, its flags and kNoHitEffect bit, the shader, fire effect of a fire spell or not, spell, caster, target, the active effect's flags +0x7C before and after the call, the controller vtable, stoked, thread); `hit path ApplyEffectShader [fire/player n]` for the first 40 whose shader is a fire MGEF's hit shader or whose target is the player, `[other n]` for the first 20 of the rest (OwnedController +0x10 shader with editor id, +0x08 target and +0x20 facing handle as FormIDs; a camera controller or another vtable is named, its fields not read). At window end and every 30 s while stoked, after the 0x0D / 0x0C stats lines: `hit path stats (stoke window ended after|running for <s> s, <reason>): effect start site A N calls, N the player's (caster or target) with a hit shader (N fire effects of fire spells, N kNoHitEffect); site B ...; ApplyEffectShader N calls: OwnedController N (a fire MGEF's shader N, on the player N), camera N, other N, null N`. Trampoline: 3 x 14 bytes, with the corpse grey hooks A and B 5 (344 of the 512 reserved at most).

Run 8 probe (2026-10-20, design v2 of the FEC grey corpse, AE 1.6.1170 only, log only but for one unused flag bit). The 0x0D thunk now remembers the last elemental hit (fire: a fire effect of a fire spell or a fire MGEF by the rule; frost / shock: `ResistFrost` / `ResistShock` as the resist value or the vanilla `MagicDamageFrost` / `MagicDamageShock` keyword, found by editor id; no FormIDs) of the player and of non-player Actors other than the target, per target, 128 targets, with its source (`player stoked` = the swap's own condition, fire only, `player not stoked`, `NPC`) and element; for a non-player effect NoteFireEffect resolves the caster handle (`LookupByHandle`, held while compared; the player's is a value compare) and an effect with no caster, the target itself as caster or a non-Actor caster is not recorded, so FEC's own fire on a corpse cannot turn it orange; steady and game seconds (`GameClock.h`: unpaused time summed in the main-thread drain). Round 1 review (`MagicApplySink.h`): a `TESMagicEffectApplyEvent` sink (added only when the corpse mark's ApplyEffectShader hook was written, else one `magic effect apply sink not registered` line; a fault in it skips that event) records the same for an Actor target, because FEC calls `Play` from its Papyrus `OnMagicEffectApply` handler, which may run before the effect start reaches the 0x0D thunk (the VM's own sink only queues the script event; the order is to be confirmed by these lines in run 8): `magic effect apply [n|down n]: MGEF .. '..' (fire|frost|shock), caster .. (player stoked|player not stoked|NPC), target .. (dying|dead|not down): recorded | kept stoked (not recorded) | not recorded: <reason>, game T s, thread N` (40 a session for the player's stoked fire on living targets and 24 for any hit on a down target, so a concentration spell cannot use up the lines before the first kill; the event has no spell, so there the player's fire is stoked when its MGEF is a fire effect of some fire spell while stoked - a proxy a scroll, staff or enchantment sharing that MGEF passes - and that MGEF's effect start, which knows the spell, re-judges the apply event's record and may replace it, keep rule or not; an apply event on a down target already recorded as stoked leaves the record's origin and MGEF as they were). A dying or dead target whose record is the player's stoked fire keeps it against the player's later non-stoked fire (Flame Cloak, an enchanted weapon): `kept stoked [n]: target .. (dying or dead), its last hit the player's stoked fire X game s ago; the player's fire, not stoked (MGEF .., by the apply event|effect start), does not replace it, game T s, thread N` (16 a session); NPC fire and the player's frost or shock always replace it, so a stoke then a shock kill is not marked; a non-player fire effect on a dying or dead actor logs `hit path NPC fire on a corpse [n]: MGEF ..., caster .. (recorded as an NPC's hit | not recorded: no caster | the caster is the target itself | the caster is not an Actor), target ... life state ..., killer handle ...` (20 a session). After the `ShaderReferenceEffect` constructor returns in the 19872 -> 34910 thunk, `CorpseMark.h` judges OwnedController effects: `corpse mark [n]: SRE 0x.., shader .., target .. (dying|dead), killed by the player, last elemental hit player stoked fire X game s / Y s ago (by the apply event|effect start; window 45 game s[, kept open by an earlier mark until T]), flags 0x.. -> 0x..[ (greyCorpseShaders false: bit not set)], game T s, thread N` (48 a session) or `corpse not marked [n]: ... killed by the player: <reason>` (32 a session; reasons: no game clock, no elemental hit remembered, the last elemental hit was an NPC's / the player's not stoked fire, frost or shock, older than the window). Hooks A and B (`CorpseGrey.h`, design v2 §3, the run 8 attach probe turned into hook A; both sites' E8 and the argument loads before it checked, both hooked or neither: `corpse grey hooked (...)` or `corpse grey not installed (...): neither site hooked, the corpse mark has no effect`): A, `write_call` at 34933 +0x12D (34934's only call), for an SRE with the bit and `greyCorpseShaders` on looks up `CorpsePalettes::Find` for its shader's NAM9 particle palette and NAM8 membrane palette paths and puts each grey texture found into SRE +0xD8 / +0xF0 (NiPointer reset) before the original; B, `write_call` at 34934 +0x2EC (17325's only call), runs the particle property factory with share false while A replaced +0xD8 (a thread_local flag around the original call), so that SRE's particle property is its own and the shader's shared cache stays the NPCs' (r8 is read from +0xD8 just before the call, so it is already the grey). Lines: `corpse attach [n]: SRE .., shader .., flags .., target .., textures particle .., particle palette .., fill .., holes .., membrane palette .. (0x.. 'name' or none); particle palette '<path>' 0x.. -> grey 0x.. | kept, no grey palette | none on the shader; membrane palette ...; particles private yes|no (or not swapped: greyCorpseShaders false); after a load yes|no, game T s, thread N (Main's|not Main's)` (48 a session), `corpse kept [n]: no grey palette for '<path>' (particle palette)[ and ...], SRE .., shader ..` (48 a session), `corpse particles private [n]: shader .., palette .., property 0x.. (share false), the shader's shared cache 0x.. -> 0x.. (unchanged)` (20 a session), and the first 34934 call's thread (H1). `SreProbe.h`: `SRE vtable [point] 0x36 Init: slot 0x.. -> X (module+0x..): the engine's own (34912) | in SkyrimSE.exe at +0x.. | outside SkyrimSE.exe's code: ..[; its first instruction jumps to ..]` for slots 0x27, 0x28, 0x2D, 0x2E, 0x36, 0x37, 0x38, 0x3B, 0x3E at kDataLoaded and the main menu, and at the main menu `GetTexture probe [label]: 'path': 0x.., name '..' (the path asked for: yes|no), NiSourceTexture, rendererTexture set|null[, the same texture as for the first missing file: yes|no], thread N (Main's)` for GradFireExplosion.dds with and without `textures\` and two missing paths. The session counts (marks by outcome, attach calls greyed / kept by reason, private particle properties, the grey palettes ready, NPC fire on corpses, the hits not recorded, the stoked records kept, the apply sink's counts) go on the `hit path stats` line as `corpse mark (session): ...`.

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
Casting-art objects override these colour rules with `tintAddHands`: a derived ramp even in godskin mode, source palette row selection and emissive scale kept (the ramp brightness is applied once, in the palette). Non-palette additive shapes keep the ordinary glow rules. The dark marble layer uses `handMarbleOpacity` and `handMarbleScale`, with independent 108 +/-54 degrees/s rotation and SubTex cycling averaging 8.4 cells/s at the static lifetime distribution; source white-flame motion stays unchanged. Small additive embers get twice the brightness (cap 8) and a dark clone at one third of the birth rate, drawn first. Zero opacity disables these added hand effects, retaining the ramp. Casting-art NIF keys append `handmarbleopacity`, `handmarblescale` and `handfx=2`; other copy keys and golden v2 lines are unchanged. Contract 8 rejects older builders; BFK4 and output identity 2 are retained. [Details](docs/casting-art-hands.md).
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
blackflame-greybuild --write-lantern <data folder> --in <dir> [...]
                                                    (the lantern's lantern_gnd / lantern_worn / lantern_smoke NIFs and
                                                    LampGenericSoot01.dds, GreyLantern.h; prints their read-back)
blackflame-greybuild --inspect-lantern <file.nif> [...]   (that read-back for any NIF)
```

`--write-lantern` reads loose vanilla files from the `--in` folders: `meshes\clutter\common\candlelantern01.nif`,
`meshes\actors\character\character assets\skeleton.nif` and `meshes\actors\atronachflame\character
assets\atronachflame.nif` (Skyrim - Meshes0.bsa), `textures\clutter\common\lampgeneric01.dds` (Skyrim - Textures3.bsa),
and `malebody_1.nif` / `femalebody_1.nif` from the same folder as the skeleton (Skyrim - Meshes0.bsa; required: the worn
model is placed on both bodies and checked against them). Extract them from the game's own BSAs, not a replacer. The fire
(round 7: round 6's two cut atronach strips, about 20 vertices each, did not show in game) is the same in the ground
model, the worn model and the inventory: two closed spindles the builder makes on the cage axis (lathe shapes, 12
segments round, 6 rings, open at the bottom, seen from every side). `HoMBF_FlameOuter` is 0.40 x the frame's height tall
and 0.55 x the smaller cage wall size wide, its base a third of the frame's height above its bottom (z 10.58 to 23.27 of
31.74 on the ground model, radius 4.77); `HoMBF_FlameCore` is 0.65 x as tall and 0.45 x as wide, its base 0.05 x the
outer height higher. Vertex alpha runs 0 at the bottom ring (round 8: at 0.5 the open rim showed as a hard line), 0.7 at
the 0.15 ring, 1 at the widest, 0 at the tip. Each layer takes a copy of the Flame Atronach's `FireStrip08` effect shader
with its UV scroll controller (the atronach NIF is read for nothing else) with Greyscale_To_PaletteColor, Vertex_Alpha,
Use_Falloff, Double_Sided and Vertex_Colors on and Greyscale_To_PaletteAlpha and the atronach body's Decal and
Dynamic_Decal bits off: with palette alpha on, Community Shaders (as vanilla) takes the alpha from
`palette.a(u = texture alpha, v = accumulated alpha)`, and our palettes' 64 rows are all alike, so vertex and base alpha
would do nothing; with it off the opacity is texture alpha x vertex alpha x base alpha x falloff. The falloff (round 8,
user: the hard lathe outline read as a glass spindle holding the fire) fades the outline: opacity 1 where the surface
faces the camera down to 0 edge-on, start angle cos 1.0 opacity 1.0 and stop angle cos 0.0 opacity 0.0 on the outer
layer, stop angle cos 0.2 on the core (so it is gone by 78 degrees and stays a glow inside the outer layer). The U texture
scale is 2 on both (more, narrower tongues: the scrolling tile twice round; U runs 0..1 and the template's clamp mode is
wrap, 0x0000FF03, which the read-back checks, so the seam still meets itself).
`HoMBF_FlameOuter` blends normally (alpha 0x10ED, base alpha 1), so GreyEffects gives it `GradBlackFlame.dds`, whose
dark band paints the dark parts of the flame; `HoMBF_FlameCore` is additive (0x100D, base alpha 1) and takes
`GradBlackFlameAdd.dds`. The build and the read-back keep every flame vertex at least 0.5 off the frame's triangles
(read 3.35 on the ground, 2.11 worn). No dark marble layer is added: the casting-art copies' dark companion
(`AddHandMarble`) is for additive palette particle systems, and the outer layer paints dark itself. The ground model
carries the layers itself and BSXFlags gets the Animated bit (0xC3); it no longer names an addon node, because the grey
copy behind ADDN 0x804 (`mps\mpsfiresmall01.nif`) is three additive particle systems on `GradBlackFlame.dds` without
that dark layer. The worn model (round 6, user: nothing lies on the body, so it cannot clip with what the wearer
has on) is an iron ring and the lantern only, every shape on NPC Pelvis [Pelv] (body part 60): the builder measures the
belt band's top on each body's underwear shape (`MaleUnderwear_1`, `FemaleUnderwear`) and puts the ring's centre at the
highest of them (z 77.15, the male band), 3 forward of the pelvis, starting with its inner side 0.3 off the hip's outermost point; the lantern (0.8
of round 1, handle turned upright over the cage axis and set 6 tall, an iron lug on the lid under each of its feet) hangs
vertically from the ring by its handle. Where the lantern would come within 1 unit of a body or the ring within 0.3 (both
vanilla thighs reach out under the hip in the reference pose) the ring steps out sideways in 0.5 steps up to 12, past
which the build fails rather than tilting it; on the vanilla bodies it settles 7.5 out (ring centre x -21.27, the lantern
1.32 off the male body, 1.59 off the female). The build logs every step with both bodies' clearances, and the read-back
checks that the cage axis is world Z within 0.5 degrees, the handle's height, the lugs touching lid and feet, the ring's
height and the clearances. The ring takes a plain iron area of the lamp texture (u 0.80-0.98, v 0.60-0.80), the lugs the
lid's own. The placement, size, shader and darkening constants are named in `tools/blackflame-greybuild/GreyLantern.h`.

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

[Read this section](docs/black-flame-v2-copies.md#black-flame-v2-copy-framework-spike).

### Copies (S1): `blackflame-greybuild --forms`

[Read this section](docs/black-flame-v2-copies.md#copies-s1-blackflame-greybuild---forms).

### Copy request (v2 expansion C1, DLL): `FireClosure.h`, `CopyRequest.h`

[Read this section](docs/black-flame-v2-copies.md#copy-request-v2-expansion-c1-dll-fireclosureh-copyrequesth).

### Builder from the game (v2 expansion C2): `--from-game`

[Read this section](docs/black-flame-v2-copies.md#builder-from-the-game-v2-expansion-c2---from-game).

### Texture copies (T1): `GreyTexture.h`, `--no-textures`

[Read this section](docs/black-flame-v2-copies.md#texture-copies-t1-greytextureh---no-textures).

### Decal texture set copies (TXST): `GreyFormsTxst.h`, `FormCopiesFillTexture.cpp`

[Read this section](docs/black-flame-v2-copies.md#decal-texture-set-copies-txst-greyformstxsth-formcopiesfilltexturecpp).

### Glow rules: `GreyGlow.h`

[Read this section](docs/black-flame-v2-rendering.md#glow-rules-greyglowh).

### Visual-risk check: `GreyRisk.h`

[Read this section](docs/black-flame-v2-rendering.md#visual-risk-check-greyriskh).

### Freshness and the copies map v2 (v2 expansion C3, DLL): `CopyFreshness.h`

[Read this section](docs/black-flame-v2-rendering.md#freshness-and-the-copies-map-v2-v2-expansion-c3-dll-copyfreshnessh).

### Fill (S1, DLL): `FormCopies.h`

[Read this section](docs/black-flame-v2-rendering.md#fill-s1-dll-formcopiesh).

### Swap points (S2, DLL): `CopySwap.h`

[Read this section](docs/black-flame-v2-rendering.md#swap-points-s2-dll-copyswaph).

### Ids and how each was checked (v2)

[Read this section](docs/black-flame-v2-rendering.md#ids-and-how-each-was-checked-v2).

### Spike limits and costs (v2)

[Read this section](docs/black-flame-v2-rendering.md#spike-limits-and-costs-v2).

### Single launch: the copy contract (P0): `CopyContract.h`, `SlotCoSave.h`

[Read this section](docs/single-launch.md#single-launch-the-copy-contract-p0-copycontracth-slotcosaveh).

### Single launch: the builder (P1): `GreySlotPool.h`, `GreyKeyed.h`, `GreyProgress.h`, `GreyBudget.h`

[Read this section](docs/single-launch.md#single-launch-the-builder-p1-greyslotpoolh-greykeyedh-greyprogressh-greybudgeth).

### Single launch: the DLL side (P2): `SingleLaunch.h`

[Read this section](docs/single-launch.md#single-launch-the-dll-side-p2-singlelaunchh).

## Lantern

Design: [docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md](../../docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md).
Records (this ESL, 0x80D-0x816): ARMO `HoMBF_Lantern`, ARMA `HoMBF_LanternAA`, ENCH `HoMBF_LanternEnch`, MGEF
`HoMBF_LanternEffect` "갈망" 0x810 (keyword `HoMBF_StokedKeyword`, perk 0x811), PERK `HoMBF_LanternPerk`, SPEL
`HoMBF_LanternBurnSpell` 0x812, MGEF `HoMBF_LanternBurnEffect`, MSTT `HoMBF_LanternSmoke`, GLOB `HoMBF_LanternPlaced`
(0x814 was the journal BOOK, removed 2026-10-21: the user leaves the story to the player's imagination). When any of them is missing the DLL logs one warning (`lantern records missing ...`) and only the
lantern features are off.

The worn lantern is the only player-facing trigger: its effect carries the stoke keyword, so the whole Black Flame look
switches on (`buff flag ON (<reason>, lantern)`). The stoke spell "갈망" 0x803 stays in the ESP for the self tests; nothing
in the game hands it out (console only). While worn:

- fire spells cost 30% more magicka (the perk's Mod Spell Cost entry, conditioned on `MagicDamageFire` and the player);
- the player's fire hit on an actor sets it on black flame, as Elden Ring's black flame does (design v3): damage over
  2 s equal to `lanternBurnPct` (2%) of its maximum health + `lanternBurnFlat` (20), ignoring resistance.
  `HoMBF_LanternBurnSpell` is cast by the player (kill credit kept) with a per-second magnitude of (max x pct + flat) /
  the burn spell's duration, read from the ESP at load (logged; a duration not above 0 turns the damage off with one
  warning). It does not stack: a further hit while it burns restarts the 2 s, so any stream of hits (Flames held,
  Firebolt spam, Wall of Flames) burns at most 1% + 10 per second. Every casting type is treated the same; fire hazards'
  spells count as fire. Casts on one target are at least `lanternReapply` (0.1 s) apart. Cloak hits, scrolls, staffs,
  shouts and powers get nothing. Not stacking relies on the engine replacing the same spell from the same caster
  (vanilla behaviour, not proven here): the `lantern burn` line counts the burn effects on the target after each cast.

Limits (design section 3, "Fire-spell scope"): the extra damage uses the addon's own fire rule (`IsFireSpell`: the fire
keyword, fire resistance or the catalog's fire tag, spell type Spell), so it matches what turns grey; the perk can only
see keywords, so a spell that is fire only by the catalog's tag turns grey and gets the damage but not the +30% cost.
Scrolls are out of both the damage and the look. A follower wearing the lantern gets nothing (the perk entry is the
player's, the flame and the damage are player-only).

Placement (`LanternPlace.h`): no CELL record (13 plugins override Ansilvund02 in TAKEALOOK). When cell 01F358 finishes
loading, or after a game load in it, and the global is 0, the lantern and the smoke are created on the table ref 03DFA0
(UpperTable01; table position + the config offset turned by the table's z angle) and the global becomes 1 at once: the
placement is done when the refs are made, so no save or later load can make a second lantern. If one of the two could
not be made, what was made is kept and the global is still 1 (`partial` warning); only when none was made does it stay
0. Keyframing is best effort (a failure only logs): the made lantern, and at every later load of the cell the lantern of
ours (not disabled, in the cell, within 64 units of its configured place, so not one the player dropped elsewhere), is
keyframed through `TESObjectREFR::SetMotionType` (which marks the ref's Havok state for the save) once its 3D is there
(10 s at most). Picking the lantern up disables the smoke in the player's cell (and sets the global, which matters only
for a lantern from a console `AddItem` before any visit). A save that
already visited Ansilvund02 gets the lantern at its next visit. The offset's turn assumes the z angle runs clockwise seen
from above (forward = (sin a, cos a)); not checked in game, the ok line's two coordinates settle it.

Log lines (`HeartOfMagic_BlackFlame.log`, all prefixed `BlackFlame: `):

| Line | When |
|---|---|
| `lantern place on (cell <id>, table ref <id> of <plugin>)` / `lantern place off (<reason>)` | at kDataLoaded |
| `lantern place: ok lantern=<id> smoke=<id> at (x,y,z) table=(x,y,z)` | both made, global set to 1 (`at` is the lantern); the keyframe lines follow |
| `lantern place: partial lantern=<id> smoke=<id> at (x,y,z) table=(x,y,z) (could not create the <names>; ...)` | warning: one ref not made (shown as 00000000), the other kept, global set to 1 |
| `lantern place: skipped (<reason>)` | a table check failed, or neither ref could be created (global stays 0) |
| `lantern place: candidate <id> base=<id> disabled=<b> cell=<id> has3D=<b> dist=<d>` | each lantern looked at for a keyframe (`dist` from its configured place) |
| `lantern place: keyframed <id> ok\|failed` | each keyframe (the `SetMotionType` result as is), after creation and at each later load; `failed (no 3D within 10 s)` when the 3D never came |
| `lantern place: keyframe again skipped (table ref 03DFA0 <why>)` | a later load without the table ref in the cell |
| `lantern place: nothing of ours to keyframe again (...)` | a later load found no lantern within 64 units of its place |
| `lantern place: a task ran off the main thread - nothing placed or changed` | once, if a task ran off the game thread |
| `lantern smoke: disabled <n>` | the lantern picked up |
| `lantern burn spell <id>: effect duration <s> s` | at load: the burn's length the rate is divided by |
| `lantern hit: src=<id> proj=<id> tgt=<id> t=<ms>` | a hit that burns: written by the task after its checks (actor, alive, the lantern worn), so a hit with only the console stoke spell is not counted (`t`: the hit event's time from kDataLoaded) |
| `lantern hit: rejected src=<id> type=<casting type\|none> (<why>)` | once per source id, written whatever `lanternLog` says: own spell, not a spell, cloak, not a fire spell |
| `lantern burn: tgt=<id> max=<m> rate=<r>/s for <s> s effects=<n> magnitude=<x> difficulty=<d>` | each cast: `n` the burn effects active on the target after it (expected 1), `x` the newest one's magnitude as the engine stored it (expected = rate) |
| `lantern kill: tgt=<id> killer=<id>` | a death while burning (within the burn's 2 s + 1 s after the last cast) |
| `lantern cost: <spell id> <cost>` | at each buff flag change, every fire spell the player knows |

## Runtimes and Address Library ids

[Read this section](docs/runtimes.md#runtimes-and-address-library-ids).

## Known limits

[Read this section](docs/checks-and-limits.md#known-limits).

## Self-test

[Read this section](docs/checks-and-limits.md#self-test).

## Script-placed fire

[Read this section](docs/checks-and-limits.md#script-placed-fire).

## In-game checks the self-test cannot do

[Read this section](docs/checks-and-limits.md#in-game-checks-the-self-test-cannot-do).
