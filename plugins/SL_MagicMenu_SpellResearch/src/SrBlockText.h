#pragma once

#include "SrBlockRules.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <initializer_list>
#include <utility>
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
    // Strings, from two sources (user 2026-10-30, SR Prisma's phase 3):
    //  - SR Prisma's own tables (Data/PrismaUI/views/SpellResearch/lang/<code>.json; its docs/settings-tab.md "Other plugins")
    //    for the archetype names and the three group labels: SR <lang>, SR en, then the block's own <lang>, own en, the key;
    //  - the block's own files (SKSE/Plugins/SL_MagicMenu_SpellResearch/lang/<code>.json) for everything else: own <lang>,
    //    own en, the key.
    // In every table an empty value counts as missing (SR Prisma's rule). Without SR Prisma's files its two tables are null
    // and everything comes from the block's own files
    // ---------------------------------------------------------------------
    using StringTable = std::unordered_map<std::string, std::string>;

    enum class StringSource
    {
        kSrLanguage,  // SR Prisma's table of the language
        kSrEnglish,   // SR Prisma's en.json
        kLanguage,    // the block's own table of the language
        kEnglish,     // the block's own en.json
        kKey,         // nothing had it: the key itself
    };

    struct Looked
    {
        std::string text;
        StringSource source = StringSource::kKey;
    };

    /// The four tables a text is looked up in; any may be null (a language that is English has no separate language
    /// table; SR Prisma not installed has no SR tables)
    struct Tables
    {
        const StringTable* srLanguage = nullptr;
        const StringTable* srEnglish = nullptr;
        const StringTable* language = nullptr;
        const StringTable* english = nullptr;
    };

    namespace Detail
    {
        inline const std::string* Find(const StringTable* a_table, const std::string& a_key)
        {
            if (!a_table) return nullptr;
            const auto it = a_table->find(a_key);
            return it != a_table->end() && !it->second.empty() ? &it->second : nullptr;
        }
    }

    /// The block's own text for a_key: own <lang>, own en, the key
    inline Looked LookUp(const Tables& a_tables, const std::string& a_key)
    {
        if (const auto* text = Detail::Find(a_tables.language, a_key)) return { *text, StringSource::kLanguage };
        if (const auto* text = Detail::Find(a_tables.english, a_key)) return { *text, StringSource::kEnglish };
        return { a_key, StringSource::kKey };
    }

    /// The block's own text with only its own tables (a_language may be null)
    inline Looked LookUp(const StringTable* a_language, const StringTable& a_english, const std::string& a_key)
    {
        return LookUp(Tables{ .language = a_language, .english = &a_english }, a_key);
    }

    /// A text SR Prisma's tables own: SR <lang>[a_srKeys...], SR en[a_srKeys...], own <lang>[a_ownKeys...], own en[a_ownKeys...],
    /// the first own key. Within each table the keys are tried in the order given (archetype: .short before .name)
    inline Looked LookUpShared(const Tables& a_tables, std::initializer_list<std::string> a_srKeys, std::initializer_list<std::string> a_ownKeys)
    {
        const std::pair<const StringTable*, StringSource> sr[]{ { a_tables.srLanguage, StringSource::kSrLanguage },
            { a_tables.srEnglish, StringSource::kSrEnglish } };
        for (const auto& [table, source] : sr) {
            for (const auto& key : a_srKeys) {
                if (const auto* text = Detail::Find(table, key)) return { *text, source };
            }
        }
        const std::pair<const StringTable*, StringSource> own[]{ { a_tables.language, StringSource::kLanguage },
            { a_tables.english, StringSource::kEnglish } };
        for (const auto& [table, source] : own) {
            for (const auto& key : a_ownKeys) {
                if (const auto* text = Detail::Find(table, key)) return { *text, source };
            }
        }
        return { a_ownKeys.size() ? *a_ownKeys.begin() : std::string{}, StringSource::kKey };
    }

    // Archetype names: archetype.<index>.name (SR's name, "Area Targeting") and archetype.<index>.short (without the group
    // word, "Area") - the same keys in SR Prisma's tables and the block's own. The block shows short, falling back to name
    inline std::string ArchetypeKey(int a_archetype, std::string_view a_field)
    {
        return "archetype." + std::to_string(a_archetype) + "." + std::string(a_field);
    }

    inline Looked ArchetypeName(const Tables& a_tables, int a_archetype)
    {
        const auto shortKey = ArchetypeKey(a_archetype, "short"), nameKey = ArchetypeKey(a_archetype, "name");
        return LookUpShared(a_tables, { shortKey, nameKey }, { shortKey, nameKey });
    }

    /// The block's own tables only (a_language may be null)
    inline Looked ArchetypeName(const StringTable* a_language, const StringTable& a_english, int a_archetype)
    {
        return ArchetypeName(Tables{ .language = a_language, .english = &a_english }, a_archetype);
    }

    /// The group label's key in SR Prisma's tables (its window's group headings: Elements / Targeting / Techniques)
    constexpr const char* SrGroupKey(Group a_group)
    {
        switch (a_group) {
        case Group::kElement: return "page.group.elements";
        case Group::kTargeting: return "page.group.targeting";
        case Group::kTechnique: return "page.group.techniques";
        default: return "";
        }
    }

    inline Looked GroupLabel(const Tables& a_tables, Group a_group)
    {
        return LookUpShared(a_tables, { SrGroupKey(a_group) }, { GroupKey(a_group) });
    }

    /// The archetypes the block can show (2-35, 41-44, 45-59) whose name came out as its key (no table has it)
    inline std::vector<int> UnnamedArchetypes(const Tables& a_tables)
    {
        std::vector<int> unnamed;
        for (int a = 0; a < static_cast<int>(kArchetypeCount); ++a) {
            if (GroupOf(a) != Group::kNone && ArchetypeName(a_tables, a).source == StringSource::kKey) unnamed.push_back(a);
        }
        return unnamed;
    }

    /// The keys the block's own en.json must hold (the check tool reads the file against this list); its ko.json holds the
    /// same but the archetype names (read from SR Prisma's tables)
    inline std::vector<std::string> RequiredKeys()
    {
        std::vector<std::string> keys{ "title", "unknown", "more", "group.element", "group.targeting", "group.technique", "link.journal",
            "link.alembic", "link.experience", "reason.notReady", "reason.importing", "reason.notSetup", "reason.notSpell", "reason.listsEmpty",
            "reason.unknownSpell", "reason.noArchetypes", "reason.noTier", "reason.unknownArchetype", "reason.noResponse", "notify.busy",
            "notify.refused", "notify.badMessage" };
        for (int a = 0; a < static_cast<int>(kArchetypeCount); ++a) {
            keys.push_back(ArchetypeKey(a, "name"));
            keys.push_back(ArchetypeKey(a, "short"));
        }
        return keys;
    }
}
