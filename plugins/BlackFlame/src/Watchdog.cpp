#include "Watchdog.h"

#include "MainThreadQueue.h"
#include "ThreadTrace.h"
#include "TrackedMutex.h"

#include <algorithm>
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
        // E5 / H6: the queue is installed but never ran. Startup to the main menu took 3 min 20 s on a 4,500-mod order
        // (run 580e315-auto2: kDataLoaded 21:42:09, first frame 21:45:29), so the first frame gets 300 s
        constexpr auto kFirstFrameTime = std::chrono::seconds(300);

        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_stop{ false };  // set at process exit (atexit): no flush after it
        std::atomic<bool> g_loading{ false };
        std::atomic<bool> g_stallReported{ false };
        std::atomic<std::int64_t> g_stallMark{ 0 };  // the last frame end the reported stall measured from
        std::atomic<std::int64_t> g_startedAt{ 0 };
        // H6: a load's end; until the first frame after it the loading limit still applies and the time counts from it
        std::atomic<std::int64_t> g_loadEndedAt{ 0 };
        std::atomic<std::int64_t> g_frameAtLoadEnd{ 0 };  // LastSign() when the load ended

        double SecondsAgo(std::int64_t a_ticks, std::int64_t a_now)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::duration(a_now - a_ticks)).count();
        }

        // The main-thread queue's last frame (it runs on paused frames too); without the queue, the last ticker body
        std::int64_t LastSign() noexcept
        {
            return MainQueue::Installed() ? MainQueue::LastFrameEnd() : ThreadTrace::LastTickEnd();
        }

        void ReportStall(std::int64_t a_last, std::int64_t a_now, bool a_loading, bool a_neverRan = false)
        {
            if (a_neverRan) {
                logger::warn("BlackFlame: STALL: the main-thread queue is installed but has run no frame in the {:.1f} s since the "
                             "watchdog started ({}). Last {} trace entries, bodies started and not ended, held mutexes follow",
                    SecondsAgo(a_last, a_now), a_loading ? "a load is under way" : "no load under way", kLastEntries);
            } else {
                logger::warn("BlackFlame: STALL: no {} for {:.1f} s ({}; the last frame was {}). Last {} trace entries, bodies "
                             "started and not ended, held mutexes follow", MainQueue::Installed() ? "main-thread frame" : "ticker body",
                    SecondsAgo(a_last, a_now), a_loading ? "a load is under way" : "no load under way",
                    MainQueue::LastFramePaused() ? "paused" : "not paused", kLastEntries);
            }
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
                        // E5: installed but no frame 60 s after the watchdog started: one STALL block
                        if (MainQueue::Installed() && !g_stallReported.load() &&
                            std::chrono::steady_clock::duration(now - g_startedAt.load()) > kFirstFrameTime) {
                            g_stallReported.store(true);
                            ReportStall(g_startedAt.load(), now, g_loading.load(), true);
                        }
                        continue;
                    }
                    if (g_stallReported.load() && last != g_stallMark.load()) {
                        g_stallReported.store(false);
                        logger::warn("BlackFlame: stall over: the main thread ran a frame again");
                        FlushLog();
                        continue;
                    }
                    const bool loading = g_loading.load();
                    // H6: after a load ends, no frame yet: still the loading limit (grace), measured from the load's end
                    const auto loadEnded = g_loadEndedAt.load();
                    const bool grace = loadEnded != 0 && last == g_frameAtLoadEnd.load();
                    const auto from = std::max(last, loadEnded);
                    const auto limit = loading || grace ? kLoadingStallTime : kStallTime;
                    if (!g_stallReported.load() && std::chrono::steady_clock::duration(now - from) > limit) {
                        g_stallReported.store(true);
                        g_stallMark.store(last);
                        ReportStall(from, now, loading || grace);
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
        g_startedAt.store(ThreadTrace::Now());
        std::thread(&Loop).detach();
        logger::info("BlackFlame: watchdog started (log flushed every {} s; a STALL block after {} s without a main-thread "
                     "frame, {} s while a load is under way or until the first frame after it, {} s before the session's first "
                     "frame)", kPeriod.count(), kStallTime.count(), kLoadingStallTime.count(), kFirstFrameTime.count());
    }

    void NoteLoadStarted() noexcept
    {
        g_loading.store(true);
    }

    bool LoadUnderWay() noexcept
    {
        return g_loading.load();
    }

    void NoteLoadEnded() noexcept
    {
        g_frameAtLoadEnd.store(LastSign());
        g_loadEndedAt.store(ThreadTrace::Now());
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
