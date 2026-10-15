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
    inline constexpr int kMaxThreadLogs = 3;

    /// Call where ::MarkGameThread() is called (the SKSE messaging handler)
    inline void StampGameThread()
    {
        ::MarkGameThread();
        g_gameThreadNativeId.store(REX::W32::GetCurrentThreadId());
    }

    /// The first kMaxThreadLogs calls per counter: which thread a hook ran on
    inline void LogHookThread(std::atomic<int>& a_counter, const char* a_hook)
    {
        if (a_counter.fetch_add(1) >= kMaxThreadLogs) {
            return;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        const auto game = g_gameThreadNativeId.load();
        logger::info("BlackFlame: {} ran on thread {} (game thread {}): {}", a_hook, current, game,
            current == game ? "recolour at once" : "deferred to a game-thread task");
    }
}
