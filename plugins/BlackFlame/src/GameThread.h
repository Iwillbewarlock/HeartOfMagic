#pragma once

#include "Common.h"
#include "MainThreadQueue.h"
#include "TrackedMutex.h"
#include "ThreadTrace.h"
#include "ThreadUtils.h"  // plugins/spelllearning/include: MarkGameThread, AddTaskToGameThread

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// =============================================================================
// GAME THREAD - which thread is the game's main thread, and QueueTask
// =============================================================================
// v2 fix D1: QueueTask posts to our own main-thread queue (MainThreadQueue.h), drained at the end
// of Main::Update on the main thread. SKSE's AddTask is not used for our work any more: during play
// it is drained inside a BSJobs job on worker threads (research-mainthread notes; run 11:11 had 409
// of 415 task bodies off the main thread). The game thread is stamped by the queue's first frame and
// checked against RE::Main::threadID; IsOnGameThread compares the Windows thread id with it. Only
// when the queue could not be installed does an SKSE task stamp it (Main::threadID preferred).
// The first runs of every task kind and of the message handler still log their thread.

namespace BlackFlame
{
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

    /// Records a_thread as the game's main thread (also for ThreadUtils' std::thread::id check when it is the caller)
    inline void StampGameThread(std::uint32_t a_thread, const char* a_by)
    {
        if (a_thread == REX::W32::GetCurrentThreadId()) {
            ::MarkGameThread();
        }
        g_gameThreadNativeId.store(a_thread);
        logger::info("BlackFlame: game thread {} stamped by {} (loadThread={})", a_thread, a_by, g_loadThreadNativeId.load());
    }

    /// Whether the caller is the stamped main thread (false before the stamp)
    [[nodiscard]] inline bool IsOnGameThread() noexcept
    {
        const auto game = g_gameThreadNativeId.load();
        return game != 0 && REX::W32::GetCurrentThreadId() == game;
    }

    /// SKSEPlugin_Load, after MainQueue::Install: notes the load thread; without the queue an SKSE task stamps the game
    /// thread (Main::threadID when set, else the task's own thread)
    inline void StampGameThreadFallback()
    {
        g_loadThreadNativeId.store(REX::W32::GetCurrentThreadId());
        if (MainQueue::Installed()) {
            return;  // the queue's first frame stamps it
        }
        AddTaskToGameThread("BlackFlameStamp", []() {
            const auto* main = RE::Main::GetSingleton();
            const auto current = REX::W32::GetCurrentThreadId();
            const auto mainId = main ? main->threadID : 0;
            StampGameThread(mainId != 0 ? mainId : current, mainId != 0 ? "Main::threadID (SKSE task fallback)" :
                                                                          "the first SKSE task (fallback)");
        });
    }

    /// The first a_max runs per a_what (a string literal): which thread it ran on
    inline void LogThread(const char* a_what, const char* a_kind, int a_max = kMaxThreadLogs)
    {
        static TrackedMutex mutex;
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
        logger::info("BlackFlame: {} '{}' run {}: currentThread={} ({}; gameThread={}, dataLoadedThread={})", a_kind, a_what,
            run, current, current == g_gameThreadNativeId.load() ? "the main thread" : "NOT the main thread",
            g_gameThreadNativeId.load(), g_dataLoadedThreadNativeId.load());
    }

    /// Runs a_task on the main thread at the end of the next Main::Update (MainThreadQueue.h); every body traced
    /// (ThreadTrace.h) and the first runs of each kind logged with their thread
    inline void QueueTask(const char* a_name, std::function<void()> a_task)
    {
        MainQueue::Post(a_name, std::move(a_task));
    }

    /// The first kMaxThreadLogs calls per counter: which thread a hook ran on
    inline void LogHookThread(std::atomic<int>& a_counter, const char* a_hook,
        const char* a_what = "recorded; the work goes to the main-thread queue")
    {
        if (a_counter.fetch_add(1) >= kMaxThreadLogs) {
            return;
        }
        const auto current = REX::W32::GetCurrentThreadId();
        const auto game = g_gameThreadNativeId.load();
        logger::info("BlackFlame: {} ran with callerThread={} ({}; gameThread={}): {}", a_hook, current,
            current == game ? "the main thread" : "not the main thread", game, a_what);
    }
}
