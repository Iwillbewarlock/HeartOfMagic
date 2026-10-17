// =============================================================================
// Toxicology - dev self test: the tome in Elgrim's Elixirs (read only)
// =============================================================================
// See game/ToxSelfTestState.h for the split between the self test's files.
// Spec 4a: one tome in the shop's chest, teaching the Toxicology ability, value 350.
// Nothing here changes the world: the chest's inventory is read without being set up.

#include "game/ToxSelfTestState.h"

#include "game/ToxData.h"

#include <format>
#include <string_view>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // The placed chest Elgrim's Elixirs sells from (persistent reference, base container
    // 0x0A31AE MerchantRiftenElgrimsElixirsChest, the one ToxData.cpp PutTomeInShop fills)
    constexpr RE::FormID kElgrimChestRefId = 0x0A31BE;
    constexpr std::string_view kSkyrimEsm = "Skyrim.esm";
    constexpr std::int32_t kMinTomesInShop = 1;
    constexpr std::int32_t kTomeValue = 350;  // spec 4a; plugins/Toxicology/ESL.md
    constexpr std::string_view kStep = "4a shop";

    // RE/T/TESObjectREFR.h:409, src/RE/T/TESObjectREFR.cpp:430-479: the base container counted too;
    // a_noInit keeps a reference whose inventory was never set up untouched
    std::int32_t TomeCount(RE::TESObjectREFR& a_ref, RE::TESObjectBOOK* a_tome)
    {
        const auto counts = a_ref.GetInventoryCounts([a_tome](RE::TESBoundObject& a_object) { return &a_object == a_tome; }, true);
        const auto it = counts.find(a_tome);
        return it != counts.end() ? it->second : 0;
    }
}

void SelfTest::StepShop(State& s)
{
    auto* tome = ToxGame::Tome();
    if (!tome) {
        Fail(s, kStep, "no tome (the ESL's 0x803 is missing)");
        return;
    }
    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* chest = dh ? dh->LookupForm<RE::TESObjectREFR>(kElgrimChestRefId, kSkyrimEsm) : nullptr;  // RE/T/TESDataHandler.h:65
    if (!chest) {
        Fail(s, kStep, std::format("chest reference {:06X} ({}) not found", kElgrimChestRefId, kSkyrimEsm));
    } else {
        const std::int32_t count = TomeCount(*chest, tome);
        const auto detail = std::format("chest {} holds {} of {} (at least {} expected)", FormText(chest), count,
            FormText(tome), kMinTomesInShop);
        // Runs before Setup's AddSpell, so the ability and the carried tome are the save's own
        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool abilityHeld = ToxGame::PlayerHasAbility();
        const bool tomeCarried = player && TomeCount(*player, tome) > 0;
        if (count >= kMinTomesInShop) {
            Pass(s, kStep, detail);
        } else if (abilityHeld || tomeCarried) {
            Info(kStep, std::format("tome already bought in this save ({}); count check skipped - {}",
                abilityHeld ? "ability held" : "tome carried", detail));
        } else {
            Fail(s, kStep, detail);
        }
    }
    auto* teaches = tome->GetSpell();  // RE/T/TESObjectBOOK.h:106,112: null unless the teaches-spell flag is set
    Check(s, kStep, tome->TeachesSpell() && teaches && teaches == ToxGame::Ability(), std::format("{} teaches spell {}: "
        "{} (the ability is {})", FormText(tome), tome->TeachesSpell() ? "yes" : "no", FormText(teaches), FormText(ToxGame::Ability())));
    // RE/T/TESValueForm.h:25 (the record's value, no perks or barter)
    Check(s, kStep, tome->value == kTomeValue, std::format("{} value {} ({} expected)", FormText(tome), tome->value, kTomeValue));
}
