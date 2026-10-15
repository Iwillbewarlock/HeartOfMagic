// =============================================================================
// GreyFormsRun - the whole --forms run (GreyForms.h)
// =============================================================================
// Nothing is written into --out until every output is written and verified in a staging folder
// beside it (<out>.staging): ids assigned, record bytes built and validated, NIFs, ESLs and maps
// read back. Only then are the staged files moved into --out (each replacing its old copy). A
// failure before that leaves the previous outputs as they were and removes the staging folder.

#include "GreyForms.h"

#include <fmt/format.h>
#include <iostream>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr const char* kStagingSuffix = ".staging";
        constexpr const char* kGreyMapName = "HeartOfMagic_BlackFlame_greymap.json";

        [[nodiscard]] std::filesystem::path PluginsDir(const std::filesystem::path& a_root)
        {
            return a_root / "SKSE" / "Plugins";
        }

        // The copies map an earlier run left in --out (stable ids), or an empty one
        [[nodiscard]] CopiesMap PreviousCopies(const std::filesystem::path& a_out)
        {
            const auto file = PluginsDir(a_out) / kCopiesMapName;
            std::error_code ec;
            if (!std::filesystem::exists(file, ec)) {
                return {};
            }
            std::cout << "ids from the earlier run: " << file.string() << "\n";
            return LoadCopiesMap(file);
        }

        [[nodiscard]] std::vector<std::string> CopyModels(const std::vector<FormSource>& a_sources)
        {
            std::vector<std::string> models;
            std::set<std::string> seen;
            for (const auto& form : a_sources) {
                if (!form.model.empty() && seen.insert(form.model).second) {
                    models.push_back(form.model);
                }
            }
            return models;
        }

        void RemoveAll(const std::filesystem::path& a_dir)
        {
            std::error_code ec;
            std::filesystem::remove_all(a_dir, ec);
            if (ec) {
                std::cout << fmt::format("note: could not remove {} ({})\n", a_dir.string(), ec.message());
            }
        }

        // Moves every staged file into --out, replacing old copies; false (printed) on the first failure
        [[nodiscard]] bool Commit(const std::filesystem::path& a_staging, const std::filesystem::path& a_out)
        {
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(a_staging, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file()) {
                    continue;
                }
                const auto target = a_out / std::filesystem::relative(it->path(), a_staging);
                std::filesystem::create_directories(target.parent_path(), ec);
                if (!ec) {
                    std::filesystem::rename(it->path(), target, ec);
                }
                if (ec) {
                    std::cout << fmt::format("FAIL  moving {} into place failed ({}): --out may hold a mix of old and new "
                                             "files; run again\n", target.string(), ec.message());
                    return false;
                }
            }
            if (ec) {
                std::cout << fmt::format("FAIL  reading the staging folder failed ({})\n", ec.message());
                return false;
            }
            return true;
        }

        // Everything into a_staging; false on a failure (counted in a_totals)
        [[nodiscard]] bool Stage(const std::vector<FormSource>& a_sources, const Plan& a_plan, const CopiesMap& a_copies,
            const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_staging, FormsTotals& a_totals)
        {
            // 1. Record bytes first: every value is validated before a single NIF is built
            const auto esl = a_staging / kCopiesPluginName;
            WriteCopiesEsl(a_copies, a_sources, esl);
            const auto problems = VerifyCopiesEsl(a_copies, a_sources, esl);
            for (const auto& problem : problems) {
                std::cout << "FAIL  " << kCopiesPluginName << ": " << problem << "\n";
            }
            if (!problems.empty()) {
                a_totals.failed += static_cast<int>(problems.size());
                return false;
            }
            // 2. The grey MPS and impact copies of the plan, then the full-grey copy of every distinct source model
            const Totals mps = BuildFromMap(a_plan.map, a_inputs, a_staging, true);
            std::cout << fmt::format("grey MPS and impact copies: built {}, skipped {}, failed {}\n", mps.built, mps.skipped,
                mps.failed);
            if (mps.failed != 0) {
                a_totals.failed += mps.failed;
                return false;
            }
            Totals nifs;
            for (const auto& model : CopyModels(a_sources)) {
                if (BuildNif(a_plan.map, a_inputs, a_staging, model, CopyModelFor(model), true, true, nifs)) {
                    ++a_totals.nifsBuilt;
                }
            }
            if (nifs.failed != 0) {
                a_totals.failed += nifs.failed;
                return false;
            }
            // 3. The local ADDN ESL (when needed), the grey map and the copies map
            if (!a_plan.newRecords.empty() && !SyncLocalEsl(a_plan.newRecords, a_staging / kLocalPluginName)) {
                ++a_totals.failed;
                return false;
            }
            WriteMap(a_plan.map, PluginsDir(a_staging) / kGreyMapName,
                { "Generated by blackflame-greybuild --forms for one setup (plugins/BlackFlame/README.md, \"Black Flame v2\").",
                    "It replaces the shipped map when this output folder is a mod placed after Heart of Magic - Black Flame." });
            WriteCopiesMap(a_copies, PluginsDir(a_staging) / kCopiesMapName);
            (void)LoadCopiesMap(PluginsDir(a_staging) / kCopiesMapName);  // read back: throws on a bad write
            return true;
        }
    }

    FormsTotals BuildForms(const std::vector<FormSource>& a_sources, const Request& a_addons,
        const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_out, float a_tint)
    {
        FormsTotals totals;
        const std::filesystem::path staging = a_out.string() + kStagingSuffix;
        for (const auto& input : a_inputs) {
            if (IsInside(staging, input) || IsInside(input, staging)) {
                std::cout << fmt::format("FAIL  the staging folder {} overlaps --in {}\n", staging.string(), input.string());
                ++totals.failed;
                return totals;
            }
        }
        // The grey ADDN plan (the copies' models count for the AddOnNode values that need a grey index) and the ids,
        // before anything is written
        const Plan plan = PlanFromRequest(a_addons, a_inputs, a_tint, CopyModels(a_sources));
        for (const auto& line : plan.problems) {
            std::cout << "FAIL  " << line << "\n";
        }
        if (!plan.problems.empty()) {
            totals.failed += static_cast<int>(plan.problems.size());
            std::cout << "nothing written\n";
            return totals;
        }
        const CopiesMap copies = AssignCopyIds(a_sources, PreviousCopies(a_out));

        RemoveAll(staging);
        bool staged = false;
        try {
            staged = Stage(a_sources, plan, copies, a_inputs, staging, totals);
        } catch (const std::exception& e) {
            std::cout << fmt::format("FAIL  {}\n", e.what());
            ++totals.failed;
        }
        if (!staged) {
            RemoveAll(staging);
            std::cout << "nothing written into " << a_out.string() << " (its previous outputs are unchanged)\n";
            return totals;
        }
        if (!Commit(staging, a_out)) {
            ++totals.failed;
            return totals;
        }
        RemoveAll(staging);
        // A local ESL an earlier run left that this plan no longer needs (as --request does)
        if (plan.newRecords.empty() && !SyncLocalEsl(plan.newRecords, a_out / kLocalPluginName)) {
            ++totals.failed;
            return totals;
        }
        for (const auto& entry : copies.entries) {
            std::cout << fmt::format("copy  {} {} -> {}|{:06X} {} model '{}'\n", CopyTypeTag(entry.type), entry.source,
                kCopiesPluginName, entry.localId, entry.editorId, entry.greyModel);
        }
        for (const auto& [source, id] : copies.retired) {
            std::cout << fmt::format("retired  {} keeps {:06X} (not given to another source)\n", source, id);
        }
        totals.copies = static_cast<int>(copies.entries.size());
        std::cout << fmt::format("wrote {} ({} copies) and {}\n", (a_out / kCopiesPluginName).string(), totals.copies,
            (PluginsDir(a_out) / kCopiesMapName).string());
        return totals;
    }
}
