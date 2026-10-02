#include "Common.h"
#include "IncomingWarning.h"
#include "CombatConfig.h"
#include "WardSupport.h"
#include "CounterPrompt.h"

#include <atomic>
#include <chrono>

namespace SLCombat
{
    namespace
    {
        constexpr std::size_t kUpdateSlot = 0xAB;

        // Aim point on the player, as a fraction of their height (the chest)
        constexpr float kChestHeightFraction = 0.6f;

        // A threat report older than this is gone (missiles report every frame)
        constexpr auto kThreatStale = std::chrono::milliseconds(80);

        using Clock = std::chrono::steady_clock;

        std::atomic<float> s_threatSeconds{ -1.0f };
        std::atomic<std::int64_t> s_threatAt{ 0 };  ///< Clock ticks of the report

        void PublishThreat(float a_seconds)
        {
            auto now = Clock::now().time_since_epoch().count();
            auto at = s_threatAt.load();
            bool stale = Clock::duration(now - at) > kThreatStale;
            float current = s_threatSeconds.load();
            if (stale || current < 0.0f || a_seconds < current) {
                s_threatSeconds.store(a_seconds);
                s_threatAt.store(now);
            }
        }

        /// Seconds until the missile reaches the player, or a negative value when
        /// it is moving away or will pass wider than warnRadius.
        float SecondsToHit(RE::Projectile* a_projectile, RE::PlayerCharacter* a_player, float a_radius)
        {
            const auto& velocity = a_projectile->GetProjectileRuntimeData().linearVelocity;
            float speed = velocity.Length();
            if (speed <= 0.0f) {
                return -1.0f;
            }
            RE::NiPoint3 direction = velocity / speed;

            auto target = a_player->GetPosition();
            target.z += a_player->GetHeight() * kChestHeightFraction;
            auto toTarget = target - a_projectile->GetPosition();

            float along = toTarget.Dot(direction);
            if (along <= 0.0f) {
                return -1.0f;  // already past, or flying away
            }
            auto closest = toTarget - direction * along;
            if (closest.Length() > a_radius) {
                return -1.0f;  // will miss
            }
            return along / speed;
        }
    }

    float IncomingWarning::NearestThreatSeconds()
    {
        auto at = s_threatAt.load();
        if (at == 0) {
            return -1.0f;
        }
        auto age = Clock::now() - Clock::time_point(Clock::duration(at));
        if (age > kThreatStale) {
            return -1.0f;
        }
        float seconds = s_threatSeconds.load() - std::chrono::duration<float>(age).count();
        return seconds < 0.0f ? 0.0f : seconds;
    }

    void IncomingWarning::Install()
    {
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MissileProjectile[0] };
        s_original = vtbl.write_vfunc(kUpdateSlot, OnMissileUpdate);
        logger::info("SL_Combat: MissileProjectile update hook installed (incoming spells)");
    }

    void IncomingWarning::OnMissileUpdate(RE::Projectile* a_this, float a_delta)
    {
        s_original(a_this, a_delta);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_this || !player) {
            return;
        }
        auto& runtime = a_this->GetProjectileRuntimeData();
        if (!runtime.spell) {
            return;
        }
        auto shooterPtr = runtime.shooter.get();
        if (shooterPtr.get() == player) {
            return;
        }
        const auto& config = CombatConfig::Get();
        if (!config.enableCounter) {
            return;  // switched off from the settings page
        }
        float seconds = SecondsToHit(a_this, player, config.warnRadius);
        if (seconds < 0.0f || seconds > config.warnLeadSeconds) {
            return;
        }
        // Only worth a warning when a counter is possible at all
        if (config.requireKnownWard && !WardSupport::BestKnownWard(player)) {
            return;
        }
        PublishThreat(seconds);
        CounterPrompt::OnThreat(seconds);
    }
}
