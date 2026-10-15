#include "BurnShader.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"

#include <algorithm>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        // Attempt 1 runs at the next task pump after the apply event. The ActiveEffect is
        // in the target's list by then, but its hitEffects may still be null or empty (the
        // controller can create the hit shader on a later update): replace mode then tries
        // once more at the next ticker tick (~100 ms) and applies the grey shader either way.
        constexpr int kFinalAttempt = 2;
        // ApplyEffectShader's -1 means "until stopped": an effect without a duration
        // (an instant hit) gets this many seconds of grey burn instead
        constexpr float kInstantBurnSeconds = 1.5f;
        constexpr float kMinBurnSeconds = 0.5f;

        struct PendingSwap
        {
            RE::ObjectRefHandle target;
            std::uint16_t activeEffectId = 0;
        };
        std::vector<PendingSwap> g_pending;  // game thread only

        enum class HitList
        {
            kNull,
            kEmpty,
            kNoMatch,
            kStopped
        };

        const char* HitListName(HitList a_state)
        {
            switch (a_state) {
            case HitList::kNull:
                return "hitEffects null";
            case HitList::kEmpty:
                return "hitEffects empty";
            case HitList::kNoMatch:
                return "no hit shader of that MGEF in hitEffects";
            default:
                return "engine hit shader stopped";
            }
        }

        RE::ActiveEffect* FindActiveEffect(RE::TESObjectREFR* a_target, std::uint16_t a_id)
        {
            auto* magicTarget = a_target->GetMagicTarget();
            auto* effects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
            if (!effects) {
                return nullptr;
            }
            for (auto* effect : *effects) {
                if (effect && effect->usUniqueID == a_id) {
                    return effect;
                }
            }
            return nullptr;
        }

        HitList FinishEngineShaders(RE::ActiveEffect* a_effect, const RE::TESEffectShader* a_engineShader, int& a_stopped)
        {
            a_stopped = 0;
            if (!a_effect->hitEffects) {
                return HitList::kNull;
            }
            if (a_effect->hitEffects->empty()) {
                return HitList::kEmpty;
            }
            for (auto* hit : *a_effect->hitEffects) {
                auto* shader = hit ? hit->As<RE::ShaderReferenceEffect>() : nullptr;
                if (shader && shader->effectData == a_engineShader && !shader->finished) {
                    shader->finished = true;
                    ++a_stopped;
                }
            }
            return a_stopped > 0 ? HitList::kStopped : HitList::kNoMatch;
        }

        // ProcessLists holds its spin lock while it calls back: only compare inside, apply after
        bool HasLiveGreyShader(RE::ObjectRefHandle a_target, const RE::TESEffectShader* a_ours)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return false;
            }
            bool found = false;
            lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
                if (a_effect && a_effect->effectData == a_ours && !a_effect->finished && a_effect->target == a_target) {
                    found = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return found;
        }

        float GreyDuration(const RE::ActiveEffect* a_effect)
        {
            if (a_effect->duration <= 0.0f) {
                return kInstantBurnSeconds;
            }
            return (std::max)(a_effect->duration - a_effect->elapsedSeconds, kMinBurnSeconds);
        }

        void SwapBurnShader(RE::ObjectRefHandle a_target, std::uint16_t a_id, int a_attempt)
        {
            const auto& config = GetConfig();
            if (!IsStoked() || !config.burnShader) {
                return;
            }
            const auto targetRef = a_target.get();
            auto* ours = GetForms().burnShader;
            if (!targetRef || !ours) {
                return;
            }
            auto* effect = FindActiveEffect(targetRef.get(), a_id);
            if (!effect || !IsFireSpell(effect->spell)) {
                return;
            }
            const auto* base = effect->GetBaseObject();
            const auto* engineShader = base ? base->data.effectShader : nullptr;
            if (!engineShader || engineShader == ours) {
                return;  // this effect has no hit shader to swap
            }

            int stopped = 0;
            const char* listState = "original left (overlay)";
            if (config.burnMode == BurnMode::kReplace) {
                const HitList state = FinishEngineShaders(effect, engineShader, stopped);
                if (state != HitList::kStopped && a_attempt < kFinalAttempt) {
                    logger::info("BlackFlame: burn shader: spell {:08X} on {:08X}: {} at attempt {}, trying again "
                                 "at the next tick", effect->spell->GetFormID(), targetRef->GetFormID(),
                        HitListName(state), a_attempt);
                    g_pending.push_back({ a_target, a_id });
                    return;
                }
                listState = HitListName(state);
            }

            const auto spellId = effect->spell->GetFormID();
            const char* mode = BurnModeName(config.burnMode);
            if (HasLiveGreyShader(a_target, ours)) {
                logger::info("BlackFlame: burn shader [{}]: spell {:08X} on {:08X}: {} ({} stopped, attempt {}), grey "
                             "shader already live (not stacked)",
                    mode, spellId, targetRef->GetFormID(), listState, stopped, a_attempt);
                return;
            }
            const float seconds = GreyDuration(effect);
            const bool applied = targetRef->ApplyEffectShader(ours, seconds) != nullptr;
            logger::info("BlackFlame: burn shader [{}]: spell {:08X} on {:08X}: hit shader {:08X}: {} ({} stopped, "
                         "attempt {}), grey shader {:.1f} s {}",
                mode, spellId, targetRef->GetFormID(), engineShader->GetFormID(), listState, stopped, a_attempt, seconds,
                applied ? "applied" : "NOT applied (ApplyEffectShader returned null)");
        }
    }

    void QueueBurnShaderSwap(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId)
    {
        AddTaskToGameThread("BlackFlameBurn",
            [a_target, a_activeEffectId]() { SwapBurnShader(a_target, a_activeEffectId, 1); });
    }

    void RunPendingBurnSwaps()
    {
        if (g_pending.empty()) {
            return;
        }
        std::vector<PendingSwap> due;
        due.swap(g_pending);
        for (const auto& swap : due) {
            SwapBurnShader(swap.target, swap.activeEffectId, kFinalAttempt);
        }
    }
}
