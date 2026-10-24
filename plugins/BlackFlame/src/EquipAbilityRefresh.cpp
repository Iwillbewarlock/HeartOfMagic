#include "EquipAbilityRefresh.h"

#include "Config.h"
#include "BuffState.h"
#include "VisualSet.h"
#include "VisualEquipContext.h"
#include "FormCopies.h"
#include "Forms.h"
#include "GameThread.h"
#include "MainThreadQueue.h"
#include "Patch.h"

#include <algorithm>
#include <vector>

namespace BlackFlame
{
    namespace
    {
        constexpr std::uint64_t kStartVisualsAe = 34094;  // 1.6.1170: ActiveEffect*, see diagnostics/equip-ability-refresh.md

        void LogModels(RE::ActiveEffect* a_effect, const char* a_stage)
        {
            if (!a_effect->hitEffects) {
                return;
            }
            for (auto* ref : *a_effect->hitEffects) {
                auto* model = ref ? ref->As<RE::ModelReferenceEffect>() : nullptr;
                if (model) {
                    logger::info("BlackFlame: equip ability visual {}: effect {}, art {:08X}, finished {}, has3D {}",
                        a_stage, a_effect->usUniqueID, model->artObject ? model->artObject->GetFormID() : 0,
                        model->finished, model->artObject3D != nullptr);
                }
            }
        }

        // An equip ability need not be in addedSpells. Keep its gameplay effect and replace only its visuals.
        void RefreshAbilityVisuals(RE::PlayerCharacter* a_player, RE::SpellItem* a_ability, const char* a_state)
        {
            if (REL::Module::get().version() != REL::Version(1, 6, 1170, 0) ||
                !Patch::HasIds({ kStartVisualsAe }, "equip ability visual refresh")) {
                logger::info("BlackFlame: equip ability refresh {:08X} ({}): visual restart unavailable on this runtime",
                    a_ability->GetFormID(), a_state);
                return;
            }
            auto* lists = RE::ProcessLists::GetSingleton();
            auto* target = a_player->AsMagicTarget();
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!lists || !effects || !a_player->Get3D()) {
                return;
            }
            const auto playerHandle = a_player->GetHandle();
            for (auto* effect : *effects) {
                if (!effect || effect->spell != a_ability || effect->target != target ||
                    effect->caster != playerHandle || effect->caster.native_handle() == 0 ||
                    effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    continue;
                }
                auto& controller = effect->hitEffectController;
                if (controller.effect != effect || (!controller.GetHitEffectArt() && !controller.GetHitEffectShader())) {
                    continue;
                }
                std::vector<RE::NiPointer<RE::ReferenceEffect>> previous;
                bool foreignOwner = false;
                bool ours = false;
                // As in HitArt: acquire references inside the manager lock; engine calls happen after it is released.
                lists->ForEachMagicTempEffect([&](RE::BSTempEffect* a_temp) {
                    auto* ref = netimmerse_cast<RE::ReferenceEffect*>(a_temp);
                    if (ref && ref->controller == &controller) {
                        foreignOwner |= ref->ownController;
                        if (const auto* model = ref->As<RE::ModelReferenceEffect>()) {
                            ours |= IsCopy(model->artObject);
                        }
                        if (const auto* shader = ref->As<RE::ShaderReferenceEffect>()) {
                            ours |= shader->effectData && shader->effectData == GetForms().burnShader;
                        }
                        previous.emplace_back(ref);
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
                if (foreignOwner || previous.empty() || (!IsPlayerVisualEquipEffect(effect) && !ours)) {
                    continue;  // unrelated shared users stay untouched; a previous copy may need restoring
                }
                LogModels(effect, "before");
                controller.Stop();  // remove manager ownership before Detach clears each ref's controller
                for (auto& ref : previous) {
                    ref->finished = true;
                    ref->Detach();  // cancels the clone, detaches geometry and removes this ref from hitEffects
                }
                if ((effect->hitEffects && !effect->hitEffects->empty()) ||
                    std::ranges::any_of(previous, [&](const auto& a_ref) { return a_ref->controller == &controller; })) {
                    logger::warn("BlackFlame: equip ability refresh {:08X} ({}): old visuals still linked; not restarted",
                        a_ability->GetFormID(), a_state);
                    continue;
                }
                // The engine prepares the hitEffects LIST. CommonLib's Start(ReferenceEffect**) declaration does
                // not describe that second argument correctly; never pass a local ReferenceEffect* output there.
                using StartVisuals = void (*)(RE::ActiveEffect*);
                const REL::Relocation<StartVisuals> start{ REL::ID(kStartVisualsAe) };
                start(effect);
                logger::info("BlackFlame: equip ability refresh {:08X} ({}): visual restart requested, effect {}, "
                             "{} old refs detached; gameplay effect kept",
                    a_ability->GetFormID(), a_state, effect->usUniqueID, previous.size());
                LogModels(effect, "after");  // creation/3D can be asynchronous; this is not an on-screen PASS
            }
        }

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
                if (spell && IsVisualEquipAbility(spell) && std::ranges::find(abilities, spell) == abilities.end()) {
                    abilities.push_back(spell);
                }
            }
            return abilities;
        }

        void Refresh(bool a_stoked)
        {
            const auto& config = GetConfig();
            const char* state = a_stoked ? "buff on" : "buff off";
            if (!config.copyHitArt && !config.hitArt && !config.burnShader) {
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
                RefreshAbilityVisuals(player, ability, state);
            }
        }
    }

    void RequestEquipAbilityRefresh(bool a_stoked)
    {
        const auto generation = WorldGeneration();
        if (!MainQueue::Post("equip ability refresh", [a_stoked, generation]() {
                if (MainQueue::Installed() && IsOnGameThread() && generation == WorldGeneration() && a_stoked == IsStoked()) {
                    Refresh(a_stoked);
                }
            })) {
            logger::warn("BlackFlame: equip ability refresh ({}): not queued", a_stoked ? "buff on" : "buff off");
        }
    }
}
