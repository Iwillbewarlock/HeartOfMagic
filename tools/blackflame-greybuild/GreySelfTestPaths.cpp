// =============================================================================
// GreySelfTestPaths - --self-test of the path rule the DLL and the builder share (plugins/BlackFlame/src/DataPathRule.h)
// =============================================================================
// T7: one table run against the DLL's own code (DataPathRule::Normalize and TexturePathsIn, included, not copied) and the
// builder's TextureDataPath on top of it, so the dump path the DLL writes and the path a slot looks up cannot drift.

#include "GreySelfTest.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <cstdint>
#include <fmt/format.h>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace Rule = BlackFlame::DataPathRule;

        constexpr unsigned char kFirstLeadCandidate = 0x81;
        constexpr unsigned char kLastLeadCandidate = 0xFE;

        struct PathCase
        {
            const char* text;
            const char* expected;  // nullptr: refused
        };

        // The texture root ("textures\"), as the DLL normalises a slot's text for the dump
        constexpr PathCase kTextureCases[] = {
            { "Textures/Effects/FXFire01.DDS", "textures\\effects\\fxfire01.dds" },
            { "effects\\a.dds", "textures\\effects\\a.dds" },
            { "data\\textures\\b.dds", "textures\\b.dds" },
            { "\\\\Textures\\textures\\c.dds", "textures\\c.dds" },
            { "textures\\\\effects//d.dds", "textures\\effects\\d.dds" },
            { "Data/Data/textures/e.dds", "textures\\e.dds" },
            { "..\\x.dds", nullptr },
            { "effects\\..\\..\\x.dds", nullptr },
            { "c:\\x.dds", nullptr },
            { "a<b.dds", nullptr },
            { "a\tb.dds", nullptr },
            { "", nullptr },
            { "textures\\", nullptr },
        };

        [[nodiscard]] std::string Shown(const std::optional<std::string>& a_value)
        {
            return a_value ? "'" + *a_value + "'" : std::string("refused");
        }

        [[nodiscard]] std::vector<unsigned char> Slot(const std::string& a_text, std::uint32_t a_length)
        {
            std::vector<unsigned char> bytes;
            for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i) {
                bytes.push_back(static_cast<unsigned char>((a_length >> (8 * i)) & 0xFF));
            }
            bytes.insert(bytes.end(), a_text.begin(), a_text.end());
            return bytes;
        }

        [[nodiscard]] std::vector<unsigned char> Slot(const std::string& a_text)
        {
            return Slot(a_text, static_cast<std::uint32_t>(a_text.size()));
        }

        [[nodiscard]] std::vector<std::string> Scan(const std::vector<unsigned char>& a_bytes)
        {
            return Rule::TexturePathsIn(a_bytes.data(), a_bytes.size());
        }

        void CheckNormalize(Checker& a_check)
        {
            for (const auto& item : kTextureCases) {
                const auto dll = Rule::Normalize(item.text, Rule::kTexturesRoot);
                const auto slot = TextureDataPath(item.text);
                const bool ok = item.expected ? (dll && *dll == item.expected && slot == dll) : (!dll && !slot);
                a_check.Check(ok, fmt::format("path rule: '{}' -> {} (DLL {}, builder {})", item.text,
                    item.expected ? fmt::format("'{}'", item.expected) : std::string("refused"), Shown(dll), Shown(slot)));
            }
            a_check.Check(!TextureDataPath("textures\\x.tga") && Rule::Normalize("textures\\x.tga", Rule::kTexturesRoot),
                "path rule: a non-DDS name is a path for the DLL but no texture slot for the builder");
            a_check.Check(Rule::Normalize("Meshes\\Magic\\A.NIF", Rule::kMeshesRoot) == std::string("meshes\\magic\\a.nif") &&
                              Rule::Normalize("data\\meshes\\meshes\\b.nif", Rule::kMeshesRoot) == std::string("meshes\\b.nif"),
                "path rule: model paths under meshes\\ as before");

            // A double-byte character of the system code page: its trail byte kept as it is (not lowered)
            unsigned char lead = 0;
            for (unsigned int c = kFirstLeadCandidate; c <= kLastLeadCandidate && lead == 0; ++c) {
                if (::IsDBCSLeadByteEx(CP_ACP, static_cast<BYTE>(c))) {
                    lead = static_cast<unsigned char>(c);
                }
            }
            if (lead == 0) {
                std::cout << fmt::format("NOTE  path rule: code page {} has no double-byte characters; the DBCS case is not run\n",
                    ::GetACP());
                return;
            }
            const std::string text = std::string("A\\") + static_cast<char>(lead) + "AB.dds";
            const std::string expected = std::string("textures\\a\\") + static_cast<char>(lead) + "Ab.dds";
            a_check.Check(Rule::Normalize(text, Rule::kTexturesRoot) == expected && TextureDataPath(text) == expected,
                fmt::format("path rule: a double-byte character (lead 0x{:02X}, code page {}) keeps its trail byte", lead, ::GetACP()));
        }

        void CheckScanner(Checker& a_check)
        {
            a_check.Check(Scan(Slot("textures\\effects\\a.dds")) == std::vector<std::string>{ "textures\\effects\\a.dds" },
                "texture scan: a length-prefixed slot is found");
            a_check.Check(Scan(Slot("textures\\a.dds.bak")).empty(), "texture scan: '.dds' inside a longer string is not a slot");
            a_check.Check(Scan(Slot("x.dds", 6)).empty(), "texture scan: a length that does not match the text is not a slot");
            const std::string bare = "a.dds";
            a_check.Check(Scan(std::vector<unsigned char>(bare.begin(), bare.end())).empty(),
                "texture scan: text with under 4 bytes before it is not a slot (no read before the buffer)");
            auto two = Slot("A.DDS");
            const auto second = Slot("b.dds");
            two.insert(two.end(), second.begin(), second.end());
            a_check.Check(Scan(two) == std::vector<std::string>{ "A.DDS", "b.dds" },
                "texture scan: two slots back to back, any case, each found as stored");
            std::vector<unsigned char> afterText = { 'x', 'y', 'z' };
            const auto third = Slot("c.dds");
            afterText.insert(afterText.end(), third.begin(), third.end());
            a_check.Check(Scan(afterText) == std::vector<std::string>{ "c.dds" }, "texture scan: text before the length is not taken in");
        }
    }

    void CheckPathRule(Checker& a_check)
    {
        CheckNormalize(a_check);
        CheckScanner(a_check);
    }
}
