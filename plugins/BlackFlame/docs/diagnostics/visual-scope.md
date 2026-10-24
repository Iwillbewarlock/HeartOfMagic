# HoM catalog visual scope and shared-effect isolation

2026-10-23. The refresh-only Fire Storm change ran, but the user reported the same
red appearance. The winning spell record also includes secondary MGEF 000F3931,
whose equip ability is 000225AE -> MGEF 00022477 -> ARTO 00022471,
`IllusionMassRedCastBodyFX`. The 20:57:04 art hook kept that original because the
secondary MGEF failed the old fire-effect gate. This identifies an omitted visual
path; it does not prove which remaining screen pixels originate there.

## Selection and ownership

`VisualSet` starts only from ordinary Spell roots tagged `fire` in Heart of Magic's
catalog. Missing or empty input admits nothing. Every MGEF of a qualifying root
and recursively associated Cloak spells is a private-copy candidate. A fire
keyword alone does not admit visual roots. Copy construction does not edit the
shared source records or assets and does not itself authorize a runtime swap.

The graph is also walked from non-catalog ordinary roots. A spell reached from
any such root is excluded at runtime, even if it is also a catalog root: an
ActiveEffect names its immediate spell, not the originating cloak. This
conservative rule can preserve original colours for a legitimate fire cast when
the same spell is shared with another root. Per-root visited sets stop cycles.

Ordinary effects are checked by both MGEF and actual spell, so reusing a MGEF in
an unrelated nonfire spell does not authorize that spell. Equip abilities are
copy candidates through their parents but never become ordinary visual spells
through that relation. Their current ownership is decided separately:

- At least one selected root must own the ability and qualify as visual.
- No selected nonvisual root may own that same ability.
- An unrelated nonvisual spell in another slot is irrelevant.
- Missing owners, nonfire-only selection or mixed owners keep original visuals.
- Player caster, player target and active/not-dispelled guards are required.

The getter reads live selected spell identities; the tick only detects changes
and requests refresh. It does not publish a cached eligibility boolean. Buff
state is gated separately by the hit hook. The ownership predicate can remain
true with the buff off so the refresh can rebuild original visuals.

## Copies, refresh and gameplay

The existing supported copy graph follows the qualifying roots' visual links,
including secondary hit/casting art, equip-ability art/shader, projectiles,
explosions, hazards, impacts and supported child visual assets. Shared originals
remain available to NPCs and nonvisual casts. Existing feature settings and
available copies still limit coverage; “all MGEFs” does not mean every possible
engine/plugin-generated pixel has a known interception path.

Equip hit art requires a private copy. Runtime recolouring without a copy is
refused here because a surviving shared ability must be identifiable for later
restoration. On AE 1.6.1170, both listed and unlisted abilities use Stop/Detach and
engine 34094 to rebuild only visuals on buff/selection/world changes. Current
eligibility or an existing copy/our burn shader permits refresh; the latter
restores a visual after ownership is lost. No RemoveSpell/AddSpell, gameplay
dispel/recast or shared record rewrite is used.

`FireSet`, mana-cost rules, lantern damage and corpse kill attribution retain
their prior gameplay classification. The visual set does not broaden those
decisions. A spell can qualify for gameplay fire while remaining visually
unchanged because it is absent from the catalog or shared ambiguously.

## Validation and remaining limits

Current offline checks pass: 61 actual refresh/context CPP checks, 13 closure
checks, 22 visual-set checks, 21 script-provenance seam checks and 20 pure ownership-policy checks. The old global
ability-set policy failed the nonfire-only and mixed-owner expectations; it had
no selected-root input. The current fake-engine harness compiles unmodified
production sources and exercises immediate selected-root changes and restoration.

BuildRelease Release-2026 and its final incremental build passed. Builder golden
tests passed 770/0; cosave/policy tests passed 510/0 with one junction group skipped
for filesystem permission. Two independent reviews passed after the shared-art
fallback and script-provenance findings were fixed. Removing the two reviewed
script guards in an explicit mutant makes 5 of 21 seam checks fail. Dev DLL/PDB
deployment is hash-verified; builder/config remain unchanged. Game validation is pending.
Offline results do not establish engine ABI/lifetime, final hook return values,
asynchronous 3D attachment or screen colour. The previous refresh-only build's
successful native tests must not be reported as validation of this scope change.

In game, compare Fire Storm with lantern off/on/off, switch to a nonfire ritual
sharing the ability, then back. Check NPC equivalents, mixed owners where
possible, full unequip and save/load. The sampled caster lights were already
tinted and the observed LightPlacer body light was neutral; unsampled world/GPU
lights and other art remain possible contributors to any remaining red pixels.
