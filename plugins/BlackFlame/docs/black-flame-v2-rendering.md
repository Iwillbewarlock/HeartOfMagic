# Black Flame v2: colour rules and runtime

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
| `GetImpactData` (SE 20408 / AE 20860) calls in `SpawnCollisionEffects` (43022 / 44213) and the explosion's impact function (42679 / 43851) | `copyImpacts` | inside a copy window the impact copy is returned for the impact the set would return (no IPDS copy, no hash map insert). The window opens on the impact call sites (section "Grey swap") when the projectile's base is a copy, and in `Explosion::Update` when the explosion's base is a copy. The round-10 model window (GetModel swap) stays for projectiles and explosions without a copy. **Rune decal** (AE only): a rune's waiting glyph is placed right after Launch by MagicCaster's launch helper (AE 34452 +0x52F) calling the projectile decal helper (AE 44186) with a null IPCT; the helper then looks the IPCT up itself (MGEF -> IPDS -> IPCT) and hands its `decalTextureSet` to `BGSDecalManager::ApplyDecal`, past both sites above. So the helper's own GetImpactData E8 is a third site (found by the scan on every AE build; 1.6.1170: +0xB5, RVA 7E9C95, `E8 86 E3 B2 FF`), and a copy window opens around the +0x52F call when the projectile's own base is a copy (non-copy bases close it, as on the impact call sites). The window is installed on 1.6.1170 only, and only when the 28 bytes before the call are the null-IPCT argument setup (`48 C7 44 24 28 00 00 00 00 C6 44 24 20 01 44 8B CF 4C 8D 44 24 58 48 8B D6 48 8B CB`) and the call is an E8 to AE 44186 (`E8 4C 99 22 00`, RVA 5C028F); anything else is logged and left alone. Its thunk forwards all eight arguments (integer registers and four stack slots). The helper's other direct caller (SpawnCollisionEffects +0x44B) passes the IPCT it looked up, which the site above already swapped. Log: one install line per site, then `rune decal window N/120 (...)` per copy base (6 lines a base) and the `impact trace` lines of the site `rune decal (AE44186 via AE34452)`; counters `runeDecalCalls` / `runeDecalWindows`. T0-hooks does not judge these two sites (they are in `impactNote`) |
| SetCastingArt (AE 34150) call in AE 38784; SE: off | `copyCastingArt` | v2 expansion C4. On 1.7.104 it is the only E8 to SetCastingArt in the executable (no pointer to it either), inside a function (0xDA bytes in three pdata chunks) that reads `selectedSpells[slot]`, takes that spell's costliest effect (`GetAVEffect`) and its `castingArt`. The site is found on every AE runtime by scanning the caller's first 0x200 bytes for E8s to SetCastingArt: exactly one must be there, and (fix F4) the 7 bytes before it must be `48 8B 90 E8 00 00 00` (`mov rdx, [rax+0xE8]`, the castingArt read), else it is left alone with that note; only on 1.6.1170 / 1.7.104 may +0x91 stand in for a call another plugin rewrote. The thunk forwards rcx, rdx, r8, r9, checks the caster (ActorMagicCaster vtable) and the art (BGSArtObject vtable), then `PickCastingArtImpl`: the copy when the caster's actor is the player, its casting source a hand, the hand's selected spell fire, the art that spell's costliest effect's `castingArt` (read from FireSet's kDataLoaded cache, fix F5: no GetAVEffect on a job thread), while stoked. The first 40 player calls are logged. **Off on SE** (fix F1: 33369 / 37830 unverified matches) |
| hand art refresh: DetachCastingArt (AE 34152) then SetCastingArt (AE 34150), called by us; SE: off | `refreshCastingArt` | SetCastingArt only stores an art while none is attached: it returns at once when the caster's flag `kCastingArtAttached` (1 << 3, ActorMagicCaster.h) is set (1.6.1170 code read 2026-10-17). So the site above alone gives a hand its copy only when it readies a spell, and a hand readied before the buff kept the original for the whole buff (and one readied during it the copy after it). When the buff flag changes (RecomputeBuff), both hands become pending, and the main-thread queue (after the frame's job wait) gives each hand the art `PickCastingArtImpl` picks now, by the steps of 38784's own unready and ready branches: DetachCastingArt(caster, true) as the unready branch calls it, whatever the flags (with the art attached it releases the light and detaches the art; always it clears `castingArtData` and the art's graph, releases the enchantment controller and cancels and releases the clone task), then SetCastingArt(caster, art), which with no art attached stores it with `kStartCloneTask`. `ActorMagicCaster::Update` calls CheckAttachCastingArt (34185) first every update; it clones the new art and attaches it a few frames later (the hand is bare until then; the casting light is made again in the effect's colour and tinted on the next stoked tick when `light` is on). The first try is the tick after the change. Guards, per hand: the player's; weapon state drawn (being drawn: tried again, at most 50 unpaused ticks; sheathed or sheathing: left to the next ready); the hand's selected spell a fire spell; its `castingArt` that spell's costliest effect's art or a copy of it (any other art is left alone), or unset while an art is attached - the state of an idle drawn hand (run 7, 2026-10-20: `castingArt 00000000`, attached yes, caster state 0) - when that effect's art has a copy: then the current art cannot be read, so the refresh runs without the "already wanted" skip (the same art again only leaves the hand bare for a few frames); caster state none (fix HB1: ready waits too, until a log shows which state an idle drawn hand is in - neither 38784, DeselectSpellImpl (34158) nor 34152 reads the caster's state, so the engine unreadies a hand in any state, but that does not show ready is safe here) - else, ready, charging or casting (a concentration spell held), tried again every tick for at most 600 unpaused ticks (about 60 s; a `gave up` line with caster state 3 means an idle drawn hand is in ready); the player without 3D: at most 50 ticks; past a limit the hand keeps its art until its next ready (logged); a paused game waits without counting; nothing pending after a load. Installed only on 1.6.1170 and 1.7.104 (34152 and 38784 were read on 1.6.1170; 1.7.104 is checked only by the install-time byte patterns that follow) with the site above hooked, `mov rcx, rbx` before its castingArt load (the caster is in rbx), the id present and exactly one E8 to 34152 in 38784's first 0x200 bytes preceded by `B2 01 48 8B CB` (`mov dl, 1; mov rcx, rbx`, the unready branch, the same caster register); nothing is written into the code. The refresh calls SetCastingArt itself, so when the +0x91 call is chained onto another plugin's redirect, that redirect does not see the refresh's call. Each refresh logs `hand art refresh (buff on|off): hand N, spell X, art A -> B, was attached yes/no, caster state S; after: castingArt C, attached yes/no` (an idle drawn hand: `hand N, castingArt unset, refreshed: spell X, engine art A -> B, ...`), and a hand given up `gave up after N ticks: ...` (200 lines a session at most; the first non-refresh outcome of each request is logged with its values) |
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
call another plugin rewrote): the explosion function differs between 1.7.104 and 1.6.1170 (see the ids table). The rune
decal lookup in AE 44186 is found by the scan only; the rune decal window is a fixed offset checked by its bytes, 1.6.1170 and 1.7.104 only.
Trampoline: 512 bytes (186 at most used: the two rune decal sites add 28). Counters per device are in `GetCopySwapCounters` and the self-test.

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
| projectile decal helper (rune decal lookup site) | - / 44186 | 0x7e9be0 / - | 1.6.1170 image read 2026-10-20 (build/rune-decal): one pdata entry 0x7e9be0..0x7ea4b5 with one E8 to 20860, at +0xB5; reached with a null IPCT only from 34452 +0x52F in .text. Not read on 1.7.104 or other builds (the scan decides there); no SE id |
| MagicCaster launch helper (rune decal window) | - / 34452 | 0x5bfd60 / - | 1.6.1170 image read 2026-10-20: one E8 to 44186 in its pdata range (0x5bfd60..0x5c02d2), at +0x52F after the null-IPCT setup; reached from FindTargets 34410 and from 34450 / 34451. 1.7.104 (code read 2026-10-25): the same at 0x5ce8b0 + 0x52F (RVA 0x5ceddf), the same 28 bytes. Installed on 1.6.1170 and 1.7.104 only; no SE id |
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

