#pragma once

// =============================================================================
// COPY CONTRACT TEST EFSH BASE - the shared table's effect shader base texture cases (contract 8, CopyContractEfshBase.h)
// =============================================================================
// Run from CopyContractTest.h Run (cosave-check and the builder's --self-test). The golden key16 was computed apart from
// this code (Python FNV-1a 64 over the golden string, 2026-10-20), as the table's other anchors were.

#include "CopyContractEfshBase.h"
#include "CopyContractProgress.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace BlackFlame::CopyContractTest
{
    namespace EfshBaseDetail
    {
        namespace CC = CopyContract;

        inline constexpr const char* kGoldenBase =
            "format=BFK4|out=1+dev0123456789abcdef|tint=0.600000|tintadd=0.900000|palette=derived|grad=1111111111111111|"
            "gradadd=missing|kind=efshbase|grey=luma1|src=textures\\effects\\po3_fec\\fire\\shaders\\burnshader.dds|"
            "srchash=8888888888888888";
        inline constexpr const char* kGoldenBaseKey16 = "db303af964d389e6";
        inline constexpr std::uint64_t kAnchorBaseFnv64 = 0x8888888888888888ull;

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
            const CC::ChildKeyInput source{ "Effects/PO3_FEC/Fire/Shaders/BurnShader.dds", kAnchorBaseFnv64 };
            const auto text = CC::EfshBaseKeyString(prefix, source);
            a_check(text && *text == kGoldenBase && CC::Key16Of(*text) == kGoldenBaseKey16,
                std::string("efsh base: the golden base key string and key16, now ") + (text ? CC::Key16Of(*text) : std::string("none")));
            const auto palette = CC::EfshPaletteKeyString(prefix, source);
            a_check(text && palette && *palette != *text && !CC::EfshBaseKeyString(prefix, { "..\\x.dds", 1 }) &&
                        CC::kContractVersion >= 8,
                "efsh base: a base and a palette of one source are two keys (kind), a refused path has none; contract 8 or later");
            const auto nextRecipe = CC::EfshBaseKeyString(prefix, source, "luma2");
            a_check(text && nextRecipe && CC::KeyOf(*nextRecipe) != CC::KeyOf(*text) && CC::kEfshBaseRecipe == "luma1" &&
                        text->find("|grey=luma1|") != std::string::npos,
                "efsh base: the recipe is in the key (grey=luma1): a bumped recipe re-keys every base copy");
        }

        // The rule SlotKeys collects by and hook A swaps by (contract 8)
        template <class Check>
        void CheckRoles(Check& a_check)
        {
            CC::EfshShaderFlags membraneOnly;  // membrane on, no palette; particle stage off (a fill-only shader)
            membraneOnly.disableParticleShader = true;
            CC::EfshShaderFlags palette = membraneOnly;
            palette.greyscaleToColor = true;
            CC::EfshShaderFlags alphaOnly = membraneOnly;
            alphaOnly.greyscaleToAlpha = true;
            CC::EfshShaderFlags inactive = membraneOnly;
            inactive.disableTextureShader = true;
            a_check(CC::EfshBaseRoles(membraneOnly) == CC::EfshRole::kMembrane && CC::EfshBaseRoles(palette) == CC::EfshRole::kNone &&
                        CC::EfshBaseRoles(alphaOnly) == CC::EfshRole::kMembrane && CC::EfshBaseRoles(inactive) == CC::EfshRole::kNone,
                "efsh base roles: an active non-palette membrane gives its fill (greyscale to alpha alone too); greyscale to "
                "colour or a disabled membrane gives none");
            CC::EfshShaderFlags particleOnly;
            particleOnly.disableTextureShader = true;
            CC::EfshShaderFlags particlePalette = particleOnly;
            particlePalette.particleGreyscaleColor = true;
            CC::EfshShaderFlags particleOff = particleOnly;
            particleOff.disableParticleShader = true;
            const CC::EfshShaderFlags plain;
            a_check(CC::EfshBaseRoles(particleOnly) == CC::EfshRole::kParticle && CC::EfshBaseRoles(particlePalette) == CC::EfshRole::kNone &&
                        CC::EfshBaseRoles(particleOff) == CC::EfshRole::kNone && CC::EfshBaseRoles(plain) == CC::EfshRole::kBoth &&
                        CC::EfshPaletteRoles(particlePalette) == CC::EfshRole::kParticle,
                "efsh base roles: the particle base the same way; both stages on without palettes gives both; a stage whose "
                "palette the palette rule takes is never a base");
        }

        template <class Check>
        void CheckJsonAndDedupe(Check& a_check)
        {
            auto merged = CC::MergeEfshPalettes({ { "Effects\\Fire\\Burn.dds", CC::EfshRole::kMembrane },
                { "textures/effects/fire/burn.DDS", CC::EfshRole::kMembrane }, { "effects\\fire\\burn.dds", CC::EfshRole::kParticle },
                { "effects\\smoke.dds", CC::EfshRole::kParticle }, { "effects\\soot.dds", CC::EfshRole::kMembrane } });
            const bool deduped = merged.palettes.size() == 3 && merged.palettes[0].path == "textures\\effects\\fire\\burn.dds" &&
                                 merged.palettes[0].role == CC::EfshRole::kBoth;
            merged.palettes[0].hash = CC::FileHash::Hashed(0x1234ull);
            merged.palettes[1].hash = CC::FileHash::NotDumped(5);
            merged.palettes[2].hash = CC::FileHash::Missing();
            auto asPalettes = merged.palettes;
            CC::KeyEfshBases(merged.palettes, GoldenPrefix());
            CC::KeyEfshPalettes(asPalettes, GoldenPrefix());
            a_check(deduped && merged.palettes[0].key && asPalettes[0].key && *merged.palettes[0].key != *asPalettes[0].key &&
                        !merged.palettes[1].key && !merged.palettes[2].key,
                "efsh base dedupe: three shaders' spellings of one fill / particle base are one entry with both roles; its base "
                "key is not its palette key; only the hashed one is keyed");
            nlohmann::json root = nlohmann::json::object();
            root[CC::Request::kEfshBases] = CC::EfshBasesJson<nlohmann::json>(merged.palettes);
            const auto back = CC::ParseEfshBases(nlohmann::json::parse(root.dump()));
            bool same = back.error.empty() && back.palettes.size() == merged.palettes.size();
            for (std::size_t i = 0; same && i < back.palettes.size(); ++i) {
                same = back.palettes[i].path == merged.palettes[i].path && back.palettes[i].role == merged.palettes[i].role &&
                       back.palettes[i].hash.state == merged.palettes[i].hash.state && back.palettes[i].key == merged.palettes[i].key;
            }
            a_check(same && root[CC::Request::kEfshBases][0][CC::Request::kRole] == "both" &&
                        root[CC::Request::kEfshBases][2][CC::Request::kRole] == "membrane-fill",
                "efsh base: the writer's array reads back field for field, roles written membrane-fill / particle-base / both");
            const auto error = [&](const std::function<void(nlohmann::json&)>& a_edit) {
                auto edited = root;
                a_edit(edited);
                return CC::ParseEfshBases(edited).error;
            };
            a_check(error([](nlohmann::json& a_r) { a_r.erase(CC::Request::kEfshBases); }).find("'efshBases' must be an array (contract 8)") == 0 &&
                        !error([](nlohmann::json& a_r) { a_r[CC::Request::kEfshBases][2][CC::Request::kRole] = "membrane"; }).empty() &&
                        !error([](nlohmann::json& a_r) { a_r[CC::Request::kEfshBases].push_back(a_r[CC::Request::kEfshBases][1]); }).empty() &&
                        !error([](nlohmann::json& a_r) { a_r[CC::Request::kEfshBases][1][CC::Request::kKey] = "0000000000000001"; }).empty() &&
                        error([](nlohmann::json&) {}).empty(),
                "efsh base: the reader refuses no array, a palette role text, a path twice, a key on an entry not in the dump");
            CC::ProgressLine line;
            line.kind = CC::ProgressKind::kDds;
            line.key = *merged.palettes[0].key;
            line.path = DataPathRule::CopyPath(DataPathRule::kTexturesRoot, CC::Hex16(line.key), merged.palettes[0].path).value_or("");
            line.bytes = 64;
            line.fnv64 = 0x99ull;
            const auto written = CC::WriteProgressLine(line);
            const auto parsed = written ? CC::ParseProgressLine(written->substr(0, written->size() - 1)).line : std::nullopt;
            const auto parts = DataPathRule::ParseCopyPath(line.path);
            a_check(parsed && *parsed == line && parts && !parts->palette && parts->extension == "dds",
                "efsh base: a grey base's ok dds line at its key's copy.dds writes and parses back (not a palette path)");
        }

        template <class Check>
        void CheckPlanTiers(Check& a_check)
        {
            const auto order = CC::TexturePlanTiers({ { "effects\\c.dds" }, { "effects\\b.dds", "Effects\\C.dds" },
                { "effects\\a.dds", "EFFECTS\\B.DDS", "heartofmagic\\blackflame\\x.dds" } });
            a_check(order == std::vector<std::string>{ "textures\\effects\\c.dds", "textures\\effects\\b.dds", "textures\\effects\\a.dds" } &&
                        CC::TexturePlanOrder({ "effects\\b.dds" }, { "effects\\a.dds" }) ==
                            CC::TexturePlanTiers({ { "effects\\b.dds" }, { "effects\\a.dds" } }),
                "efsh base plan: the bases after the palettes although they sort first, a path an earlier tier has once, ours "
                "left out; two tiers are TexturePlanOrder");
        }
    }

    template <class Check>
    void CheckEfshBases(Check& a_check)
    {
        EfshBaseDetail::CheckKey(a_check);
        EfshBaseDetail::CheckRoles(a_check);
        EfshBaseDetail::CheckJsonAndDedupe(a_check);
        EfshBaseDetail::CheckPlanTiers(a_check);
    }
}
