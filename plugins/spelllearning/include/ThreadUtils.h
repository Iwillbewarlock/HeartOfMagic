#pragma once

#include "Common.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// =============================================================================
// THREAD UTILITIES - Safe dispatch to the main game thread
// =============================================================================

// The game thread's id, stamped by MarkGameThread() from a callback SKSE is
// known to deliver on that thread. Empty until the first stamp.
inline std::atomic<std::thread::id> g_gameThreadId{};

// Records the calling thread as the game thread. Call it only from somewhere
// SKSE guarantees runs there, such as the messaging interface handler.
inline void MarkGameThread()
{
    g_gameThreadId.store(std::this_thread::get_id());
}

// Whether the caller is already on the game thread. Code that would otherwise
// submit a task and block until it finishes must check this first: the task can
// only run once this thread returns to the game loop, so waiting on it from the
// game thread deadlocks until the wait times out.
[[nodiscard]] inline bool IsOnGameThread()
{
    return std::this_thread::get_id() == g_gameThreadId.load();
}

// Submits a named task to the SKSE main game thread with null-check and
// exception safety. All code that needs to call RE:: APIs or interact with
// game state from a callback or background thread should use this instead
// of calling SKSE::GetTaskInterface()->AddTask() directly.
//
// - taskName: Human-readable label for debug/error logs
// - task: The work to execute on the game thread
//
// If the TaskInterface is unavailable (SKSE init failure), the task is
// dropped and an error is logged. Any unhandled exception inside the task
// is caught and logged rather than crashing Skyrim.
inline void AddTaskToGameThread(std::string taskName, std::function<void()>&& task)
{
    const SKSE::TaskInterface* taskInterface = SKSE::GetTaskInterface();
    if (taskInterface) {
        logger::debug("AddTaskToGameThread: Submitting task '{}' to main game thread", taskName);
        auto safeTask = [taskName = std::move(taskName), task = std::move(task)]() {
            try {
                task();
            } catch (const std::exception& e) {
                logger::error("AddTaskToGameThread: Exception in task '{}': {}", taskName, e.what());
            } catch (...) {
                logger::error("AddTaskToGameThread: Unknown exception in task '{}'", taskName);
            }
        };
        taskInterface->AddTask(std::move(safeTask));
    } else {
        logger::error("AddTaskToGameThread: TaskInterface is nullptr — dropping task '{}'", taskName);
    }
}
