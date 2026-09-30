#pragma once

#include "Common.h"
#include "PathText.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// Small helpers shared by the librarian sources. Not part of the public API -
// nothing outside src/librarian/ should include this.
// =============================================================================

namespace Librarian::Detail
{
    using json = nlohmann::json;

    // Where the librarian's data lives, under the game's Data directory.
    inline constexpr const char* DATA_DIR = "Data/SKSE/Plugins/SpellLearning";
    inline constexpr const char* RULE_SUBDIR = "librarian";

    [[nodiscard]] inline std::string Lowered(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // A string field, or an empty string when it is missing or not a string.
    // A scan written with a narrower preset simply lacks some fields, and a
    // rule naming one of those just will not match.
    [[nodiscard]] inline std::string ReadField(const json& object, const char* key)
    {
        const auto found = object.find(key);
        if (found != object.end() && found->is_string()) {
            return found->get<std::string>();
        }
        return {};
    }

    // The race keywords of what a summon effect calls up (the scan's
    // summonedKeywords): at least one of "any" (when given) and none of
    // "none". An effect with no summoned keywords has none of them. Shared by
    // the librarian's rules and the perk adapters.
    [[nodiscard]] inline bool SummonedMatches(const json& effect,
        const std::vector<std::string>& any, const std::vector<std::string>& none)
    {
        if (any.empty() && none.empty()) return true;
        const auto summoned = effect.find("summonedKeywords");
        const auto has = [&](const std::string& keyword) {
            if (summoned == effect.end() || !summoned->is_array()) return false;
            return std::any_of(summoned->begin(), summoned->end(),
                [&](const json& value) { return value.is_string() && value.get<std::string>() == keyword; });
        };
        if (!any.empty() && std::none_of(any.begin(), any.end(), has)) return false;
        return std::none_of(none.begin(), none.end(), has);
    }

    // Whether the effect only takes hold on an actor carrying one of these
    // keywords (the scan's targetKeywords with has: true). Empty asks nothing.
    [[nodiscard]] inline bool TargetRequires(const json& effect, const std::vector<std::string>& any)
    {
        if (any.empty()) return true;
        const auto target = effect.find("targetKeywords");
        if (target == effect.end() || !target->is_array()) return false;
        return std::any_of(target->begin(), target->end(), [&](const json& entry) {
            if (!entry.is_object()) return false;
            const auto has = entry.find("has");
            if (has == entry.end() || !has->is_boolean() || !has->get<bool>()) return false;
            const std::string keyword = ReadField(entry, "keyword");
            return std::find(any.begin(), any.end(), keyword) != any.end();
        });
    }

    [[nodiscard]] inline std::filesystem::path DataPath(const char* leaf)
    {
        return std::filesystem::path(DATA_DIR) / leaf;
    }

    [[nodiscard]] inline std::filesystem::path RulesPath()
    {
        return std::filesystem::path(DATA_DIR) / RULE_SUBDIR;
    }

    // Reads and parses a JSON file. Returns false when it is absent or
    // malformed; a malformed file is logged, an absent one is the caller's
    // business to report or ignore.
    [[nodiscard]] inline bool ReadJsonFile(const std::filesystem::path& path, json& document)
    {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        try {
            file >> document;
        } catch (const std::exception& e) {
            logger::error("Librarian: '{}' is not valid JSON - {}", PathText::Utf8(path), e.what());
            return false;
        }
        return true;
    }
}
