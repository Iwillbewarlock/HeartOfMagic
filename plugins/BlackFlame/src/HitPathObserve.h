#pragma once

#include "Common.h"

// =============================================================================
// HIT PATH OBSERVATION (2026-10-19) - log only, nothing changed (except item 6)
// =============================================================================
// Why run 5's player fire spells damaged NPCs while no 0x0D / 0x0C call (HitEffectHook)
// carried a fire MGEF. Decrypted 1.6.1170 code: a target's hit ShaderReferenceEffect for an
// ActiveEffect starts in 34094 (asked from ActiveEffect::Update 34092 at +0x23E "site A" and
// +0x4CD "site B"), which asks 0x0C, then 0x0D, the gate 40508, SetTarget 34833 and either runs
// ReferenceEffectController::Start 34761 inline or queues it (36907 -> 36991 -> 34065 -> 34761;
// a site line does not tell the two apart, its thread does: main or not); 34761 is also run by
// 26275 and 34154, and 34910 by LoadGame 34071 - those paths are not observed here (review);
// ApplyEffectShader 19872 (Papyrus, console,
// enchantments) builds an OwnedController and calls the ShaderReferenceEffect constructor
// 34910 at +0x1AC. The decrypted image showed another plugin's jump at 40508's entry.
//   1. kDataLoaded: one line per function of that path with its first 16 bytes and, when it
//      starts with a jump, the module the jump chain ends in (read only).
//   2. The two 34092 -> 34094 calls and the 19872 -> 34910 call are redirected through
//      write_call<5> to thunks that call the original with the same arguments, return its
//      result and count; the first lines of each stoke window are logged.
//   3. (2026-10-20) Corpse attribution: an ApplyEffectShader line on an actor also carries its
//      life state, its killer handle (the player or not), the shader's fill / edge colours, and
//      how long ago it last took an elemental (fire, frost or shock) hit, from whom and of which
//      element (a 64-target memory of the last hit per target, any caster - the player's, or a
//      non-player Actor's other than the target - fed from the 0x0D thunk, NoteFireEffect, and
//      the magic effect apply sink, MagicApplySink.h; the oldest hit replaced); lines on actors that are
//      not alive or were killed by the player, once the windowed pool is full, have a cap of 48
//      of their own that no stoke window gives back (HitPathAttribution.h).
//   4. (2026-10-20) LogHitShaderProbe: a few fire MGEFs' shader fields (and whether each
//      effectShader is in the kDataLoaded fire-shader set) and the null / set count over every
//      fire effect of a fire spell, at kDataLoaded (inside BlackFlame's own handler), the main
//      menu, each kPostLoadGame and each load's first stoke window (any runtime: form reads).
//   5. (2026-10-20, run 8 probe) The corpse mark in the 19872 -> 34910 thunk after the ctor
//      returns, the NPC-fire-on-corpse lines, the SRE vtable report and the GetTexture probe:
//      CorpseMark.h and SreProbe.h.
//   6. (2026-10-20, design v2 §3) Not log only: the grey palette swap of a marked SRE at
//      34933 -> 34934 (hook A, the former attach probe) and its private particle property at
//      34934 -> 17325 (hook B): CorpseGrey.h, gated by greyCorpseShaders.
// AE 1.6.1170 only (the offsets come from its code); any other runtime logs one line and
// installs nothing. Each site's bytes are checked first; a mismatch is logged and skipped.

namespace BlackFlame::HitPath
{
    /// kDataLoaded, after BuildFireSets (the fire MGEFs' hit shaders are read from the fire sets)
    void Install();

    /// After Install: the ApplyEffectShader hook (the corpse mark's thunk, CorpseMark.h) was written
    [[nodiscard]] bool CorpseMarkInstalled() noexcept;

    /// A stoke window starts (HitEffectHook OnStokeWindowChanged): the window counts start again; a_capsReset also
    /// gives the log lines their caps back (the same first-3-windows rule as the 0x0D / 0x0C lines)
    void ResetWindow(bool a_capsReset) noexcept;

    /// One "hit path stats" line (window end, and every 30 s while stoked, with the 0x0D / 0x0C stats lines)
    void LogStats(const char* a_when, double a_seconds, const char* a_reason);

    /// Any thread (HitEffectHook's 0x0D thunk, every call, outside the swap's guard): when a_effect is an elemental effect
    /// (fire: a fire effect of a fire spell or a fire MGEF by the rule; frost or shock: MagicApplySink.h ElementOf) cast by
    /// the player or by a non-player Actor other than the target, its target's slot takes this hit, its source
    /// (Detail::FireSource) and element (Detail::RecordElementalHit; the player's non-stoked fire on a dying or dead target
    /// keeps a stoked record). A non-player caster's handle is resolved here (the player's is a value compare). One with no
    /// caster, the target itself as caster or a caster that is not an Actor (FEC may apply a fire effect to the corpse
    /// itself) is not recorded, so it never turns the last hit into "NPC". A non-player fire effect on a dying or dead actor
    /// also gets one capped line (saying whether it was recorded). Read back by the ApplyEffectShader line and by the corpse
    /// mark (CorpseMark.h)
    void NoteFireEffect(RE::ActiveEffect* a_effect);

    /// kPreLoadGame / kNewGame (ResetHitEffectState): the remembered fire hits are forgotten (FF refs are reused)
    void ClearFireHits() noexcept;

    /// Log only, any runtime: the probe MGEFs' shader fields and the fire effects' data.effectShader null / set count,
    /// each line tagged with a_point; a fault or a std::exception skips the probe (logged) and never reaches the caller
    void LogHitShaderProbe(const char* a_point);
}
