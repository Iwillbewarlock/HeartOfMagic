#include "FireSet.h"
#include "CatalogFile.h"
#include "VisualSet.h"

#include "EquipAbilityRules.h"

#include "FormCopies.h"
#include "Forms.h"
#include "PluginNames.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#ifdef GetObject
#    undef GetObject  // windows.h: BGSDefaultObjectManager::GetObject below
#endif

namespace BlackFlame
{
    namespace
    {
        constexpr std::string_view kFireElement = "fire";
        constexpr std::string_view kHexPrefix = "0x";
        constexpr std::string_view kHexPrefixUpper = "0X";
        constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;    // a full plugin's local id: 24 bits
        constexpr std::uint32_t kMaxLightLocalId = 0xFFF;  // a light plugin's: 12 bits (SpellScannerFormId.cpp writes the masked id)
        constexpr int kHexBase = 16;

        std::unordered_set<RE::FormID> g_fireSpells;
        std::unordered_set<RE::FormID> g_fireExplosions;
        std::unordered_set<RE::FormID> g_fireEffects;  // fire MGEFs (keyword or fire resistance) of fire spells
        std::unordered_set<RE::FormID> g_fireHazards;  // hazards those MGEFs can leave
        std::unordered_set<RE::FormID> g_fireEquipAbilities;  // data.equipAbility of fire MGEFs (EquipAbilityRules.h)
        std::unordered_set<RE::FormID> g_cloakSpells;  // fire cloaks' associated spells (AddCloakSpells)
        std::unordered_map<RE::FormID, RE::BGSArtObject*> g_castingArts;  // F5: fire spell -> its costliest effect's casting art
        FireSetCounts g_counts;
        std::vector<BoundFireSeed> g_boundSeeds;          // bound-weapon stage 1
        std::unordered_set<RE::FormID> g_boundWeapons;   // their weapons

        // The bound fire weapon seeds (see FireSet.h): after the fire effects are known
        void BuildBoundFireSeeds()
        {
            g_boundSeeds.clear();
            g_boundWeapons.clear();
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return;
            }
            std::unordered_map<RE::FormID, std::size_t> byEffect;  // MGEF -> index in g_boundSeeds
            for (auto* mgef : dh->GetFormArray<RE::EffectSetting>()) {
                if (!mgef || !mgef->HasArchetype(RE::EffectSetting::Archetype::kBoundWeapon)) {
                    continue;
                }
                auto* weapon = mgef->data.associatedForm ? mgef->data.associatedForm->As<RE::TESObjectWEAP>() : nullptr;
                if (!weapon || !weapon->IsBound()) {
                    continue;
                }
                const bool fireEffect = IsFireEffect(mgef);
                const bool fireEnchantment = HasFireEnchantment(weapon);
                if (!fireEffect && !fireEnchantment) {
                    continue;
                }
                byEffect.emplace(mgef->GetFormID(), g_boundSeeds.size());
                g_boundSeeds.push_back({ mgef, weapon, nullptr, fireEffect, fireEnchantment });
                g_boundWeapons.insert(weapon->GetFormID());
            }
            // A spell for each seed (the self-test casts it): a fire spell first, else any castable spell (J9: SpellType kSpell,
            // no ability, power or disease) carrying the effect
            for (const bool fireOnly : { true, false }) {
                for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
                    if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell || (fireOnly && !IsFireSpell(spell))) {
                        continue;
                    }
                    for (const auto* item : spell->effects) {
                        const auto it = item && item->baseEffect ? byEffect.find(item->baseEffect->GetFormID()) : byEffect.end();
                        if (it != byEffect.end() && !g_boundSeeds[it->second].spell) {
                            g_boundSeeds[it->second].spell = spell;
                        }
                    }
                }
            }
            for (const auto& seed : g_boundSeeds) {
                logger::info("BlackFlame: bound fire weapon {:08X} '{}' from effect {:08X} (fire {}{}{}), spell {:08X}",
                    seed.weapon->GetFormID(), seed.weapon->GetName(), seed.effect->GetFormID(), seed.byEffect ? "effect" : "",
                    seed.byEffect && seed.byEnchantment ? " and " : "", seed.byEnchantment ? "enchantment" : "",
                    seed.spell ? seed.spell->GetFormID() : 0);
            }
            logger::info("BlackFlame: {} bound fire weapon seed(s), {} weapon(s)", g_boundSeeds.size(), g_boundWeapons.size());
        }

        using PluginLookup = PluginNames::Lookup<const RE::TESFile*>;

        struct PidParts
        {
            std::string plugin;
            std::uint32_t local = 0;
        };

        // "Plugin.esp|0x00XXXX" -> (plugin, local); nullopt for anything else
        // (same parsing as the NPC Spells addon's GamePool.cpp SplitPersistentId)
        std::optional<PidParts> SplitPersistentId(std::string_view pid)
        {
            const auto bar = pid.find('|');
            if (bar == std::string_view::npos || bar == 0) {
                return std::nullopt;
            }
            auto hex = pid.substr(bar + 1);
            if (hex.starts_with(kHexPrefix) || hex.starts_with(kHexPrefixUpper)) {
                hex.remove_prefix(kHexPrefix.size());
            }
            PidParts out{ std::string(pid.substr(0, bar)), 0 };
            const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out.local, kHexBase);
            if (hex.empty() || ec != std::errc{} || end != hex.data() + hex.size() || out.local > kMaxLocalId) {
                return std::nullopt;
            }
            return out;
        }

        RE::SpellItem* LookupSpell(RE::TESDataHandler* dh, PluginLookup& lookup, const PidParts& pid)
        {
            const RE::TESFile* file = lookup.Find(
                pid.plugin,
                [dh](const std::string& name) { return dh->LookupModByName(name); },
                [dh](auto&& visit) {
                    for (const auto* f : dh->files) {
                        if (f) {
                            visit(f->fileName, f);
                        }
                    }
                });
            // LookupFormID adds the local id to the file's index bits unmasked: a local id too
            // wide for the file would land in another plugin's FormID range
            if (!file || pid.local > (file->IsLight() ? kMaxLightLocalId : kMaxLocalId)) {
                return nullptr;
            }
            return dh->LookupForm<RE::SpellItem>(pid.local, file->GetFilename());
        }

        bool HasFireElement(const nlohmann::json& entry)
        {
            const auto elements = entry.find("elements");
            if (elements == entry.end() || !elements->is_array()) {
                return false;
            }
            for (const auto& e : *elements) {
                if (e.is_string() && e.get_ref<const std::string&>() == kFireElement) {
                    return true;
                }
            }
            return false;
        }

        bool IsCountedSpell(const RE::SpellItem* spell)
        {
            return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell &&
                   spell != GetForms().stokeSpell;
        }

        // A fire MGEF: the vanilla fire keyword, or fire resistance as its resist value
        bool IsFireMgef(const RE::EffectSetting* a_base, const RE::BGSKeyword* a_fireKeyword)
        {
            return a_base && ((a_fireKeyword && a_base->HasKeyword(a_fireKeyword)) ||
                                 a_base->data.resistVariable == RE::ActorValue::kResistFire);
        }

        // A spell with a fire MGEF among its effects
        bool HasFireMgef(const RE::SpellItem* a_spell, const RE::BGSKeyword* a_fireKeyword)
        {
            for (const auto* effect : a_spell->effects) {
                if (effect && IsFireMgef(effect->baseEffect, a_fireKeyword)) {
                    return true;
                }
            }
            return false;
        }

        struct CatalogCounts
        {
            std::size_t entries = 0;
            std::size_t resolved = 0;
            std::size_t fire = 0;
        };

        // Fills a_fire with the counted spells the catalog tags fire (v2: such a spell is fire, see
        // BuildFireSets). False without a readable catalog.
        bool ReadCatalog(RE::TESDataHandler* dh, std::unordered_set<RE::FormID>& a_fire, CatalogCounts& counts)
        {
            const auto catalog = ReadCatalogFile();
            if (!catalog) {
                logger::warn("BlackFlame: '{}' has no readable catalog/spells object - ignored", kCatalogPath);
                return false;
            }
            const auto spells = catalog->find("spells");
            PluginLookup lookup;
            for (const auto& [key, entry] : spells->items()) {
                ++counts.entries;
                const auto pid = SplitPersistentId(key);
                if (!pid || !entry.is_object()) {
                    continue;
                }
                auto* spell = LookupSpell(dh, lookup, *pid);
                if (!spell) {
                    continue;
                }
                ++counts.resolved;
                if (IsCountedSpell(spell) && HasFireElement(entry)) {
                    a_fire.insert(spell->GetFormID());
                    ++counts.fire;
                }
            }
            return true;
        }

        void AddHazard(const RE::TESForm* a_form)
        {
            if (const auto* hazard = a_form ? a_form->As<RE::BGSHazard>() : nullptr) {
                g_fireHazards.insert(hazard->GetFormID());
            }
        }

        // The hazards an impact data set places (BGSImpactData::hazard, NAM2)
        void AddImpactHazards(const RE::BGSImpactDataSet* a_set)
        {
            if (!a_set) {
                return;
            }
            for (const auto& [material, impact] : a_set->impactMap) {
                if (impact) {
                    AddHazard(impact->hazard);
                }
            }
        }

        void AddExplosion(const RE::BGSExplosion* a_explosion)
        {
            if (!a_explosion) {
                return;
            }
            g_fireExplosions.insert(a_explosion->GetFormID());
            AddImpactHazards(a_explosion->data.impactDataSet);
            AddHazard(a_explosion->data.impactPlacedObject);
        }

        // A fire MGEF: itself, its explosions (its own and its projectile's), and the hazards it can
        // leave (a spawn-hazard archetype's form, its impact data set's, its explosions')
        void AddFireEffect(const RE::EffectSetting* a_base)
        {
            g_fireEffects.insert(a_base->GetFormID());
            AddExplosion(a_base->data.explosion);
            if (a_base->data.projectileBase) {
                AddExplosion(a_base->data.projectileBase->data.explosionType);
            }
            AddImpactHazards(a_base->data.impactDataSet);
            if (a_base->HasArchetype(RE::EffectSetting::Archetype::kSpawnHazard)) {
                AddHazard(a_base->data.associatedForm);
            }
        }

        // R3: the associated spell of every fire Cloak MGEF becomes a fire spell (any SpellType) with all of its effects
        // fire effects, until a pass adds nothing (a cloak spell's own cloak); returns how many spells were added
        std::size_t AddCloakSpells()
        {
            std::size_t added = 0;
            for (bool grew = true; grew;) {
                grew = false;
                std::vector<const RE::SpellItem*> found;
                for (const auto id : g_fireEffects) {
                    const auto* base = RE::TESForm::LookupByID<RE::EffectSetting>(id);
                    const auto* spell = base && base->HasArchetype(RE::EffectSetting::Archetype::kCloak) && base->data.associatedForm ?
                                            base->data.associatedForm->As<RE::SpellItem>() : nullptr;
                    if (!spell || spell == GetForms().stokeSpell) {
                        continue;
                    }
                    // the lantern's cloak exclusion needs every cloak spell, also one already fire by its own keyword
                    g_cloakSpells.insert(spell->GetFormID());
                    if (!g_fireSpells.contains(spell->GetFormID())) {
                        found.push_back(spell);
                    }
                }
                for (const auto* spell : found) {
                    if (!g_fireSpells.insert(spell->GetFormID()).second) {
                        continue;  // two cloaks naming the same spell
                    }
                    ++added;
                    grew = true;
                    // J4: the effects only - their explosions and hazards are not fire explosions or hazards (a frost
                    // sub-spell's shared explosion must not turn grey while stoked)
                    for (const auto* effect : spell->effects) {
                        if (const auto* base = effect ? effect->baseEffect : nullptr) {
                            g_fireEffects.insert(base->GetFormID());
                        }
                    }
                }
            }
            return added;
        }
    }

    void BuildFireSets()
    {
        g_fireSpells.clear();
        g_fireExplosions.clear();
        g_fireEffects.clear();
        g_fireHazards.clear();
        g_cloakSpells.clear();
        g_fireEquipAbilities.clear();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("BlackFlame: TESDataHandler unavailable - no spell counts as fire");
            return;
        }

        std::unordered_set<RE::FormID> catalogFire;
        CatalogCounts catalog;
        const bool catalogRead = ReadCatalog(dh, catalogFire, catalog);
        BuildVisualSets(catalogFire);  // visual scope follows HoM; gameplay fire classification below is unchanged

        // Fire (v2, user decision 2026-10-16) = a fire MGEF (MagicDamageFire keyword or fire resistance),
        // OR the spell is tagged "fire" by Heart of Magic's catalog. A fire-MGEF spell's fire MGEFs are its
        // fire effects; a catalog-only fire spell has no fire MGEF to pick, so every effect of it is a fire
        // effect (e.g. Vigilant's Script-archetype Blinding Fire). Copies change only visuals, so a catalog
        // false positive costs a grey look and nothing else.
        const auto* fireKeyword = GetForms().fireKeyword;
        std::size_t catalogOnly = 0;
        for (const auto* spell : dh->GetFormArray<RE::SpellItem>()) {
            if (!IsCountedSpell(spell)) {
                continue;
            }
            const bool byEffect = HasFireMgef(spell, fireKeyword);
            const bool byCatalog = !byEffect && catalogFire.contains(spell->GetFormID());
            if (!byEffect && !byCatalog) {
                continue;
            }
            g_fireSpells.insert(spell->GetFormID());
            catalogOnly += byCatalog ? 1 : 0;
            for (const auto* effect : spell->effects) {
                const auto* base = effect ? effect->baseEffect : nullptr;
                if (base && (byCatalog || IsFireMgef(base, fireKeyword))) {
                    AddFireEffect(base);
                }
            }
        }

        const std::size_t associated = AddCloakSpells();
        // The equip abilities of fire effects: their body art (Fire Storm's) counts as fire for the hit art swap (after the
        // cloak pass, which adds fire effects too)
        std::vector<EquipAbilityRules::ParentFacts> parents;
        for (const auto* mgef : dh->GetFormArray<RE::EffectSetting>()) {
            const auto* ability = mgef ? mgef->data.equipAbility : nullptr;
            if (!ability) {
                continue;
            }
            const bool hasHitArt = std::ranges::any_of(ability->effects, [](const auto* a_item) {
                return a_item && a_item->baseEffect && a_item->baseEffect->data.hitEffectArt;
            });
            parents.push_back({ IsFireEffect(mgef), ability->GetFormID(), hasHitArt });
        }
        g_fireEquipAbilities = EquipAbilityRules::CollectFireEquipAbilities(parents);  // only those with a hit art
        // F5: the engine takes the casting art from the costliest effect; read here, on the kDataLoaded thread, once
        g_castingArts.clear();
        for (const auto id : g_fireSpells) {
            const auto* spell = RE::TESForm::LookupByID<RE::MagicItem>(id);
            const auto* costliest = spell ? spell->GetAVEffect() : nullptr;
            if (costliest && costliest->data.castingArt) {
                g_castingArts.emplace(id, costliest->data.castingArt);
            }
        }

        BuildBoundFireSeeds();
        logger::info("BlackFlame: fire spells (a fire MGEF, or the catalog's fire tag): {}; {} fire effects, {} fire "
                     "explosions, {} fire hazards, {} fire equip abilities. Catalog {}: {} spells tagged fire, {} of them "
                     "fire by the tag alone (every effect of those counts); {} added as a fire cloak's associated spell",
            g_fireSpells.size(), g_fireEffects.size(), g_fireExplosions.size(), g_fireHazards.size(),
            g_fireEquipAbilities.size(), catalogRead ? "read" : "not read", catalog.fire, catalogOnly, associated);
        g_counts = { catalogRead, catalog.fire, catalogOnly, associated, g_fireSpells.size(), g_fireEffects.size(),
            g_fireExplosions.size(), g_fireHazards.size() };
    }

    FireSetCounts GetFireSetCounts() noexcept
    {
        return g_counts;
    }

    bool IsBurnable(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell)
    {
        return a_effect && IsFireSpell(a_spell) && g_fireEffects.contains(a_effect->GetFormID());
    }

    bool IsFireEquipAbility(const RE::MagicItem* a_spell)
    {
        return a_spell && EquipAbilityRules::PassesArtFireCheck(false, a_spell->GetFormID(), g_fireEquipAbilities);
    }

    bool IsFireMgefAny(const RE::EffectSetting* a_effect)
    {
        return IsFireMgef(a_effect, GetForms().fireKeyword);
    }

    bool IsFireEffect(const RE::EffectSetting* a_effect)
    {
        return a_effect && g_fireEffects.contains(a_effect->GetFormID());
    }

    bool HasFireEnchantment(const RE::TESObjectWEAP* a_weapon)
    {
        const auto* enchantment = a_weapon ? a_weapon->formEnchanting : nullptr;
        if (!enchantment) {
            return false;
        }
        for (const auto* effect : enchantment->effects) {
            if (effect && IsFireMgefAny(effect->baseEffect)) {
                return true;
            }
        }
        return false;
    }

    const std::vector<BoundFireSeed>& BoundFireSeeds() noexcept
    {
        return g_boundSeeds;
    }

    bool IsFireBoundWeaponForm(const RE::TESForm* a_weapon)
    {
        return a_weapon && g_boundWeapons.contains(a_weapon->GetFormID());
    }

    std::vector<RE::TESAmmo*> BoundArrowAmmo()
    {
        std::vector<RE::TESAmmo*> out;
        const auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
        // (windows.h maps GetObject to GetObjectW: undefined at the top of this file)
        const auto* keyword = defaults ? defaults->GetObject<RE::BGSKeyword>(RE::DEFAULT_OBJECTS::kKeywordWeaponTypeBoundArrow) : nullptr;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!keyword || !dh) {
            return out;
        }
        for (auto* ammo : dh->GetFormArray<RE::TESAmmo>()) {
            const auto* keywords = ammo ? ammo->AsKeywordForm() : nullptr;
            if (keywords && keywords->HasKeyword(keyword) && ammo->GetRuntimeData().data.projectile) {
                out.push_back(ammo);
            }
        }
        return out;
    }

    RE::BGSArtObject* CostliestCastingArtOf(const RE::MagicItem* a_spell)
    {
        if (!a_spell) {
            return nullptr;
        }
        const auto it = g_castingArts.find(a_spell->GetFormID());
        return it != g_castingArts.end() ? it->second : nullptr;
    }

    std::vector<RE::SpellItem*> FireSpellList()
    {
        std::vector<RE::FormID> ids(g_fireSpells.begin(), g_fireSpells.end());
        std::ranges::sort(ids);
        std::vector<RE::SpellItem*> spells;
        spells.reserve(ids.size());
        for (const auto id : ids) {
            if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id)) {
                spells.push_back(spell);
            }
        }
        return spells;
    }

    bool IsFireSpell(const RE::MagicItem* a_spell)
    {
        return a_spell && g_fireSpells.contains(a_spell->GetFormID());
    }

    bool IsCloakSpell(RE::FormID a_spell)
    {
        return g_cloakSpells.contains(a_spell);
    }

    bool IsFireHazard(const RE::BGSHazard* a_hazard)
    {
        if (!a_hazard) {
            return false;
        }
        // A v2 copy counts as its source (FormCopies.h)
        if (const auto* source = SourceOf(a_hazard)) {
            a_hazard = source->As<RE::BGSHazard>();
        }
        const auto* spell = a_hazard->data.spell;
        return g_fireHazards.contains(a_hazard->GetFormID()) ||
               (spell && (IsFireSpell(spell) || HasFireMgef(spell, GetForms().fireKeyword)));
    }

    bool IsFireExplosion(const RE::BGSExplosion* a_explosion)
    {
        // A v2 copy counts as its source (FormCopies.h)
        return a_explosion && g_fireExplosions.contains(SourceOrSelf(a_explosion)->GetFormID());
    }
}
