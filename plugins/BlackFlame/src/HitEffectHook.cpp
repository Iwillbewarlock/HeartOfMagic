#include "HitEffectHook.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "HitArt.h"
#include "LogLimit.h"
#include "SehGuard.h"
#include "SelfTestProbe.h"

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        using Controller = RE::ActiveEffectReferenceEffectController;

        constexpr std::size_t kHitEffectArtIndex = 0x0C;     // RE/A/ActiveEffectReferenceEffectController.h
        constexpr std::size_t kHitEffectShaderIndex = 0x0D;  // same header
        constexpr int kMaxDiagnostics = 20;                  // lines per slot and kind (any effect, a stoked player's)

        enum Slot : std::size_t
        {
            kShaderSlot,
            kArtSlot,
            kSlotCount
        };

        enum Kind : std::size_t
        {
            kAnyEffect,
            kPlayerEffect,
            kKindCount
        };

        constexpr std::array<const char*, kSlotCount> kSlotNames{ "GetHitEffectShader", "GetHitEffectArt" };

        std::array<std::array<std::atomic<int>, kKindCount>, kSlotCount> g_diagnostics{};
        std::atomic<bool> g_failureReported{ false };

        void ReportFailure()
        {
            if (!g_failureReported.exchange(true)) {
                logger::error("BlackFlame: hit effect hook threw; further failures are not logged");
            }
        }

        // The player's reference handle, read once per loaded world (cleared by ResetHitEffectState)
        std::atomic<std::uint32_t> g_playerHandle{ 0 };

        std::uint32_t PlayerHandleValue()
        {
            auto value = g_playerHandle.load();
            if (value == 0) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    value = player->GetHandle().native_handle();
                    g_playerHandle.store(value);
                }
            }
            return value;
        }


        // How often the engine asks one controller: counted while the diagnostic lines last (bounded
        // by them), so the log shows whether a call comes once per effect or again and again
        int CallNumber(const Controller* a_this)
        {
            static std::mutex mutex;
            static auto* counts = std::make_unique<std::unordered_map<const Controller*, int>>().release();
            std::lock_guard lock(mutex);
            return ++(*counts)[a_this];
        }

        // The first kMaxDiagnostics calls of a slot on any effect, and on a stoked player's fire effect
        void Diagnose(Slot a_slot, Controller* a_this, const RE::TESForm* a_original, bool a_player, bool a_changed)
        {
            auto& counter = g_diagnostics[a_slot][a_player ? kPlayerEffect : kAnyEffect];
            if (counter.load() >= kMaxDiagnostics) {
                return;
            }
            const int line = counter.fetch_add(1) + 1;
            if (line > kMaxDiagnostics) {
                return;
            }
            auto* effect = a_this->effect;
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            const auto target = a_this->target.get();
            logger::info("BlackFlame: {} [{} {}]: controller 0x{:X} (call {} for it), effect {} MGEF {:08X} spell {:08X} "
                         "elapsed {:.2f} s, target {:08X}, caster {}, original {:08X} -> {}, thread {}",
                kSlotNames[a_slot], a_player ? "player" : "any", line, reinterpret_cast<std::uintptr_t>(a_this),
                CallNumber(a_this), effect ? effect->usUniqueID : 0, base ? base->GetFormID() : 0,
                effect && effect->spell ? effect->spell->GetFormID() : 0, effect ? effect->elapsedSeconds : 0.0f,
                target ? target->GetFormID() : 0,
                effect && effect->caster.native_handle() == PlayerHandleValue() ? "player" : "other",
                a_original ? a_original->GetFormID() : 0, a_changed ? "ours" : "kept", REX::W32::GetCurrentThreadId());
        }

        // The controller's target, or else the effect's own target (its MagicTarget's reference)
        RE::FormID TargetIdOf(Controller* a_this)
        {
            if (const auto target = a_this->target.get()) {
                return target->GetFormID();
            }
            auto* magicTarget = a_this->effect ? a_this->effect->target : nullptr;
            auto* ref = magicTarget ? magicTarget->GetTargetStatsObject() : nullptr;
            return ref ? ref->GetFormID() : 0;
        }

        // While the self-test records: every swap, and every fire effect left to the engine because
        // someone else cast it (the direct evidence for T5-npc)
        void Record(Controller* a_this, const RE::TESEffectShader* a_original, bool a_swapped)
        {
            auto* effect = a_this->effect;
            SelfTestProbe::RecordBurn({ 0, TargetIdOf(a_this), effect && effect->spell ? effect->spell->GetFormID() : 0,
                a_original->GetFormID(), REX::W32::GetCurrentThreadId(), reinterpret_cast<std::uintptr_t>(a_this),
                a_swapped });
        }

        void LogSwap(Controller* a_this, const RE::TESEffectShader* a_original)
        {
            auto* effect = a_this->effect;
            const auto spellId = effect && effect->spell ? effect->spell->GetFormID() : 0;
            const auto targetId = TargetIdOf(a_this);
            if (SelfTestProbe::Recording()) {
                Record(a_this, a_original, true);
            }
            int suppressed = 0;
            if (LogLimit::Allow("hit shader swap", spellId, suppressed)) {
                logger::info("BlackFlame: hit shader: spell {:08X} on {:08X}: engine shader {:08X} swapped for ours{}",
                    spellId, targetId, a_original->GetFormID(),
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        RE::TESEffectShader* ChooseShader(Controller* a_this, RE::TESEffectShader* a_original)
        {
            auto* ours = GetForms().burnShader;
            // Cheap checks first: every hit shader of every actor passes through here
            const bool player = a_original && ours && a_original != ours && IsStoked() && IsPlayerFireEffect(a_this->effect);
            Diagnose(kShaderSlot, a_this, a_original, player, player);
            if (!player) {
                if (a_original && SelfTestProbe::Recording() && a_this->effect &&
                    IsBurnable(a_this->effect->GetBaseObject(), a_this->effect->spell)) {
                    Record(a_this, a_original, false);
                }
                return a_original;
            }
            LogSwap(a_this, a_original);
            return ours;
        }

        void OnHitArt(Controller* a_this, RE::BGSArtObject* a_art)
        {
            const bool player = a_art && IsStoked() && IsPlayerFireEffect(a_this->effect);
            Diagnose(kArtSlot, a_this, a_art, player, false);
            if (player) {
                RememberHitArt(a_this, a_art);
            }
        }

        struct HitEffectShaderHook
        {
            static RE::TESEffectShader* thunk(Controller* a_this)
            {
                auto* original = func(a_this);
                auto* result = original;
                // Entered from the engine's vtable: nothing may throw out of here, and a fault in our
                // part returns the engine's shader
                try {
                    if (a_this) {
                        (void)Guarded("hit effect shader", [&]() { result = ChooseShader(a_this, original); });
                    }
                } catch (...) {
                    result = original;
                    ReportFailure();
                }
                return result;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct HitEffectArtHook
        {
            static RE::BGSArtObject* thunk(Controller* a_this)
            {
                auto* art = func(a_this);
                try {
                    if (a_this) {
                        (void)Guarded("hit effect art", [&]() { OnHitArt(a_this, art); });
                    }
                } catch (...) {
                    ReportFailure();
                }
                return art;
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    bool IsPlayerFireEffect(RE::ActiveEffect* a_effect)
    {
        const auto player = PlayerHandleValue();
        if (!a_effect || player == 0 || a_effect->caster.native_handle() != player) {
            return false;
        }
        return IsBurnable(a_effect->GetBaseObject(), a_effect->spell);
    }

    void ResetHitEffectState()
    {
        g_playerHandle.store(0);
    }

    void InstallHitEffectHooks()
    {
        const auto& config = GetConfig();
        if (!config.burnShader && !config.hitArt) {
            return;
        }
        REL::Relocation<std::uintptr_t> vtbl{ Controller::VTABLE[0] };
        if (config.burnShader) {
            HitEffectShaderHook::func = vtbl.write_vfunc(kHitEffectShaderIndex, HitEffectShaderHook::thunk);
        }
        if (config.hitArt) {
            HitEffectArtHook::func = vtbl.write_vfunc(kHitEffectArtIndex, HitEffectArtHook::thunk);
        }
        logger::info("BlackFlame: hooked the ActiveEffectReferenceEffectController vtable: GetHitEffectShader (0x{:X}) {}, "
                     "GetHitEffectArt (0x{:X}) {}", kHitEffectShaderIndex, config.burnShader ? "on" : "off (burnShader false)",
            kHitEffectArtIndex, config.hitArt ? "on" : "off (hitArt false)");
    }
}
