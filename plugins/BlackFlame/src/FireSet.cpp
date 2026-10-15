#include "FireSet.h"

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
        std::unordered_set<RE::FormID> g_fireEffects;      // fire MGEFs (keyword or fire resistance) of fire spells
        // The costliest effect of a catalog-fire spell without a fire MGEF: fire only together with
        // THAT spell, so it is kept as (spell, MGEF) pairs, and its explosions only for that spell
        std::unordered_set<std::uint64_t> g_fallbackPairs;
        std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> g_fallbackExplosions;  // explosion -> spells

        std::uint64_t PairKey(RE::FormID a_spell, RE::FormID a_effect)
        {
            constexpr int kShift = 32;
            return (static_cast<std::uint64_t>(a_spell) << kShift) | a_effect;
        }
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

        // A spell the catalog does not decide is fire when one of its effects is a fire MGEF
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

        // Fills `decided` with every catalog spell found in this load order and
        // g_fireSpells with those the catalog tags fire. False when there is no catalog.
        bool ReadCatalog(RE::TESDataHandler* dh, std::unordered_set<RE::FormID>& decided, CatalogCounts& counts)
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
                // Decided by the catalog only when it says which elements the spell has
                const auto elements = entry.find("elements");
                if (elements == entry.end() || !elements->is_array()) {
                    continue;
                }
                decided.insert(spell->GetFormID());
                if (IsCountedSpell(spell) && HasFireElement(entry)) {
                    g_fireSpells.insert(spell->GetFormID());
                    ++counts.fire;
                }
            }
            return true;
        }

        // The explosions an MGEF names: its own and its projectile's
        template <class Add>
        void ForEachExplosion(const RE::EffectSetting* a_base, Add&& a_add)
        {
            if (a_base->data.explosion) {
                a_add(a_base->data.explosion->GetFormID());
            }
            if (a_base->data.projectileBase && a_base->data.projectileBase->data.explosionType) {
                a_add(a_base->data.projectileBase->data.explosionType->GetFormID());
            }
        }

        void AddFireEffect(const RE::EffectSetting* a_base)
        {
            g_fireEffects.insert(a_base->GetFormID());
            ForEachExplosion(a_base, [](RE::FormID a_explosion) { g_fireExplosions.insert(a_explosion); });
        }

        void AddFallbackEffect(const RE::SpellItem* a_spell, const RE::EffectSetting* a_base)
        {
            g_fallbackPairs.insert(PairKey(a_spell->GetFormID(), a_base->GetFormID()));
            ForEachExplosion(a_base, [&](RE::FormID a_explosion) {
                g_fallbackExplosions[a_explosion].insert(a_spell->GetFormID());
            });
        }

        // A fire spell's fire MGEFs; a spell the catalog calls fire without one (its fire is
        // in a script or its name) contributes only its costliest effect
        void AddFireEffects(const RE::SpellItem* a_spell, const RE::BGSKeyword* a_fireKeyword)
        {
            bool added = false;
            for (const auto* effect : a_spell->effects) {
                const auto* base = effect ? effect->baseEffect : nullptr;
                if (IsFireMgef(base, a_fireKeyword)) {
                    AddFireEffect(base);
                    added = true;
                }
            }
            if (added) {
                return;
            }
            const auto* costliest = a_spell->GetCostliestEffectItem();
            if (costliest && costliest->baseEffect) {
                AddFallbackEffect(a_spell, costliest->baseEffect);
            }
        }
    }

    void BuildFireSets()
    {
        g_fireSpells.clear();
        g_fireExplosions.clear();
        g_fireEffects.clear();
        g_fallbackPairs.clear();
        g_fallbackExplosions.clear();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("BlackFlame: TESDataHandler unavailable - no spell counts as fire");
            return;
        }

        std::unordered_set<RE::FormID> decided;
        CatalogCounts catalog;
        const bool catalogRead = ReadCatalog(dh, decided, catalog);
        if (!catalogRead) {
            logger::warn("BlackFlame: no readable '{}' (run a Heart of Magic scan) - fire spells by the "
                         "fire MGEF rule only (MagicDamageFire keyword or fire resistance)", kCatalogPath);
        }

        const auto* fireKeyword = GetForms().fireKeyword;
        std::size_t keywordFire = 0;
        for (const auto* spell : dh->GetFormArray<RE::SpellItem>()) {
            if (!IsCountedSpell(spell) || decided.contains(spell->GetFormID())) {
                continue;
            }
            if (HasFireMgef(spell, fireKeyword)) {
                g_fireSpells.insert(spell->GetFormID());
                ++keywordFire;
            }
        }

        for (const auto* spell : dh->GetFormArray<RE::SpellItem>()) {
            if (spell && g_fireSpells.contains(spell->GetFormID())) {
                AddFireEffects(spell, fireKeyword);
            }
        }

        logger::info("BlackFlame: fire spells: {} from the catalog ({} entries, {} found in this load order), "
                     "{} by fire MGEF, {} total; {} fire effects, {} fire explosions",
            catalog.fire, catalog.entries, catalog.resolved, keywordFire, g_fireSpells.size(),
            g_fireEffects.size() + g_fallbackPairs.size(), g_fireExplosions.size() + g_fallbackExplosions.size());
        g_counts = { catalogRead, catalog.fire, keywordFire, g_fireSpells.size(),
            g_fireEffects.size() + g_fallbackPairs.size(), g_fireExplosions.size() + g_fallbackExplosions.size() };
    }

    FireSetCounts GetFireSetCounts() noexcept
    {
        return g_counts;
    }

    bool IsBurnable(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell)
    {
        if (!a_effect || !IsFireSpell(a_spell)) {
            return false;
        }
        return g_fireEffects.contains(a_effect->GetFormID()) ||
               g_fallbackPairs.contains(PairKey(a_spell->GetFormID(), a_effect->GetFormID()));
    }

    bool IsFireSpell(const RE::MagicItem* a_spell)
    {
        return a_spell && g_fireSpells.contains(a_spell->GetFormID());
    }

    bool IsFireExplosion(const RE::BGSExplosion* a_explosion, const RE::MagicItem* a_casterSpell)
    {
        if (!a_explosion) {
            return false;
        }
        if (g_fireExplosions.contains(a_explosion->GetFormID())) {
            return true;
        }
        const auto it = g_fallbackExplosions.find(a_explosion->GetFormID());
        return it != g_fallbackExplosions.end() && a_casterSpell && it->second.contains(a_casterSpell->GetFormID());
    }
}
