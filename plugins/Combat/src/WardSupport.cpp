#include "Common.h"
#include "WardSupport.h"
#include "CombatConfig.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace SLCombat
{
    namespace
    {
        constexpr const char* kSphereWardPlugin = "360 Ward.esp";
        constexpr float kMsPerSecond = 1000.0f;

        using Clock = std::chrono::steady_clock;

        bool s_sphereWard = false;
        RE::BGSPerk* s_dualCastPerk = nullptr;

        // Vanilla dual casting: magnitude and cost multipliers (game settings)
        constexpr const char* kDualEffectSetting = "fMagicDualCastingEffectivenessBase";
        constexpr const char* kDualCostSetting = "fMagicDualCastingCostMult";

        float GameSettingFloat(const char* a_name, float a_fallback)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(a_name) : nullptr;
            return setting ? setting->GetFloat() : a_fallback;
        }

        /// "Plugin.esp|0xLocalID" -> the perk, or null.
        RE::BGSPerk* LookupPerk(const std::string& a_id)
        {
            auto bar = a_id.find('|');
            auto* data = RE::TESDataHandler::GetSingleton();
            if (bar == std::string::npos || !data) {
                return nullptr;
            }
            try {
                auto local = static_cast<RE::FormID>(std::stoul(a_id.substr(bar + 1), nullptr, 16));
                return data->LookupForm<RE::BGSPerk>(local, a_id.substr(0, bar));
            } catch (const std::exception&) {
                return nullptr;
            }
        }

        // Held ward state: touched only from the input sink and game-thread tasks
        std::atomic<RE::SpellItem*> s_holdWard{ nullptr };
        Clock::time_point s_lastRefresh{};
        bool s_loopRunning = false;                       ///< game thread
        std::atomic<std::int64_t> s_lingerUntil{ 0 };     ///< Clock ticks; set from the collision hook

        float WardMagnitude(const RE::SpellItem* a_spell)
        {
            float best = 0.0f;
            for (const auto* effect : a_spell->effects) {
                if (effect && effect->baseEffect &&
                    effect->baseEffect->data.primaryAV == RE::ActorValue::kWardPower) {
                    best = std::max(best, effect->effectItem.magnitude);
                }
            }
            return best;
        }

        bool SpellInHand(RE::PlayerCharacter* a_player, bool a_leftHand)
        {
            auto* form = a_player->GetEquippedObject(a_leftHand);
            return form && form->Is(RE::FormType::Spell);
        }
    }

    void WardSupport::Detect()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        // 360 Ward.esp ships ESL-flagged: a light file is not in the full-plugin list
        s_sphereWard = data && (data->LookupLoadedModByName(kSphereWardPlugin) != nullptr
                                || data->LookupLoadedLightModByName(kSphereWardPlugin) != nullptr);
        logger::info("SL_Combat: {} {}", kSphereWardPlugin, s_sphereWard ? "loaded - wards cover every direction" : "not loaded");

        const auto& perkId = CombatConfig::Get().dualCastPerk;
        s_dualCastPerk = LookupPerk(perkId);
        if (s_dualCastPerk) {
            logger::info("SL_Combat: dual-cast perk {} = '{}'", perkId, s_dualCastPerk->GetName());
        } else {
            logger::warn("SL_Combat: dual-cast perk {} not found - the counter's ward is never dual-cast", perkId);
        }
    }

    std::string WardSupport::DualCastPerkName()
    {
        return s_dualCastPerk ? std::string(s_dualCastPerk->GetName()) : std::string("not found (") + CombatConfig::Get().dualCastPerk + ")";
    }

    bool WardSupport::CanDualCast(RE::PlayerCharacter* a_player)
    {
        return a_player && s_dualCastPerk && SpellInHand(a_player, true) &&
               SpellInHand(a_player, false) && a_player->HasPerk(s_dualCastPerk);
    }

    bool WardSupport::AllAngles()
    {
        return s_sphereWard && CombatConfig::Get().sphereWardAllAngles;
    }

    RE::SpellItem* WardSupport::BestKnownWard(RE::PlayerCharacter* a_player)
    {
        if (!a_player) {
            return nullptr;
        }
        RE::SpellItem* best = nullptr;
        float bestMagnitude = 0.0f;
        for (auto* spell : a_player->GetActorRuntimeData().addedSpells) {
            if (!spell) {
                continue;
            }
            float magnitude = WardMagnitude(spell);
            if (magnitude > bestMagnitude) {
                best = spell;
                bestMagnitude = magnitude;
            }
        }
        return best;
    }

    void WardSupport::CastWard(RE::PlayerCharacter* a_player, RE::SpellItem* a_ward, bool a_dual)
    {
        float effectiveness = a_dual ? GameSettingFloat(kDualEffectSetting, 1.0f) : 1.0f;
        if (auto* caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
            caster->CastSpellImmediate(a_ward, false, a_player, effectiveness, false, 0.0f, a_player);
        }
    }

    float WardSupport::RaiseWard(RE::PlayerCharacter* a_player)
    {
        auto* ward = BestKnownWard(a_player);
        if (!ward) {
            return 0.0f;
        }
        bool dual = CanDualCast(a_player);
        CastWard(a_player, ward, dual);
        logger::info("counter.ward raise ward={:08X} dual={}", ward->GetFormID(), dual);

        // The ward stays up a moment even when the key is let go (free)
        auto linger = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(CombatConfig::Get().wardLingerSeconds));
        s_lingerUntil.store((Clock::now() + linger).time_since_epoch().count());
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { EnsureLoop(); });
        }
        return a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWardPower);
    }

    // =========================================================================
    // Linger and held ward (game thread): one loop every holdRefreshSeconds
    //   held key       -> the ward again, magicka charged
    //   within linger  -> the ward again, free
    //   neither        -> the loop ends
    // =========================================================================

    void WardSupport::StartHold()
    {
        const auto& config = CombatConfig::Get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!config.holdWard || !player) {
            return;
        }
        auto* ward = BestKnownWard(player);
        if (!ward) {
            return;
        }
        // Stop a hand's own charge (a firebolt would otherwise fire on release)
        player->InterruptCast(false);
        s_holdWard.store(ward);
        s_lastRefresh = Clock::now();
        logger::info("counter.ward hold start ward={:08X} '{}'", ward->GetFormID(), ward->GetName());
        EnsureLoop();
    }

    void WardSupport::EnsureLoop()
    {
        if (s_loopRunning) {
            return;
        }
        s_loopRunning = true;
        ScheduleTick();
    }

    void WardSupport::ScheduleTick()
    {
        auto delay = std::chrono::milliseconds(static_cast<int>(CombatConfig::Get().holdRefreshSeconds * kMsPerSecond));
        std::thread([delay]() {
            std::this_thread::sleep_for(delay);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { Tick(); });
            }
        }).detach();
    }

    void WardSupport::Tick()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            s_loopRunning = false;
            return;
        }
        if (s_holdWard.load() && !CounterInput::GetSingleton()->IsHeld()) {
            StopHold("released");
        }
        if (s_holdWard.load()) {
            Refresh(player);  // may stop the hold when magicka runs out
        } else if (Clock::now().time_since_epoch().count() < s_lingerUntil.load()) {
            if (auto* ward = BestKnownWard(player)) {
                CastWard(player, ward, CanDualCast(player));
            }
        } else {
            s_loopRunning = false;
            logger::info("counter.ward down");
            FlushLog();
            return;
        }
        ScheduleTick();
    }

    void WardSupport::ReleaseHold()
    {
        if (s_holdWard.load()) {
            StopHold("released");
        }
    }

    void WardSupport::Refresh(RE::PlayerCharacter* a_player)
    {
        auto now = Clock::now();
        float elapsed = std::chrono::duration<float>(now - s_lastRefresh).count();
        s_lastRefresh = now;

        auto* ward = s_holdWard.load();
        if (!ward) {
            return;
        }
        // A concentration ward's cost is per second; dual-cast costs more and holds more
        bool dual = CanDualCast(a_player);
        float cost = ward->CalculateMagickaCost(a_player) * elapsed * (dual ? GameSettingFloat(kDualCostSetting, 1.0f) : 1.0f);
        auto* values = a_player->AsActorValueOwner();
        if (values->GetActorValue(RE::ActorValue::kMagicka) < cost) {
            StopHold("out of magicka");
            return;
        }
        values->DamageActorValue(RE::ActorValue::kMagicka, cost);
        CastWard(a_player, ward, dual);
        logger::info("counter.ward refresh dual={} cost={:.1f} wardPower={:.1f}", dual, cost,
            values->GetActorValue(RE::ActorValue::kWardPower));
    }

    void WardSupport::StopHold(const char* a_reason)
    {
        logger::info("counter.ward hold end ({})", a_reason);
        s_holdWard.store(nullptr);
        FlushLog();
    }
}
