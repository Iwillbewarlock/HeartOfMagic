// =============================================================================
// GreySelfTestGame - --self-test of the game's request (version 3), the copies map v2, the id budget,
// warn-and-continue and the per-file commit (v2 expansion C2)
// =============================================================================

#include "GreyForms.h"
#include "GreySelfTest.h"

#include <NifFile.hpp>

#include <fmt/format.h>
#include <fstream>
#include <stdexcept>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr std::uint32_t kFirst = 0x800;
        constexpr std::uint32_t kLast = 0xFFF;
        constexpr std::size_t kEslCapacity = kLast - kFirst + 1;

        std::filesystem::path Write(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream(a_file, std::ios::binary) << a_text;
            return a_file;
        }

        std::string ReadAll(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        const CopyEntry* Entry(const CopiesMap& a_map, const std::string& a_source)
        {
            for (const auto& entry : a_map.entries) {
                if (entry.source == a_source) {
                    return &entry;
                }
            }
            return nullptr;
        }

        // A version 3 request as the DLL writes it: the made-up NIF as a projectile and its muzzle, a missing model,
        // an unreadable one, an ARTO; addons 9 only (the NIF's AddOnNode 24 has no ADDN), an own record and a twice-listed
        // index; one NIF the game could not read
        std::string GameRequest()
        {
            return R"({"version":3,"game":"1.6.1170.0","fingerprint":"fnv64:00000000000000ab","copiesPlugin":"HeartOfMagic_BlackFlame_Copies.esp",
"forms":[
 {"source":"Skyrim.esm|012FCF","type":"PROJ","editorId":"","model":"magic\\test.nif","muzzleModel":"magic\\muzzle.nif","bounds":[-1,-1,-1,1,1,1],"data":{"projectileType":8,"speed":2500,"soundLevel":1},"viaSpell":"Skyrim.esm|012FCD","costliest":true},
 {"source":"Skyrim.esm|0ABCDE","type":"PROJ","editorId":"","model":"magic\\missing.nif","muzzleModel":"","bounds":[0,0,0,0,0,0],"data":{},"viaSpell":"","costliest":false},
 {"source":"Mod.esp|000801","type":"IPCT","editorId":"","model":"effects\\broken.nif","data":{"effectDuration":1},"viaSpell":"","costliest":false},
 {"source":"Mod.esp|000802","type":"IPCT","editorId":"","model":"effects\\unread.nif","data":{},"viaSpell":"","costliest":false},
 {"source":"Skyrim.esm|02ACD7","type":"ARTO","editorId":"","model":"magic\\test.nif","bounds":[-2,-2,-2,2,2,2],"data":{"artType":1},"viaSpell":"","costliest":false}],
"excluded":[{"source":"Skyrim.esm|0DE93E","type":"ACTI","reason":"placed object is not a copy type","viaSpell":""}],
"addons":[{"index":9,"model":"mps\\a.nif","editorId":"","source":"Skyrim.esm|000009","masterParticleCap":0,"dnamFlags":1,"own":false},
 {"index":9,"model":"mps\\a.nif","editorId":"","source":"Later.esp|000009","masterParticleCap":0,"dnamFlags":1,"own":false},
 {"index":9109,"model":"heartofmagic\\blackflame\\mps\\a.nif","editorId":"","source":"HeartOfMagic_BlackFlame.esp|000804","masterParticleCap":0,"dnamFlags":1,"own":true}],
"models":["effects\\broken.nif"],
"nifs":[{"path":"meshes\\magic\\test.nif","size":1,"fnv64":"01","ok":true},{"path":"meshes\\effects\\unread.nif","size":0,"fnv64":"00","ok":false,"error":"does not open"}]})";
        }

        void CheckParse(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto request = LoadFormsRequestFile(Write(a_dir / "game_request.json", GameRequest()));
            a_check.Check(request.version == 3 && request.forms.size() == 5 && request.fingerprint == "fnv64:00000000000000ab" &&
                              request.game == "1.6.1170.0" && request.excluded.size() == 1 &&
                              request.excluded[0].reason == "placed object is not a copy type",
                "game request v3: forms, fingerprint, game and excluded read");
            a_check.Check(request.forms[0].muzzleModel == "meshes\\magic\\muzzle.nif" && request.forms[0].model == "meshes\\magic\\test.nif",
                "game request v3: PROJ model and muzzle model as Data-relative meshes\\ paths");
            a_check.Check(request.addons && request.addons->addons.size() == 2 && request.addons->addons.at(9).source == "Later.esp|000009" &&
                              request.warnings.size() == 1,
                fmt::format("game request v3: ADDN index 9 twice is a warning, the later kept ({} warnings)", request.warnings.size()));
            a_check.Check(request.failedNifs.size() == 1 && request.failedNifs[0] == "meshes\\effects\\unread.nif",
                "game request v3: the NIF the game could not read is listed");
            bool refused = false;
            try {
                (void)LoadFormsRequestFile(Write(a_dir / "game_v4.json", R"({"version":4,"forms":[]})"));
            } catch (const std::runtime_error&) {
                refused = true;
            }
            a_check.Check(refused, "game request: version 4 is refused (2 and 3 only)");
        }

        void CheckMapV2(Checker& a_check, const std::filesystem::path& a_dir)
        {
            CopiesMap map;
            CopyEntry entry;
            entry.source = "skyrim.esm|012FCF";
            entry.localId = kFirst;
            entry.editorId = "HoMBF_C_PROJ_800";
            entry.sourceModel = "meshes\\magic\\a.nif";
            entry.sourceMuzzleModel = "meshes\\magic\\m.nif";
            entry.greyMuzzleModel = CopyModelFor("meshes\\magic\\m.nif");
            entry.note = "no grey copy of the model: test";
            map.entries.push_back(entry);
            map.requestFingerprint = "fnv64:1234";
            map.excluded.push_back({ "a.esp|000801", "ACTI", "why" });
            WriteCopiesMap(map, a_dir / "map_v2.json");
            const auto back = LoadCopiesMap(a_dir / "map_v2.json");
            a_check.Check(back.requestFingerprint == "fnv64:1234" && back.excluded.size() == 1 && back.entries.size() == 1 &&
                              back.entries[0].greyMuzzleModel == "meshes\\heartofmagic\\blackflame\\copies\\magic\\m.nif" &&
                              back.entries[0].note == entry.note && back.entries[0].greyModel.empty(),
                "copies map v2: fingerprint, excluded, muzzle models and note written and read back");
            a_check.Check(CopyRecordModel(back.entries[0]) == "magic\\a.nif",
                "copies map v2: a copy without its grey model names the source's model (without meshes\\)");
            a_check.Check(ReadAll(a_dir / "map_v2.json").find("\"version\": 2") != std::string::npos, "copies map: written as version 2");
        }

        void CheckIds(Checker& a_check)
        {
            std::vector<FormSource> sources(kEslCapacity + 1);
            for (std::size_t i = 0; i < sources.size(); ++i) {
                sources[i].source = fmt::format("big.esp|{:06X}", i + 1);
                sources[i].type = CopyType::kImpact;
            }
            std::string message;
            try {
                (void)AssignCopyIds(sources, {});
            } catch (const std::runtime_error& e) {
                message = e.what();
            }
            a_check.Check(message.find("2049 forms requested") != std::string::npos && message.find("0 retired") != std::string::npos &&
                              message.find("--reclaim-retired") != std::string::npos,
                "copy ids: the 2049th form is refused before anything is written, naming the counts and --reclaim-retired");

            CopiesMap previous;
            previous.retired = { { "gone.esp|000001", kFirst }, { "gone.esp|000002", kFirst + 1 }, { "gone.esp|000003", kLast } };
            std::vector<FormSource> two(2);
            two[0].source = "new.esp|000001";
            two[1].source = "new.esp|000002";
            bool threw = false;
            try {
                (void)AssignCopyIds(two, previous);
            } catch (const std::runtime_error&) {
                threw = true;
            }
            const auto reclaimed = AssignCopyIds(two, previous, true);
            a_check.Check(threw && reclaimed.entries.size() == 2 && reclaimed.entries[0].localId == kFirst &&
                              reclaimed.entries[1].localId == kFirst + 1 && reclaimed.retired.size() == 1 &&
                              reclaimed.retired.count("gone.esp|000003") == 1,
                "copy ids: with no id left above, --reclaim-retired gives retired ids lowest first (and only then)");
            std::vector<FormSource> muzzle(1);
            muzzle[0].source = "skyrim.esm|012FCF";
            muzzle[0].muzzleModel = "meshes\\magic\\m.nif";
            a_check.Check(AssignCopyIds(muzzle, {}).entries[0].greyMuzzleModel == "meshes\\heartofmagic\\blackflame\\copies\\magic\\m.nif",
                "copy ids: a PROJ muzzle model gets its grey copy path");
        }

        void CheckEmptyEsl(Checker& a_check, const std::filesystem::path& a_dir)
        {
            WriteEmptyEsl(a_dir / "empty.esp");
            EslHeader header;
            const auto forms = ReadCopiesEsl(a_dir / "empty.esp", header);
            a_check.Check(forms.empty() && (header.flags & kEslFlag) != 0 && header.masters == 0 && header.records == 0 &&
                              header.version == kHedrVersion && header.nextObjectId == kFirst,
                "empty ESL: light flag, no masters, no records, HEDR 1.70, next id 0x800");
        }

        // warn-and-continue: a missing model, an unparsable one and one the game could not read keep the source model;
        // AddOnNode 24 (no ADDN) stays; meta.ini in --out survives the commit; the local ESL is written
        void CheckRun(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
        {
            const auto in = a_dir / "game_in";
            const auto out = a_dir / "game_out";
            for (const char* rel : { "meshes/magic/test.nif", "meshes/magic/muzzle.nif", "meshes/mps/a.nif" }) {
                std::filesystem::create_directories((in / rel).parent_path());
                std::filesystem::copy_file(a_sourceNif, in / rel, std::filesystem::copy_options::overwrite_existing);
            }
            Write(in / "meshes/effects/broken.nif", "not a nif at all");
            Write(out / "meta.ini", "[General]\nmodid=0\n");
            const auto request = LoadFormsRequestFile(a_dir / "game_request.json");
            FormsRunOptions options;
            options.fingerprint = request.fingerprint;
            options.excluded = request.excluded;
            options.failedNifs = request.failedNifs;
            options.warnAndContinue = true;
            const auto totals = BuildForms(request.forms, *request.addons, { in }, out, kDefaultTint, options);
            a_check.Check(totals.failed == 0 && totals.copies == 5 && totals.warnings >= 4,
                fmt::format("warn-and-continue run: {} copies, {} warnings, {} failed (missing, broken, unread, AddOnNode 24)",
                    totals.copies, totals.warnings, totals.failed));
            const auto map = LoadCopiesMap(out / "SKSE/Plugins" / kCopiesMapName);
            const auto* missing = Entry(map, "skyrim.esm|0ABCDE");
            const auto* broken = Entry(map, "mod.esp|000801");
            const auto* unread = Entry(map, "mod.esp|000802");
            const auto* proj = Entry(map, "skyrim.esm|012FCF");
            a_check.Check(missing && missing->greyModel.empty() && !missing->note.empty() && broken && broken->greyModel.empty() &&
                              unread && unread->note.find("could not read") != std::string::npos,
                "warn-and-continue: missing, unparsable and unreadable models leave the grey path empty with a note");
            a_check.Check(proj && !proj->greyModel.empty() && !proj->greyMuzzleModel.empty() &&
                              map.requestFingerprint == "fnv64:00000000000000ab" && map.excluded.size() == 1,
                "warn-and-continue: the good PROJ has grey model and muzzle copies; the map carries fingerprint and excluded");
            EslHeader header;
            const auto forms = ReadCopiesEsl(out / kCopiesPluginName, header);
            bool namesSource = false;
            for (const auto& form : forms) {
                namesSource |= missing && form.formId == missing->localId && form.model == "magic\\missing.nif";
            }
            a_check.Check(namesSource, "warn-and-continue: the ESL record of a copy without its grey NIF names the source model");
            nifly::NifFile nif;
            LoadNif(nif, out / "meshes/heartofmagic/blackflame/copies/magic/test.nif");
            std::vector<int> values;
            const auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                    values.push_back(node->value);
                }
            }
            a_check.Check(std::find(values.begin(), values.end(), 24) != values.end() &&
                              std::find(values.begin(), values.end(), 9) == values.end(),
                "warn-and-continue: AddOnNode 9 on its grey index, 24 (no ADDN) kept as it was");
            a_check.Check(ReadAll(out / "meta.ini") == "[General]\nmodid=0\n", "commit: a file already in --out (meta.ini) is kept");
            std::error_code ec;
            a_check.Check(std::filesystem::exists(out / kLocalPluginName, ec) && !std::filesystem::exists(out.string() + ".staging", ec),
                "commit: the local ESL is written and no staging folder is left");
        }
    }

    void CheckGameRequest(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        try {
            CheckParse(a_check, a_dir);
            CheckMapV2(a_check, a_dir);
            CheckIds(a_check);
            CheckEmptyEsl(a_check, a_dir);
            CheckRun(a_check, a_dir, a_sourceNif);
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("game request: exception: {}", e.what()));
        }
    }
}
