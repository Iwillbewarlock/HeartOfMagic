#include "Common.h"

#include "Lang.h"

#include "Settings.h"
#include "SrBlockRules.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>

namespace Lang
{
    namespace
    {
        constexpr const char* kLangDir = "Data/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang";
        constexpr const char* kEnglish = "en";
        constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;  // a language file is a few KB

        SrBlock::StringTable g_english;
        std::optional<SrBlock::StringTable> g_language;  // none when the language is English or its file did not read
        std::mutex g_loggedLock;
        std::set<std::string> g_logged;  // keys already logged as missing

        std::filesystem::path FilePath(const std::string& a_code)
        {
            std::error_code ec;
            const auto dir = std::filesystem::current_path(ec);
            const std::filesystem::path relative = std::filesystem::path(kLangDir) / (a_code + ".json");
            return ec ? relative : dir / relative;
        }

        // The string values of one language file; nullopt (logged) when it is missing or not a JSON object
        std::optional<SrBlock::StringTable> Read(const std::string& a_code)
        {
            const auto path = FilePath(a_code);
            const auto shown = SKSE::stl::utf16_to_utf8(path.wstring()).value_or("(path not UTF-8)");
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec) {
                logger::error("Lang: {} not read ({}) - reinstall the add-on", shown, ec.message());
                return std::nullopt;
            }
            if (size > kMaxFileBytes) {
                logger::error("Lang: {} is {} bytes, more than a language file ({}) - not read", shown, size, kMaxFileBytes);
                return std::nullopt;
            }
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            if (file.is_open()) text << file.rdbuf();
            if (!file.is_open() || file.bad()) {
                logger::error("Lang: {} could not be opened", shown);
                return std::nullopt;
            }
            nlohmann::json json;
            try {
                json = nlohmann::json::parse(text.str());
            } catch (const nlohmann::json::exception& e) {
                logger::error("Lang: {} is not valid JSON ({}) - not used", shown, e.what());
                return std::nullopt;
            }
            if (!json.is_object()) {
                logger::error("Lang: {} is not a JSON object - not used", shown);
                return std::nullopt;
            }
            SrBlock::StringTable table;
            std::size_t skipped = 0;
            for (const auto& [key, value] : json.items()) {
                if (value.is_string()) {
                    table.emplace(key, value.get<std::string>());
                } else {
                    ++skipped;
                }
            }
            logger::info("Lang: {} - {} strings{}", shown, table.size(), skipped ? std::format(", {} non-string values skipped", skipped) : "");
            return table;
        }
    }

    void Load()
    {
        g_english = Read(kEnglish).value_or(SrBlock::StringTable{});
        const auto code = Settings::ResolveLanguage();
        if (code != kEnglish) g_language = Read(code);
        std::size_t missing = 0;
        for (const auto& key : SrBlock::RequiredKeys()) {
            if (!g_english.contains(key)) ++missing;
        }
        if (missing > 0) logger::warn("Lang: en.json lacks {} of the block's {} keys - they show as the key", missing, SrBlock::RequiredKeys().size());
        logger::info("Lang: language '{}'{}", code, code != kEnglish && !g_language ? " (its file did not read: English)" : "");
    }

    std::string Get(const std::string& a_key)
    {
        auto looked = SrBlock::LookUp(g_language ? &*g_language : nullptr, g_english, a_key);
        if (looked.source != SrBlock::StringSource::kLanguage && (g_language || looked.source == SrBlock::StringSource::kKey)) {
            const std::scoped_lock lock(g_loggedLock);
            if (g_logged.insert(a_key).second) {
                logger::warn("Lang: '{}' missing - shown as {}", a_key,
                    looked.source == SrBlock::StringSource::kEnglish ? "the English text" : "the key itself");
            }
        }
        return std::move(looked.text);
    }
}
