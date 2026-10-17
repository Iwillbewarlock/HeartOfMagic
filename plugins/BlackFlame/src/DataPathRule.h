#pragma once

// =============================================================================
// DATA PATH RULE - one rule for a record's or a NIF slot's path text, shared by the DLL and the builder
// =============================================================================
// The DLL (NifDump: which file the dump reads and where it writes it) and the builder (tools/blackflame-greybuild
// GreyTexture: which dumped file a slot names) must agree byte for byte, so both include this header. C++17 and the
// Windows API only (the builder is C++17 and links no CommonLib); its own test table runs in the builder self-test.

#include <algorithm>
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

    /// IsLeadByte as a callable type: the default lead-byte rule of every *With function's caller (the system code page)
    struct SystemLeadByte
    {
        [[nodiscard]] bool operator()(unsigned char a_byte) const noexcept { return IsLeadByte(a_byte); }
    };

    /// ASCII A-Z to a-z, nothing else changed; a double-byte character (lead byte by a_isLeadByte) is copied as it is, so a
    /// trail byte that looks like a capital is never lowered (code page 932: 0x83 0x41 stays, it does not become 0x83 0x61)
    template <class LeadByte>
    [[nodiscard]] std::string LowerAsciiWith(std::string_view a_text, LeadByte a_isLeadByte)
    {
        std::string out(a_text);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto c = static_cast<unsigned char>(out[i]);
            if (a_isLeadByte(c) && i + 1 < out.size()) {
                ++i;
                continue;
            }
            if (c >= 'A' && c <= 'Z') {
                out[i] = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    /// A path text as a Data-relative path under a_root ("meshes\" or "textures\"): LowerSlashes, leading and repeated '\'
    /// folded, every leading "data\" and a_root dropped and one a_root put in front. A double-byte character of the system
    /// code page (lead byte by IsDBCSLeadByteEx) is copied as it is and counts as one character of its path token: its
    /// trail byte is never lowered, taken for a separator (a real '\' after it is kept, a path ending in it is not taken
    /// for one ending in '\') or split off a ".." token. nullopt (refused) for an empty path, a drive letter or ':'
    /// anywhere, a "." or ".." path token (AC2: a "." token is refused, not folded - textures\.\fx.dds is refused), a
    /// trailing separator, or a character Windows forbids in a file name (<>"|?* and control characters): such a path
    /// could name a file outside Data, a folder, or no file at all. A trail byte is copied unchecked, so a normalised
    /// path with a byte >= 0x80 may hold any byte after it ('|' and '\' included). Normalize uses the system code page;
    /// NormalizeWith takes a_isLeadByte instead (the shared test table runs a fixed double-byte code page on any system).
    template <class LeadByte>
    [[nodiscard]] std::optional<std::string> NormalizeWith(std::string_view a_text, std::string_view a_root, LeadByte a_isLeadByte)
    {
        std::string path;
        path.reserve(a_text.size() + a_root.size());
        bool lastSeparator = false;  // U3: the last byte written was a real separator (not a DBCS trail byte 0x5C)
        std::size_t token = 0;       // where the current path token starts in path
        // AC2: "." and ".." tokens both (a "." token names its folder, a ".." one leaves it)
        const auto tokenIsDots = [&]() {
            const auto text = std::string_view(path).substr(token);
            return text == ".." || text == ".";
        };
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            const auto c = static_cast<unsigned char>(a_text[i]);
            if (a_isLeadByte(c) && i + 1 < a_text.size()) {
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
            return std::nullopt;  // a folder, not a file; or a last "." or ".." token
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

    [[nodiscard]] inline std::optional<std::string> Normalize(std::string_view a_text, std::string_view a_root)
    {
        return NormalizeWith(a_text, a_root, IsLeadByte);
    }

    // Z6: our palettes, as Normalize under textures\ gives them: the two shipped ones and the builder's derived ones
    inline constexpr std::string_view kOurPalette = "textures\\heartofmagic\\blackflame\\gradblackflame.dds";
    inline constexpr std::string_view kOurPaletteAdd = "textures\\heartofmagic\\blackflame\\gradblackflameadd.dds";
    inline constexpr std::string_view kDerivedPaletteAdd = "textures\\heartofmagic\\blackflame\\copies\\palettes\\add\\";
    inline constexpr std::string_view kDerivedPaletteNormal = "textures\\heartofmagic\\blackflame\\copies\\palettes\\normal\\";

    /// Z6: the one definition of "a palette of ours" (the DLL's IsAnyPalettePath, the builder's VerifyGrey and the risk
    /// check's PAL_FOREIGN): a slot naming one of the two shipped palettes, or a derived palette under
    /// copies\palettes\add\ or copies\palettes\normal\; compared by these constants, never by what loaded
    [[nodiscard]] inline bool IsOurPalettePath(std::string_view a_slot)
    {
        const auto path = Normalize(a_slot, kTexturesRoot);
        return path && (*path == kOurPalette || *path == kOurPaletteAdd || StartsWith(*path, kDerivedPaletteAdd) ||
                           StartsWith(*path, kDerivedPaletteNormal));
    }

    // =========================================================================
    // Single-launch copies (CopyContract.h): <root>heartofmagic\blackflame\copies\<key16>\copy.<ext>
    // =========================================================================
    // One object per key folder, under a fixed ASCII name: copy.<ext>, ext the source's extension (lower case, 1..8
    // ASCII letters or digits). So every copy path is ASCII: progress.jsonl carries it as plain JSON text, and no code
    // page decides where a separator is. The source's own name, non-ASCII included, appears only inside its key string.
    // The staging folder (<SKSE log folder>\HeartOfMagic_BlackFlame\staging) mirrors Data, so a copy's staging-relative
    // path is its Data-relative path. The key16 folder never collides with the older copies\palettes\ and copies\_tint\
    // folders (16 hex digits).

    inline constexpr std::string_view kCopiesFolder = "heartofmagic\\blackflame\\copies\\";
    inline constexpr std::size_t kKey16Length = 16;
    inline constexpr std::string_view kCopyStem = "copy.";
    inline constexpr std::size_t kMaxCopyExtension = 8;
    // Windows device names: a file named so (any extension, any case) is the device, not a file
    inline constexpr std::string_view kDeviceNames[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5",
        "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };

    struct CopyPathParts
    {
        std::string_view root;  // kMeshesRoot or kTexturesRoot
        std::string key16;
        std::string extension;  // "nif", "dds", ...
    };

    /// 16 lower-case hex digits (the one definition; CopyContract::IsKey16 calls it)
    [[nodiscard]] inline constexpr bool IsCopyKey16(std::string_view a_text) noexcept
    {
        if (a_text.size() != kKey16Length) {
            return false;
        }
        for (const char c : a_text) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    /// A file name Windows keeps as written: printable ASCII only (no byte >= 0x80 or < 0x20), none of <>"|?*:/\, no
    /// trailing '.' or ' ' (Windows trims them, so "a.nif." is "a.nif"), not a device name (con, nul, com1, ... with any
    /// extension, any case)
    [[nodiscard]] inline bool IsPlainAsciiName(std::string_view a_name) noexcept
    {
        if (a_name.empty() || a_name.back() == '.' || a_name.back() == ' ') {
            return false;
        }
        for (const char c : a_name) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < kFirstPrintable || byte >= kDelete || kForbidden.find(c) != std::string_view::npos || c == ':' ||
                c == '/' || c == '\\') {
                return false;
            }
        }
        const auto stem = a_name.substr(0, a_name.find('.'));
        for (const auto device : kDeviceNames) {
            if (stem.size() == device.size() &&
                std::equal(stem.begin(), stem.end(), device.begin(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; })) {
                return false;
            }
        }
        return true;
    }

    /// Where a normalised path's last token starts: one past its last real '\' (a double-byte character's trail byte
    /// 0x5C is no separator, by a_isLeadByte), 0 when it has none
    template <class LeadByte>
    [[nodiscard]] std::size_t LastTokenStartWith(std::string_view a_path, LeadByte a_isLeadByte) noexcept
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i < a_path.size(); ++i) {
            const auto c = static_cast<unsigned char>(a_path[i]);
            if (a_isLeadByte(c) && i + 1 < a_path.size()) {
                ++i;
                continue;
            }
            if (c == '\\') {
                start = i + 1;
            }
        }
        return start;
    }

    [[nodiscard]] inline std::size_t LastTokenStart(std::string_view a_path) noexcept
    {
        return LastTokenStartWith(a_path, IsLeadByte);
    }

    /// The extension a copy keeps: the name's text after its last '.', 1..8 lower-case ASCII letters or digits; else
    /// nullopt
    [[nodiscard]] inline std::optional<std::string> CopyExtension(std::string_view a_name)
    {
        const auto dot = a_name.rfind('.');
        if (dot == std::string_view::npos) {
            return std::nullopt;
        }
        const auto extension = a_name.substr(dot + 1);
        if (extension.empty() || extension.size() > kMaxCopyExtension) {
            return std::nullopt;
        }
        for (const char c : extension) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                return std::nullopt;
            }
        }
        return std::string(extension);
    }

    /// CopyPath back: a_path must be exactly <root>heartofmagic\blackflame\copies\<key16>\copy.<ext> - ASCII only (any
    /// byte >= 0x80 or < 0x20 refused), Normalize leaves it unchanged, the name a plain ASCII name (IsPlainAsciiName) of
    /// the form copy.<ext>; else nullopt. The DLL's guard for every path it copies from staging into Data.
    [[nodiscard]] inline std::optional<CopyPathParts> ParseCopyPath(std::string_view a_path)
    {
        for (const char c : a_path) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < kFirstPrintable || byte >= kDelete) {
                return std::nullopt;
            }
        }
        for (const auto root : { kMeshesRoot, kTexturesRoot }) {
            if (!StartsWith(a_path, root)) {
                continue;
            }
            const auto normalized = Normalize(a_path, root);
            if (!normalized || *normalized != a_path) {
                return std::nullopt;
            }
            std::string_view rest = a_path.substr(root.size());
            if (!StartsWith(rest, kCopiesFolder)) {
                return std::nullopt;
            }
            rest.remove_prefix(kCopiesFolder.size());
            if (rest.size() <= kKey16Length + 1 || rest[kKey16Length] != '\\' || !IsCopyKey16(rest.substr(0, kKey16Length))) {
                return std::nullopt;
            }
            const std::string_view name = rest.substr(kKey16Length + 1);
            const auto extension = CopyExtension(name);
            if (!IsPlainAsciiName(name) || !StartsWith(name, kCopyStem) || !extension ||
                name.size() != kCopyStem.size() + extension->size()) {
                return std::nullopt;  // a folder below the key folder, another name, a name Windows would change
            }
            return CopyPathParts{ root, std::string(rest.substr(0, kKey16Length)), *extension };
        }
        return std::nullopt;
    }

    /// CopyPath with a_isLeadByte in place of the system code page's (the test table's fixed double-byte code page)
    template <class LeadByte>
    [[nodiscard]] std::optional<std::string> CopyPathWith(std::string_view a_root, std::string_view a_key16,
        std::string_view a_source, LeadByte a_isLeadByte)
    {
        if ((a_root != kMeshesRoot && a_root != kTexturesRoot) || !IsCopyKey16(a_key16)) {
            return std::nullopt;
        }
        const auto source = NormalizeWith(a_source, a_root, a_isLeadByte);
        if (!source) {
            return std::nullopt;
        }
        const auto extension = CopyExtension(std::string_view(*source).substr(LastTokenStartWith(*source, a_isLeadByte)));
        if (!extension) {
            return std::nullopt;
        }
        std::string path;
        path.append(a_root).append(kCopiesFolder).append(a_key16).append("\\").append(kCopyStem).append(*extension);
        if (!ParseCopyPath(path)) {
            return std::nullopt;  // unreachable by construction; kept so CopyPath never gives what the guard refuses
        }
        return path;
    }

    /// The copy of a_source (a record's or a slot's path text) under a_root (kMeshesRoot or kTexturesRoot) for a_key16:
    /// <root>heartofmagic\blackflame\copies\<key16>\copy.<the source's extension>. nullopt for another root, a key16 that
    /// is not 16 lower-case hex digits, a source Normalize refuses, or one without a plain extension.
    [[nodiscard]] inline std::optional<std::string> CopyPath(std::string_view a_root, std::string_view a_key16,
        std::string_view a_source)
    {
        return CopyPathWith(a_root, a_key16, a_source, IsLeadByte);
    }

    /// A copy's path inside the staging folder (which mirrors Data): a_dataRelative itself when ParseCopyPath takes it,
    /// else nullopt
    [[nodiscard]] inline std::optional<std::string> StagingRelative(std::string_view a_dataRelative)
    {
        if (!ParseCopyPath(a_dataRelative)) {
            return std::nullopt;
        }
        return std::string(a_dataRelative);
    }

    /// AB1: a record string's own bytes as hex (upper case, two digits a byte), so a text that is no text in any code page
    /// travels through request.json and the copies map unchanged
    [[nodiscard]] inline std::string ToHex(std::string_view a_bytes)
    {
        constexpr char kDigits[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(a_bytes.size() * 2);
        for (const char c : a_bytes) {
            const auto byte = static_cast<unsigned char>(c);
            out.push_back(kDigits[byte >> 4]);
            out.push_back(kDigits[byte & 0x0F]);
        }
        return out;
    }

    /// ToHex back; nullopt for an odd length or a character that is no hex digit
    [[nodiscard]] inline std::optional<std::string> FromHex(std::string_view a_hex)
    {
        if (a_hex.size() % 2 != 0) {
            return std::nullopt;
        }
        const auto digit = [](char a_c) -> int {
            return a_c >= '0' && a_c <= '9' ? a_c - '0' : a_c >= 'A' && a_c <= 'F' ? a_c - 'A' + 10 : a_c >= 'a' && a_c <= 'f' ? a_c - 'a' + 10 : -1;
        };
        std::string out;
        out.reserve(a_hex.size() / 2);
        for (std::size_t i = 0; i < a_hex.size(); i += 2) {
            const int high = digit(a_hex[i]);
            const int low = digit(a_hex[i + 1]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>(high * 16 + low));
        }
        return out;
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
