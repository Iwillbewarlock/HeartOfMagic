#pragma once

#include "Common.h"
#include "TrackedMutex.h"
#include "ThreadTrace.h"
#include "ThreadUtils.h"  // plugins/spelllearning/include: MarkGameThread, IsOnGameThread, AddTaskToGameThread

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// =============================================================================
// GAME THREAD - the Windows id of the game thread, for the log
// =============================================================================
// Dispatch and the game-thread check come from Heart of Magic's ThreadUtils.h
// (header only, global namespace). Only the native id stamps, the thread logs and
// QueueTask are this addon's.
//
// SKSE tasks (QueueTask) run in the SKSE task phase of Main::Update (main thread,
// per SKSE 2.2.8 Hooks_Threads.cpp: s_tasks drained in BSTaskPool::ProcessTasks).
// Heart of Magic's own log showed its task bodies on the SKSEPlugin_Load thread.
// kDataLoaded runs on another (init) thread, which is why the old stamp there
// never matched. So the game thread is stamped by the first task itself (queued
// at SKSEPlugin_Load), which logs the load thread, its own and Main::threadID; the
// kDataLoaded thread is only logged. The first runs of every task kind and of the
// message handler still log their thread. Containers shared between tasks, the
// message handler and hooks are under mutexes (cheap, harmless).

namespace BlackFlame
{
    // Stamped by the first SKSE task (StampGameThreadFromTask)
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

    /// SKSEPlugin_Load: notes the load thread and queues the task that stamps the game thread
    /// (IsOnGameThread compares with it) and logs one line: load thread, its own, Main::threadID
    inline void StampGameThreadFromTask()
    {
        g_loadThreadNativeId.store(REX::W32::GetCurrentThreadId());
        AddTaskToGameThread("BlackFlameStamp", []() {
            ::MarkGameThread();
            const auto current = REX::W32::GetCurrentThreadId();
            g_gameThreadNativeId.store(current);
            const auto* main = RE::Main::GetSingleton();
            logger::info("BlackFlame: game thread stamped by the first SKSE task: taskThread={}, loadThread={}, "
                         "Main::threadID={}", current, g_loadThreadNativeId.load(),
                main ? std::to_string(main->threadID) : std::string("(no Main)"));
            if (main && main->threadID != 0 && main->threadID != current) {
                logger::warn("BlackFlame: Main::threadID {} differs from the first SKSE task's thread {} - the task "
                             "thread stays the game thread; please report this line", main->threadID, current);
            }
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
        logger::info("BlackFlame: {} '{}' run {}: currentThread={} ({}; gameThread={} stamped by the first task, "
                     "dataLoadedThread={})", a_kind, a_what, run, current,
            current == g_gameThreadNativeId.load() ? "the stamped game thread" : "NOT the stamped game thread",
            g_gameThreadNativeId.load(), g_dataLoadedThreadNativeId.load());
    }

    /// AddTaskToGameThread, with the thread of the first runs of each task kind logged, and every body
    /// traced (ThreadTrace.h, v2 S4: thread and interval, measurement only)
    inline void QueueTask(const char* a_name, std::function<void()> a_task)
    {
        AddTaskToGameThread(a_name, [a_name, task = std::move(a_task)]() {
            const ThreadTrace::Scope trace(a_name);
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
        logger::info("BlackFlame: {} ran with callerThread={} (gameThread={} stamped by the first task, dataLoadedThread="
                     "{}): {}", a_hook, current, game, g_dataLoadedThreadNativeId.load(),
            current == game ? a_onGame : a_elsewhere);
    }
}
