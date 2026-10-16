#include "CopyModelProbe.h"

#include "FireClosure.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "SehGuard.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr auto kBudget = std::chrono::seconds(20);  // past this the rest is left out (listed), copies stay enabled
        constexpr std::size_t kPathsPerLine = 8;            // the left-out list, a few paths a line

        struct Result
        {
            bool probed = false;
            bool ok = false;
            bool fault = false;
            int code = 0;
            bool node = false;
            long long ms = 0;
        };

        std::string Key(std::string_view a_path)
        {
            std::string out(a_path);
            std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::ranges::replace(out, '/', '\\');
            return out;
        }

        // The record's own model (FireClosure.h ModelPathOf: a per-type cast; TESModel is a component, not a form type)
        std::string ModelOf(const RE::TESForm* a_form)
        {
            return ModelPathOf(a_form);
        }

        std::string MuzzleOf(const RE::TESForm* a_form)
        {
            const auto* projectile = a_form ? a_form->As<RE::BGSProjectile>() : nullptr;
            const char* path = projectile ? projectile->muzzleFlashModel.model.c_str() : nullptr;
            return path ? std::string(path) : std::string();
        }

        Result Demand(const std::string& a_path)
        {
            Result result;
            result.probed = true;
            RE::NiPointer<RE::NiNode> out;
            auto code = RE::BSResource::ErrorCode::kNone;
            const auto begin = Clock::now();
            result.fault = !GuardedKeep("copy model probe", [&]() { code = RE::BSModelDB::Demand(a_path.c_str(), out, {}); });
            result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
            result.code = static_cast<int>(code);
            result.node = out != nullptr;
            result.ok = !result.fault && code == RE::BSResource::ErrorCode::kNone && result.node;
            out.reset();  // our reference given back (the model DB keeps or frees its entry)
            return result;
        }

        std::string Describe(const std::string& a_path, const Result& a_result)
        {
            return std::format("'{}': {}error code {}, node {}, {} ms", a_path, a_result.fault ? "FAULT; " : "", a_result.code,
                a_result.node ? "yes" : "NULL", a_result.ms);
        }

        struct Use
        {
            const RE::TESForm* source = nullptr;
            const RE::TESForm* copy = nullptr;
            std::string copyPath;
            std::string sourcePath;  // the source's model in the same role (the control), may be empty
            const char* role = "";
        };

        void Run()
        {
            // Every (copy, role) use with its path; distinct paths probed once, in first-use order
            std::vector<Use> uses;
            for (const auto& filled : FilledCopies()) {
                if (auto path = ModelOf(filled.copy); !path.empty()) {
                    uses.push_back({ filled.source, filled.copy, std::move(path), ModelOf(filled.source), "model" });
                }
                if (auto path = MuzzleOf(filled.copy); !path.empty()) {
                    uses.push_back({ filled.source, filled.copy, std::move(path), MuzzleOf(filled.source), "muzzle" });
                }
            }
            std::map<std::string, Result> results;  // by Key(path)
            std::vector<std::string> order;
            for (const auto& use : uses) {
                if (results.try_emplace(Key(use.copyPath)).second) {
                    order.push_back(use.copyPath);
                }
            }
            const auto start = Clock::now();
            std::vector<std::string> leftOut;
            int ok = 0;
            int failed = 0;
            for (const auto& path : order) {
                if (Clock::now() - start > kBudget) {
                    leftOut.push_back(path);
                    continue;
                }
                auto& result = results[Key(path)];
                result = Demand(path);
                if (result.ok) {
                    ++ok;
                } else {
                    ++failed;
                    logger::warn("BlackFlame: copy model probe: FAILED {}", Describe(path, result));
                }
            }
            const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            logger::info("BlackFlame: copy model probe: {} distinct copy model path(s) of {} copies: {} ok, {} failed, {} left out; "
                         "{} ms", order.size(), FilledCopies().size(), ok, failed, leftOut.size(), totalMs);
            for (std::size_t i = 0; i < leftOut.size(); i += kPathsPerLine) {
                std::string line;
                for (std::size_t j = i; j < std::min(leftOut.size(), i + kPathsPerLine); ++j) {
                    line += (line.empty() ? "" : ", ") + leftOut[j];
                }
                logger::info("BlackFlame: copy model probe: not probed (the {} s budget ran out): {}",
                    std::chrono::duration_cast<std::chrono::seconds>(kBudget).count(), line);
            }

            // The safety net: a failed copy path whose source path (the control) loads takes that copy out
            std::map<std::string, Result> controls;
            std::vector<DisabledCopy> disabled;
            std::map<RE::FormID, std::size_t> seen;  // copy -> index in disabled (a copy failing in two roles: one entry)
            for (const auto& use : uses) {
                const auto& result = results[Key(use.copyPath)];
                if (!result.probed || result.ok) {
                    continue;
                }
                std::string controlText = "no source model in that role";
                bool controlOk = false;
                if (!use.sourcePath.empty()) {
                    auto [it, fresh] = controls.try_emplace(Key(use.sourcePath));
                    if (fresh) {
                        it->second = Demand(use.sourcePath);
                    }
                    controlOk = it->second.ok;
                    controlText = "source " + Describe(use.sourcePath, it->second);
                }
                const auto copyId = use.copy->GetFormID();
                const std::string text = std::format("{} {} (source {:08X}; control {})", use.role, Describe(use.copyPath, result),
                    use.source->GetFormID(), controlText);
                if (!controlOk) {
                    logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} failed but the source's did not load either "
                                 "- not a copy fault: {}", copyId, use.role, text);
                    continue;
                }
                if (const auto it = seen.find(copyId); it != seen.end()) {
                    disabled[it->second].text += "; " + text;
                    continue;
                }
                seen.emplace(copyId, disabled.size());
                disabled.push_back({ copyId, text });
            }
            for (const auto& item : disabled) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} out of the swaps this session (stays original): {}", item.copy,
                    item.text);
            }
            logger::info("BlackFlame: copy NIF failed to load in game: {} copies taken out of the swaps", disabled.size());
            DisableCopiesForSession(std::move(disabled));
        }
    }

    void QueueCopyModelProbe()
    {
        if (!QueueTask("BlackFlameCopyModelProbe", []() { (void)Guarded("copy model probe run", []() { Run(); }); })) {
            logger::warn("BlackFlame: copy model probe: could not be queued for the main thread");
        }
    }
}
