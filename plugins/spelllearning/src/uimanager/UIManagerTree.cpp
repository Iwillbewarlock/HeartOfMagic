#include "Common.h"
#include "FileUtils.h"
#include "uimanager/UIManager.h"
#include "SpellScanner.h"
#include "EncodingUtils.h"
#include "ProgressionManager.h"
#include "treebuilder/TreeBuilder.h"
#include "treebuilder/TreeNLP.h"
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

        // Check if saved tree exists
        if (!std::filesystem::exists(treePath)) {
            logger::info("UIManager: No saved spell tree found");
            instance->UpdateTreeStatus("No saved tree - import one");
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

                // Parse and validate tree - this resolves persistentId to current formId
                // when load order has changed since tree was generated
                try {
                    json treeData = json::parse(treeContent);

                    // Validate and fix form IDs using persistent IDs
                    auto validationResult = SpellScanner::ValidateAndFixTree(treeData);
                    if (validationResult.resolvedFromPersistent > 0) {
                        logger::info("UIManager: Resolved {} spells from persistent IDs (load order changed)",
                            validationResult.resolvedFromPersistent);
                        // Update tree content with resolved form IDs
                        treeContent = treeData.dump();
                    }
                    if (validationResult.invalidNodes > 0) {
                        logger::warn("UIManager: {} spells could not be resolved (plugins may be missing)",
                            validationResult.invalidNodes);
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
                        treePath.string());
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
        instance->CallView("updateSpellIcon", reply.dump().c_str());
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

                std::string spellInfo = SpellScanner::GetSpellInfoByFormId(formIdStr);

                if (!spellInfo.empty()) {
                    try {
                        resultArray.push_back(json::parse(spellInfo));
                        foundCount++;
                    } catch (const std::exception& e) {
                        logger::warn("UIManager: Failed to parse spell info in batch for {}: {}", formIdStr, e.what());
                        json notFound;
                        notFound["formId"] = formIdStr;
                        notFound["notFound"] = true;
                        resultArray.push_back(notFound);
                        notFoundCount++;
                    }
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
            instance->SendSpellInfoBatch(resultArray.dump());

        } catch (const std::exception& e) {
            logger::error("UIManager: GetSpellInfoBatch exception: {}", e.what());
        }
    });
}

void UIManager::OnSaveSpellTree(const char* argument)
{
    logger::info("UIManager: SaveSpellTree callback triggered");

    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SaveSpellTree - no content to save");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("SaveSpellTree", [argStr]() {
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
            instance->UpdateTreeStatus("Tree saved");
            return;
        }

        try {
            // Through a temp file and a move, keeping one .bak: this is the
            // player's whole generated tree, and truncating the real file meant
            // a crash partway through lost it with nothing to fall back on.
            if (FileUtils::WriteAtomically(treePath, argStr)) {
                s_lastSavedHash = hash;
                logger::info("UIManager: Saved spell tree to {}", treePath.string());
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
            response["error"] = "Tree build already in progress. Please wait for the current build to finish.";
            instance->CallView("onProceduralTreeComplete", response.dump().c_str());
            return;
        }

        try {
            nlohmann::json request = nlohmann::json::parse(argStr);

            std::string command = "build_tree";
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
                    instance->CallView("onProceduralTreeComplete", response.dump().c_str());
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

                    // Marshal result back to game thread for UI callback
                    AddTaskToGameThread("TreeBuildComplete", [result = std::move(result), command]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_treeBuildInProgress = false;

                        if (!inst->m_prismaUI) return;

                        nlohmann::json response;
                        if (result.success) {
                            response["success"] = true;
                            // The tree goes in as an object, not as a string of JSON.
                            // As a string it was written out three times (once here,
                            // once more for the size in the log, once escaped inside
                            // the reply) and the panel had to parse it twice.
                            response["treeData"] = result.treeData;
                            response["elapsed"] = result.elapsedMs / 1000.0;
                        } else {
                            response["success"] = false;
                            response["error"] = result.error;
                            logger::error("UIManager: {} failed: {}", command, result.error);
                        }

                        const std::string payload = response.dump();
                        if (result.success) {
                            logger::info("UIManager: {} completed in {:.2f}s Data size: {} bytes (background thread)",
                                command, result.elapsedMs / 1000.0, payload.size());
                        }
                        inst->CallView("onProceduralTreeComplete", payload.c_str());
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
                        inst->CallView("onProceduralTreeComplete", response.dump().c_str());
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
                        inst->CallView("onProceduralTreeComplete", response.dump().c_str());
                    });
                }
            }).detach();

        } catch (const std::exception& e) {
            logger::error("UIManager: ProceduralTreeGenerate failed: {}", e.what());
            instance->m_treeBuildInProgress = false;

            nlohmann::json response;
            response["success"] = false;
            response["error"] = e.what();
            instance->CallView("onProceduralTreeComplete", response.dump().c_str());
        }
    });
}

// =============================================================================
// PRE REQ MASTER NLP SCORING (C++ native)
// =============================================================================

void UIManager::OnPreReqMasterScore(const char* argument)
{
    logger::info("UIManager: PreReqMasterScore callback triggered (C++ native)");

    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("PreReqMasterScore", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Guard against concurrent PRM scoring
        bool expected = false;
        if (!instance->m_prmScoreInProgress.compare_exchange_strong(expected, true)) {
            logger::warn("UIManager: PRM scoring already in progress, ignoring request");
            nlohmann::json response;
            response["success"] = false;
            response["error"] = "PRM scoring already in progress. Please wait.";
            instance->CallView("onPreReqMasterComplete", response.dump().c_str());
            return;
        }

        try {
            nlohmann::json request = nlohmann::json::parse(argStr);

            logger::info("UIManager: Dispatching PRM scoring to background thread");

            // Launch background thread — TreeNLP has ZERO RE:: dependencies
            std::thread([request = std::move(request)]() {
                try {
                    auto startTime = std::chrono::high_resolution_clock::now();
                    auto result = TreeNLP::ProcessPRMRequest(request);
                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;

                    logger::info("UIManager: prm_score completed in {:.2f}s (background thread)", elapsed);

                    AddTaskToGameThread("PRMScoreComplete", [result = std::move(result)]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_prmScoreInProgress = false;

                        if (!inst->m_prismaUI) return;
                        inst->CallView("onPreReqMasterComplete", result.dump().c_str());
                    });
                } catch (const std::exception& e) {
                    logger::error("UIManager: ProcessPRMRequest exception: {}", e.what());
                    AddTaskToGameThread("PRMScoreFailed", [error = std::string(e.what())]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_prmScoreInProgress = false;
                        if (!inst->m_prismaUI) return;
                        nlohmann::json result;
                        result["success"] = false;
                        result["error"] = error;
                        inst->CallView("onPreReqMasterComplete", result.dump().c_str());
                    });
                } catch (...) {
                    logger::error("UIManager: ProcessPRMRequest unknown exception");
                    AddTaskToGameThread("PRMScoreFailed", []() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->m_prmScoreInProgress = false;
                        if (!inst->m_prismaUI) return;
                        nlohmann::json result;
                        result["success"] = false;
                        result["error"] = "Unknown internal error during PRM scoring";
                        inst->CallView("onPreReqMasterComplete", result.dump().c_str());
                    });
                }
            }).detach();

        } catch (const std::exception& e) {
            logger::error("UIManager: PRM scoring failed: {}", e.what());
            instance->m_prmScoreInProgress = false;

            nlohmann::json response;
            response["success"] = false;
            response["error"] = e.what();
            instance->CallView("onPreReqMasterComplete", response.dump().c_str());
        }
    });
}
