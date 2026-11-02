#include "environment/EnvironmentSelfTest.h"
#include "librarian/SpellResearchMemory.h"
#include "librarian/SpellResearchMemoryMath.h"

#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// EnvironmentSelfTestMemory - the Spell Research practice and memory check of the dev self-test
//
//   sr-memory  With SR set up: the first spell in SR's tier lists that has an archetype. Its
//              globals (every archetype x rank) are snapshotted, one payment of v goes in through
//              the game side's Pay, and every global must hold what SpellResearchMemoryMath.h
//              gives (ApplyGain of the gain table). Then B's multiplier for the spell against
//              1 + maxPracticeBoost% x (archetypes above 0 at its rank / archetypes) - once paid,
//              once restored (1.0 while memoryToPractice is off). The snapshot is put back by an
//              RAII guard, on an exception or early return too, and checked: the save is left as it
//              was. SKIP without SR set up, without such a spell or with v 0 (memoryMinutes 0).
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        namespace Memory = Librarian::SpellResearchMemory;
        namespace MM = Librarian::SpellResearchMemoryMath;

        constexpr const char* kId = "sr-memory";
        constexpr double kRelativeTolerance = 1e-5;
        constexpr float kPercent = 100.0f;

        using Snapshot = std::vector<std::pair<RE::TESGlobal*, float>>;

        bool Close(float actual, float expected)
        {
            const double scale = std::fabs(static_cast<double>(expected)) > 1.0 ? std::fabs(static_cast<double>(expected)) : 1.0;
            return std::fabs(static_cast<double>(actual) - static_cast<double>(expected)) <= kRelativeTolerance * scale;
        }

        // Puts every snapshotted global back when the check leaves, however it leaves
        class GlobalsRestore
        {
        public:
            explicit GlobalsRestore(const Snapshot& snapshot) : m_snapshot(snapshot) {}
            ~GlobalsRestore()
            {
                for (const auto& [global, value] : m_snapshot) global->value = value;
            }
            GlobalsRestore(const GlobalsRestore&) = delete;
            GlobalsRestore& operator=(const GlobalsRestore&) = delete;

        private:
            const Snapshot& m_snapshot;
        };

        // B's multiplier for the spell now, against the formula over the globals as they are
        bool MultiplierMatches(const RE::SpellItem* spell, const Memory::SpellMemory& memory, std::string& detail)
        {
            std::size_t known = 0;
            for (const std::size_t a : memory.archetypes) {
                const auto* global = Memory::Global(a, memory.rank);
                if (global && global->value > 0.0f) ++known;
            }
            const auto settings = Memory::CurrentSettings();
            const float share = static_cast<float>(known) / static_cast<float>(memory.archetypes.size());
            const float expected = settings.memoryToPractice ? 1.0f + settings.maxPracticeBoost / kPercent * share : 1.0f;
            const float actual = Memory::PracticeMultiplier(spell->GetFormID());
            detail = std::format("x{:.4f} (formula x{:.4f}, {} of {} known)", actual, expected, known, memory.archetypes.size());
            return Close(actual, expected);
        }
    }

    void CheckSpellResearchMemory()
    {
        std::string why;
        if (!Memory::Ready(why)) {
            Report(kId, Outcome::kSkip, why);
            return;
        }
        const auto* spell = Memory::SampleSpell();
        const auto memory = spell ? Memory::Lookup(spell) : std::nullopt;
        if (!memory) {
            Report(kId, Outcome::kSkip, "no spell in SR's tier lists has an archetype");
            return;
        }
        const float value = Memory::PaymentValue();
        if (!(value > 0.0f)) {
            Report(kId, Outcome::kSkip, "v is 0 (memoryMinutes 0 or SR's research time base 0)");
            return;
        }

        Snapshot snapshot;
        std::vector<std::size_t> ranks;   // each snapshot entry's rank
        for (const std::size_t a : memory->archetypes) {
            for (std::size_t t = 0; t < MM::kRanks; ++t) {
                if (auto* global = Memory::Global(a, t)) {
                    snapshot.emplace_back(global, global->value);
                    ranks.push_back(t);
                }
            }
        }

        const auto gains = MM::GainTable(memory->rank, value);
        std::size_t mismatches = 0;
        std::size_t expectedChanges = 0;
        std::size_t changed = 0;
        std::string firstMismatch;
        std::string paidB;
        bool paidBOk = false;
        {
            const GlobalsRestore restore(snapshot);
            changed = Memory::Pay(*memory, 1, value);
            for (std::size_t i = 0; i < snapshot.size(); ++i) {
                const auto& [global, before] = snapshot[i];
                const float expected = MM::ApplyGain(before, gains[ranks[i]]);
                if (expected != before) ++expectedChanges;
                if (!Close(global->value, expected) && mismatches++ == 0) {
                    firstMismatch = std::format("; first: {:08X} rank {} holds {:.4f}, math {:.4f}",
                        global->GetFormID(), ranks[i], global->value, expected);
                }
            }
            paidBOk = MultiplierMatches(spell, *memory, paidB);
        }

        // The guard has run: every global as it was
        std::size_t notRestored = 0;
        for (const auto& [global, before] : snapshot) {
            if (global->value != before) ++notRestored;
        }
        std::string restoredB;
        const bool restoredBOk = MultiplierMatches(spell, *memory, restoredB);

        const bool ok = mismatches == 0 && changed == expectedChanges && paidBOk && restoredBOk && notRestored == 0;
        Report(kId, ok ? Outcome::kPass : Outcome::kFail,
            std::format("{} ({:08X}) rank {}, {} archetypes, v {:.2f}: {} globals changed (math {}), {} mismatches{}, "
                        "{} not restored; B paid {}; B restored {}",
                spell->GetName(), spell->GetFormID(), memory->rank, memory->archetypes.size(), value, changed,
                expectedChanges, mismatches, firstMismatch, notRestored, paidB, restoredB));
    }
}
