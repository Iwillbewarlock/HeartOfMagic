#pragma once

#include "Common.h"

#include <string>
#include <string_view>

// =============================================================================
// EnvironmentSelfTest - in-game checks for 1.6's environment work (dev only)
//
// Off unless Data/SKSE/Plugins/SpellLearning/selftest.json says
// {"enabled": true} (not shipped; DeployDev.ps1 -WithSelfTest puts it in the dev
// install). Armed at a save load or a new game, it starts once per game session
// after delaySeconds (default 30) of unpaused play with the player in a loaded
// cell, then writes one "[selftest-1.6] <id> PASS|FAIL|SKIP|ANSWER ..." line per check
// to SpellLearning.log. What each check reads: docs/ENVIRONMENT.md, "Self-test".
// =============================================================================

namespace Environment::SelfTest
{
    inline constexpr const char* kSwitchFile = "Data/SKSE/Plugins/SpellLearning/selftest.json";
    inline constexpr const char* kPrefix = "[selftest-1.6]";

    // Game thread: kPostLoadGame and kNewGame.
    void OnGameLoaded();

    // The checks (EnvironmentSelfTestChecks.cpp), game thread, each writes its own lines.
    void CheckH2();
    void CheckTwins();
    void CheckMysticismPatch();
    void CheckFrostwalker();
    void CheckAdapters();
    // H1-d (ProgressionNotifySelfTest.cpp): each notice sees the state after the
    // change that made it, in the order it was made. Changes the save, then undoes it.
    void CheckProgressionNotify();
    void CheckSpellTomes();   // EnvironmentSelfTestTomes.cpp
    void CheckClearedFlagTomes();   // EnvironmentSelfTestTomes.cpp
    void CheckGameThreadFlag();   // EnvironmentSelfTestTomes.cpp
    void CheckOwnedOutside();     // EnvironmentSelfTestTomes.cpp

    enum class Outcome
    {
        kPass,
        kFail,
        kSkip
    };

    // One result line, counted for the DONE line. FAIL means a defect.
    void Report(std::string_view id, Outcome outcome, const std::string& observed);
    // An experiment's finding (H2): "ANSWER yes|no", counted apart from FAIL.
    void Answer(std::string_view id, bool yes, const std::string& observed);
    void Note(std::string_view id, const std::string& text);
}
