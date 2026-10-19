#pragma once

#include "Common.h"

// =============================================================================
// HIT PATH OBSERVATION (2026-10-19) - log only, nothing changed
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
// AE 1.6.1170 only (the offsets come from its code); any other runtime logs one line and
// installs nothing. Each site's bytes are checked first; a mismatch is logged and skipped.

namespace BlackFlame::HitPath
{
    /// kDataLoaded, after BuildFireSets (the fire MGEFs' hit shaders are read from the fire sets)
    void Install();

    /// A stoke window starts (HitEffectHook OnStokeWindowChanged): the window counts start again; a_capsReset also
    /// gives the log lines their caps back (the same first-3-windows rule as the 0x0D / 0x0C lines)
    void ResetWindow(bool a_capsReset) noexcept;

    /// One "hit path stats" line (window end, and every 30 s while stoked, with the 0x0D / 0x0C stats lines)
    void LogStats(const char* a_when, double a_seconds, const char* a_reason);
}
