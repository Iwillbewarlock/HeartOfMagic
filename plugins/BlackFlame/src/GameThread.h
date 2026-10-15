#pragma once

#include "Common.h"
#include "ThreadUtils.h"  // plugins/spelllearning/include: MarkGameThread, IsOnGameThread, AddTaskToGameThread

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

// =============================================================================
// GAME THREAD - the Windows id of the game thread, for the log
// =============================================================================
// Dispatch and the game-thread check come from Heart of Magic's ThreadUtils.h
// (header only, global namespace). Only the native id stamps, the thread logs and
// QueueTask are this addon's.
//
// What "game thread" means in this addon: an SKSE task (QueueTask) or the SKSE
// message handler. That those run on the engine's main thread is NOT proven: the
// first in-game run (AE 1.6.1170) logged tasks on six different OS threads,
// Handle3DLoaded on the SKSEPlugin_Load thread and kDataLoaded on another. The
// game thread is stamped at SKSEPlugin_Load, the kDataLoaded thread is kept
// beside it, and the first runs of every task kind and of the message handler log
// their thread, so the second run settles it. Containers shared between tasks,
// the message handler and hooks are under mutexes either way.

namespace BlackFlame
{
    // Stamped at SKSEPlugin_Load (round 8; it was kDataLoaded's thread before, which matched nothing
    // in the first run)
    inline std::atomic<std::uint32_t> g_gameThreadNativeId{ 0 };
    inline std::atomic<std::uint32_t> g_loadThreadNativeId{ 0 };        // SKSEPlugin_Load's thread
    inline std::atomic<std::uint32_t> g_dataLoadedThreadNativeId{ 0 };  // kDataLoaded's thread
    inline constexpr int kMaxThreadLogs = 3;

    // Bumped when a save loads or a new game starts (Main.cpp ResetLayers): a task queued in the
    // old world captures the value it was queued with and does nothing when it no longer matches
    inline std::atomic<std::uint32_t> g_worldGeneration{ 0 };

    [[nodiscard]] inline std::uint32_t WorldGeneration() noexcept
    {
        return g_worldGeneration.load();
    }

    /// SKSEPlugin_Load: this thread is taken as the game thread (IsOnGameThread compares with it)
    inline void StampGameThread()
    {
        ::MarkGameThread();
        g_gameThreadNativeId.store(REX::W32::GetCurrentThreadId());
        g_loadThreadNativeId.store(REX::W32::GetCurrentThreadId());
    }

    /// The first a_max runs per a_what (a string literal): which thread it ran on
    inline void LogThread(const char* a_what, const char* a_kind, int a_max = kMaxThreadLogs)
    {
        static std::mutex mutex;
        static auto* counts = std::make_unique<std::unordered_map<const char*, int>>().release();
        int run = 0;
        {
            std::lock_guard lock(mutex);
            run = ++(*counts)[a_what];
        }
        if (run > a_max) {
            return;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        logger::info("BlackFlame: {} '{}' run {} on thread {} ({}; stamped game thread {} = SKSEPlugin_Load, "
                     "kDataLoaded thread {})", a_kind, a_what, run, current,
            current == g_gameThreadNativeId.load() ? "the stamped game thread" : "NOT the stamped game thread",
            g_gameThreadNativeId.load(), g_dataLoadedThreadNativeId.load());
    }

    /// AddTaskToGameThread, with the thread of the first runs of each task kind logged
    inline void QueueTask(const char* a_name, std::function<void()> a_task)
    {
        AddTaskToGameThread(a_name, [a_name, task = std::move(a_task)]() {
            LogThread(a_name, "task");
            task();
        });
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
        logger::info("BlackFlame: {} ran on thread {} (stamped game thread {} = SKSEPlugin_Load, kDataLoaded thread "
                     "{}): {}", a_hook, current, game, g_dataLoadedThreadNativeId.load(),
            current == game ? a_onGame : a_elsewhere);
    }
}
