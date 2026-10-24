#include "VisualSet.h"

#include <algorithm>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        std::vector<RE::SpellItem*> g_candidates;
        std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> g_runtimeEffects;
        std::unordered_set<RE::FormID> g_candidateEffects;
        std::unordered_set<RE::FormID> g_equipAbilities;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> g_equipOwners;
        std::unordered_map<RE::FormID, RE::BGSArtObject*> g_castingArts;

        struct Reach
        {
            RE::SpellItem* spell = nullptr;
            bool catalog = false;
            bool other = false;
        };
    }

    void BuildVisualSets(const std::unordered_set<RE::FormID>& a_catalogFire)
    {
        g_candidates.clear();
        g_runtimeEffects.clear();
        g_candidateEffects.clear();
        g_equipAbilities.clear();
        g_equipOwners.clear();
        g_castingArts.clear();
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            logger::error("BlackFlame: visual selection: no data handler; all visuals left unchanged");
            return;
        }

        std::unordered_map<RE::FormID, Reach> reached;
        std::size_t catalogRoots = 0;
        // This load-time traversal is O(roots * reachable graph). Per-root visited sets break cycles;
        // if large shared cloak graphs become costly, propagate root sets over their condensed graph.
        for (auto* root : handler->GetFormArray<RE::SpellItem>()) {
            if (!root || root->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
                continue;
            }
            const auto rootID = root->GetFormID();
            const bool catalog = a_catalogFire.contains(rootID);
            catalogRoots += catalog ? 1 : 0;
            std::vector<RE::SpellItem*> pending{ root };
            std::unordered_set<RE::FormID> visited;
            while (!pending.empty()) {
                auto* spell = pending.back();
                pending.pop_back();
                if (!visited.insert(spell->GetFormID()).second) {
                    continue;
                }
                auto& reach = reached[spell->GetFormID()];
                reach.spell = spell;
                reach.catalog |= catalog;
                reach.other |= !catalog;
                for (const auto* item : spell->effects) {
                    const auto* base = item ? item->baseEffect : nullptr;
                    if (!base) {
                        continue;
                    }
                    if (const auto* ability = base->data.equipAbility) {
                        g_equipOwners[ability->GetFormID()].push_back(rootID);
                    }
                    if (base->HasArchetype(RE::EffectSetting::Archetype::kCloak) && base->data.associatedForm) {
                        if (auto* child = base->data.associatedForm->As<RE::SpellItem>()) {
                            pending.push_back(child);
                        }
                    }
                }
            }
        }

        std::size_t ambiguous = 0;
        for (const auto& [id, reach] : reached) {
            if (!reach.catalog) {
                continue;
            }
            auto* spell = reach.spell;
            g_candidates.push_back(spell);
            ambiguous += reach.other ? 1 : 0;
            // Empty-effect spells still have a meaningful runtime spell classification.
            auto* runtime = reach.other ? nullptr : &g_runtimeEffects[id];
            for (const auto* item : spell->effects) {
                const auto* base = item ? item->baseEffect : nullptr;
                if (!base) {
                    continue;
                }
                g_candidateEffects.insert(base->GetFormID());
                if (runtime) {
                    runtime->insert(base->GetFormID());
                }
                if (const auto* ability = base->data.equipAbility) {
                    g_equipAbilities.insert(ability->GetFormID());
                }
            }
            if (const auto* costliest = spell->GetAVEffect(); costliest && costliest->data.castingArt) {
                g_castingArts.emplace(id, costliest->data.castingArt);
            }
        }
        std::ranges::sort(g_candidates, {}, [](const auto* spell) { return spell->GetFormID(); });
        for (auto& [ability, owners] : g_equipOwners) {
            std::ranges::sort(owners);
            const auto duplicate = std::ranges::unique(owners);
            owners.erase(duplicate.begin(), duplicate.end());
        }
        logger::info("BlackFlame: visual selection (HoM catalog only): {} roots, {} copy-candidate spells, {} runtime "
                     "spells, {} shared with non-catalog roots kept original, {} candidate MGEFs, {} equip abilities",
            catalogRoots, g_candidates.size(), g_runtimeEffects.size(), ambiguous, g_candidateEffects.size(),
            g_equipAbilities.size());
    }

    bool IsVisualSpell(const RE::MagicItem* a_spell)
    {
        return a_spell && g_runtimeEffects.contains(a_spell->GetFormID());
    }

    bool IsVisualEffect(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell)
    {
        if (!a_effect || !a_spell) {
            return false;
        }
        const auto found = g_runtimeEffects.find(a_spell->GetFormID());
        return found != g_runtimeEffects.end() && found->second.contains(a_effect->GetFormID());
    }

    const std::vector<RE::SpellItem*>& VisualSpellList() noexcept
    {
        return g_candidates;
    }

    bool IsVisualEffectForm(const RE::EffectSetting* a_effect)
    {
        return a_effect && g_candidateEffects.contains(a_effect->GetFormID());
    }

    RE::BGSArtObject* CostliestVisualCastingArtOf(const RE::MagicItem* a_spell)
    {
        const auto found = a_spell ? g_castingArts.find(a_spell->GetFormID()) : g_castingArts.end();
        return found != g_castingArts.end() ? found->second : nullptr;
    }

    bool IsVisualEquipAbility(const RE::MagicItem* a_ability)
    {
        return a_ability && g_equipAbilities.contains(a_ability->GetFormID());
    }

    const std::vector<RE::FormID>& VisualEquipOwners(const RE::MagicItem* a_ability) noexcept
    {
        static const std::vector<RE::FormID> empty;
        const auto found = a_ability ? g_equipOwners.find(a_ability->GetFormID()) : g_equipOwners.end();
        return found != g_equipOwners.end() ? found->second : empty;
    }
}
