#include "Common.h"
#include "uimanager/UIManager.h"
#include "treebuilder/LayoutDeclutter.h"
#include "ThreadUtils.h"

#include <thread>

// =============================================================================
// TREE DECLUTTER (C++ native)
// =============================================================================
//
// The panel sends a tree about to be saved (LayoutDeclutter.applyAsync in
// modules/layoutDeclutter.js); the pass runs on a worker thread and the
// positions go back to window.onDeclutterResult. The panel's browser has no
// JIT: the same pass in its JavaScript took seconds of frames for a big tree.
//
// Threads, as the tree build's:
//   listener (PrismaUI)  -> game thread (AddTaskToGameThread: never call back
//                           into the view from inside its own listener)
//   game thread          -> worker (std::thread: parse, declutter, write the reply)
//   worker               -> game thread -> CallView("onDeclutterResult")
//
// No in-progress guard: every request is answered, and the panel drops a reply
// whose id is not its latest request's. A failure answers { id, error }, and
// the panel then runs its own JavaScript pass.

namespace
{
    void SendDeclutterReply(std::string payload)
    {
        AddTaskToGameThread("DeclutterTreeComplete", [payload = std::move(payload)]() {
            auto* instance = UIManager::GetSingleton();
            if (!instance) return;
            instance->SendDeclutterResult(payload);
        });
    }

    // A parse error's message can quote bytes that are not UTF-8: replaced, not thrown
    std::string Dump(const nlohmann::json& value)
    {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string ErrorReply(const nlohmann::json& id, const std::string& error)
    {
        nlohmann::json reply;
        reply["id"] = id;
        reply["error"] = error;
        return Dump(reply);
    }
}

void UIManager::SendDeclutterResult(const std::string& payload)
{
    CallView("onDeclutterResult", payload.c_str());
}

void UIManager::OnDeclutterTree(const char* argument)
{
    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("DeclutterTree", [argStr = std::move(argStr)]() mutable {
        std::thread([argStr = std::move(argStr)]() {
            nlohmann::json id;
            try {
                const nlohmann::json request = nlohmann::json::parse(argStr);
                if (request.is_object() && request.contains("id")) id = request["id"];
                const nlohmann::json reply = LayoutDeclutter::Run(request);
                SendDeclutterReply(Dump(reply));
            } catch (const std::exception& e) {
                logger::error("UIManager: DeclutterTree failed: {}", e.what());
                SendDeclutterReply(ErrorReply(id, e.what()));
            } catch (...) {
                logger::error("UIManager: DeclutterTree failed with an unknown exception");
                SendDeclutterReply(ErrorReply(id, "unknown error in the native declutter"));
            }
        }).detach();
    });
}
