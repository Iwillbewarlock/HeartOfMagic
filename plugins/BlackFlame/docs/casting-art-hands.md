# Casting-art hand flames (contract 8)

The single-launch builder gives every request object whose role is casting art its own
`tintAddHands` override. Selection uses the object's role, not a FormID, EditorID, model
path or texture name. Other objects retain their existing keys and colour rules.

## Colour rule

`tintAddHands` defaults to `1.4`, accepts `0..2`, and uses the same six-decimal
quantisation as `tintAdd`. It travels on the casting-art request object and in that
object's key. It does not change the common key prefix. Additive hand palette copies
have separate keys; normal-blend smoke shares the ordinary grey palette key and copy.
Hand effects use derived palettes even when the selected palette mode is godskin.

An additive hand palette keeps the source dimensions, row structure and alpha bytes. For each
row, let `L(u)` be the source palette's linear-light luminance and `peak` its maximum.
The new grey value before output encoding is:

```text
t = clamp((u - 0.30) / (0.70 - 0.30), 0, 1)
ramp = t * t * (3 - 2 * t)
grey = clamp(ramp * tintAddHands * L(u) / peak, 0, 1)
```

A zero-luminance row remains black. High indices become bright while the low indices
remain dark; the source row's internal variation and alpha remain meaningful. Palette
shaders keep their original red channel as the row selector and their original emissive
multiple, including emissive animation. The brightness factor is applied in the palette
only. Normal-blend (smoke) palettes keep the ordinary grey rule. Non-palette additive
shaders retain the ordinary glow rules; they do not receive a blanket hand multiplier.

The shader lookup is two dimensional: colour uses
`palette(sourceTexture.green, colour.red)`; a palette-alpha shader samples alpha with
`sourceTexture.alpha` as its horizontal coordinate. The existing `SimulatePalette`
implementation is the reference for the offline preview's sampling and colour space.

## Compatibility

The request remains version 5, with `kContractVersion = 8`. An older builder could
ignore the object override and publish the old colour under a new key, so mixed
contract versions of the DLL and builder must reject one another. Builder output identity
remains **2**. The released v2 golden pin and existing golden lines are unchanged.

## Flame marble layer

Every casting-art request carries `handMarbleOpacity` (default 0.9, clamp 0..1)
and `handMarbleScale` (default 1.15, clamp 0.1..4), quantised to six decimals.
Only casting-art NIF keys append `handmarbleopacity`, `handmarblescale` and `handfx=2`.
Zero opacity disables all added hand effects: no marble or dark ember systems and
no bright ember multiplier; the hand palette ramp remains. No FormID, model path or
texture filename participates in selection.

A main flame is a `NiParticleSystem` with additive blending (destination ONE),
a `BSEffectShaderProperty` with Greyscale_To_PaletteColor, a source atlas and at
least two `NiPSysData` atlas cells. Its source palette must satisfy the shared
`EfshFireShare >= 0.20` rule: among lit texels, at least 20% have HSV hue 0..55
or 345..360 degrees, saturation >=0.45 and value >=0.30. This excludes single-cell
glows and non-fire palettes. Unreadable palettes cannot qualify; existing texture
read-failure handling applies.

The source particle graph is the nifly template: a distinct system, particle data,
effect shader, alpha property, modifiers, emitter, controller chains and animation
interpolators/data are cloned. Owned references and back pointers are remapped;
external emitter nodes, gravity nodes and mesh emission surfaces remain shared.
Sequence-controlled links are duplicated with the new target name; object palette
entries are added. Modifier names remain unchanged so controllers still resolve.
Name-only sequence links are resolved to the matching controller (ambiguous binding
skips the marble step). The shader float controller's numeric `ctrlID` convention is
unverified; unresolved bindings safely fall back to the ramp-only copy.
An invalid emitter lifetime skips the added rotation and SubTex only on that marble,
retaining its dark clone and source motion; the result summary records the reason.
Other valid marbles and embers continue. Other `std::runtime_error` failures from
`AddHandMarble` or `ValidateMarbleRefs` preserve and publish the ramp-only copy with a
deterministic fallback reason. Allocation, load/save, file I/O and commit failures
are failed objects, retried on the next run.
The string-table-preserving writer
serializes new names and refs.

The emitter keeps its source volume, speed, randomness, rate and lifetime. Initial
particle radius and radius variation (including animated radius) scale by 1.15;
the emitter volume itself does not grow. Pool capacity, LOD, death and spawn settings
remain those of the source. Each selected flame or ember system is cloned once:
added maximum pool is 100% of selected source pools, or at most 200% of all source
pools combined. A separate emitter generates independent random particles.
Atlas offsets shift by one cell (0/3/6 becomes 1/4/7). Each dark marble has its own
`NiPSysRotationModifier`: speed 108 +/-54 degrees/s, random direction, and a random
initial angle spanning a full turn. The source white flame's motion is unchanged.

`BSPSysSubTexModifier` cycles only the existing atlas cells. For `N` cells it uses
start frame 0, start-frame variation `N - 1e-3`, inclusive end frame `N - 1`, and
loop start/variation 0. The margin keeps `trunc(U * variation)` below `N` even when
the engine's float RNG rounds `U` to 1. Frame Count is transitions per particle
lifetime, not FPS. It targets an average 8.4 cells/s at the authored static lifetime
distribution; Life Span Variation is its full width, so the calculation uses half
that value on either side of the lifespan and compensates for reciprocal lifetime.
Frame Count Variation 1 gives stochastic integer rounding. Animated lifetimes can
change the rate. Both motion modifiers use order 3000 in the system's ordered
modifier list, with `NiPSysUpdateCtlr` last in its controller chain.
Independent sampling can still choose the same cell; there is no shared runtime
particle buffer or cross-system synchronization.

The dark shader reuses the exact source atlas path. Its material RGB is linear
0.006, scale 1; atlas RGB modulates this near-black material. Palette-colour and
palette-alpha are disabled for this pass, using the real atlas alpha mask.
Particle colour modifiers retain their alpha curve timing while RGB becomes white.
Opacity follows the source alpha envelopes with peak `handMarbleOpacity`.
Radius/alpha animation includes sequence-driven keys. Colour/emissive shader
animation is disconnected so it cannot relight the dark pass. The bright ramp and
its original palette sampling remain intact. Disconnected controller blocks remain
in the file: nifly deletion renumbers blocks but omits persisted blend interpolator
children from reference enumeration, so deleting these orphans could corrupt live links.

The marble's `NiAlphaProperty.flags = 4333 (0x10ED)`: SrcAlpha / InvSrcAlpha blending,
alpha test off. Bits 10-12 encode the alpha test function; No_Sorter (bit 13, `0x2000`)
is not set - the Skyrim effect path does not read it (1.6.1170 image, build/ordered/REPORT.md).
The source flame's alpha property is left exactly as it was. ZBuffer_Test (SLSF1 bit31) is on;
ZBuffer_Write (SLSF2 bit0) is off. Draw order comes from a new `BSOrderedNode` put in the
flame's place in its parent: children `[marble, flame]`, identity transform, always a dynamic
bound (flags `0x8000E`, no fixed bound - the particles move with the hand), so the pair keeps
that order whatever the camera angle; the node itself sorts against other objects by its
bound. A pair that a `BSMasterParticleSystem` references directly (`childRefs` /
`particleSysRefs`) is not wrapped (an ordered node there is unverified): the marble stays the
flame's direct preceding sibling and the reason is recorded. The casting-art key carries
`|handfx=2`, replacing the released `|marbleorder=1` token. Only an in-game capture
establishes the final look.

## Embers

Small additive fire-palette systems with at most one atlas cell and positive emitter
radius plus absolute variation no greater than 4 are embers rather than main flames;
`BSStripParticleSystem` is excluded. Names such as EmberCloud or FlashingSparks are not
selection rules. Their bright shader's `baseColorScale` and emissive animation are
doubled, clamped to 0..8, with shared shader and float paths isolated first.

Each also gets one dark alpha-blended clone at its original size, with one third
of its birth rate (two dark for six bright). Lifetime and activation timing stay
unchanged. Its peak opacity is `min(0.9, handMarbleOpacity)`, so 0.5 caps both dark
flames and dark embers at 0.5. Bright and dark embers use the same `BSOrderedNode`
pair rule, dark first, including the direct-sibling exception above. Their pool
capacity is included in the one-clone-per-source bound; reduced birth rate does
not reduce the reserved pool.

## Verification and preview

Builder tests serialize/re-read the clone, validate refs, inspect dumped blocks,
check rates/lifetimes/capacity, motion values and update order, ember brightness,
opacity-zero identity, dark ember opacity/count, blend/depth flags and sequence links, and compare
ordinary copies byte-for-byte against a build without a casting-art object.
Config/contract tests cover defaults, clamps, quantisation and role-specific keys.
Golden v2 lines and the released pin are unchanged. Dirty Release source reports
`2+dev<digest>`; a clean pinned Release reports `2` without an override option.

The animated preview (`build/hand-motion/preview/index.html`) uses the built NIFs'
bright palette, source atlas and dark material/motion settings in a linear-light
composition. It illustrates intended dark-before-additive ordering over bright,
middle and dark backgrounds. It is a 2D approximation, not a game capture or proof
of engine particle animation, draw order, bloom or tone mapping.

## Real dump verification (2026-10-20)

Round-2 rebuild (`build/hand-preview/impl/round2-real-validation.json`):
`fireball01handeffects`: two added NiParticleSystem graphs, 352 -> 524 blocks;
selected particle pools 190 -> 380. `firestormhandeffects`: one added
BSStripParticleSystem graph (a NiParticleSystem subtype), 271 -> 296 blocks;
selected pool 40 -> 80. Concrete particle/data types are preserved: the latter
uses its original fire-palette-coloured strip atlas, not Fireball's atlas.
The Fireball and Firestorm copies each re-read with zero unknown blocks, all
840/473 dumped references in range and byte-identical roundtrips (34,344/58,030 bytes).
All three marble alpha properties were 12525 in round 2; the ordered-node round set them to 4333 and wraps each pair (build/ordered/rebuilt). Both radius and alpha paths have separate
animation data, including sequence links; the shared-alpha fixture retains rate/lifetime
17 after opacity normalisation. I/O failures are non-reusable and recover on retry;
unsupported marble animation is explicitly a ramp-only `kFallback`. Normal smoke
reuses the ordinary palette key and copy. The current dump mask was used to regenerate
both source dumps (four original lines per physical line keep artifacts below 600 lines).
The Release build and explicit tests pass: greybuild 649, cosave-check 414, zero failures.
Native re-read verification (`build/hand-preview/impl/round2-exact-floats.log`) compares
emitter lifetimes, rate/lifetime controllers, blend values and sequence float keys,
including interpolation/tangents: all 590 Fireball and 3 Firestorm float comparisons
are bit-identical to the source. Fireball includes 18 sequence float links; Firestorm's
marble rate is the unchanged static 30. Boolean activation links are outside this check.
These checks do not establish Skyrim's particle appearance or draw order.
