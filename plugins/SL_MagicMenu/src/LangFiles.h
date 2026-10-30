#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

// =============================================================================
// LANG FILES - the language files of the magic menu (SL_MagicMenu) and its
// Heart of Magic block (SL_MagicMenu_HoM): which language ([Display] sLanguage
// of SL_MagicMenu.ini, auto = the game's sLanguage:General), the files
// (<lang dir>/<code>.json: one flat JSON object of strings, UTF-8, a BOM
// accepted, "_" keys are notes) and each table key's text: the language's
// file, then en.json, then the built-in English compiled in. A Catalog holds
// one DLL's resolved texts, loaded once per game session (LangGame.h does it
// in the game). No game types: tools/magic-menu-lang-check runs all of it.
// The Spell Research block keeps its own reader (SrBlockText.h).
// =============================================================================

namespace LangFiles
{
    inline constexpr std::string_view kEnglish = "en", kKorean = "ko", kAuto = "auto";
    inline constexpr std::string_view kIniSection = "Display", kIniKey = "sLanguage";
    inline constexpr std::string_view kGameKorean = "KOREAN";  // Skyrim.ini [General] sLanguage of the Korean game
    inline constexpr std::string_view kNotePrefix = "_";      // "_about": a note for translators, not a text
    inline constexpr std::string_view kSeparator = " \xC2\xB7 ";  // " · ", the lines' separator: kept in every language
    inline constexpr std::size_t kMaxCodeLength = 16;
    inline constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;  // a language file is a few KB

    namespace Detail
    {
        inline constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

        inline std::string_view Trim(std::string_view a_text)
        {
            const auto first = a_text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            const auto last = a_text.find_last_not_of(" \t\r\n");
            return a_text.substr(first, last - first + 1);
        }

        inline char LowerAscii(char a_c)
        {
            return a_c >= 'A' && a_c <= 'Z' ? static_cast<char>(a_c - 'A' + 'a') : a_c;
        }

        inline std::string Lower(std::string_view a_text)
        {
            std::string out(a_text);
            std::ranges::transform(out, out.begin(), LowerAscii);
            return out;
        }
    }

    /// A text without its leading UTF-8 byte order mark (Notepad writes one)
    inline std::string_view StripBom(std::string_view a_text)
    {
        return a_text.starts_with(Detail::kUtf8Bom) ? a_text.substr(Detail::kUtf8Bom.size()) : a_text;
    }

    // ---------------------------------------------------------------------
    // The language code
    // ---------------------------------------------------------------------

    /// A code a file can be named by: 1-16 of a-z, 0-9, '-', '_' (no path parts)
    inline bool IsCode(std::string_view a_code)
    {
        return !a_code.empty() && a_code.size() <= kMaxCodeLength &&
               std::ranges::all_of(a_code, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
    }

    /// The game's sLanguage:General as a code: KOREAN -> ko, anything else (ENGLISH, GERMAN, "") -> en
    inline std::string GameLanguageCode(std::string_view a_gameLanguage)
    {
        return std::string(Detail::Lower(Detail::Trim(a_gameLanguage)) == Detail::Lower(kGameKorean) ? kKorean : kEnglish);
    }

    enum class CodeSource
    {
        kSetting,     // sLanguage named a code
        kGame,        // sLanguage auto, empty or not set: the game's language
        kBadSetting,  // sLanguage is not a code: the game's language
    };

    struct CodeChoice
    {
        std::string code;
        CodeSource source = CodeSource::kGame;
    };

    /// sLanguage (as written, may be empty) and the game's language -> the code to look for first
    inline CodeChoice ChooseCode(std::string_view a_setting, std::string_view a_gameLanguage)
    {
        const auto setting = Detail::Lower(Detail::Trim(a_setting));
        if (setting.empty() || setting == kAuto) return { GameLanguageCode(a_gameLanguage), CodeSource::kGame };
        if (IsCode(setting)) return { setting, CodeSource::kSetting };
        return { GameLanguageCode(a_gameLanguage), CodeSource::kBadSetting };
    }

    // ---------------------------------------------------------------------
    // A language file
    // ---------------------------------------------------------------------
    using StringTable = std::unordered_map<std::string, std::string>;

    enum class TableProblem
    {
        kNone,
        kMissing,     // no such file (or its size could not be read)
        kTooBig,      // more than a_maxBytes
        kUnreadable,  // could not be opened or read
        kNotJson,     // not valid JSON
        kNotObject,   // valid JSON, not an object
    };

    struct TableRead
    {
        std::optional<StringTable> table;
        TableProblem problem = TableProblem::kNone;
        std::string detail;       // the system or parser message
        std::size_t skipped = 0;  // non-string values left out
    };

    inline std::filesystem::path FilePath(const std::filesystem::path& a_dir, std::string_view a_code)
    {
        return a_dir / (std::string(a_code) + ".json");
    }

    /// The string values of a language file; "_" note keys and non-string values left out
    inline TableRead ReadTableFile(const std::filesystem::path& a_path, std::uintmax_t a_maxBytes = kMaxFileBytes)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(a_path, ec);
        if (ec) return { std::nullopt, TableProblem::kMissing, ec.message(), 0 };
        if (size > a_maxBytes) return { std::nullopt, TableProblem::kTooBig, std::to_string(size) + " bytes", 0 };
        std::ifstream file(a_path, std::ios::binary);
        std::stringstream text;
        if (file.is_open()) text << file.rdbuf();
        if (!file.is_open() || file.bad()) return { std::nullopt, TableProblem::kUnreadable, "could not be opened", 0 };
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(StripBom(text.str()));
        } catch (const nlohmann::json::exception& e) {
            return { std::nullopt, TableProblem::kNotJson, e.what(), 0 };
        }
        if (!json.is_object()) return { std::nullopt, TableProblem::kNotObject, "not a JSON object", 0 };
        TableRead read;
        StringTable table;
        for (const auto& [key, value] : json.items()) {
            if (key.starts_with(kNotePrefix)) continue;
            if (value.is_string()) {
                table.emplace(key, value.get<std::string>());
            } else {
                ++read.skipped;
            }
        }
        read.table = std::move(table);
        return read;
    }

    // ---------------------------------------------------------------------
    // Texts
    // ---------------------------------------------------------------------

    /// a_text with each {placeholder} replaced (every occurrence). Not std::format: the braces are names
    inline std::string FillText(std::string a_text, std::initializer_list<std::pair<std::string_view, std::string>> a_values)
    {
        for (const auto& [name, value] : a_values) {
            for (auto at = a_text.find(name); at != std::string::npos; at = a_text.find(name, at + value.size())) {
                a_text.replace(at, name.size(), value);
            }
        }
        return a_text;
    }

    /// The {placeholders} of a text, sorted (a translation must keep the same ones)
    inline std::vector<std::string> Placeholders(std::string_view a_text)
    {
        std::vector<std::string> names;
        for (auto open = a_text.find('{'); open != std::string_view::npos; open = a_text.find('{', open + 1)) {
            const auto close = a_text.find('}', open);
            if (close == std::string_view::npos) break;
            names.emplace_back(a_text.substr(open, close - open + 1));
        }
        std::ranges::sort(names);
        return names;
    }

    inline std::size_t CountOf(std::string_view a_text, std::string_view a_part)
    {
        std::size_t count = 0;
        for (auto at = a_text.find(a_part); at != std::string_view::npos; at = a_text.find(a_part, at + a_part.size())) ++count;
        return count;
    }

    namespace Detail
    {
        inline const std::string* Find(const std::optional<StringTable>& a_table, const std::string& a_key)
        {
            if (!a_table) return nullptr;
            const auto it = a_table->find(a_key);
            return it != a_table->end() && !it->second.empty() ? &it->second : nullptr;  // an empty value counts as missing
        }
    }

    /// What one load read and chose (the DLL logs it, LangGame.h)
    struct LoadReport
    {
        std::string setting;  // sLanguage as written ("" when not set)
        std::string game;     // the game's sLanguage:General
        CodeChoice choice;
        std::string shown;    // the code whose file comes first: choice.code, or en when that file is not there
        TableRead language;   // <choice.code>.json (not read for en)
        TableRead english;    // en.json
        std::size_t fromLanguage = 0, fromEnglish = 0, fromBuiltIn = 0;
        std::vector<std::string> notInLanguage;  // keys <code>.json lacks (a language other than en, its file read)
        std::vector<std::string> notInEnglish;   // keys en.json lacks (its file read)
    };

    /// Each entry's text (a_table: rows with .key and .text, the built-in English): <code>.json, en.json, built-in
    template <class Entry, std::size_t N>
    std::pair<std::array<std::string, N>, LoadReport> Resolve(const std::array<Entry, N>& a_table, const std::filesystem::path& a_dir,
        std::string_view a_setting, std::string_view a_gameLanguage)
    {
        LoadReport report{ .setting = std::string(a_setting), .game = std::string(a_gameLanguage), .choice = ChooseCode(a_setting, a_gameLanguage) };
        report.english = ReadTableFile(FilePath(a_dir, kEnglish));
        if (report.choice.code != kEnglish) report.language = ReadTableFile(FilePath(a_dir, report.choice.code));
        report.shown = report.language.table ? report.choice.code : std::string(kEnglish);
        std::array<std::string, N> texts;
        for (std::size_t i = 0; i < N; ++i) {
            const std::string key = a_table[i].key;
            const auto* own = Detail::Find(report.language.table, key);
            const auto* english = Detail::Find(report.english.table, key);
            if (report.language.table && !own) report.notInLanguage.push_back(key);
            if (report.english.table && !english) report.notInEnglish.push_back(key);
            if (own) {
                texts[i] = *own;
                ++report.fromLanguage;
            } else if (english) {
                texts[i] = *english;
                ++report.fromEnglish;
            } else {
                texts[i] = a_table[i].text;
                ++report.fromBuiltIn;
            }
        }
        return { std::move(texts), std::move(report) };
    }

    /// One DLL's texts: the built-in table until Load, the resolved texts from then on. Loaded once (kDataLoaded) and never
    /// changed after, so Text's pointer stays valid for the session and draws on the UI thread need no lock
    template <class Entry, std::size_t N>
    class Catalog
    {
    public:
        constexpr explicit Catalog(const std::array<Entry, N>& a_table) noexcept :
            table_(a_table)
        {}

        Catalog(const Catalog&) = delete;
        Catalog& operator=(const Catalog&) = delete;

        const char* Text(std::size_t a_index) const
        {
            return loaded_.load(std::memory_order_acquire) ? texts_[a_index].c_str() : table_[a_index].text;
        }

        bool Loaded() const { return loaded_.load(std::memory_order_acquire); }

        /// Reads the files and keeps the texts; nullopt when already loaded (the first load stands)
        std::optional<LoadReport> Load(const std::filesystem::path& a_dir, std::string_view a_setting, std::string_view a_gameLanguage)
        {
            if (loaded_.load(std::memory_order_acquire)) return std::nullopt;
            auto [texts, report] = Resolve(table_, a_dir, a_setting, a_gameLanguage);
            texts_ = std::move(texts);
            loaded_.store(true, std::memory_order_release);
            return std::move(report);
        }

    private:
        const std::array<Entry, N>& table_;
        std::array<std::string, N> texts_;
        std::atomic<bool> loaded_{ false };
    };

    // ---------------------------------------------------------------------
    // The log lines of a load (LangGame.h writes them with the DLL's logger)
    // ---------------------------------------------------------------------
    enum class Level
    {
        kInfo,
        kWarn,
        kError,
    };

    namespace Detail
    {
        inline std::string Problem(const TableRead& a_read)
        {
            switch (a_read.problem) {
            case TableProblem::kMissing: return "not found (" + a_read.detail + ")";
            case TableProblem::kTooBig: return "too big (" + a_read.detail + ", more than " + std::to_string(kMaxFileBytes) + ")";
            case TableProblem::kUnreadable: return "unreadable (" + a_read.detail + ")";
            case TableProblem::kNotJson: return "not valid JSON (" + a_read.detail + ")";
            case TableProblem::kNotObject: return "not a JSON object";
            default: return "read";
            }
        }

        inline std::string Keys(const std::vector<std::string>& a_keys)
        {
            std::string list;
            for (const auto& key : a_keys) list += (list.empty() ? "" : ", ") + key;
            return list;
        }
    }

    /// a_shownDir: the language folder as the log shows it (UTF-8)
    inline std::vector<std::pair<Level, std::string>> ReportLines(const LoadReport& a_report, std::string_view a_shownDir)
    {
        std::vector<std::pair<Level, std::string>> lines;
        const auto& r = a_report;
        const std::string why = r.choice.source == CodeSource::kSetting ?
                                    "[Display] sLanguage = '" + r.setting + "'" :
                                    "the game's sLanguage '" + r.game + "', [Display] sLanguage " + (r.setting.empty() ? "not set" : "'" + r.setting + "'");
        if (r.choice.source == CodeSource::kBadSetting) {
            lines.emplace_back(Level::kWarn, "Lang: [Display] sLanguage = '" + r.setting + "' is not a language code (a-z, 0-9, - or _) - the game's language is used");
        }
        lines.emplace_back(Level::kInfo, "Lang: language '" + r.choice.code + "' (" + why + "), folder " + std::string(a_shownDir));
        if (r.choice.code != kEnglish) {
            if (r.language.table) {
                lines.emplace_back(Level::kInfo, "Lang: " + r.choice.code + ".json - " + std::to_string(r.language.table->size()) + " strings");
            } else {
                const auto level = r.language.problem == TableProblem::kMissing ? Level::kInfo : Level::kError;
                lines.emplace_back(level, "Lang: " + r.choice.code + ".json " + Detail::Problem(r.language) + " - English is shown");
            }
        }
        if (r.english.table) {
            lines.emplace_back(Level::kInfo, "Lang: en.json - " + std::to_string(r.english.table->size()) + " strings");
        } else {
            lines.emplace_back(Level::kError, "Lang: en.json " + Detail::Problem(r.english) + " - the built-in English is the fallback; reinstall the mod");
        }
        for (const auto* read : { &r.language, &r.english }) {
            if (read->skipped) lines.emplace_back(Level::kWarn, "Lang: " + std::to_string(read->skipped) + " non-string value(s) skipped in a language file");
        }
        if (!r.notInLanguage.empty()) {
            lines.emplace_back(Level::kWarn, "Lang: " + r.choice.code + ".json lacks " + Detail::Keys(r.notInLanguage) + " - shown in English");
        }
        if (!r.notInEnglish.empty()) lines.emplace_back(Level::kWarn, "Lang: en.json lacks " + Detail::Keys(r.notInEnglish) + " - the built-in text is shown");
        const std::string own = r.choice.code == kEnglish ? std::string{} : std::to_string(r.fromLanguage) + " from " + r.choice.code + ".json, ";
        lines.emplace_back(Level::kInfo, "Lang: shown '" + r.shown + "' - texts: " + own + std::to_string(r.fromEnglish) + " from en.json, " +
                                             std::to_string(r.fromBuiltIn) + " built-in");
        return lines;
    }
}
