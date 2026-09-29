#pragma once

#include <string>

// =============================================================================
// ENCODING & SANITIZATION UTILITIES
// =============================================================================
// Common text utilities used throughout the plugin:
// - UTF-8 conversion/validation (encoding)
// - Filename sanitization (filesystem safety)
// =============================================================================

namespace EncodingUtils
{
    // Convert string from system ANSI codepage (GBK/Shift-JIS/etc.) to UTF-8.
    // Skyrim's GetFullName() returns strings in the system's ANSI codepage.
    // The codepage used is determined by the user's Windows locale (CP_ACP).
    std::string ConvertToUTF8(const std::string& input);

    // The same from a given code page (1252, 1251, 949 ...): ConvertToUTF8 is
    // this with CP_ACP. Lets a check test a code page other than the machine's.
    std::string ConvertFromCodePage(const std::string& input, unsigned int codePage);

    // Sanitize a string to valid UTF-8 for safe JSON serialization.
    // If already valid UTF-8, returns the input unchanged (fast path).
    // Otherwise converts from the system's ANSI codepage.
    std::string SanitizeToUTF8(const std::string& input);

    // The same with an explicit code page for the fallback (SanitizeToUTF8 above
    // is this with CP_ACP): lets a check run the German or Korean case anywhere.
    std::string SanitizeToUTF8(const std::string& input, unsigned int codePage);

    // Sanitize a string for use as a Windows filename.
    // Replaces forbidden characters (/ \ : * ? " < > |) and control characters
    // (0x00-0x1F) with underscores, trims trailing dots/spaces, prefixes Windows
    // reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9) with "_",
    // and returns "_unnamed" if the result is empty.
    std::string SanitizeFilename(const std::string& name);
}
