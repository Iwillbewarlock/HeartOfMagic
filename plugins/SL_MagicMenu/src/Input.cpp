#include "Common.h"

#include "Settings.h"
#include "Widget.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// INPUT - the keys of the magic menu prototype and the layout mode's input boundary.
//
// The next-learnable and set-target keys ([Keys] iNext / iSetTarget, defaults U and G: build-5 key scan,
// docs/superpowers/specs/v5-key-scan.md): keyboard only, key-down only, only while the magic menu is open
// and no text field takes keys (ControlMap's textEntryCount, which AllowTextInput counts up and down; SkyUI's
// search field uses it). The work is queued as a UI task; Widget does it on the menu's next frame.
//
// The layout mode's boundary (blocks spec "Input boundary (Task 0)", layer 1; plan Task 4): a vtable hook on
// MenuControls::ProcessEvent (CommonLib's RE::VTABLE_MenuControls[0], slot 1: the BSTEventSink<InputEvent*> base). While
// the mode is on every non-mouse event is kept from MenuControls and the menu handlers below it (the list, the 3D
// rotation, MenuOpenHandler ...); mouse moves, buttons and the wheel pass (the drag needs them). A press belongs to whoever
// got its down: a down this hook kept is "owned" and its holds and up are kept too, past the mode's end and the menu's
// close. Kept events are unlinked for the original call only; every link is put back before returning, so the other
// sinks (PlayerControls, SKSE, the key sink below) see the whole list. With nothing kept the original gets the caller's
// list untouched. The decisions are taken in order, so a toggle earlier in a batch changes how the rest of it is judged.
// =============================================================================

namespace Input
{
    namespace
    {
        constexpr std::uint32_t kEscape = 0x01;  // DirectInput scan code of Esc

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

        // (device, idCode) as one key
        std::uint64_t KeyOf(RE::INPUT_DEVICE a_device, std::uint32_t a_code)
        {
            return (static_cast<std::uint64_t>(a_device) << 32) | a_code;
        }

        // The input thread's state (the key sink below runs on that thread too)
        std::unordered_set<std::uint64_t> g_held;   // non-mouse buttons pressed now
        std::unordered_set<std::uint64_t> g_owned;  // presses whose down this hook kept: their holds and up are kept too
        std::array<bool, 2> g_stickOwned{};         // a stick kept in the mode until its first neutral sample (left, right)
        std::atomic<bool> g_hooked{ false };
        std::atomic<int> g_heldCount{ 0 };

        // per menu (ResetCounters at its open)
        std::atomic<std::uint64_t> g_eventsOff{ 0 }, g_strayOff{ 0 }, g_ownedOff{ 0 }, g_toggles{ 0 }, g_downsOn{ 0 }, g_keyboardDownsOff{ 0 };
        std::array<std::atomic<bool>, 256> g_keyboardSeenOff{};  // keyboard scan codes whose down passed with the mode off

        bool IsToggle(RE::INPUT_DEVICE a_device, std::uint32_t a_code)
        {
            const auto& settings = Settings::Get();
            if (a_device == RE::INPUT_DEVICE::kKeyboard) return a_code == settings.keyLayout;
            if (a_device == RE::INPUT_DEVICE::kGamepad && settings.gamepadLayout >= 0) {
                // the event's code is an XInput mask (BSWin32GamepadDevice.h), the ini's the SKSE code
                return SKSE::InputMap::GamepadMaskToKeycode(a_code) == static_cast<std::uint32_t>(settings.gamepadLayout);
            }
            return false;
        }

        // The spec's exceptions: the console and screenshot buttons always pass (UserEvents.h)
        bool Exempt(const RE::ButtonEvent& a_button)
        {
            auto* events = RE::UserEvents::GetSingleton();
            const auto& name = a_button.QUserEvent();
            return events && (name == events->console || name == events->screenshot);
        }

        // Esc, or the menu's "Cancel" user event (B on a gamepad): in the mode it leaves without saving
        bool IsCancel(const RE::ButtonEvent& a_button)
        {
            if (a_button.GetDevice() == RE::INPUT_DEVICE::kKeyboard && a_button.GetIDCode() == kEscape) return true;
            auto* events = RE::UserEvents::GetSingleton();
            return events && a_button.QUserEvent() == events->cancel;
        }

        enum class Take
        {
            kPass,
            kToggle,  // the accepted toggle press (owned)
            kMode,    // kept because the mode is on
            kOwned,   // the hold or up of an owned press
            kStick,   // a stick still owned after the mode
        };

        bool HeldOther(std::uint64_t a_key)
        {
            for (const auto key : g_held) {
                if (key != a_key) return true;
            }
            return false;
        }

        Take DecideButton(RE::ButtonEvent& a_button, bool a_menuOpen)
        {
            const auto device = a_button.GetDevice();
            const auto code = a_button.GetIDCode();
            const auto key = KeyOf(device, code);
            const bool down = a_button.IsDown();
            if (down) {
                g_owned.erase(key);  // a fresh down is judged anew
            } else if (g_owned.contains(key)) {
                if (a_button.IsUp()) {
                    g_owned.erase(key);
                    g_held.erase(key);
                }
                return Take::kOwned;
            }
            if (a_button.IsPressed()) {
                g_held.insert(key);  // IsPressed, not IsDown: a press begun before the menu opened counts as held
            } else if (a_button.IsUp()) {
                g_held.erase(key);
            }
            if (down && a_menuOpen && IsToggle(device, code)) {
                if (Widget::ToggleLayoutMode(device == RE::INPUT_DEVICE::kKeyboard ? "the key" : "the gamepad button", HeldOther(key))) {
                    g_owned.insert(key);
                    return Take::kToggle;
                }
                return Take::kPass;  // refused: passed through untouched (Widget logged why)
            }
            if (!Widget::LayoutModeOn() || Exempt(a_button)) return Take::kPass;
            if (down) {
                if (IsCancel(a_button)) Widget::CancelLayoutMode(device == RE::INPUT_DEVICE::kKeyboard ? "Esc" : "the gamepad's Cancel");
                g_owned.insert(key);
                ++g_downsOn;
            }
            return Take::kMode;
        }

        Take Decide(RE::InputEvent& a_event, bool a_menuOpen)
        {
            const auto type = a_event.GetEventType();
            if (a_event.GetDevice() == RE::INPUT_DEVICE::kMouse || type == RE::INPUT_EVENT_TYPE::kMouseMove ||
                type == RE::INPUT_EVENT_TYPE::kDeviceConnect) {
                return Take::kPass;
            }
            if (type == RE::INPUT_EVENT_TYPE::kButton) {
                auto* button = a_event.AsButtonEvent();
                return button ? DecideButton(*button, a_menuOpen) : Take::kPass;
            }
            if (type == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = a_event.AsThumbstickEvent();
                if (!stick) return Take::kPass;
                const std::size_t side = stick->IsRight() ? 1 : 0;
                const bool neutral = stick->xValue == 0.0f && stick->yValue == 0.0f;
                if (Widget::LayoutModeOn()) {
                    g_stickOwned[side] = !neutral;
                    return Take::kMode;
                }
                if (g_stickOwned[side]) {
                    if (!neutral) return Take::kStick;
                    g_stickOwned[side] = false;  // the neutral sample passes: the handlers below see the stick at rest
                }
                return Take::kPass;
            }
            // char events, the VR touchpad and the AE motion types: kept in the mode
            return Widget::LayoutModeOn() ? Take::kMode : Take::kPass;
        }

        bool Armed(bool a_menuOpen)
        {
            return a_menuOpen || !g_owned.empty() || g_stickOwned[0] || g_stickOwned[1];
        }

        // Every link of the list as it came, put back on every return path
        class Relink
        {
        public:
            explicit Relink(RE::InputEvent* a_head)
            {
                for (auto* event = a_head; event; event = event->next) _links.emplace_back(event, event->next);
            }
            ~Relink()
            {
                for (auto& [event, next] : _links) event->next = next;
            }
            Relink(const Relink&) = delete;
            Relink& operator=(const Relink&) = delete;

        private:
            std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> _links;
        };

        // SelfTest L5's counts for one event judged while the magic menu was open with the mode off
        void CountOff(const RE::InputEvent& a_event, Take a_take)
        {
            ++g_eventsOff;
            if (a_take == Take::kOwned || a_take == Take::kStick) ++g_ownedOff;
            if (a_take == Take::kToggle) ++g_toggles;
            if (a_take == Take::kMode) ++g_strayOff;  // with the mode off only the toggle and owned tails may be kept
            if (a_take != Take::kPass || a_event.GetEventType() != RE::INPUT_EVENT_TYPE::kButton || a_event.GetDevice() != RE::INPUT_DEVICE::kKeyboard) return;
            const auto* button = a_event.AsButtonEvent();
            if (!button || !button->IsDown()) return;
            ++g_keyboardDownsOff;
            if (button->GetIDCode() < g_keyboardSeenOff.size()) g_keyboardSeenOff[button->GetIDCode()] = true;
        }

        struct MenuControlsHook
        {
            static RE::BSEventNotifyControl thunk(RE::MenuControls* a_this, RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>* a_source)
            {
                const bool menuOpen = MagicMenuOpen();
                if (!a_events || !*a_events || !Armed(menuOpen)) return original(a_this, a_events, a_source);
                static std::atomic<bool> s_threadLogged{ false };
                if (!s_threadLogged.exchange(true)) logger::info("Input: layout input hook first used on thread {}", REX::W32::GetCurrentThreadId());
                std::vector<bool> kept;
                bool any = false;
                for (auto* event = *a_events; event; event = event->next) {
                    const bool modeBefore = Widget::LayoutModeOn();
                    const Take take = Decide(*event, menuOpen);
                    kept.push_back(take != Take::kPass);
                    any = any || take != Take::kPass;
                    if (menuOpen && !modeBefore) CountOff(*event, take);
                }
                g_heldCount = static_cast<int>(g_held.size());
                if (!any) return original(a_this, a_events, a_source);
                const Relink relink(*a_events);
                RE::InputEvent* head = nullptr;
                RE::InputEvent* tail = nullptr;
                std::size_t i = 0;
                for (auto* event = *a_events; event; ++i) {
                    auto* next = event->next;
                    if (!kept[i]) {
                        (tail ? tail->next : head) = event;
                        tail = event;
                    }
                    event = next;
                }
                if (tail) tail->next = nullptr;
                return original(a_this, &head, a_source);  // once, also with nothing left (head null)
            }

            static inline REL::Relocation<decltype(thunk)> original;
        };

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
                    // blocks spec "When the mode is off": U and G do nothing while the layout mode is on or the press is the mode's
                    if (Widget::LayoutModeOn() || g_owned.contains(KeyOf(RE::INPUT_DEVICE::kKeyboard, key))) continue;
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

        void InstallHook()
        {
            constexpr std::size_t kProcessEvent = 1;  // BSTEventSink<InputEvent*>::ProcessEvent (MenuControls.h "// 01")
            REL::Relocation<std::uintptr_t> table{ RE::VTABLE_MenuControls[0] };
            MenuControlsHook::original = table.write_vfunc(kProcessEvent, MenuControlsHook::thunk);
            g_hooked = true;
            const auto& settings = Settings::Get();
            logger::info("Input: layout input hook on MenuControls::ProcessEvent (VTABLE_MenuControls[0] slot {}); layout toggle key {}, gamepad {}",
                kProcessEvent, settings.keyLayout, settings.gamepadLayout < 0 ? std::string("none") : std::to_string(settings.gamepadLayout));
        }
    }

    void Install()
    {
        InstallHook();
        auto* devices = RE::BSInputDeviceManager::GetSingleton();
        if (!devices) {
            logger::error("Input: no BSInputDeviceManager - the next-learnable and set-target keys are off");
            return;
        }
        devices->AddEventSink(KeySink::GetSingleton());
        const auto& settings = Settings::Get();
        logger::info("Input: key sink installed (next {}, set target {})", settings.keyNext, settings.keySetTarget);
        // the key scan of the installed mods (2026-10-25, v5-key-scan.md), so the game log alone explains the defaults
        const auto avoided = [](std::uint32_t a_key) { return a_key == 19 || a_key == 49; };
        logger::info("Input: key scan - avoided 19 R (Spell Organizer RemoveHotkey, live in the magic menu; its saved value is in the "
                     "save, assumed R) and 49 N (Creature Framework PupTargetKey, no menu check); 22 U and 34 G: nothing live in the "
                     "magic menu; 37 K (the layout mode): in no controlmap line, SexLab scenes only{}",
            avoided(settings.keyNext) || avoided(settings.keySetTarget) || avoided(settings.keyLayout) ? " - WARNING: the ini sets one of the avoided keys" :
                                                                                                         "");
    }

    bool HookInstalled()
    {
        return g_hooked;
    }

    int HeldCount()
    {
        return g_heldCount;
    }

    void ResetCounters()
    {
        g_eventsOff = g_strayOff = g_ownedOff = g_toggles = g_downsOn = g_keyboardDownsOff = 0;
        for (auto& seen : g_keyboardSeenOff) seen = false;
    }

    Counters ReadCounters()
    {
        return { g_eventsOff, g_strayOff, g_ownedOff, g_toggles, g_downsOn, g_keyboardDownsOff };
    }

    bool KeyboardDownSeenOff(std::uint32_t a_code)
    {
        return a_code < g_keyboardSeenOff.size() && g_keyboardSeenOff[a_code];
    }
}
