#include "MainThreadQueue.h"
#include "Perf.h"

#include "BuffState.h"
#include "GameClock.h"
#include "GameThread.h"
#include "Patch.h"
#include "SehGuard.h"
#include "ThreadTrace.h"
#include "TrackedMutex.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <mutex>
#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame::MainQueue
{
    namespace
    {
        constexpr std::uint64_t kMainUpdateSe = 35565;
        constexpr std::uint64_t kMainUpdateAe = 36564;
        constexpr std::uintptr_t kSiteSe = 0x748;
        constexpr std::uintptr_t kSiteAe = 0xC26;      // AE before 1.7.99
        constexpr std::uintptr_t kSiteAe1799 = 0xC38;  // 1.7.99 and later
        constexpr std::uintptr_t kAeTailFromSite = 0x37;  // pdata end - site on 1.6.1170 and 1.7.104 (checked only there)
        constexpr auto kHeartbeat = std::chrono::seconds(5);
        constexpr int kFirstFramesLogged = 3;
        constexpr std::size_t kPendingWarn = 10000;  // E5: a queue this long means the drain is not keeping up

        struct Task
        {
            const char* name;
            std::function<void()> run;
        };

        TrackedMutex g_mutex;
        auto* g_pending = std::make_unique<std::vector<Task>>().release();  // under g_mutex; never freed (exit)
        std::atomic<std::size_t> g_pendingCount{ 0 };
        std::atomic<bool> g_installed{ false };
        std::atomic<bool> g_draining{ false };
        std::atomic<bool> g_fallbackLogged{ false };
        std::atomic<std::int64_t> g_lastFrameEnd{ 0 };
        std::atomic<bool> g_lastPaused{ false };
        std::atomic<std::uint64_t> g_frames{ 0 };
        std::atomic<std::uint64_t> g_pausedFrames{ 0 };
        std::atomic<std::uint64_t> g_bodies{ 0 };
        std::atomic<std::uint64_t> g_bodiesOffMain{ 0 };
        std::atomic<std::uint64_t> g_overlaps{ 0 };
        std::atomic<std::uint64_t> g_fallbacks{ 0 };
        std::atomic<std::uint64_t> g_jobHooksDuringDrain{ 0 };
        std::atomic<std::uint32_t> g_thread{ 0 };
        std::atomic<std::uint32_t> g_mainThreadId{ 0 };
        bool g_pausedLogged = false;  // drain thread only
        std::vector<std::string_view> g_stoppedKinds;  // under g_mutex (E9/F3): kinds that threw a non-C++ exception
        std::atomic<bool> g_pendingWarned{ false };
        std::chrono::steady_clock::time_point g_nextHeartbeat{};  // drain thread only

        // Inside the game's code segment (.text)
        bool InGameImage(std::uintptr_t a_address)
        {
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            return a_address >= text.address() && a_address < text.address() + text.size();
        }

        std::uintptr_t SiteOffset()
        {
            if (!REL::Module::IsAE()) {
                return kSiteSe;
            }
            return REL::Module::get().version() >= REL::Version(1, 7, 99, 0) ? kSiteAe1799 : kSiteAe;
        }

        // The first frames: their thread against Main::threadID; the game thread is stamped here (GameThread.h)
        void NoteFirstFrames(std::uint64_t a_frame, std::uint32_t a_thread)
        {
            if (a_frame == 1) {
                g_thread.store(a_thread);
                StampGameThread(a_thread, "the main-thread queue's first frame");
            }
            if (g_mainThreadId.load() == 0) {  // E5: re-read until the game has set it
                const auto* main = RE::Main::GetSingleton();
                const auto mainId = main ? main->threadID : 0;
                g_mainThreadId.store(mainId);
                if (mainId != 0 && mainId != a_thread) {
                    logger::warn("BlackFlame: main-thread queue: drain thread {} is not Main::threadID {} - please report",
                        a_thread, mainId);
                }
            }
            if (a_frame <= kFirstFramesLogged) {
                logger::info("BlackFlame: main-thread queue frame {} on thread {} (Main::threadID {})", a_frame, a_thread,
                    g_mainThreadId.load());
            }
        }

        void NotePaused()
        {
            auto* ui = RE::UI::GetSingleton();  // the main thread, after the frame's jobs: a safe read
            const bool paused = ui && ui->GameIsPaused();
            g_lastPaused.store(paused);
            GameClock::OnDrainFrame(paused);  // the corpse mark's unpaused seconds (GameClock.h)
            if (paused) {
                g_pausedFrames.fetch_add(1);
                if (!g_pausedLogged) {
                    g_pausedLogged = true;
                    logger::info("BlackFlame: main-thread queue: it also runs on paused frames (first at frame {})",
                        g_frames.load());
                }
            }
        }

        void Heartbeat(std::uint32_t a_thread, std::size_t a_ran)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < g_nextHeartbeat) {
                return;
            }
            g_nextHeartbeat = now + kHeartbeat;
            logger::info("BlackFlame: heartbeat: frame {}, tasks run this frame {}, waiting {}, stoked {}, paused {}, thread {} | ours "
                         "this window: {}",
                g_frames.load(), a_ran, g_pendingCount.load(), IsStoked() ? "yes" : "no", g_lastPaused.load() ? "yes" : "no",
                a_thread, Perf::TakeWindow());
        }

        // Caller holds g_mutex; compared by content (F3: the same name from two translation units may be two pointers)
        bool StoppedLocked(std::string_view a_kind)
        {
            return std::ranges::find(g_stoppedKinds, a_kind) != g_stoppedKinds.end();
        }

        bool Stopped(std::string_view a_kind)
        {
            std::lock_guard lock(g_mutex);
            return StoppedLocked(a_kind);
        }

        void StopKind(const char* a_name, const char* a_why)
        {
            {
                std::lock_guard lock(g_mutex);
                g_stoppedKinds.emplace_back(a_name);
            }
            const bool ticker = std::string_view(a_name) == "BlackFlameTick";
            logger::error("BlackFlame: main-thread task '{}' {}: tasks of this kind are not run again this session{}", a_name, a_why,
                ticker ? " - that is the ticker: the buff flag, the hand and bound weapon art, script-placed fire and the self-test "
                         "stop until the game is restarted" : "");
        }

        // True when the task ran (G5: the heartbeat counts these)
        bool RunTask(Task& a_task, std::uint32_t a_thread, std::uint32_t a_main)
        {
            if (Stopped(a_task.name)) {
                return false;  // E9: this kind threw a non-C++ exception earlier: not run again this session
            }
            const ThreadTrace::Scope trace(a_task.name);
            LogThread(a_task.name, "task");
            g_bodies.fetch_add(1);
            if (a_main != 0 && a_thread != a_main) {
                g_bodiesOffMain.fetch_add(1);
            }
            try {
                // G4: the build uses /EHa, so a hardware fault would reach catch (...) with nothing said about where;
                // under the SEH guard it is logged with its code and module+offset (SehGuard.h) and the kind stops
                if (!Guarded(a_task.name, [&]() { a_task.run(); })) {
                    StopKind(a_task.name, "faulted (hardware exception, logged above)");
                }
            } catch (const std::exception& e) {
                logger::error("BlackFlame: main-thread task '{}' threw: {}", a_task.name, e.what());
            } catch (...) {
                // E9: another exception type: its state may be broken, so this kind stops for the session
                StopKind(a_task.name, "threw an exception of an unknown type");
            }
            return true;
        }

        // Marks the frame's end even when a task escapes the per-task catch (the watchdog's stall signal)
        struct FrameEnd
        {
            ~FrameEnd()
            {
                g_lastFrameEnd.store(ThreadTrace::Now());
                g_draining.store(false);
            }
        };

        void Drain()
        {
            if (g_draining.exchange(true)) {
                g_overlaps.fetch_add(1);
                return;
            }
            const Perf::Scope perf(Perf::kDrain);  // perf audit 2: our share of the frame (heartbeat line)
            const FrameEnd frameEnd;
            const auto drainStart = ThreadTrace::Now();
            const auto thread = REX::W32::GetCurrentThreadId();
            const auto frame = g_frames.fetch_add(1) + 1;
            NoteFirstFrames(frame, thread);
            NotePaused();
            std::vector<Task> batch;
            {
                std::lock_guard lock(g_mutex);
                batch.swap(*g_pending);
                g_pendingCount.store(0);
            }
            const auto main = g_mainThreadId.load();
            std::size_t ran = 0;
            for (auto& task : batch) {
                ran += RunTask(task, thread, main) ? 1 : 0;
            }
            if (ran > 0) {  // G1: only drains that ran tasks go into the trace ring (it holds ~30 s of every frame otherwise)
                ThreadTrace::Record(kDrainKind, thread, drainStart, ThreadTrace::Now());
            }
            Heartbeat(thread, ran);
        }

        struct MainUpdateHook
        {
            static void thunk()
            {
                func();
                try {
                    Drain();
                } catch (...) {
                    // nothing may leave Main::Update (G4: logged once; a task's own failure was logged by its catch)
                    static std::atomic<bool> logged{ false };
                    if (!logged.exchange(true)) {
                        logger::error("BlackFlame: main-thread queue: the drain threw outside a task; further failures are not "
                                      "logged");
                    }
                }
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

        bool SiteOk(std::uintptr_t a_function, std::uintptr_t a_site, bool& a_chained)
        {
            const auto bytes = Patch::HexBytes(a_site, Patch::kRel32Size);
            if (*reinterpret_cast<const std::uint8_t*>(a_site) != Patch::kCallOpcode) {
                logger::warn("BlackFlame: main-thread queue: Main::Update +0x{:X} is not a call ({}) - not hooked",
                    a_site - a_function, bytes);
                return false;
            }
            const auto target = Patch::Rel32Target(a_site);
            a_chained = Patch::IsTrampolineStub(target);
            if (!a_chained && !(InGameImage(target) && Patch::IsExecutable(target, 1))) {
                logger::warn("BlackFlame: main-thread queue: the call at Main::Update +0x{:X} goes to {:X}, neither the game "
                             "image nor a trampoline stub - not hooked", a_site - a_function, target);
                return false;
            }
            // The site offsets are the community's for every runtime (OAR, Precision, TDM: SE 0x748, AE 0xC26, 1.7.99+ 0xC38);
            // the E8 and its target are checked everywhere. The tail anchor was measured on 1.6.1170 and 1.7.104 only: it is
            // enforced there and logged elsewhere (another AE build may end the function at another distance)
            if (REL::Module::IsAE()) {
                DWORD64 imageBase = 0;
                const auto* entry = RtlLookupFunctionEntry(a_function, &imageBase, nullptr);
                const auto end = entry ? static_cast<std::uintptr_t>(imageBase + entry->EndAddress) : 0;
                if (!Patch::IsCodeReadBuild()) {
                    logger::info("BlackFlame: main-thread queue: Main::Update ends {} after the site on this build (0x{:X} on "
                                 "1.6.1170 / 1.7.104; not enforced here)", end ? std::format("0x{:X}", end - a_site) : std::string("(no pdata)"),
                        kAeTailFromSite);
                } else if (end == 0 || end - a_site != kAeTailFromSite) {
                    logger::warn("BlackFlame: main-thread queue: Main::Update ends {} bytes after the site, not 0x{:X} - another "
                                 "build; not hooked", end ? std::format("0x{:X}", end - a_site) : std::string("(no pdata)"),
                        kAeTailFromSite);
                    return false;
                }
            }
            return true;
        }
    }

    void Install()
    {
        const auto id = Patch::IdFor(kMainUpdateSe, kMainUpdateAe);
        if (!Patch::HasIds({ id }, "main-thread queue (Main::Update)")) {
            return;
        }
        const auto function = Patch::AddressOf(id);
        const auto site = function + SiteOffset();
        bool chained = false;
        if (!SiteOk(function, site, chained)) {
            Patch::RecordHook("main-thread queue (Main::Update)", false, site, Patch::HexBytes(site, Patch::kRel32Size),
                "the site check failed (see the log)");
            return;
        }
        MainUpdateHook::func = Patch::WriteCall("main-thread queue (Main::Update)", site, MainUpdateHook::thunk,
            std::format("Main::Update +0x{:X}{}", site - function, chained ? ", chained" : ""));
        g_installed.store(true);
        logger::info("BlackFlame: main-thread queue hooked at Main::Update +0x{:X} ({:X}){}", site - function,
            site - REL::Module::get().base(), chained ? ", chained onto another plugin's redirect" : "");
    }

    bool Installed() noexcept
    {
        return g_installed.load();
    }

    bool Draining() noexcept
    {
        return g_draining.load(std::memory_order_relaxed);
    }

    void NoteJobSideHook() noexcept
    {
        if (g_draining.load(std::memory_order_relaxed) && REX::W32::GetCurrentThreadId() != g_mainThreadId.load()) {
            g_jobHooksDuringDrain.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool Post(const char* a_name, std::function<void()> a_task)
    {
        if (!g_installed.load()) {
            g_fallbacks.fetch_add(1);
            if (!g_fallbackLogged.exchange(true)) {
                logger::warn("BlackFlame: main-thread queue not installed: tasks go to SKSE's AddTask (worker threads "
                             "during play)");
            }
            AddTaskToGameThread(a_name, [a_name, task = std::move(a_task)]() {
                const ThreadTrace::Scope trace(a_name);
                LogThread(a_name, "task (SKSE fallback)");
                task();
            });
            return true;
        }
        std::lock_guard lock(g_mutex);
        if (StoppedLocked(a_name)) {
            return false;  // F3/G3: a stopped kind is not queued either; the caller clears its own flag
        }
        g_pending->push_back({ a_name, std::move(a_task) });
        g_pendingCount.store(g_pending->size());
        if (g_pending->size() > kPendingWarn && !g_pendingWarned.exchange(true)) {
            logger::warn("BlackFlame: main-thread queue: {} tasks waiting (last '{}') - the drain is not keeping up",
                g_pending->size(), a_name);
        }
        return true;
    }

    Stats GetStats() noexcept
    {
        return { g_frames.load(), g_pausedFrames.load(), g_bodies.load(), g_bodiesOffMain.load(), g_overlaps.load(),
            g_fallbacks.load(), g_jobHooksDuringDrain.load(), g_thread.load(), g_mainThreadId.load() };
    }

    std::int64_t LastFrameEnd() noexcept
    {
        return g_lastFrameEnd.load();
    }

    bool LastFramePaused() noexcept
    {
        return g_lastPaused.load();
    }

    std::size_t Pending() noexcept
    {
        return g_pendingCount.load();
    }
}
