#pragma once

#include "Common.h"
#include "GameThreadFlag.h"

#include <array>
#include <atomic>
#include <cstdint>
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

// Whether the caller may do game thread work now: the engine's main thread, or inside
// a GameThreadFlag::Scope (a task, an SKSE message, a Papyrus native; GameThreadFlag.h).
// Code that would otherwise submit a task and block until it finishes must check this
// first: the task only runs once this thread goes back to its loop, so waiting on it
// here deadlocks until the wait times out.
[[nodiscard]] inline bool IsOnGameThread()
{
    if (GameThreadFlag::InScope()) return true;
    const auto* main = RE::Main::GetSingleton();
    return main && main->threadID == REX::W32::GetCurrentThreadId();
}

// The first thread seen in each kind of entry, with the main thread's id then, for the
// dev self-test's game-thread line (data to judge in game). Lock-free: the event sink
// kind is sampled from a sink that may only touch atomics.
enum class ThreadSampleKind : std::size_t
{
    kTask,
    kNative,
    kMessage,
    kEventSink,
    kCount
};
inline constexpr unsigned kThreadIdShift = 32;
inline std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(ThreadSampleKind::kCount)> g_threadSamples{};

// (thread id << 32) | main thread id; 0 = not seen yet
inline void SampleThread(ThreadSampleKind kind) noexcept
{
    auto& slot = g_threadSamples[static_cast<std::size_t>(kind)];
    if (slot.load(std::memory_order_relaxed) != 0) return;
    const auto* main = RE::Main::GetSingleton();
    const std::uint64_t value = (static_cast<std::uint64_t>(REX::W32::GetCurrentThreadId()) << kThreadIdShift) |
                                (main ? main->threadID : 0u);
    std::uint64_t empty = 0;
    slot.compare_exchange_strong(empty, value, std::memory_order_relaxed);
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
            const GameThreadFlag::Scope gameThreadWork;   // IsOnGameThread() inside the task
            SampleThread(ThreadSampleKind::kTask);
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

// =============================================================================
// Papyrus natives as game thread work
// =============================================================================
//
// A native not callable from tasklets is run by the VM as game thread work; the
// wrapper opens the scope for it (and samples its thread). Register every such native
// through RegisterGameThreadNative, never a tasklet-callable one: that one runs on a
// Papyrus worker thread and must post its work.
namespace GameThreadNative
{
    template <auto Fn>
    struct Entry;

    template <class R, class... Args, R (*Fn)(Args...)>
    struct Entry<Fn>
    {
        static R Call(Args... args)
        {
            const GameThreadFlag::Scope gameThreadWork;
            SampleThread(ThreadSampleKind::kNative);
            return Fn(std::forward<Args>(args)...);
        }
    };
}

template <auto Fn>
void RegisterGameThreadNative(RE::BSScript::IVirtualMachine* vm, std::string_view name, std::string_view className)
{
    vm->RegisterFunction(name, className, &GameThreadNative::Entry<Fn>::Call, false);
}
