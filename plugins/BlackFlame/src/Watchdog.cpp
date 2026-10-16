#include "Watchdog.h"

#include "ThreadTrace.h"
#include "TrackedMutex.h"

#include <atomic>
#include <chrono>
#include <format>
#include <thread>

namespace BlackFlame::Watchdog
{
    namespace
    {
        constexpr auto kPeriod = std::chrono::seconds(1);
        constexpr auto kStallTime = std::chrono::seconds(10);
        constexpr std::size_t kLastEntries = 64;

        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_loading{ false };
        std::atomic<bool> g_stallReported{ false };
        std::atomic<std::int64_t> g_stallTick{ 0 };  // the last tick end the reported stall measured from

        double SecondsAgo(std::int64_t a_ticks, std::int64_t a_now)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::duration(a_now - a_ticks)).count();
        }

        void ReportStall(std::int64_t a_lastTick, std::int64_t a_now)
        {
            logger::warn("BlackFlame: STALL: no ticker body finished for {:.1f} s (not loading, not paused). Last {} trace "
                         "entries, bodies started and not ended, held mutexes follow", SecondsAgo(a_lastTick, a_now),
                kLastEntries);
            for (const auto& e : ThreadTrace::LastEntries(kLastEntries)) {
                logger::warn("BlackFlame: STALL trace: {} {} thread {} started {:.3f} s ago{}", e.end == 0 ? "START" : "END  ",
                    e.kind, e.thread, SecondsAgo(e.start, a_now),
                    e.end == 0 ? std::string() : std::format(", took {:.3f} s", SecondsAgo(e.start, e.end)));
            }
            const auto open = ThreadTrace::OpenBodies();
            if (open.empty()) {
                logger::warn("BlackFlame: STALL open bodies: none (no thread is inside our traced code)");
            }
            for (const auto& body : open) {
                logger::warn("BlackFlame: STALL open body: {} on thread {} since {:.3f} s", body.kind, body.thread,
                    SecondsAgo(body.start, a_now));
            }
            const auto held = HeldTrackedMutexes();
            if (held.empty()) {
                logger::warn("BlackFlame: STALL mutexes: none of ours is held");
            }
            for (const auto& line : held) {
                logger::warn("BlackFlame: STALL mutex: {}", line);
            }
            logger::warn("BlackFlame: STALL: take the stacks of the hung game without closing it: cdbX64 -pv -p <pid> -c "
                         "\"~*kb 30;q\" (README \"Known limits\")");
            FlushLog();
        }

        bool GamePaused()
        {
            auto* ui = RE::UI::GetSingleton();  // read only from this thread: a counter, a stale read is harmless
            return ui && ui->GameIsPaused();
        }

        void Loop()
        {
            for (;;) {
                std::this_thread::sleep_for(kPeriod);
                try {
                    FlushLog();
                    const auto last = ThreadTrace::LastTickEnd();
                    const auto now = ThreadTrace::Now();
                    if (last == 0) {
                        continue;
                    }
                    if (g_stallReported.load() && last != g_stallTick.load()) {
                        g_stallReported.store(false);
                        logger::warn("BlackFlame: stall over: a ticker body finished again");
                        FlushLog();
                        continue;
                    }
                    const bool stalled = std::chrono::steady_clock::duration(now - last) > kStallTime;
                    if (stalled && !g_stallReported.load() && !g_loading.load() && !GamePaused()) {
                        g_stallReported.store(true);
                        g_stallTick.store(last);
                        ReportStall(last, now);
                    }
                } catch (...) {
                    // a diagnostic thread must never end the process
                }
            }
        }
    }

    void Start()
    {
        if (g_started.exchange(true)) {
            return;
        }
        std::thread(&Loop).detach();
        logger::info("BlackFlame: watchdog started (log flushed every {} s; a STALL block after {} s without a ticker body)",
            kPeriod.count(), kStallTime.count());
    }

    void NoteLoadStarted() noexcept
    {
        g_loading.store(true);
    }

    void NoteLoadEnded() noexcept
    {
        g_loading.store(false);
    }

    void SetFlushEveryLine(bool a_on) noexcept
    {
        try {
            spdlog::flush_on(a_on ? spdlog::level::info : kLogFlushImmediateLevel);
        } catch (...) {
        }
    }
}
