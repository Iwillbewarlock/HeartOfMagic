#include "MainThreadQueue.h"

#include "BuffState.h"
#include "GameThread.h"
#include "Patch.h"
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
        constexpr std::uintptr_t kAeTailFromSite = 0x37;  // pdata end - site on 1.6.1170 and 1.7.104
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
        std::atomic<std::uint32_t> g_thread{ 0 };
        std::atomic<std::uint32_t> g_mainThreadId{ 0 };
        bool g_pausedLogged = false;  // drain thread only
        std::vector<const char*> g_stoppedKinds;  // drain thread only (E9): kinds that threw a non-C++ exception
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
            logger::info("BlackFlame: heartbeat: frame {}, tasks run this frame {}, waiting {}, stoked {}, paused {}, thread {}",
                g_frames.load(), a_ran, g_pendingCount.load(), IsStoked() ? "yes" : "no", g_lastPaused.load() ? "yes" : "no",
                a_thread);
        }

        bool Stopped(const char* a_kind)
        {
            return std::ranges::find(g_stoppedKinds, a_kind) != g_stoppedKinds.end();
        }

        void RunTask(Task& a_task, std::uint32_t a_thread, std::uint32_t a_main)
        {
            if (Stopped(a_task.name)) {
                return;  // E9: this kind threw a non-C++ exception earlier: not run again this session
            }
            const ThreadTrace::Scope trace(a_task.name);
            LogThread(a_task.name, "task");
            g_bodies.fetch_add(1);
            if (a_main != 0 && a_thread != a_main) {
                g_bodiesOffMain.fetch_add(1);
            }
            try {
                a_task.run();
            } catch (const std::exception& e) {
                logger::error("BlackFlame: main-thread task '{}' threw: {}", a_task.name, e.what());
            } catch (...) {
                // E9: not a C++ exception of ours: its state may be broken, so this kind stops for the session
                g_stoppedKinds.push_back(a_task.name);
                logger::error("BlackFlame: main-thread task '{}' threw a non-C++ exception: tasks of this kind are not run again "
                              "this session", a_task.name);
            }
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
            const FrameEnd frameEnd;
            const ThreadTrace::Scope drainTrace(kDrainKind);  // E7: hooks starting inside it are counted by T-threads
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
            for (auto& task : batch) {
                RunTask(task, thread, main);
            }
            Heartbeat(thread, batch.size());
        }

        struct MainUpdateHook
        {
            static void thunk()
            {
                func();
                try {
                    Drain();
                } catch (...) {
                    // nothing may leave Main::Update; the per-task catch already logged a task's failure
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
            if (REL::Module::IsAE()) {
                DWORD64 imageBase = 0;
                const auto* entry = RtlLookupFunctionEntry(a_function, &imageBase, nullptr);
                const auto end = entry ? static_cast<std::uintptr_t>(imageBase + entry->EndAddress) : 0;
                if (end == 0 || end - a_site != kAeTailFromSite) {
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
            return;
        }
        MainUpdateHook::func = SKSE::GetTrampoline().write_call<5>(site, MainUpdateHook::thunk);
        g_installed.store(true);
        logger::info("BlackFlame: main-thread queue hooked at Main::Update +0x{:X} ({:X}){}", site - function,
            site - REL::Module::get().base(), chained ? ", chained onto another plugin's redirect" : "");
    }

    bool Installed() noexcept
    {
        return g_installed.load();
    }

    void Post(const char* a_name, std::function<void()> a_task)
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
            return;
        }
        std::lock_guard lock(g_mutex);
        g_pending->push_back({ a_name, std::move(a_task) });
        g_pendingCount.store(g_pending->size());
        if (g_pending->size() > kPendingWarn && !g_pendingWarned.exchange(true)) {
            logger::warn("BlackFlame: main-thread queue: {} tasks waiting (last '{}') - the drain is not keeping up",
                g_pending->size(), a_name);
        }
    }

    Stats GetStats() noexcept
    {
        return { g_frames.load(), g_pausedFrames.load(), g_bodies.load(), g_bodiesOffMain.load(), g_overlaps.load(),
            g_fallbacks.load(), g_thread.load(), g_mainThreadId.load() };
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
