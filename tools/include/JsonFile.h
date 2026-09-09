#pragma once

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

// Shared by the standalone harnesses in tools/. Throws on a missing file so a
// typo in a path fails loudly instead of producing an empty run.
inline nlohmann::json ReadJsonFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    return nlohmann::json::parse(file);
}
