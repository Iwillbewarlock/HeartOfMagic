#pragma once

#include "SelfTestCtx.h"

// =============================================================================
// SELF-TEST E3 - a grey Fire Rune across a save and a reload (v2 spike)
// =============================================================================
// The last step of a run (after the cleanup T9): the Stoke buff cast again, a vanilla
// Fire Rune launched at a placed marker 300 units ahead, its projectile checked
// (base = the copy, explosion = a copy), the game saved to the throwaway slot
// kSaveName, and once the run has finished (DONE written) that save is loaded. After
// the load the self-test resumes on its own: the rune found again by its FormID (or
// by a projectile of the copy base in the player's cell), alive, base = the copy, and
// its explosion still a copy (PostLoad3D resets a permanent projectile's explosion
// from base->data.explosionType: the copy PROJ's link to the explosion copy). Results
// are added to the self-test file with "DONE E3". Game thread only.

namespace BlackFlame::SelfTest::Reload
{
    inline constexpr const char* kSaveName = "HoMBF_SelfTest_E3";

    /// The step (registered after T9 in the step table)
    bool StepE3(Ctx& a_c, bool a_first);

    /// Called by SelfTest::Tick after the run finished: loads the save the step wrote, once
    void LoadIfRequested();

    /// kPostLoadGame: true when this load is the one E3 asked for (the resume is then armed)
    bool OnGameLoaded();

    /// While armed: the resume's own state machine (player ready, then judge, then DONE E3)
    void Tick();

    /// Armed or waiting for its load (the ticker stays fast; Abort stays quiet)
    [[nodiscard]] bool Active() noexcept;

    /// kPreLoadGame / kNewGame that is not E3's own load: forgets everything
    void Cancel(const char* a_reason);
}
