#include "Common.h"
#include "FileUtils.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"
#include "ProgressionManager.h"
#include "OpenRouterAPI.h"
#include "ThreadUtils.h"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// =============================================================================
// UNIFIED CONFIG SAVE
// =============================================================================
//
// The panel saves its settings every time it closes. Reading config.json,
// merging and writing it back (through a temp file, keeping a .bak) used to
// run as a game-thread task, on the frame the game resumes. The file work now
// runs on one background worker, one save at a time in the order they came
// in; applying the settings still happens on the game thread afterwards,
// because every setter there changes state the game thread reads unlocked.

namespace
{
    // How long a config load waits for saves still queued ahead of it
    constexpr auto kLoadWaitForPendingSaves = std::chrono::seconds(2);

    struct ConfigSaveQueue
    {
        std::mutex queueMutex;             // pending, busy
        std::condition_variable wake;      // a save was queued
        std::condition_variable idle;      // nothing queued or running
        std::deque<std::string> pending;
        bool busy = false;
        std::mutex fileMutex;              // config.json, from reading it to writing it
        std::once_flag started;
    };

    ConfigSaveQueue& SaveQueue()
    {
        static ConfigSaveQueue queue;
        return queue;
    }

    // Game thread: what the old synchronous save applied after its merge
    void ApplySavedConfig(const nlohmann::json& config,
                          std::optional<std::uint32_t> hotkeyCode,
                          std::optional<bool> pauseGameOnFocus)
    {
        if (hotkeyCode) {
            UpdateInputHandlerHotkey(*hotkeyCode);
        }
        if (pauseGameOnFocus) {
            UIManager::GetSingleton()->SetPauseGameOnFocus(*pauseGameOnFocus);
        }

        auto* progression = ProgressionManager::GetSingleton();
        ProgressionManager::XPSettings xpSettings = ReadXPSettingsFromConfig(config);

        // Load modded XP source settings from config
        if (config.contains("moddedXPSources") && config["moddedXPSources"].is_object()) {
            for (auto& [srcId, srcData] : config["moddedXPSources"].items()) {
                ProgressionManager::ModdedSourceConfig sourceConfig;
                sourceConfig.displayName = SafeJsonValue<std::string>(srcData, "displayName", srcId);
                sourceConfig.enabled = SafeJsonValue<bool>(srcData, "enabled", true);
                sourceConfig.multiplier = SafeJsonValue<float>(srcData, "multiplier", 100.0f);
                sourceConfig.cap = SafeJsonValue<float>(srcData, "cap", 25.0f);
                xpSettings.moddedSources[srcId] = sourceConfig;
            }
            logger::debug("UIManager: Loaded {} modded XP source configs", xpSettings.moddedSources.size());
        }

        // Preserve modded sources registered by API consumers that aren't in the saved config
        for (auto& [srcId, srcConfig] : progression->GetXPSettings().moddedSources) {
            if (xpSettings.moddedSources.find(srcId) == xpSettings.moddedSources.end()) {
                xpSettings.moddedSources[srcId] = srcConfig;
            }
        }
        progression->SetXPSettings(xpSettings);

        // Apply early learning, tome, passive, and notification settings
        ApplySettingsFromConfig(config);
    }

    void UpdateOpenRouterConfig(const nlohmann::json& llm)
    {
        OpenRouterAPI::UpdateConfig([&](OpenRouterAPI::Config& config) {
            std::string newKey = SafeJsonValue<std::string>(llm, "apiKey", "");
            if (!newKey.empty() && newKey.find("...") == std::string::npos) {
                config.apiKey = newKey;
            }
            config.model = SafeJsonValue<std::string>(llm, "model", config.model);
            config.maxTokens = SafeJsonValue<int>(llm, "maxTokens", config.maxTokens);
        });

        // Save to OpenRouter's config file too for compatibility
        OpenRouterAPI::SaveConfig();
    }

    // Worker thread: read, merge, write, then hand the result to the game thread
    void WriteUnifiedConfig(const std::string& configData)
    {
        auto path = GetUnifiedConfigPath();

        try {
            nlohmann::json newConfig = nlohmann::json::parse(configData);
            nlohmann::json merged;
            bool written = false;
            {
                std::lock_guard<std::mutex> fileLock(SaveQueue().fileMutex);
                std::filesystem::create_directories(path.parent_path());

                // Load existing config to preserve any fields not in the update
                if (std::filesystem::exists(path)) {
                    try {
                        std::ifstream existingFile(path);
                        merged = nlohmann::json::parse(existingFile);
                    } catch (const std::exception& e) {
                        // The merge below would start from nothing and the write would
                        // then replace every setting the player had with defaults.
                        // Better to save nothing and say why.
                        logger::error("UIManager: {} could not be read ({}) - settings were NOT saved, "
                                      "so the file can be recovered by hand", path.string(), e.what());
                        return;
                    }
                }

                // Deep merge new config into existing (preserves nested keys)
                MergeJsonNonNull(merged, newConfig);

                // Write merged config through a temp file and a move, keeping one .bak
                written = FileUtils::WriteAtomically(path, merged.dump(2));
            }

            if (written) {
                logger::info("UIManager: Unified config saved to {}", path.string());
            } else {
                logger::error("UIManager: Failed to write unified config to {}", path.string());
            }

            // The panel's language, for the page to read before it draws next time
            WritePanelLocale(merged);

            // Also update OpenRouter if LLM settings changed
            if (written && newConfig.contains("llm") && !newConfig["llm"].is_null()) {
                UpdateOpenRouterConfig(newConfig["llm"]);
            }

            std::optional<std::uint32_t> hotkeyCode;
            if (newConfig.contains("hotkeyCode") && newConfig["hotkeyCode"].is_number()) {
                hotkeyCode = newConfig["hotkeyCode"].get<std::uint32_t>();
            }
            std::optional<bool> pauseGameOnFocus;
            if (newConfig.contains("pauseGameOnFocus") && newConfig["pauseGameOnFocus"].is_boolean()) {
                pauseGameOnFocus = newConfig["pauseGameOnFocus"].get<bool>();
            }

            // The game follows what the player chose even if the write failed,
            // as it did when all of this ran on the game thread
            auto config = std::make_shared<const nlohmann::json>(std::move(merged));
            AddTaskToGameThread("ApplyUnifiedConfig", [config, hotkeyCode, pauseGameOnFocus]() {
                ApplySavedConfig(*config, hotkeyCode, pauseGameOnFocus);
            });
        } catch (const std::exception& e) {
            logger::error("UIManager: Failed to save unified config: {}", e.what());
        }
    }

    // Detached rather than joined: when the game exits, Windows ends this
    // thread before static destructors run, and a join there could wait on a
    // lock the ended thread still held. A save cut off that way leaves the
    // previous config.json in place (WriteAtomically moves the new one in
    // only when it is complete).
    void RunConfigSaveWorker()
    {
        auto& queue = SaveQueue();
        for (;;) {
            std::string configData;
            {
                std::unique_lock<std::mutex> lock(queue.queueMutex);
                queue.wake.wait(lock, [&queue] { return !queue.pending.empty(); });
                configData = std::move(queue.pending.front());
                queue.pending.pop_front();
                queue.busy = true;
            }

            WriteUnifiedConfig(configData);

            {
                std::lock_guard<std::mutex> lock(queue.queueMutex);
                queue.busy = false;
            }
            queue.idle.notify_all();
        }
    }

    void QueueConfigSave(std::string configData)
    {
        auto& queue = SaveQueue();
        {
            std::lock_guard<std::mutex> lock(queue.queueMutex);
            queue.pending.push_back(std::move(configData));
        }
        queue.wake.notify_one();

        try {
            std::call_once(queue.started, [] { std::thread(RunConfigSaveWorker).detach(); });
        } catch (const std::exception& e) {
            // The save stays queued; the next one tries to start the worker again
            logger::error("UIManager: could not start the config save thread: {}", e.what());
        }
    }
}

std::unique_lock<std::mutex> LockUnifiedConfigFile()
{
    auto& queue = SaveQueue();
    {
        std::unique_lock<std::mutex> lock(queue.queueMutex);
        const bool drained = queue.idle.wait_for(lock, kLoadWaitForPendingSaves,
            [&queue] { return queue.pending.empty() && !queue.busy; });
        if (!drained) {
            logger::warn("UIManager: config saves still pending after {}s - reading config.json anyway",
                std::chrono::duration_cast<std::chrono::seconds>(kLoadWaitForPendingSaves).count());
        }
    }
    return std::unique_lock<std::mutex>(queue.fileMutex);
}

void UIManager::OnSaveUnifiedConfig(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SaveUnifiedConfig - no data provided");
        return;
    }

    // Debounce: skip if we saved very recently (prevents double-save on panel close)
    auto* instance = GetSingleton();
    {
        std::scoped_lock lock(instance->m_configSaveMutex);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - instance->m_lastConfigSaveTime).count();
        if (elapsed < kConfigSaveDebounceMs) {
            logger::info("UIManager: SaveUnifiedConfig debounced ({}ms since last save)", elapsed);
            return;
        }
        instance->m_lastConfigSaveTime = now;
    }

    logger::info("UIManager: SaveUnifiedConfig");

    // File IO on the save worker; the settings reach the game thread from there
    QueueConfigSave(std::string(argument));
}
