#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - HIT SHADER AND HIT ART, where the engine asks for them
// =============================================================================
// Every hit shader and hit art an active effect puts on an actor comes from its
// ActiveEffectReferenceEffectController: GetHitEffectShader (vtable 0x0D) and
// GetHitEffectArt (0x0C). Both are hooked. While the player is stoked, for an
// effect the player cast whose MGEF is a fire effect of a fire spell (FireSet
// IsBurnable: a scroll, staff or enchantment sharing the MGEF does not count):
//   - GetHitEffectShader returns our grey HoMBF_BurnShader instead of the engine's
//     (config burnShader), so the engine makes the hit shader from ours: instant
//     hits, concentration and cloaks (caster = target = player) alike, no event,
//     no record changed, nothing to finish. NPC effects get the engine's shader.
//   - GetHitEffectArt returns the art's grey copy when it has one (config copyHitArt,
//     FormCopies.h: the engine then makes the art's 3D from the copy's grey NIF);
//     without a copy the engine's art is returned and its 3D recoloured once it
//     exists (config hitArt, HitArt.h).
// What decides is the state when the engine asks: a shader or art made while
// stoked stays grey after the buff ends; one made before the buff stays as it was.

namespace BlackFlame
{
    /// After the fire sets are built (kDataLoaded): hooks 0x0D and 0x0C (always: diagnostics); config burnShader and
    /// hitArt decide the swap and the recolour
    void InstallHitEffectHooks();

    /// Any thread: a fire effect of a fire spell the player cast (caster compared by handle, cheap)
    [[nodiscard]] bool IsPlayerFireEffect(RE::ActiveEffect* a_effect);

    /// S3 counters of one slot (true: GetHitEffectShader 0x0D, false: GetHitEffectArt 0x0C), calls on effects the
    /// player cast only, by outcome
    struct HitSlotStats
    {
        std::uint64_t playerCalls = 0;
        std::uint64_t swapped = 0;         // shader: ours returned; art: remembered for the runtime recolour
        std::uint64_t copied = 0;          // art: the art's copy returned (v2 ARTO copies)
        std::uint64_t engineNull = 0;      // the engine answered null
        std::uint64_t engineNullFlag = 0;  // of those, with flags bit 1 (the engine's no-hit-shader bit)
        std::uint64_t notStoked = 0;
        std::uint64_t notFire = 0;
        std::uint64_t otherRejected = 0;   // no burn shader form, already ours, or the config key off
    };

    [[nodiscard]] HitSlotStats GetHitSlotStats(bool a_shader) noexcept;

    /// Any thread: the player's reference handle (cached; 0 before the player exists)
    [[nodiscard]] std::uint32_t PlayerHandleNative();

    /// At kPreLoadGame / kNewGame: forgets the cached player handle (read again on the next call)
    void ResetHitEffectState();

    /// H7: the per-call diagnostic lines start counting again (each self-test step start, so a late step's lines show)
    void ResetHitDiagnostics() noexcept;

    /// Phase 0, game thread, when the stoke buff flag changes (RecomputeBuff) or is dropped by a load (ResetBuffFlag).
    /// On: the diagnostic lines' caps and the window's counts by outcome start again (GetHitSlotStats is not reset).
    /// Off: one "hit shader stats" and one "hit art stats" line for the window that ends.
    void OnStokeWindowChanged(bool a_on, const char* a_reason);

    /// Game thread, each tick while stoked: the same stats lines every 30 s of the window
    void TickHitEffectStats();
}
