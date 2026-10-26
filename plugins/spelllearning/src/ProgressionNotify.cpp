#include "ProgressionNotify.h"
#include "ProgressionNotifyQueue.h"
#include "FormIdText.h"
#include "uimanager/UIManager.h"

#include <mutex>
#include <utility>

// =============================================================================
// ProgressionNotify - hold, then pass on to UIManager (ProgressionNotify.h)
// =============================================================================

namespace ProgressionNotify
{
    namespace
    {
        // Per thread: a notice made on a worker thread is never held by, or sent
        // inside, a Batch the game thread has open
        thread_local ProgressionNotifyQueue<Event> t_queue;

        // The self-test sets it on the game thread; a worker thread's notice reads it too
        std::mutex g_observerLock;
        Observer g_observer;

        Observer CurrentObserver()
        {
            std::lock_guard lock(g_observerLock);
            return g_observer;
        }

        void Dispatch(const Event& event)
        {
            auto* ui = UIManager::GetSingleton();
            switch (event.kind) {
            case Kind::kProgress:
                ui->NotifyProgressUpdate(event.formId, event.a, event.b);
                break;
            case Kind::kProgressRefresh:
                // The formId text the caller had, parsed (and complained about) where it always was
                ui->NotifyProgressUpdate(event.text);
                break;
            case Kind::kSpellReady:
                ui->NotifySpellReady(event.formId);
                break;
            case Kind::kSpellUnlocked:
                ui->NotifySpellUnlocked(event.formId, event.flag);
                break;
            case Kind::kTargetSet:
                ui->NotifyLearningTargetSet(event.school, event.formId, event.text);
                break;
            case Kind::kTargetCleared:
                ui->NotifyLearningTargetCleared(event.formId);
                break;
            case Kind::kTargetDropped:
                ui->NotifyLearningTargetDropped(event.school, event.formId);
                break;
            case Kind::kSpellState:
                ui->UpdateSpellState(FormIdText::Format(event.formId), event.text);
                break;
            case Kind::kSourceRegistered:
                ui->NotifyModdedSourceRegistered(event.text, event.name, event.a, event.b, event.flag);
                break;
            case Kind::kSpellResearchStatus:
                ui->NotifySpellResearchStatus();
                break;
            }
        }

        // One notice going out. Nothing thrown leaves here: a Batch sends from
        // its destructor, and the notices after a failed one still go
        void Send(const Event& event)
        {
            // The self-test's observer first, on its own: if it fails, the panel still gets the notice
            bool deliver = true;
            try {
                if (auto observer = CurrentObserver()) deliver = observer(event);
            } catch (...) {
                logger::error("ProgressionNotify: the observer failed on {} for {:08X}", KindName(event.kind), event.formId);
            }
            if (!deliver) return;
            try {
                Dispatch(event);
            } catch (const std::exception& e) {
                logger::error("ProgressionNotify: {} for {:08X} failed: {}", KindName(event.kind), event.formId, e.what());
            } catch (...) {
                logger::error("ProgressionNotify: {} for {:08X} failed", KindName(event.kind), event.formId);
            }
        }

        void Post(Event event)
        {
            t_queue.Post(std::move(event), Send);
        }
    }

    const char* KindName(Kind kind)
    {
        switch (kind) {
        case Kind::kProgress: return "progress";
        case Kind::kProgressRefresh: return "progress-refresh";
        case Kind::kSpellReady: return "ready";
        case Kind::kSpellUnlocked: return "unlocked";
        case Kind::kTargetSet: return "target-set";
        case Kind::kTargetCleared: return "target-cleared";
        case Kind::kTargetDropped: return "target-dropped";
        case Kind::kSpellState: return "spell-state";
        case Kind::kSourceRegistered: return "source-registered";
        case Kind::kSpellResearchStatus: return "spell-research-status";
        }
        return "?";
    }

    Batch::Batch() { t_queue.Open(); }
    Batch::~Batch() { t_queue.Close(Send); }

    void Progress(RE::FormID formId, float currentXP, float requiredXP)
    {
        Post({ .kind = Kind::kProgress, .formId = formId, .a = currentXP, .b = requiredXP });
    }

    void ProgressRefresh(const std::string& formIdText)
    {
        Post({ .kind = Kind::kProgressRefresh, .formId = FormIdText::Parse(formIdText).value_or(0), .text = formIdText });
    }

    void SpellReady(RE::FormID formId)
    {
        Post({ .kind = Kind::kSpellReady, .formId = formId });
    }

    void SpellUnlocked(RE::FormID formId, bool success)
    {
        Post({ .kind = Kind::kSpellUnlocked, .formId = formId, .flag = success });
    }

    void TargetSet(const std::string& school, RE::FormID formId, const std::string& spellName)
    {
        Post({ .kind = Kind::kTargetSet, .formId = formId, .school = school, .text = spellName });
    }

    void TargetCleared(RE::FormID formId)
    {
        Post({ .kind = Kind::kTargetCleared, .formId = formId });
    }

    void TargetDropped(const std::string& school, RE::FormID formId)
    {
        Post({ .kind = Kind::kTargetDropped, .formId = formId, .school = school });
    }

    void SpellState(RE::FormID formId, const std::string& state)
    {
        Post({ .kind = Kind::kSpellState, .formId = formId, .text = state });
    }

    void SourceRegistered(const std::string& sourceId, const std::string& displayName,
        float multiplier, float cap, bool enabled)
    {
        Post({ .kind = Kind::kSourceRegistered, .text = sourceId, .name = displayName,
            .a = multiplier, .b = cap, .flag = enabled });
    }

    void SpellResearchStatus()
    {
        Post({ .kind = Kind::kSpellResearchStatus });
    }

    void SetObserver(Observer observer)
    {
        std::lock_guard lock(g_observerLock);
        g_observer = std::move(observer);
    }
}
