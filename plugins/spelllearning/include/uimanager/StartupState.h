#pragma once

#include "LoadOrderFingerprint.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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

/// The saved tree as the panel's load gets it (OnLoadSpellTree): validated
/// (SpellScanner::ValidateAndFixTree), and the text the panel parses - the file's own
/// when validation changed nothing, else the validated tree's dump
struct PanelTree
{
    nlohmann::json validated;   // after ValidateAndFixTree, for the requiredXP sync
    std::string panelText;
    bool fromValidated = false;
};

/// Game thread. Throws on a tree that does not parse or that validation cannot read,
/// as the panel path's try does (UIManagerTreeState.cpp)
PanelTree ValidateTreeAsPanelPath(std::string text);

/// The tree's prerequisites into ProgressionManager as the panel's SetTreePrerequisites
/// applies them: the entries of panelText read in its own key order (the school order
/// decides which school's node of a two-school spell applies last), the old ones cleared
/// first. `skipped` says why nothing was applied (a UTF-8 BOM the panel's JSON.parse
/// rejects, entries that could not be built). Game thread; throws as the appliers do,
/// with `stage` naming the step then.
struct TreePrereqsApplied
{
    std::optional<std::string> skipped;
    int applied = 0;
    std::size_t entries = 0;
};
TreePrereqsApplied ApplyTreePrerequisitesOf(const PanelTree& tree, std::string_view& stage);

/// ApplyTreePrerequisitesOf, then the tree's requiredXP (SyncRequiredXPFromTree): the whole
/// tree state a tree load leaves. Why nothing was applied, or nothing. Game thread; throws
/// as ApplyTreePrerequisitesOf (game start runs the two parts itself, its parity baseline between)
std::optional<std::string> ApplyTreeState(const PanelTree& tree, std::string_view& stage);

// =============================================================================
// THE TREE GRAFT AT THE MAIN MENU (TreeGraftRun.cpp; the rules: TreeGraftRun.h)
// =============================================================================

/// New spells of the load order join the saved tree: called by the scan at game start
/// (AutoScan.cpp, its waiter thread) after its scan, or when the catalog already was the
/// load order's. Reads and plans on the calling thread; validation, the write, the tree
/// state and the panel in two game thread tasks it waits for. Logs "TreeGraft: ...";
/// writes nothing when it does not run or fails. Not on the game thread (it waits on it).
void RunTreeGraftAtMainMenu(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog);

/// spell_catalog.json parsed when a graft could take its seen list from it (the tree record
/// has none), read before the scan at game start replaces it; nothing otherwise. Any thread.
std::optional<nlohmann::json> ReadCatalogForTreeGraft();

/// What the session's last graft did (the dev self-tests read it). Any thread.
struct TreeGraftOutcome
{
    enum class State { kNotRun, kNothingWritten, kWritten, kDropped, kFailed };
    State state = State::kNotRun;
    bool wrote = false;            // spell_tree.json or its record was written
    bool passed = false;           // every check of the plan passed (TreeGraftRun.h step 4, live parents)
    std::size_t grafted = 0;
    std::size_t writtenHash = 0;   // std::hash of the tree text written (0: none)
    std::string detail;            // why it did not run, what it did, or what failed
};
TreeGraftOutcome LastTreeGraft();

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
