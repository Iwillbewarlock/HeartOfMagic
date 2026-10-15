#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - BURN SHADER
// =============================================================================
// A fire spell's effect the player applied while stoked gets the grey
// HoMBF_BurnShader for the effect's remaining time. Config burnMode "replace"
// (default) first finishes the engine's hit shader (whichever
// EffectSetting::data.effectShader the MGEF names); "overlay" leaves it and puts
// ours on top. Swapped after the fact, so Flames (re-applied while held) may
// flicker orange for a frame in replace mode.

namespace BlackFlame
{
    /// From the effect event (any thread): defers the swap to a game-thread task.
    void QueueBurnShaderSwap(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId);

    /// Game thread, from the ticker: the second and last attempt of swaps whose hit
    /// shader was not there yet at the first.
    void RunPendingBurnSwaps();
}
