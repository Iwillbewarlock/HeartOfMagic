// magic-menu-lang-check - offline check of the magic menu's language files (2026-10-31, before the first Nexus release):
// for the core (plugins/SL_MagicMenu, WidgetStrings.h) and its Heart of Magic block (plugins/SL_MagicMenu_HoM, Strings.h)
// the shipped dist/lang/en.json and ko.json against the DLL's table - every key there, no extra key, the files parse, the
// {placeholders} and the " · " separators the same in en, ko and the built-in text, the built-in text equal to en.json -
// and the shared LangFiles.h as the DLLs run it: [Display] sLanguage + the game's language -> the code, the file chain
// (<code>.json, en.json, built-in) on the real folders and on made-up ones, the Catalog's one load and its refresh at a menu open, the log lines. The
// shipped SL_MagicMenu.ini must carry [Display] sLanguage=auto and an empty [Blocks] sHidden.
// Not covered here (the game only): LangGame.h's ini and game INI setting reads.
//
//   magic-menu-lang-check               one PASS/FAIL line per check, a total line
//   magic-menu-lang-check --selftest    the same checks, then a "selftest: all PASS / N FAIL" summary line
//
// No game types. Exit 0 when every check passes, 1 when one fails, 2 on a bad argument.

#include "LangFiles.h"
#include "Strings.h"
#include "WidgetStrings.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if !defined(MM_CORE_LANG_DIR) || !defined(MM_HOM_LANG_DIR) || !defined(MM_DIST_INI)
#    error "MM_CORE_LANG_DIR, MM_HOM_LANG_DIR and MM_DIST_INI (the shipped lang folders and ini) must be defined by CMake"
#endif

namespace
{
    namespace fs = std::filesystem;
    using namespace LangFiles;

    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;
    constexpr std::size_t kShownMissing = 5;  // missing keys named in one FAIL line at most

    int g_pass = 0;
    int g_fail = 0;

    void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    std::string Names(const std::vector<std::string>& a_keys)
    {
        std::string list;
        for (std::size_t i = 0; i < a_keys.size() && i < kShownMissing; ++i) list += (list.empty() ? "" : ", ") + a_keys[i];
        if (a_keys.size() > kShownMissing) list += ", ...";
        return list.empty() ? "none" : list;
    }

    std::string ReadText(const fs::path& a_path)
    {
        std::ifstream file(a_path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    void WriteText(const fs::path& a_path, std::string_view a_text)
    {
        std::ofstream file(a_path, std::ios::binary | std::ios::trunc);
        file.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
    }

    // ---- one DLL's table against its shipped files ----

    template <class Entry, std::size_t N>
    void Files(const char* a_name, const std::array<Entry, N>& a_table, const fs::path& a_dir)
    {
        std::printf("%s: %s against its table (%zu keys)\n", a_name, a_dir.generic_string().c_str(), N);
        std::set<std::string> keys;
        for (const auto& row : a_table) keys.insert(row.key);
        Expect(keys.size() == N, Text("%s: every table key once (%zu distinct of %zu)", a_name, keys.size(), N));

        const auto en = ReadTableFile(FilePath(a_dir, kEnglish));
        const auto ko = ReadTableFile(FilePath(a_dir, kKorean));
        for (const auto& [code, read] : { std::pair{ kEnglish, &en }, std::pair{ kKorean, &ko } }) {
            const std::string file = std::string(code) + ".json";
            Expect(read->problem == TableProblem::kNone && read->skipped == 0,
                Text("%s: %s parses as a flat object of strings (%s)", a_name, file.c_str(), read->detail.empty() ? "ok" : read->detail.c_str()));
            if (!read->table) continue;
            std::vector<std::string> missing, extra;
            for (const auto& key : keys) {
                const auto it = read->table->find(key);
                if (it == read->table->end() || it->second.empty()) missing.push_back(key);
            }
            for (const auto& [key, value] : *read->table) {
                if (!keys.contains(key)) extra.push_back(key);
            }
            std::ranges::sort(extra);
            Expect(missing.empty(), Text("%s: %s has every table key (missing: %s)", a_name, file.c_str(), Names(missing).c_str()));
            Expect(extra.empty(), Text("%s: %s has no key the table lacks (extra: %s)", a_name, file.c_str(), Names(extra).c_str()));
            const auto raw = ReadText(FilePath(a_dir, code));
            Expect(raw.find("\"_about\"") != std::string::npos, Text("%s: %s carries an \"_about\" note", a_name, file.c_str()));
        }
        if (!en.table || !ko.table) return;

        std::vector<std::string> placeholders, separators, builtIn;
        for (const auto& row : a_table) {
            const std::string key = row.key, own = row.text;
            const auto enIt = en.table->find(key), koIt = ko.table->find(key);
            if (enIt == en.table->end() || koIt == ko.table->end()) continue;
            const auto& enText = enIt->second;
            const auto& koText = koIt->second;
            if (Placeholders(enText) != Placeholders(own) || Placeholders(koText) != Placeholders(own)) placeholders.push_back(key);
            if (CountOf(enText, kSeparator) != CountOf(own, kSeparator) || CountOf(koText, kSeparator) != CountOf(own, kSeparator)) separators.push_back(key);
            if (enText != own) builtIn.push_back(key);
        }
        Expect(placeholders.empty(), Text("%s: the same {placeholders} in en, ko and the built-in text (differ: %s)", a_name, Names(placeholders).c_str()));
        Expect(separators.empty(), Text("%s: the same number of ' \xC2\xB7 ' separators in en, ko and the built-in text (differ: %s)", a_name,
                                       Names(separators).c_str()));
        Expect(builtIn.empty(), Text("%s: the built-in text equals en.json (differ: %s)", a_name, Names(builtIn).c_str()));
    }

    // The rows both tables have (school names, the empty line) read the same in both DLLs' files
    void SharedKeys()
    {
        std::printf("shared keys: the core's and the block's files agree\n");
        const auto coreDir = fs::path(MM_CORE_LANG_DIR), homDir = fs::path(MM_HOM_LANG_DIR);
        std::size_t shared = 0;
        std::vector<std::string> differ;
        for (const auto code : { kEnglish, kKorean }) {
            const auto core = ReadTableFile(FilePath(coreDir, code)), hom = ReadTableFile(FilePath(homDir, code));
            if (!core.table || !hom.table) {
                differ.push_back(std::string(code) + ".json unread");
                continue;
            }
            for (const auto& [key, value] : *core.table) {
                const auto it = hom.table->find(key);
                if (it == hom.table->end()) continue;
                ++shared;
                if (it->second != value) differ.push_back(std::string(code) + ":" + key);
            }
        }
        Expect(shared == 12 && differ.empty(), Text("school_* and empty_choose (6 keys x en, ko = %zu) the same text in both (differ: %s)", shared,
                                                    Names(differ).c_str()));
    }

    // ---- the shared resolver ----

    void Codes()
    {
        std::printf("language code: [Display] sLanguage + the game's sLanguage\n");
        const auto is = [](std::string_view a_setting, std::string_view a_game, std::string_view a_code, CodeSource a_source) {
            const auto choice = ChooseCode(a_setting, a_game);
            return choice.code == a_code && choice.source == a_source;
        };
        Expect(is("auto", "KOREAN", "ko", CodeSource::kGame), "auto + KOREAN -> ko");
        Expect(is("auto", "ENGLISH", "en", CodeSource::kGame), "auto + ENGLISH -> en");
        Expect(is("auto", "GERMAN", "en", CodeSource::kGame), "auto + GERMAN -> en (no German files: English)");
        Expect(is("", "korean", "ko", CodeSource::kGame) && is("  AUTO ", " Korean ", "ko", CodeSource::kGame), "not set or AUTO, a lower-case KOREAN -> ko");
        Expect(is("auto", "", "en", CodeSource::kGame), "auto + no game language -> en");
        Expect(is("ko", "ENGLISH", "ko", CodeSource::kSetting) && is(" KO ", "ENGLISH", "ko", CodeSource::kSetting), "explicit ko (any case) -> ko");
        Expect(is("en", "KOREAN", "en", CodeSource::kSetting), "explicit en on a Korean game -> en");
        Expect(is("de", "ENGLISH", "de", CodeSource::kSetting), "explicit de -> de (its file is looked for)");
        Expect(is("../ko", "KOREAN", "ko", CodeSource::kBadSetting) && is("k?", "ENGLISH", "en", CodeSource::kBadSetting) &&
                   is("abcdefghijklmnopq", "ENGLISH", "en", CodeSource::kBadSetting),
            "not a code (a path, a non-ASCII '?', 17 letters) -> the game's language");
        // 2026-10-31: auto on a Korean-patched English game - Hangul in a localized game string decides
        const auto auto3 = [](std::string_view a_setting, std::string_view a_game, std::string_view a_text, std::string_view a_code, CodeSource a_source,
                               AutoSignal a_signal) {
            const auto choice = ChooseCode(a_setting, a_game, a_text);
            return choice.code == a_code && choice.source == a_source && choice.signal == a_signal;
        };
        const std::string alteration = "\xEB\xB3\x80\xEC\x84\xB1";  // "변성" (U+BCC0 U+C131) in UTF-8
        Expect(HasHangul(alteration) && HasHangul("Alteration, " + alteration) && HasHangul("\xE3\x84\xB1") && HasHangul("\xE1\x84\x80"),
            "HasHangul: syllables, mixed with English, a compatibility jamo, a jamo");
        Expect(!HasHangul("Alteration") && !HasHangul("") && !HasHangul("\xE5\xA4\x89") && !HasHangul("\xE3\x81\x82"),
            "HasHangul: English, empty, Han only (变), kana only - no");
        Expect(!HasHangul("\xBA\xAF\xBC\xBA") && !HasHangul("\xEB\xB3") && !HasHangul("\xC1\x81") && !HasHangul("\xED\xA0\x80") && !HasHangul("\xFF\xEB"),
            "HasHangul: CP949 bytes, a cut sequence, an overlong one, a surrogate, a bad lead - no, and no crash");
        Expect(HasHangul("\xBA\xAF" + alteration), "HasHangul: Hangul after invalid bytes is still found");
        Expect(auto3("auto", "ENGLISH", alteration, "ko", CodeSource::kGame, AutoSignal::kGameText) &&
                   auto3("", "ENGLISH", "Alteration, " + alteration, "ko", CodeSource::kGame, AutoSignal::kGameText),
            "auto + ENGLISH + Korean game text -> ko (the Korean patch on an English game)");
        Expect(auto3("auto", "ENGLISH", "Alteration, Conjuration", "en", CodeSource::kGame, AutoSignal::kNeither) &&
                   auto3("auto", "ENGLISH", "", "en", CodeSource::kGame, AutoSignal::kNeither),
            "auto + ENGLISH + English or no game text -> en");
        Expect(auto3("auto", "KOREAN", "Alteration", "ko", CodeSource::kGame, AutoSignal::kGameLanguage), "auto + KOREAN -> ko by the game language, the text not needed");
        Expect(auto3("en", "ENGLISH", alteration, "en", CodeSource::kSetting, AutoSignal::kNone) &&
                   auto3("ko", "ENGLISH", "Alteration", "ko", CodeSource::kSetting, AutoSignal::kNone),
            "an explicit code wins over the game text");
        Expect(auto3("k?", "ENGLISH", alteration, "ko", CodeSource::kBadSetting, AutoSignal::kGameText), "a bad setting falls back to auto, the game text too");
        {
            LoadReport report{ .setting = "auto", .game = "ENGLISH", .gameText = alteration, .choice = ChooseCode("auto", "ENGLISH", alteration) };
            report.shown = "ko";
            const auto lines = ReportLines(report, "dir");
            Expect(std::ranges::any_of(lines, [&](const auto& a_line) {
                return a_line.second.find("language 'ko'") != std::string::npos && a_line.second.find("decided by Hangul in the game text '" + alteration + "'") != std::string::npos;
            }), "log: the language line names the Hangul signal and the text");
            report.choice = ChooseCode("auto", "ENGLISH", "");
            report.gameText.clear();
            const auto none = ReportLines(report, "dir");
            Expect(std::ranges::any_of(none, [](const auto& a_line) { return a_line.second.find("no Korean signal: not KOREAN, no Hangul in no game text read") != std::string::npos; }),
                "log: no signal says so");
        }
        Expect(FillText("{n} of {n} {key}", { { "{n}", "3" }, { "{key}", "[U]" } }) == "3 of 3 [U]" && FillText("{n}", { { "{n}", "{n}{n}" } }) == "{n}{n}",
            "FillText: every occurrence, a value holding the name is not filled again");
        Expect(Placeholders("{b} x {a} {b}") == std::vector<std::string>{ "{a}", "{b}", "{b}" } && CountOf("a \xC2\xB7 b \xC2\xB7 c", kSeparator) == 2,
            "Placeholders sorted with repeats, two separators counted");
    }

    template <class Entry, std::size_t N>
    std::vector<std::string> BuiltIn(const std::array<Entry, N>& a_table)
    {
        std::vector<std::string> texts;
        for (const auto& row : a_table) texts.emplace_back(row.text);
        return texts;
    }

    template <class Entry, std::size_t N>
    bool AllFrom(const std::array<std::string, N>& a_texts, const std::array<Entry, N>& a_table, const fs::path& a_file)
    {
        const auto read = ReadTableFile(a_file);
        if (!read.table) return false;
        for (std::size_t i = 0; i < N; ++i) {
            const auto it = read.table->find(a_table[i].key);
            if (it == read.table->end() || it->second != a_texts[i]) return false;
        }
        return true;
    }

    template <class Entry, std::size_t N>
    void Chain(const char* a_name, const std::array<Entry, N>& a_table, const fs::path& a_dir)
    {
        std::printf("%s: the file chain on the shipped folder\n", a_name);
        {
            const auto [texts, r] = Resolve(a_table, a_dir, "auto", "KOREAN");
            Expect(r.shown == "ko" && r.fromLanguage == N && r.fromEnglish == 0 && r.fromBuiltIn == 0 && AllFrom(texts, a_table, FilePath(a_dir, kKorean)),
                Text("%s: auto + KOREAN -> every text from ko.json (%zu / %zu)", a_name, r.fromLanguage, N));
        }
        {
            const auto [texts, r] = Resolve(a_table, a_dir, "auto", "ENGLISH");
            Expect(r.shown == "en" && !r.language.table && r.fromEnglish == N && AllFrom(texts, a_table, FilePath(a_dir, kEnglish)),
                Text("%s: auto + ENGLISH -> every text from en.json, ko.json not read (%zu / %zu)", a_name, r.fromEnglish, N));
        }
        {
            const auto [texts, r] = Resolve(a_table, a_dir, "auto", "GERMAN");
            Expect(r.choice.code == "en" && r.shown == "en" && r.fromEnglish == N, Text("%s: auto + GERMAN -> en.json", a_name));
        }
        {
            const auto [texts, r] = Resolve(a_table, a_dir, "de", "ENGLISH");
            Expect(r.choice.code == "de" && r.language.problem == TableProblem::kMissing && r.shown == "en" && r.fromEnglish == N &&
                       r.notInLanguage.empty(),
                Text("%s: explicit de, no de.json -> en.json for every text (%zu / %zu)", a_name, r.fromEnglish, N));
        }
        {
            const auto [texts, r] = Resolve(a_table, a_dir, "ko", "ENGLISH");
            Expect(r.choice.source == CodeSource::kSetting && r.shown == "ko" && r.fromLanguage == N, Text("%s: explicit ko on an English game -> ko.json", a_name));
        }
    }

    // Made-up folders: no files at all; a ko.json without one key, an en.json without another, a BOM, an empty value
    template <class Entry, std::size_t N>
    void MadeUp(const char* a_name, const std::array<Entry, N>& a_table)
    {
        std::printf("%s: the file chain on made-up folders\n", a_name);
        std::error_code ec;
        const auto root = fs::temp_directory_path(ec) / "magic-menu-lang-check" / a_name;
        fs::remove_all(root, ec);
        const auto empty = root / "none", partial = root / "partial";
        fs::create_directories(empty, ec);
        fs::create_directories(partial, ec);
        Expect(!ec, Text("%s: temporary folders under %s", a_name, root.generic_string().c_str()));

        const auto builtIn = BuiltIn(a_table);
        {
            const auto [texts, r] = Resolve(a_table, empty, "ko", "KOREAN");
            Expect(r.shown == "en" && r.english.problem == TableProblem::kMissing && r.language.problem == TableProblem::kMissing &&
                       r.fromBuiltIn == N && std::ranges::equal(texts, builtIn),
                Text("%s: no ko.json, no en.json -> every text built-in English (%zu / %zu)", a_name, r.fromBuiltIn, N));
            const auto lines = ReportLines(r, empty.generic_string());
            const bool error = std::ranges::any_of(lines, [](const auto& a_line) {
                return a_line.first == Level::kError && a_line.second.find("en.json not found") != std::string::npos;
            });
            const bool info = std::ranges::any_of(lines, [](const auto& a_line) {
                return a_line.first == Level::kInfo && a_line.second.find("ko.json not found") != std::string::npos;
            });
            Expect(error && info, Text("%s: log - en.json missing is an error, ko.json missing is info (%zu lines)", a_name, lines.size()));
        }

        // key 0 missing in ko.json, key 1 empty in ko.json and missing in en.json, key 2 missing in en.json only
        std::string ko = "\xEF\xBB\xBF{\"_about\": \"test\", \"extra\": 5", en = "{\"_about\": \"test\"";
        for (std::size_t i = 0; i < N; ++i) {
            const std::string key = a_table[i].key;
            if (i != 0) ko += ", \"" + key + "\": \"" + (i == 1 ? std::string{} : "KO" + std::to_string(i)) + "\"";
            if (i != 1 && i != 2) en += ", \"" + key + "\": \"EN" + std::to_string(i) + "\"";
        }
        WriteText(FilePath(partial, kKorean), ko + "}");
        WriteText(FilePath(partial, kEnglish), en + "}");
        const auto [texts, r] = Resolve(a_table, partial, "auto", "KOREAN");
        Expect(r.language.problem == TableProblem::kNone && r.language.skipped == 1 && !r.language.table->contains("_about"),
            Text("%s: a BOM read, the \"_about\" note left out, a number value skipped", a_name));
        Expect(texts[0] == "EN0" && texts[1] == builtIn[1] && texts[2] == "KO2" && texts[3] == "KO3",
            Text("%s: per key: ko lacks -> en; ko empty and en lacks -> built-in; en lacks but ko has -> ko", a_name));
        Expect(r.fromLanguage == N - 2 && r.fromEnglish == 1 && r.fromBuiltIn == 1 && r.notInLanguage.size() == 2 && r.notInEnglish.size() == 2,
            Text("%s: counts %zu ko / %zu en / %zu built-in, 2 lacking in each file", a_name, r.fromLanguage, r.fromEnglish, r.fromBuiltIn));

        WriteText(FilePath(partial, kKorean), "{ not json");
        const auto [badTexts, bad] = Resolve(a_table, partial, "ko", "ENGLISH");
        const auto badLines = ReportLines(bad, partial.generic_string());
        Expect(bad.language.problem == TableProblem::kNotJson && bad.shown == "en" && badTexts[0] == "EN0" &&
                   std::ranges::any_of(badLines, [](const auto& a_line) { return a_line.first == Level::kError; }),
            Text("%s: a broken ko.json -> English, logged as an error", a_name));
        fs::remove_all(root, ec);
    }

    template <class Entry, std::size_t N>
    void OneLoad(const char* a_name, const std::array<Entry, N>& a_table, const fs::path& a_dir)
    {
        std::printf("%s: the Catalog loads once\n", a_name);
        Catalog<Entry, N> catalog(a_table);
        const bool before = !catalog.Loaded() && std::string_view(catalog.Text(0)) == a_table[0].text;
        const auto first = catalog.Load(a_dir, "ko", "");
        const auto ko = ReadTableFile(FilePath(a_dir, kKorean));
        const bool loaded = first && catalog.Loaded() && ko.table && catalog.Text(0) == ko.table->at(a_table[0].key);
        const auto second = catalog.Load(a_dir, "en", "");
        const bool kept = !second && ko.table && catalog.Text(0) == ko.table->at(a_table[0].key);
        Expect(before && loaded && kept, Text("%s: built-in English before, ko.json after the load, a second load refused (first stands)", a_name));
    }

    // 2026-10-31 (the settings tab writes [Display] sLanguage while the game runs): Refresh at a menu open resolves again only for
    // another setting, and a pointer Text gave before stays valid with its old text
    template <class Entry, std::size_t N>
    void Refreshes(const char* a_name, const std::array<Entry, N>& a_table, const fs::path& a_dir)
    {
        std::printf("%s: the Catalog's refresh at a menu open\n", a_name);
        Catalog<Entry, N> catalog(a_table);
        Expect(!catalog.Refresh(a_dir, "ko", "") && !catalog.Loaded(), Text("%s: a refresh before the load does nothing", a_name));
        const auto en = ReadTableFile(FilePath(a_dir, kEnglish)), ko = ReadTableFile(FilePath(a_dir, kKorean));
        const std::string key = a_table[0].key;
        catalog.Load(a_dir, "", "");
        const char* old = catalog.Text(0);
        Expect(!catalog.Refresh(a_dir, " AUTO ", "") && !catalog.Refresh(a_dir, "auto", "") && catalog.Generations() == 1,
            Text("%s: \"\" then \" AUTO \" / \"auto\": the same setting, not resolved again", a_name));
        const auto changed = catalog.Refresh(a_dir, "ko", "");
        Expect(changed && changed->choice.code == kKorean && ko.table && catalog.Text(0) == ko.table->at(key) && catalog.Generations() == 2,
            Text("%s: auto -> ko resolves again: ko.json's text now", a_name));
        Expect(en.table && old == en.table->at(key), Text("%s: the pointer taken before the refresh still reads the English text", a_name));
        Expect(!catalog.Refresh(a_dir, "KO", "") && catalog.Generations() == 2, Text("%s: ko -> KO: the same setting, not resolved again", a_name));
        const auto back = catalog.Refresh(a_dir, "en", "KOREAN");
        Expect(back && en.table && catalog.Text(0) == en.table->at(key) && catalog.Generations() == 3, Text("%s: ko -> en: English again", a_name));
        Expect(NormalizedSetting("") == kAuto && NormalizedSetting(" Ko\t") == kKorean, "NormalizedSetting: \"\" is auto, trimmed and lower case");
    }

    // The value of a_key in a_section ("[Display]") of the shipped ini; nullopt when the key is not there
    std::optional<std::string> DistValue(std::string_view a_section, std::string_view a_key)
    {
        std::istringstream in(ReadText(MM_DIST_INI));
        std::string line, section;
        const std::string prefix = std::string(a_key) + "=";
        std::optional<std::string> value;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("[")) section = line;
            if (section == a_section && line.starts_with(prefix)) value = line.substr(prefix.size());
        }
        return value;
    }

    void DistIni()
    {
        std::printf("the shipped SL_MagicMenu.ini\n");
        const auto language = DistValue("[Display]", "sLanguage");
        Expect(language == kAuto, Text("[Display] sLanguage=auto (found %s, value '%s')", language ? "yes" : "no", language.value_or("").c_str()));
        const auto hidden = DistValue("[Blocks]", "sHidden");
        Expect(hidden && hidden->empty(), Text("[Blocks] sHidden= (empty: every block shown; found %s)", hidden ? "yes" : "no"));
    }

    // Fix round 1 I2: the settings tab writes the player's SL_MagicMenu_User.ini (never shipped); a key there wins over the shipped
    // SL_MagicMenu.ini, an empty value included (LangFiles::Layered, the order LangGame::IniString reads them in)
    void UserFileOrder()
    {
        std::printf("the player's SL_MagicMenu_User.ini before the shipped SL_MagicMenu.ini\n");
        using Value = std::optional<std::string>;
        Expect(Layered(Value("ko"), Value("auto")) == "ko", "both files have the key: the player's file wins");
        Expect(Layered(Value(""), Value("preview")) == "", "the player's file has the key empty (every block ticked on again): empty, not the shipped value");
        Expect(Layered(std::nullopt, Value("auto")) == "auto", "the player's file lacks the key (or is not there): the shipped value");
        Expect(Layered(std::nullopt, std::nullopt).empty(), "neither file has it: \"\"");
        Expect(Layered(Value("en"), std::nullopt) == "en", "only the player's file has it: the player's value");
    }

    int Run()
    {
        const fs::path coreDir = MM_CORE_LANG_DIR, homDir = MM_HOM_LANG_DIR;
        Files("core", WidgetText::kStrings, coreDir);
        Files("hom", HomStrings::kStrings, homDir);
        SharedKeys();
        Codes();
        Chain("core", WidgetText::kStrings, coreDir);
        Chain("hom", HomStrings::kStrings, homDir);
        MadeUp("core", WidgetText::kStrings);
        MadeUp("hom", HomStrings::kStrings);
        OneLoad("core", WidgetText::kStrings, coreDir);
        OneLoad("hom", HomStrings::kStrings, homDir);
        Refreshes("core", WidgetText::kStrings, coreDir);
        Refreshes("hom", HomStrings::kStrings, homDir);
        DistIni();
        UserFileOrder();
        std::printf("lang-check: %d PASS, %d FAIL\n", g_pass, g_fail);
        return g_fail == 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    bool selftest = false;
    for (int i = 1; i < a_argc; ++i) {
        if (std::string_view(a_argv[i]) == "--selftest") {
            selftest = true;
        } else {
            std::fprintf(stderr, "usage: magic-menu-lang-check [--selftest]\n");
            return kExitUsage;
        }
    }
    const int code = Run();
    if (selftest) std::printf(g_fail == 0 ? "selftest: all PASS\n" : "selftest: %d FAIL\n", g_fail);
    return code;
}
