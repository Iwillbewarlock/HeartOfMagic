#pragma once

#include "Common.h"
#include "SpellScanner.h"
#include "librarian/PerkAdapters.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

        void ForEachCatalogSpell(const std::function<void(const std::string&, const json&, const json&)>& visit);

        // Every item using the effect. The index of all magic items is built on
        // the first call: only keyword lines ask, so a plan of FormList lines
        // never pays for it.
        std::vector<Adapters::EffectUser> UsersOf(const std::string& effectKey);

        RE::EffectSetting* Effect(const std::string& effectKey);

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
        SpellScanner::FieldConfig m_fields;
        std::vector<CatalogSpell> m_catalogSpells;
        bool m_usersIndexed = false;
        std::unordered_map<RE::FormID, std::vector<RE::MagicItem*>> m_users;
        std::unordered_map<RE::FormID, std::unique_ptr<json>> m_spellJson;
        std::unordered_map<std::string, RE::FormID> m_effectIds;
    };
}
