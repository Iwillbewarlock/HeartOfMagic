#include "Common.h"

#include "Settings.h"
#include "Widget.h"

// =============================================================================
// INPUT - the next-learnable and set-target keys of the magic menu prototype
// ([Keys] iNext / iSetTarget, defaults U and G: build-5 key scan,
// docs/superpowers/specs/v5-key-scan.md). Keyboard only, key-down only, only while the magic menu is open
// and no text field takes keys (ControlMap's textEntryCount, which
// AllowTextInput counts up and down; SkyUI's search field uses it). The work
// is queued as a UI task; Widget does it on the menu's next frame.
// =============================================================================

namespace Input
{
    namespace
    {
        bool TypingText()
        {
            auto* controls = RE::ControlMap::GetSingleton();
            return controls && controls->GetRuntimeData().textEntryCount > 0;
        }

        bool MagicMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen(RE::MagicMenu::MENU_NAME);
        }

        class KeySink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static KeySink* GetSingleton()
            {
                static KeySink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!a_events || !*a_events || !MagicMenuOpen() || TypingText()) return RE::BSEventNotifyControl::kContinue;
                const auto& settings = Settings::Get();
                for (auto* event = *a_events; event; event = event->next) {
                    if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
                    const auto* button = event->AsButtonEvent();
                    if (!button || !button->IsDown()) continue;
                    const auto key = button->GetIDCode();
                    if (key == settings.keyNext) {
                        SKSE::GetTaskInterface()->AddUITask([] { Widget::RequestNext(); });
                    } else if (key == settings.keySetTarget) {
                        SKSE::GetTaskInterface()->AddUITask([] { Widget::RequestSetTarget(); });
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            KeySink(const KeySink&) = delete;
            KeySink& operator=(const KeySink&) = delete;

        private:
            KeySink() = default;
        };
    }

    void Install()
    {
        auto* devices = RE::BSInputDeviceManager::GetSingleton();
        if (!devices) {
            logger::error("Input: no BSInputDeviceManager - the next-learnable and set-target keys are off");
            return;
        }
        devices->AddEventSink(KeySink::GetSingleton());
        const auto& settings = Settings::Get();
        logger::info("Input: key sink installed (next {}, set target {})", settings.keyNext, settings.keySetTarget);
        // the key scan of the installed mods (2026-10-25, v5-key-scan.md), so the game log alone explains the defaults
        logger::info("Input: key scan - avoided 19 R (Spell Organizer RemoveHotkey, live in the magic menu; its saved value is in the "
                     "save, assumed R) and 49 N (Creature Framework PupTargetKey, no menu check); 22 U and 34 G: nothing live in the "
                     "magic menu{}",
            settings.keyNext == 19 || settings.keyNext == 49 || settings.keySetTarget == 19 || settings.keySetTarget == 49 ?
                " - WARNING: the ini sets one of the avoided keys" :
                "");
    }
}
