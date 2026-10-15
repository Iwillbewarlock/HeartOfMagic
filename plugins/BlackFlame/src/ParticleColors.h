#pragma once

#include "Common.h"

#include <array>
#include <span>

// =============================================================================
// PARTICLE COLOURS - BSPSysSimpleColorModifier desaturation (config particleColors)
// =============================================================================
// A particle system's simple colour modifier multiplies the palette with its own
// colour keys, so orange keys keep a grey palette orange. CommonLib has no layout
// for the modifier; the one used here is from the Jonahex/CommonLibSSE fork and
// is not verified on AE. Off by default: the modifier may be shared between
// instances, which the self-test (T4/T7) must rule out first. The read helpers
// below are shared with the self-test, so both use the one layout.

namespace BlackFlame
{
    inline constexpr std::size_t kColorKeysOffset = 0x30;  // three RGBA keys, by the fork's layout
    inline constexpr std::size_t kColorKeyFloats = 12;
    using ColorKeys = std::array<float, kColorKeyFloats>;

    /// The modifier is a BSPSysSimpleColorModifier (exact NiRTTI)
    [[nodiscard]] bool IsSimpleColorModifier(const RE::NiPSysModifier* a_modifier);

    /// The 12 floats at kColorKeysOffset, read raw (no write)
    [[nodiscard]] ColorKeys ReadColorKeys(const RE::NiPSysModifier* a_modifier);

    /// Every value finite and within 0..2, and at least one above 0: they read as colours, so the
    /// layout matches this runtime (0..2 leaves room for HDR-ish keys above 1 while catching
    /// floats that are really pointers or counts; all zero would be an empty or wrong read)
    [[nodiscard]] bool KeysPlausible(std::span<const float> a_keys);

    /// Game thread only. Sets the RGB of every colour key of the simple colour
    /// modifiers on a_particles to its luminance (alpha kept), skipping any whose keys
    /// are not plausible. Returns how many modifiers changed (an already grey one does
    /// not count).
    int DesaturateColorModifiers(RE::NiParticleSystem* a_particles);
}
