// =============================================================================
// MenuWatch - the MenuOpenCloseEvent sink, always registered: W2 opens at the main menu, closes at a load
// =============================================================================

#include "MenuWatch.h"

#include "GameThread.h"
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
        std::atomic<bool> g_proven{ false };
        std::atomic<std::uint32_t> g_mainThread{ 0 };

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
            if (g_menuOpened.exchange(true)) {
                return;
            }
            {
                std::lock_guard lock(g_mutex);
                g_menuAt = Clock::now();
            }
            const auto thread = REX::W32::GetCurrentThreadId();
            logger::info("BlackFlame: menu watch: the main menu opened (W2 {}) on thread {} ({})",
                g_closed.load() ? "not opened: the publish window is already closed" : "begins",
                thread, thread == g_mainThread.load() ? "Main's thread" : "NOT Main's thread");
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
                } catch (...) {
                    logger::error("BlackFlame: menu watch: the menu event handler threw");
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        Sink g_sink;
    }

    void Install()
    {
        if (g_installed.exchange(true)) {
            return;
        }
        const auto* main = RE::Main::GetSingleton();
        const auto mainId = main ? main->threadID : 0;
        const auto loaded = g_dataLoadedThreadNativeId.load();
        const bool queue = MainQueue::Installed();
        const bool proven = queue && mainId != 0 && loaded == mainId;
        g_mainThread.store(mainId);
        g_proven.store(proven);
        {
            std::lock_guard lock(g_mutex);
            g_proofText = std::format("queue installed {}, kDataLoaded thread {}, Main::threadID {}: {}", queue ? "yes" : "NO",
                loaded, mainId, proven ? "the drains may publish (W1 drains and W2)" :
                                         "publication at kDataLoaded itself only (no drain is proven to be the main thread)");
        }
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
        return g_proven.load();
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
        return PublishOpen() && g_proven.load() && REX::W32::GetCurrentThreadId() == g_mainThread.load();
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
        if (g_closed.exchange(true)) {
            return;
        }
        {
            std::lock_guard lock(g_mutex);
            g_closeReason = a_reason;
        }
        const auto thread = REX::W32::GetCurrentThreadId();
        logger::info("BlackFlame: menu watch: the publish window closed for the session: {} (thread {}, {}; the main menu {})",
            a_reason, thread, thread == g_mainThread.load() ? "Main's thread" : "NOT Main's thread",
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
        a_listener();
    }
}
