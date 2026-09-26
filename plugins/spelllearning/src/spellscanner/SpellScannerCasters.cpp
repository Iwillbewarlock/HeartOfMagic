#include "Common.h"
#include "SpellScanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
// SpellScannerCasters - which spells the game hands to vampires
//
// Blood magic is not in the records: a vampire's drain and a mage's absorb
// health are the same effect. What does tell them apart is who casts it, so
// the scan marks the spells a vampire NPC carries (castByVampires) and the tag
// librarian's rules decide what that means.
//
// A vampire is an NPC whose race or own record has the Vampire keyword (every
// vanilla vampire race has it). Its spells are its own spell list and its
// race's, with leveled spell lists opened all the way down - and the spells
// Spell Distribution Framework ini files hand to vampires (a mod that adds
// its spells to vampires that way leaves no trace in the records). Read once,
// on the first scan: none of it changes after the game has loaded its data.
// =============================================================================

namespace SpellScanner
{
    namespace
    {
        constexpr std::string_view kVampireKeyword = "Vampire";
        constexpr const char* kDataDir = "Data";
        constexpr std::string_view kDistrSuffix = "_distr.ini";
        constexpr std::string_view kSpellLine = "spell";
        constexpr char kFieldSeparator = '|';
        constexpr char kPluginSeparator = '~';

        std::once_flag g_vampireOnce;
        std::unordered_set<RE::FormID> g_vampireSpells;

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string Trim(const std::string& text)
        {
            const auto first = text.find_first_not_of(" \t\r");
            if (first == std::string::npos) return "";
            const auto last = text.find_last_not_of(" \t\r");
            return text.substr(first, last - first + 1);
        }

        // A SPID form reference: "0x810~Abyss.esp" or an editor id
        RE::SpellItem* ResolveDistrForm(const std::string& reference)
        {
            const auto tilde = reference.find(kPluginSeparator);
            if (tilde != std::string::npos) {
                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                if (!dataHandler) return nullptr;
                try {
                    const auto localId = static_cast<RE::FormID>(std::stoul(reference.substr(0, tilde), nullptr, 16));
                    return dataHandler->LookupForm<RE::SpellItem>(localId, Trim(reference.substr(tilde + 1)));
                } catch (const std::exception&) {
                    return nullptr;
                }
            }
            return RE::TESForm::LookupByEditorID<RE::SpellItem>(reference);
        }

        // "Spell = <form>|<string filters>|..." where the string filters name
        // Vampire (a keyword or an editor id; "," or "+" between entries)
        std::size_t ReadDistrFile(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            std::size_t added = 0;
            std::string line;
            while (std::getline(file, line)) {
                const auto equals = line.find('=');
                if (equals == std::string::npos || line.starts_with(";")) continue;
                if (Lower(Trim(line.substr(0, equals))) != kSpellLine) continue;

                const std::string rest = line.substr(equals + 1);
                const auto bar = rest.find(kFieldSeparator);
                if (bar == std::string::npos) continue;
                const auto nextBar = rest.find(kFieldSeparator, bar + 1);
                const std::string filters = rest.substr(bar + 1, nextBar == std::string::npos ? std::string::npos : nextBar - bar - 1);

                bool vampire = false;
                std::size_t start = 0;
                while (start <= filters.size()) {
                    const auto end = filters.find_first_of(",+", start);
                    const std::string token = Trim(filters.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (Lower(token) == Lower(std::string(kVampireKeyword))) vampire = true;
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                if (!vampire) continue;

                if (auto* spell = ResolveDistrForm(Trim(rest.substr(0, bar)))) {
                    added += g_vampireSpells.insert(spell->GetFormID()).second ? 1 : 0;
                }
            }
            return added;
        }

        std::size_t ReadDistrFiles()
        {
            std::size_t added = 0;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(kDataDir, error)) {
                if (!entry.is_regular_file()) continue;
                if (!Lower(entry.path().filename().string()).ends_with(kDistrSuffix)) continue;
                added += ReadDistrFile(entry.path());
            }
            return added;
        }

        void AddLeveled(RE::TESLevSpell* list, std::unordered_set<RE::FormID>& seenLists)
        {
            if (!list || !seenLists.insert(list->GetFormID()).second) return;
            for (auto* form : list->GetContainedForms()) {
                if (!form) continue;
                if (auto* spell = form->As<RE::SpellItem>()) {
                    g_vampireSpells.insert(spell->GetFormID());
                } else if (auto* nested = form->As<RE::TESLevSpell>()) {
                    AddLeveled(nested, seenLists);
                }
            }
        }

        void AddSpellList(const RE::TESSpellList::SpellData* data, std::unordered_set<RE::FormID>& seenLists)
        {
            if (!data) return;
            for (std::uint32_t i = 0; data->spells && i < data->numSpells; ++i) {
                if (data->spells[i]) g_vampireSpells.insert(data->spells[i]->GetFormID());
            }
            for (std::uint32_t i = 0; data->levSpells && i < data->numlevSpells; ++i) {
                AddLeveled(data->levSpells[i], seenLists);
            }
        }

        void CollectVampireSpells()
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return;

            std::unordered_set<RE::FormID> seenLists;
            std::unordered_set<RE::FormID> seenRaces;
            std::size_t vampires = 0;
            for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                if (!npc) continue;
                auto* race = npc->GetRace();
                const bool vampire = npc->HasKeywordString(kVampireKeyword) ||
                                     (race && race->HasKeywordString(kVampireKeyword));
                if (!vampire) continue;

                ++vampires;
                AddSpellList(npc->actorEffects, seenLists);
                if (race && seenRaces.insert(race->GetFormID()).second) {
                    AddSpellList(race->actorEffects, seenLists);
                }
            }
            const std::size_t distributed = ReadDistrFiles();
            logger::info("SpellScanner: {} vampire NPCs carry {} spells ({} more handed out by SPID ini files)",
                vampires, g_vampireSpells.size(), distributed);
        }
    }

    bool IsCastByVampires(RE::FormID spellFormId)
    {
        std::call_once(g_vampireOnce, CollectVampireSpells);
        return g_vampireSpells.contains(spellFormId);
    }
}
