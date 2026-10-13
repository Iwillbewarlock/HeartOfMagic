#pragma once

#include <cstdint>

// =============================================================================
// RisaMenu - a launcher button in Risa's All In One Menu (Nexus 183829)
//
// Registers the panel through Risa's C API (RisaMenuIntegrationAPI.h, MIT) when
// RisaAllInOneMenu.dll is loaded; does nothing otherwise. While Risa manages the
// panel it turns our own hotkey off - runtime only, config.json keeps the key.
// =============================================================================

namespace RisaMenu
{
    // kPostLoad: registers when Risa's DLL and API version 1 are there.
    // keyCode: the InputHandler's key now (its default until config loads).
    void Register(std::uint32_t keyCode);

    // Whether our own hotkey opens the panel. False only while Risa owns it.
    [[nodiscard]] bool NativeHotkeyEnabled();

    // The panel's hotkey changed (config loaded or saved): Risa shows the key as
    // text, so a registered menu registers again with the new label.
    void OnHotkeyChanged(std::uint32_t keyCode);
}
