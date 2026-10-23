#include "EquipAbilityRefresh.h"

#include "Config.h"
#include "FireSet.h"
#include "MainThreadQueue.h"

#include <algorithm>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        // The distinct fire equip abilities among the player's live active effects
        std::vector<RE::SpellItem*> LiveFireEquipAbilities(RE::PlayerCharacter* a_player)
        {
            std::vector<RE::SpellItem*> abilities;
            auto* target = a_player->AsMagicTarget();
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!effects) {
                return abilities;
            }
            for (const auto* effect : *effects) {
                if (!effect || effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    continue;
                }
                auto* spell = effect->spell ? effect->spell->As<RE::SpellItem>() : nullptr;
                if (spell && IsFireEquipAbility(spell) && std::ranges::find(abilities, spell) == abilities.end()) {
                    abilities.push_back(spell);
                }
            }
            return abilities;
        }

        void Refresh(bool a_stoked)
        {
            const auto& config = GetConfig();
            const char* state = a_stoked ? "buff on" : "buff off";
            if (!config.copyHitArt && !config.hitArt) {  // the art slot changes nothing then
                logger::info("BlackFlame: equip ability refresh ({}): art disabled", state);
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::info("BlackFlame: equip ability refresh ({}): no player", state);
                return;
            }
            const auto abilities = LiveFireEquipAbilities(player);
            logger::info("BlackFlame: equip ability refresh ({}): {} live fire equip abilities", state, abilities.size());
            for (auto* ability : abilities) {
                const auto id = ability->GetFormID();
                if (!player->HasSpell(ability)) {
                    logger::info("BlackFlame: equip ability refresh {:08X} ({}): not in the actor's spell list: left as is",
                        id, state);
                    continue;
                }
                if (!player->RemoveSpell(ability)) {
                    logger::warn("BlackFlame: equip ability refresh {:08X} ({}): RemoveSpell returned false: not added again",
                        id, state);
                    continue;
                }
                const bool added = player->AddSpell(ability);
                logger::info("BlackFlame: equip ability refresh {:08X} ({}): removed and added again (AddSpell {})", id,
                    state, added);
            }
        }
    }

    void RequestEquipAbilityRefresh(bool a_stoked)
    {
        if (!MainQueue::Post("equip ability refresh", [a_stoked]() { Refresh(a_stoked); })) {
            logger::warn("BlackFlame: equip ability refresh ({}): not queued", a_stoked ? "buff on" : "buff off");
        }
    }
}
