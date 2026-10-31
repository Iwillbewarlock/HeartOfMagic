#include "Common.h"
#include "JsonText.h"
#include "PathText.h"
#include "uimanager/UIManager.h"
#include "uimanager/PresetFiles.h"
#include "EncodingUtils.h"
#include "HudNotify.h"
#include "ThreadUtils.h"

#include <optional>

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

namespace
{
    // A preset's folder and file name part, sanitized; nullopt (logged) for an empty one or
    // one that still holds ".." after sanitizing (path traversal). `what` names the caller
    std::optional<std::string> SafePresetPart(const std::string& text, const char* what)
    {
        if (text.empty()) {
            logger::warn("UIManager: {} - missing type or name", what);
            return std::nullopt;
        }
        std::string safe = EncodingUtils::SanitizeFilename(text);
        if (safe.find("..") != std::string::npos) {
            logger::error("UIManager: {} - rejected suspicious name/type", what);
            return std::nullopt;
        }
        return safe;
    }

    // Writes <presets>/<type>/<name>.json (PresetFiles::Write: temp file and move); false,
    // logged, when the type or name is refused or the file could not be written
    bool WritePresetFile(const std::string& type, const std::string& name, const nlohmann::json& data, const char* what)
    {
        const auto safeType = SafePresetPart(type, what);
        const auto safeName = SafePresetPart(name, what);
        if (!safeType || !safeName) return false;

        const auto dir = GetPresetsBasePath() / PathText::FromUtf8(*safeType);
        std::filesystem::create_directories(dir);

        const auto filePath = PresetFiles::FilePath(dir, *safeName);
        if (!PresetFiles::Write(filePath, JsonText::Dump(data, 2))) {
            logger::error("UIManager: {} - failed to write {}", what, PathText::Utf8(filePath));
            return false;
        }
        logger::info("UIManager: {} - saved {}/{}.json", what, type, *safeName);
        return true;
    }

    // Deletes the preset's file (and an older build's file for a non-ASCII name: PresetFiles::Delete)
    void DeletePresetFiles(const std::string& type, const std::string& name, const char* what)
    {
        const auto safeType = SafePresetPart(type, what);
        const auto safeName = SafePresetPart(name, what);
        if (!safeType || !safeName) return;

        const auto dir = GetPresetsBasePath() / PathText::FromUtf8(*safeType);
        const auto result = PresetFiles::Delete(dir, name, *safeName);
        for (const auto& problem : result.problems) {
            logger::warn("UIManager: {} - {}", what, problem);
        }
        if (result.walkError) {
            logger::warn("UIManager: {} - the folder walk stopped early: {}", what, result.walkError.message());
        }
        if (result.direct) {
            logger::info("UIManager: {} - deleted {}/{}.json", what, type, *safeName);
        } else if (!result.legacy.empty()) {
            for (const auto& path : result.legacy) {
                logger::info("UIManager: {} - deleted '{}' (an older build's file for '{}')", what,
                    PathText::Utf8(path), name);
            }
        } else {
            logger::warn("UIManager: {} - no file for {}/{}", what, type, name);
        }
    }
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
            (void)WritePresetFile(args.value("type", ""), args.value("name", ""), args.value("data", json::object()),
                                  "SavePreset");
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
            DeletePresetFiles(args.value("type", ""), args.value("name", ""), "DeletePreset");
        } catch (const std::exception& e) {
            logger::error("UIManager: DeletePreset exception: {}", e.what());
        }
    });
}

// The panel renamed a preset. It used to send DeletePreset then SavePreset: a new name
// SavePreset refused, or a write that failed, left neither file. PresetFiles::Rename deletes
// the old files only once the new one is written. A failure is logged and shown on the
// Scan tab's status line (UpdateStatus) and in a corner message (the Settings tab, where
// the settings presets are, has no status line); the panel's list keeps the new name until
// it reads the presets again (onPresetsLoaded would apply the scanner preset again), the
// old file still on disk. A name whose file is another preset's ("Fire." onto "Fire") is
// refused unless the panel says the player confirmed replacing it ("overwrite").
void UIManager::OnRenamePreset(const char* argument)
{
    if (!argument || strlen(argument) == 0) {
        logger::warn("UIManager: RenamePreset - no data provided");
        return;
    }

    std::string argStr(argument);

    AddTaskToGameThread("RenamePreset", [argStr]() {
        try {
            json args = json::parse(argStr);
            const std::string type = args.value("type", "");
            const std::string oldName = args.value("oldName", "");
            const std::string newName = args.value("newName", "");
            const auto fail = [&]() {
                logger::error("UIManager: RenamePreset - '{}' not renamed to '{}' ({}): its file is kept", oldName,
                    newName, type);
                // No "saved" in the text: updateStatus would show it as a success
                GetSingleton()->UpdateStatus(std::format(
                    "Error: preset '{}' could not be renamed to '{}' - the old file is kept (see SpellLearning.log)",
                    oldName, newName));
                HudNotify::Show("hud.presetRenameFailed",
                    "Preset {{name}} could not be renamed. It is kept under its old name.", { { "name", oldName } });
            };
            const auto safeType = SafePresetPart(type, "RenamePreset");
            const auto safeOld = SafePresetPart(oldName, "RenamePreset");
            const auto safeNew = SafePresetPart(newName, "RenamePreset");
            if (!safeType || !safeOld || !safeNew) return fail();

            const auto dir = GetPresetsBasePath() / PathText::FromUtf8(*safeType);
            std::error_code dirError;
            std::filesystem::create_directories(dir, dirError);
            if (dirError) {
                logger::error("UIManager: RenamePreset - cannot make {}: {}", PathText::Utf8(dir), dirError.message());
                return fail();
            }
            const bool overwrite = args.value("overwrite", false);
            const auto result = PresetFiles::Rename(dir, oldName, *safeOld, *safeNew,
                                                    JsonText::Dump(args.value("data", json::object()), 2), overwrite);
            for (const auto& problem : result.problems) {
                logger::warn("UIManager: RenamePreset - {}", problem);
            }
            if (result.refused) {
                logger::error("UIManager: RenamePreset - {} is another preset's file and no overwrite was confirmed",
                    PathText::Utf8(result.newPath));
                return fail();
            }
            if (!result.written) {
                logger::error("UIManager: RenamePreset - failed to write {}", PathText::Utf8(result.newPath));
                return fail();
            }
            logger::info("UIManager: RenamePreset - saved {}/{}.json", type, *safeNew);
            for (const auto& path : result.removed) {
                logger::info("UIManager: RenamePreset - deleted '{}' ('{}')", PathText::Utf8(path), oldName);
            }
            for (const auto& path : result.kept) {
                logger::info("UIManager: RenamePreset - '{}' is the new file too, kept", PathText::Utf8(path));
            }
            if (result.removed.empty() && result.kept.empty()) {
                logger::warn("UIManager: RenamePreset - no file for {}/{} to delete", type, oldName);
            }
        } catch (const std::exception& e) {
            logger::error("UIManager: RenamePreset exception: {}", e.what());
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

            std::string resultStr = JsonText::Dump(result);
            logger::info("UIManager: LoadPresets - sending {} {} presets to UI",
                         result["presets"].size(), type);
            instance->CallView("onPresetsLoaded", resultStr.c_str());

        } catch (const std::exception& e) {
            logger::error("UIManager: LoadPresets exception: {}", e.what());
        }
    });
}
