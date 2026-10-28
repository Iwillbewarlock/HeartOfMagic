#include "Common.h"
#include "JsonText.h"
#include "PathText.h"
#include "FileUtils.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "uimanager/StartupState.h"
#include "uimanager/TreeGraftRecord.h"
#include "uimanager/TreeRecord.h"
#include "SpellScanner.h"
#include "EncodingUtils.h"
#include "ProgressionManager.h"
#include "treebuilder/TreeBuilder.h"
#include "ThreadUtils.h"

// =============================================================================
// TREE TAB CALLBACKS
// =============================================================================

void UIManager::OnLoadSpellTree([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: LoadSpellTree callback triggered");

    AddTaskToGameThread("LoadSpellTree", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        auto treePath = GetTreeFilePath();

        // Every way out tells the panel what the game start found (AutoBuild);
        // a file that is there but fails to read stays Unreadable
        auto state = StartupTreeState::Unreadable;
        std::size_t nodeCount = 0;
        struct ReportOnExit
        {
            UIManager* ui;
            const StartupTreeState& state;
            const std::size_t& nodes;
            ~ReportOnExit() { ui->SendStartupTreeInfo(state, nodes); }
        } reportOnExit{ instance, state, nodeCount };

        // Check if saved tree exists
        if (!std::filesystem::exists(treePath)) {
            logger::info("UIManager: No saved spell tree found");
            instance->UpdateTreeStatus("No saved tree - import one");
            state = StartupTreeState::Missing;
            return;
        }

        try {
            auto treeFile = ReadTreeFileText(treePath);
            if (treeFile.outcome == TreeFileRead::Ok) {
                std::string treeContent = std::move(treeFile.text);
                logger::info("UIManager: Loaded spell tree from file ({} bytes)", treeContent.size());
                if (IsBlankTreeText(treeContent)) {
                    logger::warn("UIManager: spell_tree.json is empty");
                    instance->UpdateTreeStatus("No saved tree - import one");
                    state = StartupTreeState::Empty;
                    return;
                }

                // Parse and validate tree - this resolves persistentId to current formId
                // when load order has changed since tree was generated
                try {
                    json treeData = json::parse(treeContent);
                    nodeCount = CountTreeNodes(treeData);
                    state = nodeCount > 0 ? StartupTreeState::Present : StartupTreeState::Empty;

                    // Validate and fix form IDs using persistent IDs. The panel gets the fixed
                    // tree when that moved or dropped a spell: new ids, and without the dropped
                    // nodes and the links to them (the file on disk keeps the old one)
                    if (ValidateTreeForPanel(treeData)) {
                        treeContent = JsonText::Dump(treeData);
                    }

                    // Send validated tree data to viewer. When it went, the panel's next
                    // SetTreePrerequisites is this tree's: game start's state is compared
                    // with it then (TreeParity)
                    if (instance->SendTreeData(treeContent)) {
                        TreeParity::ArmForPanelLoad();
                    }

                    // Sync requiredXP to ProgressionManager
                    SyncRequiredXPFromTree(treeData);

                    // No spell info is pushed from here. The panel asks for it
                    // (GetSpellInfoBatch) the moment it has the tree, on both of
                    // its load paths; pushing it as well looked up all 1428
                    // spells and sent 1.6 MB twice on every game start.
                } catch (const std::exception& e) {
                    // Do NOT pass the bytes on. They failed to parse here and
                    // they will fail to parse in the panel too, where the only
                    // sign of it is a tree that never appears. Say so instead,
                    // and leave the file alone so it can be recovered by hand.
                    logger::error("UIManager: spell_tree.json could not be read: {}", e.what());
                    logger::error("UIManager: the file is at {} - it has not been touched",
                        PathText::Utf8(treePath));
                    instance->UpdateTreeStatus("Saved tree is damaged - see SpellLearning.log");
                }

            } else {
                logger::warn("UIManager: Could not open spell tree file");
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: Exception while loading spell tree: {}", e.what());
        }
    });
}

void UIManager::OnGetSpellInfo(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: GetSpellInfo - no formId provided");
        return;
    }

    logger::info("UIManager: GetSpellInfo for formId: {}", argument);

    std::string argStr(argument);

    AddTaskToGameThread("GetSpellInfo", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Get spell info from SpellScanner
        std::string spellInfo = SpellScanner::GetSpellInfoByFormId(argStr);

        if (!spellInfo.empty()) {
            instance->SendSpellInfo(spellInfo);
        } else {
            logger::warn("UIManager: No spell found for formId: {}", argStr);
        }
    });
}

// The spell card asks for one icon at a time, by the key GetSpellInfo handed it.
// Replies with { key, svg }; svg is empty when the file is gone or unreadable.
void UIManager::OnGetSpellIcon(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        return;
    }

    std::string key(argument);

    AddTaskToGameThread("GetSpellIcon", [key]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI || !instance->m_prismaUI->IsValid(instance->m_view)) return;

        nlohmann::json reply;
        reply["key"] = key;
        reply["svg"] = EncodingUtils::SanitizeToUTF8(SpellScanner::ReadSpellIconSvg(key));
        instance->CallView("updateSpellIcon", JsonText::Dump(reply).c_str());
    });
}

void UIManager::OnGetSpellInfoBatch(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: GetSpellInfoBatch - no data provided");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("GetSpellInfoBatch", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            const auto started = std::chrono::steady_clock::now();
            // Parse JSON array of formIds
            json formIdArray = json::parse(argStr);

            if (!formIdArray.is_array()) {
                logger::error("UIManager: GetSpellInfoBatch - expected JSON array");
                return;
            }

            logger::info("UIManager: GetSpellInfoBatch for {} formIds", formIdArray.size());

            json resultArray = json::array();
            int foundCount = 0;
            int notFoundCount = 0;

            for (const auto& formIdJson : formIdArray) {
                if (!formIdJson.is_string()) {
                    logger::warn("UIManager: Skipping non-string formId in batch request");
                    continue;
                }
                std::string formIdStr = formIdJson.get<std::string>();

                // Built as JSON (a text that is not a formId comes back null: not found) and kept that way - no text round trip per spell
                json spellInfo = SpellScanner::GetSpellInfoJsonByFormId(formIdStr);

                if (!spellInfo.is_null()) {
                    resultArray.push_back(std::move(spellInfo));
                    foundCount++;
                } else {
                    json notFound;
                    notFound["formId"] = formIdStr;
                    notFound["notFound"] = true;
                    resultArray.push_back(notFound);
                    notFoundCount++;
                }
            }

            logger::info("UIManager: Batch result - {} found, {} not found", foundCount, notFoundCount);

            // Send batch result
            instance->SendSpellInfoBatch(JsonText::Dump(resultArray));
            logger::info("UIManager: GetSpellInfoBatch done in {} ms (game thread)", MsSince(started));

        } catch (const std::exception& e) {
            logger::error("UIManager: GetSpellInfoBatch exception: {}", e.what());
        }
    });
}

namespace
{
    // A build's payload: the tree's text without its seen list, and the list
    // (TreeRecord.h, spell_tree_loadorder.json)
    struct BuiltTreePayload
    {
        std::string treeText;
        std::vector<std::string> seen;
    };

    // Parsed here, in the listener, not in the game thread task (the tree is 0.5-1 MB:
    // the task only scans its text, TreeSaveCheck.h). ordered_json keeps the panel's key
    // order, which the school order depends on (TreePrereqEntries.h). A payload that does
    // not parse is saved as it came, with no seen list.
    BuiltTreePayload SplitBuiltTreePayload(const std::string& text)
    {
        const auto started = std::chrono::steady_clock::now();
        try {
            auto tree = nlohmann::ordered_json::parse(text, nullptr, false);
            if (tree.is_discarded() || !tree.is_object()) {
                logger::warn("UIManager: SaveSpellTree - a build's tree that does not parse: saved as sent, no seen list recorded");
                return { text, {} };
            }
            auto seen = TreeRecordFile::TakeSeenSpells(tree);
            // Not JsonText::Dump, which takes nlohmann::json (sorted keys); the same error handler
            auto stripped = tree.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
            logger::info("UIManager: SaveSpellTree - {} spells seen by the build's scan taken off the tree ({} ms)",
                seen.size(), MsSince(started));
            return { std::move(stripped), std::move(seen) };
        } catch (const std::exception& e) {
            logger::error("UIManager: SaveSpellTree - the build's seen list could not be taken off ({}): saved as sent", e.what());
            return { text, {} };
        }
    }
}

namespace
{
    // A save over the tree the session's graft wrote, without some of the spells it added.
    // The player's save still goes through. Stale (TreeGraftRun::StaleGraftSave: asked for
    // before the graft's write, `stampAtEntry`, and none of them in it) - the panel held an
    // older copy (its updateTreeData did not arrive or was replaced) - the lost spells'
    // persistentIds come back for the record (an edit save: ForgetLostGraftedSpells, Ruling
    // 23); else the player deleted them, and they stay seen (Ruling 24). Game thread.
    struct LostGraft
    {
        std::vector<std::string> persistentIds;   // a stale save's lost spells (none otherwise)
        TreeGraftOutcome graft;
    };
    LostGraft GraftedSpellsDropped(const std::string& text, std::uint64_t stampAtEntry)
    {
        LostGraft lost{ {}, LastTreeGraft() };
        const auto& graft = lost.graft;
        if (graft.state != TreeGraftOutcome::State::kWritten || graft.writtenHash == 0 || graft.graftedFormIds.empty()) {
            return lost;
        }
        const auto current = ReadTreeFileText(UIManager::GetTreeFilePath());
        if (current.outcome != TreeFileRead::Ok || std::hash<std::string>{}(current.text) != graft.writtenHash) return lost;
        std::size_t missing = 0;
        std::string first;
        std::vector<std::string> persistentIds;
        for (std::size_t i = 0; i < graft.graftedFormIds.size(); ++i) {
            const auto& id = graft.graftedFormIds[i];
            if (text.find("\"" + id + "\"") != std::string::npos) continue;
            if (missing++ == 0) first = id;
            if (i < graft.graftedPersistentIds.size() && !graft.graftedPersistentIds[i].empty()) {
                persistentIds.push_back(graft.graftedPersistentIds[i]);
            }
        }
        if (missing == 0) return lost;
        const bool stale = TreeGraftRun::StaleGraftSave(stampAtEntry, TreeGraftWriteStamp(), graft.graftedFormIds.size() - missing);
        logger::warn("UIManager: SaveSpellTree - this save drops {} of the {} spells the tree graft added at the main "
                     "menu (first {}): {}", missing, graft.graftedFormIds.size(), first,
            stale ? "asked for before the graft wrote the tree, none of them in it - the panel's tree was older than the file"
                  : "asked for after the graft wrote the tree, or it keeps the others - the player's edit, they stay seen");
        if (stale) lost.persistentIds = std::move(persistentIds);
        return lost;
    }
}

void UIManager::SaveSpellTreeFromPanel(const char* argument, TreeSaveKind kind)
{
    logger::info("UIManager: SaveSpellTree callback triggered{}",
        kind == TreeSaveKind::Built ? " (a build's Apply)" : kind == TreeSaveKind::Imported ? " (an Import)" : "");

    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SaveSpellTree - no content to save");
        return;
    }

    // Read first: a graft that writes the tree after this is newer than the panel's copy (StaleGraftSave)
    const auto graftStamp = TreeGraftWriteStamp();
    BuiltTreePayload payload = kind == TreeSaveKind::Built ? SplitBuiltTreePayload(argument)
                                                           : BuiltTreePayload{ std::string(argument), {} };

    AddTaskToGameThread("SaveSpellTree", [argStr = std::move(payload.treeText), seen = std::move(payload.seen), kind,
                                             graftStamp]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // What the save does to spell_tree_loadorder.json besides the tree file
        const auto updateRecord = [&]() {
            if (kind == TreeSaveKind::Built) instance->RecordTreeLoadOrder(seen);
            else if (kind == TreeSaveKind::Imported) instance->ForgetSeenSpells();
        };

        // Create output directory
        std::filesystem::path outputDir = "Data/SKSE/Plugins/SpellLearning";
        std::filesystem::create_directories(outputDir);

        // Write to file
        auto treePath = GetTreeFilePath();

        // The same tree again - applying twice, or a builder that saves on
        // every step - is not written again. Rewriting it would also rotate
        // the .bak, replacing the real previous tree with a copy of this one.
        // A build's text is hashed without its seen list (the record still gets that).
        // Not when the tree graft wrote the file since that save (SameAsLastSave).
        static std::size_t s_lastSavedHash = 0;
        static std::uint64_t s_lastSavedGraftStamp = 0;
        const std::size_t hash = std::hash<std::string>{}(argStr);
        std::error_code existsError;
        if (TreeGraftRun::SameAsLastSave(hash, s_lastSavedHash, s_lastSavedGraftStamp, TreeGraftWriteStamp()) &&
            std::filesystem::exists(treePath, existsError)) {
            logger::info("UIManager: Spell tree unchanged since the last save - not rewritten");
            updateRecord();
            instance->UpdateTreeStatus("Tree saved");
            return;
        }

        // Never a tree without spells over the player's file (the panel's SpellTreeSave refuses one too)
        const auto checkMs = CheckTreeHasSpells(argStr);
        if (!checkMs) {
            instance->UpdateTreeStatus("Save failed");
            return;
        }
        // Only an edit save gives lost grafted spells back (a build or Import writes its own record)
        const auto lost = kind == TreeSaveKind::Edit ? GraftedSpellsDropped(argStr, graftStamp) : LostGraft{};

        try {
            // Through a temp file and a move, keeping one .bak: this is the
            // player's whole generated tree, and truncating the real file meant
            // a crash partway through lost it with nothing to fall back on.
            const auto writeStarted = std::chrono::steady_clock::now();
            if (FileUtils::WriteAtomically(treePath, argStr)) {
                s_lastSavedHash = hash;
                s_lastSavedGraftStamp = TreeGraftWriteStamp();
                logger::info("UIManager: Saved spell tree to {} (spell check {} ms, written in {} ms)", PathText::Utf8(treePath),
                    *checkMs, MsSince(writeStarted));
                updateRecord();
                // A stale edit save gives the lost grafted spells back
                if (!lost.persistentIds.empty()) {
                    instance->ForgetLostGraftedSpells(lost.persistentIds, { lost.graft.priorLoadOrder,
                        lost.graft.priorPlugins, lost.graft.priorSpells, lost.graft.priorNoticedLoadOrder });
                }
                instance->UpdateTreeStatus("Tree saved");
            } else {
                instance->UpdateTreeStatus("Save failed");
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: Exception while saving spell tree: {}", e.what());
            instance->UpdateTreeStatus("Save failed");
        }
    });
}

// =============================================================================
// PROCEDURAL TREE GENERATION (C++ native)
// =============================================================================

// The spells named by `ids`, in that order, out of a full scan's JSON text.
// Runs on the build thread. An id the scan does not have is skipped and
// counted, never guessed at.
static std::vector<json> PickSpellsFromScan(const std::string& scanText, const std::vector<std::string>& ids)
{
    json scan = json::parse(scanText);
    auto& all = scan.at("spells");

    std::unordered_map<std::string, std::size_t> byId;
    byId.reserve(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& s = all[i];
        if (s.contains("formId") && s["formId"].is_string()) {
            byId.emplace(s["formId"].get<std::string>(), i);
        }
    }

    std::vector<json> picked;
    picked.reserve(ids.size());
    std::size_t missing = 0;
    for (const auto& id : ids) {
        auto it = byId.find(id);
        if (it == byId.end()) { ++missing; continue; }
        picked.push_back(std::move(all[it->second]));
    }
    if (missing > 0) {
        logger::warn("UIManager: {} of {} requested spells are not in the held scan", missing, ids.size());
    }
    return picked;
}

void UIManager::OnProceduralTreeGenerate(const char* argument)
{
    logger::info("UIManager: ProceduralTreeGenerate callback triggered (C++ native)");

    // Copy argument — must defer via AddTask to avoid re-entrant JS calls.
    // InteropCall back into JS from within a RegisterJSListener callback
    // doesn't work in Ultralight (re-entrant), so we defer to SKSE task thread.
    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("ProceduralTreeGenerate", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Guard against concurrent tree builds
        bool expected = false;
        if (!instance->m_treeBuildInProgress.compare_exchange_strong(expected, true)) {
            logger::warn("UIManager: Tree build already in progress, ignoring request");
            nlohmann::json response;
            response["success"] = false;
            // busy: this request was turned away, the build in flight goes on - the
            // panel must not take this answer as that build's result
            response["busy"] = true;
            response["error"] = "Tree build already in progress. Please wait for the current build to finish.";
            instance->CallView("onProceduralTreeComplete", JsonText::Dump(response).c_str());
            return;
        }

        try {
            nlohmann::json request = nlohmann::json::parse(argStr);

            std::string command = "build_tree_classic";  // the one builder
            if (request.contains("command") && request["command"].is_string()) {
                command = request["command"].get<std::string>();
            }

            auto configJson = request.value("config", nlohmann::json::object());

            // Either the spells themselves, or - the usual case - their ids in
            // the scan this plugin already holds (see m_scanText). The ids are
            // a few KB where the spells were 9-20 MB, which the panel had to
            // stringify and this thread had to parse and copy while the game
            // waited.
            std::vector<json> spells;
            std::vector<std::string> spellIds;
            std::shared_ptr<const std::string> scanText;
            if (request.contains("spellIds") && request["spellIds"].is_array()) {
                const auto scanId = request.value("scanId", std::uint32_t{0});
                if (!instance->m_scanText || scanId != instance->m_scanId) {
                    logger::warn("UIManager: build asked for scan #{} but the held scan is #{}", scanId, instance->m_scanId);
                    instance->m_treeBuildInProgress = false;
                    nlohmann::json response;
                    response["success"] = false;
                    response["error"] = "The spell scan changed while this build was being set up. Build again.";
                    instance->CallView("onProceduralTreeComplete", JsonText::Dump(response).c_str());
                    return;
                }
                scanText = instance->m_scanText;
                spellIds.reserve(request["spellIds"].size());
                for (const auto& id : request["spellIds"]) {
                    if (id.is_string()) spellIds.push_back(id.get<std::string>());
                }
            } else {
                auto spellsJson = request.value("spells", nlohmann::json::array());
                spells.reserve(spellsJson.size());
                for (auto& s : spellsJson) {
                    spells.push_back(std::move(s));
                }
            }

            logger::info("UIManager: Dispatching tree build to background thread ({} command, {} spells{})",
                command, scanText ? spellIds.size() : spells.size(), scanText ? ", from the held scan" : "");

            // Launch background thread — TreeBuilder has ZERO RE:: dependencies
            std::thread([command, spells = std::move(spells), spellIds = std::move(spellIds), scanText, configJson]() mutable {
                try {
                    if (scanText) {
                        spells = PickSpellsFromScan(*scanText, spellIds);
                    }
                    auto result = TreeBuilder::Build(command, spells, configJson);

                    // The reply is put together here on the build thread: the tree moves
                    // into it and is written out once, and the game thread only sends it.
                    // The tree goes in as an object, not as a string of JSON. As a string
                    // it was written out three times (once here, once more for the size
                    // in the log, once escaped inside the reply) and the panel had to
                    // parse it twice.
                    const auto dumpStarted = std::chrono::steady_clock::now();
                    nlohmann::json response;
                    if (result.success) {
                        response["success"] = true;
                        response["treeData"] = std::move(result.treeData);
                        response["elapsed"] = result.elapsedMs / 1000.0;
                    } else {
                        response["success"] = false;
                        response["error"] = result.error;
                        logger::error("UIManager: {} failed: {}", command, result.error);
                    }
                    std::string payload = JsonText::Dump(response);
                    if (result.success) {
                        logger::info("UIManager: {} completed in {:.2f}s Data size: {} bytes, reply written in {} ms "
                                     "(background thread)", command, result.elapsedMs / 1000.0, payload.size(), MsSince(dumpStarted));
                    }

                    // Marshal the reply back to the game thread for the UI callback
                    AddTaskToGameThread("TreeBuildComplete", [payload = std::move(payload)]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_treeBuildInProgress = false;

                        if (!inst->m_prismaUI) return;
                        const auto sendStarted = std::chrono::steady_clock::now();
                        inst->CallView("onProceduralTreeComplete", payload.c_str());
                        logger::info("UIManager: tree build reply ({} bytes) sent to the panel in {} ms", payload.size(),
                            MsSince(sendStarted));
                    });
                } catch (const std::exception& e) {
                    logger::error("UIManager: TreeBuilder::Build exception: {}", e.what());
                    AddTaskToGameThread("TreeBuildFailed", [error = std::string(e.what())]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_treeBuildInProgress = false;
                        if (!inst->m_prismaUI) return;
                        nlohmann::json response;
                        response["success"] = false;
                        response["error"] = error;
                        inst->CallView("onProceduralTreeComplete", JsonText::Dump(response).c_str());
                    });
                } catch (...) {
                    logger::error("UIManager: TreeBuilder::Build unknown exception");
                    AddTaskToGameThread("TreeBuildFailed", []() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_treeBuildInProgress = false;
                        if (!inst->m_prismaUI) return;
                        nlohmann::json response;
                        response["success"] = false;
                        response["error"] = "Unknown internal error during tree build";
                        inst->CallView("onProceduralTreeComplete", JsonText::Dump(response).c_str());
                    });
                }
            }).detach();

        } catch (const std::exception& e) {
            logger::error("UIManager: ProceduralTreeGenerate failed: {}", e.what());
            instance->m_treeBuildInProgress = false;

            nlohmann::json response;
            response["success"] = false;
            response["error"] = e.what();
            instance->CallView("onProceduralTreeComplete", JsonText::Dump(response).c_str());
        }
    });
}
