#pragma once

#include <cstdint>

// Public C ABI for menus that want to register themselves with
// Risa's All In One Menu. Risa copies every string during registration.
// The author retains ownership of the callback context and must keep it valid
// until UnregisterMenu is called or the game process exits.
namespace RisaMenuIntegrationAPI {
    inline constexpr std::uint32_t kInterfaceVersion = 1;
    inline constexpr std::uint32_t kMaximumExternalMenus = 16;

    enum RegistrationResult : std::uint32_t {
        Registration_Success = 0,
        Registration_InvalidArgument = 1,
        Registration_IncompatibleVersion = 2,
        Registration_MissingRequiredCallback = 3,
        Registration_InvalidId = 4,
        Registration_DuplicateId = 5,
        Registration_CapacityReached = 6,
        Registration_NotFound = 7
    };

    enum MenuFlags : std::uint32_t {
        MenuFlag_None = 0,
        // Hide the launcher button initially. The player can reveal it in
        // launcher edit mode without affecting the registered hotkey.
        MenuFlag_DefaultHidden = 1u << 0,
        // The menu intentionally leaves Skyrim's OS cursor hidden while open.
        MenuFlag_IgnoreOSCursor = 1u << 1
    };

    using OpenMenuCallback = bool (*)(void* context);
    using CloseMenuCallback = void (*)(void* context);
    using IsMenuOpenCallback = bool (*)(void* context);
    using SetHotkeyEnabledCallback = void (*)(void* context, bool enabled);

    struct MenuRegistrationV1 {
        std::uint32_t structSize{sizeof(MenuRegistrationV1)};
        std::uint32_t interfaceVersion{kInterfaceVersion};
        std::uint32_t flags{MenuFlag_None};

        // Stable ASCII identifier used for settings persistence. Use a value
        // such as "AuthorName.ModName" and never change it after release.
        const char* menuId{};
        // UTF-8 text shown on the launcher and in Hotkey Control.
        const char* displayName{};
        // Font Awesome 6 Free Solid codepoint. Zero uses Risa's default icon.
        std::uint32_t iconCodepoint{};
        // Short user-facing description of the mod's own hotkey, for example
        // "F8" or "Shift + O". Risa does not parse or simulate this value.
        const char* nativeHotkeyLabel{};

        void* context{};
        OpenMenuCallback OpenMenu{};
        CloseMenuCallback CloseMenu{};
        IsMenuOpenCallback IsMenuOpen{};
        // Required. Risa calls false while it owns the integration and true
        // when the player excludes it or the registration is removed.
        SetHotkeyEnabledCallback SetHotkeyEnabled{};
    };

    struct InterfaceV1 {
        std::uint32_t interfaceVersion{kInterfaceVersion};
        RegistrationResult (*RegisterMenu)(const MenuRegistrationV1* registration){};
        RegistrationResult (*UnregisterMenu)(const char* menuId){};
    };

    inline constexpr std::uint32_t kHotkeyInterfaceVersion = 2;

    struct HotkeyBindingV1 {
        std::uint32_t structSize{sizeof(HotkeyBindingV1)};
        std::uint32_t device{};
        std::uint32_t primary{};
        std::uint32_t chord{};
        std::uint32_t modifiers{};
        std::uint32_t triggerMode{};
        std::uint32_t holdDurationMs{300};
    };

    // The first three fields match InterfaceV1. Request version 2 only when
    // shortcut sharing is needed; version 1 consumers remain supported.
    struct InterfaceV2 {
        std::uint32_t interfaceVersion{kHotkeyInterfaceVersion};
        RegistrationResult (*RegisterMenu)(const MenuRegistrationV1* registration){};
        RegistrationResult (*UnregisterMenu)(const char* menuId){};
        bool (*GetMenuHotkey)(const char* menuId, HotkeyBindingV1* binding){};
        bool (*SetMenuHotkey)(const char* menuId, const HotkeyBindingV1* binding){};
    };

    inline constexpr std::uint32_t kHotkeyActionInterfaceVersion = 3;

    // Append-only V2 prefix. Query again after unregister/re-register: action
    // IDs describe this process's registration, not a persistent menu identity.
    struct InterfaceV3 {
        std::uint32_t interfaceVersion{kHotkeyActionInterfaceVersion};
        RegistrationResult (*RegisterMenu)(const MenuRegistrationV1* registration){};
        RegistrationResult (*UnregisterMenu)(const char* menuId){};
        bool (*GetMenuHotkey)(const char* menuId, HotkeyBindingV1* binding){};
        bool (*SetMenuHotkey)(const char* menuId, const HotkeyBindingV1* binding){};
        // Pair the ID with QueryHotkeyConflicts.applicationId="RisaAllInOneMenu".
        // False for unknown IDs/null output; non-null output is cleared on failure.
        bool (*GetMenuHotkeyActionId)(const char* menuId, std::uint32_t* actionId){};
    };

    using GetInterfaceFn = const InterfaceV1* (*)(std::uint32_t requestedVersion);
}

