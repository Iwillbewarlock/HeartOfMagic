#include "ThreadTrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <map>
#include <memory>
#include <utility>

namespace BlackFlame::ThreadTrace
{
    namespace
    {
        // seq is 0 while a writer fills the slot, else the record number + 1 (written last, read first and last)
        struct Slot
        {
            std::atomic<std::uint64_t> seq{ 0 };
            std::atomic<const char*> kind{ nullptr };
            std::atomic<std::uint32_t> thread{ 0 };
            std::atomic<std::int64_t> start{ 0 };
            std::atomic<std::int64_t> end{ 0 };
        };

        std::atomic<std::uint64_t> g_next{ 0 };

        // Heap-held and never freed (hooks may record during shutdown)
        std::array<Slot, kTraceCapacity>& Ring()
        {
            static auto* ring = std::make_unique<std::array<Slot, kTraceCapacity>>().release();
            return *ring;
        }

        constexpr std::size_t kMaxThreadsListed = 8;
    }

    std::int64_t Now() noexcept
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    void Record(const char* a_kind, std::uint32_t a_thread, std::int64_t a_start, std::int64_t a_end) noexcept
    {
        const auto n = g_next.fetch_add(1, std::memory_order_relaxed);
        auto& slot = Ring()[n % kTraceCapacity];
        slot.seq.store(0, std::memory_order_release);
        slot.kind.store(a_kind, std::memory_order_relaxed);
        slot.thread.store(a_thread, std::memory_order_relaxed);
        slot.start.store(a_start, std::memory_order_relaxed);
        slot.end.store(a_end, std::memory_order_relaxed);
        slot.seq.store(n + 1, std::memory_order_release);
    }

    Scope::Scope(const char* a_kind) noexcept :
        m_kind(a_kind), m_start(Now())
    {}

    Scope::~Scope()
    {
        Record(m_kind, REX::W32::GetCurrentThreadId(), m_start, Now());
    }

    std::vector<Entry> Snapshot()
    {
        std::vector<Entry> out;
        out.reserve(kTraceCapacity);
        for (auto& slot : Ring()) {
            const auto before = slot.seq.load(std::memory_order_acquire);
            if (before == 0) {
                continue;
            }
            Entry e{ slot.kind.load(std::memory_order_relaxed), slot.thread.load(std::memory_order_relaxed),
                slot.start.load(std::memory_order_relaxed), slot.end.load(std::memory_order_relaxed) };
            if (slot.seq.load(std::memory_order_acquire) != before || !e.kind) {
                continue;  // rewritten while read
            }
            out.push_back(e);
        }
        return out;
    }

    std::uint64_t TotalRecords() noexcept
    {
        return g_next.load(std::memory_order_relaxed);
    }

    namespace
    {
        using KindPair = std::pair<std::string, std::string>;

        // Sweep in start order: every entry against the still-running entries of other threads.
        // O(n log n + n * k), k = intervals alive at once (small)
        std::map<KindPair, std::size_t> Overlaps(std::vector<Entry> a_entries)
        {
            std::ranges::sort(a_entries, {}, &Entry::start);
            std::map<KindPair, std::size_t> pairs;
            std::vector<const Entry*> active;
            for (const auto& e : a_entries) {
                std::erase_if(active, [&](const Entry* a_old) { return a_old->end <= e.start; });
                for (const auto* old : active) {
                    if (old->thread == e.thread) {
                        continue;
                    }
                    std::string a = old->kind;
                    std::string b = e.kind;
                    if (b < a) {
                        std::swap(a, b);
                    }
                    ++pairs[{ a, b }];
                }
                active.push_back(&e);
            }
            return pairs;
        }
    }

    std::size_t CountCrossThreadOverlaps(const std::vector<Entry>& a_entries)
    {
        std::size_t total = 0;
        for (const auto& [pair, count] : Overlaps(a_entries)) {
            total += count;
        }
        return total;
    }

    std::vector<std::string> Report(std::uint32_t a_gameThread)
    {
        const auto entries = Snapshot();
        std::map<std::string, std::map<std::uint32_t, std::size_t>> byKind;
        for (const auto& e : entries) {
            ++byKind[e.kind][e.thread];
        }
        std::vector<std::string> lines;
        lines.push_back(std::format("{} bodies in the ring ({} recorded in all, ring holds {}); stamped game thread {}",
            entries.size(), TotalRecords(), kTraceCapacity, a_gameThread));
        for (const auto& [kind, threads] : byKind) {
            std::size_t count = 0;
            std::size_t onGame = 0;
            std::string list;
            std::size_t listed = 0;
            for (const auto& [thread, n] : threads) {
                count += n;
                onGame += thread == a_gameThread ? n : 0;
                if (listed++ < kMaxThreadsListed) {
                    list += std::format("{}{}(x{})", list.empty() ? "" : ", ", thread, n);
                }
            }
            if (threads.size() > kMaxThreadsListed) {
                list += std::format(", +{} more", threads.size() - kMaxThreadsListed);
            }
            lines.push_back(std::format("{}: {} bodies on {} thread(s) [{}]; {} on the stamped game thread", kind, count,
                threads.size(), list, onGame));
        }
        const auto pairs = Overlaps(entries);
        if (pairs.empty()) {
            lines.push_back("overlaps: no two intervals on different threads overlapped");
        }
        for (const auto& [pair, count] : pairs) {
            lines.push_back(std::format("overlap: {} x {} on different threads at the same time: {}", pair.first,
                pair.second, count));
        }
        return lines;
    }
}
