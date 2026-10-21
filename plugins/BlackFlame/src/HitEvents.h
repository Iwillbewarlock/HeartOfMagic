#pragma once

#include "Common.h"

// =============================================================================
// HIT EVENTS - TESHitEvent: self-test records and the lantern's extra damage
// =============================================================================
// The sink records (target, cause, source, projectile) into the self-test probe
// while it records (E2: the damage path still fires with a projectile copy;
// E4: whether the player's Firebolt hit the target), and hands every event to
// LanternDamage::OnHit (FormID work and a queued task only, LanternDamage.h).

namespace BlackFlame
{
    class HitEventSink final : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        static HitEventSink* GetSingleton();

        HitEventSink(const HitEventSink&) = delete;
        HitEventSink& operator=(const HitEventSink&) = delete;

        /// kDataLoaded
        void Register();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event,
            RE::BSTEventSource<RE::TESHitEvent>* a_source) override;

    private:
        HitEventSink() = default;
        ~HitEventSink() override = default;

        bool m_registered = false;
    };
}
