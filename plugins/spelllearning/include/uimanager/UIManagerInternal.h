#pragma once

#include "Common.h"
#include "FormIdText.h"
#include "ProgressionManager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>

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

// The spell a panel message names (FormIdText::ParseSpellOf: hex, "0x" optional; an
// edit mode copy's "<formId>_dup_<n>" is its spell, as the panel's own getCanonicalFormId
// reads it), or nullopt with a warning naming the handler and the text it got
inline std::optional<RE::FormID> ParsePanelFormId(std::string_view text, std::string_view handler)
{
    const auto formId = FormIdText::ParseSpellOf(text);
    if (!formId) logger::warn("UIManager: {} - '{}' is not a formId", handler, text);
    return formId;
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
// Everything a config changes in play: XP settings and modded sources (SetXPSettings),
// then ApplySettingsFromConfig. The panel's load, its saves and game start. Game thread.
void ApplyGameplayConfig(const nlohmann::json& config);
// Every config key with its default value
nlohmann::json GenerateDefaultConfig();
// The pre-unified settings file, merged into a first config.json only
std::filesystem::path GetSettingsFilePath();
// That file's settings over `config` when it exists and parses; returns whether it merged.
// The panel's load and game start both use it (UIManagerStartupConfig.cpp)
bool MergeLegacySettings(nlohmann::json& config);

// The tree's prerequisites from SetTreePrerequisites entries ({formId, hardPrereqs,
// softPrereqs, softNeeded, legacy prereqs}) into ProgressionManager, then the tome
// cache dropped. Logs the count. May throw on a malformed entry (as the panel path
// always has: the caller catches). Returns the entries applied. Game thread.
int ApplyTreePrerequisiteEntries(const nlohmann::json& entries);
// Each tree node's requiredXP > 0 into ProgressionManager::SetRequiredXP. Throws on a
// formId that is not a string, as OnLoadSpellTree always has. Game thread.
void SyncRequiredXPFromTree(const nlohmann::json& tree);

// spell_tree.json read whole (UIManagerTreeState.cpp), by OnLoadSpellTree and game start
enum class TreeFileRead
{
    Ok,            ///< `text` holds the file
    CouldNotOpen,
    TooLarge,      ///< over the caller's maxBytes, not read
};
struct TreeFileText
{
    TreeFileRead outcome = TreeFileRead::CouldNotOpen;
    std::string text;
};
inline constexpr std::uintmax_t kNoTreeFileCap = (std::numeric_limits<std::uintmax_t>::max)();
TreeFileText ReadTreeFileText(const std::filesystem::path& path, std::uintmax_t maxBytes = kNoTreeFileCap);
// Only spaces, tabs and line breaks: there is no saved tree
bool IsBlankTreeText(std::string_view text);
// SpellScanner::ValidateAndFixTree on the parsed saved tree, with OnLoadSpellTree's two log
// lines. True when spells were resolved from persistent ids or dropped: the panel then gets
// the validated tree's dump instead of the file's text. Throws as the validator does.
bool ValidateTreeForPanel(nlohmann::json& tree);

// Config saves are written by a background worker (UIManagerConfigSave.cpp).
// Waits (bounded) for queued saves to reach the disk, then keeps new saves
// away from config.json for as long as the returned lock is held.
[[nodiscard]] std::unique_lock<std::mutex> LockUnifiedConfigFile();
