#include "RisaMenu.h"
#include "RisaMenuIntegrationAPI.h"
#include "ThreadUtils.h"
#include "uimanager/UIManager.h"

#include <atomic>
#include <string>
#include <string_view>

// =============================================================================
// RisaMenu - see RisaMenu.h. Contract: risas-all-in-one-menu.pages.dev/for-mod-authors
// (register once at kPostLoad, never LoadLibrary, keep the hotkey change runtime-only).
// =============================================================================

namespace RisaMenu
{
    namespace
    {
        namespace Api = RisaMenuIntegrationAPI;

        // Permanent: Risa keys the player's button position, visibility and
        // its own hotkey for us by this. Never change it after release.
        constexpr const char* kMenuId = "HeartOfMagic.Librarian";
        constexpr const char* kDisplayName = "Heart of Magic";
        constexpr std::uint32_t kIconHatWizard = 0xF6E8;   // Font Awesome 6 Solid "hat-wizard"

        std::atomic<bool> g_nativeHotkeyEnabled{ true };
        const Api::InterfaceV1* g_api = nullptr;            // set once registered
        std::string g_hotkeyLabel;                         // Risa copies it; kept for re-registering

        // DirectInput scan codes to the label a player reads on the key.
        std::string KeyLabel(std::uint32_t code)
        {
            constexpr std::uint32_t kNone = 0;
            constexpr std::uint32_t kF1 = 59, kF10 = 68, kF11 = 87, kF12 = 88;
            struct Row { std::uint32_t first; std::string_view keys; };
            // The number row (1..9, 0) and the three letter rows, each in scan-code order
            constexpr Row kRows[] = { { 2, "1234567890" }, { 16, "QWERTYUIOP" }, { 30, "ASDFGHJKL" }, { 44, "ZXCVBNM" } };
            if (code == kNone) return "None";
            if (code >= kF1 && code <= kF10) return "F" + std::to_string(code - kF1 + 1);
            if (code == kF11) return "F11";
            if (code == kF12) return "F12";
            for (const auto& row : kRows) {
                if (code >= row.first && code < row.first + row.keys.size()) return std::string(1, row.keys[code - row.first]);
            }
            return "Key " + std::to_string(code);
        }

        bool Open(void*)
        {
            // IsInitialized is a plain bool read off the game thread: it goes true
            // once, before any player could reach Risa's launcher, and never back
            auto* ui = UIManager::GetSingleton();
            if (!ui || !ui->IsInitialized()) return false;   // main menu before data loaded
            AddTaskToGameThread("RisaMenu.Open", []() { UIManager::GetSingleton()->ShowPanel(); });
            return true;
        }

        void Close(void*)
        {
            // HidePanel returns at once when the panel is already closed
            AddTaskToGameThread("RisaMenu.Close", []() {
                auto* ui = UIManager::GetSingleton();
                if (ui && ui->IsInitialized()) ui->HidePanel();
            });
        }

        bool IsOpen(void*)
        {
            auto* ui = UIManager::GetSingleton();
            return ui && ui->IsPanelVisible();
        }

        void SetNativeHotkey(void*, bool enabled)
        {
            // Risa repeats this every second during its integration maintenance: log changes only
            if (g_nativeHotkeyEnabled.exchange(enabled) == enabled) return;
            logger::info("RisaMenu: own hotkey {} by Risa's All In One Menu", enabled ? "given back" : "taken over");
        }

        Api::RegistrationResult RegisterWith(const Api::InterfaceV1* api)
        {
            Api::MenuRegistrationV1 menu;
            menu.menuId = kMenuId;
            menu.displayName = kDisplayName;
            menu.iconCodepoint = kIconHatWizard;
            menu.nativeHotkeyLabel = g_hotkeyLabel.c_str();
            menu.OpenMenu = Open;
            menu.CloseMenu = Close;
            menu.IsMenuOpen = IsOpen;
            menu.SetHotkeyEnabled = SetNativeHotkey;
            return api->RegisterMenu(&menu);
        }
    }

    void Register(std::uint32_t keyCode)
    {
        const HMODULE module = GetModuleHandleW(L"RisaAllInOneMenu.dll");
        if (!module) return;
        const auto getApi = reinterpret_cast<Api::GetInterfaceFn>(
            GetProcAddress(module, "RisaAllInOneMenu_GetIntegrationAPI"));
        if (!getApi) {
            logger::warn("RisaMenu: RisaAllInOneMenu.dll has no integration API (older than 5.4?) - not registered");
            return;
        }
        const auto* api = getApi(Api::kInterfaceVersion);
        if (!api || !api->RegisterMenu) {
            logger::warn("RisaMenu: Risa's All In One Menu does not offer API version {} - not registered",
                Api::kInterfaceVersion);
            return;
        }

        g_hotkeyLabel = KeyLabel(keyCode);
        const auto result = RegisterWith(api);
        if (result != Api::Registration_Success) {
            logger::warn("RisaMenu: registration refused (result {}) - our own hotkey stays on", static_cast<std::uint32_t>(result));
            return;
        }
        g_api = api;
        logger::info("RisaMenu: registered with Risa's All In One Menu as '{}'", kMenuId);
    }

    bool NativeHotkeyEnabled()
    {
        return g_nativeHotkeyEnabled;
    }

    void OnHotkeyChanged(std::uint32_t keyCode)
    {
        std::string label = KeyLabel(keyCode);
        if (!g_api || label == g_hotkeyLabel) {
            g_hotkeyLabel = std::move(label);
            return;
        }
        // Risa's rule for a changed key: unregister and register again with the same id
        g_hotkeyLabel = std::move(label);
        if (g_api->UnregisterMenu) g_api->UnregisterMenu(kMenuId);
        const auto result = RegisterWith(g_api);
        if (result != Api::Registration_Success) {
            g_api = nullptr;
            g_nativeHotkeyEnabled = true;
            logger::warn("RisaMenu: re-registration refused (result {}) - our own hotkey is back on", static_cast<std::uint32_t>(result));
        }
    }
}
