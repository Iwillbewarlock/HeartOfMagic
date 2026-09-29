#include "Common.h"
#include "PathText.h"
#include "uimanager/UIManager.h"
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

namespace
{
    bool IsJsonFile(const std::filesystem::path& path)
    {
        const auto ext = PathText::Utf8(path.extension());
        return ext == ".json" || ext == ".JSON";
    }

    std::string AsciiLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // Removes the other files of one preset from dir. Builds before PathText::FromUtf8 wrote a
    // non-ASCII name through the ANSI code page, so its file name is garbled and only the "name"
    // inside finds it. keep (the file just written; empty for a delete) is never removed, compared
    // with equivalent(): NTFS keeps an existing file's case, so "Default.json" written over
    // DEFAULT.json stays DEFAULT.json on disk. After a save (matchStem false) only the "name" inside
    // counts and a file whose stem is safeName in any case is that same slot, never removed; a
    // delete (matchStem true) also takes a file whose stem is safeName.
    // Each file has its own try: one odd file must not stop the rest.
    std::size_t RemovePresetsNamed(const std::filesystem::path& dir, const std::string& name,
        const std::string& safeName, const std::filesystem::path& keep, bool matchStem)
    {
        std::size_t removed = 0;
        std::error_code error;
        if (!std::filesystem::is_directory(dir, error)) return removed;
        std::filesystem::directory_iterator it(dir, error);
        const std::filesystem::directory_iterator end;
        const std::string slot = AsciiLower(safeName);
        for (; !error && it != end; it.increment(error)) {
            try {
                const auto& path = it->path();
                if (!it->is_regular_file() || !IsJsonFile(path)) continue;
                std::error_code same;
                if (!keep.empty() && std::filesystem::equivalent(path, keep, same)) continue;
                const std::string stem = PathText::Utf8(path.stem());
                bool match = false;
                if (matchStem) {
                    match = stem == safeName;
                } else if (AsciiLower(stem) == slot) {
                    continue;
                }
                if (!match) {
                    std::ifstream file(path);
                    const json data = json::parse(file, nullptr, false);
                    match = data.is_object() && data.contains("name") && data["name"].is_string() &&
                            data["name"].get<std::string>() == name;
                }
                if (match && std::filesystem::remove(path)) {
                    ++removed;
                    logger::info("UIManager: removed preset file '{}' (preset '{}')", PathText::Utf8(path), name);
                }
            } catch (const std::exception& e) {
                logger::warn("UIManager: skipped a preset file while looking for '{}': {}", name, e.what());
            }
        }
        if (error) {
            logger::warn("UIManager: the preset folder walk stopped early: {}", error.message());
        }
        return removed;
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

            auto filePath = dir / PathText::FromUtf8(safeName + ".json");
            std::ofstream file(filePath);
            if (!file.is_open()) {
                logger::error("UIManager: SavePreset - failed to open {}", PathText::Utf8(filePath));
                return;
            }
            file << data.dump(2);
            file.close();
            if (!file) {
                // Nothing else is removed when this copy may not be complete
                logger::error("UIManager: SavePreset - failed to write {}", PathText::Utf8(filePath));
                return;
            }

            // A copy of this preset under another file name (an older build's garbled name) would
            // list twice and come back after a delete
            RemovePresetsNamed(dir, name, safeName, filePath, false);

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
            auto filePath = dir / PathText::FromUtf8(safeName + ".json");

            if (std::filesystem::exists(filePath)) {
                std::filesystem::remove(filePath);
                logger::info("UIManager: DeletePreset - deleted {}/{}.json", type, safeName);
            } else if (RemovePresetsNamed(dir, name, safeName, {}, true) == 0) {
                // Not under its own name: an older build may have saved it under a garbled one
                logger::warn("UIManager: DeletePreset - file not found: {}", PathText::Utf8(filePath));
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
                        if (!entry.is_regular_file() || !IsJsonFile(entry.path())) continue;
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
