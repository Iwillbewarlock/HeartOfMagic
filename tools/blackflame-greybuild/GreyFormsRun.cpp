// =============================================================================
// GreyFormsRun - the whole --forms / --from-game run (GreyForms.h)
// =============================================================================
// Nothing is written into --out until every output is written and verified in a staging folder
// beside it (<out>.staging): ids assigned, record bytes built and validated, NIFs, ESLs and maps
// read back. Only then is each staged file moved into --out on its own (MoveFileExW, replacing the
// old copy and written through). The --out folder itself is never removed or renamed, so a mod
// manager's own files in it (MO2's meta.ini) stay. A failure before the moves leaves the previous
// outputs as they were; a failure during them is printed with the file it stopped at, and running
// again converges on the same result (the staging folder is rebuilt from scratch).
// Warn and continue (--from-game): a copy whose NIF cannot be read or parsed keeps the source's
// model (its map entry says why), a grey MPS that cannot be made drops its grey index (the
// AddOnNodes keep the original value), an AddOnNode value in no ADDN stays as it is.

#include "GreyForms.h"

#include <fmt/format.h>
#include <iostream>
#include <set>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

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
                std::cout << "no copies map in --out yet: ids start at 0x800 (use the same --out every time, so the ids stay)\n";
                return {};
            }
            std::cout << "ids from the earlier run: " << PathText(file) << "\n";
            return LoadCopiesMap(file);
        }

        // Every distinct model a copy names: form models and PROJ muzzle models
        [[nodiscard]] std::vector<std::string> CopyModels(const std::vector<FormSource>& a_sources)
        {
            std::vector<std::string> models;
            std::set<std::string> seen;
            for (const auto& form : a_sources) {
                for (const auto* model : { &form.model, &form.muzzleModel }) {
                    if (!model->empty() && seen.insert(*model).second) {
                        models.push_back(*model);
                    }
                }
            }
            return models;
        }

        // True when a_dir is gone afterwards
        bool RemoveAll(const std::filesystem::path& a_dir)
        {
            std::error_code ec;
            std::filesystem::remove_all(a_dir, ec);
            if (ec) {
                std::cout << fmt::format("note: could not remove {} ({})\n", PathText(a_dir), ec.message());
            }
            std::error_code exists;
            return !std::filesystem::exists(a_dir, exists) && !exists;
        }

        std::string LastErrorText(DWORD a_code)
        {
            return fmt::format("Windows error {}", a_code);
        }

        // Moves every staged file into --out, one file at a time (MoveFileExW: replace, write through); false (printed)
        // on the first failure, naming how far it got. The --out folder itself is never removed or renamed.
        [[nodiscard]] bool Commit(const std::filesystem::path& a_staging, const std::filesystem::path& a_out)
        {
            // W7: every staged file is listed first (no moves while the folder is walked), then moved
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(a_staging, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file()) {
                    files.push_back(it->path());
                }
            }
            if (ec) {
                std::cout << fmt::format("FAIL  reading the staging folder failed ({}): nothing moved\n", ec.message());
                return false;
            }
            for (std::size_t i = 0; i < files.size(); ++i) {
                const auto target = a_out / std::filesystem::relative(files[i], a_staging);
                std::filesystem::create_directories(target.parent_path(), ec);
                if (!ec && !::MoveFileExW(files[i].c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    std::cout << fmt::format("FAIL  moving {} into place failed ({}): {} of {} files moved, --out may hold a mix "
                                             "of old and new files; close whatever holds it (the game, an xEdit) and run again\n",
                        PathText(target), LastErrorText(::GetLastError()), i, files.size());
                    return false;
                }
                if (ec) {
                    std::cout << fmt::format("FAIL  making the folder of {} failed ({}): {} of {} files moved; run again\n",
                        PathText(target), ec.message(), i, files.size());
                    return false;
                }
            }
            return true;
        }

        void Warn(FormsTotals& a_totals, const std::string& a_line)
        {
            std::cout << "WARN  " << a_line << "\n";
            ++a_totals.warnings;
        }

        // Grey MPS copies: a failure drops the grey index under warn-and-continue (its AddOnNodes keep the original value)
        [[nodiscard]] bool StageMps(Plan& a_plan, const std::vector<std::filesystem::path>& a_inputs,
            const std::filesystem::path& a_staging, bool a_warn, TextureJob* a_textures, FormsTotals& a_totals)
        {
            std::map<std::string, NifResult> done;
            for (const auto& [index, entry] : a_plan.map.addons) {
                if (done.count(entry.greyModel) == 0) {  // indices sharing one MPS share its grey copy
                    Totals totals;
                    const auto& result = done.emplace(entry.greyModel, BuildNifWith(a_plan.map, a_inputs, a_staging, entry.model,
                                                      entry.greyModel, NifOptions{ true, true, a_warn, a_textures }, totals)).first->second;
                    if (result.ok) {
                        AddShaderCounts(a_totals.shaders, result.stats);
                    }
                }
            }
            for (const auto& [grey, result] : done) {
                if (result.ok) {
                    continue;
                }
                if (!a_warn) {
                    ++a_totals.failed;
                    return false;
                }
                std::vector<int> dropped;
                for (const auto& [index, entry] : a_plan.map.addons) {
                    if (entry.greyModel == grey) {
                        dropped.push_back(index);
                    }
                }
                for (const int index : dropped) {
                    const auto greyForm = a_plan.map.addons.at(index).greyForm;
                    a_plan.map.addons.erase(index);
                    a_plan.newRecords.erase(std::remove_if(a_plan.newRecords.begin(), a_plan.newRecords.end(),
                                                [&](const AddonEntry& a_record) { return a_record.greyForm == greyForm; }),
                        a_plan.newRecords.end());
                    Warn(a_totals, fmt::format("grey MPS {} not made ({}): ADDN index {} keeps its original particles", grey,
                        result.reason, index));
                }
            }
            return true;
        }

        // Remapped impact / hazard copies for the grey map (the DLL's GetModel swap): a failure drops the model under warn
        [[nodiscard]] bool StageRemapped(Plan& a_plan, const std::vector<std::filesystem::path>& a_inputs,
            const std::filesystem::path& a_staging, bool a_warn, FormsTotals& a_totals)
        {
            for (auto it = a_plan.map.models.begin(); it != a_plan.map.models.end();) {
                Totals totals;
                const auto result = BuildNifWith(a_plan.map, a_inputs, a_staging, it->first, it->second,
                    NifOptions{ false, false, a_warn }, totals);
                if (result.ok || result.missing) {
                    ++it;
                    continue;
                }
                if (!a_warn) {
                    ++a_totals.failed;
                    return false;
                }
                Warn(a_totals, fmt::format("remapped copy of {} not made ({}): left out of the grey map", it->first, result.reason));
                it = a_plan.map.models.erase(it);
            }
            return true;
        }

        // Full-grey copy NIFs: each distinct model once; under warn-and-continue a failure leaves "" as the grey path and
        // a note (the record then names the source's model)
        [[nodiscard]] bool StageCopies(const Plan& a_plan, const std::vector<FormSource>& a_sources, CopiesMap& a_copies,
            const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_staging,
            const FormsRunOptions& a_options, TextureJob* a_textures, FormsTotals& a_totals)
        {
            const std::set<std::string> unreadable(a_options.failedNifs.begin(), a_options.failedNifs.end());
            std::map<std::string, std::string> failures;  // model -> why
            for (const auto& model : CopyModels(a_sources)) {
                if (unreadable.count(model) != 0) {
                    failures.emplace(model, "the game could not read this NIF (request nifs ok false)");
                    continue;
                }
                Totals totals;
                const auto result = BuildNifWith(a_plan.map, a_inputs, a_staging, model, CopyModelFor(model),
                    NifOptions{ true, true, a_options.warnAndContinue, a_textures }, totals);
                if (result.ok) {
                    ++a_totals.nifsBuilt;
                    AddShaderCounts(a_totals.shaders, result.stats);
                    if (!result.keptValues.empty()) {
                        Warn(a_totals, fmt::format("{}: {} AddOnNode value(s) in no ADDN kept as they are", model,
                            result.keptValues.size()));
                    }
                    continue;
                }
                if (!a_options.warnAndContinue) {
                    ++a_totals.failed;
                    return false;
                }
                failures.emplace(model, result.reason);
            }
            for (auto& entry : a_copies.entries) {
                if (const auto it = failures.find(entry.sourceModel); it != failures.end()) {
                    entry.greyModel.clear();
                    entry.note = "no grey copy of the model: " + it->second;
                    Warn(a_totals, fmt::format("{} {}: the copy keeps the source's model {} ({})", CopyTypeTag(entry.type),
                        entry.source, entry.sourceModel, it->second));
                }
                if (const auto it = failures.find(entry.sourceMuzzleModel); it != failures.end()) {
                    entry.greyMuzzleModel.clear();
                    entry.note += (entry.note.empty() ? "" : "; ") + std::string("no grey copy of the muzzle model: ") + it->second;
                    Warn(a_totals, fmt::format("PROJ {}: the muzzle keeps {} ({})", entry.source, entry.sourceMuzzleModel,
                        it->second));
                }
            }
            return true;
        }

        [[nodiscard]] bool WriteEsl(const CopiesMap& a_copies, const std::vector<FormSource>& a_sources,
            const std::filesystem::path& a_file, FormsTotals& a_totals)
        {
            WriteCopiesEsl(a_copies, a_sources, a_file);
            const auto problems = VerifyCopiesEsl(a_copies, a_sources, a_file);
            for (const auto& problem : problems) {
                std::cout << "FAIL  " << kCopiesPluginName << ": " << problem << "\n";
            }
            a_totals.failed += static_cast<int>(problems.size());
            return problems.empty();
        }

        // Everything into a_staging; false on a failure (counted in a_totals)
        [[nodiscard]] bool Stage(const std::vector<FormSource>& a_sources, Plan& a_plan, CopiesMap& a_copies,
            const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_staging,
            const FormsRunOptions& a_options, FormsTotals& a_totals)
        {
            const bool warn = a_options.warnAndContinue;
            // 1. Record bytes first: every value is validated before a single NIF is built (written again in step 3)
            const auto esl = a_staging / kCopiesPluginName;
            if (!WriteEsl(a_copies, a_sources, esl, a_totals)) {
                return false;
            }
            // 2. The grey MPS, the remapped impact copies, then the full-grey copy of every distinct source model; the
            //    full-grey ones get texture copies (T1) unless textures are off
            TextureJob job;
            job.inputs = a_inputs;
            job.out = a_staging;
            job.notDumped = a_options.texturesNotDumped;
            TextureJob* textures = a_options.textures ? &job : nullptr;
            const bool built = StageMps(a_plan, a_inputs, a_staging, warn, textures, a_totals) &&
                               StageRemapped(a_plan, a_inputs, a_staging, warn, a_totals) &&
                               StageCopies(a_plan, a_sources, a_copies, a_inputs, a_staging, a_options, textures, a_totals);
            a_totals.textures = job.totals;
            if (!built) {
                return false;
            }
            // 3. The copies ESL again (a copy without its grey NIF names the source's model), the local ADDN ESL (always,
            //    empty or not: the same rule as the empty one shipped with the mod), the grey map and the copies map
            if (!WriteEsl(a_copies, a_sources, esl, a_totals)) {
                return false;
            }
            const auto local = a_staging / kLocalPluginName;
            if (a_plan.newRecords.empty()) {
                WriteAddonEsl({}, local);
                if (const auto problems = VerifyAddonEsl({}, local); !problems.empty()) {
                    std::cout << "FAIL  " << kLocalPluginName << ": " << problems.front() << "\n";
                    ++a_totals.failed;
                    return false;
                }
            } else if (!SyncLocalEsl(a_plan.newRecords, local)) {
                ++a_totals.failed;
                return false;
            }
            WriteMap(a_plan.map, PluginsDir(a_staging) / kGreyMapName,
                { "Generated by blackflame-greybuild for one setup (plugins/BlackFlame/README.md, \"Black Flame v2\").",
                    "It replaces the shipped map when this output folder is a mod placed after Heart of Magic - Black Flame." });
            a_copies.shaders = a_totals.shaders;
            a_copies.tint = a_plan.map.tint;
            a_copies.tintAdd = a_plan.map.tintAdd;
            WriteCopiesMap(a_copies, PluginsDir(a_staging) / kCopiesMapName);
            (void)LoadCopiesMap(PluginsDir(a_staging) / kCopiesMapName);  // read back: throws on a bad write
            return true;
        }
    }

    FormsTotals BuildForms(const std::vector<FormSource>& a_sources, const Request& a_addons,
        const std::vector<std::filesystem::path>& a_inputs, const std::filesystem::path& a_out, float a_tint,
        const FormsRunOptions& a_options)
    {
        FormsTotals totals;
        // W7: --out normalised (no trailing separator) before the staging path is made beside it
        std::filesystem::path out = a_out.lexically_normal();
        if (!out.has_filename()) {
            out = out.parent_path();
        }
        std::filesystem::path staging = out;
        staging += kStagingSuffix;
        for (const auto& input : a_inputs) {
            if (IsInside(staging, input) || IsInside(input, staging)) {
                std::cout << fmt::format("FAIL  the staging folder {} overlaps --in {}\n", PathText(staging), PathText(input));
                ++totals.failed;
                return totals;
            }
        }
        // The grey ADDN plan (the copies' models count for the AddOnNode values that need a grey index) and the ids,
        // before anything is written
        Plan plan = PlanFromRequest(a_addons, a_inputs, a_tint, CopyModels(a_sources), a_options.warnAndContinue);
        plan.map.tintAdd = a_options.tintAdd;
        for (const auto& line : plan.warnings) {
            Warn(totals, line);
        }
        for (const auto& line : plan.problems) {
            std::cout << "FAIL  " << line << "\n";
        }
        if (!plan.problems.empty()) {
            totals.failed += static_cast<int>(plan.problems.size());
            std::cout << "nothing written\n";
            return totals;
        }
        CopiesMap copies = AssignCopyIds(a_sources, PreviousCopies(out), a_options.reclaimRetired);
        copies.requestFingerprint = a_options.fingerprint;
        copies.requestFingerprintSizes = a_options.fingerprintSizes;
        copies.excluded = a_options.excluded;

        if (!RemoveAll(staging)) {
            std::cout << fmt::format("FAIL  the staging folder {} left by an earlier run cannot be cleared: nothing written\n",
                PathText(staging));
            ++totals.failed;
            return totals;
        }
        bool staged = false;
        try {
            staged = Stage(a_sources, plan, copies, a_inputs, staging, a_options, totals);
        } catch (const std::exception& e) {
            std::cout << fmt::format("FAIL  {}\n", e.what());
            ++totals.failed;
        }
        if (!staged) {
            (void)RemoveAll(staging);
            std::cout << "nothing written into " << PathText(out) << " (its previous outputs are unchanged)\n";
            return totals;
        }
        if (!Commit(staging, out)) {
            ++totals.failed;
            return totals;
        }
        (void)RemoveAll(staging);
        for (const auto& entry : copies.entries) {
            std::cout << fmt::format("copy  {} {} -> {}|{:06X} {} model '{}'{}\n", CopyTypeTag(entry.type), entry.source,
                kCopiesPluginName, entry.localId, entry.editorId, CopyRecordModel(entry),
                entry.note.empty() ? std::string() : " (" + entry.note + ")");
        }
        for (const auto& [source, id] : copies.retired) {
            std::cout << fmt::format("retired  {} keeps {:06X} (not given to another source)\n", source, id);
        }
        totals.copies = static_cast<int>(copies.entries.size());
        std::cout << fmt::format("shaders  {} effect shaders in the grey NIFs: palette normal {} (GradBlackFlame.dds, tint {}; {} "
                                 "without an alpha property), palette additive {} (GradBlackFlameAdd.dds, tint {}), left with a grey "
                                 "base colour {}\n", totals.shaders.effectShaders, totals.shaders.paletted, plan.map.tint,
            totals.shaders.paletteNoAlpha, totals.shaders.palettedAdd, plan.map.tintAdd, totals.shaders.paletteLeft);
        std::cout << (a_options.textures ? TextureTotalsLine(totals.textures) :
                                           std::string("textures  off (--no-textures, or a game request without a texture list)"))
                  << "\n";
        std::cout << fmt::format("wrote {} ({} copies) and {}\n", PathText(out / kCopiesPluginName), totals.copies,
            PathText(PluginsDir(out) / kCopiesMapName));
        return totals;
    }
}
