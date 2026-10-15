#include "SelfTest.h"

#include "Config.h"
#include "SelfTestCtx.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestWorld.h"

#include <atomic>
#include <chrono>
#include <format>
#include <string>

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

        // Every exit ends here. Except for an abort for a load (the world is being torn down, its
        // handles must not be touched), the buff is dispelled and the actors deleted here too
        // when the cleanup step did not get to it (a timeout or an exception inside it).
        void Finish(bool a_forLoad = false) noexcept
        {
            // The state is reset first, so a throw below can never leave the machine running on a
            // step index past the table; each later part has its own guard
            const auto dummy = g_ctx.dummy;
            const auto caster = g_ctx.caster;
            const bool lateCleanup = !a_forLoad && !g_ctx.cleanedUp;
            g_running = false;
            g_active.store(false);
            g_ctx = {};
            SelfTestProbe::SetRecording(false);
            try {
                if (lateCleanup) {
                    // Each removal on its own first, so a throw in one cannot skip the other
                    const int dispelled = SelfTestWorld::DispelStoke();
                    std::string target = "not removed";
                    std::string casterDone = "not removed";
                    try {
                        target = SelfTestWorld::Remove(dummy);
                    } catch (...) {
                    }
                    try {
                        casterDone = SelfTestWorld::Remove(caster);
                    } catch (...) {
                    }
                    SelfTestLog::Info("CLEANUP", std::format("late cleanup: dispelled {} Stoke effect(s); target: {}; "
                        "caster: {}", dispelled, target, casterDone));
                }
            } catch (...) {
                logger::error("BlackFlame: self-test late cleanup threw; the test actors may be left in the world");
            }
            try {
                SelfTestProbe::Clear();
            } catch (...) {
                logger::error("BlackFlame: self-test could not clear its records");
            }
            try {
                SelfTestLog::Done();
            } catch (...) {
                logger::error("BlackFlame: self-test could not write its summary");
            }
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
            if (g_ctx.npcHitEta) {
                *g_ctx.npcHitEta += a_paused;
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
            if (g_index >= steps.size()) {
                return;  // never past the table, whatever ended the run
            }
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

        void OnStepException(const char* a_what) noexcept
        {
            try {
                SelfTestLog::Fail("ABORT", std::format("exception in step {}: {}", g_index, a_what));
            } catch (...) {
                logger::error("BlackFlame: self-test step {} threw and its FAIL line could not be written", g_index);
            }
            if (!g_running) {
                return;
            }
            if (g_index + 1 >= Steps().size()) {
                Finish();  // the cleanup itself threw: Finish removes what is left
            } else {
                JumpToCleanup();
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
            try {
                SelfTestLog::Fail("ABORT", std::format("{} - the test stopped; its actors and buff are left to the load",
                    a_reason));
            } catch (...) {
                logger::error("BlackFlame: self-test aborted ({}) and its FAIL line could not be written", a_reason);
            }
            Finish(true);
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
            OnStepException(e.what());
        } catch (...) {
            OnStepException("unknown exception");
        }
    }

    bool WantsFastTicks() noexcept
    {
        return g_active.load();
    }
}
