#pragma once

#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>

namespace BlackFlame
{
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";

    // HoM atomically replaces this file after classification. Never read its .part staging file.
    // An empty spells object is valid; zero fire spells must not cause an endless wait.
    inline std::optional<nlohmann::json> ReadCatalogFile(const char* a_path = kCatalogPath)
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        auto catalog = nlohmann::json::parse(file, nullptr, false);
        if (!catalog.is_object()) {
            return std::nullopt;
        }
        const auto spells = catalog.find("spells");
        if (spells == catalog.end() || !spells->is_object()) {
            return std::nullopt;
        }
        return catalog;
    }
}
