#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - BURN SHADER
// =============================================================================
// A fire spell's fire effect (FireSet IsBurnable: a scroll, staff or
// enchantment sharing the MGEF does not count) the player applied while stoked gets the
// grey HoMBF_BurnShader at once, on the first attempt, for the effect's remaining
// time (1.5 s for an instant hit); a grey shader already live on that target is
// extended instead of stacked. Config burnMode "replace" (default) also finishes
// the engine's hit shader (whichever EffectSetting::data.effectShader the MGEF
// names) found in that effect's own hitEffects - never target-wide, which would
// also stop an ally's identical shader; when hitEffects is not filled yet that is
// tried once more on a later ticker tick. "overlay" leaves the engine's shader.
// While an effect with a duration or a concentration cast lasts, its grey shader is
// kept alight every tick; when the effect is removed or gone and the player has no
// other fire effect on the target, the grey shader is finished. Replace mode may still
// flash orange for a frame.

namespace BlackFlame
{
    /// From an apply event of the player's: on the game thread it reads the effect (read only)
    /// while it is still in the target's list; then it defers the burn to a game-thread task
    /// carrying the handle, the id and what it read (an instant effect may be gone by the task:
    /// the grey shader still applies from those facts; off the game thread there are none).
    void OnPlayerEffectApplied(RE::TESObjectREFR* a_target, std::uint16_t a_activeEffectId);

    /// From a removal event of the player's (any thread): ends a tracked burn when no other
    /// player fire effect is left on the target.
    void QueueBurnEnd(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId);

    /// Game thread, every ticker tick: engine-shader retries and live burns kept alight.
    void TickBurns();

    /// Any thread: burns or retries are pending (the ticker then stays at 100 ms).
    [[nodiscard]] bool HasLiveBurns() noexcept;

    /// Game thread, at kPreLoadGame / kNewGame: drops every tracked burn and retry
    void ResetBurnState();
}
