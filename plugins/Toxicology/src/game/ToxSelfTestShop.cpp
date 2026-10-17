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
        // RE/T/TESObjectREFR.h:409, src/RE/T/TESObjectREFR.cpp:430-479: the base container counted too;
        // a_noInit keeps a chest whose inventory was never set up untouched
        const auto counts = chest->GetInventoryCounts([tome](RE::TESBoundObject& a_object) { return &a_object == tome; }, true);
        const auto it = counts.find(tome);
        const std::int32_t count = it != counts.end() ? it->second : 0;
        Check(s, kStep, count >= kMinTomesInShop, std::format("chest {} holds {} of {} (at least {} expected; 0 after the "
            "player bought it in this save)", FormText(chest), count, FormText(tome), kMinTomesInShop));
    }
    auto* teaches = tome->GetSpell();  // RE/T/TESObjectBOOK.h:106,112: null unless the teaches-spell flag is set
    Check(s, kStep, tome->TeachesSpell() && teaches && teaches == ToxGame::Ability(), std::format("{} teaches spell {}: "
        "{} (the ability is {})", FormText(tome), tome->TeachesSpell() ? "yes" : "no", FormText(teaches), FormText(ToxGame::Ability())));
    // RE/T/TESValueForm.h:25 (the record's value, no perks or barter)
    Check(s, kStep, tome->value == kTomeValue, std::format("{} value {} ({} expected)", FormText(tome), tome->value, kTomeValue));
}
