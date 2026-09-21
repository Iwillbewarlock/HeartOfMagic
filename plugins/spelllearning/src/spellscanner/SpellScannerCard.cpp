#include "Common.h"
#include "SpellScanner.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <unordered_map>

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
// Wheeler's standard set sits one folder over and carries the vanilla school
// emblems (alteration.svg, conjuration.svg, destruction.svg and its _fire /
// _frost / _shock variants, illusion.svg, restoration.svg). Those are the
// stand-in when no keyword icon matches.
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

        std::mutex g_iconMutex;
        std::unordered_map<std::string, bool> g_iconExists;

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

    std::string FindSchoolIconKey(RE::SpellItem* spell, bool withElement)
    {
        if (!spell) return "";

        std::string school = GetSchoolName(GetSpellSchool(spell));
        std::transform(school.begin(), school.end(), school.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!IsPlainName(school)) return "";

        // destruction_fire and friends, from the same resist value the chips use
        if (withElement) {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;
            for (const auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;
                if (effect->baseEffect->data.flags.any(Flag::kHideInUI)) continue;

                const char* element = nullptr;
                switch (effect->baseEffect->data.resistVariable) {
                    case RE::ActorValue::kResistFire: element = "fire"; break;
                    case RE::ActorValue::kResistFrost: element = "frost"; break;
                    case RE::ActorValue::kResistShock: element = "shock"; break;
                    default: break;
                }
                if (!element) continue;

                const std::string key = StandardIconKey(school + "_" + element);
                if (IconExists(key)) return key;
                break;
            }
        }

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
