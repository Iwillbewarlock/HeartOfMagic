#include "Common.h"
#include "FileUtils.h"
#include "LoadOrderFingerprint.h"
#include "PathText.h"
#include "ScanRecord.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "tomereach/TomeReach.h"
#include "uimanager/StartupState.h"
#include "uimanager/TreeGraftRun.h"
#include "uimanager/TreeRecord.h"
#include "uimanager/TreeSaveCheck.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

// =============================================================================
// TREE GRAFT AT THE MAIN MENU (StartupState.h; the rules: TreeGraftRun.h)
// =============================================================================
//
//   AutoScan's waiter thread  the files read, the conditions, the scan parsed
//   game thread (task 1)      a tagged copy of the tree validated: each node's verdict
//   waiter thread             the plan: ids refreshed, Graft, declutter, checks (pure,
//                             the slow part: a few hundred ms on a big tree)
//   game thread (task 2)      the files checked unchanged since they were read (the
//                             panel's saves run on this thread too), the tree written
//                             (.bak kept) and its record, the tree state applied, the
//                             panel sent the validated tree, the live-parent check
//
// Nothing is written when a condition fails, the plan's checks fail, or a step throws
// or times out before the write; the load order notice then applies as before.

namespace
{
    using Clock = std::chrono::steady_clock;
    using Outcome = TreeGraftOutcome;

    constexpr const char* kPrefix = "TreeGraft:";
    constexpr const char* kConfigFile = "Data/SKSE/Plugins/SpellLearning/config.json";
    constexpr const char* kCatalogFile = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    constexpr const char* kScanOutputFile = "Data/SKSE/Plugins/SpellLearning/spell_scan_output.json";
    // Validation of ~1,400 nodes takes tens of ms; the write a few hundred. A game thread
    // that does not come round in this long is stuck: the run gives up (a late write task
    // still checks the files and writes on its own)
    constexpr auto kGameThreadWait = std::chrono::seconds(60);

    std::mutex s_outcomeMutex;
    Outcome s_outcome;

    void SetOutcome(Outcome outcome)
    {
        std::lock_guard lock(s_outcomeMutex);
        s_outcome = std::move(outcome);
    }

    double MsFrom(Clock::time_point started)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    std::optional<std::string> ReadText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream text;
        text << file.rdbuf();
        if (file.bad()) return std::nullopt;
        return std::move(text).str();
    }

    /// There, but not opened (locked, no access): not the same as missing
    bool ThereButUnread(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec) || ec;
    }

    void NotRun(const std::string& why)
    {
        logger::info("{} not run - {}", kPrefix, why);
        SetOutcome({ Outcome::State::kNotRun, false, false, 0, 0, why });
    }

    void Failed(const std::string& why, bool wrote = false)
    {
        logger::error("{} {} - {}", kPrefix, why,
            wrote ? "see above for what was written" : "nothing written, the load order notice applies");
        SetOutcome({ Outcome::State::kFailed, wrote, false, 0, 0, why });
    }

    std::string Summary(const TreeGraftRun::Plan& plan)
    {
        return std::format("{} candidates: {} grafted, {} left out by the player's rules, {} with no school or parent, "
                           "{} deferred; {} stale formIds refreshed, {} held, {} persistentIds rewritten",
            plan.candidates, plan.grafted.size(), plan.leftOut.size(), plan.skippedNoSchool.size(), plan.deferred.size(),
            plan.refresh.nodesMoved, plan.refresh.held.size(), plan.refresh.persistentIdsWritten);
    }

    // The files as read on the waiter thread, for the write's "unchanged" check
    struct ReadState
    {
        std::string treeText;
        std::optional<std::string> recordText;
    };

    // Game thread: the plan written, applied and sent. Sets the outcome itself.
    bool Commit(const TreeGraftRun::Plan& plan, const ReadState& read)
    {
        const auto started = Clock::now();
        const auto treePath = UIManager::GetTreeFilePath();
        if (ReadText(treePath) != std::optional<std::string>(read.treeText) ||
            ReadText(UIManager::GetTreeLoadOrderFilePath()) != read.recordText) {
            logger::info("{} spell_tree.json or its record changed while the graft was planned (a save from the panel) - "
                         "nothing written, tried again at the next game start", kPrefix);
            SetOutcome({ Outcome::State::kDropped, false, false, 0, 0, "the tree changed while the graft was planned" });
            return false;
        }

        Outcome outcome{ Outcome::State::kNothingWritten, false, true, plan.grafted.size(), 0, Summary(plan) };
        std::string text;
        if (plan.writeTree) {
            // As the panel's save writes it: compact, in the file's own key order
            text = plan.tree.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
            if (!TreeSaveCheck::HasSpellNodes(text) || !FileUtils::WriteAtomically(treePath, text)) {
                Failed(std::format("could not write {} ({} bytes)", PathText::Utf8(treePath), text.size()));
                return false;
            }
            outcome.wrote = true;
            outcome.writtenHash = std::hash<std::string>{}(text);
            logger::info("{} spell_tree.json written with {} new spells ({} bytes, the old file kept as .bak)", kPrefix,
                plan.grafted.size(), text.size());
        }
        if (plan.writeRecord) {
            if (UIManager::WriteTreeRecord(plan.record)) {
                outcome.wrote = true;
                logger::info("{} spell_tree_loadorder.json: {} seen spells, {} to tell about, load order {}", kPrefix,
                    plan.record.seen.size(), plan.record.pendingGrafted,
                    plan.complete ? "now this one's" : "kept (spells deferred or unplaced: the graft runs again)");
            } else {
                // The next start judges the left-out spells again; the grafted ones are in the tree
                logger::warn("{} the record was not written - the next game start runs the graft again", kPrefix);
            }
        }
        if (!plan.writeTree) {
            outcome.state = outcome.wrote ? Outcome::State::kWritten : Outcome::State::kNothingWritten;
            SetOutcome(std::move(outcome));
            return true;
        }
        outcome.state = Outcome::State::kWritten;

        auto* ui = UIManager::GetSingleton();
        std::string_view stage = "validating the grafted tree threw";
        try {
            logger::info("{} validating the grafted tree for the game and the panel", kPrefix);
            const PanelTree panel = ValidateTreeAsPanelPath(text);
            if (const auto skipped = ApplyTreeState(panel, stage)) {
                logger::warn("{} the tree state was not applied ({}) - the panel's tree load applies it", kPrefix, *skipped);
            }
            // Game start's state is no longer the panel's tree load's: no comparison this session
            TreeParity::Expire("a tree graft changed the tree after game start applied it");
            stage = "the live-parent check threw";
            if (const auto problem = TreeGraftRun::GraftedHaveLiveParents(panel.validated, plan.tree, plan.preCounts);
                !problem.empty()) {
                outcome.passed = false;
                outcome.detail = "after the write: " + problem;
                logger::error("{} {}", kPrefix, outcome.detail);
            }
            stage = "sending the tree to the panel threw";
            if (ui && ui->IsInitialized()) ui->SendTreeData(panel.panelText);
        } catch (const std::exception& e) {
            outcome.passed = false;
            outcome.detail = std::format("{}: {}", stage, e.what());
            logger::error("{} {} (the tree is written; the panel's tree load applies it)", kPrefix, outcome.detail);
        }
        // A save loaded meanwhile (Continue pressed during the graft) hears of it now
        if (ui && ui->IsSaveLoaded()) ui->ShowPendingGraftNotice();
        logger::info("{} done - {} ({:.0f} ms on the game thread)", kPrefix, outcome.detail, MsFrom(started));
        SetOutcome(std::move(outcome));
        return true;
    }

    void Run(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog)
    {
        const auto started = Clock::now();
        const auto treePath = UIManager::GetTreeFilePath();
        const auto recordPath = UIManager::GetTreeLoadOrderFilePath();

        ReadState read;
        const auto treeText = ReadText(treePath);
        read.recordText = ReadText(recordPath);
        TreeRecordFile::TreeRecord record;
        bool recordUnreadable = !read.recordText && ThereButUnread(recordPath);
        if (read.recordText) {
            const auto data = nlohmann::json::parse(*read.recordText, nullptr, false);
            if (data.is_discarded() || !data.is_object()) recordUnreadable = true;
            else record = TreeRecordFile::ParseTreeRecord(data);
        }
        TreeGraftRun::Facts facts;
        facts.loadOrder = loadOrder.hash;
        facts.recordLoadOrder = record.loadOrder;
        facts.recordUnreadable = recordUnreadable;
        facts.treeHasSpells = treeText && TreeSaveCheck::HasSpellNodes(*treeText);
        facts.catalogMatches = ScanRecord::CatalogMatches(loadOrder);
        if (const auto why = TreeGraftRun::WhyNotRun(facts)) return NotRun(*why);
        read.treeText = *treeText;

        const auto baseline = TreeGraftRun::BaselineOf(record, previousCatalog);
        if (baseline.source == TreeGraftRun::SeenSource::kNone) {
            return NotRun("no seen list and no earlier catalog: this scan is the baseline, nothing grafted "
                          "(the next load order change grafts what it brings)");
        }

        // The player's rules: no file is the panel's defaults (none); a broken one is unknown
        nlohmann::json config = nlohmann::json::object();
        if (const auto configText = ReadText(kConfigFile)) {
            config = nlohmann::json::parse(*configText, nullptr, false);
            if (config.is_discarded() || !config.is_object()) return NotRun("config.json could not be read - the player's rules are unknown");
        } else if (ThereButUnread(kConfigFile)) {
            return NotRun("config.json could not be opened - the player's rules are unknown");
        }
        auto rules = TreeGraft::PlayerRulesOf(config);

        nlohmann::json spells;
        {
            const auto scanText = ReadText(kScanOutputFile);
            if (!scanText) return NotRun("spell_scan_output.json could not be read");
            auto output = nlohmann::json::parse(*scanText, nullptr, false);
            if (output.is_discarded()) return NotRun("spell_scan_output.json is not JSON");
            if (const auto why = TreeGraftRun::WhyScanUnusable(output, TomeReach::OutcomeKey())) return NotRun(*why);
            spells = std::move(output["spells"]);
        }
        auto tree = nlohmann::ordered_json::parse(read.treeText);
        logger::info("{} the tree record is load order {}, this is {} - grafting new spells (seen list: {}, {} spells; "
                     "files read in {:.0f} ms)", kPrefix, record.loadOrder.empty() ? "unknown" : record.loadOrder,
            loadOrder.hash, baseline.source == TreeGraftRun::SeenSource::kRecord ? "the record's" : "the previous catalog's",
            baseline.seen.size(), MsFrom(started));

        // Each node's verdict, from the game (task 1)
        std::size_t tagged = 0;
        auto copy = std::make_shared<nlohmann::json>(TreeGraftRun::TaggedCopy(tree, tagged));
        const auto verdicts = RunOnGameThreadAndWait("TreeGraftValidate", [copy, tagged]() {
            logger::info("{} validating a copy of the tree (the lines below repeat the tree load's)", kPrefix);
            SpellScanner::ValidateAndFixTree(*copy);
            return TreeGraftRun::VerdictsOf(*copy, tagged);
        }, kGameThreadWait);
        copy.reset();
        if (!verdicts) return Failed("the tree's validation on the game thread did not answer");

        TreeGraftRun::PlanInput input;
        input.tree = std::move(tree);
        input.scanSpells = std::move(spells);
        input.record = record;
        input.baseline = baseline;
        input.rules = std::move(rules);
        input.verdicts = *verdicts;
        input.loadOrder = loadOrder.hash;
        input.plugins = loadOrder.plugins;
        input.spells = loadOrder.spells;
        const auto planStarted = Clock::now();
        auto plan = std::make_shared<TreeGraftRun::Plan>(TreeGraftRun::PlanGraft(std::move(input), TreeBuilder::BuildConfig{}));
        logger::info("{} planned in {:.0f} ms - {}; {} scan spells only dropped nodes hold left out; declutter {}", kPrefix,
            MsFrom(planStarted), Summary(*plan), plan->scanLeft, plan->declutterSummary.dump());
        for (const auto& id : plan->refresh.held) {
            logger::info("{} {} keeps its stale formId: its formId now is another node's saved one", kPrefix, id);
        }
        if (!plan->problem.empty()) return Failed("the plan's check failed: " + plan->problem);
        if (!plan->writeTree && !plan->writeRecord) {
            logger::info("{} nothing to write", kPrefix);
            SetOutcome({ Outcome::State::kNothingWritten, false, true, 0, 0, Summary(*plan) });
            return;
        }

        auto state = std::make_shared<ReadState>(std::move(read));
        const auto written = RunOnGameThreadAndWait("TreeGraftWrite", [plan, state]() {
            try {
                return Commit(*plan, *state);
            } catch (const std::exception& e) {
                Failed(std::format("the write threw: {}", e.what()));
                return false;
            }
        }, kGameThreadWait);
        if (!written) {
            logger::warn("{} the game thread did not take the write within {} s - it writes when it runs", kPrefix,
                std::chrono::duration_cast<std::chrono::seconds>(kGameThreadWait).count());
        }
        logger::info("{} finished in {:.1f} s since it started", kPrefix, MsFrom(started) / 1000.0);
    }
}

void RunTreeGraftAtMainMenu(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog)
{
    try {
        Run(loadOrder, previousCatalog);
    } catch (const std::exception& e) {
        Failed(std::format("failed: {}", e.what()));
    } catch (...) {
        Failed("failed (unknown error)");
    }
}

std::optional<nlohmann::json> ReadCatalogForTreeGraft()
{
    try {
        if (const auto recordText = ReadText(UIManager::GetTreeLoadOrderFilePath())) {
            const auto data = nlohmann::json::parse(*recordText, nullptr, false);
            if (!data.is_discarded() && TreeRecordFile::ParseTreeRecord(data).hasSeen) return std::nullopt;
        }
        const auto text = ReadText(kCatalogFile);
        if (!text) return std::nullopt;
        auto catalog = nlohmann::json::parse(*text, nullptr, false);
        if (catalog.is_discarded() || !catalog.is_object()) return std::nullopt;
        return catalog;
    } catch (const std::exception& e) {
        logger::warn("{} the previous catalog could not be read ({}) - no seen list from it", kPrefix, e.what());
        return std::nullopt;
    }
}

TreeGraftOutcome LastTreeGraft()
{
    std::lock_guard lock(s_outcomeMutex);
    return s_outcome;
}
