#include "environment/EnvironmentSelfTest.h"
#include "FormIdText.h"
#include "MasteryMath.h"
#include "ProgressionManager.h"
#include "SpellCastHandler.h"
#include "SpellScanner.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <format>
#include <string>

// =============================================================================
// EnvironmentSelfTestMastery - the mastery ranks checks of the dev self-test
//
//   mastery-cast  A tree spell the player knows and has mastered (MasteryKeyForCast answers it),
//                 its mastery XP set to 0 and the learning targets set aside (so the casts pay no
//                 learning XP). One cast reported both ways - the cast event's path and the
//                 immediate-cast hook's, the same moment - through SpellCastHandler's own handler
//                 (HandleCastForSelfTest: the work its queued tasks run, the one-cast-one-credit
//                 pairing included): the spell must hold exactly one cast's mastery XP, worked out
//                 here from MasteryMath (Magicka cost / 10 at least 1, xpRate, the base school skill's
//                 factor), at the rank MasteryMath gives; and the panel's progress reply must carry it
//                 in spellMastery. A second cast reported the same way 2 s apart (outside the pairing
//                 window; dated in the past, so nothing is left pending for a real cast to pair with)
//                 adds a second one, held at the ranks' total.
//   mastery-gate  A spell the player knows that is not a tree spell earns no mastery XP
//                 (MasteryKeyForCast 0). SKIP without one.
//   Not covered here: the cast event's own thread hop (ProcessEvent posts the same handler as a task),
//   the save (SLMX is checked offline: progression-check), a weakened or ISL-held spell, and the
//   99% -> 100% cast (the order is in OnSpellCast: MasteryKeyForCast before the learning XP).
//   The spell's mastery XP and the targets are put back by RAII guards, however the check leaves.
//   A cast large enough to reach a rank shows the rank-up corner message once.
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        constexpr const char* kCastId = "mastery-cast";
        constexpr const char* kGateId = "mastery-gate";
        constexpr float kMinBaseXP = 1.0f;          // SpellCastHandler: max(1, cost / 10)
        constexpr float kCostPerXP = 10.0f;
        constexpr float kTolerance = 1e-3f;
        // Two reports of one cast are paired within SpellCastHandler's 0.1 s window: a cast this far
        // apart is a cast of its own. Dated this far in the past, not the future: a future time left in
        // the handler's pending list could pair with (and swallow) a real hook report of the spell then
        constexpr auto kOtherCast = std::chrono::seconds(2);

        class MasteryRestore
        {
        public:
            explicit MasteryRestore(RE::FormID spellId) :
                m_spellId(spellId), m_before(ProgressionManager::GetSingleton()->GetMasteryXP(spellId))
            {}
            ~MasteryRestore() { ProgressionManager::GetSingleton()->SetMasteryXPForTest(m_spellId, m_before); }
            MasteryRestore(const MasteryRestore&) = delete;
            MasteryRestore& operator=(const MasteryRestore&) = delete;
            float Before() const { return m_before; }

        private:
            RE::FormID m_spellId;
            float m_before;
        };

        bool Near(float a, float b)
        {
            return std::fabs(a - b) <= kTolerance * (std::fabs(b) > 1.0f ? std::fabs(b) : 1.0f);
        }

        // A kSpell with a magic school, so the handler does not drop it before OnSpellCast
        bool Castable(RE::SpellItem* spell)
        {
            return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell &&
                   !ProgressionManager::TargetSchoolOf(spell).empty();
        }

        RE::SpellItem* PickMastered(ProgressionManager* pm, RE::PlayerCharacter* player)
        {
            for (const RE::FormID id : pm->GetTreeSpells()) {
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
                if (Castable(spell) && player->HasSpell(spell) && pm->MasteryKeyForCast(id) == id) return spell;
            }
            return nullptr;
        }

        float ExpectedGain(RE::SpellItem* spell, RE::PlayerCharacter* player, const MasteryMath::Settings& settings)
        {
            const float base = (std::max)(kMinBaseXP, spell->CalculateMagickaCost(player) / kCostPerXP);
            const auto school = SpellScanner::GetSpellSchool(spell);
            auto* owner = player->AsActorValueOwner();
            const float skill = owner && SpellScanner::IsValidMagicSchool(school) ? owner->GetBaseActorValue(school) : 0.0f;
            return MasteryMath::GainFor(base, skill, settings);
        }

        float ReplyXP(ProgressionManager* pm, RE::FormID id)
        {
            const auto reply = nlohmann::json::parse(pm->GetProgressJSON(), nullptr, false);
            if (reply.is_discarded() || !reply.contains("spellMastery")) return -1.0f;
            const auto& map = reply["spellMastery"];
            const auto entry = map.find(FormIdText::Format(id));
            if (entry == map.end() || !entry->is_object() || !entry->contains("xp")) return 0.0f;
            return (*entry)["xp"].get<float>();
        }

        void CheckCast(ProgressionManager* pm, RE::PlayerCharacter* player)
        {
            auto* spell = PickMastered(pm, player);
            if (!spell) {
                Report(kCastId, Outcome::kSkip, "no mastered tree spell the player knows earns mastery XP now (ranks off, or none known)");
                return;
            }
            const RE::FormID id = spell->GetFormID();
            const auto& settings = pm->GetMasterySettings();
            const auto tier = MasteryMath::TierIndex(SpellScanner::DetermineSpellTier(spell));
            const float total = MasteryMath::TotalXP(settings, tier);
            const float gain = ExpectedGain(spell, player, settings);

            const MasteryRestore restore(id);
            const ProgressionManager::LearningTargetsAside targetsAside(*pm);
            pm->SetMasteryXPForTest(id, 0.0f);

            auto* handler = SpellCastHandler::GetSingleton();
            const auto castAt = std::chrono::steady_clock::now();
            handler->HandleCastForSelfTest(id, false, castAt);   // the cast event's report
            handler->HandleCastForSelfTest(id, true, castAt);    // the immediate-cast hook's report of the same cast
            const float once = pm->GetMasteryXP(id);
            const float expectedOnce = (std::min)(gain, total);
            const int rank = pm->GetMasteryRank(id);
            const float reply = ReplyXP(pm, id);
            handler->HandleCastForSelfTest(id, false, castAt - kOtherCast);   // a cast of its own
            const float twice = pm->GetMasteryXP(id);
            const float expectedTwice = (std::min)(2.0f * gain, total);

            const bool ok = Near(once, expectedOnce) && rank == MasteryMath::RankFor(expectedOnce, settings, tier) &&
                            Near(reply, once) && Near(twice, expectedTwice);
            Report(kCastId, ok ? Outcome::kPass : Outcome::kFail,
                std::format("{} ({:08X}, {}): one cast reported twice -> {:.3f} XP (expected {:.3f}, rank {}), progress reply "
                            "{:.3f}, a second cast -> {:.3f} (expected {:.3f}, total {:.0f}); mastery XP before the test {:.3f}, "
                            "put back",
                    spell->GetName(), id, SpellScanner::DetermineSpellTier(spell), once, expectedOnce, rank, reply, twice,
                    expectedTwice, total, restore.Before()));
        }

        void CheckGate(ProgressionManager* pm, RE::PlayerCharacter* player)
        {
            RE::SpellItem* outside = nullptr;
            for (auto* spell : player->GetActorRuntimeData().addedSpells) {
                if (Castable(spell) && !pm->IsTreeSpell(spell->GetFormID()) &&
                    !pm->IsTreeSpell(SpellScanner::TreeSpellForTome(spell)->GetFormID())) {
                    outside = spell;
                    break;
                }
            }
            if (!outside) {
                Report(kGateId, Outcome::kSkip, "the player knows no castable spell outside the tree");
                return;
            }
            const RE::FormID key = pm->MasteryKeyForCast(outside->GetFormID());
            Report(kGateId, key == 0 ? Outcome::kPass : Outcome::kFail,
                std::format("{} ({:08X}), not in the tree: earns for {:08X} (expected none)", outside->GetName(),
                    outside->GetFormID(), key));
        }
    }

    void CheckMasteryRanks()
    {
        auto* pm = ProgressionManager::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!pm || !player) {
            Report(kCastId, Outcome::kSkip, "no player");
            return;
        }
        CheckCast(pm, player);
        CheckGate(pm, player);
    }
}
