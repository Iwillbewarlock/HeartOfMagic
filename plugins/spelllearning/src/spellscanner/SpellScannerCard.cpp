#include "Common.h"
#include "SpellScanner.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// SPELL CARD - ICON AND DESCRIPTION
// =============================================================================
//
// Icon: custom I4 icon mods ship their spell icons twice - once inside a SWF for
// the inventory menus, which a web view cannot draw, and once as plain SVG files
// for Wheeler, named after the keyword they stand for:
//
//     Data/SKSE/Plugins/wheeler/resources/icons_custom/KWD_<KeywordEditorID>.svg
//
// So the icon for a spell is found by file name alone: walk the spell's keywords
// and take the first one that has such a file. Nothing is listed here per mod;
// whatever icon packs the player has installed are picked up as they are.
//
// A spell no pack keyworded still gets a fitting picture through the icon rules
// (SKSE/Plugins/SpellLearning/card_icons.json). A rule says "a spell with these
// traits uses this file" - traits being the same closed-set ids the chips are
// made of (element.fire, kind.cloak, school.destruction ...). Rules are tried in
// order and one only counts if its file is actually installed, so a rule for a
// pack the player does not have is simply skipped. The shipped rules point at
// the icon set of Kome's Inventory Tweaks, which is the mod's icon requirement;
// the file is data, so another pack can be wired in without touching the DLL.
// The last rules are the vanilla school emblems from Wheeler's standard set
// (icons/<school>.svg), one folder over.
//
// An icon key is "<folder>/<file stem>": "icons_custom/KWD_<keyword>" or
// "icons/<name>". The panel treats it as opaque and hands it back to
// ReadSpellIconSvg.

namespace SpellScanner
{
    namespace
    {
        constexpr const char* kIconRoot = "Data/SKSE/Plugins/wheeler/resources";
        constexpr const char* kCustomDir = "icons_custom";
        constexpr const char* kStandardDir = "icons";
        constexpr const char* kKeywordPrefix = "KWD_";
        constexpr const char* kIconExtension = ".svg";

        // An icon is a few KB. Anything far past that is not something to push
        // through the JS bridge for a 20px picture.
        constexpr std::uintmax_t kMaxIconBytes = 256 * 1024;

        constexpr const char* kRulesPath = "Data/SKSE/Plugins/SpellLearning/card_icons.json";

        // Placeholders a rule's icon may carry.
        constexpr const char* kSchoolLetterTag = "{S}";     // A C D I R
        constexpr const char* kSchoolNameTag = "{school}";  // alteration ...

        struct IconRule
        {
            std::vector<std::string> when;  // every trait must be present
            std::string icon;               // icon key, may carry placeholders
        };

        std::mutex g_iconMutex;
        std::unordered_map<std::string, bool> g_iconExists;

        std::once_flag g_rulesOnce;
        std::vector<IconRule> g_rules;

        bool IsPlainName(const std::string& name)
        {
            if (name.empty()) return false;
            return std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-';
            });
        }

        // Keys come back from the panel and turn into file paths, so they are
        // held to exactly one of the two known folders plus a plain file stem -
        // no dots, no further separators.
        bool IsSafeIconKey(const std::string& key)
        {
            const auto slash = key.find('/');
            if (slash == std::string::npos) return false;
            const std::string folder = key.substr(0, slash);
            if (folder != kCustomDir && folder != kStandardDir) return false;
            return IsPlainName(key.substr(slash + 1));
        }

        std::filesystem::path IconPath(const std::string& key)
        {
            return std::filesystem::path(kIconRoot) / (key + kIconExtension);
        }

        std::string KeywordIconKey(const std::string& keywordEditorId)
        {
            return std::string(kCustomDir) + "/" + kKeywordPrefix + keywordEditorId;
        }

        std::string StandardIconKey(const std::string& name)
        {
            return std::string(kStandardDir) + "/" + name;
        }

        bool IconExists(const std::string& key)
        {
            if (!IsSafeIconKey(key)) return false;

            std::lock_guard<std::mutex> lock(g_iconMutex);
            const auto cached = g_iconExists.find(key);
            if (cached != g_iconExists.end()) return cached->second;

            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(IconPath(key), error) && !error;
            g_iconExists.emplace(key, exists);
            return exists;
        }

        std::string FirstKeywordWithIcon(const RE::BGSKeywordForm* keywordForm)
        {
            if (!keywordForm || !keywordForm->keywords) return "";
            for (std::uint32_t i = 0; i < keywordForm->numKeywords; i++) {
                const auto* keyword = keywordForm->keywords[i];
                if (!keyword) continue;
                const char* editorId = keyword->GetFormEditorID();
                if (!editorId) continue;
                const std::string key = KeywordIconKey(editorId);
                if (IconExists(key)) return key;
            }
            return "";
        }

        std::string FormatNumber(float value)
        {
            return std::format("{:.0f}", value);
        }

        void ReplaceAllNoCase(std::string& text, const std::string& tag, const std::string& value)
        {
            const std::regex pattern(tag, std::regex::icase);
            text = std::regex_replace(text, pattern, value);
        }
    }

    std::string FindSpellIconKey(RE::SpellItem* spell)
    {
        if (!spell) return "";

        // The spell's own keywords first - that is where icon packs put theirs.
        std::string key = FirstKeywordWithIcon(spell);
        if (!key.empty()) return key;

        for (const auto* effect : spell->effects) {
            if (!effect || !effect->baseEffect) continue;
            key = FirstKeywordWithIcon(effect->baseEffect);
            if (!key.empty()) return key;
        }
        return "";
    }

    namespace
    {
        std::string LowerSchoolName(RE::SpellItem* spell)
        {
            std::string school = GetSchoolName(GetSpellSchool(spell));
            std::transform(school.begin(), school.end(), school.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return IsPlainName(school) ? school : std::string();
        }

        void ReplaceTag(std::string& text, const std::string& tag, const std::string& value)
        {
            for (auto at = text.find(tag); at != std::string::npos; at = text.find(tag, at + value.size())) {
                text.replace(at, tag.size(), value);
            }
        }

        void LoadIconRules()
        {
            try {
                std::ifstream file(kRulesPath);
                if (!file.is_open()) {
                    logger::info("SpellScanner: no {} - spell cards use keyword icons only", kRulesPath);
                    return;
                }

                const json data = json::parse(file, nullptr, true, true);
                for (const auto& entry : data.value("rules", json::array())) {
                    IconRule rule;
                    rule.icon = entry.value("icon", std::string());
                    for (const auto& trait : entry.value("when", json::array())) {
                        if (trait.is_string()) rule.when.push_back(trait.get<std::string>());
                    }
                    if (!rule.icon.empty()) g_rules.push_back(std::move(rule));
                }
                logger::info("SpellScanner: loaded {} spell card icon rules", g_rules.size());
            } catch (const std::exception& e) {
                logger::warn("SpellScanner: could not read {}: {}", kRulesPath, e.what());
                g_rules.clear();
            }
        }
    }

    std::string FindRuleIconKey(RE::SpellItem* spell)
    {
        if (!spell) return "";
        std::call_once(g_rulesOnce, LoadIconRules);
        if (g_rules.empty()) return "";

        std::unordered_set<std::string> traits;
        for (const auto& trait : BuildSpellTraits(spell)) {
            traits.insert(trait.get<std::string>());
        }

        const std::string school = LowerSchoolName(spell);
        const std::string letter = school.empty()
            ? std::string()
            : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(school[0]))));

        for (const auto& rule : g_rules) {
            const bool matches = std::all_of(rule.when.begin(), rule.when.end(),
                [&traits](const std::string& trait) { return traits.contains(trait); });
            if (!matches) continue;

            std::string key = rule.icon;
            ReplaceTag(key, kSchoolLetterTag, letter);
            ReplaceTag(key, kSchoolNameTag, school);
            if (IconExists(key)) return key;  // also rejects anything that is not a safe key
        }
        return "";
    }

    std::string FindSchoolIconKey(RE::SpellItem* spell)
    {
        if (!spell) return "";
        const std::string school = LowerSchoolName(spell);
        if (school.empty()) return "";

        const std::string key = StandardIconKey(school);
        return IconExists(key) ? key : "";
    }

    std::string ReadSpellIconSvg(const std::string& key)
    {
        if (!IconExists(key)) return "";

        try {
            const auto path = IconPath(key);
            if (std::filesystem::file_size(path) > kMaxIconBytes) {
                logger::warn("SpellScanner: icon '{}' is too large, skipped", path.string());
                return "";
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return "";
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        } catch (const std::exception& e) {
            logger::warn("SpellScanner: could not read icon for '{}': {}", key, e.what());
            return "";
        }
    }

    // The game writes descriptions with placeholders - "<mag> points for <dur>
    // seconds" - and with plain numbers wrapped in <> to mark them for
    // highlighting. The menus fill those in; a web view shows them raw.
    std::string ResolveDescriptionTags(std::string text, const RE::Effect* effect)
    {
        if (text.find('<') == std::string::npos) return text;

        if (effect) {
            ReplaceAllNoCase(text, "<mag>", FormatNumber(effect->effectItem.magnitude));
            ReplaceAllNoCase(text, "<dur>", std::to_string(effect->effectItem.duration));
            ReplaceAllNoCase(text, "<area>", std::to_string(effect->effectItem.area));
        }

        // What is left is "<25>" style emphasis: keep the content, drop the marks.
        static const std::regex emphasis("<([^<>]*)>");
        return std::regex_replace(text, emphasis, "$1");
    }
}
