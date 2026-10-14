#include "Common.h"
#include "JsonText.h"
#include "uimanager/UIManager.h"
#include "treebuilder/TreeNLP.h"
#include "ThreadUtils.h"

#include <chrono>
#include <string>
#include <thread>

// =============================================================================
// PRE REQ MASTER NLP SCORING (C++ native)
// =============================================================================
//
// Moved out of UIManagerTree.cpp (600-line limit). The request - every spell of
// the tree with its text - is parsed on the scoring thread, not on the game
// thread: a parse error comes back through the same PRMScoreFailed reply.

namespace
{
    // The panel's reply for a scoring that did not run
    void ReportPRMFailure(std::string error)
    {
        AddTaskToGameThread("PRMScoreFailed", [error = std::move(error)]() {
            auto* inst = UIManager::GetSingleton();
            if (!inst) return;
            inst->FinishPRMScore(nullptr, error);
        });
    }
}

void UIManager::FinishPRMScore(const nlohmann::json* result, const std::string& error)
{
    m_prmScoreInProgress = false;
    if (!m_prismaUI) return;
    if (result) {
        CallView("onPreReqMasterComplete", JsonText::Dump(*result).c_str());
        return;
    }
    nlohmann::json failed;
    failed["success"] = false;
    failed["error"] = error;
    CallView("onPreReqMasterComplete", JsonText::Dump(failed).c_str());
}

void UIManager::OnPreReqMasterScore(const char* argument)
{
    logger::info("UIManager: PreReqMasterScore callback triggered (C++ native)");

    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("PreReqMasterScore", [argStr = std::move(argStr)]() mutable {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        // Guard against concurrent PRM scoring
        bool expected = false;
        if (!instance->m_prmScoreInProgress.compare_exchange_strong(expected, true)) {
            logger::warn("UIManager: PRM scoring already in progress, ignoring request");
            nlohmann::json response;
            response["success"] = false;
            response["error"] = "PRM scoring already in progress. Please wait.";
            instance->CallView("onPreReqMasterComplete", JsonText::Dump(response).c_str());
            return;
        }

        logger::info("UIManager: Dispatching PRM scoring to background thread ({} bytes of request)", argStr.size());
        try {
            // Launch background thread — TreeNLP has ZERO RE:: dependencies
            std::thread([argStr = std::move(argStr)]() {
                try {
                    auto startTime = std::chrono::high_resolution_clock::now();
                    const nlohmann::json request = nlohmann::json::parse(argStr);
                    auto result = TreeNLP::ProcessPRMRequest(request);
                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;

                    logger::info("UIManager: prm_score completed in {:.2f}s (background thread, request parse included)", elapsed);

                    AddTaskToGameThread("PRMScoreComplete", [result = std::move(result)]() {
                        auto* inst = GetSingleton();
                        if (!inst) return;
                        inst->FinishPRMScore(&result, "");
                    });
                } catch (const std::exception& e) {
                    logger::error("UIManager: ProcessPRMRequest exception: {}", e.what());
                    ReportPRMFailure(e.what());
                } catch (...) {
                    logger::error("UIManager: ProcessPRMRequest unknown exception");
                    ReportPRMFailure("Unknown internal error during PRM scoring");
                }
            }).detach();
        } catch (const std::exception& e) {
            logger::error("UIManager: PRM scoring failed: {}", e.what());
            instance->FinishPRMScore(nullptr, e.what());
        }
    });
}
