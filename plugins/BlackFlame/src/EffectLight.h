#pragma once

#include "Common.h"

// =============================================================================
// EFFECT LIGHT - the per-instance light colour of the player's fire (config light)
// =============================================================================
// Projectile, explosion, hazard, casting, light-effect and script-placed lights. No LIGH copy exists (the copies
// keep the source's light link), so this per-instance tint stays after C6 retired the runtime material recolour.

namespace BlackFlame
{
    /// The NiLight behind a light slot whose declared type may not be what it holds (a projectile's
    /// BSLight slot was the prime suspect for the AE fault): asked through MSVC RTTI, a BSLight gives its
    /// NiLight. a_kind names what it was ("BSLight", "NiLight", "null" or neither).
    RE::NiLight* AsNiLight(RE::NiRefObject* a_raw, const char*& a_kind);

    /// AsNiLight, and the first time per a_owner (a string literal: "projectile", "explosion",
    /// "hazard", "casting") its real type is logged. Every light the layers tint goes through this.
    RE::NiLight* CheckedLight(RE::NiRefObject* a_raw, const char* a_owner);

    /// Sets a light's diffuse colour to config lightColor. False for a null light.
    bool TintLight(RE::NiLight* a_light);
}
