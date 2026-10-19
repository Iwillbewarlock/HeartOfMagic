#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// =============================================================================
// FORM ID TEXT - a form id (or a plugin's local id) written as hex (no game types)
// =============================================================================
//
// The scanner writes every formId as "0x%08X" and the panel sends it back as it
// got it; a tree imported or edited by hand may drop the "0x". Every formId text
// is read as hex: "00012345" is 0x00012345, not octal, and "12345678" is not
// decimal. Anything else - empty, a space, a sign, trailing text such as an edit
// mode copy's "_dup_", more than 8 digits - is no formId. The caller logs what it
// dropped; this header has no logger so the offline checks can use it.
namespace FormIdText
{
    inline constexpr int kHexBase = 16;
    /// A form id is 32 bits: at most 8 hex digits
    inline constexpr std::size_t kMaxHexDigits = 8;

    /// The id a text names: "0x" or "0X" optional, then 1-8 hex digits in any case and nothing else
    [[nodiscard]] inline std::optional<std::uint32_t> Parse(std::string_view text) noexcept
    {
        if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text.remove_prefix(2);
        if (text.empty() || text.size() > kMaxHexDigits) return std::nullopt;
        std::uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, kHexBase);
        if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
        return value;
    }

    /// "0x%08X": the one form formIds are written in (scan output, panel messages, progress JSON)
    [[nodiscard]] inline std::string Format(std::uint32_t formId)
    {
        return std::format("0x{:08X}", formId);
    }
}
