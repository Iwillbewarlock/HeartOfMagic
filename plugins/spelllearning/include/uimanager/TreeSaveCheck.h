#pragma once

#include <cstddef>
#include <string_view>

// =============================================================================
// TREE SAVE CHECK - SaveSpellTree's guard against a tree without spells
// =============================================================================
//
// UIManager::CheckTreeHasSpells runs on the game thread for every tree save, so
// it does not parse the 0.5-1 MB tree: the panel built that text with
// JSON.stringify and checked it already (SpellTreeSave.send). This only has to
// refuse a tree with no spell node: true when some "nodes" key holds an array
// whose first element is an object ({ "formId": ..., ... }). A spell tree keeps
// its spells under schools.<school>.nodes and has no other "nodes" key.
//
// Exact on valid JSON: an unescaped quote opens or closes a string, so "nodes"
// whose first quote is not escaped (an even run of backslashes before it) is a
// whole string token, and one followed by ':' is a key, never a value.
// Whitespace between the tokens is allowed. No game types: tools/tree-save-check.cpp compiles it as it is.
// O(n) in the text's length, no allocation.
namespace TreeSaveCheck
{
    namespace Detail
    {
        [[nodiscard]] constexpr bool IsSpace(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        [[nodiscard]] constexpr std::size_t SkipSpace(std::string_view text, std::size_t at) noexcept
        {
            while (at < text.size() && IsSpace(text[at])) ++at;
            return at;
        }

        /// The quote at `at` is escaped (an odd run of backslashes before it): inside a string
        [[nodiscard]] constexpr bool IsEscaped(std::string_view text, std::size_t at) noexcept
        {
            std::size_t slashes = 0;
            while (at > slashes && text[at - slashes - 1] == '\\') ++slashes;
            return (slashes % 2) == 1;
        }
    }

    /// The tree text has at least one spell node (see above).
    [[nodiscard]] constexpr bool HasSpellNodes(std::string_view text) noexcept
    {
        constexpr std::string_view kKey = "\"nodes\"";
        for (std::size_t at = text.find(kKey); at != std::string_view::npos; at = text.find(kKey, at + 1)) {
            if (Detail::IsEscaped(text, at)) continue;
            std::size_t next = Detail::SkipSpace(text, at + kKey.size());
            if (next >= text.size() || text[next] != ':') continue;
            next = Detail::SkipSpace(text, next + 1);
            if (next >= text.size() || text[next] != '[') continue;
            next = Detail::SkipSpace(text, next + 1);
            if (next < text.size() && text[next] == '{') return true;
        }
        return false;
    }
}
