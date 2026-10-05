#include "librarian/SpellResearch.h"

#include <algorithm>
#include <optional>

// =============================================================================
// LibrarianSpellResearch - the SR registrations, from the catalog and a plan
// =============================================================================

namespace Librarian::SpellResearch
{
    namespace
    {
        bool ReadId(const json& section, const std::string& key, const std::string& where, std::string& target,
            std::vector<std::string>& problems)
        {
            const auto found = section.find(key);
            if (found == section.end() || !found->is_string() || found->get<std::string>().empty()) {
                problems.push_back(where + "." + key + ": missing");
                return false;
            }
            target = found->get<std::string>();
            return true;
        }

        bool ReadTiers(const json& section, const char* key, std::array<std::string, 5>& target,
            std::vector<std::string>& problems)
        {
            const auto found = section.find(key);
            if (found == section.end() || !found->is_object()) {
                problems.push_back(std::string("lists.") + key + ": missing");
                return false;
            }
            bool ok = true;
            for (std::size_t i = 0; i < kTiers.size(); ++i) {
                ok = ReadId(*found, std::string(kTiers[i]), std::string("lists.") + key, target[i], problems) && ok;
            }
            return ok;
        }

        // Every name given must be there; with none given, whatever names the file has
        bool ReadNamed(const json& section, const char* key, std::initializer_list<const char*> names,
            std::unordered_map<std::string, std::string>& target, std::vector<std::string>& problems)
        {
            const auto found = section.find(key);
            if (found == section.end() || !found->is_object()) {
                problems.push_back(std::string("lists.") + key + ": missing");
                return false;
            }
            bool ok = true;
            for (const char* name : names) {
                ok = ReadId(*found, name, std::string("lists.") + key, target[name], problems) && ok;
            }
            if (names.size() == 0) {
                for (const auto& [name, value] : found->items()) {
                    ok = ReadId(*found, name, std::string("lists.") + key, target[name], problems) && ok;
                }
            }
            return ok;
        }

        std::string NameIn(const std::unordered_map<std::string, std::string>& named, const std::string& listId)
        {
            for (const auto& [name, id] : named) {
                if (id == listId) return name;
            }
            return {};
        }

        std::string Text(const json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string()) ? found->get<std::string>() : std::string();
        }

        std::optional<std::size_t> TierIndex(const std::string& tier)
        {
            const auto found = std::ranges::find(kTiers, tier);
            if (found == kTiers.end()) return std::nullopt;
            return static_cast<std::size_t>(found - kTiers.begin());
        }

        void AddOnce(std::vector<std::string>& lists, const std::string& list)
        {
            if (!list.empty() && std::ranges::find(lists, list) == lists.end()) lists.push_back(list);
        }
    }

    bool ParseLists(const json& document, Lists& lists, std::vector<std::string>& problems)
    {
        const auto section = document.find("lists");
        if (section == document.end() || !section->is_object()) {
            problems.push_back("no \"lists\" object");
            return false;
        }
        bool ok = ReadTiers(*section, "allSpells", lists.allSpells, problems);
        ok = ReadTiers(*section, "tomes", lists.tomes, problems) && ok;
        ok = ReadTiers(*section, "tomeSpells", lists.tomeSpells, problems) && ok;
        ok = ReadNamed(*section, "school", { "alteration", "conjuration", "destruction", "illusion", "restoration" },
                 lists.school, problems) && ok;
        ok = ReadNamed(*section, "casting", { "fireforget", "concentration" }, lists.casting, problems) && ok;
        ok = ReadNamed(*section, "target", { "actor", "aoe", "location", "self" }, lists.target, problems) && ok;
        ok = ReadNamed(*section, "element", {}, lists.element, problems) && ok;
        ok = ReadNamed(*section, "technique", {}, lists.technique, problems) && ok;
        ok = ReadId(*section, "isSetup", "lists", lists.isSetup, problems) && ok;
        ok = ReadId(*section, "importing", "lists", lists.importing, problems) && ok;
        return ok;
    }

    std::string ArchetypeOf(const Lists& lists, const std::string& listId)
    {
        for (std::size_t i = 0; i < kTiers.size(); ++i) {
            if (lists.allSpells[i] == listId) return "tier." + std::string(kTiers[i]);
        }
        const std::pair<const char*, const std::unordered_map<std::string, std::string>*> axes[] = {
            { "school.", &lists.school }, { "casting.", &lists.casting }, { "target.", &lists.target },
            { "element.", &lists.element }, { "technique.", &lists.technique } };
        for (const auto& [prefix, named] : axes) {
            if (std::string name = NameIn(*named, listId); !name.empty()) return prefix + name;
        }
        return {};
    }

    Registrations Build(const Lists& lists, const Adapters::Plan& plan, const json& catalogSpells,
        const std::function<std::string(const std::string& spellId)>& tomeOf)
    {
        // Element and technique lists per spell, in rules file order
        std::unordered_map<std::string, std::vector<std::string>> planned;
        for (const auto& write : plan.listWrites) {
            AddOnce(planned[write.spellId], write.formList);
        }

        Registrations result;
        for (const auto& [id, entry] : catalogSpells.items()) {
            const auto tier = TierIndex(Text(entry, "tier"));
            const auto school = lists.school.find(Text(entry, "school"));
            if (!tier || school == lists.school.end()) {
                ++result.noSchoolOrTier;
                continue;
            }

            Registration registration;
            registration.spellId = id;
            registration.tier = *tier;
            registration.lists.push_back(lists.allSpells[*tier]);
            registration.lists.push_back(school->second);
            if (const auto casting = lists.casting.find(Text(entry, "casting")); casting != lists.casting.end()) {
                registration.lists.push_back(casting->second);
            }
            if (const auto targeting = entry.find("targeting"); targeting != entry.end() && targeting->is_array()) {
                for (const auto& value : *targeting) {
                    if (!value.is_string()) continue;
                    const auto target = lists.target.find(value.get<std::string>());
                    if (target != lists.target.end()) AddOnce(registration.lists, target->second);
                }
            }
            if (const auto found = planned.find(id); found != planned.end()) {
                for (const auto& list : found->second) AddOnce(registration.lists, list);
            }
            if (tomeOf) registration.tome = tomeOf(id);
            result.spells.push_back(std::move(registration));
        }
        return result;
    }
}
