#include "BuffState.h"

#include "CasterArt.h"
#include "Forms.h"
#include "GameThread.h"

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
            if (IsStoked()) {
                TickCasterArt();  // layer 4
            }
        }

        void TickerLoop()
        {
            int ticks = 0;
            for (;;) {
                std::this_thread::sleep_for(kTickInterval);
                ++ticks;
                if (!IsStoked() && ticks % kIdleTicksPerCheck != 0) {
                    continue;
                }
                // One tick in the queue at most: a paused game (a menu) does not pile them up
                if (g_tickPending.exchange(true)) {
                    continue;
                }
                AddTaskToGameThread("BlackFlameTick", &Tick);
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
