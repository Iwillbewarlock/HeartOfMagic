#include "EffectEvents.h"

#include "BuffState.h"
#include "BurnShader.h"
#include "Config.h"
#include "GameThread.h"

#include <atomic>

namespace BlackFlame
{
    namespace
    {
        std::atomic<bool> g_sinkFailureReported{ false };
    }

    EffectEventSink* EffectEventSink::GetSingleton()
    {
        static EffectEventSink instance;
        return &instance;
    }

    void EffectEventSink::Register()
    {
        if (m_registered) {
            return;
        }
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::error("BlackFlame: ScriptEventSourceHolder unavailable - no effect events (buff only via the ticker)");
            return;
        }
        holder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(this);
        m_registered = true;
        logger::info("BlackFlame: listening for active effect apply/remove events");
    }

    RE::BSEventNotifyControl EffectEventSink::ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* a_event,
        RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*)
    {
        // Called from engine code: nothing may throw out of here
        try {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!a_event || !player) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (a_event->target.get() == player) {
                const char* reason = a_event->isApplied ? "effect applied to the player" : "effect removed from the player";
                RequestRecomputeBuff(reason);
            }
            // Layer 1: a fire effect the player put on anyone while stoked (the task checks the MGEF),
            // and the end of one the burn layer is tracking
            auto* target = a_event->target.get();
            if (target && a_event->caster.get() == player && GetConfig().burnShader) {
                if (a_event->isApplied && IsStoked()) {
                    OnPlayerEffectApplied(target, a_event->activeEffectUniqueID);
                } else if (!a_event->isApplied && HasLiveBurns()) {
                    QueueBurnEnd(target->GetHandle(), a_event->activeEffectUniqueID);
                }
            }
        } catch (...) {
            if (!g_sinkFailureReported.exchange(true)) {
                logger::error("BlackFlame: effect event handler threw; further failures are not logged");
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
