#include "BuffState.h"

#include "CasterArt.h"
#include "ExplosionHook.h"
#include "Forms.h"
#include "GameThread.h"
#include "HazardHook.h"
#include "HitArt.h"
#include "SelfTest.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace BlackFlame
{
    namespace
    {
        constexpr auto kTickInterval = std::chrono::milliseconds(100);
        constexpr int kIdleTicksPerCheck = 10;  // 1 s between checks while the buff is off

        std::atomic<bool> g_stoked{ false };
        std::atomic<bool> g_tickerStarted{ false };
        std::atomic<bool> g_tickPending{ false };
        std::atomic<bool> g_recomputePending{ false };  // one RecomputeBuff task queued at a time
        std::atomic<bool> g_loopFailureReported{ false };

        // MagicTarget::HasMagicEffectWithKeyword would also count an effect that has
        // ended but is still in the list (a remove event can arrive before the engine
        // drops it); this walk skips dispelled, inactive and expired ones.
        bool PlayerHasStokeEffect()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* keyword = GetForms().stokedKeyword;
            if (!player || !keyword) {
                return false;
            }
            auto* target = player->AsMagicTarget();
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!effects) {
                return false;
            }
            for (const auto* effect : *effects) {
                if (!effect) {
                    continue;
                }
                const auto* base = effect->GetBaseObject();
                if (!base || !base->HasKeyword(keyword)) {
                    continue;
                }
                if (effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    continue;
                }
                if (effect->duration > 0.0f && effect->elapsedSeconds >= effect->duration) {
                    continue;
                }
                return true;
            }
            return false;
        }

        void Tick()
        {
            g_tickPending.store(false);
            RecomputeBuff("tick");
            TickHitArt();  // layer 1: hit arts waiting for their 3D
            TickExplosionLights();  // layer 3: lights that did not exist at Set3D
            TickHazards();          // layer 3: hazards whose owner was not set yet
            if (IsStoked()) {
                TickCasterArt();  // layer 4
            }
            SelfTest::Tick();  // does nothing unless config selfTest armed it
        }

        void TickerLoop()
        {
            int ticks = 0;
            for (;;) {
                std::this_thread::sleep_for(kTickInterval);
                ++ticks;
                if (!IsStoked() && !SelfTest::WantsFastTicks() && !HasPendingHitArt() && !HasPendingHazards() && ticks % kIdleTicksPerCheck != 0) {
                    continue;
                }
                // A throw here would end the thread (and std::terminate the game): keep looping
                try {
                    // One tick in the queue at most: a paused game (a menu) does not pile them up
                    if (g_tickPending.exchange(true)) {
                        continue;
                    }
                    // ThreadUtils' AddTaskToGameThread returns nothing and drops the task only
                    // without a TaskInterface: checked here so the pending flag is not left set
                    if (!SKSE::GetTaskInterface()) {
                        g_tickPending.store(false);
                        continue;
                    }
                    AddTaskToGameThread("BlackFlameTick", &Tick);
                } catch (...) {
                    g_tickPending.store(false);
                    if (!g_loopFailureReported.exchange(true)) {
                        logger::error("BlackFlame: ticker loop threw; further failures are not logged");
                    }
                }
            }
        }
    }

    bool IsStoked() noexcept
    {
        return g_stoked.load(std::memory_order_acquire);
    }

    void RecomputeBuff(const char* a_reason)
    {
        const bool now = PlayerHasStokeEffect();
        const bool before = g_stoked.exchange(now, std::memory_order_acq_rel);
        if (now != before) {
            logger::info("BlackFlame: buff flag {} ({})", now ? "ON" : "off", a_reason);
            OnBuffChanged(now);  // the hand flames: grey at once, or put back
        }
    }

    void RequestRecomputeBuff(const char* a_reason)
    {
        if (g_recomputePending.exchange(true)) {
            return;  // one already queued: it reads the effects as they are when it runs
        }
        if (!SKSE::GetTaskInterface()) {
            g_recomputePending.store(false);  // the task would be dropped
            return;
        }
        // Called from the effect event sink: a throw (allocation) must neither escape nor leave the
        // flag set, or no recompute would ever be queued again
        try {
            AddTaskToGameThread("BlackFlameBuff", [a_reason]() {
                g_recomputePending.store(false);
                RecomputeBuff(a_reason);
            });
        } catch (...) {
            g_recomputePending.store(false);
            logger::error("BlackFlame: could not queue the buff recompute ({})", a_reason);
        }
    }

    void StartTicker()
    {
        if (g_tickerStarted.exchange(true)) {
            return;
        }
        // Detached, never joined: a thread joined at DLL unload can hang the game's exit
        // (plugins/Common.h). It only sleeps and posts tasks, so it holds nothing at exit.
        std::thread(&TickerLoop).detach();
        logger::info("BlackFlame: ticker started ({} ms while the buff is on, {} ms while off)", kTickInterval.count(),
            kTickInterval.count() * kIdleTicksPerCheck);
    }
}
