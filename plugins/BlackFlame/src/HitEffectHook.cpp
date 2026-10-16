#include "HitEffectHook.h"
#include "TrackedMutex.h"

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

        // S3 (hit shader research, 2026-10-16): slot 0x0D (AE 34846 / SE 34049) is the engine's only read of
        // MGEF data.effectShader (or hitVisuals +0x28, dual cast +0x40); it returns null when the active
        // effect's flags (+0x7C) have bit 1 set (written only by the cloak callback). ReferenceEffectController::
        // Start (34761) and the ShaderReferenceEffect constructor (34910) call it. So the hook stays where it is.
        constexpr std::uint32_t kNoHitShaderBit = 1u << 1;
        constexpr int kMaxAnyLines = 20;     // lines per slot for casters other than the player
        constexpr int kMaxPlayerLines = 60;  // lines per slot for the player's effects, a cap of their own

        enum class Outcome
        {
            kSwapped,        // ours returned (shader) / remembered for the recolour (art)
            kNotPlayer,      // someone else cast it
            kEngineNull,     // the engine's answer was null
            kEngineNullFlag, // null with flags bit 1 set (the engine's "no hit shader" bit)
            kNotStoked,
            kNotFire,        // not a fire effect of a fire spell (IsBurnable)
            kNoBurnShader,
            kAlreadyOurs,
            kConfigOff       // burnShader false (shader slot) / hitArt false (art slot: no later recolour)
        };

        const char* OutcomeText(Outcome a_outcome)
        {
            switch (a_outcome) {
            case Outcome::kSwapped:
                return "SWAPPED (ours)";
            case Outcome::kNotPlayer:
                return "kept: not the player's";
            case Outcome::kEngineNull:
                return "kept: the engine answered null";
            case Outcome::kEngineNullFlag:
                return "kept: the engine answered null (flags bit 1)";
            case Outcome::kNotStoked:
                return "kept: not stoked";
            case Outcome::kNotFire:
                return "kept: not a fire effect of a fire spell";
            case Outcome::kNoBurnShader:
                return "kept: no burn shader form";
            case Outcome::kAlreadyOurs:
                return "kept: already ours";
            default:
                return "kept: the config key is off";
            }
        }

        struct SlotStats
        {
            std::atomic<std::uint64_t> playerCalls{ 0 }, swapped{ 0 }, engineNull{ 0 }, engineNullFlag{ 0 }, notStoked{ 0 },
                notFire{ 0 }, otherRejected{ 0 };
        };

        std::array<std::array<std::atomic<int>, kKindCount>, kSlotCount> g_diagnostics{};
        std::array<SlotStats, kSlotCount> g_stats;
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
            static TrackedMutex mutex;
            static auto* counts = std::make_unique<std::unordered_map<const Controller*, int>>().release();
            std::lock_guard lock(mutex);
            return ++(*counts)[a_this];
        }

        // The first kMaxAnyLines calls of a slot with something to show (the engine's answer or the MGEF's own
        // field not null) whose caster is not the player, and apart from them the first kMaxPlayerLines calls of
        // a slot whose caster is the player, whatever the outcome: the "any" lines can never use the player's up
        void Diagnose(Slot a_slot, Controller* a_this, const RE::TESForm* a_original, Outcome a_outcome)
        {
            auto* effect = a_this->effect;
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            const RE::TESForm* field = !base ? nullptr :
                                       a_slot == kShaderSlot ? static_cast<const RE::TESForm*>(base->data.effectShader) :
                                                               static_cast<const RE::TESForm*>(base->data.hitEffectArt);
            const auto playerHandle = PlayerHandleValue();
            const bool player = effect && playerHandle != 0 && effect->caster.native_handle() == playerHandle;
            if (!player && !a_original && !field) {
                return;
            }
            auto& counter = g_diagnostics[a_slot][player ? kPlayerEffect : kAnyEffect];
            const int cap = player ? kMaxPlayerLines : kMaxAnyLines;
            if (counter.load() >= cap) {
                return;
            }
            const int line = counter.fetch_add(1) + 1;
            if (line > cap) {
                return;
            }
            const auto target = a_this->target.get();
            const auto* ours = GetForms().burnShader;
            const auto flags = effect ? effect->flags.underlying() : 0u;
            logger::info("BlackFlame: {} [{} {}]: controller 0x{:X} (call {} for it), effect at 0x{:X} flags 0x{:08X} (bit1 {}), "
                         "MGEF {:08X} (its field {:08X}) spell {:08X}, target {:08X}, caster {}, stoked {}, burnShader {:08X} "
                         "at 0x{:X}, engine answer {:08X} -> {}, callerThread={}",
                kSlotNames[a_slot], player ? "player" : "any", line, reinterpret_cast<std::uintptr_t>(a_this),
                CallNumber(a_this), reinterpret_cast<std::uintptr_t>(effect), flags, (flags & kNoHitShaderBit) ? "set" : "clear",
                base ? base->GetFormID() : 0, field ? field->GetFormID() : 0,
                effect && effect->spell ? effect->spell->GetFormID() : 0, target ? target->GetFormID() : 0,
                player ? "player" : "other", IsStoked() ? "yes" : "no", ours ? ours->GetFormID() : 0,
                reinterpret_cast<std::uintptr_t>(ours), a_original ? a_original->GetFormID() : 0, OutcomeText(a_outcome),
                REX::W32::GetCurrentThreadId());
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
                reinterpret_cast<std::uintptr_t>(effect), a_swapped });
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

        // Why a call was or was not swapped (cheap checks first: every hit shader and art of every actor passes here)
        Outcome Decide(Slot a_slot, Controller* a_this, const RE::TESForm* a_original)
        {
            auto* effect = a_this->effect;
            const auto playerHandle = PlayerHandleValue();
            // No player handle yet (or none for the effect): never the player's - a caster-less effect would match 0
            if (!effect || playerHandle == 0 || effect->caster.native_handle() != playerHandle) {
                return Outcome::kNotPlayer;
            }
            if (!a_original) {
                return a_slot == kShaderSlot && (effect->flags.underlying() & kNoHitShaderBit) ? Outcome::kEngineNullFlag :
                                                                                               Outcome::kEngineNull;
            }
            if (!IsStoked()) {
                return Outcome::kNotStoked;
            }
            if (!IsBurnable(effect->GetBaseObject(), effect->spell)) {
                return Outcome::kNotFire;
            }
            if (a_slot == kArtSlot) {
                return GetConfig().hitArt ? Outcome::kSwapped : Outcome::kConfigOff;
            }
            const auto* ours = GetForms().burnShader;
            if (!ours) {
                return Outcome::kNoBurnShader;
            }
            if (a_original == ours) {
                return Outcome::kAlreadyOurs;
            }
            return GetConfig().burnShader ? Outcome::kSwapped : Outcome::kConfigOff;
        }

        void Count(Slot a_slot, Outcome a_outcome)
        {
            if (a_outcome == Outcome::kNotPlayer) {
                return;
            }
            auto& stats = g_stats[a_slot];
            stats.playerCalls.fetch_add(1);
            switch (a_outcome) {
            case Outcome::kSwapped:
                stats.swapped.fetch_add(1);
                break;
            case Outcome::kEngineNullFlag:
                stats.engineNullFlag.fetch_add(1);
                stats.engineNull.fetch_add(1);
                break;
            case Outcome::kEngineNull:
                stats.engineNull.fetch_add(1);
                break;
            case Outcome::kNotStoked:
                stats.notStoked.fetch_add(1);
                break;
            case Outcome::kNotFire:
                stats.notFire.fetch_add(1);
                break;
            default:
                stats.otherRejected.fetch_add(1);
                break;
            }
        }

        RE::TESEffectShader* ChooseShader(Controller* a_this, RE::TESEffectShader* a_original)
        {
            const Outcome outcome = Decide(kShaderSlot, a_this, a_original);
            Count(kShaderSlot, outcome);
            Diagnose(kShaderSlot, a_this, a_original, outcome);
            if (outcome != Outcome::kSwapped) {
                if (a_original && SelfTestProbe::Recording() && a_this->effect &&
                    IsBurnable(a_this->effect->GetBaseObject(), a_this->effect->spell)) {
                    Record(a_this, a_original, false);
                }
                return a_original;
            }
            LogSwap(a_this, a_original);
            return GetForms().burnShader;
        }

        void OnHitArt(Controller* a_this, RE::BGSArtObject* a_art)
        {
            const Outcome outcome = Decide(kArtSlot, a_this, a_art);
            Count(kArtSlot, outcome);
            Diagnose(kArtSlot, a_this, a_art, outcome);
            if (outcome == Outcome::kSwapped) {
                RememberHitArt(a_this, a_art);  // config hitArt: recolour once its 3D exists (HitArt.h)
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

    HitSlotStats GetHitSlotStats(bool a_shader) noexcept
    {
        const auto& stats = g_stats[a_shader ? kShaderSlot : kArtSlot];
        return { stats.playerCalls.load(), stats.swapped.load(), stats.engineNull.load(), stats.engineNullFlag.load(),
            stats.notStoked.load(), stats.notFire.load(), stats.otherRejected.load() };
    }

    void InstallHitEffectHooks()
    {
        // Both slots are hooked whatever the config says (S3: the diagnostics tell whether the engine asks, with
        // what, and why a call was kept); burnShader decides whether the shader call is swapped, hitArt only whether the
        // returned art's 3D is recoloured later (the art call always returns the engine's art)
        const auto& config = GetConfig();
        REL::Relocation<std::uintptr_t> vtbl{ Controller::VTABLE[0] };
        HitEffectShaderHook::func = vtbl.write_vfunc(kHitEffectShaderIndex, HitEffectShaderHook::thunk);
        HitEffectArtHook::func = vtbl.write_vfunc(kHitEffectArtIndex, HitEffectArtHook::thunk);
        logger::info("BlackFlame: hooked the ActiveEffectReferenceEffectController vtable: GetHitEffectShader (0x{:X}) swap {}, "
                     "GetHitEffectArt (0x{:X}) recolour {}", kHitEffectShaderIndex, config.burnShader ? "on" : "off (burnShader false)",
            kHitEffectArtIndex, config.hitArt ? "on" : "off (hitArt false)");
    }
}
