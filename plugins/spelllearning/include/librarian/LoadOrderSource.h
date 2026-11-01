#pragma once

#include "Common.h"
#include "SpellScanner.h"
#include "librarian/PerkAdapters.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================================================================
// LoadOrderSource - the running game as the adapter planner sees it
//
// What Librarian::Adapters::BuildPlan asks about (PerkAdapters.h, PlanSource),
// answered from the loaded forms: the catalog spells as scan-shaped JSON, and
// every item that uses an effect. Shared by the perk adapters and the Spell
// Research registrations. Game thread only.
// =============================================================================

namespace Librarian
{
    // Whether a plugin is loaded, as a full or a light (ESL-flagged) file
    [[nodiscard]] inline bool PluginLoaded(RE::TESDataHandler* dataHandler, const std::string& name)
    {
        return dataHandler->LookupLoadedModByName(name) || dataHandler->LookupLoadedLightModByName(name);
    }

    class LoadOrderSource
    {
    public:
        using json = nlohmann::json;

        // `catalogSpells` is the catalog's "spells" object; it must outlive this.
        explicit LoadOrderSource(const json& catalogSpells);
        // m_fields points into this object (times, baseEffects): never copied
        LoadOrderSource(const LoadOrderSource&) = delete;
        LoadOrderSource& operator=(const LoadOrderSource&) = delete;

        void ForEachCatalogSpell(const std::function<void(const std::string&, const json&, const json&)>& visit);

        // Every item using the effect. The index of all magic items is built on
        // the first call: only keyword lines ask, so a plan of FormList lines
        // never pays for it.
        std::vector<Adapters::EffectUser> UsersOf(const std::string& effectKey);

        RE::EffectSetting* Effect(const std::string& effectKey);

        // Where this source's time went, in ms, for the perk adapters' stage line
        struct Timings
        {
            double tomes = 0;       // spells a tome teaches
            double resolve = 0;     // catalog ids to loaded spells
            double spellJson = 0;   // scan-shaped JSON, built once per spell
            double users = 0;       // the index of every magic item by effect
            SpellScanner::SpellJsonTimes spellJsonParts;   // spellJson's parts (Phase 6d)
        };
        [[nodiscard]] const Timings& GetTimings() const { return m_timings; }

    private:
        struct CatalogSpell
        {
            std::string id;
            RE::SpellItem* spell = nullptr;
            const json* entry = nullptr;
        };

        void IndexUsers();
        RE::FormID EffectFormId(const std::string& effectKey);
        const json& SpellJson(RE::SpellItem* spell);

        const json& m_catalog;
        std::unordered_set<RE::FormID> m_taught;   // spells a tome teaches
        SpellScanner::FieldConfig m_fields;
        std::vector<CatalogSpell> m_catalogSpells;
        bool m_usersIndexed = false;
        std::unordered_map<RE::FormID, std::vector<RE::MagicItem*>> m_users;
        std::unordered_map<RE::FormID, std::unique_ptr<json>> m_spellJson;
        std::unordered_map<RE::FormID, json> m_baseEffects;   // FieldConfig::baseEffects
        std::unordered_map<std::string, RE::FormID> m_effectIds;
        Timings m_timings;
    };
}
