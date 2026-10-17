// =============================================================================
// GreySelfTestDecal - --self-test of the decal texture set (TXST) copies (GreyFormsTxst.h)
// =============================================================================
// The type table, the TXST request parts and their refusals, the record written and read back byte for byte, the map's
// TXST parts and the type kept with an id, the code page round trip, the texture step (diffuse at --tint, glow at 1, a
// texture two sets share made once per tint, grey / missing / textures off) with the decal rule, and the refusal of an
// old request against an output that holds TXST copies.

#include "GreyForms.h"
#include "GreyFormsTxst.h"
#include "GreyRisk.h"
#include "GreySelfTest.h"
#include "GreyTexture.h"

#include <DirectXTex.h>

#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <set>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr std::size_t kSide = 4;
        constexpr std::uint8_t kOrange[3] = { 255, 120, 0 };
        constexpr std::uint8_t kMidGrey = 128;
        constexpr float kTint = 0.6f;
        constexpr UINT kKoreanCodePage = 949;

        [[nodiscard]] std::filesystem::path WriteText(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream(a_file, std::ios::binary) << a_text;
            return a_file;
        }

        // A kSide square opaque R8G8B8A8 DDS, orange or mid grey
        [[nodiscard]] bool WriteSquare(const std::filesystem::path& a_file, bool a_orange)
        {
            DirectX::ScratchImage image;
            if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, kSide, kSide, 1, 1))) {
                return false;
            }
            const DirectX::Image& mip = *image.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < kSide; ++y) {
                for (std::size_t x = 0; x < kSide; ++x) {
                    auto* texel = mip.pixels + y * mip.rowPitch + x * 4;
                    for (int c = 0; c < 3; ++c) {
                        texel[c] = a_orange ? kOrange[c] : kMidGrey;
                    }
                    texel[3] = 255;
                }
            }
            std::filesystem::create_directories(a_file.parent_path());
            return SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
                DirectX::DDS_FLAGS_NONE, a_file.c_str()));
        }

        [[nodiscard]] std::string Txst(const char* a_source, const char* a_editorId, bool a_decal, const std::string& a_slots)
        {
            return fmt::format(R"({{"source":"{}","type":"TXST","editorId":"{}","bounds":[0,0,0,0,0,0],"data":{{"flags":1}},)"
                               R"("hasDecal":{},"textures":[{}]}})", a_source, a_editorId, a_decal ? "true" : "false", a_slots);
        }

        const std::string kRuneSlots = R"("Effects\\RuneFire01.dds","Effects\\RuneFire01_n.dds","","Effects\\RuneFire01.dds","","","","")";
        const std::string kScorchSlots =
            R"("ImpactDecals\\DecalFlameBurn01.dds","","ImpactDecals\\DecalFlameBurn01_m.dds","","","black.dds","","")";
        const std::string kProj =
            R"({"source":"MysticismMagic.esp|EDB0FA","type":"PROJ","editorId":"MAG_RuneFireProjectile02","model":"Magic\\Rune.nif",)"
            R"("bounds":[0,0,0,1,1,1],"data":{"speed":1}})";

        [[nodiscard]] std::string Forms(const std::string& a_forms)
        {
            return R"({"version":2,"forms":[)" + a_forms + "]}";
        }

        [[nodiscard]] const EslForm* Find(const std::vector<EslForm>& a_forms, std::uint32_t a_id)
        {
            for (const auto& form : a_forms) {
                if (form.formId == a_id) {
                    return &form;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const CopyEntry* EntryOf(const CopiesMap& a_map, const std::string& a_source)
        {
            for (const auto& entry : a_map.entries) {
                if (entry.source == a_source) {
                    return &entry;
                }
            }
            return nullptr;
        }

        [[nodiscard]] int CountRule(const RiskJob& a_job, const char* a_rule)
        {
            const auto it = a_job.perRule.find(a_rule);
            return it == a_job.perRule.end() ? 0 : it->second;
        }

        void CheckTraits(Checker& a_check)
        {
            const std::pair<CopyType, const char*> expected[] = { { CopyType::kProjectile, "PROJ" }, { CopyType::kHazard, "HAZD" },
                { CopyType::kExplosion, "EXPL" }, { CopyType::kImpact, "IPCT" }, { CopyType::kArtObject, "ARTO" },
                { CopyType::kTextureSet, "TXST" } };
            bool ok = true;
            std::set<CopyType> listed(std::begin(kCopyTypes), std::end(kCopyTypes));
            for (const auto& [type, tag] : expected) {
                const auto back = CopyTypeFromTag(tag);
                ok &= std::string(CopyTypeTag(type)) == tag && back && *back == type && listed.count(type) == 1;
            }
            a_check.Check(ok && listed.size() == std::size(expected) && !CopyTypeFromTag("GRUP") && kCopyTypes[0] == CopyType::kTextureSet,
                "types: every tag reads back as its own type (six, TXST first in the group order), an unknown tag is none");
            a_check.Check(std::string(CopyDataTag(CopyType::kTextureSet)) == "DNAM" && CopyDataSize(CopyType::kTextureSet) == 2 &&
                              CopyHasBounds(CopyType::kTextureSet) && CopyLinkOffsets(CopyType::kTextureSet).empty() &&
                              std::string(CopyDataTag(CopyType::kImpact)) == "DATA" && !CopyHasBounds(CopyType::kImpact) &&
                              CopyDataSize(CopyType::kImpact) == 24 && CopyLinkOffsets(CopyType::kProjectile).size() == 9,
                "types: TXST has DNAM (2 bytes), bounds and no links; IPCT keeps DATA 24 without bounds; PROJ its 9 links");
        }

        void CheckRecord(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto sources = LoadFormsRequest(WriteText(a_dir / "txst.json",
                Forms(kProj + "," + Txst("Skyrim.esm|06051D", "DecalRuneFire", true, kRuneSlots) + "," +
                      Txst("Skyrim.esm|02B95A", "DecalFlameBurn01", false, kScorchSlots))));
            const CopiesMap map = AssignCopyIds(sources, {});
            const auto esl = a_dir / "txst.esp";
            WriteCopiesEsl(map, sources, esl);
            EslHeader header;
            const auto back = ReadCopiesEsl(esl, header);
            const auto* runeEntry = EntryOf(map, "skyrim.esm|06051D");
            const auto* scorchEntry = EntryOf(map, "skyrim.esm|02B95A");
            const auto* rune = runeEntry ? Find(back, runeEntry->localId) : nullptr;
            const auto* scorch = scorchEntry ? Find(back, scorchEntry->localId) : nullptr;
            a_check.Check(back.size() == 3 && back[0].type == "TXST" && back[1].type == "TXST" && back[2].type == "PROJ" && rune &&
                              scorch && runeEntry->editorId == fmt::format("HoMBF_C_TXST_{:03X}", runeEntry->localId),
                "TXST record: the TXST group comes first, HoMBF_C_TXST_<id> editor ids");
            a_check.Check(rune && rune->textureMask == 0b1011 && rune->textures[0] == "Effects\\RuneFire01.dds" &&
                              rune->textures[1] == "Effects\\RuneFire01_n.dds" && rune->textures[3] == "Effects\\RuneFire01.dds" &&
                              rune->hasDodt && rune->dodt == std::string(kDodtSize, '\0') && rune->hasDnam &&
                              rune->dnam == std::string("\x01\x00", 2) && !rune->hasData && !rune->hasModel && rune->hasBounds &&
                              rune->otherSubrecords.empty(),
                "TXST record: TX00, TX01, TX03 only, a 36-byte zero DODT stub, DNAM 2 bytes, OBND, no DATA, no MODL");
            a_check.Check(scorch && scorch->textureMask == 0b100101 && !scorch->hasDodt && scorch->textures[5] == "black.dds",
                "TXST record: a source without DODT gets none; TX02 and TX05 kept as the source's");
            a_check.Check(VerifyCopiesEsl(map, sources, esl).empty(), "TXST record: VerifyCopiesEsl finds no difference");
            CopiesMap changed = map;
            for (auto& entry : changed.entries) {
                if (entry.type == CopyType::kTextureSet) {
                    entry.greyTextures[kTxstSlotDiffuse] = "heartofmagic\\blackflame\\copies\\x.dds";
                }
            }
            a_check.Check(!VerifyCopiesEsl(changed, sources, esl).empty(), "TXST record: a slot the record does not hold is found");

            const auto refuses = [&](const char* a_name, const std::string& a_form) {
                try {
                    (void)LoadFormsRequest(WriteText(a_dir / a_name, Forms(a_form)));
                } catch (const std::runtime_error&) {
                    return true;
                }
                return false;
            };
            a_check.Check(refuses("txst_model.json", R"({"source":"a.esp|000801","type":"TXST","model":"Magic\\X.nif","hasDecal":true,)"
                                                     R"("textures":["","","","","","","",""]})") &&
                              refuses("txst_seven.json", R"({"source":"a.esp|000801","type":"TXST","hasDecal":true,"textures":["","","","","","",""]})") &&
                              refuses("txst_nodecal.json", R"({"source":"a.esp|000801","type":"TXST","textures":["","","","","","","",""]})") &&
                              refuses("txst_key.json", R"({"source":"a.esp|000801","type":"TXST","hasDecal":true,"data":{"speed":1},)"
                                                       R"("textures":["","","","","","","",""]})"),
                "TXST request: a model, seven slots, no hasDecal, a DATA key are each refused");
        }

        void CheckMapAndIds(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto rune = Txst("Skyrim.esm|06051D", "DecalRuneFire", true, kRuneSlots);
            const auto sources = LoadFormsRequest(WriteText(a_dir / "ids1.json", Forms(kProj + "," + rune)));
            CopiesMap first = AssignCopyIds(sources, {});
            for (auto& entry : first.entries) {
                if (entry.type == CopyType::kTextureSet) {
                    entry.greyTextures[kTxstSlotGlow] = "heartofmagic\\blackflame\\copies\\_tint\\100\\effects\\runefire01.dds";
                }
            }
            WriteCopiesMap(first, a_dir / "ids1_map.json");
            const CopiesMap read = LoadCopiesMap(a_dir / "ids1_map.json");
            const auto* entry = EntryOf(read, "skyrim.esm|06051D");
            a_check.Check(entry && entry->type == CopyType::kTextureSet && entry->hasDecal &&
                              entry->sourceTextures[0] == "Effects\\RuneFire01.dds" && entry->greyTextures[0].empty() &&
                              entry->greyTextures[3] == "heartofmagic\\blackflame\\copies\\_tint\\100\\effects\\runefire01.dds",
                "TXST map: sourceTextures, greyTextures and hasDecal read back");
            // dropped: retired with its type; the same source back as another type is refused (Codex 8)
            const CopiesMap second = AssignCopyIds(LoadFormsRequest(WriteText(a_dir / "ids2.json", Forms(kProj))), read);
            WriteCopiesMap(second, a_dir / "ids2_map.json");
            const CopiesMap secondRead = LoadCopiesMap(a_dir / "ids2_map.json");
            const auto retiredType = secondRead.retiredTypes.find("skyrim.esm|06051D");
            a_check.Check(secondRead.retired.count("skyrim.esm|06051D") == 1 && retiredType != secondRead.retiredTypes.end() &&
                              retiredType->second == CopyType::kTextureSet,
                "TXST map: a dropped TXST is retired with its type");
            const auto typeChange = [&](const CopiesMap& a_previous, const char* a_name) {
                const std::string asImpact = R"({"source":"Skyrim.esm|06051D","type":"IPCT","editorId":"X"})";
                try {
                    (void)AssignCopyIds(LoadFormsRequest(WriteText(a_dir / a_name, Forms(asImpact))), a_previous);
                } catch (const std::runtime_error& e) {
                    return std::string(e.what()).find("TXST") != std::string::npos;
                }
                return false;
            };
            a_check.Check(typeChange(read, "ids3.json") && typeChange(secondRead, "ids4.json"),
                "copy ids: a source back as another type is refused, active or retired (Codex 8)");
        }

        void CheckCodePage(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const std::string utf8 = "Effects\\\xED\x9A\xA8\xEA\xB3\xBC.dds";  // Effects\효과.dds
            const auto ansi = Utf8ToAnsiExact(utf8);
            // 949: other bytes; 65001 (Windows' UTF-8 option): the same bytes; any other code page cannot hold it
            const bool korean = ::GetACP() == kKoreanCodePage || ::GetACP() == CP_UTF8;
            const auto back = ansi ? AnsiToUtf8Exact(*ansi) : std::nullopt;
            const bool sameBytes = ::GetACP() == CP_UTF8;
            a_check.Check(korean ? (ansi && (*ansi == utf8) == sameBytes && back && *back == utf8) : !ansi,
                fmt::format("code page {}: a Korean slot {} (reviewer 1)", ::GetACP(),
                    korean ? "goes to the code page's bytes and back to the same UTF-8" : "is refused (lossy)"));
            const auto slots = R"(")" + std::string("Effects\\\\\xED\x9A\xA8\xEA\xB3\xBC.dds") + R"(","","","","","","","")";
            const auto file = WriteText(a_dir / "cp.json", R"({"version":3,"fingerprint":"x","addons":[],"models":[],"forms":[)" +
                Txst("A.esp|000801", "K", false, slots) + "," + kProj + "]}");
            const auto request = LoadFormsRequestFile(file);
            const bool kept = request.forms.size() == (korean ? 2u : 1u) && request.warnings.size() == (korean ? 0u : 1u);
            bool mapped = true;
            if (korean && kept) {
                const CopiesMap map = AssignCopyIds(request.forms, {});
                WriteCopiesMap(map, a_dir / "cp_map.json");  // a code page byte in JSON would throw here
                const CopiesMap mapBack = LoadCopiesMap(a_dir / "cp_map.json");
                const auto* entry = EntryOf(mapBack, "a.esp|000801");
                mapped = entry && entry->sourceTextures[0] == *ansi;
            }
            a_check.Check(kept && mapped, fmt::format("code page: a game request's TXST with that slot is kept (map round trip in "
                "the code page's bytes) or left out with one warning ({} forms, {} warnings{})", request.forms.size(),
                request.warnings.size(), request.warnings.empty() ? std::string() : ": " + request.warnings.front()));
        }

        void CheckTextureStep(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto in = a_dir / "decal_in";
            const auto out = a_dir / "decal_out";
            const bool written = WriteSquare(in / "textures/effects/test/rune.dds", true) &&
                                 WriteSquare(in / "textures/effects/test/glowgrey.dds", false);
            const auto slots = [](const char* a_tx00, const char* a_tx03) {
                return fmt::format(R"("{}","effects\\test\\rune_n.dds","","{}","","","","")", a_tx00, a_tx03);
            };
            const auto request = LoadFormsRequest(WriteText(a_dir / "decal.json",
                Forms(Txst("A.esp|000801", "Rune", true, slots("Effects\\\\Test\\\\Rune.dds", "Effects\\\\Test\\\\Rune.dds")) + "," +
                      Txst("A.esp|000802", "Shared", false, slots("effects\\\\test\\\\rune.dds", "")) + "," +
                      Txst("A.esp|000803", "Grey", false, slots("", "effects\\\\test\\\\glowgrey.dds")) + "," +
                      Txst("A.esp|000804", "Missing", false, slots("effects\\\\test\\\\none.dds", "")))));
            CopiesMap map = AssignCopyIds(request, {});
            TextureJob job;
            job.inputs = { in };
            job.out = out;
            job.diffuseTint = kTint;
            TextureSource textures({ in, out }, {}, out);
            RiskJob risk;
            risk.textures = &textures;
            StageTextureSets(map, &job, risk);
            const auto* rune = EntryOf(map, "a.esp|000801");
            const auto* shared = EntryOf(map, "a.esp|000802");
            const auto* grey = EntryOf(map, "a.esp|000803");
            const auto* missing = EntryOf(map, "a.esp|000804");
            const std::string diffuse = "heartofmagic\\blackflame\\copies\\_tint\\60\\effects\\test\\rune.dds";
            const std::string glow = "heartofmagic\\blackflame\\copies\\_tint\\100\\effects\\test\\rune.dds";
            std::error_code ec;
            a_check.Check(written && rune && rune->greyTextures[0] == diffuse && rune->greyTextures[3] == glow &&
                              rune->greyTextures[1].empty() && std::filesystem::is_regular_file(out / "textures" / diffuse, ec) &&
                              std::filesystem::is_regular_file(out / "textures" / glow, ec),
                fmt::format("decal textures: TX00 at --tint and TX03 at 1 get their own copies (record text without textures\\), "
                    "TX01 kept ({} / {})", rune ? rune->greyTextures[0] : "?", rune ? rune->greyTextures[3] : "?"));
            a_check.Check(shared && shared->greyTextures[0] == diffuse && job.totals.recoloured == 2,
                fmt::format("decal textures: a texture two sets name is recoloured once per tint ({} copies)", job.totals.recoloured));
            a_check.Check(grey && grey->greyTextures[3].empty() && missing && missing->greyTextures[0].empty() &&
                              job.totals.missing == 1 && job.totals.decalSlots == 5 && job.totals.decalRepointed == 3,
                fmt::format("decal textures: a grey glow at tint 1 and a missing texture keep the source's slot ({} of {} slots "
                    "moved)", job.totals.decalRepointed, job.totals.decalSlots));
            a_check.Check(CountRule(risk, "DECAL_COLOUR") == 0 && CountRule(risk, "UNREAD") == 1,
                fmt::format("decal rule: the grey copies and the grey glow pass, the missing texture is UNREAD ({} / {})",
                    CountRule(risk, "DECAL_COLOUR"), CountRule(risk, "UNREAD")));
            const auto esl = a_dir / "decal.esp";
            WriteCopiesEsl(map, request, esl);
            a_check.Check(VerifyCopiesEsl(map, request, esl).empty(), "decal textures: the records name the grey copies (read back)");

            CopiesMap off = AssignCopyIds(request, {});
            RiskJob offRisk;
            offRisk.textures = &textures;
            StageTextureSets(off, nullptr, offRisk);
            a_check.Check(CountRule(offRisk, "DECAL_COLOUR") == 3 && !RiskGatePasses(offRisk) && EntryOf(off, "a.esp|000801") &&
                              EntryOf(off, "a.esp|000801")->greyTextures[0].empty(),
                fmt::format("decal rule: textures off leaves the orange slots and DECAL_COLOUR fails the gate ({})",
                    CountRule(offRisk, "DECAL_COLOUR")));
        }

        void CheckOldRequest(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto withTxst = a_dir / "old_out";
            const auto sources = LoadFormsRequest(WriteText(a_dir / "old.json", Forms(Txst("A.esp|000801", "R", true, kRuneSlots))));
            WriteCopiesMap(AssignCopyIds(sources, {}), withTxst / "SKSE/Plugins" / kCopiesMapName);
            const auto before = LoadCopiesMap(withTxst / "SKSE/Plugins" / kCopiesMapName);
            FormsRunOptions options;
            options.decalTextureSets = false;
            options.warnAndContinue = true;
            const auto refused = BuildForms({}, Request{}, {}, withTxst, kTint, options);
            const auto after = LoadCopiesMap(withTxst / "SKSE/Plugins" / kCopiesMapName);
            const auto empty = a_dir / "old_out_empty";
            std::filesystem::create_directories(empty);
            const auto control = BuildForms({}, Request{}, {}, empty, kTint, options);
            a_check.Check(refused.failed == 1 && after.entries.size() == before.entries.size() && control.failed == 0,
                fmt::format("an old request against an output holding TXST copies is refused before writing (Codex 1; failed {}, "
                    "control {})", refused.failed, control.failed));
        }
    }

    void CheckDecals(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto dir = a_dir / "decals";
        std::filesystem::create_directories(dir);
        CheckTraits(a_check);
        CheckRecord(a_check, dir);
        CheckMapAndIds(a_check, dir);
        CheckCodePage(a_check, dir);
        CheckTextureStep(a_check, dir);
        CheckOldRequest(a_check, dir);
    }
}
