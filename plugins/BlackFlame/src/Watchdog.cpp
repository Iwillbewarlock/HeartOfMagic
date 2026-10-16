#include "Watchdog.h"

#include "MainThreadQueue.h"
#include "ThreadTrace.h"
#include "TrackedMutex.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <thread>

namespace BlackFlame::Watchdog
{
    namespace
    {
        constexpr auto kPeriod = std::chrono::seconds(1);
        constexpr auto kStallTime = std::chrono::seconds(10);
        constexpr auto kLoadingStallTime = std::chrono::seconds(120);  // a save load may hold the main thread that long
        constexpr std::size_t kLastEntries = 64;

        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_stop{ false };  // set at process exit (atexit): no flush after it
        std::atomic<bool> g_loading{ false };
        std::atomic<bool> g_stallReported{ false };
        std::atomic<std::int64_t> g_stallMark{ 0 };  // the last frame end the reported stall measured from

        double SecondsAgo(std::int64_t a_ticks, std::int64_t a_now)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::duration(a_now - a_ticks)).count();
        }

        // The main-thread queue's last frame (it runs on paused frames too); without the queue, the last ticker body
        std::int64_t LastSign() noexcept
        {
            return MainQueue::Installed() ? MainQueue::LastFrameEnd() : ThreadTrace::LastTickEnd();
        }

        void ReportStall(std::int64_t a_last, std::int64_t a_now, bool a_loading)
        {
            logger::warn("BlackFlame: STALL: no {} for {:.1f} s ({}; the last frame was {}). Last {} trace entries, bodies "
                         "started and not ended, held mutexes follow", MainQueue::Installed() ? "main-thread frame" : "ticker body",
                SecondsAgo(a_last, a_now), a_loading ? "a load is under way" : "no load under way",
                MainQueue::LastFramePaused() ? "paused" : "not paused", kLastEntries);
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
                         "\"~*kb 30;q\" with _NT_SYMBOL_PATH set to the PDB folder (README \"Known limits\")");
            FlushLog();
        }

        // No RE:: read here (D2): this thread must not touch engine data that may be mid-change
        void Loop()
        {
            for (;;) {
                std::this_thread::sleep_for(kPeriod);
                if (g_stop.load()) {
                    return;
                }
                try {
                    FlushLog();
                    const auto last = LastSign();
                    const auto now = ThreadTrace::Now();
                    if (last == 0) {
                        continue;  // no frame yet
                    }
                    if (g_stallReported.load() && last != g_stallMark.load()) {
                        g_stallReported.store(false);
                        logger::warn("BlackFlame: stall over: the main thread ran a frame again");
                        FlushLog();
                        continue;
                    }
                    const bool loading = g_loading.load();
                    const auto limit = loading ? kLoadingStallTime : kStallTime;
                    if (!g_stallReported.load() && std::chrono::steady_clock::duration(now - last) > limit) {
                        g_stallReported.store(true);
                        g_stallMark.store(last);
                        ReportStall(last, now, loading);
                    }
                } catch (...) {
                    // a diagnostic thread must never end the process
                }
            }
        }

        void StopAtExit()
        {
            g_stop.store(true);
        }
    }

    void Start()
    {
        if (g_started.exchange(true)) {
            return;
        }
        // Registered after spdlog's statics exist: runs before they are destroyed, so the loop stops flushing first
        (void)std::atexit(&StopAtExit);
        std::thread(&Loop).detach();
        logger::info("BlackFlame: watchdog started (log flushed every {} s; a STALL block after {} s without a main-thread "
                     "frame, {} s while a load is under way)", kPeriod.count(), kStallTime.count(), kLoadingStallTime.count());
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
