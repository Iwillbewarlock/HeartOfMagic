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

    /// A path text as a Data-relative path under a_root ("meshes\" or "textures\"): ASCII letters lower case, '/' as '\',
    /// leading and repeated '\' folded, every leading "data\" and a_root dropped and one a_root put in front. A double-byte
    /// character of the system code page (lead byte by IsDBCSLeadByteEx) is copied as it is, so its trail byte is never
    /// lowered or taken for a separator. nullopt (refused) for an empty path, a drive letter or ':' anywhere, a ".." path
    /// token, or a character Windows forbids in a file name (<>"|?* and control characters): such a path could name a file
    /// outside Data or no file at all.
    [[nodiscard]] inline std::optional<std::string> Normalize(std::string_view a_text, std::string_view a_root)
    {
        std::string path;
        path.reserve(a_text.size() + a_root.size());
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            const auto c = static_cast<unsigned char>(a_text[i]);
            if (c >= kFirstHighByte && ::IsDBCSLeadByteEx(CP_ACP, c) && i + 1 < a_text.size()) {
                path.push_back(a_text[i]);  // a double-byte character as it is: its trail byte may look like '\\' or a letter
                path.push_back(a_text[++i]);
                continue;
            }
            if (c == ':' || c < kFirstPrintable || kForbidden.find(static_cast<char>(c)) != std::string_view::npos) {
                return std::nullopt;  // a drive letter, a stream name or a character no file name has
            }
            const char out = c == '/' ? '\\' : static_cast<char>(c < kFirstHighByte ? std::tolower(c) : c);
            if (out == '\\' && (path.empty() || path.back() == '\\')) {
                continue;  // a leading or repeated separator
            }
            path.push_back(out);
        }
        for (bool stripped = true; stripped;) {  // "data\\meshes\\meshes\\x.nif" too
            stripped = false;
            for (const auto root : { kDataRoot, a_root }) {
                if (StartsWith(path, root)) {
                    path.erase(0, root.size());
                    stripped = true;
                }
            }
        }
        if (path.empty() || path.back() == '\\') {
            return std::nullopt;
        }
        for (std::size_t at = 0; at < path.size();) {
            const std::size_t found = path.find('\\', at);
            const std::size_t end = found == std::string::npos ? path.size() : found;
            if (std::string_view(path).substr(at, end - at) == "..") {
                return std::nullopt;
            }
            at = end + 1;
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
