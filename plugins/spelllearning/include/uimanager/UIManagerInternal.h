#pragma once

#include "Common.h"
#include "ProgressionManager.h"

#include <chrono>
#include <filesystem>
#include <mutex>

// =============================================================================
// Internal helpers shared across UIManager implementation files.
// NOT part of the public API — only included by UIManager*.cpp files.
// =============================================================================

// nlohmann::json::value() throws type_error.306 when key exists but is null.
// This helper safely returns the default if the key is missing OR null.
template<typename T>
T SafeJsonValue(const nlohmann::json& j, const std::string& key, const T& defaultValue) {
    if (j.contains(key) && !j[key].is_null()) {
        try {
            return j[key].get<T>();
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

// Milliseconds since `started`, for the timing lines in the log
inline long long MsSince(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

// Forward declaration for InputHandler access (defined in Main.cpp)
void UpdateInputHandlerHotkey(uint32_t keyCode);

// Write the panel's chosen language where index.html reads it before drawing
// (lang/user_locale.js). Defined in UIManagerLocale.cpp. File IO only - safe
// off the game thread.
void WritePanelLocale(const nlohmann::json& config);
// The same from the saved config.json, before the panel's view is made (the view
// holds the file once it has loaded it). Defined in UIManagerLocale.cpp.
void WritePanelLocaleFromSavedConfig();
// The panel's language: config "language", else lang/locale.js, else "en".
// Reads locale.js when the config names none. Defined in UIManagerLocale.cpp.
std::string ResolvePanelLocale(const nlohmann::json& config);
// lang/<localeCode>.json of the panel (the corner messages' strings, HudNotify.h)
std::filesystem::path PanelLanguageFile(const std::string& localeCode);

// Unified config (config.json) helpers, defined in UIManagerConfig.cpp.
std::filesystem::path GetUnifiedConfigPath();
// Recursively merge src into dst, only overwriting non-null values
void MergeJsonNonNull(nlohmann::json& dst, const nlohmann::json& src);
// XP rates, caps, tier XP and reverse unlock settings (moddedSources left empty)
ProgressionManager::XPSettings ReadXPSettingsFromConfig(const nlohmann::json& config);
// The modded XP sources to use: the registered ones, each config "moddedXPSources" entry
// over its source (or added), internal and reserved names left out. Game thread.
std::unordered_map<std::string, ProgressionManager::ModdedSourceConfig> ReadModdedSourcesFromConfig(const nlohmann::json& config);
// Early learning, tome, passive learning and notification settings. Game thread.
void ApplySettingsFromConfig(const nlohmann::json& config);

// Config saves are written by a background worker (UIManagerConfigSave.cpp).
// Waits (bounded) for queued saves to reach the disk, then keeps new saves
// away from config.json for as long as the returned lock is held.
[[nodiscard]] std::unique_lock<std::mutex> LockUnifiedConfigFile();
