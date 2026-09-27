#include "Common.h"
#include "uimanager/UIManager.h"
#include "treebuilder/LayoutDeclutter.h"
#include "ThreadUtils.h"

#include <eh.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

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
// The panel only takes the reply whose id is its latest request's, so a new
// request cancels the workers still running (they stop at the next spell or
// round and send nothing). Every other request is answered: a failure answers
// { id, error } - the id read off the front of the request when it cannot be
// parsed, or when no worker could be started - and the panel then runs its own
// JavaScript pass. The workers are joined when the plugin unloads (cancelled
// first, so that is quick).

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

    // The request's id without parsing it: the panel writes {"id":"declutter-..."
    // first. For a request that cannot be parsed and a worker that cannot be
    // started, so the error still reaches the request it is for; null if absent.
    nlohmann::json PeekId(const std::string& argument)
    {
        static constexpr std::string_view kPrefix = R"({"id":")";
        if (argument.compare(0, kPrefix.size(), kPrefix) != 0) return nullptr;
        const auto end = argument.find('"', kPrefix.size());
        if (end == std::string::npos) return nullptr;
        std::string id = argument.substr(kPrefix.size(), end - kPrefix.size());
        if (id.find('\\') != std::string::npos) return nullptr;  // escaped: not ours
        return id;
    }

    // =========================================================================
    // STRUCTURED EXCEPTIONS (/EHa)
    // =========================================================================

    // A Windows structured exception (an access violation, say) as a C++ one,
    // so the worker logs its code instead of catch (...) taking it unnamed
    class SehException : public std::exception
    {
    public:
        explicit SehException(unsigned int code) : m_code(code) {}
        const char* what() const noexcept override { return "structured exception in the native declutter"; }
        unsigned int Code() const { return m_code; }

    private:
        unsigned int m_code;
    };

    void TranslateSeh(unsigned int code, struct _EXCEPTION_POINTERS*)
    {
        throw SehException(code);
    }

    // _set_se_translator is per thread: set for the worker's life, then put back
    class SehTranslatorScope
    {
    public:
        SehTranslatorScope() : m_previous(_set_se_translator(TranslateSeh)) {}
        ~SehTranslatorScope() { _set_se_translator(m_previous); }
        SehTranslatorScope(const SehTranslatorScope&) = delete;
        SehTranslatorScope& operator=(const SehTranslatorScope&) = delete;

    private:
        _se_translator_function m_previous;
    };

    // =========================================================================
    // ONE WORKER
    // =========================================================================

    void RunWorker(std::string argument, std::shared_ptr<std::atomic<bool>> cancel, std::shared_ptr<std::atomic<bool>> finished)
    {
        SehTranslatorScope seh;
        nlohmann::json id = PeekId(argument);
        std::string reply;
        try {
            const nlohmann::json request = nlohmann::json::parse(argument);
            if (request.is_object() && request.contains("id")) id = request["id"];
            reply = Dump(LayoutDeclutter::Run(request, cancel.get()));
        } catch (const LayoutDeclutter::Cancelled&) {
            logger::info("UIManager: DeclutterTree {} cancelled (a newer request, or the plugin unloading)", Dump(id));
        } catch (const SehException& e) {
            logger::critical("UIManager: DeclutterTree {} failed with structured exception 0x{:08X}", Dump(id), e.Code());
            reply = ErrorReply(id, e.what());
        } catch (const std::exception& e) {
            logger::error("UIManager: DeclutterTree failed: {}", e.what());
            reply = ErrorReply(id, e.what());
        } catch (...) {
            logger::error("UIManager: DeclutterTree failed with an unknown exception");
            reply = ErrorReply(id, "unknown error in the native declutter");
        }
        // Cancelled: a newer request's reply is the one the panel waits for, or the plugin is going
        try {
            if (!reply.empty() && !cancel->load()) SendDeclutterReply(std::move(reply));
        } catch (...) {
            logger::error("UIManager: DeclutterTree could not queue its reply");
        }
        finished->store(true);
    }

    // =========================================================================
    // THE WORKERS
    // =========================================================================

    class DeclutterWorkers
    {
    public:
        static DeclutterWorkers& Get()
        {
            static DeclutterWorkers workers;
            return workers;
        }

        DeclutterWorkers(const DeclutterWorkers&) = delete;
        DeclutterWorkers& operator=(const DeclutterWorkers&) = delete;

        // The plugin unloading: cancel every worker, then wait for each
        ~DeclutterWorkers()
        {
            std::vector<Worker> workers;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_closed = true;
                workers.swap(m_workers);
            }
            for (auto& worker : workers) worker.cancel->store(true);
            for (auto& worker : workers) {
                if (worker.thread.joinable()) worker.thread.join();
            }
        }

        // Cancel the workers still running and start one for `argument`.
        // Throws (std::system_error) when no thread can be started.
        void Start(std::string argument)
        {
            std::vector<std::thread> done;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_closed) return;
                for (auto it = m_workers.begin(); it != m_workers.end();) {
                    it->cancel->store(true);
                    if (it->finished->load()) {
                        done.push_back(std::move(it->thread));
                        it = m_workers.erase(it);
                    } else {
                        ++it;
                    }
                }
                auto cancel = std::make_shared<std::atomic<bool>>(false);
                auto finished = std::make_shared<std::atomic<bool>>(false);
                std::thread thread(RunWorker, std::move(argument), cancel, finished);
                m_workers.push_back(Worker{ std::move(thread), std::move(cancel), std::move(finished) });
            }
            // Finished already: each returns at once
            for (auto& thread : done) {
                if (thread.joinable()) thread.join();
            }
        }

    private:
        DeclutterWorkers() = default;

        struct Worker
        {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> cancel;
            std::shared_ptr<std::atomic<bool>> finished;
        };

        std::mutex m_mutex;
        std::vector<Worker> m_workers;
        bool m_closed = false;
    };
}

void UIManager::SendDeclutterResult(const std::string& payload)
{
    CallView("onDeclutterResult", payload.c_str());
}

void UIManager::OnDeclutterTree(const char* argument)
{
    std::string argStr(argument ? argument : "");

    AddTaskToGameThread("DeclutterTree", [argStr = std::move(argStr)]() mutable {
        const nlohmann::json id = PeekId(argStr);
        try {
            DeclutterWorkers::Get().Start(std::move(argStr));
        } catch (const std::exception& e) {
            logger::error("UIManager: DeclutterTree could not start a worker: {}", e.what());
            SendDeclutterReply(ErrorReply(id, std::string("could not start the native declutter: ") + e.what()));
        }
    });
}
