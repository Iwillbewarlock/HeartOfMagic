#include "Common.h"
#include "FileUtils.h"
#include "PathText.h"
#include "uimanager/ConfigFile.h"
#include "uimanager/ConfigRenames.h"
#include "uimanager/UIManagerInternal.h"
#include "uimanager/StartupState.h"

#include <fstream>

// =============================================================================
// GAMEPLAY CONFIG AT GAME START
// =============================================================================
//
// The settings the panel's LoadUnifiedConfig would apply, picked from the same
// files by the same rules (ConfigFile::Peek, the read half of the panel's
// ConfigFile::Load), but read only: the panel's load still moves a broken
// config.json aside, writes the defaults, merges legacy settings.json into a
// first file, sets the save mode, writes the page's language file and applies
// the hotkey and pauseGameOnFocus (both only matter to the panel).

bool MergeLegacySettings(nlohmann::json& config)
{
    const auto legacyPath = GetSettingsFilePath();
    std::error_code legacyError;
    if (!std::filesystem::exists(legacyPath, legacyError)) return false;
    std::ifstream legacyFile(legacyPath);
    auto legacy = nlohmann::json::parse(legacyFile, nullptr, false);
    if (legacy.is_discarded()) return false;
    ConfigRenames::Migrate(legacy);   // old setting names under their new ones, before the merge
    MergeJsonNonNull(config, legacy);
    return true;
}

namespace
{
    // The file's settings over `config` as the panel's load would take them, with nothing
    // moved or written. Returns whether config.json is broken (not JSON / not an object).
    bool MergeConfigFileReadOnly(nlohmann::json& config)
    {
        const auto path = GetUnifiedConfigPath();
        const auto peeked = ConfigFile::Peek(path);
        // Old setting names under their new ones, before the defaults (new names) could stand in, as the panel's load
        nlohmann::json fileConfig = peeked.config;
        ConfigRenames::Migrate(fileConfig);
        switch (peeked.state) {
            case ConfigFile::State::Loaded:
                MergeJsonNonNull(config, fileConfig);
                logger::info("Startup config: config.json read");
                return false;
            case ConfigFile::State::Missing:
                // The one state ConfigFile::MergesLegacy lets the panel merge settings.json into
                if (MergeLegacySettings(config)) {
                    logger::info("Startup config: no config.json - defaults with legacy settings.json");
                } else {
                    logger::info("Startup config: no config.json - defaults");
                }
                return false;
            case ConfigFile::State::Unreadable:
                // The panel leaves it as it is and uses the defaults too
                logger::warn("Startup config: config.json could not be read ({}) - defaults", peeked.detail);
                return false;
            case ConfigFile::State::Broken:
            case ConfigFile::State::MovedAside:   // Load's states, not Peek's
            case ConfigFile::State::BrokenKept:
                break;
        }

        // Broken: the panel moves it aside and takes the last good save (.bak) in its place
        const auto backupName = PathText::Utf8(FileUtils::BackupPath(path).filename());
        if (peeked.fromBackup) {
            MergeJsonNonNull(config, fileConfig);
            logger::warn("Startup config: config.json is {} - {} read in its place (left as it is: the panel moves "
                         "the broken file aside)", peeked.detail, backupName);
        } else {
            logger::warn("Startup config: config.json is {} and {} is not usable ({}) - defaults", peeked.detail,
                backupName, peeked.backupDetail);
        }
        return true;
    }
}

StartupConfigResult LoadGameplayConfigAtStartup()
{
    StartupConfigResult result;
    // The panel's load runs in a game thread task, whose runner catches; this runs in the
    // SKSE message handler, where an exception would take the game down
    try {
        nlohmann::json config = GenerateDefaultConfig();
        {
            // No save can be queued this early; the lock keeps the read path the panel's
            auto configFileLock = LockUnifiedConfigFile();
            result.configBroken = MergeConfigFileReadOnly(config);
        }
        ApplyGameplayConfig(config);
        logger::info("Startup config: gameplay settings applied");
    } catch (const std::exception& e) {
        // Settings partly applied at most; the panel's load applies them whole. The tree
        // waits for the panel too (StartupConfigResult::applyThrew)
        logger::error("Startup config: applying config.json threw ({}) - the panel's load applies the settings", e.what());
        result.applyThrew = true;
    }
    return result;
}
