// =============================================================================
// GreySelfTestPlan - --self-test of the request plan and the generated ESL
// =============================================================================

#include "GreySelfTest.h"

#include <fmt/format.h>
#include <fstream>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr int kIndexA = 9;
        constexpr int kIndexB = 24;
        constexpr int kShippedGreyA = 9109;  // a shipped grey record that already names a.nif's copy
        constexpr int kTakenB = 9124;        // the preferred grey index of 24, taken by another mod
        constexpr const char* kModel = "meshes\\effects\\test.nif";

        [[nodiscard]] std::filesystem::path WriteFile(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream(a_file, std::ios::binary) << a_text;
            return a_file;
        }

        [[nodiscard]] std::string RequestText(bool a_withB)
        {
            std::string addons =
                R"({"index":9,"model":"MPS\\A.nif","editorId":"MPSA","source":"Skyrim.esm|000009","dnamFlags":3},)"
                R"({"index":9109,"model":"HeartOfMagic\\BlackFlame\\MPS\\A.nif","editorId":"HoMBF_GreyAddon_MPSA","source":"HeartOfMagic_BlackFlame.esp|000804"},)"
                R"({"index":9124,"model":"Other\\Thing.nif","editorId":"OtherThing","source":"Other.esp|000800"})";
            if (a_withB) {
                addons += R"(,{"index":24,"model":"MPS/B.nif","editorId":"MPSB","source":"Skyrim.esm|000024","masterParticleCap":7})";
            }
            return R"({"version":1,"addons":[)" + addons + R"(],"models":["Effects\\Test.nif","Effects\\Missing.nif"]})";
        }

        void CheckPlan(Checker& a_check, const std::filesystem::path& a_dir, const std::vector<std::filesystem::path>& a_in,
            Plan& a_plan)
        {
            const Request request = LoadRequest(WriteFile(a_dir / "request.json", RequestText(true)));
            a_plan = PlanFromRequest(request, a_in, kDefaultTint);
            a_check.Check(a_plan.problems.empty() && a_plan.skipped.size() == 1,
                fmt::format("plan: no problems, the missing model skipped ({} problem(s), {} skipped)",
                    a_plan.problems.size(), a_plan.skipped.size()));
            a_check.Check(a_plan.map.models.count(kModel) == 1 &&
                              a_plan.map.models.at(kModel) == "meshes\\heartofmagic\\blackflame\\effects\\test.nif",
                "plan: the model's grey copy path");
            const auto a = a_plan.map.addons.find(kIndexA);
            a_check.Check(a != a_plan.map.addons.end() && a->second.grey == kShippedGreyA &&
                              a->second.greyForm == "HeartOfMagic_BlackFlame.esp|000804",
                "plan: index 9 reuses the shipped grey record 9109 that already names its copy");
            const auto b = a_plan.map.addons.find(kIndexB);
            a_check.Check(b != a_plan.map.addons.end() && b->second.grey == kShippedGreyA + 16 &&
                              b->second.greyForm == "HeartOfMagic_BlackFlame_Local.esp|000800" &&
                              b->second.greyEditorId == "HoMBF_GreyAddon_MPSB",
                fmt::format("plan: index 24, its preferred {} taken, gets the next free index above all ({})", kTakenB,
                    b == a_plan.map.addons.end() ? -1 : b->second.grey));
            a_check.Check(a_plan.newRecords.size() == 1 && a_plan.newRecords.front().masterParticleCap == 7,
                "plan: one new record, DNAM cap carried");

            const Request partial = LoadRequest(WriteFile(a_dir / "partial.json", RequestText(false)));
            const Plan missing = PlanFromRequest(partial, a_in, kDefaultTint);
            a_check.Check(missing.problems.size() == 1, "plan: an AddOnNode value with no ADDN in the request fails");
        }

        void CheckEsl(Checker& a_check, const std::filesystem::path& a_dir, const Plan& a_plan)
        {
            auto records = a_plan.newRecords;
            AddonEntry second = records.front();
            second.grey = 9300;
            second.greyEditorId = "HoMBF_GreyAddon_Second";
            second.dnamFlags = kDnamAlwaysLoaded;
            records.push_back(second);
            const auto file = a_dir / kLocalPluginName;
            WriteAddonEsl(records, file);
            std::uint32_t flags = 0;
            const auto back = ReadAddonEsl(file, flags);
            a_check.Check(flags == 0x200, fmt::format("ESL: header flags 0x{:X} (light plugin)", flags));
            const bool ok = back.size() == 2 && back[0].formId == 0x800 && back[1].formId == 0x801 &&
                            back[0].index == records[0].grey && back[1].index == 9300 &&
                            back[0].editorId == "HoMBF_GreyAddon_MPSB" &&
                            back[0].model == "heartofmagic\\blackflame\\mps\\b.nif" &&
                            back[0].masterParticleCap == 7 && back[1].dnamFlags == kDnamAlwaysLoaded;
            a_check.Check(ok, "ESL: two ADDN records read back (FormIDs, EditorIDs, MODL without meshes\\, DATA, DNAM)");
        }
    }

    void CheckPlanAndEsl(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif)
    {
        const auto input = a_dir / "in";
        std::filesystem::create_directories(input / "meshes" / "effects");
        std::filesystem::copy_file(a_sourceNif, input / "meshes" / "effects" / "test.nif",
            std::filesystem::copy_options::overwrite_existing);
        Plan plan;
        CheckPlan(a_check, a_dir, { input }, plan);
        if (!plan.newRecords.empty()) {
            CheckEsl(a_check, a_dir, plan);
        }
    }
}
