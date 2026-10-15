#pragma once

#include <cstddef>

// =============================================================================
// VTABLE SLOTS shared by the reference hooks (SE/AE; VR is not built)
// =============================================================================

namespace BlackFlame
{
    inline constexpr std::size_t kSet3DIndex = 0x6C;       // RE/T/TESObjectREFR.h: Set3D (Explosion and Hazard do not override it)
    inline constexpr std::size_t kInitializeIndex = 0xA2;  // RE/E/Explosion.h and RE/H/Hazard.h: Initialize
}
