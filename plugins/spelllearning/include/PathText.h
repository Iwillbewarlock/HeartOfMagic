#pragma once

#include <filesystem>
#include <string>

/**
 * A path's text for logs, JSON and name checks.
 *
 * On Windows std::filesystem::path::string() converts the wide path to the
 * system's ANSI code page and throws std::system_error ("No mapping for the
 * Unicode character exists in the target multi-byte code page") for any name
 * that page cannot hold - a file called 화염.ini on an English Windows, a Data
 * file with an emoji in it. It does that for every file a directory walk meets,
 * mods' own files included, so a scan died on a name the player never chose.
 * u8string() has no such page to fail on: it is UTF-8, which holds every name.
 *
 * Use this for any path text whose file the plugin did not name itself.
 * It only goes from a path to text: going from narrow text back to a path
 * (std::filesystem::path(std::string) reads it in the ANSI code page) is not
 * covered, so keep a path a path and convert it for the log line only.
 */
namespace PathText
{
    [[nodiscard]] inline std::string Utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
}
