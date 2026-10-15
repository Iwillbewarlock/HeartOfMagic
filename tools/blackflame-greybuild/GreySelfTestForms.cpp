// =============================================================================
// GreySelfTestForms - --self-test of the v2 copies (--forms): ESL layout, stable ids,
// refusals and a whole run on the made-up NIF
// =============================================================================

#include "GreyForms.h"
#include "GreySelfTest.h"

#include <NifFile.hpp>

#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <stdexcept>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr std::uint32_t kFirst = 0x800;
        constexpr float kSpeed = 2500.0f;
        constexpr float kRadius = 320.0f;
        constexpr std::uint16_t kFlameType = 0x8;
        constexpr std::uint32_t kHazardLimit = 12;

        [[nodiscard]] std::filesystem::path WriteText(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream(a_file, std::ios::binary) << a_text;
            return a_file;
        }

        template <class T>
        [[nodiscard]] T At(const std::string& a_bytes, std::size_t a_at)
        {
            T value{};
            if (a_at + sizeof(T) <= a_bytes.size()) {
                std::memcpy(&value, a_bytes.data() + a_at, sizeof(T));
            }
            return value;
        }

        [[nodiscard]] std::string FormsText(const std::string& a_forms)
        {
            return R"({"version":2,"forms":[)" + a_forms + "]}";
        }

        const std::string kProj =
            R"({"source":"Skyrim.esm|012FCF","type":"PROJ","editorId":"FlamesProjectile","model":"Magic\\Test.nif",)"
            R"("bounds":[-26,-14,-13,26,190,12],"data":{"flags":512,"projectileType":"Flame","speed":2500,"soundLevel":1}})";
        const std::string kHazd =
            R"({"source":"Skyrim.esm|03E075","type":"HAZD","editorId":"FireLgLongHazard","bounds":[-64,-64,0,64,64,8],)"
            R"("data":{"limit":12,"radius":3,"lifetime":7,"flags":4}})";
        const std::string kExpl =
            R"({"source":"Skyrim.esm|0439C0","type":"EXPL","editorId":"FireBallExp01","bounds":[-57,-60,-9,57,60,33],)"
            R"("data":{"force":1,"radius":320,"flags":65,"soundLevel":1}})";
        const std::string kIpct =
            R"({"source":"Skyrim.esm|06051C","type":"IPCT","editorId":"MAG_RuneFireImpact",)"
            R"("data":{"effectDuration":1,"angleThreshold":80,"flags":1,"soundLevel":2}})";
        const std::string kIpct2 = R"({"source":"Other.esp|000D66","type":"IPCT","editorId":"Two"})";
        const std::string kIpct3 = R"({"source":"Other.esp|000D67","type":"IPCT","editorId":"Three"})";

        [[nodiscard]] const EslForm* Find(const std::vector<EslForm>& a_forms, std::uint32_t a_id)
        {
            for (const auto& form : a_forms) {
                if (form.formId == a_id) {
                    return &form;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool LinksZero(const EslForm& a_form, CopyType a_type)
        {
            for (const auto offset : CopyLinkOffsets(a_type)) {
                if (At<std::uint32_t>(a_form.data, offset) != 0) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::uint32_t IdOf(const CopiesMap& a_map, const std::string& a_source)
        {
            for (const auto& entry : a_map.entries) {
                if (entry.source == a_source) {
                    return entry.localId;
                }
            }
            return 0;
        }

        void CheckEsl(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto sources = LoadFormsRequest(WriteText(a_dir / "forms1.json", FormsText(kProj + "," + kHazd + "," +
                                                                                       kExpl + "," + kIpct)));
            const CopiesMap map = AssignCopyIds(sources, {});
            a_check.Check(map.entries.size() == 4 && map.entries[0].localId == kFirst && map.entries[3].localId == kFirst + 3,
                "forms: first run gives ids 0x800.. in request order");
            const auto esl = a_dir / "copies.esp";
            WriteCopiesEsl(map, sources, esl);
            EslHeader header;
            const auto forms = ReadCopiesEsl(esl, header);
            a_check.Check((header.flags & kEslFlag) != 0 && header.masters == 0 && header.version == kHedrVersion,
                fmt::format("forms ESL: light flag, no masters, HEDR {} (flags 0x{:X}, masters {})", header.version,
                    header.flags, header.masters));
            a_check.Check(header.records == 8 && header.nextObjectId == kFirst + 4,
                fmt::format("forms ESL: HEDR count {} (4 records + 4 groups), next id {:X}", header.records,
                    header.nextObjectId));
            a_check.Check(forms.size() == 4 && forms[0].type == "PROJ" && forms[1].type == "HAZD" && forms[2].type == "EXPL" &&
                              forms[3].type == "IPCT",
                "forms ESL: groups in Skyrim.esm order PROJ, HAZD, EXPL, IPCT");
            const auto* proj = Find(forms, map.entries[0].localId);
            const auto* hazd = Find(forms, map.entries[1].localId);
            const auto* expl = Find(forms, map.entries[2].localId);
            const auto* ipct = Find(forms, map.entries[3].localId);
            a_check.Check(proj && hazd && expl && ipct, "forms ESL: every copy read back by its FormID");
            if (!proj || !hazd || !expl || !ipct) {
                return;
            }
            a_check.Check(proj->data.size() == 92 && hazd->data.size() == 40 && expl->data.size() == 52 &&
                              ipct->data.size() == 24,
                fmt::format("forms ESL: DATA sizes PROJ {} HAZD {} EXPL {} IPCT {} (92/40/52/24 as Skyrim.esm)",
                    proj->data.size(), hazd->data.size(), expl->data.size(), ipct->data.size()));
            a_check.Check(LinksZero(*proj, CopyType::kProjectile) && LinksZero(*hazd, CopyType::kHazard) &&
                              LinksZero(*expl, CopyType::kExplosion),
                "forms ESL: every form link slot is 0 (no masters)");
            a_check.Check(At<std::uint16_t>(proj->data, 0) == 512 && At<std::uint16_t>(proj->data, 2) == kFlameType &&
                              At<float>(proj->data, 8) == kSpeed && proj->hasSoundLevel && proj->soundLevel == 1,
                "forms ESL: PROJ flags 512, type Flame (8), speed 2500, VNAM sound level 1");
            a_check.Check(At<float>(expl->data, 0x20) == kRadius && At<std::uint32_t>(expl->data, 0x2C) == 65 &&
                              At<std::uint32_t>(expl->data, 0x30) == 1,
                "forms ESL: EXPL radius 320 @0x20, flags 65 @0x2C, sound level 1 @0x30");
            a_check.Check(At<std::uint32_t>(hazd->data, 0) == kHazardLimit && At<std::uint32_t>(hazd->data, 0x14) == 4,
                "forms ESL: HAZD limit 12 @0, flags 4 @0x14");
            a_check.Check(At<std::uint8_t>(ipct->data, 0x14) == 1 && At<std::uint32_t>(ipct->data, 0x10) == 2 &&
                              !ipct->hasBounds && !ipct->hasSoundLevel,
                "forms ESL: IPCT flags u8 1 @0x14, sound level 2 @0x10, no OBND, no VNAM");
            a_check.Check(proj->hasBounds && proj->bounds[4] == 190 && proj->model == "heartofmagic\\blackflame\\copies\\magic\\test.nif" &&
                              proj->editorId == "HoMBF_Copy_FlamesProjectile_012FCF",
                fmt::format("forms ESL: PROJ OBND, MODL '{}', EDID '{}'", proj->model, proj->editorId));
            a_check.Check(VerifyCopiesEsl(map, sources, esl).empty(), "forms ESL: VerifyCopiesEsl finds no difference");

            // Stable ids: HAZD dropped (retired), a new IPCT after every id; then HAZD back gets its old id
            const auto second = LoadFormsRequest(WriteText(a_dir / "forms2.json", FormsText(kProj + "," + kExpl + "," +
                                                                                       kIpct + "," + kIpct2)));
            WriteCopiesMap(map, a_dir / "map1.json");
            const CopiesMap map2 = AssignCopyIds(second, LoadCopiesMap(a_dir / "map1.json"));
            a_check.Check(IdOf(map2, "skyrim.esm|012FCF") == kFirst && IdOf(map2, "skyrim.esm|0439C0") == kFirst + 2 &&
                              IdOf(map2, "other.esp|000D66") == kFirst + 4 && map2.retired.count("skyrim.esm|03E075") == 1 &&
                              map2.retired.at("skyrim.esm|03E075") == kFirst + 1,
                "forms ids: kept ids stay, a new source takes the id after all, a dropped one is retired");
            WriteCopiesMap(map2, a_dir / "map2.json");
            const auto third = LoadFormsRequest(WriteText(a_dir / "forms3.json", FormsText(kHazd + "," + kIpct3)));
            const CopiesMap map3 = AssignCopyIds(third, LoadCopiesMap(a_dir / "map2.json"));
            a_check.Check(IdOf(map3, "skyrim.esm|03E075") == kFirst + 1 && IdOf(map3, "other.esp|000D67") == kFirst + 5,
                "forms ids: a retired source requested again gets its old id; a retired id is not given to another");
            a_check.Check(map3.retired.size() == 4, fmt::format("forms ids: the other four are retired ({})", map3.retired.size()));
        }

        // Each malformed request must throw, not write anything
        void CheckRefusals(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto refuses = [&](const char* a_name, const std::string& a_forms) {
                try {
                    (void)LoadFormsRequest(WriteText(a_dir / a_name, FormsText(a_forms)));
                    return false;
                } catch (const std::runtime_error&) {
                    return true;
                }
            };
            a_check.Check(refuses("bad_type.json", R"({"source":"A.esp|000801","type":"IPDS"})"),
                "forms request: type IPDS is refused (no IPDS copies)");
            a_check.Check(refuses("bad_key.json", R"({"source":"A.esp|000801","type":"PROJ","data":{"explosion":5}})"),
                "forms request: a link or unknown data key is refused");
            a_check.Check(refuses("bad_bounds.json", R"({"source":"A.esp|000801","type":"IPCT","bounds":[0,0,0,0,0,0]})"),
                "forms request: bounds on an IPCT are refused");
            a_check.Check(refuses("dup.json", R"({"source":"A.esp|000801","type":"IPCT"},{"source":"a.ESP|801","type":"IPCT"})"),
                "forms request: the same source twice (case and padding ignored) is refused");
            a_check.Check(refuses("bad_id.json", R"({"source":"A.esp|1000000","type":"IPCT"})"),
                "forms request: a local id over 6 hex digits is refused");
            a_check.Check(refuses("empty.json", ""), "forms request: no forms is refused");
            bool threw = false;
            try {
                const auto sources = LoadFormsRequest(
                    WriteText(a_dir / "frac.json", FormsText(R"({"source":"A.esp|000801","type":"HAZD","data":{"limit":1.5}})")));
                WriteCopiesEsl(AssignCopyIds(sources, {}), sources, a_dir / "frac.esp");
            } catch (const std::runtime_error&) {
                threw = true;
            }
            a_check.Check(threw, "forms ESL: a fractional integer field (HAZD limit 1.5) is refused");

            // V16: a source once across copies and retired
            const auto mapRefuses = [&](const char* a_name, const std::string& a_text) {
                try {
                    (void)LoadCopiesMap(WriteText(a_dir / a_name, a_text));
                    return false;
                } catch (const std::runtime_error&) {
                    return true;
                }
            };
            a_check.Check(mapRefuses("map_dup_retired.json",
                              R"({"version":1,"copies":[{"source":"A.esp|000801","type":"IPCT","copy":"000800"}],)"
                              R"("retired":[{"source":"a.esp|801","copy":"000801"}]})"),
                "copies map: a source both copied and retired is refused");
            a_check.Check(mapRefuses("map_dup_copies.json",
                              R"({"version":1,"copies":[{"source":"A.esp|000801","type":"IPCT","copy":"000800"},)"
                              R"({"source":"A.esp|000801","type":"IPCT","copy":"000801"}]})"),
                "copies map: a source copied twice is refused");
            const auto retiredHigh = LoadCopiesMap(WriteText(a_dir / "map_high.json",
                R"({"version":1,"copies":[{"source":"A.esp|000801","type":"IPCT","copy":"000800"}],)"
                R"("retired":[{"source":"B.esp|000802","copy":"000900"}]})"));
            const auto next = AssignCopyIds(LoadFormsRequest(WriteText(a_dir / "forms_high.json", FormsText(kIpct3))), retiredHigh);
            a_check.Check(next.entries.size() == 1 && next.entries[0].localId == 0x901,
                "copy ids: a new source takes the id after the highest retired id (0x901)");
        }

        // A whole --forms run: the made-up NIF as a projectile model, its AddOnNodes 9 and 24 on grey indices
        void CheckRun(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto in = a_dir / "forms_in";
            const auto out = a_dir / "forms_out";
            for (const char* rel : { "meshes/magic/test.nif", "meshes/mps/a.nif", "meshes/mps/b.nif" }) {
                std::filesystem::create_directories((in / rel).parent_path());
                std::filesystem::copy_file(a_sourceNif, in / rel, std::filesystem::copy_options::overwrite_existing);
            }
            const auto request = LoadRequest(WriteText(a_dir / "forms_addons.json",
                R"({"version":1,"addons":[{"index":9,"model":"MPS\\A.nif","source":"Skyrim.esm|000009"},)"
                R"({"index":24,"model":"MPS\\B.nif","source":"Skyrim.esm|000024"}],"models":[]})"));
            const auto sources = LoadFormsRequest(WriteText(a_dir / "forms_run.json", FormsText(kProj)));
            const FormsTotals totals = BuildForms(sources, request, { in }, out, kDefaultTint);
            a_check.Check(totals.copies == 1 && totals.nifsBuilt == 1 && totals.failed == 0,
                fmt::format("forms run: {} copies, {} copy NIFs, {} failed", totals.copies, totals.nifsBuilt, totals.failed));
            const auto copyNif = out / "meshes/heartofmagic/blackflame/copies/magic/test.nif";
            const GreyMap map = LoadMap(out / "SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json");
            nifly::NifFile nif;
            LoadNif(nif, copyNif);
            a_check.Check(VerifyGrey(nif, true, map).empty(),
                "forms run: the copy NIF has every effect shader grey and every AddOnNode on a grey index");
            a_check.Check(map.models.empty() && map.addons.size() == 2,
                fmt::format("forms run: the grey map has the 2 addons and no remapped model of the copy ({} models)",
                    map.models.size()));
            const CopiesMap first = LoadCopiesMap(out / "SKSE/Plugins" / kCopiesMapName);
            const FormsTotals again = BuildForms(sources, request, { in }, out, kDefaultTint);
            const CopiesMap second = LoadCopiesMap(out / "SKSE/Plugins" / kCopiesMapName);
            a_check.Check(again.failed == 0 && first.entries.size() == 1 && second.entries.size() == 1 &&
                              first.entries[0].localId == second.entries[0].localId,
                "forms run: a second run into the same folder keeps the copy's id");

            // W7: --out with a trailing separator stages beside it, not inside it
            const FormsTotals slash = BuildForms(sources, request, { in }, out / "", kDefaultTint);
            std::error_code slashEc;
            a_check.Check(slash.failed == 0 && slash.copies == 1 && !std::filesystem::exists(out / ".staging", slashEc),
                "forms run: --out with a trailing separator works and stages beside it (no <out>\\.staging inside)");

            // V15: a failing run leaves the previous outputs as they were
            const auto mapFile = out / "SKSE/Plugins" / kCopiesMapName;
            const auto eslFile = out / kCopiesPluginName;
            const auto readAll = [](const std::filesystem::path& a_file) {
                std::ifstream in(a_file, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            };
            const std::string mapBefore = readAll(mapFile);
            const std::string eslBefore = readAll(eslFile);
            const auto broken = LoadFormsRequest(WriteText(a_dir / "forms_broken.json", FormsText(kProj + "," +
                R"({"source":"Skyrim.esm|0ABCDE","type":"PROJ","model":"Magic\\Missing.nif"})")));
            const FormsTotals failedRun = BuildForms(broken, request, { in }, out, kDefaultTint);
            std::error_code ec;
            a_check.Check(failedRun.failed > 0 && readAll(mapFile) == mapBefore && readAll(eslFile) == eslBefore &&
                              !std::filesystem::exists(out.string() + ".staging", ec),
                "forms run: a run with a missing model fails and leaves the previous ESL and map byte for byte, no staging left");
        }
    }

    void CheckForms(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        try {
            CheckEsl(a_check, a_dir);
            CheckRefusals(a_check, a_dir);
            CheckRun(a_check, a_dir, a_sourceNif);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("forms: exception: {}", e.what()));
        }
    }
}
