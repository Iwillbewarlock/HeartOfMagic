#pragma once

#include "Common.h"

#include <cstdint>

// =============================================================================
// IMPACT SWAP - the grey copies of impact meshes, for the player's fire only
// =============================================================================
// Projectile::SpawnCollisionEffects (REL id SE 43022 / AE 44213) spawns an impact
// effect from the impact data's model (TESModel::GetModel, vfunc 4). Its three
// call sites (in ProcessImpacts 42943/44123, 43014/44205, 42638/43806) are found by
// scanning those functions for an E8 call to it and redirected through the SKSE
// trampoline (chained when another plugin redirected the call first). Around the
// call a thread_local window opens when the projectile is the player's fire
// (Attribution.h) and the buff is on; the explosion side opens the same window
// around Explosion::Update. While a window is open, BGSImpactData's TESModel
// GetModel (VTABLE_BGSImpactData[1], slot 4) returns the grey copy when the grey
// map has one. No form is changed and no form is made; NPC fire never opens a window.
// Every REL id is checked in the Address Library first (REL::Offset2ID): a missing id
// leaves the swap off and is logged, never a failed lookup.

namespace BlackFlame
{
    /// RAII: open while the current call belongs to the player's fire (a_open), closed otherwise, on this
    /// thread; the previous state comes back at the end, so the innermost call decides
    class ImpactWindow
    {
    public:
        explicit ImpactWindow(bool a_open) noexcept;
        ~ImpactWindow();
        ImpactWindow(const ImpactWindow&) = delete;
        ImpactWindow& operator=(const ImpactWindow&) = delete;

    private:
        bool m_previous;
    };

    /// This thread is inside an open window
    [[nodiscard]] bool ImpactWindowOpen() noexcept;

    /// kDataLoaded, after the grey map and the fire sets: finds the call sites and hooks (config impactSwap)
    void InstallImpactSwap();

    struct ImpactSwapStatus
    {
        bool idsPresent = false;
        int sitesHooked = 0;   // of 3
        int sitesChained = 0;  // of those, onto another plugin's redirect
        bool modelHooked = false;
    };

    [[nodiscard]] ImpactSwapStatus GetImpactSwapStatus() noexcept;

    /// GetModel calls that returned a grey copy (for the self-test)
    [[nodiscard]] std::uint64_t ImpactSwapCount() noexcept;

    /// True when one of the set's impact models has a grey copy in the map (window closed: original paths)
    [[nodiscard]] bool ImpactSetHasGreyModel(const RE::BGSImpactDataSet* a_set);
}
