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

        /// One NIF: grey (MPS) and/or remap, write, read back, verify.
        void BuildOne(const GreyMap& a_map, const std::vector<std::filesystem::path>& a_inputs,
            const std::filesystem::path& a_out, const std::string& a_rel, const std::string& a_greyRel,
            bool a_greyEffects, bool a_needed, Totals& a_totals)
        {
            const auto source = FindSource(a_inputs, a_rel);
            if (!source) {
                std::cout << fmt::format("{}  {}: in no --in folder\n", a_needed ? "FAIL" : "SKIP", a_rel);
                (a_needed ? a_totals.failed : a_totals.skipped) += 1;
                return;
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
                if (!remap.unmapped.empty()) {
                    std::cout << fmt::format("FAIL  {} ({}): AddOnNode value(s) in no map entry, nothing written:\n",
                        a_rel, source->string());
                    for (const auto& line : remap.unmapped) {
                        std::cout << "      " << line << "\n";
                    }
                    ++a_totals.failed;
                    return;
                }
                SaveNif(nif, target);

                nifly::NifFile written;
                LoadNif(written, target);
                const auto problems = VerifyGrey(written, a_greyEffects, a_map);
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
            } catch (const std::exception& e) {
                std::cout << fmt::format("FAIL  {}: {}\n", a_rel, e.what());
                ++a_totals.failed;
            }
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
            if (done.insert(entry.model).second) {
                BuildOne(a_map, a_inputs, a_out, entry.model, entry.greyModel, true, a_needAll, totals);
            }
        }
        for (const auto& [source, grey] : a_map.models) {
            BuildOne(a_map, a_inputs, a_out, source, grey, false, false, totals);
        }
        return totals;
    }
}
