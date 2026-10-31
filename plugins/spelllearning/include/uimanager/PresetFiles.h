#pragma once

#include "AsciiText.h"
#include "FileUtils.h"
#include "PathText.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// =============================================================================
// PRESET FILES - writing, deleting and renaming one preset's file (UIManagerIO.cpp)
// =============================================================================
//
// No logging and no game types, so an offline check can compile it as it is; the
// caller logs what the results say. A preset's file is <dir>/<safeName>.json, the
// name going to the file system as UTF-8 (PathText::FromUtf8).
namespace PresetFiles
{
    using AsciiText::HasNonAscii;

    [[nodiscard]] inline bool IsJsonFile(const std::filesystem::path& path)
    {
        const auto ext = PathText::Utf8(path.extension());
        return ext == ".json" || ext == ".JSON";
    }

    [[nodiscard]] inline std::filesystem::path FilePath(const std::filesystem::path& dir, const std::string& safeName)
    {
        return dir / PathText::FromUtf8(safeName + ".json");
    }

    /**
     * Writes the preset's file through a temp file and a move (FileUtils::WriteAtomically),
     * so a crash mid-write no longer leaves an empty preset; false when it could not be
     * written. No .bak: the presets folder lists every file in it.
     */
    [[nodiscard]] inline bool Write(const std::filesystem::path& filePath, const std::string& text)
    {
        return FileUtils::WriteAtomically(filePath, text, false);
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

    /** A preset's files on disk (Find): its own file, or an older build's files for it */
    struct FoundFiles
    {
        std::filesystem::path direct;                     // the file named after the preset, empty when none
        std::vector<std::filesystem::path> legacy;        // files found by the "name" inside
        std::vector<std::string> problems;                // a file that could not be read
        std::error_code walkError;                        // the folder walk stopped early
    };

    /**
     * A preset's files: its own file, or when there is none and the name has a non-ASCII
     * letter, every file whose "name" inside is the name. Builds before PathText::FromUtf8
     * wrote a non-ASCII name through the ANSI code page, so such a file sits under a garbled
     * name; an ASCII name was never garbled, so a file that only carries it inside (an
     * add-on's AddonFire.json named "Fire") is left alone. Each file has its own try.
     */
    inline FoundFiles Find(const std::filesystem::path& dir, const std::string& name, const std::string& safeName)
    {
        FoundFiles found;
        const auto filePath = FilePath(dir, safeName);
        std::error_code error;
        if (std::filesystem::exists(filePath, error)) {
            found.direct = filePath;
            return found;
        }
        if (!HasNonAscii(name) || !std::filesystem::is_directory(dir, error)) return found;

        std::filesystem::directory_iterator it(dir, error);
        const std::filesystem::directory_iterator end;
        for (; !error && it != end; it.increment(error)) {
            try {
                const auto& path = it->path();
                if (!it->is_regular_file() || !IsJsonFile(path)) continue;
                if (NameInside(path) == name) found.legacy.push_back(path);
            } catch (const std::exception& e) {
                found.problems.push_back(e.what());
            }
        }
        found.walkError = error;
        return found;
    }

    /** Deletes a preset's files (Find): its own file, else an older build's files for it. */
    inline DeleteResult Delete(const std::filesystem::path& dir, const std::string& name, const std::string& safeName)
    {
        DeleteResult result;
        auto found = Find(dir, name, safeName);
        result.problems = std::move(found.problems);
        result.walkError = found.walkError;
        std::error_code error;
        if (!found.direct.empty()) {
            result.direct = std::filesystem::remove(found.direct, error);
            if (error) result.problems.push_back(error.message());
            return result;
        }
        for (const auto& path : found.legacy) {
            if (std::filesystem::remove(path, error)) {
                result.legacy.push_back(path);
            } else if (error) {
                result.problems.push_back(PathText::Utf8(path) + ": " + error.message());
            }
        }
        return result;
    }

    struct RenameResult
    {
        bool written = false;                             // the new file is on disk
        bool refused = false;                             // the new name's file is another preset's: nothing written
        std::filesystem::path newPath;
        std::vector<std::filesystem::path> removed;       // the old preset's files, deleted after the write
        std::vector<std::filesystem::path> kept;          // an old file that is the new file (same name on disk)
        std::vector<std::string> problems;                // a file that could not be read or removed
    };

    /**
     * Renames a preset: its old files are found first (Find, an older build's file under a
     * garbled name too), the new file is written (Write), and only then are the old files
     * deleted - none that is the new file itself (two names that sanitize alike, a case-only
     * change: Windows file names ignore case). A failed write deletes nothing. When the new
     * name's file is there and is none of the old preset's own files - another preset, whose
     * name sanitizes to the same file ("Fire." and "Fire", "a/b" and "a_b") - nothing is
     * written unless `overwrite` (the player confirmed replacing that preset).
     */
    inline RenameResult Rename(const std::filesystem::path& dir, const std::string& oldName,
                               const std::string& oldSafeName, const std::string& newSafeName,
                               const std::string& text, bool overwrite)
    {
        RenameResult result;
        auto found = Find(dir, oldName, oldSafeName);
        result.problems = std::move(found.problems);
        if (found.walkError) result.problems.push_back("folder walk stopped early: " + found.walkError.message());
        std::vector<std::filesystem::path> olds = std::move(found.legacy);
        if (!found.direct.empty()) olds.push_back(found.direct);

        result.newPath = FilePath(dir, newSafeName);
        std::error_code taken;
        if (!overwrite && std::filesystem::exists(result.newPath, taken)) {
            const bool own = std::ranges::any_of(olds, [&](const std::filesystem::path& old) {
                std::error_code error;
                return std::filesystem::equivalent(old, result.newPath, error) && !error;
            });
            if (!own) {
                result.refused = true;
                return result;
            }
        }
        result.written = Write(result.newPath, text);
        if (!result.written) return result;

        for (const auto& old : olds) {
            std::error_code error;
            if (std::filesystem::equivalent(old, result.newPath, error) && !error) {
                result.kept.push_back(old);
                continue;
            }
            error.clear();
            if (std::filesystem::remove(old, error)) {
                result.removed.push_back(old);
            } else if (error) {
                result.problems.push_back(PathText::Utf8(old) + ": " + error.message());
            }
        }
        return result;
    }
}
