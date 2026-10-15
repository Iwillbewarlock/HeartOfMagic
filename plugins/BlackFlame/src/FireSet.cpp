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
        std::unordered_set<RE::FormID> g_fireEffects;  // MGEFs: what the burn and explosion layers key on
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

        bool HasFireKeyword(const RE::SpellItem* spell, const RE::BGSKeyword* fireKeyword)
        {
            if (!fireKeyword) {
                return false;
            }
            for (const auto* effect : spell->effects) {
                if (effect && effect->baseEffect && effect->baseEffect->HasKeyword(fireKeyword)) {
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

        // A fire MGEF: the vanilla fire keyword, or fire resistance as its resist value
        bool IsFireMgef(const RE::EffectSetting* a_base, const RE::BGSKeyword* a_fireKeyword)
        {
            return a_base && ((a_fireKeyword && a_base->HasKeyword(a_fireKeyword)) ||
                                 a_base->data.resistVariable == RE::ActorValue::kResistFire);
        }

        void AddFireEffect(const RE::EffectSetting* a_base)
        {
            g_fireEffects.insert(a_base->GetFormID());
            if (a_base->data.explosion) {
                g_fireExplosions.insert(a_base->data.explosion->GetFormID());
            }
            if (a_base->data.projectileBase && a_base->data.projectileBase->data.explosionType) {
                g_fireExplosions.insert(a_base->data.projectileBase->data.explosionType->GetFormID());
            }
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
                AddFireEffect(costliest->baseEffect);
            }
        }
    }

    void BuildFireSets()
    {
        g_fireSpells.clear();
        g_fireExplosions.clear();
        g_fireEffects.clear();
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
                         "MagicDamageFire keyword only", kCatalogPath);
        }

        const auto* fireKeyword = GetForms().fireKeyword;
        std::size_t keywordFire = 0;
        for (const auto* spell : dh->GetFormArray<RE::SpellItem>()) {
            if (!IsCountedSpell(spell) || decided.contains(spell->GetFormID())) {
                continue;
            }
            if (HasFireKeyword(spell, fireKeyword)) {
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
                     "{} by keyword, {} total; {} fire effects, {} fire explosions",
            catalog.fire, catalog.entries, catalog.resolved, keywordFire, g_fireSpells.size(), g_fireEffects.size(),
            g_fireExplosions.size());
        g_counts = { catalogRead, catalog.fire, keywordFire, g_fireSpells.size(), g_fireEffects.size(),
            g_fireExplosions.size() };
    }

    FireSetCounts GetFireSetCounts() noexcept
    {
        return g_counts;
    }

    bool IsFireEffect(const RE::EffectSetting* a_effect)
    {
        return a_effect && g_fireEffects.contains(a_effect->GetFormID());
    }

    bool IsFireSpell(const RE::MagicItem* a_spell)
    {
        return a_spell && g_fireSpells.contains(a_spell->GetFormID());
    }

    bool IsFireExplosion(const RE::BGSExplosion* a_explosion)
    {
        return a_explosion && g_fireExplosions.contains(a_explosion->GetFormID());
    }
}
