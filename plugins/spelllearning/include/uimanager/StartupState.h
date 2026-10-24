#pragma once

#include <string_view>

// =============================================================================
// STARTUP STATE - the progression state filled at kDataLoaded, without the panel
// =============================================================================
//
// The gameplay settings (config.json) and the saved tree's prerequisites used to
// reach the plugin only through the panel (LoadUnifiedConfig, SetTreePrerequisites),
// sent once its page was ready. Game start now reads the same files itself, read
// only, and applies them with the same functions; the panel later sends the same
// again (docs/ARCHITECTURE.md, "Progression state at game start"). Game thread.

/// What reading config.json at game start found
struct StartupConfigResult
{
    /// config.json is there but not JSON / not an object: the panel will move it aside
    /// (and write a new one) before it reads the tree, so the tree waits for the panel
    bool configBroken = false;
    /// Reading or applying the settings threw: they are partly applied at most, so the
    /// tree waits for the panel's load too
    bool applyThrew = false;
};

/// config.json picked as the panel picks it (ConfigFile::Peek: never moved, written or
/// renamed), over the defaults, then ApplyGameplayConfig (UIManagerStartupConfig.cpp).
/// The hotkey and pauseGameOnFocus are left to the panel's load.
StartupConfigResult LoadGameplayConfigAtStartup();

/// spell_tree.json's prerequisites as the panel's fast path sends them
/// (TreePrereqEntries.h), and its requiredXP; the file is never written
/// (UIManagerTreeState.cpp). Nothing when config.json was broken or applying it threw.
void LoadTreeStateAtStartup(const StartupConfigResult& config);

// One SpellLearning.log line per session comparing the tree state game start
// applied with the panel's (UIManagerTreeParity.cpp): "TreeParity PASS (...)",
// "TreeParity FAIL (...)" with up to 5 spells that differ, or "TreeParity SKIP (reason)".
// All on the game thread (the message handler and game thread tasks): no lock.
namespace TreeParity
{
    /// Game start applied the tree: its state is the baseline
    void RecordBaseline();
    /// Game start did not apply the tree: SKIP with the reason, the session's one line
    void Skip(std::string_view reason);
    /// No comparison will come for the recorded baseline (no panel this session, or a game
    /// started or loaded before the panel's tree load): SKIP with the reason, once
    void Expire(std::string_view reason);
    /// OnLoadSpellTree, right after the saved tree went to the panel: the next data
    /// SetTreePrerequisites is compared (once), if it comes within the window
    void ArmForPanelLoad();
    /// OnSetTreePrerequisites after a data apply; `error` when the apply threw
    void AfterPanelApply(const char* error);
}
