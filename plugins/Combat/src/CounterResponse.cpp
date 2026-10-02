#include "Common.h"
#include "CounterResponse.h"
#include "CombatConfig.h"
#include "WardSupport.h"

#include "SpellLearningAPI.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace SLCombat
{
    namespace
    {
        constexpr const char* kXPSourceId = "combat";
        constexpr const char* kXPSourceName = "Combat (counter)";

        constexpr float kMsPerSecond = 1000.0f;

        // Magicka is taken when the cast fires, which can be after the cast event
        // (charged and fire-and-forget spells): refund a moment later.
        constexpr auto kRefundDelay = std::chrono::milliseconds(600);

        using Clock = std::chrono::steady_clock;

        SpellLearning::ISpellLearningAPI* s_api = nullptr;
        bool s_sourceRegistered = false;

        // --- slow motion ----------------------------------------------------
        std::mutex s_slowMoLock;
        bool s_slowMoActive = false;
        float s_savedTarget = 1.0f;    ///< the target before our slow motion
        float s_appliedTarget = 1.0f;  ///< the target we wrote
        std::uint64_t s_slowMoGeneration = 0;

        float* TimeMultiplierTarget()
        {
            // Same address CommonLib's BSTimer::QGlobalTimeMultiplierTarget() reads
            static REL::Relocation<float*> target{ RELOCATION_ID(511883, 388443) };
            return target.get();
        }

        /// Caller holds s_slowMoLock.
        void RestoreLocked()
        {
            if (!s_slowMoActive) {
                return;
            }
            // Only put it back when the target is still ours: if Slow Time (or another
            // mod) wrote it meanwhile, that value wins and ours is dropped.
            auto* target = TimeMultiplierTarget();
            if (target && *target == s_appliedTarget) {
                *target = s_savedTarget;
                logger::info("counter.slowmo restored={:.2f}", s_savedTarget);
            } else {
                logger::info("counter.slowmo left alone (target {:.2f} changed by someone else)", target ? *target : -1.0f);
            }
            s_slowMoActive = false;
            ++s_slowMoGeneration;
        }

        // --- free next cast ---------------------------------------------------
        std::atomic<std::int64_t> s_freeCastUntil{ 0 };  ///< Clock ticks, 0 = not armed

        void AddTaskLater(std::chrono::milliseconds a_delay, std::function<void()> a_task)
        {
            std::thread([a_delay, task = std::move(a_task)]() mutable {
                std::this_thread::sleep_for(a_delay);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(std::move(task));
                }
            }).detach();
        }
    }

    CounterResponse* CounterResponse::GetSingleton()
    {
        static CounterResponse instance;
        return &instance;
    }

    void CounterResponse::Register()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESSpellCastEvent>(GetSingleton());
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
        }
        logger::info("SL_Combat: spell cast and menu sinks registered");
    }

    void CounterResponse::SetAPI(SpellLearning::ISpellLearningAPI* a_api)
    {
        s_api = a_api;
    }

    void CounterResponse::RegisterXPSource()
    {
        if (!s_api || s_sourceRegistered) {
            return;
        }
        bool ok = s_api->RegisterXPSource(kXPSourceId, kXPSourceName);
        s_sourceRegistered = true;
        logger::info("SL_Combat: RegisterXPSource('{}') = {}", kXPSourceId, ok);
    }

    // =========================================================================
    // Counter
    // =========================================================================

    void CounterResponse::OnCounter(RE::MagicItem* a_spell, RE::TESObjectREFR*, Hand a_hand, bool a_bothHands)
    {
        // Called from the collision hook, whose thread is not known: everything
        // here waits for the game thread (one frame; the effects last 0.35 s+).
        std::string spellName = a_spell ? a_spell->GetName() : "";
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            return;
        }
        tasks->AddTask([spellName = std::move(spellName), a_hand, a_bothHands]() {
            const auto& config = CombatConfig::Get();
            StartSlowMotion();
            if (CounterInput::GetSingleton()->IsHeld(a_hand)) {
                WardSupport::StartHold(a_hand, a_bothHands);
            }

            if (config.freeNextCast || config.xpPerCounter > 0.0f) {
                auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<float>(config.freeCastWindow));
                s_freeCastUntil.store(until.time_since_epoch().count());
            }
            if (!config.hudText.empty() && !spellName.empty()) {
                RE::SendHUDMessage::ShowHUDMessage(std::format("{}: {}", config.hudText, spellName).c_str());
            }
        });
    }

    void CounterResponse::Reset()
    {
        std::lock_guard lock(s_slowMoLock);
        s_slowMoActive = false;  // a load sets its own time; nothing of ours to put back
        ++s_slowMoGeneration;
        s_freeCastUntil.store(0);
    }

    // =========================================================================
    // Slow motion
    // =========================================================================

    void CounterResponse::StartSlowMotion()
    {
        const auto& config = CombatConfig::Get();
        if (config.slowMoScale >= 1.0f || config.slowMoDuration <= 0.0f) {
            return;
        }
        auto* target = TimeMultiplierTarget();
        if (!target) {
            return;
        }

        std::uint64_t generation;
        {
            std::lock_guard lock(s_slowMoLock);
            if (!s_slowMoActive) {
                s_savedTarget = *target;
                s_slowMoActive = true;
            }
            s_appliedTarget = std::min(config.slowMoScale, s_savedTarget);
            *target = s_appliedTarget;
            generation = ++s_slowMoGeneration;  // a second counter extends, it does not stack
        }
        logger::info("counter.slowmo scale={:.2f} saved={:.2f}", config.slowMoScale, s_savedTarget);

        auto delay = std::chrono::milliseconds(static_cast<int>(config.slowMoDuration * kMsPerSecond));
        AddTaskLater(delay, [generation]() {
            std::lock_guard lock(s_slowMoLock);
            if (generation == s_slowMoGeneration) {  // else a later counter owns the restore
                RestoreLocked();
            }
        });
    }

    void CounterResponse::EndSlowMotion()
    {
        std::lock_guard lock(s_slowMoLock);
        RestoreLocked();
    }

    // =========================================================================
    // Free next cast + learning XP
    // =========================================================================

    void CounterResponse::RefundLater(RE::FormID a_spellID)
    {
        AddTaskLater(kRefundDelay, [a_spellID]() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellID);
            if (!player || !spell) {
                return;
            }
            float cost = spell->CalculateMagickaCost(player);
            if (cost > 0.0f) {
                player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, cost);
            }
            logger::info("counter.refund spell={:08X} '{}' cost={:.1f}", a_spellID, spell->GetName(), cost);
            FlushLog();
        });
    }

    RE::BSEventNotifyControl CounterResponse::ProcessEvent(const RE::TESSpellCastEvent* a_event,
        RE::BSTEventSource<RE::TESSpellCastEvent>*)
    {
        if (!a_event || a_event->object.get() != RE::PlayerCharacter::GetSingleton()) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto until = s_freeCastUntil.load();
        if (until == 0 || Clock::now().time_since_epoch().count() > until) {
            return RE::BSEventNotifyControl::kContinue;
        }
        s_freeCastUntil.store(0);

        const auto& config = CombatConfig::Get();
        RE::FormID spellID = a_event->spell;
        if (config.freeNextCast) {
            RefundLater(spellID);
        }
        if (s_api && config.xpPerCounter > 0.0f) {
            float granted = s_api->AddSourcedXP(spellID, config.xpPerCounter, kXPSourceId);
            logger::info("counter.xp spell={:08X} asked={:.1f} granted={:.1f}", spellID, config.xpPerCounter, granted);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl CounterResponse::ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (a_event && a_event->opening) {
            EndSlowMotion();  // a menu never opens into slow motion
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
