#include "PlayerArt.h"

#include "CasterArt.h"
#include "Config.h"
#include "FireSet.h"
#include "HitEffectHook.h"
#include "LogLimit.h"
#include "Recolor.h"
#include "SehGuard.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        constexpr std::array kFirstPersonViews{ false, true };  // 3rd person biped, then 1st person

        bool IsRunning(const RE::ActiveEffect* a_effect)
        {
            return !a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive);
        }

        template <class Visit>
        void ForEachPlayerEffect(Visit&& a_visit)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* target = player ? player->AsMagicTarget() : nullptr;
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!effects) {
                return;
            }
            for (auto* effect : *effects) {
                if (effect && IsRunning(effect)) {
                    a_visit(effect);
                }
            }
        }

        // The player's bound fire weapons, refreshed by the ticker task (the only place the player's
        // effect list is read); the projectile hook only compares FormIDs under the lock
        std::mutex g_boundMutex;

        std::vector<RE::FormID>& BoundFire()
        {
            static auto* bound = std::make_unique<std::vector<RE::FormID>>().release();
            return *bound;
        }

        bool HasFireEnchantment(const RE::TESObjectWEAP* a_weapon)
        {
            const auto* enchantment = a_weapon->formEnchanting;
            if (!enchantment) {
                return false;
            }
            for (const auto* effect : enchantment->effects) {
                if (effect && IsFireMgefAny(effect->baseEffect)) {
                    return true;
                }
            }
            return false;
        }

        // A bound weapon named by an active Bound Weapon effect the player cast on themselves, when that
        // effect is a fire effect of a fire spell or the weapon's own enchantment carries a fire MGEF
        void RefreshBoundFire(RE::PlayerCharacter* a_player)
        {
            std::vector<RE::FormID> found;
            ForEachPlayerEffect([&](RE::ActiveEffect* a_effect) {
                const auto* base = a_effect->GetBaseObject();
                if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kBoundWeapon)) {
                    return;
                }
                const auto* weapon = base->data.associatedForm ? base->data.associatedForm->As<RE::TESObjectWEAP>() : nullptr;
                if (!weapon || !weapon->IsBound() || a_effect->GetCasterActor().get() != a_player) {
                    return;
                }
                if (IsPlayerFireEffect(a_effect) || HasFireEnchantment(weapon)) {
                    found.push_back(weapon->GetFormID());
                }
            });
            std::lock_guard lock(g_boundMutex);
            BoundFire().swap(found);
        }

        bool IsPlayerBoundFireWeapon(const RE::TESForm* a_weapon)
        {
            if (!a_weapon) {
                return false;
            }
            std::lock_guard lock(g_boundMutex);
            return std::ranges::find(BoundFire(), a_weapon->GetFormID()) != BoundFire().end();
        }

        void RecolorBoundWeapons(RE::PlayerCharacter* a_player)
        {
            for (const bool firstPerson : kFirstPersonViews) {
                const auto& biped = a_player->GetBiped(firstPerson);
                if (!biped) {
                    continue;
                }
                for (auto& object : biped->objects) {
                    if (!object.partClone || !IsPlayerBoundFireWeapon(object.item)) {
                        continue;
                    }
                    const auto stats = RecolorTree(object.partClone.get(), HandArtObserver());
                    int suppressed = 0;
                    if (stats.Recolored() > 0 && LogLimit::Allow("bound weapon", object.item->GetFormID(), suppressed)) {
                        logger::info("BlackFlame: bound weapon {:08X} ({} person): {}{}", object.item->GetFormID(),
                            firstPerson ? "1st" : "3rd", stats.Describe(),
                            suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
                    }
                }
            }
        }

        void TintLightEffects()
        {
            ForEachPlayerEffect([](RE::ActiveEffect* a_effect) {
                auto* lightEffect = skyrim_cast<RE::LightEffect*>(a_effect);
                if (!lightEffect || !IsPlayerFireEffect(a_effect)) {
                    return;
                }
                const bool tinted = TintRememberedLight(CheckedLight(lightEffect->light.get(), "light effect"));
                const auto* base = a_effect->GetBaseObject();
                int suppressed = 0;
                if (base && LogLimit::Allow("light effect", base->GetFormID(), suppressed)) {
                    logger::info("BlackFlame: light effect {:08X}: light {}{}", base->GetFormID(),
                        tinted ? "tinted" : "none or not an NiLight",
                        suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
                }
            });
        }
    }

    bool IsBoundFireArrow(RE::Projectile* a_projectile)
    {
        return IsPlayerBoundFireWeapon(a_projectile->GetProjectileRuntimeData().weaponSource);
    }

    void TickPlayerArt()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        (void)Guarded("bound weapon list", [&]() { RefreshBoundFire(player); });
        if (GetConfig().boundWeapon) {
            (void)Guarded("bound weapon recolour", [&]() { RecolorBoundWeapons(player); });
        }
        if (GetConfig().light) {
            (void)Guarded("light effect tint", [&]() { TintLightEffects(); });
        }
    }
}
