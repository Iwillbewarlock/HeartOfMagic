#pragma once

#include "Common.h"

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// THREAD TRACE (v2 spike S4) - which threads run our task bodies and new hooks
// =============================================================================
// Measurement only: no gate is changed by it. Every QueueTask body (GameThread.h)
// and every v2 hook body records (kind, thread id, start, end) into a fixed ring
// buffer (kTraceCapacity entries, the oldest overwritten). The self-test step
// T-threads reads a snapshot: the threads seen per kind, and how many intervals of
// different threads overlapped in time (two bodies running at once). Lock-free:
// each slot is a sequence-checked set of atomics, so a record costs a fetch_add and
// five relaxed stores, and a reader skips a slot that was being written.

namespace BlackFlame::ThreadTrace
{
    inline constexpr std::size_t kTraceCapacity = 4096;

    struct Entry
    {
        const char* kind = "";  // a string literal (compared by pointer and printed)
        std::uint32_t thread = 0;
        std::int64_t start = 0;  // steady_clock ticks
        std::int64_t end = 0;
    };

    /// Any thread: records one finished body
    void Record(const char* a_kind, std::uint32_t a_thread, std::int64_t a_start, std::int64_t a_end) noexcept;

    /// steady_clock ticks now
    [[nodiscard]] std::int64_t Now() noexcept;

    /// RAII: records the scope's interval on the current thread when it ends
    class Scope
    {
    public:
        explicit Scope(const char* a_kind) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const char* m_kind;
        std::int64_t m_start;
    };

    /// The complete entries in the ring now (any thread)
    [[nodiscard]] std::vector<Entry> Snapshot();

    /// Total records ever made (the ring holds the last kTraceCapacity)
    [[nodiscard]] std::uint64_t TotalRecords() noexcept;

    /// Report lines for T-threads: per kind "kind: N bodies on threads a(x n), b(x m)", then the overlap
    /// counts of intervals on different threads per kind pair, and the stamped game thread's share
    [[nodiscard]] std::vector<std::string> Report(std::uint32_t a_gameThread);

    /// Overlapping pairs of intervals on different threads in a snapshot (for the PASS/INFO decision)
    [[nodiscard]] std::size_t CountCrossThreadOverlaps(const std::vector<Entry>& a_entries);
}
