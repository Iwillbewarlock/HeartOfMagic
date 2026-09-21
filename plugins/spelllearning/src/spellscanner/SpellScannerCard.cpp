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

namespace SpellScanner
{
    namespace
    {
        constexpr const char* kIconDir = "Data/SKSE/Plugins/wheeler/resources/icons_custom";
        constexpr const char* kIconPrefix = "KWD_";
        constexpr const char* kIconExtension = ".svg";

        // An icon is a few KB. Anything far past that is not something to push
        // through the JS bridge for a 20px picture.
        constexpr std::uintmax_t kMaxIconBytes = 256 * 1024;

        std::mutex g_iconMutex;
        std::unordered_map<std::string, bool> g_iconExists;

        // Keyword editor ids become file names, so only plain identifier
        // characters are let through - no separators, no dots.
        bool IsSafeIconKey(const std::string& key)
        {
            if (key.empty()) return false;
            return std::all_of(key.begin(), key.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-';
            });
        }

        std::filesystem::path IconPath(const std::string& key)
        {
            return std::filesystem::path(kIconDir) / (std::string(kIconPrefix) + key + kIconExtension);
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
                if (editorId && IconExists(editorId)) return editorId;
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
