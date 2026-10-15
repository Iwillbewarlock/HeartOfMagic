#include "BurnShader.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"

#include <algorithm>

namespace BlackFlame
{
    namespace
    {
        // The hit shader may not be in ActiveEffect::hitEffects yet when the apply
        // event arrives (its controller can create it on a later update): look again
        // on the next task pumps, at most this many times in all
        constexpr int kMaxAttempts = 3;
        // ApplyEffectShader's -1 means "until stopped": an effect without a duration
        // (an instant hit) gets this many seconds of grey burn instead
        constexpr float kInstantBurnSeconds = 1.5f;
        constexpr float kMinBurnSeconds = 0.5f;

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

        int FinishEngineShaders(RE::ActiveEffect* a_effect, const RE::TESEffectShader* a_engineShader)
        {
            if (!a_effect->hitEffects) {
                return 0;
            }
            int stopped = 0;
            for (auto* hit : *a_effect->hitEffects) {
                auto* shader = hit ? hit->As<RE::ShaderReferenceEffect>() : nullptr;
                if (shader && shader->effectData == a_engineShader && !shader->finished) {
                    shader->finished = true;
                    ++stopped;
                }
            }
            return stopped;
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
            if (!IsStoked() || !GetConfig().burnShader) {
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

            const int stopped = FinishEngineShaders(effect, engineShader);
            if (stopped == 0 && a_attempt < kMaxAttempts) {
                AddTaskToGameThread("BlackFlameBurnRetry",
                    [a_target, a_id, a_attempt]() { SwapBurnShader(a_target, a_id, a_attempt + 1); });
                return;
            }

            const auto spellId = effect->spell ? effect->spell->GetFormID() : 0;
            if (HasLiveGreyShader(a_target, ours)) {
                logger::info("BlackFlame: burn shader: spell {:08X} on {:08X}: {} engine shader(s) {:08X} stopped, "
                             "grey shader already live (not stacked)",
                    spellId, targetRef->GetFormID(), stopped, engineShader->GetFormID());
                return;
            }
            const float seconds = GreyDuration(effect);
            const bool applied = targetRef->ApplyEffectShader(ours, seconds) != nullptr;
            logger::info("BlackFlame: burn shader: spell {:08X} on {:08X}: {} engine shader(s) {:08X} stopped "
                         "(attempt {}), grey shader {:.1f} s {}",
                spellId, targetRef->GetFormID(), stopped, engineShader->GetFormID(), a_attempt, seconds,
                applied ? "applied" : "NOT applied (ApplyEffectShader returned null)");
        }
    }

    void QueueBurnShaderSwap(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId)
    {
        AddTaskToGameThread("BlackFlameBurn",
            [a_target, a_activeEffectId]() { SwapBurnShader(a_target, a_activeEffectId, 1); });
    }
}
