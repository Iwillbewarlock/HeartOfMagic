// =============================================================================
// MenuWatch - the MenuOpenCloseEvent sink, always registered: W2 opens at the main menu, closes at a load
// =============================================================================

#include "MenuWatch.h"

#include "GameThread.h"
#include "MainThreadProof.h"
#include "MainThreadQueue.h"
#include "TrackedMutex.h"

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

namespace BlackFlame::MenuWatch
{
    namespace
    {
        constexpr int kEventThreadLogs = 1;  // the menu event's thread, once (design v3.2 §5)

        std::atomic<bool> g_installed{ false };
        std::atomic<bool> g_closed{ false };
        std::atomic<bool> g_menuOpened{ false };
        using MainThreadProof::Verdict;
        std::atomic<Verdict> g_verdict{ Verdict::kRefused };  // Install sets kPending when the queue is installed
        std::atomic<std::uint32_t> g_mainThread{ 0 };
        std::uint32_t g_loadedThread = 0;  // kDataLoaded's thread (logged with the proof; not part of it)

        TrackedMutex g_mutex;  // the texts, the open time and the listeners
        std::string g_proofText = "not made (Install did not run)";
        std::string g_closeReason;
        std::optional<Clock::time_point> g_menuAt;
        auto* g_listeners = std::make_unique<std::vector<std::function<void()>>>().release();

        void RunListeners()
        {
            std::vector<std::function<void()>> listeners;
            {
                std::lock_guard lock(g_mutex);
                listeners.swap(*g_listeners);
            }
            for (auto& listener : listeners) {
                try {
                    listener();
                } catch (const std::exception& e) {
                    logger::error("BlackFlame: menu watch: a main menu listener threw: {}", e.what());
                } catch (...) {
                    logger::error("BlackFlame: menu watch: a main menu listener threw an exception of an unknown type");
                }
            }
        }

        void NoteMainMenuOpen()
        {
            {
                // review Gemini / agy 11: the time first, the flag after, both under the lock OnMainMenuOpen takes - a reader
                // that sees MainMenuOpened() also gets MainMenuOpenedAt()
                std::lock_guard lock(g_mutex);
                if (g_menuAt) {
                    return;
                }
                g_menuAt = Clock::now();
                g_menuOpened.store(true);
            }
            try {  // agy 11 #6: a throw in the log line must not keep the listeners from running
                const auto thread = REX::W32::GetCurrentThreadId();
                logger::info("BlackFlame: menu watch: the main menu opened (W2 {}) on thread {} ({})",
                    g_closed.load() ? "not opened: the publish window is already closed" : "begins",
                    thread, thread == g_mainThread.load() ? "Main's thread" : "NOT Main's thread");
            } catch (...) {
            }
            RunListeners();
        }

        class Sink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                try {
                    LogThread("menu watch MenuOpenCloseEvent", "event", kEventThreadLogs);
                    const bool mainMenu = a_event->menuName == RE::MainMenu::MENU_NAME;
                    if (mainMenu && a_event->opening) {
                        NoteMainMenuOpen();
                    } else if (g_menuOpened.load() && !g_closed.load()) {
                        if (mainMenu && !a_event->opening) {
                            Close("the main menu closed");
                        } else if (a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME) {
                            Close("the loading menu opened");
                        }
                    }
                } catch (const std::exception& e) {
                    logger::error("BlackFlame: menu watch: the menu event handler threw: {}", e.what());
                } catch (...) {
                    logger::error("BlackFlame: menu watch: the menu event handler threw an exception of an unknown type");
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        Sink g_sink;

        // The proof, made in the first drain that asks (before any publish step: DrainMayPublish is each step's first check)
        Verdict Proof() noexcept
        {
            auto verdict = g_verdict.load();
            if (verdict != Verdict::kPending) {
                return verdict;
            }
            try {
                const auto stats = MainQueue::GetStats();
                const auto* main = RE::Main::GetSingleton();
                const auto mainId = main ? main->threadID : stats.mainThreadId;
                const auto decided = MainThreadProof::Decide(MainQueue::Installed(), stats.frames, stats.thread, mainId);
                if (decided == Verdict::kPending || !g_verdict.compare_exchange_strong(verdict, decided)) {
                    return g_verdict.load();
                }
                g_mainThread.store(mainId);
                const bool proven = decided == Verdict::kProven;
                {
                    std::lock_guard lock(g_mutex);
                    g_proofText = std::format("queue installed yes, first drain thread {}, Main::threadID {} (kDataLoaded thread {}, not "
                                              "part of the proof): {}", stats.thread, mainId, g_loadedThread,
                        proven ? "the drains may publish (W1 drains and W2)" :
                                 "the drains do not publish this session (only kDataLoaded itself published)");
                }
                logger::info("BlackFlame: menu watch: main-thread proof made in a drain on thread {}: {}", REX::W32::GetCurrentThreadId(),
                    ProofText());
                return decided;
            } catch (...) {
                return g_verdict.load();
            }
        }
    }

    void Install()
    {
        static std::atomic<bool> s_once{ false };
        if (s_once.exchange(true)) {
            return;
        }
        const auto* main = RE::Main::GetSingleton();
        const auto mainId = main ? main->threadID : 0;
        const auto loaded = g_dataLoadedThreadNativeId.load();
        const bool queue = MainQueue::Installed();
        g_mainThread.store(mainId);
        {
            std::lock_guard lock(g_mutex);
            g_loadedThread = loaded;
            g_proofText = std::format("queue installed {}, kDataLoaded thread {}, Main::threadID {}: {}", queue ? "yes" : "NO",
                loaded, mainId, queue ? "made at the first drain (its thread must be Main::threadID; kDataLoaded's thread is not part of it)" :
                                        "publication at kDataLoaded itself only (no main-thread drain)");
        }
        g_verdict.store(queue ? Verdict::kPending : Verdict::kRefused);
        g_installed.store(true);  // review agy 11 #5: the window opens once the proof is set up
        logger::info("BlackFlame: menu watch: main-thread proof: {}", ProofText());
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_sink);
            logger::info("BlackFlame: menu watch: MenuOpenCloseEvent sink registered; the publish window is open until the first "
                         "save load or new game");
        } else {
            logger::error("BlackFlame: menu watch: no RE::UI - the main menu cannot be seen (W2 never opens; only kDataLoaded publishes)");
        }
    }

    bool MainThreadProven() noexcept
    {
        return Proof() == Verdict::kProven;
    }

    bool DrainsMayProve() noexcept
    {
        return Proof() != Verdict::kRefused;
    }

    std::string ProofText()
    {
        std::lock_guard lock(g_mutex);
        return g_proofText;
    }

    bool PublishOpen() noexcept
    {
        return g_installed.load() && !g_closed.load();
    }

    bool DrainMayPublish() noexcept
    {
        return PublishOpen() && Proof() == Verdict::kProven && REX::W32::GetCurrentThreadId() == g_mainThread.load();
    }

    bool MainMenuOpened() noexcept
    {
        return g_menuOpened.load();
    }

    std::optional<Clock::time_point> MainMenuOpenedAt()
    {
        std::lock_guard lock(g_mutex);
        return g_menuAt;
    }

    void Close(const char* a_reason)
    {
        {
            // review Gemini / agy 11: the reason before the flag, so a reader that sees the window closed gets why
            std::lock_guard lock(g_mutex);
            if (g_closed.load()) {
                return;
            }
            g_closeReason = a_reason ? a_reason : "unknown";
            g_closed.store(true);
        }
        const auto thread = REX::W32::GetCurrentThreadId();
        logger::info("BlackFlame: menu watch: the publish window closed for the session: {} (thread {}, {}; the main menu {})",
            a_reason ? a_reason : "unknown", thread, thread == g_mainThread.load() ? "Main's thread" : "NOT Main's thread",
            g_menuOpened.load() ? "had opened" : "had not opened");
    }

    std::string CloseReason()
    {
        std::lock_guard lock(g_mutex);
        return g_closeReason;
    }

    void OnMainMenuOpen(std::function<void()> a_listener)
    {
        if (!a_listener) {
            return;
        }
        {
            std::lock_guard lock(g_mutex);
            if (!g_menuOpened.load()) {
                g_listeners->push_back(std::move(a_listener));
                return;
            }
        }
        try {
            a_listener();  // the main menu is already open: at once
        } catch (const std::exception& e) {
            logger::error("BlackFlame: menu watch: a main menu listener threw: {}", e.what());
        } catch (...) {
            logger::error("BlackFlame: menu watch: a main menu listener threw an exception of an unknown type");
        }
    }
}
