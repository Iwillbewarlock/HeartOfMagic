#include "SelfTest.h"

#include "Config.h"
#include "HitEffectHook.h"
#include "MainThreadQueue.h"
#include "SelfTestCtx.h"
#include "SelfTestLog.h"
#include "SelfTestProbe.h"
#include "SelfTestReload.h"
#include "SelfTestWorld.h"

#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

namespace BlackFlame::SelfTest
{
    namespace
    {
        constexpr auto kArmedLimit = std::chrono::minutes(5);       // never started by then: disarm
        constexpr auto kNominalTick = std::chrono::milliseconds(100);

        std::atomic<bool> g_active{ false };  // armed or running
        // Game thread only, from here on
        bool g_armed = false;
        bool g_ran = false;  // once per game session
        bool g_running = false;
        bool g_entered = false;
        std::size_t g_index = 0;
        Clock::time_point g_armedAt;
        Clock::time_point g_lastTick;
        // C1: unpaused play counted since the arm (config selfTestDelaySeconds); menus, loading and pauses add nothing
        Clock::duration g_readyPlay{};
        std::optional<Clock::time_point> g_lastReadyCheck;
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
            const auto extra = g_ctx.extraTarget;  // E2's target
            const bool lateCleanup = !a_forLoad && !g_ctx.cleanedUp;
            g_running = false;
            g_active.store(false);
            g_ctx = {};
            SelfTestProbe::SetRecording(false);
            try {
                if (lateCleanup) {
                    // J8: E6's cloak and E-bound's spell (its arrows deleted, the player's hands put back), each on its own
                    try {
                        CleanupArtSpells();
                    } catch (...) {
                        logger::error("BlackFlame: self-test late cleanup: CleanupArtSpells threw");
                    }
                    try {
                        CleanupBoundSpell();
                        NoteBoundRestoreUnchecked();
                    } catch (...) {
                        logger::error("BlackFlame: self-test late cleanup: CleanupBoundSpell threw");
                    }
                    // Each removal on its own first, so a throw in one cannot skip the other
                    int dispelled = 0;
                    std::string target = "not removed (threw)";
                    std::string casterDone = "not removed (threw)";
                    try {
                        dispelled = SelfTestWorld::DispelStoke();
                    } catch (...) {
                        logger::error("BlackFlame: self-test late cleanup: DispelStoke threw");
                    }
                    try {
                        target = SelfTestWorld::Remove(dummy);
                    } catch (...) {
                        logger::error("BlackFlame: self-test late cleanup: removing the target threw");
                    }
                    try {
                        casterDone = SelfTestWorld::Remove(caster);
                        if (extra.get()) {
                            casterDone += "; E2 target: " + SelfTestWorld::Remove(extra);
                        }
                    } catch (...) {
                        logger::error("BlackFlame: self-test late cleanup: removing the caster threw");
                    }
                    SelfTestLog::Info("CLEANUP", std::format("late cleanup: dispelled {} Stoke effect(s); target: {}; "
                        "caster: {}", dispelled, target, casterDone));
                }
            } catch (...) {
                logger::error("BlackFlame: self-test late cleanup threw; the test actors may be left in the world");
            }
            try {
                Reload::OnRunFinished(a_forLoad);  // E3 ended by its timeout or an exception: its leftovers (W1)
            } catch (...) {
                logger::error("BlackFlame: self-test E3 cleanup threw; its rune or marker may be left in the world");
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
            SelfTestProbe::AddPausedSeconds(std::chrono::duration<float>(a_paused).count());
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
            if (g_ctx.targetXYSince) {
                *g_ctx.targetXYSince += a_paused;
            }
            if (g_ctx.targetZSince) {
                *g_ctx.targetZSince += a_paused;
            }
            if (g_ctx.aim.hitAt) {
                *g_ctx.aim.hitAt += a_paused;
            }
            ShiftV2Clocks(a_paused);  // the v2 steps' own clocks (V13)
        }

        // The cleanup step (T9). v2: E3 runs after it, so the cleanup is no longer the last step
        std::size_t CleanupIndex()
        {
            const auto steps = Steps();
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (std::string_view(steps[i].id) == "T9") {
                    return i;
                }
            }
            return steps.size() - 1;
        }

        void JumpToCleanup()
        {
            g_index = CleanupIndex();
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
                ResetHitDiagnostics();  // H7: each step gets its own diagnostic lines (E6's were cut by the cap)
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
                if (step.onTimeout) {
                    step.onTimeout();
                }
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
            if (g_index >= CleanupIndex()) {
                Finish();  // the cleanup itself threw: Finish removes what is left
            } else {
                JumpToCleanup();
            }
        }

        bool StartWhenReady(Clock::time_point a_now)
        {
            const auto delay = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<float>(GetConfig().selfTestDelaySeconds));
            if (a_now - g_armedAt > kArmedLimit + delay) {
                logger::info("BlackFlame: self-test disarmed: it could not start within {} minutes plus the {:.0f} s start delay",
                    kArmedLimit.count(), GetConfig().selfTestDelaySeconds);
                g_armed = false;
                g_active.store(false);
                return false;
            }
            const auto gap = g_lastReadyCheck ? std::min<Clock::duration>(a_now - *g_lastReadyCheck, kMaxTickGap) :
                                                Clock::duration{};
            g_lastReadyCheck = a_now;
            if (!PlayerReady()) {
                return false;  // not counted (a menu, a load, a pause)
            }
            g_readyPlay += gap;
            if (g_readyPlay < delay) {
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

    void OnPreLoadGame()
    {
        Reload::OnPreLoadGame();
    }

    void OnNewGame()
    {
        Reload::OnNewGame();
    }

    void OnGameLoaded(bool a_success)
    {
        if (Reload::OnPostLoadGame(a_success)) {
            return;  // the reload E3 asked the player for: its resume runs from Tick (README "Self-test", E3)
        }
        if (!a_success) {
            return;
        }
        if (!GetConfig().selfTest || g_ran || g_armed) {
            return;
        }
        if (!MainQueue::Installed()) {
            // F2: its spawns, casts, moves and dispels change engine data; without the queue they would run on job threads
            logger::warn("BlackFlame: self-test not armed: the main-thread queue is not installed");
            g_ran = true;
            return;
        }
        g_armed = true;
        g_armedAt = Clock::now();
        g_readyPlay = {};
        g_lastReadyCheck.reset();
        g_active.store(true);
        logger::info("BlackFlame: self-test armed: starts after {:.0f} s of unpaused play with the player in a loaded cell "
                     "(config selfTestDelaySeconds; menus, loading and pauses are not counted)", GetConfig().selfTestDelaySeconds);
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
        // E3 (v2): the resume after the player's reload, or its window running out
        try {
            Reload::Tick();
            if (!g_active.load()) {
                return;
            }
        } catch (const std::exception& e) {
            logger::error("BlackFlame: self-test E3 threw: {}", e.what());
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
        return g_active.load() || Reload::Active();
    }
}
