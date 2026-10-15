#pragma once

#include "Common.h"

// =============================================================================
// EFFECT EVENTS - TESActiveEffectApplyRemoveEvent
// =============================================================================
// An effect applied to or removed from the player: recompute the buff flag. An
// effect the player applied to anyone while the flag is on: hand it to the burn
// shader layer. Everything is deferred to a game-thread task carrying handles and
// the effect's unique id, never raw pointers.

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
