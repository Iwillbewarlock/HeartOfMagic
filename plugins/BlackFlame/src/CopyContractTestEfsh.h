#pragma once

// =============================================================================
// COPY CONTRACT TEST EFSH - the shared table's effect shader palette cases (contract 6, CopyContractEfsh.h)
// =============================================================================
// Run from CopyContractTest.h Run (cosave-check and the builder's --self-test). The golden key16 was computed apart from
// this code (Python FNV-1a over the golden string, 2026-10-20), as the table's other anchors were.

#include "CopyContractEfsh.h"
#include "CopyContractProgress.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace BlackFlame::CopyContractTest
{
    namespace EfshDetail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kGoldenEfsh =
            "format=BFK4|out=1+dev0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=efshpalette|grey=luma|src=textures\\effects\\gradients\\gradfire01.dds|srchash=8888888888888888";
        inline constexpr const char* kGoldenEfshKey16 = "dde9ce47abf45a80";
        inline constexpr std::uint64_t kAnchorEfshFnv64 = 0x8888888888888888ull;

        [[nodiscard]] inline CC::KeyPrefix GoldenPrefix()
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = "1+dev0123456789abcdef";
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.paletteMode = CC::PaletteMode::kDerived;
            prefix.gradBlackFlame = CC::FileHash::Hashed(0x1111111111111111ull);
            prefix.gradBlackFlameAdd = CC::FileHash::Missing();
            return prefix;
        }

        template <class Check>
        void CheckKey(Check& a_check)
        {
            const auto prefix = GoldenPrefix();
            const CC::ChildKeyInput source{ "Effects/Gradients/GradFire01.dds", kAnchorEfshFnv64 };
            const auto text = CC::EfshPaletteKeyString(prefix, source);
            a_check(text && *text == kGoldenEfsh && CC::Key16Of(*text) == kGoldenEfshKey16,
                std::string("efsh: the golden palette key string and key16, now ") + (text ? CC::Key16Of(*text) : std::string("none")));
            auto other = source;
            other.sourceFnv64 = kAnchorEfshFnv64 + 1;
            const auto add = CC::PaletteKeyString(prefix, CC::PaletteBlend::kAdd, source);
            a_check(text && add && CC::EfshPaletteKeyString(prefix, other) != text && *add != *text &&
                        !CC::EfshPaletteKeyString(prefix, { "..\\x.dds", 1 }),
                "efsh: the source bytes re-key, a NIF palette child of the same source is another key, a refused path has none");
        }

        template <class Check>
        void CheckMerge(Check& a_check)
        {
            const auto merged = CC::MergeEfshPalettes({ { "Effects\\B.dds", CC::EfshRole::kMembrane }, { "effects/a.dds", CC::EfshRole::kParticle },
                { "TEXTURES\\Effects\\A.DDS", CC::EfshRole::kMembrane }, { "", CC::EfshRole::kParticle },
                { "HeartOfMagic\\BlackFlame\\x.dds", CC::EfshRole::kParticle }, { "C:\\x.dds", CC::EfshRole::kParticle },
                { "effects\\\xE9t\xE9.dds", CC::EfshRole::kParticle } });
            const auto& p = merged.palettes;
            a_check(p.size() == 2 && p[0].path == "textures\\effects\\a.dds" && p[0].role == CC::EfshRole::kBoth &&
                        p[1].path == "textures\\effects\\b.dds" && p[1].role == CC::EfshRole::kMembrane && merged.own == 1 &&
                        merged.refused == 1 && merged.nonAscii == 1 && !p[0].key,
                "efsh: merge folds case, separators and textures\\, joins the roles, sorts, and counts ours, the refused and the "
                "non-ASCII path (an empty text skipped)");
        }

        template <class Check>
        void CheckJson(Check& a_check)
        {
            auto merged = CC::MergeEfshPalettes({ { "effects\\a.dds", CC::EfshRole::kParticle }, { "effects\\b.dds", CC::EfshRole::kBoth },
                { "effects\\c.dds", CC::EfshRole::kMembrane } });
            merged.palettes[0].hash = CC::FileHash::Hashed(0x1234ull);
            merged.palettes[1].hash = CC::FileHash::NotDumped(99);
            merged.palettes[2].hash = CC::FileHash::Missing();
            CC::KeyEfshPalettes(merged.palettes, GoldenPrefix());
            nlohmann::json root = nlohmann::json::object();
            root[CC::Request::kEfshPalettes] = CC::EfshPalettesJson<nlohmann::json>(merged.palettes);
            const auto back = CC::ParseEfshPalettes(nlohmann::json::parse(root.dump()));
            bool same = back.error.empty() && back.palettes.size() == merged.palettes.size();
            for (std::size_t i = 0; same && i < back.palettes.size(); ++i) {
                same = back.palettes[i].path == merged.palettes[i].path && back.palettes[i].role == merged.palettes[i].role &&
                       back.palettes[i].hash.state == merged.palettes[i].hash.state && back.palettes[i].key == merged.palettes[i].key;
            }
            a_check(same && merged.palettes[0].key && !merged.palettes[1].key && !merged.palettes[2].key &&
                        root[CC::Request::kEfshPalettes][1][CC::Request::kTextureState] == "notdumped",
                "efsh: the writer's array reads back field for field (only the hashed palette has a key)");
            const auto refused = [&](const std::function<void(nlohmann::json&)>& a_edit) {
                auto edited = root;
                a_edit(edited);
                return !CC::ParseEfshPalettes(edited).error.empty();
            };
            a_check(refused([](nlohmann::json& a_r) { a_r.erase(CC::Request::kEfshPalettes); }) &&
                        refused([](nlohmann::json& a_r) { a_r[CC::Request::kEfshPalettes][0][CC::Request::kTextureState] = "unread"; }) &&
                        refused([](nlohmann::json& a_r) { a_r[CC::Request::kEfshPalettes][1][CC::Request::kKey] = "0000000000000001"; }) &&
                        refused([](nlohmann::json& a_r) { a_r[CC::Request::kEfshPalettes][0][CC::Request::kKey] = "ABCDEF0123456789"; }) &&
                        !refused([](nlohmann::json&) {}),
                "efsh: the reader refuses no array, state unread, a key on a palette not in the dump, an upper-case key");
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = *merged.palettes[0].key;
            line.path = DataPathRule::PaletteCopyPath(CC::Hex16(line.key)).value_or("");
            line.bytes = 64;
            line.fnv64 = 0x99ull;
            const auto written = CC::WriteProgressLine(line);
            const auto parsed = written ? CC::ParseProgressLine(written->substr(0, written->size() - 1)).line : std::nullopt;
            a_check(parsed && *parsed == line, "efsh: a grey palette's ok dds line at its key's palette.dds writes and parses back");
        }
    }

    namespace EfshDetail
    {
        // The flags -> roles rule SlotKeys reads the record texts by (review round 1)
        template <class Check>
        void CheckRoles(Check& a_check)
        {
            CC::EfshShaderFlags particle;
            particle.particleGreyscaleColor = true;
            CC::EfshShaderFlags both;
            both.particleGreyscaleAlpha = true;
            both.greyscaleToColor = true;
            both.greyscaleToAlpha = true;
            a_check(CC::EfshPaletteRoles(particle) == CC::EfshRole::kParticle && CC::EfshPaletteRoles(both) == CC::EfshRole::kBoth,
                "efsh roles: particle greyscale colour alone gives the particle palette; particle alpha with membrane colour and "
                "alpha gives both");
            auto offParticle = both;
            offParticle.disableParticleShader = true;
            auto offMembrane = both;
            offMembrane.disableTextureShader = true;
            CC::EfshShaderFlags disabledOnly;
            disabledOnly.disableParticleShader = true;
            disabledOnly.particleGreyscaleColor = true;
            a_check(CC::EfshPaletteRoles(offParticle) == CC::EfshRole::kMembrane && CC::EfshPaletteRoles(offMembrane) == CC::EfshRole::kParticle &&
                        CC::EfshPaletteRoles(disabledOnly) == CC::EfshRole::kNone,
                "efsh roles: a disabled stage's palette is never read, however its greyscale flags are set");
            const CC::EfshShaderFlags plain;
            CC::EfshShaderFlags disabledNoGrey;
            disabledNoGrey.disableParticleShader = true;
            disabledNoGrey.disableTextureShader = true;
            a_check(CC::EfshPaletteRoles(plain) == CC::EfshRole::kNone && CC::EfshPaletteRoles(disabledNoGrey) == CC::EfshRole::kNone &&
                        CC::EfshRoleHas(CC::EfshRole::kBoth, CC::EfshRole::kMembrane) && !CC::EfshRoleHas(CC::EfshRole::kParticle, CC::EfshRole::kMembrane) &&
                        !CC::EfshRoleHas(CC::EfshRole::kBoth, CC::EfshRole::kNone) &&
                        CC::MergeEfshPalettes({ { "effects\\x.dds", CC::EfshRole::kNone } }).palettes.empty(),
                "efsh roles: no greyscale flag reads no palette; a role-less source is no entry");
        }

        // PlanTextures' order: the palettes after the NIF and TXST textures, none twice (review round 1)
        template <class Check>
        void CheckPlanOrder(Check& a_check)
        {
            const auto order = CC::TexturePlanOrder({ "Effects\\C.dds", "textures/effects/a.dds", "effects\\A.DDS",
                                                        "HeartOfMagic\\BlackFlame\\GradBlackFlame.dds" },
                { "Effects/C.dds", "effects\\b.dds", "EFFECTS\\B.DDS", "heartofmagic\\blackflame\\x.dds", "C:\\x.dds", "" });
            const std::vector<std::string> expected{ "textures\\effects\\a.dds", "textures\\effects\\c.dds", "textures\\effects\\b.dds" };
            a_check(order == expected,
                "efsh plan order: the NIF and TXST textures sorted and once, then the palettes not among them (b after c although "
                "it sorts first); ours, refused and empty texts left out on both sides");
            const auto lateOnly = CC::TexturePlanOrder({}, { "effects\\b.dds", "effects\\a.dds" });
            const auto firstOnly = CC::TexturePlanOrder({ "effects\\a.dds" }, { "Effects\\A.dds" });
            a_check(lateOnly == std::vector<std::string>{ "textures\\effects\\a.dds", "textures\\effects\\b.dds" } &&
                        firstOnly == std::vector<std::string>{ "textures\\effects\\a.dds" },
                "efsh plan order: palettes alone are sorted; a palette a NIF already names stays where the NIF put it");
        }

        // The DLL's guard: an entry the writer would refuse is written as missing, never the whole request lost (review round 1)
        template <class Check>
        void CheckDemote(Check& a_check)
        {
            std::vector<CC::EfshPalette> palettes{ { "textures\\a.dds", CC::EfshRole::kParticle, CC::FileHash::Hashed(1), std::nullopt },
                { "textures\\b.dds", CC::EfshRole::kMembrane, {}, std::nullopt },
                { "textures\\c.dds", CC::EfshRole::kNone, CC::FileHash::Missing(), std::nullopt },
                { "textures\\d.dds", CC::EfshRole::kBoth, CC::FileHash::Hashed(2), 0x42ull },
                { "textures\\e.dds", CC::EfshRole::kParticle, CC::FileHash::NotDumped(7), 0x43ull } };
            const bool faults = CC::EfshEntryFault(palettes[0]) && CC::EfshEntryFault(palettes[1]) && CC::EfshEntryFault(palettes[2]) &&
                                !CC::EfshEntryFault(palettes[3]) && CC::EfshEntryFault(palettes[4]);
            bool threw = false;
            try {
                (void)CC::EfshPalettesJson<nlohmann::json>(palettes);
            } catch (const std::runtime_error&) {
                threw = true;
            }
            const auto notes = CC::DemoteUnwritableEfsh(palettes);
            const bool demoted = palettes.size() == 4 && notes.size() == 4 && palettes[0].hash.state == CC::FileState::kMissing &&
                                 !palettes[0].key && palettes[1].hash.state == CC::FileState::kMissing && palettes[2].path == "textures\\d.dds" &&
                                 palettes[2].key == 0x42ull && palettes[3].hash.state == CC::FileState::kMissing && !palettes[3].key &&
                                 std::none_of(palettes.begin(), palettes.end(), [](const CC::EfshPalette& a_p) { return a_p.path == "textures\\c.dds"; });
            bool written = false;
            try {
                const auto list = CC::EfshPalettesJson<nlohmann::json>(palettes);
                written = list.size() == 4 && list[0][CC::Request::kTextureState] == "missing" && !list[0].contains(CC::Request::kKey);
            } catch (const std::runtime_error&) {
                written = false;
            }
            a_check(faults && threw && demoted && written,
                "efsh guard: hashed without a key, an unset hash and a key on a not-dumped one are written as missing, a role-less "
                "entry left out, a sound one kept; the writer then takes the list it refused before");
        }
    }

    template <class Check>
    void CheckEfsh(Check& a_check)
    {
        EfshDetail::CheckKey(a_check);
        EfshDetail::CheckMerge(a_check);
        EfshDetail::CheckJson(a_check);
        EfshDetail::CheckRoles(a_check);
        EfshDetail::CheckPlanOrder(a_check);
        EfshDetail::CheckDemote(a_check);
    }
}
