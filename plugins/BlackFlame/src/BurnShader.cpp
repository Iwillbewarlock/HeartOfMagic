#include "BurnShader.h"

#include "BuffState.h"
#include "Config.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "LogLimit.h"
#include "SelfTestProbe.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        // Attempt 1 runs at the next task pump after the apply event and applies the grey
        // shader at once. Replace mode also finishes the engine's hit shader from the effect's
        // hitEffects; when that list is not filled yet, the finishing (only) is tried once more
        // after at least one full ticker tick has passed since it was queued.
        constexpr int kFirstAttempt = 1;
        constexpr int kRetryAttempt = 2;
        constexpr std::uint64_t kRetryTicks = 1;  // full ticks between queue and retry
        // ApplyEffectShader's -1 means "until stopped": an effect without a duration
        // (an instant hit) gets this many seconds of grey burn instead
        constexpr float kInstantBurnSeconds = 1.5f;
        constexpr float kMinBurnSeconds = 0.5f;
        // A live burn's grey shader is kept this far ahead of its age on every tick while the
        // effect lasts (more than one 100 ms tick, so it never lapses between ticks)
        constexpr float kKeepAliveSeconds = 0.5f;

        struct PendingFinish
        {
            RE::ObjectRefHandle target;
            std::uint16_t activeEffectId = 0;
            std::uint64_t queuedTick = 0;
        };

        // A grey shader tied to an effect with a duration or a concentration cast
        struct LiveBurn
        {
            RE::ObjectRefHandle target;
            std::uint16_t activeEffectId = 0;
            RE::NiPointer<RE::ShaderReferenceEffect> shader;
        };

        // Game thread only. Heap-held and never freed: a static vector of NiPointers would
        // DecRef engine objects at DLL unload, after the engine is gone (intentional leak)
        std::vector<LiveBurn>& Live()
        {
            static auto* live = std::make_unique<std::vector<LiveBurn>>().release();
            return *live;
        }

        std::vector<PendingFinish>& Pending()
        {
            static auto* pending = std::make_unique<std::vector<PendingFinish>>().release();
            return *pending;
        }

        // An instant hit's claim on a grey shader: not finished before this age (seconds), even when
        // the duration effect that created it ends (raw pointers as keys only, never dereferenced
        // outside the ProcessLists lock; pruned when the shader is gone). Heap-held like the rest.
        std::unordered_map<RE::ShaderReferenceEffect*, float>& AgeFloors()
        {
            static auto* floors = std::make_unique<std::unordered_map<RE::ShaderReferenceEffect*, float>>().release();
            return *floors;
        }

        std::uint64_t g_tick = 0;  // game thread only
        std::atomic<bool> g_lifetimeLogged{ false };
        std::atomic<std::size_t> g_liveCount{ 0 };  // for the ticker thread: keep ticking fast while > 0
        std::atomic<int> g_sinkThreadLogs{ 0 };

        void UpdateLiveCount()
        {
            // Floors too: the ticker keeps running (and pruning them) while any exist
            g_liveCount.store(Live().size() + Pending().size() + AgeFloors().size());
        }

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

        bool IsConcentration(const RE::EffectSetting* a_base)
        {
            return a_base && a_base->data.castingType == RE::MagicSystem::CastingType::kConcentration;
        }

        // Still running: not dispelled or inactive, not past its duration (BuffState's rule) - except
        // a concentration effect, which the engine refreshes while the cast is held
        bool IsLive(const RE::ActiveEffect* a_effect)
        {
            if (!a_effect || a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                return false;
            }
            return IsConcentration(a_effect->GetBaseObject()) ||
                   !(a_effect->duration > 0.0f && a_effect->elapsedSeconds >= a_effect->duration);
        }

        // One pass over the ProcessLists' shader effects, under their lock like every writer here
        // (CommonLib's ProcessLists::StopAllMagicEffects writes there too): a_act on each match
        template <class Match, class Act>
        void ForEachShaderUnderLock(Match&& a_match, Act&& a_act)
        {
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
                    if (a_effect && a_match(a_effect)) {
                        a_act(a_effect);
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
        }

        // Sets `finished` on exactly these shaders
        void FinishUnderLock(const std::unordered_set<RE::ShaderReferenceEffect*>& a_shaders)
        {
            if (a_shaders.empty()) {
                return;
            }
            ForEachShaderUnderLock([&](RE::ShaderReferenceEffect* a_effect) { return a_shaders.contains(a_effect); },
                [](RE::ShaderReferenceEffect* a_effect) { a_effect->finished = true; });
        }

        // Our grey shaders: finished, unless an instant hit still claims one (its age floor) - then its
        // lifetime is cut to that floor, so it ends there instead of at its original lifetime. Returns
        // how many were held to their floor.
        int FinishGreyUnderLock(const std::vector<RE::NiPointer<RE::ShaderReferenceEffect>>& a_shaders)
        {
            if (a_shaders.empty()) {
                return 0;
            }
            auto& floors = AgeFloors();
            int held = 0;
            ForEachShaderUnderLock(
                [&](RE::ShaderReferenceEffect* a_effect) {
                    return std::ranges::any_of(a_shaders, [&](const auto& s) { return s.get() == a_effect; });
                },
                [&](RE::ShaderReferenceEffect* a_effect) {
                    const auto floor = floors.find(a_effect);
                    if (floor == floors.end() || a_effect->age >= floor->second) {
                        a_effect->finished = true;
                        return;
                    }
                    if (a_effect->lifetime >= 0.0f) {
                        a_effect->lifetime = (std::min)(a_effect->lifetime, floor->second);
                    }
                    ++held;
                });
            return held;
        }

        // Only this effect's own hit shaders: an ally's identical shader on the same target stays
        HitList FinishEngineShaders(RE::ActiveEffect* a_effect, const RE::TESEffectShader* a_engineShader, int& a_stopped,
            std::vector<SelfTestProbe::HitShader>& a_seen)
        {
            a_stopped = 0;
            if (!a_effect->hitEffects) {
                return HitList::kNull;
            }
            if (a_effect->hitEffects->empty()) {
                return HitList::kEmpty;
            }
            std::unordered_set<RE::ShaderReferenceEffect*> found;
            for (auto* hit : *a_effect->hitEffects) {
                auto* shader = hit ? hit->As<RE::ShaderReferenceEffect>() : nullptr;
                if (shader && shader->effectData == a_engineShader) {
                    a_seen.push_back({ reinterpret_cast<std::uintptr_t>(shader), shader->age });  // for the self-test
                    if (!shader->finished) {
                        found.insert(shader);
                    }
                }
            }
            FinishUnderLock(found);
            a_stopped = static_cast<int>(found.size());
            return a_stopped > 0 ? HitList::kStopped : HitList::kNoMatch;
        }

        void ReportBurn(SelfTestProbe::BurnRecord a_record)
        {
            if (SelfTestProbe::Recording()) {
                SelfTestProbe::RecordBurn(std::move(a_record));
            }
        }

        // One line per spell and target per second (LogLimit)
        void LogBurn(RE::FormID a_spell, RE::FormID a_target, const std::string& a_text)
        {
            int suppressed = 0;
            if (LogLimit::Allow(std::format("burn {:08X}", a_target), a_spell, suppressed)) {
                logger::info("BlackFlame: burn shader: {}{}", a_text,
                    suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
            }
        }

        float GreyDuration(const RE::ActiveEffect* a_effect)
        {
            if (a_effect->duration <= 0.0f) {
                return kInstantBurnSeconds;
            }
            return (std::max)(a_effect->duration - a_effect->elapsedSeconds, kMinBurnSeconds);
        }

        // Lifetime -1 is "until stopped": never shortened or extended
        void Extend(RE::ShaderReferenceEffect* a_shader, float a_seconds)
        {
            if (a_shader->lifetime >= 0.0f) {
                a_shader->lifetime = (std::max)(a_shader->lifetime, a_shader->age + a_seconds);
            }
        }

        // A live grey shader on the target is extended (under the ProcessLists lock); none: a new
        // one is applied.
        RE::NiPointer<RE::ShaderReferenceEffect> ApplyOrExtendGrey(RE::TESObjectREFR* a_target,
            RE::ObjectRefHandle a_handle, RE::TESEffectShader* a_ours, float a_seconds, bool& a_extended)
        {
            RE::NiPointer<RE::ShaderReferenceEffect> live;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
                    if (a_effect && a_effect->effectData == a_ours && !a_effect->finished && a_effect->target == a_handle) {
                        Extend(a_effect, a_seconds);
                        live.reset(a_effect);
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            a_extended = live != nullptr;
            if (live) {
                return live;
            }
            live.reset(a_target->ApplyEffectShader(a_ours, a_seconds));
            if (live && !g_lifetimeLogged.exchange(true)) {
                logger::info("BlackFlame: ApplyEffectShader({:.2f} s) returned an effect with lifetime {:.2f}, age {:.2f}",
                    a_seconds, live->lifetime, live->age);
            }
            return live;
        }

        bool PlayerHasOtherFireEffect(RE::TESObjectREFR* a_target, std::uint16_t a_exceptId)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* magicTarget = a_target->GetMagicTarget();
            auto* effects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
            if (!effects || !player) {
                return false;
            }
            for (auto* effect : *effects) {
                if (!effect || effect->usUniqueID == a_exceptId || !IsLive(effect)) {
                    continue;
                }
                if (IsBurnable(effect->GetBaseObject(), effect->spell) && effect->GetCasterActor().get() == player) {
                    return true;
                }
            }
            return false;
        }

        // What the apply event's effect was, read in the event sink (game thread only) while the
        // effect is still in the target's list: an instant effect may be gone by the time the task
        // runs. Forms are permanent, so the MGEF and spell pointers may cross into the task.
        struct EffectFacts
        {
            const RE::EffectSetting* base = nullptr;
            const RE::MagicItem* spell = nullptr;
            float duration = 0.0f;
        };

        void StartBurn(RE::ObjectRefHandle a_target, std::uint16_t a_id, EffectFacts a_facts)
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
            const auto* base = effect ? effect->GetBaseObject() : a_facts.base;
            const auto* spell = effect ? effect->spell : a_facts.spell;
            if (!base && !effect) {
                // Read off the game thread: no facts, and the effect is already gone
                LogBurn(0, targetRef->GetFormID(), std::format("effect {} on {:08X} gone before the task ran and no "
                    "facts from the event (sink off the game thread, or the effect not found there): nothing applied", a_id, targetRef->GetFormID()));
                ReportBurn({ 0, targetRef->GetFormID(), 0, 0, "active effect gone, no event facts", 0, kFirstAttempt,
                    false, false });
                return;
            }
            if (!IsBurnable(base, spell)) {
                return;
            }
            const auto* engineShader = base->data.effectShader;
            if (!engineShader || engineShader == ours) {
                return;  // this effect has no hit shader to swap
            }

            // Grey on the first attempt, whatever state hitEffects is in
            const float seconds = effect ? GreyDuration(effect) :
                                           (a_facts.duration > 0.0f ? a_facts.duration : kInstantBurnSeconds);
            bool extended = false;
            auto grey = ApplyOrExtendGrey(targetRef.get(), a_target, ours, seconds, extended);
            const bool tracked = effect && (effect->duration > 0.0f || IsConcentration(base));
            if (grey && tracked) {
                Live().push_back({ a_target, a_id, grey });  // kept alight while the effect lasts (TickBurns)
            } else if (grey) {
                // An instant hit: its seconds stand even if a duration effect's burn on the same shader ends
                auto& floor = AgeFloors()[grey.get()];
                floor = (std::max)(floor, grey->age + seconds);
            }

            int stopped = 0;
            std::vector<SelfTestProbe::HitShader> hitShaders;
            const char* listState = "original left (overlay)";
            if (!effect) {
                // Applied from the event's facts; the engine shader cannot be found without the effect
                listState = "active effect gone before the task ran (grey from the event's data, engine shader left)";
            } else if (config.burnMode == BurnMode::kReplace) {
                const HitList state = FinishEngineShaders(effect, engineShader, stopped, hitShaders);
                listState = HitListName(state);
                if (state != HitList::kStopped) {
                    Pending().push_back({ a_target, a_id, g_tick });
                }
            }
            UpdateLiveCount();
            const auto spellId = spell ? spell->GetFormID() : 0;
            ReportBurn({ 0, targetRef->GetFormID(), spellId, engineShader->GetFormID(), listState, stopped, kFirstAttempt,
                grey != nullptr && !extended, extended, std::move(hitShaders) });
            LogBurn(spellId, targetRef->GetFormID(), std::format("[{}] spell {:08X} MGEF {:08X} on {:08X}: hit shader "
                "{:08X}: {} ({} stopped{}), grey shader {} {:.1f} s", BurnModeName(config.burnMode), spellId,
                base->GetFormID(), targetRef->GetFormID(), engineShader->GetFormID(), listState, stopped,
                (effect && config.burnMode == BurnMode::kReplace && stopped == 0) ? ", retry after a tick" : "",
                extended ? "extended to" : (grey ? "applied for" : "NOT applied (null) for"), seconds));
        }

        // The retry finishes the engine's shader only; the grey one was applied at attempt 1
        void RetryFinish(const PendingFinish& a_pending)
        {
            const auto targetRef = a_pending.target.get();
            if (!targetRef) {
                logger::info("BlackFlame: burn retry: the target of effect {} is gone", a_pending.activeEffectId);
                return;
            }
            const auto targetId = targetRef->GetFormID();
            auto* effect = FindActiveEffect(targetRef.get(), a_pending.activeEffectId);
            if (!effect) {
                LogBurn(0, targetId, std::format("retry: effect {} on {:08X} gone, its engine shader is left to run out",
                    a_pending.activeEffectId, targetId));
                ReportBurn({ 0, targetId, 0, 0, "retry: effect gone, engine shader left", 0, kRetryAttempt, false, false });
                return;
            }
            const auto* base = effect->GetBaseObject();
            const auto* engineShader = base ? base->data.effectShader : nullptr;
            const auto spellId = effect->spell ? effect->spell->GetFormID() : 0;
            if (!engineShader) {
                LogBurn(spellId, targetId, std::format("retry: effect {} on {:08X} has no hit shader any more",
                    a_pending.activeEffectId, targetId));
                return;
            }
            int stopped = 0;
            std::vector<SelfTestProbe::HitShader> hitShaders;
            const HitList state = FinishEngineShaders(effect, engineShader, stopped, hitShaders);
            ReportBurn({ 0, targetId, spellId, engineShader->GetFormID(), HitListName(state), stopped, kRetryAttempt,
                false, false, std::move(hitShaders) });
            LogBurn(spellId, targetId, std::format("retry: spell {:08X} on {:08X}: {} ({} stopped)", spellId, targetId,
                HitListName(state), stopped));
        }

        // Live burns: kept alight while their effect lasts; when the effect is gone and the player
        // has no other fire effect on the target, the grey shader is finished; an entry whose
        // shader the ProcessLists no longer hold is dropped.
        void KeepLiveBurns()
        {
            auto& live = Live();
            auto& floors = AgeFloors();
            if (live.empty() && floors.empty()) {
                return;
            }
            std::vector<RE::NiPointer<RE::ShaderReferenceEffect>> toFinish;
            std::erase_if(live, [&](const LiveBurn& a_burn) {
                const auto targetRef = a_burn.target.get();
                if (!targetRef || !a_burn.shader || a_burn.shader->finished) {
                    return true;
                }
                if (IsLive(FindActiveEffect(targetRef.get(), a_burn.activeEffectId))) {
                    return false;
                }
                if (!PlayerHasOtherFireEffect(targetRef.get(), a_burn.activeEffectId)) {
                    toFinish.push_back(a_burn.shader);
                }
                return true;
            });
            (void)FinishGreyUnderLock(toFinish);
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return;
            }
            // Finished entries stay in the ProcessLists for a while: they count as gone here
            std::unordered_set<RE::ShaderReferenceEffect*> seen;
            std::unordered_set<RE::ShaderReferenceEffect*> floorSeen;
            lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
                if (!a_effect || a_effect->finished) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (floors.contains(a_effect)) {
                    floorSeen.insert(a_effect);
                }
                for (const auto& burn : live) {
                    if (burn.shader.get() == a_effect) {
                        Extend(a_effect, kKeepAliveSeconds);
                        seen.insert(a_effect);
                        break;
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            std::erase_if(live, [&](const LiveBurn& a_burn) { return !seen.contains(a_burn.shader.get()); });
            std::erase_if(floors, [&](const auto& a_entry) { return !floorSeen.contains(a_entry.first); });
        }
    }

    void OnPlayerEffectApplied(RE::TESObjectREFR* a_target, std::uint16_t a_activeEffectId)
    {
        LogHookThread(g_sinkThreadLogs, "Active effect event sink", "effect read in the sink, burn in a task",
            "effect read in the task (no facts from the event)");
        EffectFacts facts;
        // The target's effect list is read here only on the game thread; elsewhere the task reads
        // it (an instant effect may then be gone: StartBurn logs and records that)
        if (IsOnGameThread()) {
            if (auto* effect = FindActiveEffect(a_target, a_activeEffectId)) {
                facts = { effect->GetBaseObject(), effect->spell, effect->duration };
                if (!IsBurnable(facts.base, facts.spell)) {
                    return;  // not a fire spell's fire effect: no task at all
                }
            }
        }
        const auto handle = a_target->GetHandle();
        const auto generation = WorldGeneration();
        AddTaskToGameThread("BlackFlameBurn", [handle, a_activeEffectId, facts, generation]() {
            if (generation == WorldGeneration()) {
                StartBurn(handle, a_activeEffectId, facts);
            }
        });
    }

    void QueueBurnEnd(RE::ObjectRefHandle a_target, std::uint16_t a_activeEffectId)
    {
        const auto generation = WorldGeneration();
        AddTaskToGameThread("BlackFlameBurnEnd", [a_target, a_activeEffectId, generation]() {
            if (generation != WorldGeneration()) {
                return;  // queued before a load
            }
            auto& live = Live();
            const auto targetRef = a_target.get();
            const auto tracked = std::ranges::find_if(live, [&](const LiveBurn& a_burn) {
                return a_burn.target == a_target && a_burn.activeEffectId == a_activeEffectId;
            });
            // Only a tracked burn (an effect with a duration or a concentration cast): an instant
            // hit's grey shader runs its own short time even though its effect is removed at once
            if (!targetRef || tracked == live.end() || PlayerHasOtherFireEffect(targetRef.get(), a_activeEffectId)) {
                return;
            }
            auto shader = tracked->shader;
            std::erase_if(live, [&](const LiveBurn& a_burn) { return a_burn.shader == shader; });
            const int held = FinishGreyUnderLock({ shader });
            UpdateLiveCount();
            LogBurn(0, targetRef->GetFormID(), std::format("effect {} on {:08X} removed, no other fire effect of the "
                "player there: grey shader {}", a_activeEffectId, targetRef->GetFormID(),
                held ? "held to an instant hit's floor" : "finished"));
        });
    }

    bool HasLiveBurns() noexcept
    {
        return g_liveCount.load() > 0;
    }

    void TickBurns()
    {
        ++g_tick;
        auto& pending = Pending();
        if (!pending.empty()) {
            std::vector<PendingFinish> due;
            std::vector<PendingFinish> later;
            for (const auto& p : pending) {
                (p.queuedTick + kRetryTicks < g_tick ? due : later).push_back(p);
            }
            pending.swap(later);
            for (const auto& p : due) {
                RetryFinish(p);
            }
        }
        KeepLiveBurns();
        UpdateLiveCount();
    }

    void ResetBurnState()
    {
        Live().clear();  // on the game thread at kPreLoadGame / kNewGame: the engine is still up
        Pending().clear();
        AgeFloors().clear();
        UpdateLiveCount();
    }
}
