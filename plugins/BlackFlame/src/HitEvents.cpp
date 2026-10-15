#include "HitEvents.h"

#include "SelfTestProbe.h"

namespace BlackFlame
{
    HitEventSink* HitEventSink::GetSingleton()
    {
        static HitEventSink instance;
        return &instance;
    }

    void HitEventSink::Register()
    {
        if (m_registered) {
            return;
        }
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::warn("BlackFlame: ScriptEventSourceHolder unavailable - the self-test sees no hit events");
            return;
        }
        holder->AddEventSink<RE::TESHitEvent>(this);
        m_registered = true;
    }

    RE::BSEventNotifyControl HitEventSink::ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*)
    {
        // Called from engine code: nothing may throw out of here
        try {
            if (a_event && SelfTestProbe::Recording()) {
                SelfTestProbe::RecordHit({ 0, a_event->target ? a_event->target->GetFormID() : 0,
                    a_event->cause ? a_event->cause->GetFormID() : 0, a_event->source, a_event->projectile });
            }
        } catch (...) {
            // a record lost: the self-test then reports no hit
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
