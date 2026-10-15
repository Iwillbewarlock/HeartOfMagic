#include "FireSet.h"

#include "FormCopies.h"
#include "Forms.h"
#include "PluginNames.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
        FireSetCounts g_counts;

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
            std::ifstream file(kCatalogPath, std::ios::binary);
            if (!file) {
                return false;
            }
            std::stringstream text;
            text << file.rdbuf();
            const auto catalog = nlohmann::json::parse(text.str(), nullptr, false);
            if (catalog.is_discarded() || !catalog.is_object()) {
                logger::warn("BlackFlame: '{}' is not a JSON object - ignored", kCatalogPath);
                return false;
            }
            const auto spells = catalog.find("spells");
            if (spells == catalog.end() || !spells->is_object()) {
                logger::warn("BlackFlame: '{}' has no \"spells\" object - ignored", kCatalogPath);
                return false;
            }
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
    }

    void BuildFireSets()
    {
        g_fireSpells.clear();
        g_fireExplosions.clear();
        g_fireEffects.clear();
        g_fireHazards.clear();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("BlackFlame: TESDataHandler unavailable - no spell counts as fire");
            return;
        }

        std::unordered_set<RE::FormID> catalogFire;
        CatalogCounts catalog;
        const bool catalogRead = ReadCatalog(dh, catalogFire, catalog);

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

        logger::info("BlackFlame: fire spells (a fire MGEF, or the catalog's fire tag): {}; {} fire effects, {} fire "
                     "explosions, {} fire hazards. Catalog {}: {} spells tagged fire, {} of them fire by the tag alone "
                     "(every effect of those counts)",
            g_fireSpells.size(), g_fireEffects.size(), g_fireExplosions.size(), g_fireHazards.size(),
            catalogRead ? "read" : "not read", catalog.fire, catalogOnly);
        g_counts = { catalogRead, catalog.fire, catalogOnly, g_fireSpells.size(), g_fireEffects.size(),
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

    bool IsFireMgefAny(const RE::EffectSetting* a_effect)
    {
        return IsFireMgef(a_effect, GetForms().fireKeyword);
    }

    bool IsFireEffect(const RE::EffectSetting* a_effect)
    {
        return a_effect && g_fireEffects.contains(a_effect->GetFormID());
    }

    bool IsFireSpell(const RE::MagicItem* a_spell)
    {
        return a_spell && g_fireSpells.contains(a_spell->GetFormID());
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
