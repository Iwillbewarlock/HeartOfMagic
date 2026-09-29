#pragma once

#include <nlohmann/json.hpp>

#include <string>

/**
 * JSON text for the panel and for files.
 *
 * nlohmann::json::dump() throws type_error.316 when a string in the value is
 * not valid UTF-8. The game hands out names, editor ids and plugin file names
 * in the system's ANSI code page (Windows-1252 on a German or French game,
 * Windows-1251 on a Russian one, CP949 on a Korean one), so one such string
 * that missed EncodingUtils::SanitizeToUTF8 used to stop whatever was being
 * sent - the scan, the spell cards, the tome's learning notice - with the
 * player seeing nothing but a feature that did not happen.
 *
 * Dump never throws for that: a byte that is not UTF-8 becomes U+FFFD. Text
 * that should read correctly still goes through SanitizeToUTF8 first; this is
 * the net under it. Use it for every dump whose text leaves the plugin.
 */
namespace JsonText
{
    /** @param indent  -1 for one line (the panel), 2 for files people read */
    [[nodiscard]] inline std::string Dump(const nlohmann::json& value, int indent = -1)
    {
        return value.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}
