// =============================================================================
// Toxicology - dev self test: step 7, Magicka spent -> bottles
// =============================================================================
// AddSpentMagicka uses up bottles of the copied poison and moves on to the next
// favorited one. Before each call the selection order must start with the two
// test poisons (A, then B): otherwise a favorited poison of the player's own
// would be next and could be used up, so that part is skipped (FAIL 7 setup).
// See game/ToxSelfTestState.h for the split between the self test's files.

#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxMagicka.h"
#include "game/ToxSelection.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;

namespace
{
    constexpr std::int32_t kDoseBottlesA = 2;  // spec 9.7
    constexpr std::int32_t kDoseBottlesB = 3;
    constexpr float kDosesFirst = 2.5f;        // x magickaPerDose: A's two bottles, half a dose left
    constexpr float kDosesSpan = 2.0f;         // x magickaPerDose: A's last bottle, then one of B
    constexpr float kHalf = 0.5f;

    struct Ranked
    {
        RE::AlchemyItem* item = nullptr;
        std::string lower;
    };

    // The player's favorited poisons with bottles, in the order the addon picks them: lowercased display
    // name, then FormID (core PickTopPoison; ToxSelection.cpp PickFromInventory reads the same fields)
    std::vector<Ranked> SelectionOrder()
    {
        std::vector<Ranked> out;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return out;
        auto inventory = player->GetInventory([](RE::TESBoundObject& a_object) {  // RE/T/TESObjectREFR.h:406
            const auto* item = a_object.As<RE::AlchemyItem>();
            return item && item->IsPoison();  // RE/A/AlchemyItem.h:72
        });
        for (auto& [object, data] : inventory) {
            auto& [count, entry] = data;
            if (count <= 0 || !entry || !entry->IsFavorited()) continue;  // RE/I/InventoryEntryData.h:50
            const char* name = entry->GetDisplayName();                   // RE/I/InventoryEntryData.h:39
            out.push_back({ object->As<RE::AlchemyItem>(), SelfTest::LowerAscii(name ? name : "") });
        }
        std::sort(out.begin(), out.end(), [](const Ranked& a_l, const Ranked& a_r) {
            return a_l.lower != a_r.lower ? a_l.lower < a_r.lower : a_l.item->GetFormID() < a_r.item->GetFormID();
        });
        return out;
    }

    // True when the order starts A, B; otherwise FAIL a_step naming what comes first
    bool OrderStartsAB(State& s, std::string_view a_step)
    {
        const auto order = SelectionOrder();
        if (order.size() >= 2 && order[0].item == s.poisonA && order[1].item == s.poisonB) return true;
        std::string first;
        for (std::size_t i = 0; i < order.size() && i < 3; ++i) {
            first += std::format("{}{}", first.empty() ? "" : ", ", SelfTest::FormText(order[i].item));
        }
        SelfTest::Fail(s, a_step, std::format("the favorited poisons in pick order start with {} - not {} then {}: a poison of the "
            "player's own would be next and could be used up, so this part is skipped", first.empty() ? "none" : first,
            SelfTest::FormText(s.poisonA), SelfTest::FormText(s.poisonB)));
        return false;
    }

    // A x a_bottlesA, B x a_bottlesB (favorited), count 0, A chosen, A then B first in the order
    bool Prepare(State& s, std::string_view a_step, std::int32_t a_bottlesA, std::optional<std::int32_t> a_bottlesB)
    {
        std::string why;
        if (!SelfTest::GiveFavorited(s.poisonA, a_bottlesA, why) || (a_bottlesB && !SelfTest::GiveFavorited(s.poisonB, *a_bottlesB, why))) {
            SelfTest::Fail(s, a_step, why);
            return false;
        }
        ToxGame::SetAccumulated(0.0f);
        ToxGame::OnSelectionMaybeChanged();
        if (ToxGame::CurrentPoison(true) != s.poisonA) {
            SelfTest::Fail(s, a_step, std::format("the copied poison is {}, not {}", SelfTest::FormText(ToxGame::CurrentPoison(true)),
                SelfTest::FormText(s.poisonA)));
            return false;
        }
        return OrderStartsAB(s, a_step);
    }
}

void SelfTest::StepDoses(State& s)
{
    s.dosesRan = true;
    const float perDose = ToxGame::GetSettings().magickaPerDose;
    if (!Prepare(s, "7 setup", kDoseBottlesA, kDoseBottlesB)) return;
    const float spent = perDose * kDosesFirst;
    ToxGame::AddSpentMagicka(spent);
    const auto a = BottleCount(s.poisonA);
    const auto b = BottleCount(s.poisonB);
    const float left = ToxGame::Accumulated();
    auto* next = ToxGame::CurrentPoison(true);
    Check(s, "7 bottles", a == 0 && b == kDoseBottlesB, std::format("spent {:.1f} ({} x magickaPerDose {:.1f}): A {} -> {}, "
        "B {} -> {} (expected 0 and {})", spent, kDosesFirst, perDose, kDoseBottlesA, a, kDoseBottlesB, b, kDoseBottlesB));
    Check(s, "7 remainder", std::fabs(left - perDose * kHalf) < kMagickaTolerance,
        std::format("count toward the next bottle {:.2f} (expected {:.2f})", left, perDose * kHalf));
    Check(s, "7 next poison", next == s.poisonB,
        std::format("copied poison after A ran out: {} (expected {})", FormText(next), FormText(s.poisonB)));
    CheckKeyword(s, "7 keyword", s.poisonB, s.poisonA);
    if (next != s.poisonB) {
        Info("7 span", "skipped: the next poison was not B (see 7 next poison above)");
        return;
    }

    // Doses that cross from one poison to the next in one call; B keeps the bottles the first part left it
    const auto bBefore = BottleCount(s.poisonB);
    if (!Prepare(s, "7 setup", 1, std::nullopt)) return;
    ToxGame::AddSpentMagicka(perDose * kDosesSpan);
    const auto a2 = BottleCount(s.poisonA);
    const auto b2 = BottleCount(s.poisonB);
    const float left2 = ToxGame::Accumulated();
    Check(s, "7 doses span poisons", a2 == 0 && b2 == bBefore - 1 && std::fabs(left2) < kMagickaTolerance,
        std::format("{} x magickaPerDose with A x1, B x{}: A {}, B {}, count {:.2f} (expected 0, {}, 0)", kDosesSpan, bBefore, a2,
            b2, left2, bBefore - 1));
}
