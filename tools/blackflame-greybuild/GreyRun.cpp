// =============================================================================
// GreyRun - writes and verifies every NIF a map lists
// =============================================================================

#include "GreyBuild.h"

#include <NifFile.hpp>

#include <algorithm>
#include <cwctype>
#include <fmt/format.h>
#include <iostream>
#include <set>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        void PrintStats(const GreyStats& a_stats)
        {
            std::cout << fmt::format(
                "      effect shaders {} (palette flag forced on {}), lighting shaders left {}, colour modifiers {}, "
                "colour controllers {} ({} keys, {} with keys not reached), NiPSysColorModifier data {}\n",
                a_stats.effectShaders, a_stats.paletteForced, a_stats.lightingShaders, a_stats.colorModifiers,
                a_stats.colorControllers, a_stats.controllerKeys, a_stats.controllersUnreached, a_stats.psysColorData);
        }

        void PrintResult(const std::string& a_rel, const std::string& a_greyRel, const std::filesystem::path& a_source,
            int a_unknown, const RoundTrip& a_roundTrip, const std::vector<std::string>& a_problems)
        {
            std::cout << fmt::format("{}  {} -> {}\n      from {}; unknown blocks {}; unchanged re-save: {}\n",
                a_problems.empty() ? "OK  " : "FAIL", a_rel, a_greyRel, a_source.string(), a_unknown,
                RoundTripName(a_roundTrip.kind));
            if (a_roundTrip.kind == RoundTripKind::Different) {
                std::cout << "      re-save detail: " << a_roundTrip.detail << "\n";
            }
        }

        /// Writes the ESL, reads it back and compares; false (problems printed) on a difference.
        [[nodiscard]] bool WriteLocalEsl(const std::vector<AddonEntry>& a_records, const std::filesystem::path& a_file)
        {
            WriteAddonEsl(a_records, a_file);
            std::uint32_t flags = 0;
            const auto back = ReadAddonEsl(a_file, flags);
            std::cout << fmt::format("wrote {} (header flags 0x{:X}), {} ADDN record(s) read back:\n", a_file.string(),
                flags, back.size());
            for (const auto& record : back) {
                std::cout << fmt::format("  {:06X} {} index {} model '{}' DNAM cap {} flags {}\n", record.formId,
                    record.editorId, record.index, record.model, record.masterParticleCap, record.dnamFlags);
            }
            const auto problems = VerifyAddonEsl(a_records, a_file);
            for (const auto& problem : problems) {
                std::cout << "FAIL  " << a_file.string() << ": " << problem << "\n";
            }
            return problems.empty();
        }
    }

    bool BuildNif(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, const std::string& a_rel, const std::string& a_greyRel, bool a_greyEffects,
        bool a_needed, Totals& a_totals)
    {
        return BuildNifWith(a_map, a_inputs, a_out, a_rel, a_greyRel, NifOptions{ a_greyEffects, a_needed, false }, a_totals).ok;
    }

    NifResult BuildNifWith(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, const std::string& a_rel, const std::string& a_greyRel, const NifOptions& a_options,
        Totals& a_totals)
    {
        const bool a_greyEffects = a_options.greyEffects;
        NifResult result;
        const auto source = FindSource(a_inputs, a_rel);
        if (!source) {
            std::cout << fmt::format("{}  {}: in no --in folder\n", a_options.needed ? "FAIL" : "SKIP", a_rel);
            (a_options.needed ? a_totals.failed : a_totals.skipped) += 1;
            result.missing = true;
            result.reason = "in no --in folder";
            return result;
        }
        const auto target = a_out / std::filesystem::path(a_greyRel);
        try {
            nifly::NifFile nif;
            LoadNif(nif, *source);
            const int unknown = CountUnknownBlocks(nif);
            const RoundTrip roundTrip = CheckRoundTrip(*source);

            GreyStats stats;
            if (a_greyEffects) {
                stats = GreyEffects(nif, a_map.tint);
            }
            const RemapStats remap = RemapAddonValues(nif, a_map);
            if (!remap.unmapped.empty() && a_options.keepUnmapped) {
                result.keptValues = remap.unmapped;
                std::cout << fmt::format("WARN  {} ({}): AddOnNode value(s) in no map entry, kept as they are:\n", a_rel,
                    source->string());
                for (const auto& line : remap.unmapped) {
                    std::cout << "      " << line << "\n";
                }
            } else if (!remap.unmapped.empty()) {
                std::cout << fmt::format("FAIL  {} ({}): AddOnNode value(s) in no map entry, nothing written:\n", a_rel,
                    source->string());
                for (const auto& line : remap.unmapped) {
                    std::cout << "      " << line << "\n";
                }
                ++a_totals.failed;
                result.reason = "AddOnNode value(s) in no map entry";
                return result;
            }
            SaveNif(nif, target);

            nifly::NifFile written;
            LoadNif(written, target);
            const auto problems = VerifyGrey(written, a_greyEffects, a_map, a_options.keepUnmapped);
            PrintResult(a_rel, a_greyRel, *source, unknown, roundTrip, problems);
            if (a_greyEffects) {
                PrintStats(stats);
            }
            std::cout << fmt::format("      AddOnNodes {}: remapped {}, already grey {}\n", remap.valueNodes,
                remap.remapped, remap.alreadyGrey);
            for (const auto& line : remap.remapLines) {
                std::cout << "        " << line << "\n";
            }
            for (const auto& problem : problems) {
                std::cout << "      problem: " << problem << "\n";
            }
            (problems.empty() ? a_totals.built : a_totals.failed) += 1;
            result.ok = problems.empty();
            if (!result.ok) {
                result.reason = problems.front();
            }
            return result;
        } catch (const std::exception& e) {
            std::cout << fmt::format("FAIL  {}: {}\n", a_rel, e.what());
            ++a_totals.failed;
            result.reason = e.what();
            return result;
        }
    }

    std::optional<std::filesystem::path> FindSource(const std::vector<std::filesystem::path>& a_inputs,
        const std::string& a_dataPath)
    {
        for (const auto& dir : a_inputs) {
            const auto candidate = dir / std::filesystem::path(a_dataPath);
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    bool IsInside(const std::filesystem::path& a_child, const std::filesystem::path& a_parent)
    {
        const auto folded = [](const std::filesystem::path& a_path) {
            // No throwing overloads: a path the OS refuses falls back to its absolute form
            std::error_code ec;
            std::filesystem::path full = std::filesystem::weakly_canonical(a_path, ec);
            if (ec) {
                full = std::filesystem::absolute(a_path, ec);
                if (ec) {
                    full = a_path;
                }
            }
            std::wstring text = full.lexically_normal().wstring();
            std::transform(text.begin(), text.end(), text.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (text.empty() || text.back() != L'\\') {
                text += L'\\';
            }
            return text;
        };
        const std::wstring child = folded(a_child);
        const std::wstring parent = folded(a_parent);
        return child.compare(0, parent.size(), parent) == 0;
    }

    Totals BuildFromMap(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
        const std::filesystem::path& a_out, bool a_needAll)
    {
        Totals totals;
        std::set<std::string> done;
        for (const auto& [index, entry] : a_map.addons) {
            if (done.insert(entry.greyModel).second) {  // indices sharing one MPS share its grey copy
                BuildNif(a_map, a_inputs, a_out, entry.model, entry.greyModel, true, a_needAll, totals);
            }
        }
        for (const auto& [source, grey] : a_map.models) {
            BuildNif(a_map, a_inputs, a_out, source, grey, false, false, totals);
        }
        return totals;
    }

    /// Writes the local ESL, or with no records for it removes one an earlier run left in --out
    /// (the new map names none of its records). False (reason printed) on a failure.
    bool SyncLocalEsl(const std::vector<AddonEntry>& a_records,
        const std::filesystem::path& a_file)
    {
        if (!a_records.empty()) {
            return WriteLocalEsl(a_records, a_file);
        }
        std::error_code ec;
        if (!std::filesystem::exists(a_file, ec) && !ec) {
            return true;
        }
        if (ec || !std::filesystem::remove(a_file, ec) || ec) {
            std::cout << fmt::format("FAIL  {} is left from an earlier run and could not be removed ({}): this setup "
                                     "needs no local grey records. Remove it by hand, then run again.\n",
                a_file.string(), ec ? ec.message() : std::string("not removed"));
            return false;
        }
        std::cout << fmt::format("removed {}: left from an earlier run, this setup needs no local grey records\n",
            a_file.string());
        return true;
    }
}
