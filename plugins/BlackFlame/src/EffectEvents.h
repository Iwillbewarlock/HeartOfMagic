#pragma once

#include "Common.h"

// =============================================================================
// EFFECT EVENTS - TESActiveEffectApplyRemoveEvent
// =============================================================================
// An effect applied to or removed from the player: recompute the buff flag (on a
// game-thread task; the sink itself runs off the game thread on AE). The burn
// shader no longer listens here: it is swapped where the engine asks for it
// (HitEffectHook.h).

namespace BlackFlame
{
    class EffectEventSink final : public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
    {
    public:
        static EffectEventSink* GetSingleton();

        EffectEventSink(const EffectEventSink&) = delete;
        EffectEventSink& operator=(const EffectEventSink&) = delete;

        void Register();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* a_event,
            RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>* a_source) override;

    private:
        EffectEventSink() = default;
        ~EffectEventSink() override = default;

        bool m_registered = false;
    };
}
