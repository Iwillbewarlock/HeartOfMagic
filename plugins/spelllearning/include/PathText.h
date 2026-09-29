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
 * u8string() does not go through that page: it is UTF-8, which holds every
 * well-formed name. It can still throw for an ill-formed one (an unpaired
 * UTF-16 surrogate, which NTFS allows), so a walk over files the plugin did not
 * name must still guard each entry, as ReadDistrFiles does.
 *
 * Use Utf8 for any path text whose file the plugin did not name itself.
 * The way back is FromUtf8: std::filesystem::path(std::string) reads the text
 * in the ANSI code page, so UTF-8 text from the panel (a preset name in Korean)
 * came out as a garbled file name or a throw. Keep a path a path otherwise.
 */
namespace PathText
{
    [[nodiscard]] inline std::string Utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    /** A path from UTF-8 text (the panel's JSON, a name made from it). */
    [[nodiscard]] inline std::filesystem::path FromUtf8(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
}
