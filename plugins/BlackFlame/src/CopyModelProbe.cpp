#include "CopyModelProbe.h"

#include "Config.h"
#include "FireClosure.h"
#include "FormCopies.h"
#include "GameThread.h"
#include "SehGuard.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // I6: the work is spread over main-thread drains, a slice each, so the main menu does not freeze; the budget is the
        // sum of the slices (Demand time, controls included). Past it the rest is left out (listed) and those copies stay
        constexpr auto kBudget = std::chrono::seconds(20);
        constexpr auto kSlice = std::chrono::milliseconds(50);
        constexpr std::size_t kPathsPerLine = 8;  // the left-out list, a few paths a line

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
            bool muzzle = false;
        };

        // The whole run's state, kept between drains (main thread only)
        struct Run
        {
            std::vector<Use> uses;
            std::vector<std::string> order;          // distinct copy paths, first-use order
            std::map<std::string, Result> results;  // by Key(path): copy paths and the controls asked for
            std::size_t next = 0;                    // next index into order, then into uses (the safety net)
            bool pathsDone = false;
            Clock::duration spent{};
            int ok = 0;
            int failed = 0;
            std::vector<std::string> leftOut;
            std::vector<DisabledCopy> disabled;
            std::vector<RE::FormID> muzzlesReverted;
            std::map<RE::FormID, std::size_t> seen;  // copy -> index in disabled (a copy failing in model and muzzle: one entry)
        };

        bool OverBudget(const Run& a_run) { return a_run.spent > kBudget; }

        // A probed result for a_path: already probed (a copy path or an earlier control) or probed now (I5)
        const Result& ResultFor(Run& a_run, const std::string& a_path)
        {
            auto& slot = a_run.results[Key(a_path)];
            if (!slot.probed) {
                slot = Demand(a_path);
            }
            return slot;
        }

        void Prepare(Run& a_run)
        {
            for (const auto& filled : FilledCopies()) {
                if (auto path = ModelPathOf(filled.copy); !path.empty()) {
                    a_run.uses.push_back({ filled.source, filled.copy, std::move(path), ModelPathOf(filled.source), false });
                }
                if (auto path = MuzzleOf(filled.copy); !path.empty()) {
                    a_run.uses.push_back({ filled.source, filled.copy, std::move(path), MuzzleOf(filled.source), true });
                }
            }
            for (const auto& use : a_run.uses) {
                if (a_run.results.try_emplace(Key(use.copyPath)).second) {
                    a_run.order.push_back(use.copyPath);
                }
            }
        }

        // One copy path (left out when the budget ran out)
        void ProbePath(Run& a_run, const std::string& a_path)
        {
            if (OverBudget(a_run)) {
                a_run.leftOut.push_back(a_path);
                return;
            }
            const auto& result = ResultFor(a_run, a_path);
            if (result.ok) {
                ++a_run.ok;
            } else {
                ++a_run.failed;
                logger::warn("BlackFlame: copy model probe: FAILED {}", Describe(a_path, result));
            }
        }

        // The safety net for one use: a failed copy path whose source path (the control) loads takes the copy out (its model)
        // or puts the source's muzzle back (its muzzle, I8)
        void Judge(Run& a_run, const Use& a_use)
        {
            const auto& result = a_run.results[Key(a_use.copyPath)];
            if (!result.probed || result.ok) {
                return;
            }
            const char* role = a_use.muzzle ? "muzzle" : "model";
            const auto copyId = a_use.copy->GetFormID();
            if (Key(a_use.copyPath) == Key(a_use.sourcePath)) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} is the source's own (the builder kept it), "
                             "nothing to fall back to: {}", copyId, role, Describe(a_use.copyPath, result));  // I5
                return;
            }
            std::string controlText = "no source model in that role";
            bool controlOk = false;
            if (!a_use.sourcePath.empty()) {
                const auto known = a_run.results.find(Key(a_use.sourcePath));
                if ((known == a_run.results.end() || !known->second.probed) && OverBudget(a_run)) {
                    logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} failed, the control was not checked (the {} s "
                                 "budget ran out): {}", copyId, role, std::chrono::duration_cast<std::chrono::seconds>(kBudget).count(),
                        Describe(a_use.copyPath, result));  // I6
                    return;
                }
                const auto& control = ResultFor(a_run, a_use.sourcePath);
                controlOk = control.ok;
                controlText = "source " + Describe(a_use.sourcePath, control);
            }
            const std::string text = std::format("{} {} (source {:08X}; control {})", role, Describe(a_use.copyPath, result),
                a_use.source->GetFormID(), controlText);
            if (!controlOk) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} kept: its {} failed but the source's did not load either - "
                             "not a copy fault: {}", copyId, role, text);
                return;
            }
            if (a_use.muzzle) {
                a_run.muzzlesReverted.push_back(copyId);
                logger::warn("BlackFlame: copy model probe: copy {:08X} keeps its swaps; its muzzle goes back to the source's: {}",
                    copyId, text);
                return;
            }
            if (const auto it = a_run.seen.find(copyId); it != a_run.seen.end()) {
                a_run.disabled[it->second].text += "; " + text;
                return;
            }
            a_run.seen.emplace(copyId, a_run.disabled.size());
            a_run.disabled.push_back({ copyId, text });
        }

        void Finish(Run& a_run)
        {
            logger::info("BlackFlame: copy model probe: {} distinct copy model path(s) of {} copies: {} ok, {} failed, {} left out; "
                         "{} ms of probing", a_run.order.size(), FilledCopies().size(), a_run.ok, a_run.failed, a_run.leftOut.size(),
                std::chrono::duration_cast<std::chrono::milliseconds>(a_run.spent).count());
            for (std::size_t i = 0; i < a_run.leftOut.size(); i += kPathsPerLine) {
                std::string line;
                for (std::size_t j = i; j < std::min(a_run.leftOut.size(), i + kPathsPerLine); ++j) {
                    line += (line.empty() ? "" : ", ") + a_run.leftOut[j];
                }
                logger::info("BlackFlame: copy model probe: not probed (the {} s budget ran out): {}",
                    std::chrono::duration_cast<std::chrono::seconds>(kBudget).count(), line);
            }
            for (const auto& item : a_run.disabled) {
                logger::warn("BlackFlame: copy model probe: copy {:08X} out of the swaps this session (stays original): {}", item.copy,
                    item.text);
            }
            logger::info("BlackFlame: copy NIF failed to load in game: {} copies taken out of the swaps, {} muzzle(s) back to the "
                         "source's", a_run.disabled.size(), a_run.muzzlesReverted.size());
            const bool changed = !a_run.disabled.empty() || !a_run.muzzlesReverted.empty();
            DisableCopiesForSession(std::move(a_run.disabled), std::move(a_run.muzzlesReverted));
            // I7: links other copies hold to a dropped copy go back to the original now, and a reverted muzzle is written
            if (changed) {
                RefreshCopies("copy model probe");
            }
        }

        // One drain's slice; true when the run is over
        bool Step(Run& a_run)
        {
            const auto start = Clock::now();
            while (Clock::now() - start < kSlice) {
                const auto before = Clock::now();
                if (!a_run.pathsDone) {
                    if (a_run.next < a_run.order.size()) {
                        ProbePath(a_run, a_run.order[a_run.next++]);
                    } else {
                        a_run.pathsDone = true;
                        a_run.next = 0;
                    }
                } else if (a_run.next < a_run.uses.size()) {
                    Judge(a_run, a_run.uses[a_run.next++]);
                } else {
                    return true;
                }
                a_run.spent += Clock::now() - before;
            }
            return false;
        }

        void Post(std::shared_ptr<Run> a_run)
        {
            const bool queued = QueueTask("BlackFlameCopyModelProbe", [a_run]() {
                bool done = true;
                if (!Guarded("copy model probe run", [&]() { done = Step(*a_run); })) {
                    return;  // faulted (logged): the copies stay as they are
                }
                if (done) {
                    (void)Guarded("copy model probe finish", [&]() { Finish(*a_run); });
                } else {
                    Post(a_run);
                }
            });
            if (!queued) {
                logger::warn("BlackFlame: copy model probe: could not be queued for the main thread - the copies stay as they are");
            }
        }
    }

    void QueueCopyModelProbe()
    {
        if (!GetConfig().copyModelProbe) {
            logger::info("BlackFlame: copy model probe: off (copyModelProbe false)");
            return;
        }
        auto run = std::make_shared<Run>();
        Prepare(*run);
        Post(std::move(run));
    }
}
