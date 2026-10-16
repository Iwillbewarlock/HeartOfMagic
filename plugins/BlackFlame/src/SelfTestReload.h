#pragma once

#include "SelfTestCtx.h"

// =============================================================================
// SELF-TEST E3 - a grey Fire Rune across a save and a reload (v2 spike, semi-manual)
// =============================================================================
// The last step of a run (after the cleanup T9): the Stoke buff cast again, a vanilla
// Fire Rune launched at a marker 300 units ahead, its projectile checked (base = the
// copy, explosion = a copy), a marker file (HeartOfMagic_BlackFlame_e3.json next to
// the self-test file: rune FormID, copy base, cell, game days, session nonce) written,
// and one line asks the player to quicksave and quickload. The shipped DLL never saves
// or loads by itself; only the dev key devAutoE3 (DevAutomation.h) saves and loads
// HoMBF_E3 for an unattended run. On the next kPostLoadGame that succeeded, came after its kPreLoadGame and
// within kE3Window of the request, with the marker file matching this session, in the
// same cell and not earlier in game time: the rune is found again (FormID, else a
// projectile of the copy base in the cell) and judged - base the copy, explosion a
// copy (PostLoad3D resets a permanent projectile's explosion from the copy PROJ's
// link). Any other outcome is INFO "E3 not run (manual step skipped)". The cleanup (rune
// and marker removed; the Stoke buff dispelled only where E3 cast it or its rune was found
// by FormID in its own save) runs only in the world E3 ran in or in a save shown to be its
// own (same cell, within a game day after the request); the marker file is always deleted.
// Results are appended to the self-test file ending "DONE E3".

namespace BlackFlame::SelfTest::Reload
{
    inline constexpr auto kE3Window = std::chrono::minutes(10);

    /// The step (registered after T9 in the step table)
    bool StepE3(Ctx& a_c, bool a_first);

    /// kPreLoadGame (message handler): the load the player was asked for may be starting
    void OnPreLoadGame();

    /// kNewGame: E3 not run (the world it asked about is gone)
    void OnNewGame();

    /// kPostLoadGame with the message's success flag: true when E3 takes this load (its resume is armed)
    bool OnPostLoadGame(bool a_success);

    /// Every self-test tick: the resume after the load, or the window running out without one
    void Tick();

    /// Waiting for the player's reload or resuming (the ticker stays fast)
    [[nodiscard]] bool Active();

    /// SelfTest Finish (not for a load): E3's step ended by its timeout or an exception before it armed - its rune,
    /// marker and buff are cleaned up in this world
    void OnRunFinished(bool a_forLoad);

    /// Shifts E3's step clock by paused time (SelfTest ShiftClocks)
    void ShiftClocks(Clock::duration a_paused);
}
