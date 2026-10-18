#pragma once

#include "Common.h"

// =============================================================================
// DEV AUTOMATION - the self-test run without anyone at the game (dev only)
// =============================================================================
// Three config keys, off by default, absent from the shipped json and the tester package, honoured only
// with selfTest on (testerMode counts; Config.cpp GateDevKeys):
//   devAutoLoad "<save>"  the first time the main menu opens in a session, kMenuSettle later, that save is
//                         loaded (BGSSaveLoadManager::Load on the main thread). Once per session; never after
//                         any load has started (a load by hand wins).
//   devAutoE3 true        when E3 asks for a save and a reload, kBeforeSave later the plugin saves kE3Save,
//                         waits for SKSE's kSaveGame plus kAfterSave (or kSaveSignalWait without it), then
//                         loads kE3Save. E3's own checks (marker nonce, cell, game time, preload seen, window)
//                         judge that load exactly as a player's quickload.
//   devAutoQuit true      kQuitDelay after the self-test's DONE (DONE E3 when E3 armed), Main::quitGame is
//                         set on the main thread. kStallTimeout after the last load (or the auto load's
//                         request, or the main menu's first opening with devAutoLoad set: the timeouts count
//                         from the main menu, single-launch v3 decision 8) without that, it quits anyway and
//                         logs the timeout; kMenuWaitTimeout after kDataLoaded when the main menu never opens.
// The main menu's first opening comes from MenuWatch (the shared MenuOpenCloseEvent sink). With singleLaunch the auto load
// also waits for the slot publication to settle (SingleLaunch::ReadyForLoad), kSlotWait at most.
// One detached thread polls every kPoll; every engine call goes through MainQueue::Post. Each step is logged.

namespace BlackFlame::DevAutomation
{
    inline constexpr auto kMenuSettle = std::chrono::seconds(5);
    inline constexpr auto kBeforeSave = std::chrono::seconds(3);
    inline constexpr auto kAfterSave = std::chrono::seconds(3);
    inline constexpr auto kSaveSignalWait = std::chrono::seconds(10);
    inline constexpr auto kQuitDelay = std::chrono::seconds(10);
    // the main run's step caps add up to 270 s of play (SelfTestSteps.cpp, T0's probe wait aside), plus selfTestDelaySeconds,
    // the load and E3's save and load: 6 minutes left about a minute (2026-10-18), so 12
    inline constexpr auto kStallTimeout = std::chrono::minutes(12);
    inline constexpr auto kMenuWaitTimeout = std::chrono::minutes(20);  // kDataLoaded to the main menu took ~7 min (run 4)
    inline constexpr auto kSlotWait = std::chrono::minutes(5);  // single launch: the auto load waits for the slots (under kStallTimeout)
    inline constexpr auto kPoll = std::chrono::milliseconds(250);
    inline constexpr const char* kE3Save = "HoMBF_E3";

    /// kDataLoaded: the main menu sink and the polling thread, when any dev key is on
    void Install();

    /// kPreLoadGame / kPostLoadGame / kSaveGame (message handler)
    void OnPreLoadGame();
    void OnPostLoadGame();
    void OnSaveGame();

    /// E3 armed its marker and asked for the save and reload (SelfTestReload.cpp)
    void OnE3Asked();

    /// SelfTestLog::Done: a_label null for the main run, "E3" for E3's end
    void OnSelfTestDone(const char* a_label);
}
