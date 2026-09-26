#include "Common.h"
#include "FileUtils.h"

#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

#include "uimanager/UIManagerInternal.h"

// =============================================================================
// PANEL LANGUAGE FILE
// =============================================================================
//
// The panel has to know its language before it draws anything, but the saved
// settings only reach it a moment later, and PrismaUI does not keep the page's
// localStorage between game sessions. So every start drew in lang/locale.js's
// language and then switched: everything marked data-i18n changed, while text
// scripts had already built stayed in the first language.
//
// The choice is therefore also written as a one-line script that index.html
// loads ahead of i18n.js. It lives in the panel's own lang folder because the
// page can only read files under its view; under Mod Organizer the write lands
// in overwrite, so a mod update does not reset it.
//
// While the panel's view exists the game holds the file (it loaded it), and
// replacing it fails ("Access is denied") - on every settings save, so a
// language changed in game never reached it and the next start drew in the old
// one first. The file is only read when the view is made, so it is brought in
// line with the saved settings just before that (WritePanelLocaleFromSavedConfig,
// UIManager::Initialize); a save that cannot write it tries once per content,
// not on every save.

namespace
{
    constexpr const char* kPanelUserLocalePath =
        "Data/PrismaUI/views/SpellLearning/SpellLearningPanel/lang/user_locale.js";
    constexpr std::size_t kMaxLocaleCodeLength = 16;

    // Only what lang/*.js file names use ("ko", "pt-br", "zh-cn"): the code is
    // written inside a JS string, so nothing else may reach the file
    bool IsLocaleCode(const std::string& code)
    {
        if (code.size() > kMaxLocaleCodeLength) return false;
        for (char c : code) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    std::string ReadWholeFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // The content a write of which failed (the view holds the file): not tried again this session
    std::mutex g_failedLocaleMutex;
    std::string g_failedLocaleContent;
}

void WritePanelLocale(const nlohmann::json& config)
{
    auto code = SafeJsonValue<std::string>(config, "language", "");
    if (!IsLocaleCode(code)) {
        logger::warn("UIManager: language '{}' is not a locale code - panel language file not written", code);
        return;
    }

    std::filesystem::path path(kPanelUserLocalePath);
    std::error_code ec;
    // No panel installed where expected: do not create its folders
    if (!std::filesystem::is_directory(path.parent_path(), ec)) return;

    bool exists = std::filesystem::exists(path, ec);
    // '' means "whatever locale.js says": with no file that is already the case
    if (code.empty() && !exists) return;

    std::string content =
        std::string("// Written by SpellLearning.dll from Settings > UI Display > Language. Do not edit:\r\n"
                    "// it is rewritten whenever the settings are saved. '' = use lang/locale.js.\r\n"
                    "window._i18nUserLocale = '") + code + "';\r\n";

    if (exists && ReadWholeFile(path) == content) return;

    {
        std::lock_guard<std::mutex> lock(g_failedLocaleMutex);
        if (content == g_failedLocaleContent) return;   // held by the view: next start (see the top)
    }
    if (FileUtils::WriteAtomically(path, content, false)) {
        logger::info("UIManager: panel language file set to '{}'", code);
    } else {
        std::lock_guard<std::mutex> lock(g_failedLocaleMutex);
        g_failedLocaleContent = content;
        logger::warn("UIManager: could not write {} (the panel holds it) - written before the panel opens next time",
                     path.string());
    }
}

void WritePanelLocaleFromSavedConfig()
{
    std::error_code ec;
    auto path = GetUnifiedConfigPath();
    if (!std::filesystem::exists(path, ec)) return;
    auto config = nlohmann::json::parse(ReadWholeFile(path), nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        logger::warn("UIManager: {} could not be read - panel language file left as it is", path.string());
        return;
    }
    WritePanelLocale(config);
}
