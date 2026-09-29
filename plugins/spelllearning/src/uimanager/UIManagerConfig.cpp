#include "Common.h"
#include "JsonText.h"
#include "PathText.h"
#include "FileUtils.h"
#include "uimanager/ConfigFile.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "SpellTomeHook.h"
#include "SpellCastHandler.h"
#include "PassiveLearningSource.h"
#include "ThreadUtils.h"

// =============================================================================
// SETTINGS (Legacy - now uses Unified Config)
// =============================================================================

// Known higher spells (XPSettings::reverseUnlock): what they open, the XP share of
// the opened spells and their own XP gain rates.
static void ReadReverseUnlockSettings(const nlohmann::json& config, ProgressionManager::XPSettings& xpSettings)
{
    // A share of 0 would make a fresh target need 0 XP, which AddXP ignores for
    // good; the panel's sliders stay within 10 - 100%
    constexpr float kMinShare = 0.01f;
    constexpr float kMaxShare = 1.0f;
    auto share = [&](const char* key, float fallback) {
        return std::clamp(SafeJsonValue<float>(config, key, fallback), kMinShare, kMaxShare);
    };
    auto percent = [&](const char* key, float fallback) {
        return (std::max)(0.0f, SafeJsonValue<float>(config, key, fallback));
    };

    xpSettings.reverseUnlock = SafeJsonValue<bool>(config, "reverseUnlock", true);
    xpSettings.reverseUnlockToRoot = SafeJsonValue<bool>(config, "reverseUnlockToRoot", false);
    xpSettings.reverseUnlockXPNovice = share("reverseUnlockXPNovice", 0.3f);
    xpSettings.reverseUnlockXPApprentice = share("reverseUnlockXPApprentice", 0.4f);
    xpSettings.reverseUnlockXPAdept = share("reverseUnlockXPAdept", 0.5f);
    xpSettings.reverseUnlockXPExpert = share("reverseUnlockXPExpert", 0.7f);
    xpSettings.reverseUnlockXPMaster = share("reverseUnlockXPMaster", 0.8f);

    // Multipliers and caps are percentages in the config, like the xp* keys
    auto& gain = xpSettings.reverseGain;
    xpSettings.reverseXPSeparate = SafeJsonValue<bool>(config, "reverseXpSeparate", false);
    gain.globalMultiplier = percent("reverseXpGlobalMultiplier", 1.0f);
    gain.multiplierDirect = percent("reverseXpMultiplierDirect", 100.0f) / 100.0f;
    gain.multiplierSchool = percent("reverseXpMultiplierSchool", 50.0f) / 100.0f;
    gain.multiplierAny = percent("reverseXpMultiplierAny", 10.0f) / 100.0f;
    gain.capAny = percent("reverseXpCapAny", 5.0f);
    gain.capSchool = percent("reverseXpCapSchool", 15.0f);
    gain.capDirect = percent("reverseXpCapDirect", 50.0f);
}

ProgressionManager::XPSettings ReadXPSettingsFromConfig(const nlohmann::json& config)
{
    ProgressionManager::XPSettings xpSettings;
    xpSettings.learningMode = SafeJsonValue<std::string>(config, "learningMode", "perSchool");
    xpSettings.globalMultiplier = SafeJsonValue<float>(config, "xpGlobalMultiplier", 1.0f);
    xpSettings.multiplierDirect = SafeJsonValue<float>(config, "xpMultiplierDirect", 100.0f) / 100.0f;
    xpSettings.multiplierSchool = SafeJsonValue<float>(config, "xpMultiplierSchool", 50.0f) / 100.0f;
    xpSettings.multiplierAny = SafeJsonValue<float>(config, "xpMultiplierAny", 10.0f) / 100.0f;
    // XP caps (max contribution from each source)
    xpSettings.capAny = SafeJsonValue<float>(config, "xpCapAny", 5.0f);
    xpSettings.capSchool = SafeJsonValue<float>(config, "xpCapSchool", 15.0f);
    xpSettings.capDirect = SafeJsonValue<float>(config, "xpCapDirect", 50.0f);
    // Tier XP requirements
    xpSettings.xpNovice = SafeJsonValue<float>(config, "xpNovice", 100.0f);
    xpSettings.xpApprentice = SafeJsonValue<float>(config, "xpApprentice", 200.0f);
    xpSettings.xpAdept = SafeJsonValue<float>(config, "xpAdept", 400.0f);
    xpSettings.xpExpert = SafeJsonValue<float>(config, "xpExpert", 800.0f);
    xpSettings.xpMaster = SafeJsonValue<float>(config, "xpMaster", 1500.0f);
    ReadReverseUnlockSettings(config, xpSettings);
    return xpSettings;
}

// Apply runtime settings from a fully-merged config JSON.
// Shared between OnLoadUnifiedConfig and the config save (UIManagerConfigSave.cpp).
// Handles: early learning, spell tome, passive learning, and notification settings.
// Game thread: every setter here changes state the game thread reads unlocked.
void ApplySettingsFromConfig(const nlohmann::json& config)
{
    UIManager::SetPanelInfoLogging(SafeJsonValue<bool>(config, "developerMode", false));

    // Early learning settings
    if (config.contains("earlySpellLearning") && !config["earlySpellLearning"].is_null()) {
        auto& elConfig = config["earlySpellLearning"];
        SpellEffectivenessHook::EarlyLearningSettings elSettings;
        elSettings.enabled = SafeJsonValue<bool>(elConfig, "enabled", true);
        elSettings.unlockThreshold = SafeJsonValue<float>(elConfig, "unlockThreshold", 25.0f);
        elSettings.selfCastRequiredAt = SafeJsonValue<float>(elConfig, "selfCastRequiredAt", 75.0f);
        elSettings.selfCastXPMultiplier = SafeJsonValue<float>(elConfig, "selfCastXPMultiplier", 150.0f) / 100.0f;
        elSettings.binaryEffectThreshold = SafeJsonValue<float>(elConfig, "binaryEffectThreshold", 80.0f);
        elSettings.modifyGameDisplay = SafeJsonValue<bool>(elConfig, "modifyGameDisplay", true);
        SpellEffectivenessHook::GetSingleton()->SetSettings(elSettings);

        // Load configurable power steps if present
        if (elConfig.contains("powerSteps") && !elConfig["powerSteps"].is_null() && elConfig["powerSteps"].is_array()) {
            std::vector<SpellEffectivenessHook::PowerStep> steps;
            for (const auto& stepJson : elConfig["powerSteps"]) {
                if (stepJson.is_null()) continue;
                SpellEffectivenessHook::PowerStep step;
                step.progressThreshold = SafeJsonValue<float>(stepJson, "xp", 25.0f);
                step.effectiveness = SafeJsonValue<float>(stepJson, "power", 20.0f) / 100.0f;
                step.label = SafeJsonValue<std::string>(stepJson, "label", "Stage");
                steps.push_back(step);
            }
            if (!steps.empty()) {
                SpellEffectivenessHook::GetSingleton()->SetPowerSteps(steps);
            }
        }
    }

    // Spell tome settings
    if (config.contains("spellTomeLearning") && !config["spellTomeLearning"].is_null()) {
        auto& tomeConfig = config["spellTomeLearning"];
        SpellTomeHook::Settings tomeSettings;
        tomeSettings.enabled = SafeJsonValue<bool>(tomeConfig, "enabled", true);
        tomeSettings.useProgressionSystem = SafeJsonValue<bool>(tomeConfig, "useProgressionSystem", true);
        tomeSettings.grantXPOnRead = SafeJsonValue<bool>(tomeConfig, "grantXPOnRead", true);
        tomeSettings.autoSetLearningTarget = SafeJsonValue<bool>(tomeConfig, "autoSetLearningTarget", true);
        tomeSettings.showNotifications = SafeJsonValue<bool>(tomeConfig, "showNotifications", true);
        tomeSettings.xpPercentToGrant = SafeJsonValue<float>(tomeConfig, "xpPercentToGrant", 25.0f);
        tomeSettings.tomeInventoryBoost = SafeJsonValue<bool>(tomeConfig, "tomeInventoryBoost", true);
        tomeSettings.tomeInventoryBoostPercent = SafeJsonValue<float>(tomeConfig, "tomeInventoryBoostPercent", 25.0f);
        tomeSettings.requirePrereqs = SafeJsonValue<bool>(tomeConfig, "requirePrereqs", true);
        tomeSettings.requireAllPrereqs = SafeJsonValue<bool>(tomeConfig, "requireAllPrereqs", true);
        tomeSettings.requireSkillLevel = SafeJsonValue<bool>(tomeConfig, "requireSkillLevel", false);
        SpellTomeHook::GetSingleton()->SetSettings(tomeSettings);
        logger::info("UIManager: Applied SpellTomeHook settings - useProgressionSystem: {}, requirePrereqs: {}",
            tomeSettings.useProgressionSystem, tomeSettings.requirePrereqs);
    }

    // Passive learning settings
    if (config.contains("passiveLearning") && !config["passiveLearning"].is_null()) {
        auto& plConfig = config["passiveLearning"];
        SpellLearning::PassiveLearningSource::Settings plSettings;
        plSettings.enabled = SafeJsonValue<bool>(plConfig, "enabled", false);
        plSettings.scope = SafeJsonValue<std::string>(plConfig, "scope", "novice");
        plSettings.xpPerGameHour = SafeJsonValue<float>(plConfig, "xpPerGameHour", 5.0f);
        if (plConfig.contains("maxByTier") && plConfig["maxByTier"].is_object()) {
            auto& tiers = plConfig["maxByTier"];
            plSettings.maxNovice = SafeJsonValue<float>(tiers, "novice", 100.0f);
            plSettings.maxApprentice = SafeJsonValue<float>(tiers, "apprentice", 75.0f);
            plSettings.maxAdept = SafeJsonValue<float>(tiers, "adept", 50.0f);
            plSettings.maxExpert = SafeJsonValue<float>(tiers, "expert", 25.0f);
            plSettings.maxMaster = SafeJsonValue<float>(tiers, "master", 5.0f);
        }
        auto* passiveSource = SpellLearning::PassiveLearningSource::GetSingleton();
        if (passiveSource) {
            passiveSource->SetSettings(plSettings);
        }
        logger::info("UIManager: Applied passive learning settings - enabled: {}, scope: {}",
            plSettings.enabled, plSettings.scope);
    }

    // Notification settings
    if (config.contains("notifications") && !config["notifications"].is_null()) {
        auto& notifConfig = config["notifications"];
        auto* castHandler = SpellCastHandler::GetSingleton();
        if (castHandler) {
            castHandler->SetWeakenedNotificationsEnabled(SafeJsonValue<bool>(notifConfig, "weakenedSpellNotifications", true));
            castHandler->SetNotificationInterval(SafeJsonValue<float>(notifConfig, "weakenedSpellInterval", 10.0f));
            logger::info("UIManager: Applied notification settings - interval: {}s",
                castHandler->GetNotificationInterval());
        }
    }
}

std::filesystem::path GetSettingsFilePath()
{
    return "Data/SKSE/Plugins/SpellLearning/settings.json";
}

std::filesystem::path GetUnifiedConfigPath()
{
    return "Data/SKSE/Plugins/SpellLearning/config.json";
}

// =============================================================================
// UNIFIED CONFIG (All settings in one file)
// =============================================================================

// Generate a complete default config with all required fields
json GenerateDefaultConfig() {
    return json{
        {"hotkey", "F8"},
        {"hotkeyCode", 66},
        {"pauseGameOnFocus", true},  // If false, game continues running when UI is open
        {"cheatMode", false},
        {"verboseLogging", false},
        // Heart animation settings
        {"heartAnimationEnabled", true},
        {"heartPulseSpeed", 0.06},
        {"heartBgOpacity", 1.0},
        {"heartBgColor", "#0a0a14"},
        {"heartRingColor", "#b8a878"},
        {"learningPathColor", "#00ffff"},
        {"activeProfile", "normal"},
        {"learningMode", "perSchool"},
        {"xpGlobalMultiplier", 1},
        {"xpMultiplierDirect", 100},
        {"xpMultiplierSchool", 50},
        {"xpMultiplierAny", 10},
        {"xpCapAny", 5},
        {"xpCapSchool", 15},
        {"xpCapDirect", 50},
        {"xpNovice", 100},
        {"xpApprentice", 200},
        {"xpAdept", 400},
        {"xpExpert", 800},
        {"xpMaster", 1500},
        // Known higher spells (ReadReverseUnlockSettings)
        {"reverseUnlock", true},
        {"reverseUnlockToRoot", false},
        {"reverseUnlockXPNovice", 0.3},
        {"reverseUnlockXPApprentice", 0.4},
        {"reverseUnlockXPAdept", 0.5},
        {"reverseUnlockXPExpert", 0.7},
        {"reverseUnlockXPMaster", 0.8},
        {"reverseXpSeparate", false},
        {"reverseXpGlobalMultiplier", 1},
        {"reverseXpMultiplierDirect", 100},
        {"reverseXpMultiplierSchool", 50},
        {"reverseXpMultiplierAny", 10},
        {"reverseXpCapAny", 5},
        {"reverseXpCapSchool", 15},
        {"reverseXpCapDirect", 50},
        {"revealName", 10},
        {"revealEffects", 25},
        {"revealDescription", 50},
        {"discoveryMode", false},
        {"nodeSizeScaling", true},
        {"earlySpellLearning", {
            {"enabled", true},
            {"unlockThreshold", 25.0f},
            {"selfCastRequiredAt", 75.0f},
            {"selfCastXPMultiplier", 150.0f},
            {"binaryEffectThreshold", 80.0f},
            {"modifyGameDisplay", true},
            {"powerSteps", json::array({
                {{"xp", 25}, {"power", 20}, {"label", "Budding"}},
                {{"xp", 40}, {"power", 35}, {"label", "Developing"}},
                {{"xp", 55}, {"power", 50}, {"label", "Practicing"}},
                {{"xp", 70}, {"power", 65}, {"label", "Advancing"}},
                {{"xp", 85}, {"power", 80}, {"label", "Refining"}},
                {{"xp", 100}, {"power", 100}, {"label", "Mastered"}}
            })}
        }},
        {"spellTomeLearning", {
            {"enabled", true},
            {"useProgressionSystem", true},
            {"grantXPOnRead", true},
            {"autoSetLearningTarget", true},
            {"showNotifications", true},
            {"xpPercentToGrant", 25.0f},
            {"tomeInventoryBoost", true},
            {"tomeInventoryBoostPercent", 25.0f},
            {"requirePrereqs", true},
            {"requireAllPrereqs", true},
            {"requireSkillLevel", false}
        }},
        {"passiveLearning", {
            {"enabled", false},
            {"scope", "novice"},
            {"xpPerGameHour", 5},
            {"maxByTier", {
                {"novice", 100},
                {"apprentice", 75},
                {"adept", 50},
                {"expert", 25},
                {"master", 5}
            }}
        }},
        {"notifications", {
            {"weakenedSpellNotifications", true},
            {"weakenedSpellInterval", 10.0f}
        }},
        {"schoolColors", json::object()},
        {"customProfiles", json::object()}
    };
}

// Recursively merge src into dst, only overwriting non-null values
void MergeJsonNonNull(json& dst, const json& src) {
    if (!src.is_object()) return;
    for (auto& [key, value] : src.items()) {
        if (value.is_null()) continue;  // Skip null values
        if (value.is_object() && dst.contains(key) && dst[key].is_object()) {
            MergeJsonNonNull(dst[key], value);  // Recursive merge for objects
        } else {
            dst[key] = value;  // Overwrite with non-null value
        }
    }
}

void UIManager::OnLoadUnifiedConfig([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: LoadUnifiedConfig requested");

    AddTaskToGameThread("LoadUnifiedConfig", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        auto path = GetUnifiedConfigPath();

    // Saves are written off this thread. One still queued would make this read
    // stale, and one halfway through its write has config.json moved aside -
    // which would look like "no file" and have the defaults written over it.
    auto configFileLock = LockUnifiedConfigFile();

    // Also check legacy paths and merge if needed
    auto legacySettingsPath = GetSettingsFilePath();

    // Start with complete defaults - this ensures all fields exist
    json unifiedConfig = GenerateDefaultConfig();

    // What the file held decides what is merged and written (ConfigFile.h)
    const auto loaded = ConfigFile::Load(path);
    switch (loaded.state) {
        case ConfigFile::State::Loaded:
            MergeJsonNonNull(unifiedConfig, loaded.config);
            logger::info("UIManager: Loaded and merged unified config");
            break;
        case ConfigFile::State::Missing:
            logger::info("UIManager: No config file found, using defaults");
            break;
        case ConfigFile::State::Unreadable:
            // Held by another program or blocked: not moved, not written over
            logger::error("UIManager: config.json could not be read ({}) - left as it is, settings are the defaults "
                          "this session and are not saved", loaded.detail);
            break;
        case ConfigFile::State::MovedAside:
            if (loaded.fromBackup) {
                // The last good save stands in; the .bak stays as it is
                MergeJsonNonNull(unifiedConfig, loaded.config);
                logger::warn("UIManager: config.json is {} - kept as {}, config.json.bak (the last good save) is written as the new one",
                    loaded.detail, PathText::Utf8(loaded.movedTo.filename()));
            } else {
                logger::warn("UIManager: config.json is {} - kept as {}, config.json.bak not used ({}), a new one with only the "
                             "defaults is written; saves this session leave any .bak as it is",
                    loaded.detail, PathText::Utf8(loaded.movedTo.filename()), loaded.backupDetail);
            }
            break;
        case ConfigFile::State::BrokenKept:
            logger::error("UIManager: config.json is {} and could not be moved aside - left as it is, settings are the defaults "
                          "this session and are not saved", loaded.detail);
            break;
    }
    // Every load decides again: one that reads the file lifts an earlier block
    const auto saveMode = ConfigFile::SaveModeFor(loaded);
    UIManager::SetConfigSaveMode(saveMode);

    // Migrate legacy settings only into a first config.json (never over a broken one)
    std::error_code legacyError;
    if (ConfigFile::MergesLegacy(loaded.state) && std::filesystem::exists(legacySettingsPath, legacyError)) {
        try {
            std::ifstream file(legacySettingsPath);
            json legacySettings = json::parse(file);
            MergeJsonNonNull(unifiedConfig, legacySettings);
            logger::info("UIManager: Migrated legacy settings.json");
        } catch (...) {}
    }

    // Create the file when there was none or the old one was moved aside
    if (ConfigFile::WritesDefaults(loaded.state)) {
        std::error_code dirError;
        std::filesystem::create_directories(path.parent_path(), dirError);
        if (FileUtils::WriteAtomically(path, JsonText::Dump(unifiedConfig, 2))) {
            logger::info("UIManager: Created default config file at {}", PathText::Utf8(path));
        } else {
            logger::warn("UIManager: Failed to save default config at {}", PathText::Utf8(path));
        }
    }
    configFileLock.unlock();

    // One value of the wrong type (a hand-edited "hotkeyCode": "F5") used to
    // throw here and skip every setting below it; it now falls back to the default
    const json defaults = GenerateDefaultConfig();

    // Update InputHandler with loaded hotkey
    const auto keyCode = SafeJsonValue<uint32_t>(unifiedConfig, "hotkeyCode", defaults["hotkeyCode"].get<uint32_t>());
    UpdateInputHandlerHotkey(keyCode);
    logger::info("UIManager: Updated hotkey from config: {}", keyCode);

    // Update pause game on focus setting
    const bool pauseGame = SafeJsonValue<bool>(unifiedConfig, "pauseGameOnFocus", defaults["pauseGameOnFocus"].get<bool>());
    GetSingleton()->SetPauseGameOnFocus(pauseGame);
    logger::info("UIManager: Updated pauseGameOnFocus from config: {}", pauseGame);

    // Update ProgressionManager with loaded XP settings
    // All fields are guaranteed to exist from defaults, but use SafeJsonValue for extra safety
    ProgressionManager::XPSettings xpSettings = ReadXPSettingsFromConfig(unifiedConfig);
    // Preserve modded sources registered by API consumers before config loaded
    xpSettings.moddedSources = ProgressionManager::GetSingleton()->GetXPSettings().moddedSources;
    ProgressionManager::GetSingleton()->SetXPSettings(xpSettings);

    // Apply early learning, tome, passive, and notification settings
    ApplySettingsFromConfig(unifiedConfig);

    // The panel's language, for the page to read before it draws next time. Not
    // from a file that was never read: the defaults have no language, and ''
    // would reset the player's choice in user_locale.js.
    if (saveMode != ConfigFile::SaveMode::Blocked) {
        WritePanelLocale(unifiedConfig);
    }

    // The removed LLM (OpenRouter) feature kept its API key and model under "llm".
    // An older config may still have that section: the file keeps it (saves merge
    // and never delete keys), but the panel is not sent it, so the key stays out
    // of the page.
    unifiedConfig.erase("llm");

    // Strip internal sources from config before sending to UI (they have their own UI sections)
    if (unifiedConfig.contains("moddedXPSources") && unifiedConfig["moddedXPSources"].is_object()) {
        auto& sources = ProgressionManager::GetSingleton()->GetXPSettings().moddedSources;
        for (auto it = unifiedConfig["moddedXPSources"].begin(); it != unifiedConfig["moddedXPSources"].end();) {
            if (sources.count(it.key()) && sources.at(it.key()).internal) {
                it = unifiedConfig["moddedXPSources"].erase(it);
            } else {
                ++it;
            }
        }
    }

    // Send to UI
    std::string configStr = JsonText::Dump(unifiedConfig);
    logger::info("UIManager: Sending unified config to UI ({} bytes)", configStr.size());
    instance->CallView("onUnifiedConfigLoaded", configStr.c_str());
    if (saveMode == ConfigFile::SaveMode::Blocked) {
        // The scan status bar; "Error" gives it the error look (cppCallbacks.js updateStatus)
        instance->UpdateStatus("Error: config.json could not be read - settings are the defaults and changes will NOT be "
                               "saved this session (see SpellLearning.log)");
    }

    // Re-notify all registered external modded XP sources to the UI.
    // Sources registered before PrismaUI was ready had their notifications dropped,
    // so we push them all now that the view is live. Skip internal sources (e.g. passive).
    auto& moddedSources = ProgressionManager::GetSingleton()->GetXPSettings().moddedSources;
    int notifiedCount = 0;
    for (auto& [srcId, srcConfig] : moddedSources) {
        if (srcConfig.internal) continue;
        instance->NotifyModdedSourceRegistered(srcId, srcConfig.displayName, srcConfig.multiplier, srcConfig.cap);
        notifiedCount++;
    }
    if (notifiedCount > 0) {
        logger::info("UIManager: Re-notified {} modded XP sources to UI", notifiedCount);
    }
    });
}
