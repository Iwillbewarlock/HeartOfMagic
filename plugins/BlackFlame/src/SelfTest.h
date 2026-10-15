#pragma once

#include "Common.h"

// =============================================================================
// SELF-TEST - a scripted in-game check of every automatable layer (config selfTest)
// =============================================================================
// Armed at kPostLoadGame when config selfTest is true, once per game session (v2 E3 resumes once more after the
// reload it asks for itself, SelfTestReload.h). It
// starts when the player is in a loaded cell, the game is not paused and ~3 s
// have passed, then runs a state machine on the game thread, one step per ticker
// tick (100 ms; nothing sleeps on the game thread). Every step has a timeout: a
// timeout is a FAIL with its reason. Results: "[SELFTEST] PASS|FAIL|INFO <id>
// <detail>" in HeartOfMagic_BlackFlame.log and HeartOfMagic_BlackFlame_selftest.txt,
// ending with "[SELFTEST] DONE pass=N fail=M info=K". It casts spells, spawns two
// vanilla actors (deleted at the end): use a throwaway save (README "Self-test").

namespace BlackFlame::SelfTest
{
    /// kPostLoadGame, game thread
    void OnGameLoaded();

    /// kPreLoadGame / kNewGame (the world is torn down): a running test stops without touching
    /// its old handles; an armed one is disarmed
    void Abort(const char* a_reason);

    /// The ticker's game-thread task, every tick
    void Tick();

    /// True while armed or running: the ticker then ticks every 100 ms even with the buff off
    [[nodiscard]] bool WantsFastTicks() noexcept;
}
