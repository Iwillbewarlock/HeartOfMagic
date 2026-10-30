// =============================================================================
// HOM - the wrappers around Heart of Magic's public API v2. See Hom.h.
// =============================================================================

#include "Common.h"

#include "Hom.h"

#include "GameThread.h"

#include "SpellLearningAPI.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace Hom
{
    namespace
    {
        std::atomic<SpellLearning::ISpellLearningAPI*> g_api{ nullptr };
        std::atomic<bool> g_wrongThreadLogged{ false };
        // Spells whose SetLearningTarget is queued and not finished (call and read-back); one entry per spell, so a
        // finished task clears only its own
        std::mutex g_inFlightLock;
        std::unordered_set<RE::FormID> g_inFlight;
        std::atomic<bool> g_stateChanged{ false };  // a SetLearningTarget task ran (TakeStateChanged)

        // The API, or nullptr when it has not arrived or this is not the game thread (logged once)
        SpellLearning::ISpellLearningAPI* Api()
        {
            auto* api = g_api.load();
            if (!api) return nullptr;
            if (!GameThread::On()) {
                if (!g_wrongThreadLogged.exchange(true)) {
                    logger::error("Hom: API call from thread {} but the game thread is {} - call skipped (logged once)",
                        REX::W32::GetCurrentThreadId(), GameThread::Id());
                }
                return nullptr;
            }
            return api;
        }

        // The task's body, on the game thread: the spell may have become the school's target since the request
        void RunSetTarget(RE::FormID a_spell, const std::string& a_school, const std::string& a_name)
        {
            auto* api = Api();
            if (!api) {
                logger::warn("Hom: SetLearningTarget({:08X} {}) not called - no API on this thread", a_spell, a_name);
                return;
            }
            // API v2 does not expose HoM's learning mode: only the old target is logged
            const auto old = api->GetLearningTarget(a_school);
            const auto* oldForm = old ? RE::TESForm::LookupByID(old) : nullptr;
            logger::info("Hom: SetLearningTarget({:08X} {}) running; {} target before: {:08X} {}", a_spell, a_name, a_school, old,
                oldForm ? oldForm->GetName() : "(none)");
            if (old == a_spell) {
                logger::info("Hom: SetLearningTarget({:08X} {}) skipped - already the {} target", a_spell, a_name, a_school);
                return;
            }
            api->SetLearningTarget(a_spell);
            const auto now = api->GetLearningTarget(a_school);
            g_stateChanged = true;
            logger::info("Hom: SetLearningTarget({:08X} {}) -> {} target now {:08X} ({})", a_spell, a_name, a_school, now,
                now == a_spell ? "set" : "NOT set");
        }
    }

    void SetApi(SpellLearning::ISpellLearningAPI* a_api)
    {
        g_api = a_api;
    }

    bool Ready()
    {
        return g_api.load() != nullptr;
    }

    RE::FormID Target(const std::string& a_school)
    {
        auto* api = Api();
        return api && !a_school.empty() ? api->GetLearningTarget(a_school) : 0;
    }

    float ProgressPercent(RE::FormID a_spell)
    {
        auto* api = Api();
        return api && a_spell ? api->GetProgress(a_spell) : 0.0f;
    }

    float RequiredXP(RE::FormID a_spell)
    {
        auto* api = Api();
        return api && a_spell ? api->GetRequiredXP(a_spell) : 0.0f;
    }

    bool Mastered(RE::FormID a_spell)
    {
        auto* api = Api();
        return api && a_spell && api->IsSpellMastered(a_spell);
    }

    bool RequestTarget(RE::FormID a_spell, const std::string& a_school, const std::string& a_name)
    {
        if (!a_spell) return false;
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error("Hom: no SKSE task interface - SetLearningTarget({:08X}) not sent", a_spell);
            return false;
        }
        {
            const std::scoped_lock lock(g_inFlightLock);
            if (!g_inFlight.insert(a_spell).second) {
                logger::info("Hom: SetLearningTarget({:08X} {}) already queued - ignored", a_spell, a_name);
                return false;
            }
        }
        logger::info("Hom: SetLearningTarget({:08X} {}) queued", a_spell, a_name);
        tasks->AddTask([a_spell, a_school, a_name] {
            RunSetTarget(a_spell, a_school, a_name);
            const std::scoped_lock lock(g_inFlightLock);
            g_inFlight.erase(a_spell);  // this spell's reservation only, after the call and its read-back
        });
        return true;
    }

    bool TakeStateChanged()
    {
        return g_stateChanged.exchange(false);
    }
}
