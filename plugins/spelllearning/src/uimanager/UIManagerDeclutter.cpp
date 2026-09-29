#include "Common.h"
#include "JsonText.h"
#include "uimanager/UIManager.h"
#include "treebuilder/LayoutDeclutter.h"
#include "ThreadUtils.h"

#include <eh.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
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
// The panel only takes the reply whose id is its latest request's, so a new
// request cancels the worker still running (it stops at the next spell or
// round and sends nothing), and so does DeclutterCancel(id), which the panel
// sends when it stops waiting for that worker (NATIVE_TIMEOUT_MS, an
// unreadable reply, a newer request it arranges itself). A DeclutterCancel
// that stops the latest worker is answered { id, cancelled: true }: the panel
// takes nothing from it, but any readable reply ends its NATIVE_RETRY_MS pause
// after a timeout (the plugin answers, so it is asked again). A worker that
// finished just before is still flagged, so its request may get both replies;
// the panel has taken the first by then and ignores the second. A worker
// that fails answers { id, error } - the id read off the front of the request
// when it cannot be parsed, or when no worker could be started - and the panel
// then runs its own JavaScript pass. A reply can still be lost (no SKSE task
// interface, no UIManager, an allocation failing on the way): the panel's
// timeout covers that.
//
// Workers are detached, never joined. SKSE sends no message when the game
// closes and never unloads a plugin; at process exit Windows ends every thread
// before a DLL's static destructors run, and joining there (under the loader
// lock) could only hang. So a worker still running at exit is simply ended
// with the process: it holds nothing but its own copy of the request.

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
        return JsonText::Dump(value);
    }

    std::string ErrorReply(const nlohmann::json& id, const std::string& error)
    {
        nlohmann::json reply;
        reply["id"] = id;
        reply["error"] = error;
        return Dump(reply);
    }

    // DeclutterCancel's answer: nothing to apply, but readable (see the top of the file)
    std::string CancelledReply(const std::string& id)
    {
        nlohmann::json reply;
        reply["id"] = id;
        reply["cancelled"] = true;
        return Dump(reply);
    }

    // The request's id without parsing it: the panel writes {"id":"declutter-..."
    // first. For a request that cannot be parsed and a worker that cannot be
    // started, so the error still reaches the request it is for, and for
    // DeclutterCancel to find the worker; empty if absent.
    std::string PeekId(const std::string& argument)
    {
        static constexpr std::string_view kPrefix = R"({"id":")";
        if (argument.compare(0, kPrefix.size(), kPrefix) != 0) return {};
        const auto end = argument.find('"', kPrefix.size());
        if (end == std::string::npos) return {};
        std::string id = argument.substr(kPrefix.size(), end - kPrefix.size());
        if (id.find('\\') != std::string::npos) return {};  // escaped: not ours
        return id;
    }

    // PeekId's id as the reply's: null when there is none
    nlohmann::json IdValue(const std::string& id)
    {
        return id.empty() ? nlohmann::json() : nlohmann::json(id);
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

    // The reply to `argument`: the positions, or { id, error }; empty when cancelled
    std::string Declutter(const std::string& argument, const std::atomic<bool>& cancel)
    {
        nlohmann::json id = IdValue(PeekId(argument));
        try {
            const nlohmann::json request = nlohmann::json::parse(argument);
            if (request.is_object() && request.contains("id")) id = request["id"];
            return Dump(LayoutDeclutter::Run(request, &cancel));
        } catch (const LayoutDeclutter::Cancelled&) {
            logger::info("UIManager: DeclutterTree {} cancelled (a newer request, or the panel stopped waiting)", Dump(id));
            return {};
        } catch (const SehException& e) {
            logger::critical("UIManager: DeclutterTree {} failed with structured exception 0x{:08X}", Dump(id), e.Code());
            return ErrorReply(id, e.what());
        } catch (const std::exception& e) {
            logger::error("UIManager: DeclutterTree failed: {}", e.what());
            return ErrorReply(id, e.what());
        } catch (...) {
            logger::error("UIManager: DeclutterTree failed with an unknown exception");
            return ErrorReply(id, "unknown error in the native declutter");
        }
    }

    // The thread's function: nothing may leave it (std::terminate), not even
    // from the error handling above (logging and ErrorReply allocate)
    void RunWorker(std::string argument, std::shared_ptr<std::atomic<bool>> cancel) noexcept
    {
        try {
            SehTranslatorScope seh;
            std::string reply = Declutter(argument, *cancel);
            // Cancelled meanwhile: the panel no longer waits for this reply
            if (!reply.empty() && !cancel->load()) SendDeclutterReply(std::move(reply));
        } catch (...) {
            // The reply is lost; the panel runs its own pass after its timeout
            try {
                logger::error("UIManager: DeclutterTree could not finish or queue its reply");
            } catch (...) {
            }
        }
    }

    // =========================================================================
    // THE WORKERS
    // =========================================================================

    // Every Start cancels the worker before it, so only the latest can still
    // be wanted: its cancel flag and request id are all that is kept. Called
    // on the game thread (Start and Cancel in the order the panel sent them).
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

        // Cancel the worker still running and start a detached one for
        // `argument` (see the top of the file). Throws (std::system_error,
        // std::bad_alloc) when none can be started.
        void Start(std::string argument, std::string id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_current) m_current->store(true);
            auto cancel = std::make_shared<std::atomic<bool>>(false);
            std::thread thread(RunWorker, std::move(argument), cancel);
            thread.detach();
            // Neither can throw: nothing after the thread is started can fail
            m_current = std::move(cancel);
            m_currentId = std::move(id);
        }

        // DeclutterCancel: stop the latest worker if it is the one for `id`
        // (any older one was cancelled when the next started); true when this
        // call is what stopped it
        bool Cancel(const std::string& id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_current || id.empty() || id != m_currentId) return false;
            return !m_current->exchange(true);
        }

    private:
        DeclutterWorkers() = default;

        std::mutex m_mutex;
        std::shared_ptr<std::atomic<bool>> m_current;
        std::string m_currentId;
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
        std::string id = PeekId(argStr);
        const nlohmann::json idValue = IdValue(id);
        try {
            DeclutterWorkers::Get().Start(std::move(argStr), std::move(id));
        } catch (const std::exception& e) {
            logger::error("UIManager: DeclutterTree could not start a worker: {}", e.what());
            SendDeclutterReply(ErrorReply(idValue, std::string("could not start the native declutter: ") + e.what()));
        }
    });
}

// The panel stopped waiting for the request with this id (argument: the id as sent)
void UIManager::OnDeclutterCancel(const char* argument)
{
    std::string id(argument ? argument : "");

    AddTaskToGameThread("DeclutterCancel", [id = std::move(id)]() {
        if (!DeclutterWorkers::Get().Cancel(id)) return;
        logger::info("UIManager: DeclutterCancel {}: worker told to stop", id);
        SendDeclutterReply(CancelledReply(id));
    });
}
