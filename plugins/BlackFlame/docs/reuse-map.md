# Reuse map: what a new magic-effect recolour mod can take from Black Flame

Black Flame's state at 86b54422 (2026-10-20, run 10 passed by eye: rune glyph, hand casting art, stoked-kill
corpse fire). This page sorts the code into what is effect-neutral machinery and what is Black Flame's own
colour rule or trigger, so a mod that changes other magic effects can start from the first group.

## Effect-neutral machinery (take as is)

| Layer | Files | What it gives |
|---|---|---|
| Single launch | `SingleLaunch`, `BuilderLauncher`, `BuilderControl`, `BuilderSignal`, `BuilderFolderMutex`, `MenuWatch` | the builder runs in the background at the first start; the player never waits (W1 at the gate, W2 at the main menu) |
| Request and keys | `CopyContract*`, `CopyRequest*`, `SlotKeys`, `CopyContractIdentity` | one request.json per start, content keys (`<key16>`) over source bytes plus recipe, builder output identity pinned by golden digest |
| Slot pool | `SlotAssign`, `SlotLedger*`, `SlotCoSave`, `GreySlotPool`, `GreyEsl*` | 2000 placeholder records in an ESL, filled per load order without changing the plugin list |
| Progress and reuse | `BuildProgress*`, `ProgressTail`, `GreyProgress` | copies are built once and reused across starts; a crash mid-build resumes |
| Old copies | `OldCopies*` | keepsets per ledger, 14-day grace, usvfs real-path guard |
| Swap points | `CopySwap*`, `ProjectileHook`, `ExplosionHook`, `HazardHook`, `HitEffectHook`, `HandArtRefresh`, `CasterArt` | where a copy replaces the original at runtime (projectile, explosion, impact/IPCT decal, hazard, hit shader, casting art) for one caster only |
| Corpse instance | `CorpseMark`, `CorpseGrey`, `CorpsePalettes`, `SreProbe` | per-SRE swap of palettes and base/fill textures (bit 24 mark, hooks 34933+0x12D / 34934+0x2EC, private particles), 1.6.1170 and 1.7.104 only |
| Attribution | `HitPathAttribution`, `MagicApplySink`, `GameClock`, `ElementalHitRules.h` (pure, tested) | who last hit a target with which element, by game clock |
| Builder core | `GreyNif`, `GreyTexture*`, `GreyDump`, `GreyBudget`, `GreyGpuEncoder`, `GreyRoundTrip`, `GreyParticleGraph` | NIF/DDS read-modify-write with nifly and DirectXTex, memory admission, GPU BC7, particle graph cloning with ref remapping |
| Safety | `SehGuard`, `Watchdog`, `MainThreadQueue`, `GameThread`, `TrackedMutex`, `LogLimit` | crash-guarded hooks, main-thread work, bounded logs |
| Tests | `GreySelfTest*`, `CopyContractTest*`, `SelfTest*` | offline builder self-test with golden pin, shared contract tests, in-game PASS/FAIL self-test |

## Black Flame's own rules (replace per effect)

| Rule | Where | Black Flame's value |
|---|---|---|
| Trigger | `BuffState`, `FireSet`, `FireClosure`, data ESL `HoMBF_Stoke*` | the stoke buff, player's fire spells only |
| Element test | `EfshFireShare`, `kEfshBaseFireShare`, `IsFireEffect` | red-yellow hue share; another element needs its own hue rule and census |
| Colour recipe | `GreyPalette`, `GreyGlow`, `Tint`, `Recolor`, `GreyKeyedBases` (luma1) | grey ramp, `tintAdd`, luminance copies |
| Hand look | `GreyMarble`, `GreyHandFx`, `tintAddHands` | bright core + dark marble, embers |
| Risk check | `GreyRisk` | what grey may break (glow, emissive) |

A new effect family means: a new trigger, a new element test with its own measured threshold, a new colour
recipe with its own key token and recipe version, and a contract bump. The swap points and the corpse
instance swap stay as they are.

## Limits a new mod inherits

- Hooks in `CopySwapImpact` (rune window) and `CorpseGrey` are AE 1.6.1170 and 1.7.104 only (`runtimes.md`).
- Glow Be Gone nulls MGEF hit shaders; respected by design.
- Base texture dumps share a 256 MiB sub-cap; copies a 128 MiB total (`checks-and-limits.md`).
- FEC base/fill swap is built and keyed but not yet seen firing in game (run 10: no burnshader effect played).
