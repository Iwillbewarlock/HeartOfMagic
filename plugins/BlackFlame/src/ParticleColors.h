#pragma once

#include "Common.h"

// =============================================================================
// PARTICLE COLOURS - BSPSysSimpleColorModifier desaturation (config particleColors)
// =============================================================================
// A particle system's simple colour modifier multiplies the palette with its own
// colour keys, so orange keys keep a grey palette orange. CommonLib has no layout
// for the modifier; the one used here is from the Jonahex/CommonLibSSE fork and
// is not verified on AE. Off by default: the modifier may be shared between
// instances, which the in-game toggle test (README) must rule out first.

namespace BlackFlame
{
    /// Game thread only. Sets the RGB of every colour key of the simple colour
    /// modifiers on a_particles to its luminance (alpha kept). Returns how many
    /// modifiers changed (an already grey one does not count).
    int DesaturateColorModifiers(RE::NiParticleSystem* a_particles);
}
