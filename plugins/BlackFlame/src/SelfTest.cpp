#include "SelfTest.h"

#include "Config.h"
#include "SelfTestCtx.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"

#include <atomic>
#include <chrono>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        constexpr auto kReadyDelay = std::chrono::seconds(3);
        constexpr auto kArmedLimit = std::chrono::minutes(5);       // never started by then: disarm
        constexpr auto kNominalTick = std::chrono::milliseconds(100);
        constexpr auto kMaxTickGap = std::chrono::milliseconds(500);  // a longer gap counts as paused time

        std::atomic<bool> g_active{ false };  // armed or running
        // Game thread only, from here on
        bool g_armed = false;
        bool g_ran = false;  // once per game session
        bool g_running = false;
        bool g_entered = false;
        std::size_t g_index = 0;
        Clock::time_point g_armedAt;
        Clock::time_point g_lastTick;
        std::optional<Clock::time_point> g_readySince;
        Ctx g_ctx;

        bool PlayerReady()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            return player && ui && player->Is3DLoaded() && player->GetParentCell() && !player->IsDead() &&
                   !ui->GameIsPaused();
        }

        // Every exit except an abort for a load ends here (after the cleanup step, which
        // dispelled the buff and deleted the actors)
        void Finish()
        {
            SelfTestLog::Done();
            SelfTestProbe::SetRecording(false);
            SelfTestProbe::Clear();
            g_ctx = {};
            g_running = false;
            g_active.store(false);
        }

        // Paused time (a menu, a load screen, a dead player) is not counted against a timeout
        void ShiftClocks(Clock::duration a_paused)
        {
            g_ctx.stepStart += a_paused;
            if (g_ctx.lastCast) {
                *g_ctx.lastCast += a_paused;
            }
            if (g_ctx.greySeen) {
                *g_ctx.greySeen += a_paused;
            }
            if (g_ctx.secondHitAt) {
                *g_ctx.secondHitAt += a_paused;
            }
        }

        void JumpToCleanup()
        {
            g_index = Steps().size() - 1;
            g_entered = false;
        }

        void RunStep()
        {
            const auto steps = Steps();
            const auto& step = steps[g_index];
            const bool first = !g_entered;
            if (first) {
                g_entered = true;
                g_ctx.stepStart = Clock::now();
                g_ctx.phase = 0;
                g_ctx.waitingFor.clear();
            }
            bool done = step.run(g_ctx, first);
            const auto elapsed = std::chrono::duration<float>(Clock::now() - g_ctx.stepStart).count();
            if (!done && elapsed > step.timeoutSeconds) {
                SelfTestLog::Fail(step.id, std::format("timeout: {:.2f} s of unpaused play passed (limit {:.1f} s) "
                    "waiting for {}", elapsed, step.timeoutSeconds,
                    g_ctx.waitingFor.empty() ? "the step" : g_ctx.waitingFor));
                done = true;
            }
            if (!done) {
                return;
            }
            g_entered = false;
            if (++g_index >= steps.size()) {
                Finish();
            }
        }

        bool StartWhenReady(Clock::time_point a_now)
        {
            if (a_now - g_armedAt > kArmedLimit) {
                logger::info("BlackFlame: self-test disarmed: it could not start within {} minutes", kArmedLimit.count());
                g_armed = false;
                g_active.store(false);
                return false;
            }
            if (!PlayerReady()) {
                g_readySince.reset();
                return false;
            }
            if (!g_readySince) {
                g_readySince = a_now;
            }
            if (a_now - *g_readySince < kReadyDelay) {
                return false;
            }
            g_armed = false;
            g_ran = true;
            g_running = true;
            g_index = 0;
            g_entered = false;
            g_ctx = {};
            g_lastTick = a_now;
            SelfTestLog::Open();
            SelfTestProbe::SetRecording(true);
            SelfTestLog::Info("START", "Black Flame self-test");
            return true;
        }
    }

    void OnGameLoaded()
    {
        if (!GetConfig().selfTest || g_ran || g_armed) {
            return;
        }
        g_armed = true;
        g_armedAt = Clock::now();
        g_readySince.reset();
        g_active.store(true);
        logger::info("BlackFlame: self-test armed (starts when the player is in a loaded cell and the game is not paused)");
    }

    void Abort(const char* a_reason)
    {
        if (g_running) {
            // The world is being torn down: the old handles are not touched
            SelfTestLog::Fail("ABORT", std::format("{} - the test stopped; its actors and buff are left to the load",
                a_reason));
            Finish();
            return;
        }
        if (g_armed) {
            g_armed = false;
            g_active.store(false);
            logger::info("BlackFlame: self-test disarmed ({})", a_reason);
        }
    }

    void Tick()
    {
        if (!g_active.load()) {
            return;
        }
        // Entered from a game-thread task: a throw would only be logged there, so finish cleanly here
        try {
            const auto now = Clock::now();
            if (!g_running) {
                if (!g_armed || !StartWhenReady(now)) {
                    return;
                }
            }
            const auto gap = now - g_lastTick;
            g_lastTick = now;
            if (!PlayerReady()) {
                ShiftClocks(gap);
                return;
            }
            if (gap > kMaxTickGap) {
                ShiftClocks(gap - kNominalTick);
            }
            RunStep();
        } catch (const std::exception& e) {
            SelfTestLog::Fail("ABORT", std::format("exception in step {}: {}", g_index, e.what()));
            if (g_index + 1 >= Steps().size()) {
                Finish();  // the cleanup itself threw
            } else {
                JumpToCleanup();
            }
        }
    }

    bool WantsFastTicks() noexcept
    {
        return g_active.load();
    }
}
