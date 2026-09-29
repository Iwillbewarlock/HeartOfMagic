#include "Common.h"
#include "PathText.h"
#include "uimanager/UIManager.h"
#include "uimanager/PresetFiles.h"
#include "EncodingUtils.h"
#include "ThreadUtils.h"

// =============================================================================
// CLIPBOARD CALLBACKS (Windows API)
// =============================================================================

void UIManager::OnGetClipboard([[maybe_unused]] const char* argument)
{
    logger::info("UIManager: GetClipboard callback triggered");

    AddTaskToGameThread("GetClipboard", []() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        std::string content;

        // Use Windows clipboard API with UTF-16
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);

            if (hData) {
                WCHAR* pWideText = static_cast<WCHAR*>(GlobalLock(hData));
                if (pWideText) {
                    // Convert UTF-16 back to UTF-8
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pWideText, -1, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 1) {
                        content.resize(static_cast<size_t>(utf8Len));
                        WideCharToMultiByte(CP_UTF8, 0, pWideText, -1, content.data(), utf8Len, nullptr, nullptr);
                        content.pop_back();  // Remove null terminator written by WideCharToMultiByte
                    }
                    GlobalUnlock(hData);
                    logger::info("UIManager: Read {} bytes from clipboard", content.size());
                } else {
                    logger::warn("UIManager: GlobalLock failed on clipboard data");
                }
            } else {
                logger::warn("UIManager: No text data in clipboard");
            }

            CloseClipboard();
        } else {
            logger::error("UIManager: OpenClipboard failed");
        }

        // Send content to UI (even if empty)
        instance->SendClipboardContent(content);
    });
}

// =============================================================================
// PRESET FILE I/O
// =============================================================================

static std::filesystem::path GetPresetsBasePath()
{
    return "Data/SKSE/Plugins/SpellLearning/presets";
}

void UIManager::OnSavePreset(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: SavePreset - no data provided");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("SavePreset", [argStr]() {
        try {
            json args = json::parse(argStr);
            std::string type = args.value("type", "");
            std::string name = args.value("name", "");
            json data = args.value("data", json::object());

            if (type.empty() || name.empty()) {
                logger::warn("UIManager: SavePreset - missing type or name");
                return;
            }

            std::string safeName = EncodingUtils::SanitizeFilename(name);
            // Sanitize type to prevent path traversal
            std::string safeType = EncodingUtils::SanitizeFilename(type);

            // Defense-in-depth: reject if sanitized values still contain ".."
            if (safeName.find("..") != std::string::npos || safeType.find("..") != std::string::npos) {
                logger::error("UIManager: SavePreset - rejected suspicious name/type");
                return;
            }

            auto dir = GetPresetsBasePath() / PathText::FromUtf8(safeType);
            std::filesystem::create_directories(dir);

            const auto filePath = PresetFiles::FilePath(dir, safeName);
            if (!PresetFiles::Write(filePath, data.dump(2))) {
                logger::error("UIManager: SavePreset - failed to write {}", PathText::Utf8(filePath));
                return;
            }

            logger::info("UIManager: SavePreset - saved {}/{}.json", type, safeName);
        } catch (const std::exception& e) {
            logger::error("UIManager: SavePreset exception: {}", e.what());
        }
    });
}

void UIManager::OnDeletePreset(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: DeletePreset - no data provided");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("DeletePreset", [argStr]() {
        try {
            json args = json::parse(argStr);
            std::string type = args.value("type", "");
            std::string name = args.value("name", "");

            if (type.empty() || name.empty()) {
                logger::warn("UIManager: DeletePreset - missing type or name");
                return;
            }

            std::string safeName = EncodingUtils::SanitizeFilename(name);
            // Sanitize type to prevent path traversal
            std::string safeType = EncodingUtils::SanitizeFilename(type);

            // Defense-in-depth: reject if sanitized values still contain ".."
            if (safeName.find("..") != std::string::npos || safeType.find("..") != std::string::npos) {
                logger::error("UIManager: DeletePreset - rejected suspicious name/type");
                return;
            }

            const auto dir = GetPresetsBasePath() / PathText::FromUtf8(safeType);
            const auto result = PresetFiles::Delete(dir, name, safeName);
            for (const auto& problem : result.problems) {
                logger::warn("UIManager: DeletePreset - {}", problem);
            }
            if (result.walkError) {
                logger::warn("UIManager: DeletePreset - the folder walk stopped early: {}", result.walkError.message());
            }
            if (result.direct) {
                logger::info("UIManager: DeletePreset - deleted {}/{}.json", type, safeName);
            } else if (!result.legacy.empty()) {
                for (const auto& path : result.legacy) {
                    logger::info("UIManager: DeletePreset - deleted '{}' (an older build's file for '{}')",
                        PathText::Utf8(path), name);
                }
            } else {
                logger::warn("UIManager: DeletePreset - no file for {}/{}", type, name);
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: DeletePreset exception: {}", e.what());
        }
    });
}

void UIManager::OnLoadPresets(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: LoadPresets - no data provided");
        return;
    }

    // Copy argument — must defer via AddTask to avoid re-entrant JS calls.
    // InteropCall back into JS from within a RegisterJSListener callback
    // doesn't work in Ultralight (re-entrant), so we defer to SKSE task thread.
    std::string argStr(argument);

    AddTaskToGameThread("LoadPresets", [argStr]() {
        auto* instance = GetSingleton();
        if (!instance || !instance->m_prismaUI) return;

        try {
            json args = json::parse(argStr);
            std::string type = args.value("type", "");

            if (type.empty()) {
                logger::warn("UIManager: LoadPresets - missing type");
                return;
            }

            // Sanitize type to prevent path traversal
            std::string safeType = EncodingUtils::SanitizeFilename(type);

            // Defense-in-depth: reject if sanitized value still contains ".."
            if (safeType.find("..") != std::string::npos) {
                logger::error("UIManager: LoadPresets - rejected suspicious type");
                return;
            }

            auto dir = GetPresetsBasePath() / PathText::FromUtf8(safeType);
            json result;
            result["type"] = type;
            result["presets"] = json::array();

            if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    // Every read of the file's name is in here: an oddly named add-on file is skipped,
                    // it does not empty the whole list
                    try {
                        if (!entry.is_regular_file() || !PresetFiles::IsJsonFile(entry.path())) continue;
                        std::ifstream file(entry.path());
                        json presetData = json::parse(file);

                        // Use the filename (without extension) as key, but prefer "name" inside the JSON
                        std::string key = PathText::Utf8(entry.path().stem());
                        if (presetData.contains("name") && presetData["name"].is_string()) {
                            key = presetData["name"].get<std::string>();
                        }

                        json presetEntry;
                        presetEntry["key"] = key;
                        presetEntry["data"] = presetData;
                        result["presets"].push_back(presetEntry);

                        logger::info("UIManager: LoadPresets - loaded {}/{}", type, key);
                    } catch (const std::exception& e) {
                        std::string where = "a file";
                        try {
                            where = PathText::Utf8(entry.path());
                        } catch (...) {
                            // an ill-formed name: the message alone
                        }
                        logger::warn("UIManager: LoadPresets - skipped {}: {}", where, e.what());
                    }
                }
            } else {
                // Directory doesn't exist yet - that's fine, return empty array
                logger::info("UIManager: LoadPresets - no presets directory for type '{}'", type);
            }

            std::string resultStr = result.dump();
            logger::info("UIManager: LoadPresets - sending {} {} presets to UI",
                         result["presets"].size(), type);
            instance->CallView("onPresetsLoaded", resultStr.c_str());

        } catch (const std::exception& e) {
            logger::error("UIManager: LoadPresets exception: {}", e.what());
        }
    });
}
