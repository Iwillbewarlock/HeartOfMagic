#include "Common.h"
#include "MainMenuWatch.h"
#include "AutoScan.h"
#include "ThreadUtils.h"
#include "uimanager/UIManager.h"

#include <atomic>

// MainMenuWatch.h: one sink, its listeners' tasks in a fixed order

namespace
{
    std::atomic<bool> s_panel{ false };
    std::atomic<bool> s_autoScan{ false };

    class Sink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static Sink* GetSingleton()
        {
            static Sink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!event || !event->opening || event->menuName != RE::MainMenu::MENU_NAME) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (s_panel.load()) {
                AddTaskToGameThread("AutoBuildMainMenu", []() {
                    if (auto* instance = UIManager::GetSingleton()) instance->NotifyMainMenuShown();
                });
            }
            if (s_autoScan.load()) {
                AddTaskToGameThread("AutoScanMainMenu", []() { AutoScan::OnMainMenuOpened(); });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        Sink(const Sink&) = delete;
        Sink& operator=(const Sink&) = delete;

    private:
        Sink() = default;
    };
}

namespace MainMenuWatch
{
    bool Listen(Listener listener)
    {
        static bool s_registered = false;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        if (!s_registered) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(Sink::GetSingleton());
            s_registered = true;
        }
        (listener == Listener::kPanel ? s_panel : s_autoScan).store(true);
        return true;
    }
}
