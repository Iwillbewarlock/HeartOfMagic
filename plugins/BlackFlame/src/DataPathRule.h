#pragma once

// =============================================================================
// DATA PATH RULE - one rule for a record's or a NIF slot's path text, shared by the DLL and the builder
// =============================================================================
// The DLL (NifDump: which file the dump reads and where it writes it) and the builder (tools/blackflame-greybuild
// GreyTexture: which dumped file a slot names) must agree byte for byte, so both include this header. C++17 and the
// Windows API only (the builder is C++17 and links no CommonLib); its own test table runs in the builder self-test.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace BlackFlame::DataPathRule
{
    inline constexpr std::string_view kDataRoot = "data\\";
    inline constexpr std::string_view kMeshesRoot = "meshes\\";
    inline constexpr std::string_view kTexturesRoot = "textures\\";
    inline constexpr std::string_view kForbidden = "<>\"|?*";
    inline constexpr unsigned char kFirstPrintable = 0x20;
    inline constexpr unsigned char kDelete = 0x7F;
    inline constexpr unsigned char kFirstHighByte = 0x80;
    inline constexpr std::string_view kDdsSuffix = ".dds";
    inline constexpr std::size_t kMaxTexturePath = 260;  // MAX_PATH: a longer run of text before ".dds" is no texture slot

    [[nodiscard]] inline bool StartsWith(std::string_view a_text, std::string_view a_prefix) noexcept
    {
        return a_text.substr(0, a_prefix.size()) == a_prefix;
    }

    /// A lead byte of a double-byte character of the system code page (the next byte is its trail, any value)
    [[nodiscard]] inline bool IsLeadByte(unsigned char a_byte) noexcept
    {
        return a_byte >= kFirstHighByte && ::IsDBCSLeadByteEx(CP_ACP, a_byte);
    }

    /// ASCII letters lower case and '/' as '\'; a double-byte character of the system code page is copied as it is (its
    /// trail byte may look like a letter or '\'). The case and separator rule every path key follows (models, textures,
    /// the builder's and the DLL's maps); Normalize adds the folding and the checks
    [[nodiscard]] inline std::string LowerSlashes(std::string_view a_text)
    {
        std::string out;
        out.reserve(a_text.size());
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            const auto c = static_cast<unsigned char>(a_text[i]);
            if (IsLeadByte(c) && i + 1 < a_text.size()) {
                out.push_back(a_text[i]);
                out.push_back(a_text[++i]);
                continue;
            }
            out.push_back(c == '/' ? '\\' : static_cast<char>(c < kFirstHighByte ? std::tolower(c) : c));
        }
        return out;
    }

    /// A path text as a Data-relative path under a_root ("meshes\" or "textures\"): LowerSlashes, leading and repeated '\'
    /// folded, every leading "data\" and a_root dropped and one a_root put in front. A double-byte character of the system
    /// code page (lead byte by IsDBCSLeadByteEx) is copied as it is and counts as one character of its path token: its
    /// trail byte is never lowered, taken for a separator (a real '\' after it is kept, a path ending in it is not taken
    /// for one ending in '\') or split off a ".." token. nullopt (refused) for an empty path, a drive letter or ':'
    /// anywhere, a ".." path token, a trailing separator, or a character Windows forbids in a file name (<>"|?* and
    /// control characters): such a path could name a file outside Data, a folder, or no file at all.
    [[nodiscard]] inline std::optional<std::string> Normalize(std::string_view a_text, std::string_view a_root)
    {
        std::string path;
        path.reserve(a_text.size() + a_root.size());
        bool lastSeparator = false;  // U3: the last byte written was a real separator (not a DBCS trail byte 0x5C)
        std::size_t token = 0;       // where the current path token starts in path
        const auto tokenIsDots = [&]() { return std::string_view(path).substr(token) == ".."; };
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            const auto c = static_cast<unsigned char>(a_text[i]);
            if (IsLeadByte(c) && i + 1 < a_text.size()) {
                path.push_back(a_text[i]);  // a double-byte character as it is: its trail byte may look like '\\' or a letter
                path.push_back(a_text[++i]);
                lastSeparator = false;
                continue;
            }
            if (c == ':' || c < kFirstPrintable || kForbidden.find(static_cast<char>(c)) != std::string_view::npos) {
                return std::nullopt;  // a drive letter, a stream name or a character no file name has
            }
            const char out = c == '/' ? '\\' : static_cast<char>(c < kFirstHighByte ? std::tolower(c) : c);
            if (out == '\\') {
                if (path.empty() || lastSeparator) {
                    continue;  // a leading or repeated separator
                }
                if (tokenIsDots()) {
                    return std::nullopt;
                }
                path.push_back(out);
                lastSeparator = true;
                token = path.size();
                continue;
            }
            path.push_back(out);
            lastSeparator = false;
        }
        if (lastSeparator || tokenIsDots()) {
            return std::nullopt;  // a folder, not a file; or a last ".." token
        }
        for (bool stripped = true; stripped;) {  // "data\\meshes\\meshes\\x.nif" too (whole ASCII tokens only)
            stripped = false;
            for (const auto root : { kDataRoot, a_root }) {
                if (StartsWith(path, root)) {
                    path.erase(0, root.size());
                    stripped = true;
                }
            }
        }
        if (path.empty()) {
            return std::nullopt;
        }
        return std::string(a_root) + path;
    }

    /// Every texture path a NIF names inline (BSShaderTextureSet slots, effect shader textures: a uint32 length, then the
    /// text): each ".dds" (any case) whose run of printable bytes before it is exactly as long as the 4 little-endian bytes
    /// in front say. Reads bytes only (no NIF parser), so it finds the slots of any block type; the text as stored.
    [[nodiscard]] inline std::vector<std::string> TexturePathsIn(const unsigned char* a_bytes, std::size_t a_size)
    {
        std::vector<std::string> paths;
        for (std::size_t at = 0; at + kDdsSuffix.size() <= a_size; ++at) {
            bool dds = true;
            for (std::size_t i = 0; i < kDdsSuffix.size() && dds; ++i) {
                dds = std::tolower(a_bytes[at + i]) == kDdsSuffix[i];
            }
            if (!dds) {
                continue;
            }
            const std::size_t end = at + kDdsSuffix.size();
            std::size_t start = at;
            while (start > 0 && end - start < kMaxTexturePath && a_bytes[start - 1] >= kFirstPrintable &&
                   a_bytes[start - 1] != kDelete) {
                --start;
            }
            if (start < sizeof(std::uint32_t)) {
                continue;
            }
            std::uint32_t length = 0;
            for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
                length |= static_cast<std::uint32_t>(a_bytes[start - sizeof(std::uint32_t) + i]) << (8 * i);
            }
            if (length == end - start) {
                paths.emplace_back(reinterpret_cast<const char*>(a_bytes + start), end - start);
                at = end - 1;
            }
        }
        return paths;
    }
}
