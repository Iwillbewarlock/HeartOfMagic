#include "SelfTestWorld.h"

#include "Forms.h"

#include <cmath>
#include <format>

namespace BlackFlame::SelfTestWorld
{
    namespace
    {
        constexpr float kFullEffectiveness = 1.0f;
        constexpr float kNoMagnitudeOverride = 0.0f;
        constexpr float kOriginHeightShare = 0.65f;  // of the shooter's height: about the chest
        constexpr float kOriginForward = 60.0f;      // units toward the target, clear of the shooter
        constexpr float kAimHeightShare = 0.6f;      // of the target's height: its torso
        constexpr float kMinHeight = 20.0f;              // below this GetHeight has no bounds yet
        constexpr float kPlayerFallbackHeight = 100.0f;
        constexpr float kCreatureFallbackHeight = 60.0f;  // a deer
        constexpr float kTargetFallbackLift = 50.0f;
        constexpr float kMinBoundRadius = 1.0f;  // below this the 3D's world bound is not set yet
        constexpr float kHalf = 0.5f;
        // An approximation of the engine's gravity in units/s^2: 9.8 m/s^2 at about 70 units/m. Scaled by
        // the projectile's own gravity value; the launch line logs the drop it computed.
        constexpr float kWorldGravity = 686.0f;
    }

    bool LookupVanilla(VanillaForms& a_out, std::string& a_missing)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            a_missing = "TESDataHandler";
            return false;
        }
        a_out.firebolt = dh->LookupForm<RE::SpellItem>(kFireboltLocalId, kSkyrimMaster);
        a_out.fireball = dh->LookupForm<RE::SpellItem>(kFireballLocalId, kSkyrimMaster);
        a_out.deer = dh->LookupForm<RE::TESNPC>(kDeerNpcLocalId, kSkyrimMaster);
        if (a_out.fireball) {
            for (const auto* effect : a_out.fireball->effects) {
                const auto* base = effect ? effect->baseEffect : nullptr;
                if (!base || a_out.fireballExplosion) {
                    continue;
                }
                a_out.fireballExplosion = base->data.explosion ? base->data.explosion :
                    (base->data.projectileBase ? base->data.projectileBase->data.explosionType : nullptr);
            }
        }
        const auto add = [&](bool a_ok, const char* a_name) {
            if (!a_ok) {
                a_missing += a_missing.empty() ? a_name : std::format(", {}", a_name);
            }
        };
        add(a_out.firebolt, "Firebolt 012FD0");
        add(a_out.fireball, "Fireball 01C789");
        add(a_out.deer, "EncDeer 0CF89D");
        add(a_out.fireballExplosion, "Fireball's explosion");
        return a_missing.empty();
    }

    bool Cast(RE::Actor* a_caster, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target)
    {
        if (!a_caster || !a_spell || !a_target) {
            return false;
        }
        auto* caster = a_caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster) {
            return false;
        }
        caster->CastSpellImmediate(a_spell, false, a_target, kFullEffectiveness, false, kNoMagnitudeOverride, a_caster);
        return true;
    }

    namespace
    {
        struct Aim
        {
            RE::NiPoint3 origin;
            RE::Projectile::ProjectileRot angles{};
            std::string text;  // what the aim did (for the step's line)
        };

        // From the shooter's chest at the target's torso (its 3D bound centre when it has one), aimed above it by the
        // projectile's estimated gravity drop (see LaunchAt in the header)
        Aim AimAt(RE::Actor* a_shooter, RE::TESObjectREFR* a_target, const RE::BGSProjectile& a_projectile)
        {
            // Heights: a fresh spawn may have no bounds yet (GetHeight near 0): fixed fallbacks then
            float shooterHeight = a_shooter->GetHeight();
            float targetHeight = a_target->GetHeight();
            std::string fallback;
            if (shooterHeight < kMinHeight) {
                fallback += std::format(" shooter height {:.1f} -> fallback;", shooterHeight);
                shooterHeight = a_shooter->IsPlayerRef() ? kPlayerFallbackHeight : kCreatureFallbackHeight;
            }
            float aimLift = targetHeight * kAimHeightShare;
            if (targetHeight < kMinHeight) {
                aimLift = kTargetFallbackLift;
                fallback += std::format(" target height {:.1f} -> fallback lift;", targetHeight);
            }
            // Origin: the shooter's chest, nudged toward the target so it starts clear of the shooter
            const auto from = a_shooter->GetPosition();
            auto aim = a_target->GetPosition();
            aim.z += aimLift;
            // C3: the centre of the target's 3D (its world bound) when it has one: the torso of a standing deer
            const char* aimedAt = "height share";
            if (const auto* root = a_target->Get3D(); root && root->worldBound.radius >= kMinBoundRadius) {
                aim = root->worldBound.center;
                aimedAt = "3D bound centre";
            }
            const float flatX = aim.x - from.x;
            const float flatY = aim.y - from.y;
            const float flat = std::sqrt(flatX * flatX + flatY * flatY);
            Aim out;
            out.origin = from;
            out.origin.z += shooterHeight * kOriginHeightShare;
            if (flat > 0.0f) {
                out.origin.x += flatX / flat * kOriginForward;
                out.origin.y += flatY / flat * kOriginForward;
            }
            // Gravity drop over the flight, aimed above to make up for it (an estimate: see kWorldGravity)
            const float distance = out.origin.GetDistance(aim);
            const float speed = a_projectile.data.speed;
            const float flightSeconds = speed > 0.0f ? distance / speed : 0.0f;
            const float drop = kHalf * kWorldGravity * a_projectile.data.gravity * flightSeconds * flightSeconds;
            aim.z += drop;
            // Straight line: heading z = atan2(dx, dy) (as Actor::SetHeading), pitch x positive DOWN.
            // No engine solve: CombatUtilities::GetAngleToProjectedTarget's AE id is unverified
            // (CommonLib's Projectile.cpp:290 marks it 0) and a wrong id would crash on AE.
            const float dx = aim.x - out.origin.x;
            const float dy = aim.y - out.origin.y;
            const float dz = aim.z - out.origin.z;
            out.angles.z = std::atan2(dx, dy);
            out.angles.x = -std::atan2(dz, std::sqrt(dx * dx + dy * dy));
            out.text = std::format("aimed at the target's {}: launched from ({:.0f}, {:.0f}, {:.0f}) at ({:.0f}, {:.0f}, {:.0f}) incl. "
                "{:.1f} units of gravity drop ({:.0f} units at speed {:.0f}, gravity {:.2f}): pitch {:.3f}, heading {:.3f} rad;{}",
                aimedAt, out.origin.x, out.origin.y, out.origin.z, aim.x, aim.y, aim.z, drop, distance, speed,
                a_projectile.data.gravity, out.angles.x, out.angles.z, fallback);
            return out;
        }
    }

    bool LaunchAt(RE::Actor* a_shooter, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target, std::string& a_detail)
    {
        if (!a_shooter || !a_spell || !a_target || !a_shooter->GetParentCell()) {
            a_detail = "no shooter (in a cell), spell or target";
            return false;
        }
        // The LaunchData spell constructor dereferences these without a check (Projectile.cpp:213-222)
        const auto* avEffect = a_spell->GetAVEffect();
        auto* projectileBase = avEffect ? avEffect->data.projectileBase : nullptr;
        if (!projectileBase || !a_spell->GetCostliestEffectItem()) {
            a_detail = "the spell has no projectile";
            return false;
        }
        const auto aim = AimAt(a_shooter, a_target, *projectileBase);
        RE::Projectile::LaunchData data(a_shooter, aim.origin, aim.angles, a_spell);
        data.castingSource = RE::MagicSystem::CastingSource::kRightHand;
        data.desiredTarget = a_target;
        RE::ProjectileHandle handle;
        RE::Projectile::Launch(&handle, data);
        const bool launched = static_cast<bool>(handle);
        a_detail = std::format("{} {}", aim.text, launched ? "projectile handle returned" : "NO projectile handle");
        return launched;
    }

    bool LaunchAmmoAt(RE::Actor* a_shooter, RE::TESAmmo* a_ammo, RE::TESObjectWEAP* a_weapon, RE::TESObjectREFR* a_target,
        std::string& a_detail, RE::ObjectRefHandle* a_arrow)
    {
        auto* projectileBase = a_ammo ? a_ammo->GetRuntimeData().data.projectile : nullptr;
        if (!a_shooter || !a_weapon || !projectileBase || !a_target || !a_shooter->GetParentCell()) {
            a_detail = "no shooter (in a cell), ammo with a projectile, weapon or target";
            return false;
        }
        const auto aim = AimAt(a_shooter, a_target, *projectileBase);
        // The ammo constructor (Projectile.cpp:224): projectileBase = the ammo's PROJ, weaponSource, ammoSource
        RE::Projectile::LaunchData data(a_shooter, aim.origin, aim.angles, a_ammo, a_weapon);
        data.desiredTarget = a_target;
        RE::ProjectileHandle handle;
        RE::Projectile::Launch(&handle, data);
        const bool launched = static_cast<bool>(handle);
        if (a_arrow) {
            if (const auto projectile = handle.get()) {
                *a_arrow = projectile->GetHandle();
            }
        }
        a_detail = std::format("{:08X} shot ammo {:08X} (PROJ {:08X}) with weapon {:08X}, {} {}", a_shooter->GetFormID(),
            a_ammo->GetFormID(), projectileBase->GetFormID(), a_weapon->GetFormID(), aim.text,
            launched ? "projectile handle returned" : "NO projectile handle");
        return launched;
    }

    RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_base, float a_forward, float a_right)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_base) {
            return {};
        }
        const auto ref = player->PlaceObjectAtMe(a_base, false);
        if (!ref) {
            return {};
        }
        // Heading: forward is (sin z, cos z), right is (cos z, -sin z)
        const float heading = player->GetAngleZ();
        auto pos = player->GetPosition();
        pos.x += std::sin(heading) * a_forward + std::cos(heading) * a_right;
        pos.y += std::cos(heading) * a_forward - std::sin(heading) * a_right;
        ref->SetPosition(pos);
        return ref->GetHandle();
    }

    V2Forms LookupV2(std::string& a_detail)
    {
        V2Forms out;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            a_detail = "no TESDataHandler";
            return out;
        }
        out.sparks = dh->LookupForm<RE::SpellItem>(kSparksLocalId, kSkyrimMaster);
        out.fireRune = dh->LookupForm<RE::SpellItem>(kFireRuneLocalId, kSkyrimMaster);
        out.marker = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarkerFormId);
        for (const auto id : { kFlamesLocalId, kAtronachFlamesLocalId }) {
            auto* spell = dh->LookupForm<RE::SpellItem>(id, kSkyrimMaster);
            const auto* effect = spell ? spell->GetAVEffect() : nullptr;
            const auto* projectile = effect ? effect->data.projectileBase : nullptr;
            if (projectile && projectile->IsFlamethrower()) {
                out.flames = spell;
                break;
            }
        }
        a_detail = std::format("Sparks {:08X}, Flames-type spell {:08X}, Fire Rune {:08X}, XMarker {:08X}",
            out.sparks ? out.sparks->GetFormID() : 0, out.flames ? out.flames->GetFormID() : 0,
            out.fireRune ? out.fireRune->GetFormID() : 0, out.marker ? out.marker->GetFormID() : 0);
        return out;
    }

    std::string MakeTarget(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return "no actor";
        }
        a_actor->EnableAI(false);
        if (auto* values = a_actor->AsActorValueOwner()) {
            values->SetBaseActorValue(RE::ActorValue::kHealth, kDummyHealth);
        }
        return std::format("AI {}, health base {} (now {:.0f})", a_actor->IsAIEnabled() ? "still ON" : "off",
            kDummyHealth, HealthOf(a_actor));
    }

    float HealthOf(RE::Actor* a_actor)
    {
        auto* values = a_actor ? a_actor->AsActorValueOwner() : nullptr;
        return values ? values->GetActorValue(RE::ActorValue::kHealth) : 0.0f;
    }

    int StokeEffectCount()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* keyword = GetForms().stokedKeyword;
        auto* target = player ? player->AsMagicTarget() : nullptr;
        auto* effects = target ? target->GetActiveEffectList() : nullptr;
        if (!effects || !keyword) {
            return 0;
        }
        int count = 0;
        for (const auto* effect : *effects) {
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            if (base && base->HasKeyword(keyword) &&
                !effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                ++count;
            }
        }
        return count;
    }

    std::vector<ShaderOn> ShadersOn(RE::ObjectRefHandle a_target)
    {
        std::vector<ShaderOn> out;
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists || !a_target) {
            return out;
        }
        lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
            if (a_effect && a_effect->target == a_target) {
                out.push_back({ a_effect->effectData ? a_effect->effectData->GetFormID() : 0, a_effect->finished,
                    a_effect->age, reinterpret_cast<std::uintptr_t>(a_effect) });
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return out;
    }

    void FaceTarget(RE::Actor* a_actor, const RE::TESObjectREFR* a_target)
    {
        if (!a_actor || !a_target) {
            return;
        }
        const auto from = a_actor->GetPosition();
        const auto to = a_target->GetPosition();
        a_actor->SetHeading(std::atan2(to.x - from.x, to.y - from.y));
    }

    std::string Remove(RE::ObjectRefHandle a_ref)
    {
        const auto ref = a_ref.get();
        if (!ref) {
            return "already gone";
        }
        const auto id = ref->GetFormID();
        ref->Disable();       // RE/T/TESObjectREFR.h:330
        ref->SetDelete(true);  // RE/T/TESObjectREFR.h:218
        return std::format("{:08X} disabled and marked deleted", id);
    }
}
