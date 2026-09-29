#pragma once

#include <algorithm>
#include <string_view>

/**
 * ASCII-only text checks, the same on every machine.
 *
 * _stricmp and std::tolower follow the C locale; on UTF-8 or code-page bytes
 * that is not a rule anyone can rely on. These fold A-Z only and leave every
 * byte from kFirstNonAsciiByte up as it is. No game types, no Windows headers.
 */
namespace AsciiText
{
    /// The first byte value outside 7-bit ASCII (a UTF-8 lead or continuation byte, or a code-page letter)
    inline constexpr unsigned char kFirstNonAsciiByte = 0x80;

    [[nodiscard]] constexpr bool HasNonAscii(std::string_view text) noexcept
    {
        return std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) >= kFirstNonAsciiByte; });
    }

    [[nodiscard]] constexpr char ToLower(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /// Equal ignoring the case of A-Z; every other byte must match exactly
    [[nodiscard]] constexpr bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
    {
        return a.size() == b.size() &&
               std::ranges::equal(a, b, [](char x, char y) { return ToLower(x) == ToLower(y); });
    }
}
