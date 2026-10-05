#pragma once

#include "librarian/PerkAdapters.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// Spell Research - which SR archetype lists each catalog spell goes into
//
// Spell Research (SR) 2.2.3 knows a spell only through FormLists its Import
// fills: one per tier, school, casting, target, element and technique, plus
// the tome pairs. The rules file (librarian/compat/spellresearch.json) holds the
// lists' persistent ids ("lists") and, as perk adapter formList lines
// ("adapters"), how the librarian's tags fold into SR's elements and
// techniques. The tier, school, casting and targets come straight from the
// catalog, which already uses SR's words. docs/librarian/SPELL_RESEARCH.md
// has the mapping and why.
//
// Pure, like PerkAdapters.h: the game side (SpellResearchPatch.h) and
// tools/librarian-test make the same registrations from the same inputs.
// =============================================================================

namespace Librarian::SpellResearch
{
    using json = nlohmann::json;

    // SR's tiers, in the order of its numbered lists (_SR_ListAllSpells1Novice ...)
    inline constexpr std::array<std::string_view, 5> kTiers{ "novice", "apprentice", "adept", "expert", "master" };

    // Persistent ids ("SpellResearch.esp|0xD63") of SR's lists and globals.
    struct Lists
    {
        std::array<std::string, 5> allSpells;    // by tier
        std::array<std::string, 5> tomes;        // by tier, paired by index with tomeSpells
        std::array<std::string, 5> tomeSpells;
        std::unordered_map<std::string, std::string> school;    // "alteration" -> list
        std::unordered_map<std::string, std::string> casting;   // "fireforget" | "concentration"
        std::unordered_map<std::string, std::string> target;    // "actor" | "aoe" | "location" | "self"
        std::unordered_map<std::string, std::string> element;   // SR's 34 elements: "fire" -> list
        std::unordered_map<std::string, std::string> technique; // SR's 15 techniques
        std::string isSetup;      // _SR_GlobalIsSetup: 1 once an Import finished
        std::string importing;    // _SR_GlobalImporting: 1 while one runs
    };

    // Reads the rules document's "lists". Every list must be there: a file
    // missing one would register spells SR cannot find by tier or school.
    bool ParseLists(const json& document, Lists& lists, std::vector<std::string>& problems);

    // "element.fire", "technique.curing", "school.alteration" ... for a list
    // id, or empty: how reports and the scorer name a list.
    std::string ArchetypeOf(const Lists& lists, const std::string& listId);

    // One spell's registration, in the order SR's own import writes it.
    struct Registration
    {
        std::string spellId;
        std::size_t tier = 0;                  // index into kTiers
        std::vector<std::string> lists;        // tier, school, casting, targets, elements, techniques
        std::string tome;                      // persistent id of the book that teaches it, or empty
    };

    struct Registrations
    {
        std::vector<Registration> spells;
        std::size_t noSchoolOrTier = 0;        // catalog spells SR could not file, left out
    };

    // Every catalog spell with an SR school and tier, its lists from the
    // catalog fields and from `plan` (BuildPlan over the rules file ALONE:
    // its formListsNone marks live in that one call). `tomeOf` gives a spell's
    // book or an empty string.
    Registrations Build(const Lists& lists, const Adapters::Plan& plan, const json& catalogSpells,
        const std::function<std::string(const std::string& spellId)>& tomeOf);
}
