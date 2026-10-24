#include "VisualEquipContext.h"

#include "BuffState.h"
#include "EquipAbilityRefresh.h"
#include "GameThread.h"
#include "MainThreadQueue.h"
#include "VisualEquipRules.h"
#include "VisualSet.h"

#include <array>

namespace BlackFlame
{
    namespace
    {
        using Selection = std::array<RE::FormID, RE::Actor::SlotTypes::kTotal>;
        Selection g_selected{};  // only the main-thread ticker reads/writes these
        std::uint64_t g_generation = 0;
    }

    bool IsPlayerVisualEquipEffect(RE::ActiveEffect* a_effect)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_effect || !player || !IsVisualEquipAbility(a_effect->spell) ||
            a_effect->target != player->AsMagicTarget() || a_effect->caster.native_handle() == 0 ||
            a_effect->caster != player->GetHandle() ||
            a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
            return false;
        }
        std::vector<VisualEquipRules::SelectedRoot> selected;
        selected.reserve(RE::Actor::SlotTypes::kTotal);
        // Like the casting-art hook, use the engine's current selected spell pointers. Forms are
        // persistent; caching the decision until the next tick would leak grey across an equip change.
        for (const auto* spell : player->GetActorRuntimeData().selectedSpells) {
            if (spell) {
                selected.push_back({ spell->GetFormID(), IsVisualSpell(spell) });
            }
        }
        return VisualEquipRules::Allows(VisualEquipOwners(a_effect->spell), selected);
    }

    void TickVisualEquipContext()
    {
        if (!MainQueue::Installed() || !IsOnGameThread()) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        Selection selected{};
        std::size_t i = 0;
        for (const auto* spell : player->GetActorRuntimeData().selectedSpells) {
            selected[i++] = spell ? spell->GetFormID() : 0;
        }
        const auto generation = WorldGeneration();
        if (generation != g_generation || selected != g_selected) {
            g_selected = selected;
            g_generation = generation;
            RequestEquipAbilityRefresh(IsStoked());
        }
    }

    void ResetVisualEquipContext()
    {
        g_selected = {};
        g_generation = 0;
    }
}
