#include "environment/EnvironmentSelfTest.h"
#include "librarian/SpellResearchMemory.h"
#include "librarian/SpellResearchMemoryMath.h"

#include <array>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// EnvironmentSelfTestMemory - the Spell Research practice and memory checks of the dev self-test
//
//   sr-memory       With SR set up: the first spell in SR's tier lists that has an archetype. Every
//                   archetype x rank global's editor id must end with "<t+1><Rank>" (SR names them
//                   _SR_GlobalArchetype{Group}{Name}{1..5}{Rank}): the rank mapping, checked apart from
//                   the list order the code relies on. The globals are snapshotted, one payment of v goes in
//                   through the game side's Pay, and every global must hold ApplyGain of the gain table.
//                   B's multiplier against 1 + maxPracticeBoost% x the average of
//                   min(1, global[a][R] / ((R + 1) x d)^2), worked out here - once paid, once restored
//                   (1.0 while memoryToPractice is off).
//   sr-memory-hook  The hook's own entry, OnPracticeProgress(spell, 10%, 32%) from the snapshot: two
//                   boundaries, so every global must hold ApplyGain(ApplyGain(before, g), g). SKIP while
//                   practiceToMemory is off.
//   The snapshot is put back by an RAII guard, on an exception or early return too, and checked: the save
//   is left as it was. SKIP without SR set up, without such a spell or with v 0 (memoryMinutes 0).
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        namespace Memory = Librarian::SpellResearchMemory;
        namespace MM = Librarian::SpellResearchMemoryMath;

        constexpr const char* kId = "sr-memory";
        constexpr const char* kHookId = "sr-memory-hook";
        constexpr double kRelativeTolerance = 1e-5;
        constexpr float kPercent = 100.0f;
        constexpr float kHookFrom = 0.10f;   // 10% -> 32%: the 15% and 30% boundaries
        constexpr float kHookTo = 0.32f;
        constexpr int kHookPayments = 2;
        constexpr std::array<std::string_view, MM::kRanks> kRankNames{ "Novice", "Apprentice", "Adept", "Expert", "Master" };

        struct Slot
        {
            RE::TESGlobal* global;
            std::size_t archetype;
            std::size_t rank;
            float before;
        };
        using Snapshot = std::vector<Slot>;

        bool Close(float actual, float expected)
        {
            const double scale = std::fabs(static_cast<double>(expected)) > 1.0 ? std::fabs(static_cast<double>(expected)) : 1.0;
            return std::fabs(static_cast<double>(actual) - static_cast<double>(expected)) <= kRelativeTolerance * scale;
        }

        void Restore(const Snapshot& snapshot)
        {
            for (const auto& slot : snapshot) slot.global->value = slot.before;
        }

        // Puts every snapshotted global back when the check leaves, however it leaves
        class GlobalsRestore
        {
        public:
            explicit GlobalsRestore(const Snapshot& snapshot) : m_snapshot(snapshot) {}
            ~GlobalsRestore() { Restore(m_snapshot); }
            GlobalsRestore(const GlobalsRestore&) = delete;
            GlobalsRestore& operator=(const GlobalsRestore&) = delete;

        private:
            const Snapshot& m_snapshot;
        };

        // Every global against `payments` payments of the gain table from its snapshot. The first mismatch
        // goes into `detail`; returns how many globals differ.
        std::size_t Mismatches(const Snapshot& snapshot, const std::array<float, MM::kRanks>& gains, int payments,
            std::size_t& expectedChanges, std::string& detail)
        {
            std::size_t mismatches = 0;
            expectedChanges = 0;
            for (const auto& slot : snapshot) {
                float expected = slot.before;
                for (int p = 0; p < payments; ++p) expected = MM::ApplyGain(expected, gains[slot.rank]);
                if (expected != slot.before) ++expectedChanges;
                if (!Close(slot.global->value, expected) && mismatches++ == 0) {
                    detail = std::format("; first: archetype {} rank {} holds {:.4f}, math {:.4f}", slot.archetype,
                        slot.rank, slot.global->value, expected);
                }
            }
            return mismatches;
        }

        // B's multiplier for the spell now, against the proportional formula worked out here
        bool MultiplierMatches(const RE::SpellItem* spell, const Memory::SpellMemory& memory, std::string& detail)
        {
            const float difficulty = Memory::CraftDifficulty();
            const float scale = static_cast<float>(memory.rank + 1) * difficulty;
            const float full = scale * scale;
            double sum = 0.0;
            for (const std::size_t a : memory.archetypes) {
                const auto* global = Memory::Global(a, memory.rank);
                const float value = global ? global->value : 0.0f;
                sum += value > 0.0f ? (value < full ? value / full : 1.0f) : 0.0f;
            }
            const float share = static_cast<float>(sum / static_cast<double>(memory.archetypes.size()));
            const auto settings = Memory::CurrentSettings();
            const float expected = settings.memoryToPractice ? 1.0f + settings.maxPracticeBoost / kPercent * share : 1.0f;
            const float actual = Memory::PracticeMultiplier(spell->GetFormID());
            detail = std::format("x{:.4f} (formula x{:.4f}, share {:.3f} at d {:.1f})", actual, expected, share, difficulty);
            return Close(actual, expected);
        }

        // The rank mapping: each global's editor id ends with "<t+1><Rank>"
        std::size_t RankNameMismatches(const Snapshot& snapshot, std::string& detail)
        {
            std::size_t wrong = 0;
            for (const auto& slot : snapshot) {
                const char* raw = slot.global->GetFormEditorID();
                const std::string_view editorId = raw ? raw : "";
                const std::string suffix = std::to_string(slot.rank + 1) + std::string(kRankNames[slot.rank]);
                if (!editorId.ends_with(suffix) && wrong++ == 0) {
                    detail = std::format("; first: archetype {} rank {} is '{}', expected *{}", slot.archetype, slot.rank,
                        editorId, suffix);
                }
            }
            return wrong;
        }
    }

    void CheckSpellResearchMemory()
    {
        std::string why;
        if (!Memory::Ready(why)) {
            Report(kId, Outcome::kSkip, why);
            Report(kHookId, Outcome::kSkip, why);
            return;
        }
        const auto* spell = Memory::SampleSpell();
        const auto memory = spell ? Memory::Lookup(spell) : std::nullopt;
        const float value = Memory::PaymentValue();
        if (!memory || !(value > 0.0f)) {
            const char* reason = !memory ? "no spell in SR's tier lists has an archetype"
                                         : "v is 0 (memoryMinutes 0 or SR's research time base 0)";
            Report(kId, Outcome::kSkip, reason);
            Report(kHookId, Outcome::kSkip, reason);
            return;
        }

        Snapshot snapshot;
        for (const std::size_t a : memory->archetypes) {
            for (std::size_t t = 0; t < MM::kRanks; ++t) {
                if (auto* global = Memory::Global(a, t)) snapshot.push_back({ global, a, t, global->value });
            }
        }
        std::string namesDetail;
        const std::size_t badNames = RankNameMismatches(snapshot, namesDetail);

        const auto gains = MM::GainTable(memory->rank, value);
        std::size_t expectedChanges = 0;
        std::size_t hookExpectedChanges = 0;
        std::size_t changed = 0;
        std::size_t mismatches = 0;
        std::size_t hookMismatches = 0;
        std::string firstMismatch;
        std::string hookMismatch;
        std::string paidB;
        bool paidBOk = false;
        const bool hookOn = Memory::CurrentSettings().practiceToMemory;
        {
            const GlobalsRestore restore(snapshot);
            changed = Memory::Pay(*memory, 1, value);
            mismatches = Mismatches(snapshot, gains, 1, expectedChanges, firstMismatch);
            paidBOk = MultiplierMatches(spell, *memory, paidB);

            if (hookOn) {
                Restore(snapshot);
                Memory::OnPracticeProgress(spell->GetFormID(), kHookFrom, kHookTo);
                hookMismatches = Mismatches(snapshot, gains, kHookPayments, hookExpectedChanges, hookMismatch);
            }
        }

        // The guard has run: every global as it was
        std::size_t notRestored = 0;
        for (const auto& slot : snapshot) {
            if (slot.global->value != slot.before) ++notRestored;
        }
        std::string restoredB;
        const bool restoredBOk = MultiplierMatches(spell, *memory, restoredB);

        const bool ok = badNames == 0 && mismatches == 0 && changed == expectedChanges && paidBOk && restoredBOk &&
                        notRestored == 0;
        Report(kId, ok ? Outcome::kPass : Outcome::kFail,
            std::format("{} ({:08X}) rank {}, {} archetypes, v {:.2f}: {} rank names wrong{}; {} globals changed (math {}), "
                        "{} mismatches{}, {} not restored; B paid {}; B restored {}",
                spell->GetName(), spell->GetFormID(), memory->rank, memory->archetypes.size(), value, badNames,
                namesDetail, changed, expectedChanges, mismatches, firstMismatch, notRestored, paidB, restoredB));
        if (!hookOn) {
            Report(kHookId, Outcome::kSkip, "practiceToMemory is off");
            return;
        }
        Report(kHookId, hookMismatches == 0 && notRestored == 0 ? Outcome::kPass : Outcome::kFail,
            std::format("OnPracticeProgress {:.0f}% -> {:.0f}% ({} payments): {} globals to change, {} mismatches{}",
                kHookFrom * kPercent, kHookTo * kPercent, kHookPayments, hookExpectedChanges, hookMismatches, hookMismatch));
    }
}
