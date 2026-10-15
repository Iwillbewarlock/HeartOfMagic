#pragma once

// =============================================================================
// GreyJson - checked reads shared by the map and request readers
// =============================================================================

#include "GreyBuild.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <fmt/format.h>
#include <fstream>
#include <stdexcept>

namespace GreyBuild::Json
{
    [[noreturn]] inline void Fail(const std::filesystem::path& a_file, const std::string& a_what)
    {
        throw std::runtime_error(fmt::format("'{}': {}", a_file.string(), a_what));
    }

    [[nodiscard]] inline nlohmann::json ReadFile(const std::filesystem::path& a_file)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            Fail(a_file, "cannot open it");
        }
        try {
            nlohmann::json root = nlohmann::json::parse(in);
            if (!root.is_object()) {
                Fail(a_file, "the top level must be an object");
            }
            return root;
        } catch (const nlohmann::json::parse_error& e) {
            Fail(a_file, fmt::format("not valid JSON: {}", e.what()));
        }
    }

    /// A Data-relative .nif path under a_root: no drive, no leading slash, no ".." segment.
    [[nodiscard]] inline std::string CheckedPath(const std::filesystem::path& a_file, const std::string& a_raw,
        const char* a_root, const std::string& a_where)
    {
        const std::string path = NormalizeDataPath(a_raw);
        const bool escapes = path.find(':') != std::string::npos || StartsWith(path, "\\") || path == ".." ||
                             StartsWith(path, "..\\") || path.find("\\..\\") != std::string::npos ||
                             EndsWith(path, "\\..");
        if (escapes) {
            Fail(a_file, fmt::format("{}: '{}' leaves the Data folder", a_where, a_raw));
        }
        if (!StartsWith(path, a_root) || !EndsWith(path, ".nif")) {
            Fail(a_file, fmt::format("{}: '{}' must be a .nif under '{}'", a_where, a_raw, a_root));
        }
        return path;
    }

    [[nodiscard]] inline long long ReadInteger(const std::filesystem::path& a_file, const nlohmann::json& a_entry,
        const char* a_key, const std::string& a_where, long long a_min, long long a_max)
    {
        if (!a_entry.contains(a_key) || !a_entry[a_key].is_number_integer()) {
            Fail(a_file, fmt::format("{}: '{}' must be an integer", a_where, a_key));
        }
        const auto value = a_entry[a_key].get<long long>();
        if (value < a_min || value > a_max) {
            Fail(a_file, fmt::format("{}: '{}' = {} is outside {}..{}", a_where, a_key, value, a_min, a_max));
        }
        return value;
    }

    [[nodiscard]] inline int ReadIndex(const std::filesystem::path& a_file, const nlohmann::json& a_entry,
        const char* a_key, const std::string& a_where)
    {
        return static_cast<int>(ReadInteger(a_file, a_entry, a_key, a_where, 0, kMaxIndex));
    }

    /// An optional u16 (DNAM fields); a_default when the key is missing.
    [[nodiscard]] inline std::uint16_t ReadU16(const std::filesystem::path& a_file, const nlohmann::json& a_entry,
        const char* a_key, const std::string& a_where, std::uint16_t a_default)
    {
        if (!a_entry.contains(a_key)) {
            return a_default;
        }
        return static_cast<std::uint16_t>(ReadInteger(a_file, a_entry, a_key, a_where, 0, UINT16_MAX));
    }

    [[nodiscard]] inline std::string ReadString(const std::filesystem::path& a_file, const nlohmann::json& a_entry,
        const char* a_key, const std::string& a_where)
    {
        if (!a_entry.contains(a_key) || !a_entry[a_key].is_string()) {
            Fail(a_file, fmt::format("{}: '{}' must be a string", a_where, a_key));
        }
        return a_entry[a_key].get<std::string>();
    }

    /// An optional string; "" when the key is missing.
    [[nodiscard]] inline std::string ReadOptionalString(const std::filesystem::path& a_file,
        const nlohmann::json& a_entry, const char* a_key, const std::string& a_where)
    {
        return a_entry.contains(a_key) ? ReadString(a_file, a_entry, a_key, a_where) : std::string();
    }

    inline void RequireVersion(const std::filesystem::path& a_file, const nlohmann::json& a_root, int a_version)
    {
        if (!a_root.contains("version") || a_root["version"] != a_version) {
            Fail(a_file, fmt::format("'version' must be {}", a_version));
        }
    }

    [[nodiscard]] inline const nlohmann::json& RequireArray(const std::filesystem::path& a_file,
        const nlohmann::json& a_root, const char* a_key)
    {
        if (!a_root.contains(a_key) || !a_root[a_key].is_array()) {
            Fail(a_file, fmt::format("'{}' must be an array", a_key));
        }
        return a_root[a_key];
    }
}
