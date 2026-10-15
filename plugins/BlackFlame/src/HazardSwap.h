#pragma once

#include "Common.h"

#include <cstdint>

// =============================================================================
// HAZARD SWAP - the grey copies of hazard meshes, for the player's fire only
// =============================================================================
// TESObjectREFR::Load3D (vfunc 0x6A, not overridden by Hazard) loads the base's
// model through TESModel::GetModel (vfunc 4). Load3D is hooked on the Hazard
// vtable: when the hazard is the player's fire (owner, blame or script tag;
// a hazard created inside an open impact window - the player's impact - is tagged
// first) and the buff is on and the grey map has a copy of its base's model, a
// thread_local names the grey path for that base during the original Load3D, and
// BGSHazard's TESModel GetModel (VTABLE_BGSHazard[2], slot 4) returns it. No form
// is changed. A hazard whose owner is not known at Load3D keeps the original mesh
// (the per-instance recolour of HazardHook.h still applies later).

namespace BlackFlame
{
    /// kDataLoaded, after the grey map and the fire sets (config hazardSwap)
    void InstallHazardSwap();

    [[nodiscard]] bool HazardSwapHooked() noexcept;

    /// Load3D calls that loaded a grey copy (for the self-test)
    [[nodiscard]] std::uint64_t HazardSwapCount() noexcept;
}
