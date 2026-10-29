#include "Common.h"

#include "Lang.h"

#include "Settings.h"
#include "SrBlockText.h"

#include <atomic>
#include <filesystem>
#include <memory>
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

        // The four tables of one language, read together and never changed after: a language change at a menu open (Refresh)
        // publishes a new bundle by an atomic swap, so draw (UI thread) and the HUD task (game thread) never see one half-read
        struct Bundle
        {
            std::string code = kEnglish;
            SrBlock::StringTable english;                    // the block's own en.json
            std::optional<SrBlock::StringTable> language;    // the block's own <code>.json (none for English or when missing)
            std::optional<SrBlock::StringTable> srEnglish;   // SR Prisma's en.json (none when SR Prisma is not installed)
            std::optional<SrBlock::StringTable> srLanguage;  // SR Prisma's <code>.json (none for English or when missing)
        };
        std::atomic<std::shared_ptr<const Bundle>> g_bundle{ std::make_shared<const Bundle>() };
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

        // The string values of one language file (SrBlock::ReadTableFile: UTF-8, a BOM accepted, a flat object of strings);
        // nullopt (logged) when it is missing or not such an object
        std::optional<SrBlock::StringTable> Read(const std::filesystem::path& a_path, Missing a_missing)
        {
            const auto shown = SKSE::stl::utf16_to_utf8(a_path.wstring()).value_or("(path not UTF-8)");
            auto read = SrBlock::ReadTableFile(a_path, kMaxFileBytes);
            switch (read.problem) {
            case SrBlock::TableProblem::kNone:
                logger::info("Lang: {} - {} strings{}", shown, read.table->size(),
                    read.skipped ? std::format(", {} non-string values skipped", read.skipped) : "");
                return std::move(read.table);
            case SrBlock::TableProblem::kMissing:
                if (a_missing == Missing::kError) {
                    logger::error("Lang: {} not read ({}) - reinstall the add-on", shown, read.detail);
                } else {
                    logger::info("Lang: no {} ({})", shown, read.detail);
                }
                return std::nullopt;
            case SrBlock::TableProblem::kTooBig:
                logger::error("Lang: {} is {}, more than a language file ({}) - not read", shown, read.detail, kMaxFileBytes);
                return std::nullopt;
            case SrBlock::TableProblem::kNotJson:
                logger::error("Lang: {} is not valid JSON ({}) - not used", shown, read.detail);
                return std::nullopt;
            default:
                logger::error("Lang: {} - {} - not used", shown, read.detail);
                return std::nullopt;
            }
        }

        SrBlock::Tables Tables(const Bundle& a_bundle)
        {
            return { .srLanguage = a_bundle.srLanguage ? &*a_bundle.srLanguage : nullptr,
                .srEnglish = a_bundle.srEnglish ? &*a_bundle.srEnglish : nullptr,
                .language = a_bundle.language ? &*a_bundle.language : nullptr,
                .english = &a_bundle.english };
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
        std::string Logged(SrBlock::Looked&& a_looked, const std::string& a_key, const std::string& a_code)
        {
            const bool english = a_looked.source == SrBlock::StringSource::kSrEnglish || a_looked.source == SrBlock::StringSource::kEnglish;
            if (a_looked.source == SrBlock::StringSource::kKey || (english && a_code != kEnglish)) {
                const std::scoped_lock lock(g_loggedLock);
                if (g_logged.insert(a_code + "|" + a_key).second) {
                    logger::warn("Lang: '{}' missing in '{}' - shown from {}", a_key, a_code, SourceName(a_looked.source));
                }
            }
            return std::move(a_looked.text);
        }

        std::shared_ptr<const Bundle> ReadBundle(const std::string& a_code)
        {
            auto bundle = std::make_shared<Bundle>();
            bundle->code = a_code;
            bundle->english = Read(FilePath(kOwnDir, kEnglish), Missing::kError).value_or(SrBlock::StringTable{});
            if (a_code != kEnglish) bundle->language = Read(FilePath(kOwnDir, a_code), Missing::kInfo);
            bundle->srEnglish = Read(FilePath(kSrDir, kEnglish), Missing::kInfo);
            if (a_code != kEnglish) bundle->srLanguage = Read(FilePath(kSrDir, a_code), Missing::kInfo);
            logger::info("Lang: language '{}' - SR Prisma's tables {}, the block's own {}", a_code,
                bundle->srEnglish ? (a_code == kEnglish || bundle->srLanguage ? "read" : "English only") :
                                    "not found (archetype names and groups from the block's files)",
                a_code == kEnglish || bundle->language ? "read" : "English only");
            return bundle;
        }

        std::size_t Count(const std::optional<SrBlock::StringTable>& a_table)
        {
            return a_table ? a_table->size() : 0;
        }
    }

    void Load()
    {
        auto bundle = ReadBundle(Settings::ResolveLanguage());
        std::size_t missing = 0;
        for (const auto& key : SrBlock::RequiredKeys()) {
            if (!bundle->english.contains(key)) ++missing;
        }
        if (missing > 0) logger::warn("Lang: the block's en.json lacks {} of its {} keys - they show as the key", missing, SrBlock::RequiredKeys().size());
        g_bundle.store(std::move(bundle));
    }

    void Refresh()
    {
        const auto code = Settings::ResolveLanguage(false);
        const auto current = g_bundle.load();
        if (code == current->code) return;
        logger::info("Lang: the language changed from '{}' to '{}' (SR Prisma's [Display] sLanguage or the game's) - reading the tables again", current->code, code);
        g_bundle.store(ReadBundle(code));
        if (Settings::SelfTest()) SelfTest();
    }

    std::string Get(const std::string& a_key)
    {
        const auto bundle = g_bundle.load();
        return Logged(SrBlock::LookUp(Tables(*bundle), a_key), a_key, bundle->code);
    }

    std::string ArchetypeName(int a_archetype)
    {
        const auto bundle = g_bundle.load();
        return Logged(SrBlock::ArchetypeName(Tables(*bundle), a_archetype), SrBlock::ArchetypeKey(a_archetype, "short"), bundle->code);
    }

    std::string GroupLabel(SrBlock::Group a_group)
    {
        const auto bundle = g_bundle.load();
        return Logged(SrBlock::GroupLabel(Tables(*bundle), a_group), SrBlock::GroupKey(a_group), bundle->code);
    }

    void SelfTest()
    {
        const auto bundle = g_bundle.load();
        const auto& code = bundle->code;
        const auto& english = bundle->english;
        const auto& language = bundle->language;
        const auto& srEnglish = bundle->srEnglish;
        const auto& srLanguage = bundle->srLanguage;
        const auto tables = Tables(*bundle);
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
        // final review I1: the block's own en.json names everything, so "no name" alone would always pass - in another
        // language SR Prisma's table must name the archetypes (a missing or broken SR ko.json shows here)
        const auto verdict = SrBlock::LanguageVerdict(code, srEnglish.has_value(), code == kEnglish ? fromSrEnglish : fromSrLanguage, unnamed.size());
        logger::info("[selftest-sr] lang {} {} - SR table {} keys {}, SR en keys {}, own {} keys {}, own en keys {}; shown archetypes named by SR {} {}, SR en {}, "
                     "own files {}{}{}",
            code, verdict.verdict, code, Count(srLanguage), Count(srEnglish), code, Count(language), english.size(), code,
            fromSrLanguage, fromSrEnglish, fromOwn, unnamed.empty() ? std::string{} : " - NO NAME for archetype(s) " + list,
            verdict.why.empty() ? std::string{} : " - " + verdict.why);
        logger::info("[selftest-sr] lang groups: {} / {} / {} (from {} / {} / {})", GroupLabel(SrBlock::Group::kElement), GroupLabel(SrBlock::Group::kTargeting),
            GroupLabel(SrBlock::Group::kTechnique), SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kElement).source),
            SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kTargeting).source), SourceName(SrBlock::GroupLabel(tables, SrBlock::Group::kTechnique).source));
    }
}
