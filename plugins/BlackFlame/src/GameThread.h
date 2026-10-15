#pragma once

#include "Common.h"

#include <atomic>
#include <exception>
#include <functional>
#include <thread>

// =============================================================================
// GAME THREAD - stamp, check and dispatch (copied from plugins/spelllearning
// include/ThreadUtils.h: the addon does not link SpellLearning)
// =============================================================================

namespace BlackFlame
{
    // The game thread's id, stamped from the SKSE messaging handler (kDataLoaded)
    inline std::atomic<std::thread::id> g_gameThreadId{};

    inline void MarkGameThread()
    {
        g_gameThreadId.store(std::this_thread::get_id());
    }

    [[nodiscard]] inline bool IsOnGameThread()
    {
        return std::this_thread::get_id() == g_gameThreadId.load();
    }

    /// Runs `task` on the game thread at the next task pump. An exception inside it
    /// is logged, never passed back into the engine.
    inline void AddTaskToGameThread(const char* taskName, std::function<void()>&& task)
    {
        const SKSE::TaskInterface* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            logger::error("BlackFlame: TaskInterface unavailable - dropping task '{}'", taskName);
            return;
        }
        taskInterface->AddTask([taskName, task = std::move(task)]() {
            try {
                task();
            } catch (const std::exception& e) {
                logger::error("BlackFlame: task '{}' threw: {}", taskName, e.what());
            } catch (...) {
                logger::error("BlackFlame: task '{}' threw an unknown exception", taskName);
            }
        });
    }
}
