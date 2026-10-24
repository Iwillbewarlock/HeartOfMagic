# Fire Storm body/floor light: 2026-10-23 evidence

The user identified the orange as illumination on the body and floor. The reported light's
runtime identity is still unverified. The evidence below rules out the proposed holder-effect
light and unconverted body colour keys; it does not establish a production light fix.
No game, MO2, build or self-test was started for this investigation.

## Later run: 19:51 through 20:00, 2026-10-23

The original SKSE log has 900 lines, ending at 20:00:22.980 (83 more than the run1951 snapshot).
Lines 670-672 keep original art 00080E32 for ability 00080E34 before stoke. Lines 714-715 then find
one live fire equip ability but skip refresh because HasSpell is false. This establishes a refresh
omission, not that two art models overlap or that this is the source of the reported red pixels.
The earlier 17:04:49 copy/3D observation below remains valid evidence against treating every red
aura as an old original clone.

Lines 701/703, 724-725 and 727-728 show replacement casting lights tinted from orange to the
configured (0.250,0.220,0.280); no `rewritten` was logged. Twelve probes (705-749) found no orange
light in four caster slots or below the player's third-person root. That configured tint is slightly
purple, not achromatic. World lights, other roots and changes between samples remain outside this
evidence. The handoff's categorical "not light" conclusion is therefore too strong.

The STALL at line 750 recovered at line 819 (`stall over`, 19:56:13.008). Its cause is unknown.
Corpse hooks installed (492-493), but final counters at 899 still show zero marked kills/attaches:
corpse colour and NPC preservation cannot be judged from this session.

The fallback correction and next-run discriminators are documented in
[equip-ability-refresh.md](equip-ability-refresh.md). Light tint and generated NIFs are unchanged.

## Records, including the winning overrides

`docs/diagnostics/light-evidence.js records` reads the enabled profile plugins in load order, resolves their
files using overwrite then the enabled modlist from top to bottom, and scans compressed and plain
records. This was run against `D:\TAKEALOOK`, profile `TKL - MUNG ADDON`. IDs in this table are
Skyrim local IDs. File offsets point to the record header, not its DATA subrecord.

| Record | Winning record and bytes | Meaning |
| --- | --- | --- |
| MGEF `080E33` | Skyrim.esm `@0x21B34D`; DATA+18 = 0, +20 = 0, +40 = 1, +5C = 089F26, +60 = 080E32 | No light or hit shader; Script archetype; casting and hit art are present. No fire KWDA, but there is no light here for FireSet to miss. |
| SPEL `080E34` | Skyrim.esm `@0x267F4C`; sole EFID = 080E33 | The equip ability. Neither this spell nor its MGEF has VMAD, so neither carries a script/property that creates an extra light. No enabled plugin overrides either record. |
| MGEF `07A82A` | PoE-Mysticism-Patch.esp `@0xBE98`; DATA+18 = 089F27, +5C = 089F26, +80 = 080E34; KWDA includes 01CEAD | Fire parent with the orange casting light and the body equip ability. It belongs to FireSet. |
| SPEL `07A82B` | PoE-Mysticism-Patch.esp `@0x34B14`; first EFID = 07A82A | Fire Storm's spell ID; 07A82A is the MGEF. |
| ARTO `089F26` / `080E32` | Skyrim.esm `@0xED883FF` / `@0xED8854F` | `Magic\FireStormHandEffects.nif` / `Magic\FireStormCastBodyFX.nif`; no enabled overrides. |
| LIGH `089F27` | USSEP `@0x3A052F`; DATA+8 = `F2 AC 66 00`, DATA+C = `01 20 00 00`; FNAM = 1.75 | RGB (242,172,102), radius 45, dynamic/portal-strict flags; no flicker or pulse flag. The only positively identified orange illumination source in these records. |

07A82A override order is Skyrim, MysticismMagic, PathsOfEmpyrean, PoE-Mysticism-Patch.
Mysticism removes equipAbility; the later two restore it. LIGH089F27's original Skyrim record at
0x4AD587 already has RGB (242,172,102); USSEP changes flags from 0 to 0x2001, not RGB.

## NIF and DDS bytes checked again

Actual files in `overwrite\meshes\heartofmagic\blackflame\copies`, rather than the earlier
thread's conclusion, were parsed by `light-evidence.js nif`:

- Body `436975b63f923d5c/copy.nif` (old) and `8d63a4f6d6e779ad/copy.nif` (current): 497
  blocks. All 66 NiPosData RGB keys and every quadratic forward/back tangent are achromatic.
  All 28 current NiPoint3Interpolator cached RGB triples are achromatic. There are no NiLight,
  NiPointLight, NiSpotLight, NiDirectionalLight or NiAmbientLight blocks.
- The 252 changed bytes are exclusively the 28 NiPoint3Interpolator cached triples, nine bytes
  per block. Block 31 at 0x1DAF, for example, changes three copies of `FF FF 7F FF` (-FLT_MAX)
  to `65 66 66 FF` (the grey-scaled sentinel). The NiPosData key blocks are unchanged between
  these two files and were already grey in the old file. This specific diff does not prove
  that the seqcol re-key repaired any orange animation keys.
- Both referenced current palettes are RGBA8. `065981388900a041/palette.dds` has 16,384
  top-mip texels and `00eb939751b0861f/palette.dds` has 65,536; all have R=G=B.
- The hand copies `3a2b1193c0254389` (vanilla) and `c80301da9a2c3083` (Mysticism) likewise
  contain no NiLight blocks. A shape named `ppLight` is not a NiLight.
- Body BSValueNodes 452 and 455 contain index40029. Builder.log:554-555 maps original36 to
  40029, the grey MPSFlameTrapFire ADDN. Ledger slot750 publishes model `876b654cdadf8062`
  with texture `5c47d59de98f8eac/copy.dds`. That actual 37-block model also contains no
  NiLight and names the grey copied texture, not the Embers HD source texture.

The body still uses `SparksGrayGlow.dds`, `FXGlowSpotLinearAlpha.dds` and `FXfireAnim04loop.dds`
as its palette input textures. All loose `ParticleLights/*.ini` found in mods are black,
enblightglow, fxglowenb, glowsoft01_enbl, po3_fullalpha and fxevgnnsmpglow; each only sets
`Cull=true` and none matches these three texture names. The presence of a CPU particle light
cannot be inferred just from an emissive-looking shape; the installed shader implementation
and the live light instance still need to be correlated.

`mods\CS Light\LightPlacer\CS Light\CS Light - Magic FX.json:1029-1037` explicitly assigns
body model `magic\firestormcastbodyfx.nif` a (27,27,27) light, not orange. Its earlier entry
at line9 assigns MagicLightFireStormHand to FireFXShader/TurnUndeadFlamesFXS/FireFXTrapShader;
the holder MGEF has no such shader. Whether any model-name rule matched the copy model at
runtime is not in the supplied log.

`CS Light - Mysticsm.json:27-40` names only `magus\mysticism\firestormhandeffects1h.nif` and
assigns MagicLightWhite01 with an explicit (18,18,18), radius18, fade1, Linear, point(0,0,0).
Neither that rule nor the body rule uses a node-name selector. The scanned loose configs have
no rule for vanilla `magic\firestormhandeffects.nif`. The rules' literal model paths differ
from `heartofmagic\blackflame\copies\<key>\copy.nif`, so a match on the copied path would
not find them. That can remove these neutral additional lights; it does not establish that
the missing rule creates an orange light. Live model-name/attachment state is unverified.

## Runtime path and brief hypotheses

`EffectLight.cpp:12-27` unwraps BSLight to its NiLight (or accepts a NiLight).
`EffectLight.cpp:50-56` assigns only `GetLightRuntimeData().diffuse = lightColor`.
`CasterArt.cpp` applies that to both player hand casters on every 10 Hz tick, for current or
readied fire spells, and remembers the original colour for buff-off. `PlayerArt.cpp:112-129`
separately handles player fire `LightEffect` active effects; the Script-archetype holder is
not one. Projectile, explosion, hazard and script-placed light paths use the same TintLight
helper but are not the equip-ability holder path.

Supplied `HeartOfMagic_BlackFlame.full.log`:

- 1021: 17:04:41.724, left Firebolt has `casting light tinted`.
- 1022: 17:04:41.725, right Fire Storm 0007A82B has `casting light tinted`.
- 1038: 17:04:47.774, left Fire Storm first hand-art line has `casting light none`.
- 1040-1043: 17:04:49.306, holder080E33/ability080E34 takes copy hit art FEFECD60 and
  both hands take copy casting art.
- CloneWatch at 17:04:49.367: FEFECD60 has artObject3D 60 ms after the hook returned it.

The old hand-art log stopped after the first spell line when no new hand geometry was recoloured.
It still called TintCasterLight before that return. Thus the later missing light lines do not
establish that a newly created casting light escaped tinting.

| Brief hypothesis | Finding |
| --- | --- |
| (a) seqcol missed body colour-controller keys | Refuted for these files: old and current keys are grey, palettes grey, and the 252-byte diff is cached triples only. |
| (b) another light/art/shader is orange | Holder's own light/shader refuted by records; its body/hand NIFs have no direct light. The parent casting light is orange initially and is already in the tint path. Its contribution to the reported pixels remains unverified. |
| (c) a pre-buff body clone survived refresh | Refuted for the shown clone: it was newly created from copy FEFECD60 after buff-on. The silent refresh at 17:04:41.754 can mean an empty active ability list; the ability appears nearly eight seconds later. |
| (d) another cause | Still open. No generic behaviour patch is justified by the supplied light evidence yet. |

## Added diagnostics and next run

The light behaviour is unchanged. `CasterArt.cpp:174` now records light appearance, replacement
and disappearance even while the selected spell stays the same, plus a separate bounded trace
when an existing light's diffuse differs from its last tint. Each trace kind has six lines per
spell/hand and 120 total, independent of the hand-art log's cap. Addresses are diagnostic only;
they are never dereferenced after storage. Logs include selected/current spell, casting source,
candidate MGEF/LIGH, raw BSLight and NiLight addresses, and actual diffuse before/after.
The candidate follows the checked engine path: actor.selectedSpells[castingSource]->GetAVEffect()
(CheckAttachCastingArt AE34185 at RVA5B3A09/5B3A16; CommonLib MagicItem::GetAVEffect AE11302),
not currentSpell's costliest effect. A live light may predate the current selection, so this
is deliberately labelled candidate rather than asserted as its creation record.

Look for these exact prefixes (the numbers/addresses vary):

```text
BlackFlame: casting light instance #
BlackFlame: casting light absent #
BlackFlame: casting light rewritten #
BlackFlame: equip ability refresh (buff on): 0 live fire equip abilities
BlackFlame: equip ability refresh (buff on): art disabled
BlackFlame: equip ability refresh (buff on): no player
```

For Fire Storm, the expected selected/current spell is 0007A82B and expected MGEF/LIGH are
0007A82A/00089F27. With default lightColor, `after` should be (0.2500,0.2200,0.2800).
A repeated `rewritten` line with the same pointers and orange `before` would establish a
runtime rewrite; an `instance` line identifies a replacement light. If the reported orange
persists with stable tinted casting lights, no rewrite or replacement was observed at the
sampled ticks. A 10 Hz trace cannot exclude updates between ticks or other live lights.
No test of a log-format-only change was added; C++ build and in-game
verification are explicitly left to Claude/the user.

## Reproduce the read-only checks

Run from the worktree root (these commands only print):

```powershell
$copyRoot = 'D:\TAKEALOOK\overwrite\meshes\heartofmagic\blackflame\copies'
node plugins/BlackFlame/docs/diagnostics/light-evidence.js records D:\TAKEALOOK 'TKL - MUNG ADDON' 080E33 080E34 080E32 07A82A 07A82B 089F26 089F27
node plugins/BlackFlame/docs/diagnostics/light-evidence.js nif "$copyRoot\436975b63f923d5c\copy.nif" "$copyRoot\8d63a4f6d6e779ad\copy.nif" D:\TAKEALOOK\overwrite
node plugins/BlackFlame/docs/diagnostics/light-evidence.js nif "$copyRoot\3a2b1193c0254389\copy.nif" "$copyRoot\3a2b1193c0254389\copy.nif" D:\TAKEALOOK\overwrite
node plugins/BlackFlame/docs/diagnostics/light-evidence.js nif "$copyRoot\876b654cdadf8062\copy.nif" "$copyRoot\876b654cdadf8062\copy.nif" D:\TAKEALOOK\overwrite
```

The record and body checks, same-file hand/MPS checks, `node --check`, and `git diff --check`
were run successfully. They validate files and source formatting, not the installed DLL or pixels.
