#pragma once

#include "Common.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <source_location>
#include <string>
#include <vector>

// =============================================================================
// TRACKED MUTEX (v2 fix C6d) - our mutexes with their current owner, for the stall dump
// =============================================================================
// A std::mutex that remembers which thread holds it (set after lock, cleared before
// unlock) and where it was declared (the declaration's source location, taken by the
// default constructor argument). Every instance registers itself once in a fixed table
// (kMaxTracked; more are simply not listed) so the watchdog (Watchdog.h) can list the
// held ones when the game stalls. Use with std::lock_guard as before. No behaviour
// change: the lock is the std::mutex's.

namespace BlackFlame
{
    class TrackedMutex
    {
    public:
        explicit TrackedMutex(std::source_location a_where = std::source_location::current()) noexcept;
        ~TrackedMutex();
        TrackedMutex(const TrackedMutex&) = delete;
        TrackedMutex& operator=(const TrackedMutex&) = delete;

        void lock();
        void unlock() noexcept;
        [[nodiscard]] bool try_lock();

        [[nodiscard]] std::uint32_t Owner() const noexcept { return m_owner.load(std::memory_order_relaxed); }
        [[nodiscard]] const char* File() const noexcept { return m_file; }
        [[nodiscard]] std::uint32_t Line() const noexcept { return m_line; }

    private:
        std::mutex m_mutex;
        std::atomic<std::uint32_t> m_owner{ 0 };
        const char* m_file;
        std::uint32_t m_line;
        int m_slot = -1;  // its registry slot, -1 when the table was full
    };

    /// "file:line held by thread N" for every registered mutex held now (any thread, read only)
    [[nodiscard]] std::vector<std::string> HeldTrackedMutexes();
}
