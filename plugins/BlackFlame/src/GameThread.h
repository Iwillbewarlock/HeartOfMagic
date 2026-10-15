#pragma once

#include "Common.h"
#include "ThreadUtils.h"  // plugins/spelllearning/include: MarkGameThread, IsOnGameThread, AddTaskToGameThread

#include <atomic>
#include <cstdint>

// =============================================================================
// GAME THREAD - the Windows id of the game thread, for the log
// =============================================================================
// Dispatch and the game-thread check come from Heart of Magic's ThreadUtils.h
// (header only, global namespace). Only the native id stamp and the hook thread
// log are this addon's.

namespace BlackFlame
{
    inline std::atomic<std::uint32_t> g_gameThreadNativeId{ 0 };
    // The thread SKSE loaded the plugin on: the first in-game run on AE 1.6.1170 showed kDataLoaded
    // (the stamp above) on another thread than the one projectile 3D loads on, which matched this
    // one, and SKSE tasks on yet others - logged side by side until that is settled
    inline std::atomic<std::uint32_t> g_loadThreadNativeId{ 0 };
    inline constexpr int kMaxThreadLogs = 3;

    // Bumped when a save loads or a new game starts (Main.cpp ResetLayers): a task queued in the
    // old world captures the value it was queued with and does nothing when it no longer matches
    inline std::atomic<std::uint32_t> g_worldGeneration{ 0 };

    [[nodiscard]] inline std::uint32_t WorldGeneration() noexcept
    {
        return g_worldGeneration.load();
    }

    /// Call where ::MarkGameThread() is called (the SKSE messaging handler)
    inline void StampGameThread()
    {
        ::MarkGameThread();
        g_gameThreadNativeId.store(REX::W32::GetCurrentThreadId());
    }

    /// The first kMaxThreadLogs calls per counter: which thread a hook ran on
    inline void LogHookThread(std::atomic<int>& a_counter, const char* a_hook,
        const char* a_onGame = "recolour at once", const char* a_elsewhere = "deferred to a game-thread task")
    {
        if (a_counter.fetch_add(1) >= kMaxThreadLogs) {
            return;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        const auto game = g_gameThreadNativeId.load();
        logger::info("BlackFlame: {} ran on thread {} (kDataLoaded thread {}, plugin-load thread {}): {}", a_hook, current,
            game, g_loadThreadNativeId.load(),
            current == game ? a_onGame : a_elsewhere);
    }
}
