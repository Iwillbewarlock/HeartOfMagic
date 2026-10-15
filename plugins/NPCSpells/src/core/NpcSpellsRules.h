#pragma once
#include "core/NpcSpellsTypes.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace NpcSpells {
    struct FormRef { std::string pluginLower; std::uint32_t local = 0; };  // "XXXXXX:Plugin.ext" only, mod records included
    struct Rung { Tier startTier; bool below; double chance; };
    struct GroupRule { std::string name; std::vector<FormRef> factions; std::vector<std::string> prefer; double preferShare = 0.75; std::vector<std::string> never; };
    struct Rules {
        std::vector<Rung> ladder;             // default: see DefaultRules()
        double hybridScale = 0.5;
        double nonMageChance = 0.05;
        std::vector<std::string> nonMageTags{ "curing", "armor" };
        int mageMinMagicShareTenths = 6;      // magicSum*10 >= total*6
        int hybridMinMagicShareTenths = 3;
        std::vector<FormRef> necromancerClasses;
        double conjurationShare = 0.75;
        std::vector<std::string> excludeTags{ "cloak", "trap", "sense" };
        std::vector<std::string> excludeSpells;   // persistentIds (crash/script reject list, default empty)
        std::vector<FormRef> beastRaces;
        std::vector<GroupRule> groups;
        std::vector<FormRef> undeadExceptNpcs;
        FormRef redguardRace;
        FormRef keywordNpc, keywordCreature, keywordAnimal;
    };
    Rules DefaultRules();
    // Parses the user JSON (section 5.3 shape). Returns false and fills error on malformed JSON or a ladder that is not kRungCount rungs;
    // unknown keys are ignored; an entry that is not a FormID (or not a string) drops that entry only, and a chance, share or scale
    // outside 0..1 (or hybridMinMagicShare above mageMinMagicShare) goes back to its default: true, error names each one ("; " between, section 7).
    bool ParseRules(const std::string& jsonText, Rules& out, std::string& error);
    // "0C969F:Skyrim.esm" -> FormRef; anything else (a bare EditorID, "EditorID:Plugin.esp", more than 6 hex digits, no plugin) -> nullopt + error.
    std::optional<FormRef> ParseFormRef(std::string_view text, std::string& error);
}
