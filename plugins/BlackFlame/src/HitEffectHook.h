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
//   - GetHitEffectArt returns the engine's art unchanged; the art's 3D is
//     recoloured once it exists (config hitArt, HitArt.h).
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
        std::uint64_t swapped = 0;
        std::uint64_t engineNull = 0;      // the engine answered null
        std::uint64_t engineNullFlag = 0;  // of those, with flags bit 1 (the engine's no-hit-shader bit)
        std::uint64_t notStoked = 0;
        std::uint64_t notFire = 0;
        std::uint64_t otherRejected = 0;   // no burn shader form, already ours, or the config key off
    };

    [[nodiscard]] HitSlotStats GetHitSlotStats(bool a_shader) noexcept;

    /// At kPreLoadGame / kNewGame: forgets the cached player handle (read again on the next call)
    void ResetHitEffectState();
}
