#pragma once

#include "PathText.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <vector>

// =============================================================================
// PRESET FILES - writing and deleting one preset's file (UIManagerIO.cpp)
// =============================================================================
//
// No logging and no game types, so an offline check can compile it as it is; the
// caller logs what the results say. A preset's file is <dir>/<safeName>.json, the
// name going to the file system as UTF-8 (PathText::FromUtf8).
namespace PresetFiles
{
    [[nodiscard]] inline bool HasNonAscii(const std::string& text)
    {
        for (const unsigned char c : text) {
            if (c >= 0x80) return true;
        }
        return false;
    }

    [[nodiscard]] inline bool IsJsonFile(const std::filesystem::path& path)
    {
        const auto ext = PathText::Utf8(path.extension());
        return ext == ".json" || ext == ".JSON";
    }

    [[nodiscard]] inline std::filesystem::path FilePath(const std::filesystem::path& dir, const std::string& safeName)
    {
        return dir / PathText::FromUtf8(safeName + ".json");
    }

    /** Writes the preset's file; false when it could not be opened or written completely. */
    [[nodiscard]] inline bool Write(const std::filesystem::path& filePath, const std::string& text)
    {
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file << text;
        file.close();
        return static_cast<bool>(file);
    }

    struct DeleteResult
    {
        bool direct = false;                              // the file named after the preset was removed
        std::vector<std::filesystem::path> legacy;        // files found by the "name" inside
        std::vector<std::string> problems;                // a file that could not be read or removed
        std::error_code walkError;                        // the folder walk stopped early
    };

    /** The "name" inside a preset file, empty when there is none. The file is closed on return. */
    [[nodiscard]] inline std::string NameInside(const std::filesystem::path& path)
    {
        std::string text;
        {
            // Read and close before anything removes the file: MSVC opens a stream without
            // FILE_SHARE_DELETE, so remove() on a file still open here throws
            std::ifstream file(path, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        const auto data = nlohmann::json::parse(text, nullptr, false);
        if (!data.is_object()) return {};
        const auto it = data.find("name");
        return (it != data.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    /**
     * Deletes a preset: its own file, or when there is none and the name has a non-ASCII
     * letter, every file whose "name" inside is the name. Builds before PathText::FromUtf8
     * wrote a non-ASCII name through the ANSI code page, so such a file sits under a garbled
     * name; an ASCII name was never garbled, so a file that only carries it inside (an
     * add-on's AddonFire.json named "Fire") is left alone. Each file has its own try.
     */
    inline DeleteResult Delete(const std::filesystem::path& dir, const std::string& name, const std::string& safeName)
    {
        DeleteResult result;
        const auto filePath = FilePath(dir, safeName);
        std::error_code error;
        if (std::filesystem::exists(filePath, error)) {
            result.direct = std::filesystem::remove(filePath, error);
            if (error) result.problems.push_back(error.message());
            return result;
        }
        if (!HasNonAscii(name) || !std::filesystem::is_directory(dir, error)) return result;

        std::filesystem::directory_iterator it(dir, error);
        const std::filesystem::directory_iterator end;
        for (; !error && it != end; it.increment(error)) {
            try {
                const auto& path = it->path();
                if (!it->is_regular_file() || !IsJsonFile(path)) continue;
                if (NameInside(path) != name) continue;
                if (std::filesystem::remove(path)) result.legacy.push_back(path);
            } catch (const std::exception& e) {
                result.problems.push_back(e.what());
            }
        }
        result.walkError = error;
        return result;
    }
}
