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

#include <atomic>
#include <chrono>
#include <cstdint>
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
//   AutoScan's waiter thread  before the scan replaces the catalog: a record without a seen
//                             list gets the catalog's spells as one (a game thread task)
//   AutoScan's waiter thread  first thing, whatever else runs: a journal an earlier graft left
//                             settled (ResolveTreeGraftJournal; its write: a task, only over
//                             the files as read)
//   AutoScan's waiter thread  the files read, the conditions, the scan dump checked against
//                             the scan record (ScanRecord::DumpMatches) and parsed
//   game thread (task 1)      a tagged copy of the tree validated: each node's verdict
//   waiter thread             the plan: ids refreshed, Graft, declutter, checks (pure,
//                             the slow part: a few hundred ms on a big tree)
//   waiter thread             the tree's text, its spell check, the journal and the hashes
//                             (PrepareWrite: the game thread only compares and writes)
//   game thread (task 2)      the files checked unchanged since they were read (the
//                             panel's saves run on this thread too); the record with the
//                             graft's journal, the tree (.bak kept), the record without it
//                             (TreeGraftRecord.h); onTreeGrafted to the panel when the tree
//                             is this load order's; the tree state applied, the panel sent
//                             the validated tree, the live-parent check
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
    std::atomic<std::uint64_t> s_treeWriteStamp{ 0 };   // TreeGraftWriteStamp

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
        SetOutcome({ Outcome::State::kNotRun, false, false, 0, 0, why, {} });
    }

    void Failed(const std::string& why, bool wrote = false)
    {
        logger::error("{} {} - {}", kPrefix, why,
            wrote ? "see above for what was written" : "nothing written, the load order notice applies");
        SetOutcome({ Outcome::State::kFailed, wrote, false, 0, 0, why, {} });
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
        TreeRecordFile::TreeRecord record;   // recordText parsed (empty: no file)
    };

    // The record as read, or none when it is there but unreadable
    std::optional<TreeRecordFile::TreeRecord> ParseRecord(const std::optional<std::string>& text)
    {
        if (!text) return TreeRecordFile::TreeRecord{};
        const auto data = nlohmann::json::parse(*text, nullptr, false);
        if (data.is_discarded() || !data.is_object()) return std::nullopt;
        return TreeRecordFile::ParseTreeRecord(data);
    }

    // The grafted spells' node formIds and persistentIds (after each school's nodes before the graft)
    void SetGraftedIds(const TreeGraftRun::Plan& plan, Outcome& outcome)
    {
        if (!plan.tree.contains("schools") || !plan.tree["schools"].is_object()) return;
        for (const auto& [name, school] : plan.tree["schools"].items()) {
            if (!school.is_object() || !school.contains("nodes") || !school["nodes"].is_array()) continue;
            const auto pre = plan.preCounts.find(name);
            const auto& nodes = school["nodes"];
            for (std::size_t i = pre != plan.preCounts.end() ? pre->second : nodes.size(); i < nodes.size(); ++i) {
                if (nodes[i].is_object() && nodes[i].contains("formId") && nodes[i]["formId"].is_string()) {
                    outcome.graftedFormIds.push_back(nodes[i]["formId"].get<std::string>());
                    outcome.graftedPersistentIds.push_back(TreeGraftRun::Detail::Text(nodes[i], "persistentId"));
                }
            }
        }
    }

    // Game thread: a tree this file wrote made the session's (Commit's graft, a journal's
    // recovery): validated as the panel's tree load validates it, its prerequisites and
    // requiredXP applied, game start's parity baseline expired. Throws as
    // ValidateTreeAsPanelPath and ApplyTreeState do, `stage` naming the step.
    PanelTree ApplyWrittenTree(std::string text, std::string_view& stage)
    {
        stage = "validating the written tree threw";
        logger::info("{} validating the written tree for the game and the panel", kPrefix);
        PanelTree panel = ValidateTreeAsPanelPath(std::move(text));
        if (const auto skipped = ApplyTreeState(panel, stage)) {
            logger::warn("{} the tree state was not applied ({}) - the panel's tree load applies it", kPrefix, *skipped);
        }
        // Game start's state is no longer the panel's tree load's: no comparison this session
        TreeParity::Expire("the tree graft wrote the tree after game start applied it");
        return panel;
    }

    // What the waiter thread puts together for Commit
    struct Written
    {
        std::string text;                     // the tree as the panel's save writes it (writeTree)
        std::size_t hash = 0;                 // std::hash of text
        TreeRecordFile::TreeRecord journal;   // the record as read with the graft's journal (writeTree, writeRecord)
        Outcome outcome;                      // the outcome a write leaves, but for what the write itself sets
    };

    // Waiter thread: the tree's text (compact, in the file's own key order, as the panel's
    // save writes it), its spell check, the journal (FNV of the text) and the outcome's ids,
    // so the game thread only compares and writes. The reason when the tree may not be written
    std::optional<std::string> PrepareWrite(const TreeGraftRun::Plan& plan, const ReadState& read, Written& written)
    {
        written.outcome = { Outcome::State::kNothingWritten, false, true, plan.grafted.size(), 0, Summary(plan), {} };
        if (!plan.writeTree) return std::nullopt;
        written.text = plan.tree.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
        if (!TreeSaveCheck::HasSpellNodes(written.text)) return std::string("the grafted tree has no spells (not written)");
        written.hash = std::hash<std::string>{}(written.text);
        if (plan.writeRecord) written.journal = TreeGraftRun::WithJournal(read.record, written.text, plan.record);
        SetGraftedIds(plan, written.outcome);
        written.outcome.priorLoadOrder = read.record.loadOrder;
        written.outcome.priorPlugins = read.record.plugins;
        written.outcome.priorSpells = read.record.spells;
        written.outcome.priorNoticedLoadOrder = read.record.noticedLoadOrder;
        return std::nullopt;
    }

    // Game thread: the plan written (PrepareWrite's text and journal, taken), applied and
    // sent. Sets the outcome itself.
    bool Commit(const TreeGraftRun::Plan& plan, const ReadState& read, Written& written)
    {
        const auto started = Clock::now();
        const auto treePath = UIManager::GetTreeFilePath();
        const auto treeNow = ReadText(treePath);
        if (!treeNow || *treeNow != read.treeText || ReadText(UIManager::GetTreeLoadOrderFilePath()) != read.recordText) {
            logger::info("{} spell_tree.json or its record changed while the graft was planned (a save from the panel) - "
                         "nothing written, tried again at the next game start", kPrefix);
            SetOutcome({ Outcome::State::kDropped, false, false, 0, 0, "the tree changed while the graft was planned", {} });
            return false;
        }

        Outcome outcome = std::move(written.outcome);
        if (plan.writeTree) {
            // The journal first (TreeGraftRecord.h): a record write lost after the tree's is redone at the next start
            if (plan.writeRecord && !UIManager::WriteTreeRecord(written.journal)) {
                Failed("could not write the graft's journal into spell_tree_loadorder.json");
                return false;
            }
            if (!FileUtils::WriteAtomically(treePath, written.text)) {
                // The journal's tree hash is not the file's: the next start drops it
                Failed(std::format("could not write {} ({} bytes)", PathText::Utf8(treePath), written.text.size()));
                return false;
            }
            ++s_treeWriteStamp;
            outcome.wrote = true;
            outcome.writtenHash = written.hash;
            logger::info("{} spell_tree.json written with {} new spells ({} bytes, the old file kept as .bak)", kPrefix,
                plan.grafted.size(), written.text.size());
        }
        bool recordWritten = false;
        if (plan.writeRecord) {
            if (UIManager::WriteTreeRecord(plan.record)) {
                outcome.wrote = recordWritten = true;
                logger::info("{} spell_tree_loadorder.json: {} seen spells, {} to tell about, load order {}", kPrefix,
                    plan.record.seen.size(), plan.record.pendingGrafted,
                    plan.complete ? "now this one's" : "kept (spells deferred or unplaced: the graft runs again)");
            } else {
                logger::warn("{} the record was not written - {}", kPrefix,
                    plan.writeTree ? "the next game start writes it from the journal" : "the next game start runs the graft again");
            }
        }
        auto* ui = UIManager::GetSingleton();
        // The panel decided its "load order changed" line before the graft: it goes when the tree is this load order's
        if (plan.complete && recordWritten && ui && ui->IsInitialized()) {
            ui->NotifyTreeGrafted(plan.record.loadOrder, false);   // the tree itself follows (SendTreeData)
        }
        if (!plan.writeTree) {
            outcome.state = outcome.wrote ? Outcome::State::kWritten : Outcome::State::kNothingWritten;
            SetOutcome(std::move(outcome));
            return true;
        }
        outcome.state = Outcome::State::kWritten;

        std::string_view stage;
        try {
            const PanelTree panel = ApplyWrittenTree(std::move(written.text), stage);
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

    void Run(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog,
        TreeGraftKnown known)
    {
        const auto started = Clock::now();
        const auto treePath = UIManager::GetTreeFilePath();
        const auto recordPath = UIManager::GetTreeLoadOrderFilePath();

        ReadState read;
        read.recordText = ReadText(recordPath);
        auto parsed = ParseRecord(read.recordText);
        bool recordUnreadable = !parsed || (!read.recordText && ThereButUnread(recordPath));
        TreeRecordFile::TreeRecord record = parsed ? std::move(*parsed) : TreeRecordFile::TreeRecord{};

        // ResolveTreeGraftJournal ran before (AutoScan): a journal still there waits for the next start
        if (!recordUnreadable && record.extra.contains(TreeGraftRun::kJournalKey)) {
            return NotRun("an earlier graft's journal is still in spell_tree_loadorder.json (settled at the next start)");
        }
        TreeGraftRun::Facts facts;
        facts.loadOrder = loadOrder.hash;
        facts.recordLoadOrder = record.loadOrder;
        facts.recordUnreadable = recordUnreadable;
        // The cheap conditions first (WhyNotRun asks them before the tree and the catalog): the
        // tree is read and the catalog hashed only when they can still decide
        facts.treeHasSpells = facts.catalogMatches = true;
        if (const auto why = TreeGraftRun::WhyNotRun(facts)) return NotRun(*why);
        auto treeText = known.treeText ? std::move(known.treeText) : ReadText(treePath);
        facts.treeHasSpells = treeText && TreeSaveCheck::HasSpellNodes(*treeText);
        facts.catalogMatches = known.catalogMatches || ScanRecord::CatalogMatches(loadOrder);
        if (const auto why = TreeGraftRun::WhyNotRun(facts)) return NotRun(*why);
        read.treeText = std::move(*treeText);
        read.record = record;

        auto baseline = TreeGraftRun::BaselineOf(record, previousCatalog);
        if (baseline.source == TreeGraftRun::SeenSource::kNone) {
            return NotRun("no seen list and no earlier catalog: nothing grafted and no baseline saved (the next load "
                          "order change seeds the seen list from the catalog this load order's scan left)");
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
            // The skip path's dump has been matched already (TreeGraftNeedsTheScan): not read and hashed again
            const bool matched = known.matchedDump.has_value();
            const auto scanText = matched ? std::move(known.matchedDump) : ReadText(kScanOutputFile);
            if (!scanText) return NotRun("spell_scan_output.json could not be read");
            if (!matched && !ScanRecord::DumpMatches(*scanText)) {
                return NotRun("spell_scan_output.json is not the dump of the scan the catalog record is for (a panel scan "
                              "since, or a record from before the dump was recorded) - the load order notice applies");
            }
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
        SetOutcome({ Outcome::State::kFailed, false, false, 0, 0, "the validation task did not answer", {} });
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
        input.record = std::move(record);
        input.baseline = std::move(baseline);
        input.rules = std::move(rules);
        input.verdicts = std::move(*verdicts);
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
            SetOutcome({ Outcome::State::kNothingWritten, false, true, 0, 0, Summary(*plan), {} });
            return;
        }

        auto state = std::make_shared<ReadState>(std::move(read));
        auto prepared = std::make_shared<Written>();
        if (const auto refused = PrepareWrite(*plan, *state, *prepared)) return Failed(*refused);
        SetOutcome({ Outcome::State::kFailed, false, false, 0, 0,
            std::format("the write task was not taken within {} s (it writes when it runs)",
                std::chrono::duration_cast<std::chrono::seconds>(kGameThreadWait).count()), {} });
        const auto written = RunOnGameThreadAndWait("TreeGraftWrite", [plan, state, prepared]() {
            try {
                return Commit(*plan, *state, *prepared);
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

void RunTreeGraftAtMainMenu(const LoadOrderFingerprint::Value& loadOrder, const std::optional<nlohmann::json>& previousCatalog,
    TreeGraftKnown known)
{
    try {
        Run(loadOrder, previousCatalog, std::move(known));
    } catch (const std::exception& e) {
        Failed(std::format("failed: {}", e.what()));
    } catch (...) {
        Failed("failed (unknown error)");
    }
}

CatalogForTreeGraft ReadCatalogForTreeGraft(const LoadOrderFingerprint::Value& loadOrder)
{
    try {
        const auto recordPath = UIManager::GetTreeLoadOrderFilePath();
        const auto recordText = ReadText(recordPath);
        const auto record = ParseRecord(recordText);
        // The catalog about to be replaced must be the previous load order's (the scan record says whose)
        if (!record || (!recordText && ThereButUnread(recordPath)) || loadOrder.hash.empty() ||
            !TreeGraftRun::CatalogNeeded(*record, loadOrder.hash, ScanRecord::RecordedLoadOrder())) {
            return {};
        }
        const auto text = ReadText(kCatalogFile);
        if (!text) return {};
        auto catalog = nlohmann::json::parse(*text, nullptr, false);
        if (catalog.is_discarded() || !catalog.is_object()) return {};
        const auto seeded = TreeGraftRun::SeededRecord(*record, catalog);
        if (!seeded) return {};
        // Kept in the record, so a graft dropped this start (the panel saved meanwhile) still has it next time
        const auto wrote = RunOnGameThreadAndWait("TreeGraftSeenList", [target = *seeded, recordText, recordPath]() {
            if (ReadText(recordPath) != recordText) return false;   // written meanwhile (a build's Apply)
            return UIManager::WriteTreeRecord(target);
        }, kGameThreadWait);
        if (wrote && *wrote) {
            logger::info("{} the tree record had no seen list: the {} spells of the catalog the scan replaces are kept "
                         "as it", kPrefix, seeded->seen.size());
            return { std::nullopt, true };
        }
        logger::warn("{} the seen list could not be kept in the record - this start's graft takes it from memory", kPrefix);
        return { std::move(catalog), false };
    } catch (const std::exception& e) {
        logger::warn("{} the previous catalog could not be read ({}) - no seen list from it", kPrefix, e.what());
        return {};
    }
}

bool TreeGraftNeedsTheScan(const LoadOrderFingerprint::Value& loadOrder, std::optional<std::string>* matchedDump)
{
    try {
        const auto recordPath = UIManager::GetTreeLoadOrderFilePath();
        const auto recordText = ReadText(recordPath);
        const auto record = ParseRecord(recordText);
        TreeGraftRun::Facts facts;
        facts.loadOrder = loadOrder.hash;
        facts.recordLoadOrder = record ? record->loadOrder : std::string();
        facts.recordUnreadable = !record || (!recordText && ThereButUnread(recordPath));
        facts.treeHasSpells = true;    // AutoScan's Precondition
        facts.catalogMatches = true;   // its skip path
        facts.recordHasSeen = record && record->hasSeen;
        const bool scan = TreeGraftRun::ScanForGraft(facts, [matchedDump] {
            auto dump = ReadText(kScanOutputFile);
            const bool matches = dump && ScanRecord::DumpMatches(*dump);
            if (matches && matchedDump) *matchedDump = std::move(dump);   // the graft right after reads it from here
            return matches;
        });
        if (scan) {
            logger::info("{} a graft is due (the tree record is load order {}, this is {}) but spell_scan_output.json "
                         "is not the catalog scan's dump - the scan at game start runs to write one", kPrefix,
                facts.recordLoadOrder.empty() ? "unknown" : facts.recordLoadOrder, loadOrder.hash);
        }
        return scan;
    } catch (const std::exception& e) {
        logger::warn("{} could not tell whether the graft needs the scan ({}) - the scan is skipped as before", kPrefix, e.what());
        return false;
    }
}

void ResolveTreeGraftJournal(const LoadOrderFingerprint::Value& loadOrder)
{
    try {
        const auto treePath = UIManager::GetTreeFilePath();
        const auto recordPath = UIManager::GetTreeLoadOrderFilePath();
        const auto recordText = ReadText(recordPath);
        const auto record = ParseRecord(recordText);
        if (!recordText || !record) return;
        const auto treeText = ReadText(treePath);
        const bool treeUnreadable = !treeText && ThereButUnread(treePath);
        // No tree file: its .tmp may be the tree the journal was for (Ruling 21)
        const auto tempPath = FileUtils::TempPath(treePath);
        const auto tempText = !treeText && !treeUnreadable ? ReadText(tempPath) : std::nullopt;
        const auto journal = TreeGraftRun::ResolveJournal(*record, treeText, treeUnreadable, tempText);
        if (journal.action == TreeGraftRun::JournalAction::kNone) return;
        if (journal.action == TreeGraftRun::JournalAction::kWait) {
            logger::info("{} spell_tree.json is there but could not be read - the journal an earlier graft left waits "
                         "for the next start", kPrefix);
            return;
        }
        const bool recover = journal.action == TreeGraftRun::JournalAction::kRecover;
        const bool apply = journal.action == TreeGraftRun::JournalAction::kApply || recover;
        enum class Result { kWritten, kChanged, kFailed };
        const auto result = RunOnGameThreadAndWait("TreeGraftJournal",
            [action = journal.action, target = journal.record, recordText, treeText, tempText, recordPath, treePath,
                tempPath, recover, hash = loadOrder.hash]() {
                // Only over the files as read: a panel save or import meanwhile wins, and a tree
                // that cannot be read now says nothing either
                const auto treeNow = ReadText(treePath);
                if (treeNow ? recover : ThereButUnread(treePath)) return Result::kChanged;   // a tree came (or is locked)
                // A recovery compares the .tmp as read with the .tmp now, in the tree's place
                const auto& fileRead = recover ? tempText : treeText;
                const auto fileNow = recover ? ReadText(tempPath) : treeNow;
                if (!TreeGraftRun::JournalStillFits(action, recordText, ReadText(recordPath), fileRead, fileNow)) {
                    return Result::kChanged;
                }
                if (recover) {
                    std::error_code ec;
                    std::filesystem::rename(tempPath, treePath, ec);
                    if (ec) {
                        logger::warn("{} spell_tree.json.tmp could not be moved into place: {}", kPrefix, ec.message());
                        return Result::kFailed;
                    }
                    ++s_treeWriteStamp;
                    logger::info("{} spell_tree.json was missing and spell_tree.json.tmp is the tree the journal was for "
                                 "- moved into place", kPrefix);
                }
                const bool recordWritten = UIManager::WriteTreeRecord(target);
                const bool thisLoadOrder = recordWritten && action != TreeGraftRun::JournalAction::kDrop && !hash.empty() &&
                                           target.loadOrder == hash;
                auto* ui = UIManager::GetSingleton();
                if (recover) {
                    // Game start found no tree: the moved one is applied as a graft's is (Commit)
                    std::string_view stage;
                    try {
                        ApplyWrittenTree(*fileNow, stage);
                    } catch (const std::exception& e) {
                        logger::warn("{} {}: {} - the tree is in place, the panel's tree load applies it", kPrefix, stage,
                            e.what());
                    }
                    // The panel heard "missing" and may be building a tree over it: it stops and loads this one
                    if (ui && ui->IsInitialized()) ui->NotifyTreeGrafted(thisLoadOrder ? target.loadOrder : std::string(), true);
                } else if (thisLoadOrder && ui && ui->IsInitialized()) {
                    // A graft finished from its journal: the panel's notice goes as after any complete graft
                    ui->NotifyTreeGrafted(target.loadOrder, false);
                }
                return recordWritten ? Result::kWritten : Result::kFailed;
            }, kGameThreadWait);
        if (result && *result == Result::kWritten) {
            logger::info("{} {} the journal an earlier graft left ({})", kPrefix, apply ? "applied" : "dropped",
                recover  ? "its tree moved from spell_tree.json.tmp: its record written"
                : apply  ? "the tree is the one it wrote: its record written"
                         : "the tree is not the one it was for");
        } else if (result && *result == Result::kChanged) {
            logger::info("{} the record or the tree changed since the journal was read, or the tree cannot be read - "
                         "it waits for the next start", kPrefix);
        } else if (result) {
            logger::warn("{} the journal's {} could not be written - it waits for the next start", kPrefix,
                recover ? "tree or record" : "record");
        } else {
            logger::warn("{} the game thread gave no result within {} s (timeout or error logged above) - the journal "
                         "waits for the next start", kPrefix,
                std::chrono::duration_cast<std::chrono::seconds>(kGameThreadWait).count());
        }
    } catch (const std::exception& e) {
        logger::warn("{} the journal could not be settled: {}", kPrefix, e.what());
    }
}

TreeGraftOutcome LastTreeGraft()
{
    std::lock_guard lock(s_outcomeMutex);
    return s_outcome;
}

std::uint64_t TreeGraftWriteStamp()
{
    return s_treeWriteStamp.load();
}
