#pragma once

#include "Common.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

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

// How long a Papyrus native waits for the game thread before giving its script
// the fallback value. Usually the answer comes within a frame; the ceiling only
// exists so a dropped task cannot hang the script for good.
inline constexpr auto kPapyrusWait = std::chrono::milliseconds(2000);

// Runs `work` on the game thread and hands back what it returned. For a caller
// off the game thread that needs the answer now - a Papyrus native returning a
// value to a script - rather than one that can post its work and move on. On
// the game thread already, `work` simply runs. Returns nothing when the game
// thread did not come round in `timeout`, when the task was dropped, or when
// `work` threw; the caller picks the value a script should see in that case.
//
// Why wait at all: the data these natives read has no lock and is written by
// the game thread. Reading it from a Papyrus worker thread while the game
// thread inserts into the same map is a crash waiting for the right moment.
// A frame's wait is the price of reading it where it is written.
template <typename F>
[[nodiscard]] auto RunOnGameThreadAndWait(const std::string& taskName, F&& work, std::chrono::milliseconds timeout)
    -> std::optional<std::invoke_result_t<F>>
{
    using Result = std::invoke_result_t<F>;

    if (IsOnGameThread()) {
        try {
            return std::optional<Result>(work());
        } catch (const std::exception& e) {
            logger::error("{}: {}", taskName, e.what());
            return std::nullopt;
        } catch (...) {
            logger::error("{}: unknown exception", taskName);
            return std::nullopt;
        }
    }

    auto answer = std::make_shared<std::promise<Result>>();
    auto pending = answer->get_future();
    AddTaskToGameThread(taskName, [answer, work = std::forward<F>(work)]() mutable {
        try {
            answer->set_value(work());
        } catch (...) {
            answer->set_exception(std::current_exception());
        }
    });

    if (pending.wait_for(timeout) != std::future_status::ready) {
        logger::error("{}: the game thread did not come round within {} ms", taskName, timeout.count());
        return std::nullopt;
    }
    try {
        return std::optional<Result>(pending.get());
    } catch (const std::exception& e) {
        logger::error("{}: {}", taskName, e.what());
        return std::nullopt;
    } catch (...) {
        logger::error("{}: unknown exception", taskName);
        return std::nullopt;
    }
}
