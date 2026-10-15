#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - BURN SHADER
// =============================================================================
// A fire spell's effect the player applied while stoked: the engine's hit shader
// (EffectSetting::data.effectShader) on the target is finished and the grey
// HoMBF_BurnShader is applied for the effect's remaining time. Swapped after the
// fact, so Flames (re-applied while held) may flicker orange for a frame.

namespace BlackFlame
{
    /// From the effect event (any thread): defers the swap to a game-thread task.
    void QueueBurnShaderSwap(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId);
}
