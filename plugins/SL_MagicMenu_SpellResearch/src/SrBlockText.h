#pragma once

#include "SrBlockRules.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// =============================================================================
// SR BLOCK TEXT - the pure text parts of the Spell Research block: reading
// SpellResearchPrismaUI.ini as text ([MagicMenuBlock] sLayout, [Display]
// sLanguage), the game language -> language code, and the block's strings
// (language table, then English, then the key). No game types; shared with
// tools/magic-menu-sr-block-check.
// =============================================================================

namespace SrBlock
{
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

        inline bool SameAscii(std::string_view a_a, std::string_view a_b)
        {
            if (a_a.size() != a_b.size()) return false;
            for (std::size_t i = 0; i < a_a.size(); ++i) {
                if (LowerAscii(a_a[i]) != LowerAscii(a_b[i])) return false;
            }
            return true;
        }
    }

    /// A text without its leading UTF-8 byte order mark (Notepad writes one)
    inline std::string_view StripBom(std::string_view a_text)
    {
        return a_text.starts_with(Detail::kUtf8Bom) ? a_text.substr(Detail::kUtf8Bom.size()) : a_text;
    }

    // ---------------------------------------------------------------------
    // ini text, Windows ini rules: sections and keys case-insensitive, ';' or '#' starts a comment (also after a value),
    // the first occurrence counts, a leading BOM is skipped
    // ---------------------------------------------------------------------
    struct IniValue
    {
        bool found = false;
        std::string value;  // trimmed, comment cut off
    };

    inline IniValue ReadIniValue(std::string_view a_iniText, std::string_view a_section, std::string_view a_key)
    {
        const auto text = StripBom(a_iniText);
        bool inSection = false;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            const auto raw = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            start = end == std::string_view::npos ? text.size() + 1 : end + 1;
            const auto line = Detail::Trim(raw);
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[') {
                const auto close = line.find(']');
                inSection = close != std::string_view::npos && Detail::SameAscii(Detail::Trim(line.substr(1, close - 1)), a_section);
                continue;
            }
            if (!inSection) continue;
            const auto equals = line.find('=');
            if (equals == std::string_view::npos || !Detail::SameAscii(Detail::Trim(line.substr(0, equals)), a_key)) continue;
            auto value = line.substr(equals + 1);
            const auto comment = value.find_first_of(";#");
            if (comment != std::string_view::npos) value = value.substr(0, comment);
            return { true, std::string(Detail::Trim(value)) };
        }
        return {};
    }

    // [MagicMenuBlock] sLayout = list | line (SR Prisma's settings tab writes it); missing or unknown -> list
    inline constexpr std::string_view kLayoutSection = "MagicMenuBlock", kLayoutKey = "sLayout";

    struct LayoutRead
    {
        LayoutKind kind = LayoutKind::kList;
        bool found = false;  // the key is there
        bool known = false;  // its value is list or line
        std::string value;   // the value as written (for the log)
    };

    inline LayoutRead ParseLayout(std::string_view a_iniText)
    {
        const auto ini = ReadIniValue(a_iniText, kLayoutSection, kLayoutKey);
        LayoutRead read{ .found = ini.found, .value = ini.value };
        if (Detail::SameAscii(ini.value, "list")) {
            read.known = true;
        } else if (Detail::SameAscii(ini.value, "line")) {
            read.known = true;
            read.kind = LayoutKind::kLine;
        }
        return read;
    }

    // [Display] sLanguage = auto | en | ko (SR Prisma's settings design draft, docs/2026-10-29-settings-tab-design.md; not
    // final): en / ko -> that code; auto, missing or anything else -> the game's language (Skyrim.ini sLanguage:General)
    inline constexpr std::string_view kLanguageSection = "Display", kLanguageKey = "sLanguage";
    inline constexpr std::string_view kEnglishCode = "en", kKoreanCode = "ko";

    /// The game's sLanguage:General (ENGLISH, KOREAN, FRENCH ...) as a block language code: KOREAN -> ko, anything else en
    inline std::string GameLanguageCode(std::string_view a_gameLanguage)
    {
        return std::string(Detail::SameAscii(Detail::Trim(a_gameLanguage), "KOREAN") ? kKoreanCode : kEnglishCode);
    }

    struct LanguageRead
    {
        std::string code;        // the language code to use
        bool fromSetting = false;  // the ini named it (en or ko); false: the game's language
        bool found = false;      // the key is there
        std::string value;       // as written (for the log)
    };

    inline LanguageRead ParseLanguage(std::string_view a_iniText, std::string_view a_gameLanguage)
    {
        const auto ini = ReadIniValue(a_iniText, kLanguageSection, kLanguageKey);
        LanguageRead read{ .found = ini.found, .value = ini.value };
        for (const auto code : { kEnglishCode, kKoreanCode }) {
            if (Detail::SameAscii(ini.value, code)) {
                read.code = std::string(code);
                read.fromSetting = true;
                return read;
            }
        }
        read.code = GameLanguageCode(a_gameLanguage);
        return read;
    }

    // ---------------------------------------------------------------------
    // Strings: the language's table, then English, then the key itself
    // ---------------------------------------------------------------------
    using StringTable = std::unordered_map<std::string, std::string>;

    enum class StringSource
    {
        kLanguage,
        kEnglish,
        kKey,
    };

    struct Looked
    {
        std::string text;
        StringSource source = StringSource::kKey;
    };

    inline Looked LookUp(const StringTable* a_language, const StringTable& a_english, const std::string& a_key)
    {
        if (a_language) {
            if (const auto it = a_language->find(a_key); it != a_language->end()) return { it->second, StringSource::kLanguage };
        }
        if (const auto it = a_english.find(a_key); it != a_english.end()) return { it->second, StringSource::kEnglish };
        return { a_key, StringSource::kKey };
    }

    // Archetype names: the keys SR Prisma's translation tables will have (its stage 3, agreed 2026-10-29):
    // archetype.<index>.name (SR's name, "Area Targeting") and archetype.<index>.short (without the group word, "Area").
    // The block shows short, falling back to name: the language's short, its name, English short, English name, the key
    inline std::string ArchetypeKey(int a_archetype, std::string_view a_field)
    {
        return "archetype." + std::to_string(a_archetype) + "." + std::string(a_field);
    }

    inline Looked ArchetypeName(const StringTable* a_language, const StringTable& a_english, int a_archetype)
    {
        const auto shortKey = ArchetypeKey(a_archetype, "short"), nameKey = ArchetypeKey(a_archetype, "name");
        for (const auto* table : { a_language, &a_english }) {
            if (!table) continue;
            const auto source = table == &a_english ? StringSource::kEnglish : StringSource::kLanguage;
            for (const auto* key : { &shortKey, &nameKey }) {
                if (const auto it = table->find(*key); it != table->end() && !it->second.empty()) return { it->second, source };
            }
        }
        return { shortKey, StringSource::kKey };
    }

    /// The keys the block uses (en.json must hold every one; the check tool reads the file against this list)
    inline std::vector<std::string> RequiredKeys()
    {
        std::vector<std::string> keys{ "title", "unknown", "more", "group.element", "group.targeting", "group.technique", "link.journal",
            "link.alembic", "link.experience", "reason.notReady", "reason.importing", "reason.notSetup", "reason.notSpell", "reason.listsEmpty", "reason.unknownSpell",
            "reason.noArchetypes", "reason.noTier", "reason.unknownArchetype", "reason.noResponse", "notify.busy", "notify.refused",
            "notify.badMessage" };
        for (std::size_t t = 0; t < kTierCount; ++t) keys.push_back("tier." + std::to_string(t));
        for (int a = 0; a < static_cast<int>(kArchetypeCount); ++a) {
            keys.push_back(ArchetypeKey(a, "name"));
            keys.push_back(ArchetypeKey(a, "short"));
        }
        return keys;
    }
}
