#include "Common.h"

#include "Lang.h"

#include "Settings.h"
#include "SrBlockText.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
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
        constexpr const char* kOwnDir = "Data/SKSE/Plugins/SL_MagicMenu_SpellResearch/lang";
        // SR Prisma's tables (its docs/settings-tab.md "Language files" / "Other plugins": the folder, the files and the
        // archetype.* keys are fixed for other plugins)
        constexpr const char* kSrDir = "Data/PrismaUI/views/SpellResearch/lang";
        constexpr const char* kEnglish = "en";
        constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;  // a language file is tens of KB at most

        std::string g_code = kEnglish;
        SrBlock::StringTable g_english;                    // the block's own en.json
        std::optional<SrBlock::StringTable> g_language;    // the block's own <code>.json (none for English or when missing)
        std::optional<SrBlock::StringTable> g_srEnglish;   // SR Prisma's en.json (none when SR Prisma is not installed)
        std::optional<SrBlock::StringTable> g_srLanguage;  // SR Prisma's <code>.json (none for English or when missing)
        std::mutex g_loggedLock;
        std::set<std::string> g_logged;  // keys already logged as missing

        std::filesystem::path FilePath(const char* a_dir, const std::string& a_code)
        {
            std::error_code ec;
            const auto dir = std::filesystem::current_path(ec);
            const std::filesystem::path relative = std::filesystem::path(a_dir) / (a_code + ".json");
            return ec ? relative : dir / relative;
        }

        enum class Missing
        {
            kError,  // the block's own en.json: it ships with the add-on
            kInfo,   // any other: a language without its file, SR Prisma not installed
        };

        // The string values of one language file (UTF-8, a BOM accepted); nullopt (logged) when it is missing or not a JSON
        // object of strings
        std::optional<SrBlock::StringTable> Read(const std::filesystem::path& a_path, Missing a_missing)
        {
            const auto shown = SKSE::stl::utf16_to_utf8(a_path.wstring()).value_or("(path not UTF-8)");
            std::error_code ec;
            const auto size = std::filesystem::file_size(a_path, ec);
            if (ec) {
                if (a_missing == Missing::kError) {
                    logger::error("Lang: {} not read ({}) - reinstall the add-on", shown, ec.message());
                } else {
                    logger::info("Lang: no {} ({})", shown, ec.message());
                }
                return std::nullopt;
            }
            if (size > kMaxFileBytes) {
                logger::error("Lang: {} is {} bytes, more than a language file ({}) - not read", shown, size, kMaxFileBytes);
                return std::nullopt;
            }
            std::ifstream file(a_path, std::ios::binary);
            std::stringstream text;
            if (file.is_open()) text << file.rdbuf();
            if (!file.is_open() || file.bad()) {
                logger::error("Lang: {} could not be opened", shown);
                return std::nullopt;
            }
            nlohmann::json json;
            try {
                json = nlohmann::json::parse(SrBlock::StripBom(text.str()));
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

        SrBlock::Tables Tables()
        {
            return { .srLanguage = g_srLanguage ? &*g_srLanguage : nullptr,
                .srEnglish = g_srEnglish ? &*g_srEnglish : nullptr,
                .language = g_language ? &*g_language : nullptr,
                .english = &g_english };
        }

        const char* SourceName(SrBlock::StringSource a_source)
        {
            switch (a_source) {
            case SrBlock::StringSource::kSrLanguage: return "SR Prisma's table";
            case SrBlock::StringSource::kSrEnglish: return "SR Prisma's English";
            case SrBlock::StringSource::kLanguage: return "the block's own table";
            case SrBlock::StringSource::kEnglish: return "the block's own English";
            default: return "the key itself";
            }
        }

        // A text that came from a later source than it should (English while a language is set, or the key) is logged once
        std::string Logged(SrBlock::Looked&& a_looked, const std::string& a_key)
        {
            const bool english = a_looked.source == SrBlock::StringSource::kSrEnglish || a_looked.source == SrBlock::StringSource::kEnglish;
            if (a_looked.source == SrBlock::StringSource::kKey || (english && g_code != kEnglish)) {
                const std::scoped_lock lock(g_loggedLock);
                if (g_logged.insert(a_key).second) logger::warn("Lang: '{}' missing in '{}' - shown from {}", a_key, g_code, SourceName(a_looked.source));
            }
            return std::move(a_looked.text);
        }

        std::size_t Count(const std::optional<SrBlock::StringTable>& a_table)
        {
            return a_table ? a_table->size() : 0;
        }
    }

    void Load()
    {
        g_code = Settings::ResolveLanguage();
        g_english = Read(FilePath(kOwnDir, kEnglish), Missing::kError).value_or(SrBlock::StringTable{});
        if (g_code != kEnglish) g_language = Read(FilePath(kOwnDir, g_code), Missing::kInfo);
        g_srEnglish = Read(FilePath(kSrDir, kEnglish), Missing::kInfo);
        if (g_code != kEnglish) g_srLanguage = Read(FilePath(kSrDir, g_code), Missing::kInfo);
        std::size_t missing = 0;
        for (const auto& key : SrBlock::RequiredKeys()) {
            if (!g_english.contains(key)) ++missing;
        }
        if (missing > 0) logger::warn("Lang: the block's en.json lacks {} of its {} keys - they show as the key", missing, SrBlock::RequiredKeys().size());
        logger::info("Lang: language '{}' - SR Prisma's tables {}, the block's own {}", g_code,
            g_srEnglish ? (g_code == kEnglish || g_srLanguage ? "read" : "English only") : "not found (archetype names and groups from the block's files)",
            g_code == kEnglish || g_language ? "read" : "English only");
    }

    std::string Get(const std::string& a_key)
    {
        return Logged(SrBlock::LookUp(Tables(), a_key), a_key);
    }

    std::string ArchetypeName(int a_archetype)
    {
        return Logged(SrBlock::ArchetypeName(Tables(), a_archetype), SrBlock::ArchetypeKey(a_archetype, "short"));
    }

    std::string GroupLabel(SrBlock::Group a_group)
    {
        return Logged(SrBlock::GroupLabel(Tables(), a_group), SrBlock::GroupKey(a_group));
    }

    void SelfTest()
    {
        const auto tables = Tables();
        std::size_t fromSrLanguage = 0, fromSrEnglish = 0, fromOwn = 0;
        for (int a = 0; a < static_cast<int>(SrBlock::kArchetypeCount); ++a) {
            if (SrBlock::GroupOf(a) == SrBlock::Group::kNone) continue;
            switch (SrBlock::ArchetypeName(tables, a).source) {
            case SrBlock::StringSource::kSrLanguage: ++fromSrLanguage; break;
            case SrBlock::StringSource::kSrEnglish: ++fromSrEnglish; break;
            case SrBlock::StringSource::kLanguage:
            case SrBlock::StringSource::kEnglish: ++fromOwn; break;
            default: break;
            }
        }
        const auto unnamed = SrBlock::UnnamedArchetypes(tables);
        std::string list;
        for (const int a : unnamed) list += std::format("{}{}", list.empty() ? "" : ",", a);
        logger::info("[selftest-sr] lang {} {} - SR table {} keys {}, SR en keys {}, own {} keys {}, own en keys {}; shown archetypes named by SR {} {}, SR en {}, "
                     "own files {}{}",
            g_code, unnamed.empty() ? "PASS" : "FAIL", g_code, Count(g_srLanguage), Count(g_srEnglish), g_code, Count(g_language), g_english.size(), g_code,
            fromSrLanguage, fromSrEnglish, fromOwn, unnamed.empty() ? std::string{} : " - NO NAME for archetype(s) " + list);
        logger::info("[selftest-sr] lang groups: {} / {} / {} (from {} / {} / {})", GroupLabel(SrBlock::Group::kElement), GroupLabel(SrBlock::Group::kTargeting),
            GroupLabel(SrBlock::Group::kTechnique), SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kElement).source),
            SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kTargeting).source), SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kTechnique).source));
    }
}
