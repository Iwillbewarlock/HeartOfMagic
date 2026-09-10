#pragma once

#include "Common.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

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
            logger::error("Librarian: '{}' is not valid JSON - {}", path.string(), e.what());
            return false;
        }
        return true;
    }
}
