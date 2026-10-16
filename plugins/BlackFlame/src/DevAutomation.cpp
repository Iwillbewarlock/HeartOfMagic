#include "DevAutomation.h"

#include "Config.h"
#include "MainThreadQueue.h"
#include "Patch.h"
#include "SelfTestReload.h"
#include "TrackedMutex.h"

#include <atomic>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace BlackFlame::DevAutomation
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        enum class E3Phase
        {
            kIdle,
            kAsked,       // E3 asked: the save comes kBeforeSave later
            kSaveIssued,  // waiting for kSaveGame (plus kAfterSave) or kSaveSignalWait
            kLoadIssued   // waiting for the load to end
        };

        enum class Action
        {
            kLoadConfigSave,
            kSaveE3,
            kLoadE3,
            kQuit,
            kQuitOnTimeout
        };

        // Shared by the message handler, the menu sink, the self-test and the polling thread: under g_mutex
        struct State
        {
            std::optional<Clock::time_point> menuAt;  // the main menu's first opening
            bool loadStarted = false;                 // any load this session (by hand or ours)
            bool autoLoadIssued = false;
            E3Phase e3 = E3Phase::kIdle;
            Clock::time_point e3At{};                 // when the current E3 phase began
            std::optional<Clock::time_point> saveSeenAt;
            std::optional<Clock::time_point> quitAt;
            std::optional<Clock::time_point> stallFrom;  // the last load's end or the auto load's request
            bool quitIssued = false;
        };
        TrackedMutex g_mutex;
        State g_state;
        std::atomic_bool g_installed = false;  // set at kDataLoaded (message thread), read from the game thread too

        bool AnyKeyOn(const Config& a_config)
        {
            return !a_config.devAutoLoad.empty() || a_config.devAutoE3 || a_config.devAutoQuit;
        }

        void Log(const std::string& a_line)
        {
            logger::info("BlackFlame: dev automation: {}", a_line);
            FlushLog();
        }

        void PostOrLog(const char* a_name, std::function<void()> a_task)
        {
            if (!MainQueue::Post(a_name, std::move(a_task))) {
                logger::error("BlackFlame: dev automation: '{}' was not queued (the main-thread queue refused it)", a_name);
                FlushLog();
            }
        }

        void SetQuit()
        {
            auto* main = RE::Main::GetSingleton();
            if (!main) {
                logger::error("BlackFlame: dev automation: no RE::Main - the game is not quit");
                return;
            }
            main->GetRuntimeData().quitGame = true;
        }

        void Run(Action a_action)
        {
            const auto& config = GetConfig();
            switch (a_action) {
            case Action::kLoadConfigSave:
                Log(std::format("loading save '{}' (devAutoLoad, {} s after the main menu opened)", config.devAutoLoad,
                    kMenuSettle.count()));
                PostOrLog("dev auto load", [name = config.devAutoLoad]() {
                    // checkForMods false: a missing-content prompt would wait for a click nobody makes
                    RE::BGSSaveLoadManager::GetSingleton()->Load(name.c_str(), false);
                    Log(std::format("Load('{}') called on the main thread", name));
                });
                break;
            case Action::kSaveE3:
                Log(std::format("E3: saving '{}' (devAutoE3)", kE3Save));
                PostOrLog("dev auto E3 save", []() {
                    RE::BGSSaveLoadManager::GetSingleton()->Save(kE3Save);
                    Log(std::format("E3: Save('{}') called on the main thread", kE3Save));
                });
                break;
            case Action::kLoadE3:
                Log(std::format("E3: loading '{}' (devAutoE3)", kE3Save));
                PostOrLog("dev auto E3 load", []() {
                    RE::BGSSaveLoadManager::GetSingleton()->Load(kE3Save, false);
                    Log(std::format("E3: Load('{}') called on the main thread", kE3Save));
                });
                break;
            case Action::kQuit:
            case Action::kQuitOnTimeout:
                if (a_action == Action::kQuitOnTimeout) {
                    logger::warn("BlackFlame: dev automation: TIMEOUT: no self-test end within {} minutes of the last load "
                                 "(or of the auto load's request) - quitting anyway", kStallTimeout.count());
                }
                Log("quitting the game (devAutoQuit)");
                if (!MainQueue::Post("dev auto quit", []() {
                        SetQuit();
                        Log("Main::quitGame set on the main thread");
                    })) {
                    SetQuit();  // a plain flag the main loop reads: set here rather than leave the game open
                    Log("Main::quitGame set from the polling thread (the queue refused the task)");
                }
                break;
            }
        }

        // Under g_mutex: what is due now
        void Due(Clock::time_point a_now, const Config& a_config, std::vector<Action>& a_out)
        {
            auto& s = g_state;
            if (!a_config.devAutoLoad.empty() && !s.autoLoadIssued && s.menuAt && !s.loadStarted &&
                a_now - *s.menuAt >= kMenuSettle) {
                s.autoLoadIssued = true;
                s.stallFrom = a_now;
                a_out.push_back(Action::kLoadConfigSave);
            }
            if (s.e3 == E3Phase::kAsked && a_now - s.e3At >= kBeforeSave) {
                s.e3 = E3Phase::kSaveIssued;
                s.e3At = a_now;
                s.saveSeenAt.reset();
                a_out.push_back(Action::kSaveE3);
            } else if (s.e3 == E3Phase::kSaveIssued &&
                       ((s.saveSeenAt && a_now - *s.saveSeenAt >= kAfterSave) || a_now - s.e3At >= kSaveSignalWait)) {
                if (!s.saveSeenAt) {
                    logger::warn("BlackFlame: dev automation: E3: no kSaveGame within {} s of the save - loading anyway",
                        kSaveSignalWait.count());
                }
                s.e3 = E3Phase::kLoadIssued;
                s.e3At = a_now;
                a_out.push_back(Action::kLoadE3);
            }
            if (!a_config.devAutoQuit || s.quitIssued) {
                return;
            }
            if (s.quitAt && a_now >= *s.quitAt) {
                s.quitIssued = true;
                a_out.push_back(Action::kQuit);
            } else if (s.stallFrom && a_now - *s.stallFrom >= kStallTimeout) {
                s.quitIssued = true;
                a_out.push_back(Action::kQuitOnTimeout);
            }
        }

        void PollLoop()
        {
            for (;;) {
                std::this_thread::sleep_for(kPoll);
                // A throw here would end the thread (and std::terminate the game): keep looping
                try {
                    std::vector<Action> due;
                    bool finished = false;
                    {
                        std::lock_guard lock(g_mutex);
                        Due(Clock::now(), GetConfig(), due);
                        finished = g_state.quitIssued;
                    }
                    for (const auto action : due) {
                        Run(action);
                    }
                    if (finished) {
                        return;
                    }
                } catch (const std::exception& e) {
                    logger::error("BlackFlame: dev automation: poll threw: {}", e.what());
                } catch (...) {
                    logger::error("BlackFlame: dev automation: poll threw an unknown exception");
                }
            }
        }

        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME) {
                    std::lock_guard lock(g_mutex);
                    if (!g_state.menuAt) {
                        g_state.menuAt = Clock::now();
                        logger::info("BlackFlame: dev automation: main menu opened; loading '{}' in {} s unless a load "
                                     "starts first", GetConfig().devAutoLoad, kMenuSettle.count());
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        MenuSink g_menuSink;

        // The ids behind BGSSaveLoadManager::GetSingleton/Save/Load and Main::GetSingleton (CommonLib's .cpp files)
        bool EngineIdsPresent()
        {
            return Patch::HasIds({ Patch::IdFor(516860, 403340), Patch::IdFor(34818, 35727), Patch::IdFor(34819, 35728),
                                     Patch::IdFor(516943, 403449) },
                "dev automation (save manager, save, load, Main)");
        }
    }

    void Install()
    {
        const auto& config = GetConfig();
        if (g_installed || !AnyKeyOn(config)) {
            return;
        }
        if (!EngineIdsPresent()) {
            logger::error("BlackFlame: dev automation off: an engine id is missing (see the line above)");
            return;
        }
        if (!config.devAutoLoad.empty()) {
            // The timeout runs from here until the load is requested: a main menu event never seen still ends the game
            std::lock_guard lock(g_mutex);
            g_state.stallFrom = Clock::now();
        }
        if (!config.devAutoLoad.empty()) {
            auto* ui = RE::UI::GetSingleton();
            if (ui) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
            } else {
                logger::error("BlackFlame: dev automation: no RE::UI - devAutoLoad cannot see the main menu");
            }
        }
        g_installed = true;
        std::thread(&PollLoop).detach();  // detached, never joined: a thread joined at DLL unload can hang the exit
        logger::warn("BlackFlame: dev automation ON (dev only): devAutoLoad='{}' devAutoE3={} devAutoQuit={}",
            config.devAutoLoad, config.devAutoE3, config.devAutoQuit);
        FlushLog();
    }

    void OnPreLoadGame()
    {
        if (!g_installed) {
            return;
        }
        std::lock_guard lock(g_mutex);
        g_state.loadStarted = true;
    }

    void OnPostLoadGame()
    {
        if (!g_installed) {
            return;
        }
        std::lock_guard lock(g_mutex);
        g_state.stallFrom = Clock::now();
        if (g_state.e3 == E3Phase::kLoadIssued) {
            g_state.e3 = E3Phase::kIdle;
            logger::info("BlackFlame: dev automation: E3: the load ended; E3 judges it after its start delay");
        }
    }

    void OnSaveGame()
    {
        if (!g_installed) {
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_state.e3 == E3Phase::kSaveIssued && !g_state.saveSeenAt) {
            g_state.saveSeenAt = Clock::now();
            logger::info("BlackFlame: dev automation: E3: kSaveGame seen; loading in {} s", kAfterSave.count());
        }
    }

    void OnE3Asked()
    {
        if (!g_installed || !GetConfig().devAutoE3) {
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_state.e3 == E3Phase::kIdle) {
            g_state.e3 = E3Phase::kAsked;
            g_state.e3At = Clock::now();
            logger::info("BlackFlame: dev automation: E3 asked for a save and reload; saving '{}' in {} s", kE3Save,
                kBeforeSave.count());
        }
    }

    void OnSelfTestDone(const char* a_label)
    {
        if (!g_installed || !GetConfig().devAutoQuit) {
            return;
        }
        // The main run's DONE comes after E3 armed (E3 is the last step): then E3's own DONE is the end
        const bool e3Pending = a_label == nullptr && SelfTest::Reload::Active();
        std::lock_guard lock(g_mutex);
        if (e3Pending) {
            logger::info("BlackFlame: dev automation: self-test DONE; waiting for DONE E3 before quitting");
            return;
        }
        if (!g_state.quitAt) {
            g_state.quitAt = Clock::now() + kQuitDelay;
            logger::info("BlackFlame: dev automation: DONE{}{} written; quitting in {} s", a_label ? " " : "",
                a_label ? a_label : "", kQuitDelay.count());
        }
    }
}
