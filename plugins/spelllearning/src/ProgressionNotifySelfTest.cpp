#include "environment/EnvironmentSelfTest.h"
#include "FormIdText.h"
#include "ProgressionManager.h"
#include "ProgressionNotify.h"
#include "SpellEffectivenessHook.h"
#include "SpellTomeHook.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// =============================================================================
// ProgressionNotifySelfTest - the H1-d check in the dev self-test ("notify")
//
// Runs real learning steps on two tree spells A and B of one school with no
// target (spells the player does not know, with no progress, early tracking or
// tome XP of their own), watching every notice through ProgressionNotify's
// observer. At each notice it reads the state; that must equal the state once
// the step has returned (the notice went out after the whole change), and the
// notices must come in the expected order. Steps:
//   set      A as the school's target (by the tome path, which sends target-set,
//            unless single mode would clear other schools' targets: then directly)
//   replace  B as the same school's target: target-cleared(A) with the key on B
//   master   B to its required XP: mastered -> cleared -> unlocked -> progress
//            (early learning on), or ready -> progress then UnlockSpell (off)
// Then it undoes all of it (spell taken back, RelockSpell, entry dropped) and
// checks the targets map and both spells are as they were. HUD lines and
// ModEvents of those steps do go out: a dev save only (selftest.json). The
// steps' notices are watched at the moment they would be sent and kept off the
// panel (its state, "new spell" marks included, is not the test's to undo).
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        using ProgressionNotify::Kind;
        constexpr const char* kId = "notify";

        struct Probe
        {
            std::string school;
            RE::FormID a = 0;
            RE::FormID b = 0;
        };

        // What a notice may read, about the two spells and their school
        struct State
        {
            RE::FormID schoolTarget = 0;
            bool aIsTarget = false;
            bool bIsTarget = false;
            bool aUnlocked = false;
            bool bUnlocked = false;
            bool bEarly = false;
            float bPercent = 0.0f;

            bool operator==(const State&) const = default;
        };

        struct Seen
        {
            ProgressionNotify::Event event;
            State state;
        };

        bool IsAnyTarget(RE::FormID formId)
        {
            const auto& targets = ProgressionManager::GetSingleton()->GetLearningTargets();
            return std::any_of(targets.begin(), targets.end(), [formId](const auto& entry) { return entry.second == formId; });
        }

        State Read(const Probe& probe)
        {
            auto* pm = ProgressionManager::GetSingleton();
            State state;
            state.schoolTarget = pm->GetLearningTarget(probe.school);
            state.aIsTarget = IsAnyTarget(probe.a);
            state.bIsTarget = IsAnyTarget(probe.b);
            state.aUnlocked = pm->IsUnlocked(probe.a);
            state.bUnlocked = pm->IsUnlocked(probe.b);
            state.bEarly = SpellEffectivenessHook::GetSingleton()->IsEarlyLearnedSpell(probe.b);
            state.bPercent = pm->GetProgressPercent(probe.b);
            return state;
        }

        std::string Describe(const State& state)
        {
            return std::format("target={:08X} aTarget={} bTarget={} aUnlocked={} bUnlocked={} bEarly={} b={:.2f}",
                state.schoolTarget, state.aIsTarget, state.bIsTarget, state.aUnlocked, state.bUnlocked, state.bEarly,
                state.bPercent);
        }

        std::string Describe(const std::vector<Seen>& seen)
        {
            std::string text;
            for (const auto& entry : seen) {
                if (!text.empty()) text += ", ";
                text += std::format("{}({:08X}{}{})", ProgressionNotify::KindName(entry.event.kind), entry.event.formId,
                    entry.event.text.empty() ? "" : " ", entry.event.text);
            }
            return text.empty() ? "none" : text;
        }

        // A spell the steps may use and the cleanup can put back exactly
        bool Usable(RE::FormID formId, RE::SpellItem* spell)
        {
            auto* pm = ProgressionManager::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            // With early learning on, the master step expects the early grant (a tree
            // can hold non-spell forms when the scan's type filter is off)
            const bool earlyOn = SpellEffectivenessHook::GetSingleton()->GetSettings().enabled;
            return spell && player && !player->HasSpell(spell) && !pm->PlayerKnowsCopyOf(formId) &&
                   (!earlyOn || SpellEffectivenessHook::CanLearnEarly(spell)) &&
                   !pm->HasProgressRecord(formId) && !IsAnyTarget(formId) &&
                   !SpellEffectivenessHook::GetSingleton()->IsEarlyLearnedSpell(formId) &&
                   !SpellTomeHook::GetSingleton()->HasGrantedTomeXP(formId) && pm->GetRequiredXP(formId) > 0.0f;
        }

        // Two usable spells of one school with no target; B unlockable by its
        // prerequisites when one is (the UnlockSpell step). Sorted ids: the same pick each run
        std::optional<Probe> Pick()
        {
            auto* pm = ProgressionManager::GetSingleton();
            std::vector<RE::FormID> ids(pm->GetTreeSpells().begin(), pm->GetTreeSpells().end());
            std::sort(ids.begin(), ids.end());
            std::map<std::string, std::vector<RE::FormID>> bySchool;
            for (const auto formId : ids) {
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                if (!Usable(formId, spell)) continue;
                const auto school = ProgressionManager::TargetSchoolOf(spell);
                if (school.empty() || pm->GetLearningTarget(school) != 0) continue;
                bySchool[school].push_back(formId);
            }
            for (auto& [school, spells] : bySchool) {
                if (spells.size() < 2) continue;
                auto open = std::find_if(spells.begin(), spells.end(), [pm](RE::FormID id) { return pm->AreTreePrerequisitesMet(id); });
                const RE::FormID b = open != spells.end() ? *open : spells.back();
                const RE::FormID a = spells.front() != b ? spells.front() : spells[1];
                return Probe{ school, a, b };
            }
            return std::nullopt;
        }

        // One step: run it with the observer on, then hold every notice against the
        // state the step left
        template <class Step>
        std::vector<Seen> Watch(const Probe& probe, Step&& step, State& after)
        {
            std::vector<Seen> seen;
            // This thread's notices only: another plugin's call on a worker thread
            // would race the reads, and could outlive seen
            const auto self = std::this_thread::get_id();
            // and kept off the panel: it would show the test's spells until the next load
            ProgressionNotify::SetObserver([&seen, &probe, self](const ProgressionNotify::Event& event) {
                if (std::this_thread::get_id() != self) return true;
                seen.push_back({ event, Read(probe) });
                return false;
            });
            try {
                step();
            } catch (...) {
                ProgressionNotify::SetObserver({});   // it holds seen, which is about to go
                throw;
            }
            ProgressionNotify::SetObserver({});
            after = Read(probe);
            return seen;
        }

        // Every notice saw the state after the step
        bool AllSawAfter(const std::vector<Seen>& seen, const State& after, std::string& why)
        {
            for (const auto& entry : seen) {
                if (entry.state != after) {
                    why = std::format("{}({:08X}) saw [{}], after the step [{}]", ProgressionNotify::KindName(entry.event.kind),
                        entry.event.formId, Describe(entry.state), Describe(after));
                    return false;
                }
            }
            return true;
        }

        struct Expected
        {
            Kind kind;
            RE::FormID formId;
            const char* text = nullptr;   // spell-state's state, when it matters
        };

        // The notices, in order, exactly
        bool Matches(const std::vector<Seen>& seen, const std::vector<Expected>& expected)
        {
            if (seen.size() != expected.size()) return false;
            for (std::size_t i = 0; i < seen.size(); ++i) {
                const auto& event = seen[i].event;
                if (event.kind != expected[i].kind || event.formId != expected[i].formId) return false;
                if (expected[i].text && event.text != expected[i].text) return false;
            }
            return true;
        }

        void Judge(const char* step, const std::vector<Seen>& seen, const State& after,
            const std::vector<Expected>& expected, bool stateRight, const std::string& stateWhy)
        {
            const std::string id = std::format("{}-{}", kId, step);
            std::string why;
            if (!AllSawAfter(seen, after, why)) {
                Report(id, Outcome::kFail, "a notice went out before the change: " + why);
            } else if (!Matches(seen, expected)) {
                Report(id, Outcome::kFail, "notices " + Describe(seen) + " - not the expected order");
            } else if (!stateRight) {
                Report(id, Outcome::kFail, stateWhy + " [" + Describe(after) + "]");
            } else {
                Report(id, Outcome::kPass, "notices " + Describe(seen) + "; each saw [" + Describe(after) + "]");
            }
        }

        std::map<std::string, RE::FormID> Held(const std::unordered_map<std::string, RE::FormID>& targets)
        {
            std::map<std::string, RE::FormID> held;
            for (const auto& [school, formId] : targets) {
                if (formId != 0) held.emplace(school, formId);
            }
            return held;
        }

        // Puts both spells back to nothing and the targets map as it was, however
        // the steps ended (an exception included)
        struct Restore
        {
            Probe probe;
            std::unordered_map<std::string, RE::FormID> targets;

            ~Restore()
            {
                try {
                    Undo();
                } catch (const std::exception& e) {
                    Report(std::format("{}-cleanup", kId), Outcome::kFail, std::string("threw: ") + e.what());
                }
                // Whatever happened above, the game thread's notices reach the panel again
                ProgressionNotify::SetObserver({});
            }

            void Undo() const
            {
                auto* pm = ProgressionManager::GetSingleton();
                auto* player = RE::PlayerCharacter::GetSingleton();
                // The undo's own notices stay off the panel too: after a SKIP, B can still be
                // the target (target-cleared, available) of a panel that never saw it set
                const auto self = std::this_thread::get_id();
                ProgressionNotify::SetObserver([self](const ProgressionNotify::Event&) {
                    return std::this_thread::get_id() != self;
                });
                for (const auto formId : { probe.a, probe.b }) {
                    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                    if (player && spell && player->HasSpell(spell)) player->RemoveSpell(spell);
                    pm->RelockSpell(formId);
                    pm->ForgetProgress(formId);
                }
                ProgressionNotify::SetObserver({});
                // A key holding 0 reads as no target (GetLearningTarget): the steps' clear
                // may drop the school's, and putting it back would fire TargetChanged
                std::string left;
                if (Held(pm->GetLearningTargets()) != Held(targets)) left += "targets map differs; ";
                for (const auto formId : { probe.a, probe.b }) {
                    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                    if (pm->HasProgressRecord(formId)) left += std::format("{:08X} has progress; ", formId);
                    if (player && spell && player->HasSpell(spell)) left += std::format("{:08X} known; ", formId);
                    if (SpellEffectivenessHook::GetSingleton()->IsEarlyLearnedSpell(formId)) left += std::format("{:08X} early; ", formId);
                    if (SpellTomeHook::GetSingleton()->HasGrantedTomeXP(formId)) left += std::format("{:08X} tome XP; ", formId);
                }
                Report(std::format("{}-cleanup", kId), left.empty() ? Outcome::kPass : Outcome::kFail,
                    left.empty() ? "both spells and the targets map are as before" : left);
            }
        };
    }

    void CheckProgressionNotify()
    {
        auto* pm = ProgressionManager::GetSingleton();
        // unlockThreshold 0 grants at 0% (CheckAndRegrantSpell on every set): other notices than expected
        const auto early = SpellEffectivenessHook::GetSingleton()->GetSettings();
        if (early.enabled && (early.unlockThreshold <= 0.0f || early.unlockThreshold > 100.0f)) {
            Report(kId, Outcome::kSkip,
                std::format("unlockThreshold {} is outside (0, 100]: the expected notices do not apply", early.unlockThreshold));
            return;
        }
        const auto probe = Pick();
        if (!probe) {
            Report(kId, Outcome::kSkip, "no school with two unused tree spells and no target");
            return;
        }
        auto* spellA = RE::TESForm::LookupByID<RE::SpellItem>(probe->a);
        Note(kId, std::format("school {} A={:08X} ({}) B={:08X}", probe->school, probe->a, spellA->GetName(), probe->b));
        Restore restore{ *probe, pm->GetLearningTargets() };
        State after;

        // set: the tome path sends target-set; in single mode it clears every
        // other school's target first, which this test would then have to rebuild
        const auto& targets = pm->GetLearningTargets();
        const bool othersHeld = std::any_of(targets.begin(), targets.end(), [](const auto& entry) { return entry.second != 0; });
        const bool viaTome = pm->GetXPSettings().learningMode != "single" || !othersHeld;
        auto seen = Watch(*probe, [&] {
            if (viaTome) {
                pm->SetLearningTargetFromTome(FormIdText::Format(probe->a), spellA);
            } else {
                pm->SetLearningTarget(probe->school, probe->a);
            }
        }, after);
        std::vector<Expected> expected;
        if (viaTome) expected.push_back({ Kind::kTargetSet, probe->a });
        Judge(viaTome ? "set" : "set-direct", seen, after, expected, after.schoolTarget == probe->a,
            "the school's target is not A");

        // replace: the deferred clear of A arrives with the key already on B
        seen = Watch(*probe, [&] { pm->SetLearningTarget(probe->school, probe->b); }, after);
        Judge("replace", seen, after, { { Kind::kTargetCleared, probe->a } },
            after.schoolTarget == probe->b && !after.aIsTarget, "the key is not on B alone");

        // master
        const float required = pm->GetRequiredXP(probe->b);
        if (early.enabled) {
            seen = Watch(*probe, [&] { pm->AddXP(probe->b, required); }, after);
            if (!after.bUnlocked) {
                Report(std::format("{}-master", kId), Outcome::kSkip,
                    "B not mastered by its required XP (a copy known, or held for ISL): " + Describe(seen));
                return;
            }
            Judge("master", seen, after,
                { { Kind::kSpellState, probe->b, "weakened" }, { Kind::kSpellState, probe->b, "mastered" },
                  { Kind::kTargetCleared, probe->b }, { Kind::kSpellUnlocked, probe->b }, { Kind::kProgress, probe->b } },
                !after.bIsTarget && after.schoolTarget == 0 && !after.bEarly, "B still a target or still early");
        } else {
            seen = Watch(*probe, [&] { pm->AddXP(probe->b, required); }, after);
            Judge("ready", seen, after, { { Kind::kSpellReady, probe->b }, { Kind::kProgress, probe->b } },
                after.bPercent >= 1.0f && after.bIsTarget, "B not at 100% as the target");
            if (!pm->CanUnlock(probe->b)) {
                Report(std::format("{}-unlock", kId), Outcome::kSkip, "B's prerequisites are not met");
                return;
            }
            bool unlocked = false;
            seen = Watch(*probe, [&] { unlocked = pm->UnlockSpell(probe->b); }, after);
            Judge("unlock", seen, after,
                { { Kind::kSpellState, probe->b, "mastered" }, { Kind::kTargetCleared, probe->b } },
                unlocked && after.bUnlocked && !after.bIsTarget, "B not unlocked, or still a target");
        }
    }
}
