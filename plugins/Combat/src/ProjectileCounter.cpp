#include "Common.h"
#include "ProjectileCounter.h"
#include "CombatConfig.h"
#include "CounterInput.h"
#include "CounterResponse.h"
#include "WardSupport.h"

#include <array>
#include <cmath>
#include <numbers>

namespace SLCombat
{
    namespace
    {
        // MissileProjectile vtable slot used by Elden Parry for collisions
        constexpr std::size_t kCollisionSlot = 0xBE;

        // Ward-reflect DLLs (the name is the shipped DLL's; unverified ones noted in docs/COMBAT.md)
        constexpr std::array kWardReflectDlls{ "PerfectlyValidWards.dll" };
        // Weapon/shield parry DLLs: logged only
        constexpr std::array kWeaponParryDlls{ "EldenParry.dll", "ParryAll.dll", "DualWieldParryingNG.dll", "ValhallaCombat.dll" };

        // Aim reflected spells at the chest, not at the feet (fraction of the target's height)
        constexpr float kAimHeightFraction = 0.6f;

        // Collision filter words: low half = layer/biped/flags, high half = system group
        constexpr std::uint32_t kLowHalfMask = 0x0000FFFF;
        constexpr std::uint32_t kSystemGroupShift = 16;

        constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;

        bool s_wardReflectMod = false;

        bool DllLoaded(const char* a_name)
        {
            return GetModuleHandleA(a_name) != nullptr;
        }

        RE::NiPoint3 AimPoint(RE::TESObjectREFR* a_target)
        {
            auto pos = a_target->GetPosition();
            pos.z += a_target->GetHeight() * kAimHeightFraction;
            return pos;
        }

        void FaceDirection(RE::Projectile* a_projectile, RE::NiPoint3 a_direction)
        {
            a_direction.Unitize();
            a_projectile->data.angle.x = -std::asin(std::clamp(a_direction.z, -1.0f, 1.0f));
            a_projectile->data.angle.z = std::atan2(a_direction.x, a_direction.y);
            if (auto* node = a_projectile->Get3D2()) {
                node->local.rotate.EulerAnglesToAxesZXY(a_projectile->data.angle);
            }
        }

        void RewriteFilter(RE::hkpCollidable* a_collidable, RE::Actor* a_owner)
        {
            RE::CFilter ownerFilter{};
            a_owner->GetCollisionFilterInfo(ownerFilter);
            auto& filter = a_collidable->broadPhaseHandle.collisionFilterInfo;
            if (CombatConfig::Get().filterMode == FilterMode::kShift) {
                filter.filter = (filter.filter & kLowHalfMask) | (ownerFilter.filter << kSystemGroupShift);
            } else {
                filter.SetSystemGroup(ownerFilter.GetSystemGroup());
            }
        }

        /// Degrees between the player's facing and the direction the projectile
        /// came from (horizontal). At impact the projectile sits on the player's
        /// capsule, so its position says little; its velocity says where it came from.
        float IncomingAngle(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_velocity)
        {
            RE::NiPoint3 from{ -a_velocity.x, -a_velocity.y, 0.0f };
            if (from.Unitize() <= 0.0f) {
                return 0.0f;  // no horizontal motion: falling straight down, treat as in front
            }
            float heading = a_player->data.angle.z;
            RE::NiPoint3 facing{ std::sin(heading), std::cos(heading), 0.0f };
            float dot = std::clamp(facing.x * from.x + facing.y * from.y, -1.0f, 1.0f);
            return std::acos(dot) * kRadToDeg;
        }

        /// The countered spell vanishes: it stops colliding with the player now (we
        /// may be on any thread here), and the next game-thread task removes it with
        /// its explosion cleared, so a fireball does not burst in the player's face.
        void Absorb(RE::Projectile* a_projectile, RE::hkpCollidable* a_projectileCollidable, RE::Actor* a_player)
        {
            if (a_projectileCollidable) {
                RewriteFilter(a_projectileCollidable, a_player);
            }
            auto handle = a_projectile->CreateRefHandle();
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                return;
            }
            tasks->AddTask([handle, a_projectile]() {
                auto ref = handle.get();
                if (ref.get() != a_projectile) {
                    return;  // already gone, or the handle now names something else
                }
                a_projectile->GetProjectileRuntimeData().explosion = nullptr;
                a_projectile->Kill();
            });
        }
    }

    RE::hkpCollidable* OtherSideOfHit(RE::hkpAllCdPointCollector* a_collector, RE::Actor* a_actor)
    {
        if (!a_collector || !a_actor) {
            return nullptr;
        }
        for (auto& hit : a_collector->hits) {
            if (!hit.rootCollidableA || !hit.rootCollidableB) {
                continue;
            }
            if (RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA) == a_actor) {
                return const_cast<RE::hkpCollidable*>(hit.rootCollidableB);
            }
            if (RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB) == a_actor) {
                return const_cast<RE::hkpCollidable*>(hit.rootCollidableA);
            }
        }
        return nullptr;
    }

    bool DetectWardReflectMod()
    {
        for (const char* dll : kWeaponParryDlls) {
            if (DllLoaded(dll)) {
                logger::info("SL_Combat: {} loaded - weapon/shield parry, no overlap with spell counters", dll);
            }
        }
        for (const char* dll : kWardReflectDlls) {
            if (DllLoaded(dll)) {
                logger::info("SL_Combat: {} loaded - it reflects spells from wards", dll);
                s_wardReflectMod = true;
            }
        }
        return s_wardReflectMod;
    }

    void ProjectileCounter::Install()
    {
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MissileProjectile[0] };
        s_original = vtbl.write_vfunc(kCollisionSlot, OnMissileCollision);
        logger::info("SL_Combat: MissileProjectile collision hook installed (slot 0x{:X})", kCollisionSlot);
    }

    void ProjectileCounter::Reflect(RE::Projectile* a_projectile, RE::Actor* a_newOwner,
        RE::hkpCollidable* a_projectileCollidable, RE::TESObjectREFR* a_target)
    {
        auto& runtime = a_projectile->GetProjectileRuntimeData();

        a_projectile->SetActorCause(a_newOwner->GetActorCause());
        runtime.shooter = a_newOwner->CreateRefHandle();
        if (a_projectileCollidable) {
            RewriteFilter(a_projectileCollidable, a_newOwner);
        }

        float speed = runtime.linearVelocity.Length();
        RE::NiPoint3 direction;
        if (a_target && a_target->Is3DLoaded() && speed > 0.0f) {
            direction = AimPoint(a_target) - a_projectile->GetPosition();
            direction.Unitize();
            runtime.linearVelocity = direction * speed;
            runtime.desiredTarget = a_target->CreateRefHandle();
        } else {
            runtime.linearVelocity *= -1.0f;
            direction = runtime.linearVelocity;
        }
        FaceDirection(a_projectile, direction);
    }

    bool ProjectileCounter::OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_collector)
    {
        const auto& config = CombatConfig::Get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!config.enableCounter || !a_this || !a_collector || !player) {
            return s_original(a_this, a_collector);
        }
        auto& runtime = a_this->GetProjectileRuntimeData();
        if (!runtime.spell) {
            return s_original(a_this, a_collector);  // arrows and other non-spell missiles
        }

        // Is the player one side of a hit pair? The other side is the projectile.
        auto* projectileCollidable = OtherSideOfHit(a_collector, player);
        if (!projectileCollidable) {
            return s_original(a_this, a_collector);
        }

        auto shooterPtr = runtime.shooter.get();
        auto* shooter = shooterPtr.get();
        if (shooter == player) {
            return s_original(a_this, a_collector);  // our own spell
        }

        float angle = IncomingAngle(player, runtime.linearVelocity);
        // 360 Ward makes wards a sphere: then every direction, else frontAngle
        bool inFront = WardSupport::AllAngles() || angle <= config.frontAngle;
        auto press = CounterInput::GetSingleton()->Check();
        bool standDown = press.ward && s_wardReflectMod && config.skipIfOtherParryMod;
        // The counter is a ward technique: no ward known, no counter
        bool knowsWard = !config.requireKnownWard || WardSupport::BestKnownWard(player) != nullptr;
        bool countered = press.inWindow && inFront && !standDown && knowsWard;

        logger::info("counter.impact spell={:08X} '{}' shooter={:08X} msSincePress={:.0f} ward={} knowsWard={} angle={:.0f} countered={}",
            runtime.spell->GetFormID(), runtime.spell->GetName(), shooter ? shooter->GetFormID() : 0,
            press.secondsAgo < 0.0f ? -1.0f : press.secondsAgo * 1000.0f, press.ward,
            knowsWard, angle, countered);

        // Info lines wait in the buffer (Common.h); the M1 test reads these live
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { FlushLog(); });
        }

        if (!countered) {
            return s_original(a_this, a_collector);
        }

        CounterInput::GetSingleton()->Consume();
        CounterResponse::OnCounter(runtime.spell, shooter);

        switch (config.counterResult) {
        case CounterResult::kWard:
            {
                // The counter raises a ward and the hit goes on: the game's own ward
                // rules (and ward mods, 360 Ward) decide what it stops
                // Spells in both hands and the dual-cast perk: the dual-cast ward
                float wardPower = WardSupport::RaiseWard(player);
                logger::info("counter.hit spell={:08X} ward raised wardPower={:.1f}", runtime.spell->GetFormID(), wardPower);
                return s_original(a_this, a_collector);
            }
        case CounterResult::kReflect:
            Reflect(a_this, player, projectileCollidable, shooter);
            logger::info("counter.hit spell={:08X} reflected to {:08X}", runtime.spell->GetFormID(), shooter ? shooter->GetFormID() : 0);
            return config.hookReturnOnCounter;
        case CounterResult::kAbsorb:
        default:
            Absorb(a_this, projectileCollidable, player);
            logger::info("counter.hit spell={:08X} absorbed", runtime.spell->GetFormID());
            return config.hookReturnOnCounter;
        }
    }
}
