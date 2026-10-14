#include "Common.h"
#include "JsonText.h"
#include "PathText.h"
#include "FileUtils.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
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
            std::ifstream file(treePath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();

                std::string treeContent = buffer.str();
                logger::info("UIManager: Loaded spell tree from file ({} bytes)", treeContent.size());
                constexpr std::string_view kBlank = " \t\r\n";
                if (treeContent.find_first_not_of(kBlank) == std::string::npos) {
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

                    // Validate and fix form IDs using persistent IDs
                    auto validationResult = SpellScanner::ValidateAndFixTree(treeData);
                    if (validationResult.resolvedFromPersistent > 0) {
                        logger::info("UIManager: Resolved {} spells from persistent IDs (load order changed)",
                            validationResult.resolvedFromPersistent);
                    }
                    if (validationResult.invalidNodes > 0) {
                        logger::warn("UIManager: {} spells could not be resolved (plugins may be missing)",
                            validationResult.invalidNodes);
                    }
                    // The panel gets the fixed tree: new ids, and without the dropped nodes
                    // and the links to them (the file on disk keeps the old one)
                    if (validationResult.resolvedFromPersistent > 0 || validationResult.invalidNodes > 0) {
                        treeContent = JsonText::Dump(treeData);
                    }

                    // Send validated tree data to viewer
                    instance->SendTreeData(treeContent);

                    // Sync requiredXP to ProgressionManager
                    auto* pm = ProgressionManager::GetSingleton();
                    int xpSyncCount = 0;

                    if (treeData.contains("schools")) {
                        for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
                            if (schoolData.contains("nodes")) {
                                for (auto& node : schoolData["nodes"]) {
                                    if (node.contains("formId")) {
                                        std::string formIdStr = node["formId"].get<std::string>();
                                        // Sync requiredXP from tree to ProgressionManager
                                        if (node.contains("requiredXP") && node["requiredXP"].is_number()) {
                                            float reqXP = node["requiredXP"].get<float>();
                                            if (reqXP > 0) {
                                                try {
                                                    RE::FormID formId = std::stoul(formIdStr, nullptr, 0);
                                                    pm->SetRequiredXP(formId, reqXP);
                                                    xpSyncCount++;
                                                } catch (const std::exception& e) {
                                                    logger::warn("UIManager: Failed to parse formId '{}' for XP sync: {}", formIdStr, e.what());
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (xpSyncCount > 0) {
                        logger::info("UIManager: Synced requiredXP for {} spells from tree to ProgressionManager", xpSyncCount);
                    }

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

                // Validate formId format (should be 0x followed by 8 hex chars)
                if (formIdStr.length() < 3 || formIdStr.substr(0, 2) != "0x") {
                    logger::warn("UIManager: Invalid formId format: {}", formIdStr);
                    json notFound;
                    notFound["formId"] = formIdStr;
                    notFound["notFound"] = true;
                    resultArray.push_back(notFound);
                    notFoundCount++;
                    continue;
                }

                // Built as JSON and kept that way - no text round trip per spell
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

void UIManager::SaveSpellTreeFromPanel(const char* argument, bool built)
{
    logger::info("UIManager: SaveSpellTree callback triggered{}", built ? " (a build's Apply)" : "");

    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SaveSpellTree - no content to save");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("SaveSpellTree", [argStr, built]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Create output directory
        std::filesystem::path outputDir = "Data/SKSE/Plugins/SpellLearning";
        std::filesystem::create_directories(outputDir);

        // Write to file
        auto treePath = GetTreeFilePath();

        // The same tree again - applying twice, or a builder that saves on
        // every step - is not written again. Rewriting it would also rotate
        // the .bak, replacing the real previous tree with a copy of this one.
        static std::size_t s_lastSavedHash = 0;
        const std::size_t hash = std::hash<std::string>{}(argStr);
        std::error_code existsError;
        if (hash == s_lastSavedHash && std::filesystem::exists(treePath, existsError)) {
            logger::info("UIManager: Spell tree unchanged since the last save - not rewritten");
            if (built) instance->RecordTreeLoadOrder();
            instance->UpdateTreeStatus("Tree saved");
            return;
        }

        // Never a tree without spells over the player's file (the panel's SpellTreeSave refuses one too)
        const auto checkMs = CheckTreeHasSpells(argStr);
        if (!checkMs) {
            instance->UpdateTreeStatus("Save failed");
            return;
        }

        try {
            // Through a temp file and a move, keeping one .bak: this is the
            // player's whole generated tree, and truncating the real file meant
            // a crash partway through lost it with nothing to fall back on.
            const auto writeStarted = std::chrono::steady_clock::now();
            if (FileUtils::WriteAtomically(treePath, argStr)) {
                s_lastSavedHash = hash;
                logger::info("UIManager: Saved spell tree to {} (spell check {} ms, written in {} ms)", PathText::Utf8(treePath),
                    *checkMs, MsSince(writeStarted));
                if (built) instance->RecordTreeLoadOrder();
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
