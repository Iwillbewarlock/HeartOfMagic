#include "Common.h"
#include "PathText.h"
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
    constexpr const char* kPanelLangDir = "Data/PrismaUI/views/SpellLearning/SpellLearningPanel/lang";
    constexpr const char* kShippedLocaleMarker = "_i18nLocale";
    constexpr const char* kEnglish = "en";
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
                     PathText::Utf8(path));
    }
}

void WritePanelLocaleFromSavedConfig()
{
    std::error_code ec;
    auto path = GetUnifiedConfigPath();
    if (!std::filesystem::exists(path, ec)) return;
    auto config = nlohmann::json::parse(ReadWholeFile(path), nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        logger::warn("UIManager: {} could not be read - panel language file left as it is", PathText::Utf8(path));
        return;
    }
    WritePanelLocale(config);
}

// =============================================================================
// PANEL TEXT FOR THE DLL
// =============================================================================
//
// The DLL shows a few lines itself (the Spell Research HUD line). They live in
// the panel's language files with everything else, so a translator has one
// place: the language the player chose, or the one lang/locale.js ships with.

namespace
{
    // lang/locale.js: window._i18nLocale = 'ko';
    std::string ShippedLocale()
    {
        const std::string text = ReadWholeFile(std::filesystem::path(kPanelLangDir) / "locale.js");
        const auto at = text.find(kShippedLocaleMarker);
        if (at == std::string::npos) return {};
        const auto open = text.find_first_of("'\"", at);
        if (open == std::string::npos) return {};
        const auto close = text.find(text[open], open + 1);
        if (close == std::string::npos) return {};
        std::string code = text.substr(open + 1, close - open - 1);
        return IsLocaleCode(code) ? code : std::string();
    }

    // A flat key ("a.b.c") or the same key nested, as lang/build-preloads.js reads both
    std::string LookupKey(const nlohmann::json& lang, const std::string& key)
    {
        const auto flat = lang.find(key);
        if (flat != lang.end() && flat->is_string()) return flat->get<std::string>();
        const nlohmann::json* node = &lang;
        std::size_t start = 0;
        while (start <= key.size()) {
            const auto dot = key.find('.', start);
            const std::string part = key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!node->is_object()) return {};
            const auto next = node->find(part);
            if (next == node->end()) return {};
            node = &*next;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return node->is_string() ? node->get<std::string>() : std::string();
    }
}

std::string PanelText(const nlohmann::json& config, const char* key, const char* fallback)
{
    std::string code = SafeJsonValue<std::string>(config, "language", "");
    if (!IsLocaleCode(code) || code.empty()) code = ShippedLocale();
    for (const std::string& lang : { code, std::string(kEnglish) }) {
        if (lang.empty()) continue;
        const auto parsed = nlohmann::json::parse(
            ReadWholeFile(std::filesystem::path(kPanelLangDir) / (lang + ".json")), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) continue;
        std::string text = LookupKey(parsed, key);
        if (!text.empty()) return text;
    }
    return fallback;
}
