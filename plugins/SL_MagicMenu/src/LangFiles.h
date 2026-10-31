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
#include <memory>
#include <mutex>
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
// of SL_MagicMenu.ini, auto = the game's sLanguage:General, or Korean when a
// localized game string holds Hangul - the Korean patch on an English game), the files
// (<lang dir>/<code>.json: one flat JSON object of strings, UTF-8, a BOM
// accepted, "_" keys are notes) and each table key's text: the language's
// file, then en.json, then the built-in English compiled in. A Catalog holds
// one DLL's resolved texts, loaded at kDataLoaded and refreshed at each magic
// menu open when sLanguage changed (LangGame.h does both in the game). No game
// types: tools/magic-menu-lang-check runs all of it.
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

    namespace Detail
    {
        // Hangul code point blocks: Jamo, Compatibility Jamo, Jamo Extended-A, Syllables, Jamo Extended-B
        inline constexpr std::array<std::pair<char32_t, char32_t>, 5> kHangulBlocks{ { { 0x1100, 0x11FF }, { 0x3130, 0x318F }, { 0xA960, 0xA97F },
            { 0xAC00, 0xD7A3 }, { 0xD7B0, 0xD7FF } } };
        inline constexpr char32_t kMaxCodePoint = 0x10FFFF, kSurrogateFirst = 0xD800, kSurrogateLast = 0xDFFF;

        /// The UTF-8 sequence at a_text[a_at] as a code point, a_at moved past it; nullopt (a_at moved one byte) for a byte that
        /// starts no valid sequence (a stray continuation byte, a cut or overlong sequence, a surrogate - CP949 bytes read as UTF-8)
        inline std::optional<char32_t> NextCodePoint(std::string_view a_text, std::size_t& a_at)
        {
            constexpr unsigned char kCont = 0x80, kContMask = 0xC0, kContBits = 0x3F;  // 0x80: the first non-ASCII byte and the continuation mark
            constexpr std::array<char32_t, 5> kLeast{ 0, 0, 0x80, 0x800, 0x10000 };  // the smallest code point of each length (overlong below)
            const auto lead = static_cast<unsigned char>(a_text[a_at++]);
            if (lead < kCont) return lead;
            std::size_t length = 0;
            char32_t point = 0;
            if ((lead & 0xE0) == 0xC0) {
                length = 2;
                point = lead & 0x1F;
            } else if ((lead & 0xF0) == 0xE0) {
                length = 3;
                point = lead & 0x0F;
            } else if ((lead & 0xF8) == 0xF0) {
                length = 4;
                point = lead & 0x07;
            } else {
                return std::nullopt;
            }
            if (a_at + length - 1 > a_text.size()) return std::nullopt;
            for (std::size_t i = 0; i + 1 < length; ++i) {
                const auto next = static_cast<unsigned char>(a_text[a_at + i]);
                if ((next & kContMask) != kCont) return std::nullopt;
                point = (point << 6) | (next & kContBits);
            }
            if (point < kLeast[length] || point > kMaxCodePoint || (point >= kSurrogateFirst && point <= kSurrogateLast)) return std::nullopt;
            a_at += length - 1;
            return point;
        }
    }

    /// a_text (UTF-8) holds at least one Hangul letter (Detail::kHangulBlocks); invalid bytes are skipped, never read as Hangul
    inline bool HasHangul(std::string_view a_text)
    {
        for (std::size_t at = 0; at < a_text.size();) {
            const auto point = Detail::NextCodePoint(a_text, at);
            if (point && std::ranges::any_of(Detail::kHangulBlocks, [&](const auto& a_block) { return *point >= a_block.first && *point <= a_block.second; })) {
                return true;
            }
        }
        return false;
    }

    /// What decided an auto language (the log names it)
    enum class AutoSignal
    {
        kNone,          // not auto (sLanguage named a code)
        kGameLanguage,  // the game's sLanguage is KOREAN
        kGameText,      // a localized game string holds Hangul (Korean .STRINGS on a game whose sLanguage is not KOREAN)
        kNeither,       // neither: English
    };

    /// sLanguage auto: the game's sLanguage KOREAN -> ko; else a_gameText (a localized game string, UTF-8) with Hangul -> ko (the
    /// Korean patch: most Korean players run sLanguage=ENGLISH with Korean .STRINGS files); else en
    inline std::pair<std::string, AutoSignal> AutoCode(std::string_view a_gameLanguage, std::string_view a_gameText)
    {
        if (GameLanguageCode(a_gameLanguage) == kKorean) return { std::string(kKorean), AutoSignal::kGameLanguage };
        if (HasHangul(a_gameText)) return { std::string(kKorean), AutoSignal::kGameText };
        return { std::string(kEnglish), AutoSignal::kNeither };
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
        AutoSignal signal = AutoSignal::kNone;  // kGame / kBadSetting: what decided (AutoCode)
    };

    /// sLanguage (as written, may be empty), the game's language and a localized game string (AutoCode; "" = none read) -> the
    /// code to look for first
    inline CodeChoice ChooseCode(std::string_view a_setting, std::string_view a_gameLanguage, std::string_view a_gameText = {})
    {
        const auto setting = Detail::Lower(Detail::Trim(a_setting));
        const bool automatic = setting.empty() || setting == kAuto;
        if (!automatic && IsCode(setting)) return { setting, CodeSource::kSetting, AutoSignal::kNone };
        auto [code, signal] = AutoCode(a_gameLanguage, a_gameText);
        return { std::move(code), automatic ? CodeSource::kGame : CodeSource::kBadSetting, signal };
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
        std::string gameText; // the localized game string auto looked at for Hangul ("" = none read)
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
        std::string_view a_setting, std::string_view a_gameLanguage, std::string_view a_gameText = {})
    {
        LoadReport report{ .setting = std::string(a_setting), .game = std::string(a_gameLanguage), .gameText = std::string(a_gameText),
            .choice = ChooseCode(a_setting, a_gameLanguage, a_gameText) };
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

    /// Fix round 1 I2: a key's value read from the player's file (SL_MagicMenu_User.ini, the settings tab's) and the shipped
    /// SL_MagicMenu.ini (nullopt: that file has no such key): the player's when it has the key - an empty value too (the player
    /// cleared it) -, else the shipped file's, else ""
    inline std::string Layered(const std::optional<std::string>& a_user, const std::optional<std::string>& a_shipped)
    {
        return a_user ? *a_user : a_shipped.value_or(std::string{});
    }

    /// [Display] sLanguage as a Catalog compares it: trimmed, lower case, "" read as auto
    inline std::string NormalizedSetting(std::string_view a_setting)
    {
        auto setting = Detail::Lower(Detail::Trim(a_setting));
        return setting.empty() ? std::string(kAuto) : setting;
    }

    /// One DLL's texts: the built-in table until Load, the resolved texts from then on. Load once (kDataLoaded); Refresh at each
    /// magic menu open resolves again when [Display] sLanguage changed since (the settings tab writes it). Every resolved set is
    /// kept until the DLL unloads (a few KB per language change), so a pointer Text gave stays valid for the session and draws
    /// on any thread (the UI thread, the settings tab's render thread) need no lock; only Load and Refresh lock
    template <class Entry, std::size_t N>
    class Catalog
    {
    public:
        using Texts = std::array<std::string, N>;

        constexpr explicit Catalog(const std::array<Entry, N>& a_table) noexcept :
            table_(a_table)
        {}

        Catalog(const Catalog&) = delete;
        Catalog& operator=(const Catalog&) = delete;

        const char* Text(std::size_t a_index) const
        {
            const auto* texts = current_.load(std::memory_order_acquire);
            return texts ? (*texts)[a_index].c_str() : table_[a_index].text;
        }

        bool Loaded() const { return current_.load(std::memory_order_acquire) != nullptr; }

        /// Reads the files and keeps the texts; nullopt when already loaded (the first load stands - Refresh changes it)
        std::optional<LoadReport> Load(const std::filesystem::path& a_dir, std::string_view a_setting, std::string_view a_gameLanguage,
            std::string_view a_gameText = {})
        {
            const std::scoped_lock lock(lock_);
            if (current_.load(std::memory_order_acquire)) return std::nullopt;
            return Install(a_dir, a_setting, a_gameLanguage, a_gameText);
        }

        /// Resolves again when a_setting (NormalizedSetting) is not the one the texts were resolved with; nullopt when it is the
        /// same or nothing was loaded yet (Load first). The new texts are kept beside the old ones (a straddling draw reads the old)
        std::optional<LoadReport> Refresh(const std::filesystem::path& a_dir, std::string_view a_setting, std::string_view a_gameLanguage,
            std::string_view a_gameText = {})
        {
            const std::scoped_lock lock(lock_);
            if (!current_.load(std::memory_order_acquire) || NormalizedSetting(a_setting) == setting_) return std::nullopt;
            return Install(a_dir, a_setting, a_gameLanguage, a_gameText);
        }

        /// How many sets of texts were resolved this session (Load and each Refresh that changed them)
        std::size_t Generations() const
        {
            const std::scoped_lock lock(lock_);
            return generations_.size();
        }

    private:
        LoadReport Install(const std::filesystem::path& a_dir, std::string_view a_setting, std::string_view a_gameLanguage, std::string_view a_gameText)
        {
            auto [texts, report] = Resolve(table_, a_dir, a_setting, a_gameLanguage, a_gameText);
            generations_.push_back(std::make_unique<const Texts>(std::move(texts)));
            setting_ = NormalizedSetting(a_setting);
            current_.store(generations_.back().get(), std::memory_order_release);
            return std::move(report);
        }

        const std::array<Entry, N>& table_;
        mutable std::mutex lock_;
        std::vector<std::unique_ptr<const Texts>> generations_;  // never shrinks: Text's pointers stay valid
        std::string setting_;                                    // NormalizedSetting of the current texts
        std::atomic<const Texts*> current_{ nullptr };
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

        /// What decided an auto choice, for the log line ("" when sLanguage named the code)
        inline std::string SignalText(const LoadReport& a_report)
        {
            const std::string text = a_report.gameText.empty() ? "no game text read" : "the game text '" + a_report.gameText + "'";
            switch (a_report.choice.signal) {
            case AutoSignal::kGameLanguage: return "decided by the game's sLanguage";
            case AutoSignal::kGameText: return "decided by Hangul in " + text + " (Korean strings on a game that is not KOREAN)";
            case AutoSignal::kNeither: return "no Korean signal: not KOREAN, no Hangul in " + text;
            default: return {};
            }
        }
    }

    /// a_shownDir: the language folder as the log shows it (UTF-8)
    inline std::vector<std::pair<Level, std::string>> ReportLines(const LoadReport& a_report, std::string_view a_shownDir)
    {
        std::vector<std::pair<Level, std::string>> lines;
        const auto& r = a_report;
        const std::string why = r.choice.source == CodeSource::kSetting ?
                                    "[Display] sLanguage = '" + r.setting + "'" :
                                    "the game's sLanguage '" + r.game + "', [Display] sLanguage " + (r.setting.empty() ? "not set" : "'" + r.setting + "'") +
                                        "; " + Detail::SignalText(r);
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
