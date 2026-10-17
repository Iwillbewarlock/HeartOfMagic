#pragma once

// =============================================================================
// COPY CONTRACT TEST PATHS - the copy path cases of the shared test table (CopyContractTest.h)
// =============================================================================
// The double-byte cases run with a fixed code page 932 lead-byte rule (NormalizeWith / CopyPathWith), so they run the
// same on every system whatever its own code page.

#include "CopyContract.h"

#include <string>

namespace BlackFlame::CopyContractTest
{
    namespace PathDetail
    {
        inline constexpr const char* kKey16 = "0123456789abcdef";
        inline constexpr const char* kMeshCopy = "meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\copy.nif";
        inline constexpr const char* kTextureCopy = "textures\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\copy.dds";

        /// Code page 932's lead bytes (Shift-JIS: 0x81-0x9F, 0xE0-0xFC), whose trail byte may be 0x5C
        [[nodiscard]] inline bool IsCp932Lead(unsigned char a_byte) noexcept
        {
            return (a_byte >= 0x81 && a_byte <= 0x9F) || (a_byte >= 0xE0 && a_byte <= 0xFC);
        }

        /// No double-byte code page at all
        [[nodiscard]] inline bool NoLead(unsigned char) noexcept
        {
            return false;
        }
    }

    template <class Check>
    void CheckCopyPaths(Check& a_check)
    {
        namespace Rule = DataPathRule;
        using namespace PathDetail;
        const std::string base = "meshes\\heartofmagic\\blackflame\\copies\\";
        const std::string folder = base + kKey16 + "\\";

        a_check(Rule::CopyPath(Rule::kMeshesRoot, kKey16, "Magic/FX/FireBoltMuzzle.NIF") == kMeshCopy &&
                    Rule::CopyPath(Rule::kTexturesRoot, kKey16, "data\\Textures\\Effects\\FXFire01.DDS") == kTextureCopy,
            "path: copies\\<key16>\\copy.<ext> under meshes\\ and textures\\, the extension lower-cased");
        a_check(!Rule::CopyPath(Rule::kMeshesRoot, "0123456789ABCDEF", "a.nif") && !Rule::CopyPath(Rule::kMeshesRoot, "0123456789abcde", "a.nif") &&
                    !Rule::CopyPath(Rule::kMeshesRoot, "0123456789abcdef0", "a.nif") && !Rule::CopyPath("sound\\", kKey16, "a.nif") &&
                    !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "..\\a.nif") && !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "magic\\") &&
                    !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "magic\\noextension") &&
                    !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "magic\\a.toolongext") &&
                    !Rule::CopyPath(Rule::kMeshesRoot, kKey16, "magic\\a.n-f"),
            "path: refused for a bad key16, another root, a source Normalize refuses, a folder, no or an odd extension");
        const auto mesh = Rule::ParseCopyPath(kMeshCopy);
        const auto texture = Rule::ParseCopyPath(kTextureCopy);
        a_check(mesh && mesh->root == Rule::kMeshesRoot && mesh->key16 == kKey16 && mesh->extension == "nif" && texture &&
                    texture->root == Rule::kTexturesRoot && texture->extension == "dds" && Rule::StagingRelative(kMeshCopy) == kMeshCopy,
            "path: ParseCopyPath reads both roots back; the staging-relative path is the Data-relative one");
        a_check(!Rule::ParseCopyPath(folder + "sub\\copy.nif") && !Rule::ParseCopyPath(folder) && !Rule::ParseCopyPath(folder + "a.nif") &&
                    !Rule::ParseCopyPath(folder + "copy.NIF") && !Rule::ParseCopyPath(folder + "copy") &&
                    !Rule::ParseCopyPath(base + "palettes\\add\\a.dds") && !Rule::ParseCopyPath(folder + "..\\copy.nif") &&
                    !Rule::ParseCopyPath("meshes\\magic\\copy.nif") && !Rule::ParseCopyPath(base + "0123456789ABCDEF\\copy.nif") &&
                    !Rule::ParseCopyPath(base + "0123456789abcdef0\\copy.nif") && !Rule::ParseCopyPath("Meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\copy.nif") &&
                    !Rule::ParseCopyPath("meshes/heartofmagic/blackflame/copies/0123456789abcdef/copy.nif") &&
                    !Rule::StagingRelative("meshes\\a.nif"),
            "path: ParseCopyPath refuses a deeper folder, another name, upper case, the old palette folder, '..', other "
            "folders, a bad key16, an upper-case root, '/'");
        a_check(!Rule::ParseCopyPath(folder + "copy.nif.") && !Rule::ParseCopyPath(folder + "copy.nif ") &&
                    !Rule::ParseCopyPath(folder + "con.nif") && !Rule::ParseCopyPath(folder + std::string("copy.ni\x81")) &&
                    !Rule::ParseCopyPath(std::string("meshes\\heartofmagic\\blackflame\\copies\\0123456789abcdef\\copy.nif\0", 64)),
            "path: ParseCopyPath refuses a trailing dot or space, a device name, a byte >= 0x80, a NUL");
        a_check(Rule::IsPlainAsciiName("copy.nif") && Rule::IsPlainAsciiName("a b.nif") && !Rule::IsPlainAsciiName("a.nif.") &&
                    !Rule::IsPlainAsciiName("a.nif ") && !Rule::IsPlainAsciiName("CON") && !Rule::IsPlainAsciiName("nul.txt") &&
                    !Rule::IsPlainAsciiName("Com1.nif") && !Rule::IsPlainAsciiName("lpt9") && Rule::IsPlainAsciiName("com10.nif") &&
                    !Rule::IsPlainAsciiName("a:b") && !Rule::IsPlainAsciiName("a|b") && !Rule::IsPlainAsciiName(std::string("\xE3\x81\x82")) &&
                    !Rule::IsPlainAsciiName(""),
            "path: IsPlainAsciiName (Windows-trimmed names, device names in any case, forbidden and non-ASCII bytes)");

        const std::string paletteCopy = std::string("textures\\heartofmagic\\blackflame\\copies\\") + kKey16 + "\\palette.dds";
        const auto palette = Rule::ParseCopyPath(paletteCopy);
        a_check(Rule::PaletteCopyPath(kKey16) == paletteCopy && palette && palette->palette && palette->extension == "dds" &&
                    texture && !texture->palette && !Rule::PaletteCopyPath("0123456789ABCDEF") &&
                    !Rule::ParseCopyPath(folder + "palette.dds") && !Rule::ParseCopyPath(paletteCopy.substr(0, paletteCopy.size() - 3) + "DDS") &&
                    !Rule::ParseCopyPath(paletteCopy.substr(0, paletteCopy.size() - 3) + "nif"),
            "path: a derived palette is copies\\<key16>\\palette.dds under textures\\ only (not under meshes\\, no other case or "
            "extension)");
        a_check(Rule::IsOurPalettePath(paletteCopy) &&
                    Rule::IsOurPalettePath("Textures\\HeartOfMagic\\BlackFlame\\Copies\\0123456789abcdef\\Palette.dds") &&
                    Rule::IsOurPalettePath("heartofmagic/blackflame/gradblackflame.dds") &&
                    Rule::IsOurPalettePath("textures\\heartofmagic\\blackflame\\copies\\palettes\\add\\effects\\a.dds") &&
                    !Rule::IsOurPalettePath(kTextureCopy) && !Rule::IsOurPalettePath("effects\\gradientfire.dds") &&
                    !Rule::IsOurPalettePath("textures\\heartofmagic\\blackflame\\copies\\0123456789abcde\\palette.dds"),
            "path: IsOurPalettePath takes a single-launch palette (any case), the shipped and older derived ones, not a "
            "texture copy or a source palette");

        // Code page 932: "x" 0x81 0x5C is one character and a real separator, so the 0x5C is no separator
        const std::string dbcs = std::string("magic\\x\x81\\y.nif");
        const auto normalized932 = Rule::NormalizeWith(dbcs, Rule::kMeshesRoot, IsCp932Lead);
        const auto normalizedNone = Rule::NormalizeWith(dbcs, Rule::kMeshesRoot, NoLead);
        a_check(normalized932 && *normalized932 == "meshes\\" + dbcs && normalizedNone && *normalizedNone == "meshes\\" + dbcs &&
                    Rule::LastTokenStartWith(*normalized932, IsCp932Lead) == std::string("meshes\\magic\\").size() &&
                    Rule::LastTokenStartWith(*normalizedNone, NoLead) == std::string("meshes\\magic\\x\x81\\").size(),
            "path: code page 932 keeps a trail byte 0x5C inside its token; without a double-byte code page it separates");
        const std::string upperTrail = std::string("magic\\\x81Y.nif");  // 0x81 'Y': one character, its trail not lowered
        a_check(Rule::NormalizeWith(upperTrail, Rule::kMeshesRoot, IsCp932Lead) == "meshes\\" + upperTrail &&
                    Rule::NormalizeWith(upperTrail, Rule::kMeshesRoot, NoLead) == std::string("meshes\\magic\\\x81y.nif"),
            "path: code page 932 does not lower a trail byte that looks like a capital");
        a_check(Rule::CopyPathWith(Rule::kMeshesRoot, kKey16, dbcs, IsCp932Lead) == kMeshCopy &&
                    Rule::CopyPathWith(Rule::kMeshesRoot, kKey16, dbcs, NoLead) == kMeshCopy &&
                    !Rule::CopyPathWith(Rule::kMeshesRoot, kKey16, std::string("magic\\a.ni\x81\\"), IsCp932Lead),
            "path: a source with double-byte characters still gives the ASCII copy.<ext> path; a trail byte is no extension");
    }
}
