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

// A scan dump can carry console noise ahead of the JSON when it was saved
// through the panel, so start reading at the first line that opens an
// object rather than at byte zero.
inline nlohmann::json ReadDumpJson(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::string line;
    std::string body;
    bool started = false;
    while (std::getline(file, line)) {
        if (!started) {
            const auto firstReal = line.find_first_not_of(" \t\r");
            if (firstReal == std::string::npos || line[firstReal] != '{') {
                continue;
            }
            started = true;
        }
        body += line;
        body += '\n';
    }

    if (!started) {
        throw std::runtime_error("No JSON body found in: " + path);
    }
    return nlohmann::json::parse(body);
}
