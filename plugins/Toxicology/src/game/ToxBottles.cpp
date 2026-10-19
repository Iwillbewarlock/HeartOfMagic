#include "game/ToxBottles.h"

#include "core/ToxCore.h"
#include "game/ToxSelection.h"

#include <vector>

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // Safety caps on the bottle loops (each step removes at least one bottle or stops)
    constexpr int kMaxRemoveSteps = 64;
    constexpr int kMaxPoisonSwitches = 64;

    // =========================================================================
    // Stacks (spec 12)
    // =========================================================================

    struct PoisonStacks
    {
        std::vector<Toxicology::BottleStack> stacks;
        std::vector<RE::ExtraDataList*> lists;  // parallel to stacks; nullptr = bottles outside any list
        std::int32_t total = 0;
        std::int32_t favorited = 0;
    };

    // The player's bottles of a_poison by stack: each ExtraDataList of the live inventory entry, then
    // the bottles in no list (total minus the lists' counts)
    PoisonStacks ReadStacks(RE::PlayerCharacter& a_player, RE::AlchemyItem* a_poison)
    {
        PoisonStacks out;
        // RE/T/TESObjectREFR.h:409: counts with the base container included
        const auto counts = a_player.GetInventoryCounts([a_poison](RE::TESBoundObject& a_object) { return &a_object == a_poison; });
        if (const auto it = counts.find(a_poison); it != counts.end()) out.total = it->second;
        if (out.total <= 0) return out;

        std::int32_t listed = 0;
        auto* changes = a_player.GetInventoryChanges();  // RE/T/TESObjectREFR.h:410
        if (changes && changes->entryList) {             // RE/I/InventoryChanges.h:101
            for (auto* entry : *changes->entryList) {
                if (!entry || entry->object != a_poison || !entry->extraLists) continue;  // RE/I/InventoryEntryData.h:67-69
                for (auto* list : *entry->extraLists) {
                    if (!list) continue;
                    const auto count = list->GetCount();  // RE/E/ExtraDataList.h:201 (no ExtraCount = 1)
                    const bool favorited = list->HasType<RE::ExtraHotkey>();  // RE/E/ExtraDataList.h:183, ExtraHotkey.h:13
                    out.stacks.push_back({ count, favorited });
                    out.lists.push_back(list);
                    listed += count;
                    if (favorited) out.favorited += count;
                }
                break;
            }
        }
        if (out.total > listed) {
            out.stacks.push_back({ out.total - listed, false });
            out.lists.push_back(nullptr);
        }
        return out;
    }

    // Removes up to a_want bottles of a_poison, unfavorited stacks first (core NextDoseStack).
    // Returns how many went.
    std::int32_t RemoveBottles(RE::PlayerCharacter& a_player, RE::AlchemyItem* a_poison, std::int32_t a_want)
    {
        std::int32_t taken = 0;
        for (int step = 0; step < kMaxRemoveSteps && taken < a_want; ++step) {
            const auto before = ReadStacks(a_player, a_poison);
            const auto take = Toxicology::NextDoseStack(before.stacks, a_want - taken);
            if (!take) break;
            auto* list = before.lists[take->index];
            const bool fromFavorited = before.stacks[take->index].favorited;
            // RE/A/Actor.h:318 (TESObjectREFR.h:266); no message is shown (SpellTomeHook.cpp:183 precedent)
            a_player.RemoveItem(a_poison, take->count, RE::ITEM_REMOVE_REASON::kRemove, list, nullptr);
            const auto after = ReadStacks(a_player, a_poison);
            const auto removed = before.total - after.total;
            if (removed <= 0) {
                logger::error("Toxicology: RemoveItem took no bottle of {} {:08X} ({} asked, {} held) - stopped",
                    a_poison->GetName(), a_poison->GetFormID(), take->count, before.total);
                break;
            }
            if (!fromFavorited && after.favorited < before.favorited) {
                logger::warn("Toxicology: removing {} unfavorited bottle(s) of {} {:08X} ({}) shrank the favorited stack {} -> {}",
                    take->count, a_poison->GetName(), a_poison->GetFormID(), list ? "listed stack" : "stack outside any list",
                    before.favorited, after.favorited);
                // The favorite may be gone with it: choose again; UseDoses takes the rest from the new choice
                ToxGame::OnSelectionMaybeChanged();
                taken += removed;
                break;
            }
            logger::debug("Toxicology: {} bottle(s) of {:08X} removed from {} {} stack",
                removed, a_poison->GetFormID(), fromFavorited ? "the favorited" : "an unfavorited", list ? "listed" : "unlisted");
            taken += removed;
        }
        return taken;
    }
}

// =============================================================================
// Public
// =============================================================================

void ToxGame::UseDoses(std::int32_t a_doses)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    std::int32_t left = a_doses;
    for (int round = 0; round < kMaxPoisonSwitches && left > 0; ++round) {
        auto* poison = CurrentPoison(true);
        if (!poison) {
            logger::info("Toxicology: {} dose(s) not taken - no poison left to copy", left);
            return;
        }
        const auto taken = RemoveBottles(*player, poison, left);
        left -= taken;
        const auto remaining = ReadStacks(*player, poison).total;
        logger::info("Toxicology: {} bottle(s) of {} {:08X} used up by poison spells, {} left",
            taken, poison->GetName(), poison->GetFormID(), remaining);
        if (remaining <= 0) {
            ToxGame::OnSelectionMaybeChanged();  // spec 5.6: on to the next poison, keyword moved
        } else if (taken <= 0) {
            return;  // RemoveBottles logged why
        }
    }
}
