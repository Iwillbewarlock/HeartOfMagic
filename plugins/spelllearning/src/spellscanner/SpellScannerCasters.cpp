#include "Common.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "SpellScanner.h"
#include "PathText.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
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
// A vampire is an NPC whose own record has the Vampire keyword, or whose race
// has it or is named a vampire race: the race's editor id holds "vamp". Not
// every vampire race carries the keyword (vanilla FalmerFrozenVampRace, CC's
// HorkerRaceVampire and Vigilant's vampire races do not); races keep their
// editor ids in the engine, po3 Tweaks or not. An NPC that takes its traits
// from a template is judged by the template's race (CollectSummonedNPCs, as
// for summons). Its spells are its own spell list (unless it takes its spells
// from the template) and its race's, with leveled spell lists opened all the
// way down - and the spells Spell Distribution Framework ini files hand to
// vampires (a mod that adds its spells to vampires that way leaves no trace in
// the records). Read once, on the first scan: none of it changes after the
// game has loaded its data.
// =============================================================================

namespace SpellScanner
{
    namespace
    {
        constexpr std::string_view kVampireKeyword = "Vampire";
        constexpr std::string_view kVampireRaceName = "vamp";  // lower case: matched against the lowered editor id
        constexpr const char* kDataDir = "Data";
        constexpr std::string_view kDistrSuffix = "_distr.ini";
        constexpr std::string_view kSpellLine = "spell";
        constexpr char kFieldSeparator = '|';
        constexpr char kPluginSeparator = '~';

        std::once_flag g_vampireOnce;
        std::unordered_set<RE::FormID> g_vampireSpells;
        // How the one collection of the session went (VampireCastersState)
        std::atomic<ScanRecordRules::Casters> g_vampireState{ ScanRecordRules::Casters::kNotRun };

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
                const auto localId = FormIdText::Parse(Trim(reference.substr(0, tilde)));
                if (!localId) {
                    logger::warn("SpellScanner: SPID reference '{}' has no hex form id before '~'", reference);
                    return nullptr;
                }
                return dataHandler->LookupForm<RE::SpellItem>(*localId, Trim(reference.substr(tilde + 1)));
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
                if (AsciiText::ToLower(Trim(line.substr(0, equals))) != kSpellLine) continue;

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
                    if (AsciiText::EqualsIgnoreCase(token, kVampireKeyword)) vampire = true;
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
            std::filesystem::directory_iterator it(kDataDir, error);
            const std::filesystem::directory_iterator end;
            if (error) {
                logger::warn("SpellScanner: could not open the Data directory: {}", error.message());
                return added;
            }
            // Every file in Data goes through here, so a name the ANSI code page cannot hold (see PathText.h)
            // must not throw, and neither may a file that vanishes or is locked partway through the walk.
            while (!error && it != end) {
                try {
                    if (it->is_regular_file() &&
                        AsciiText::ToLower(PathText::Utf8(it->path().filename())).ends_with(kDistrSuffix)) {
                        added += ReadDistrFile(it->path());
                    }
                } catch (const std::exception& e) {
                    logger::warn("SpellScanner: skipped a file in Data ({})", e.what());
                } catch (...) {
                    // /EHa: also a fault while resolving a SPID line's form - that file only
                    logger::warn("SpellScanner: skipped a file in Data (a fault while reading it)");
                }
                it.increment(error);
            }
            if (error) {
                logger::warn("SpellScanner: the Data walk stopped early: {}", error.message());
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

        enum class VampireRace { kNo, kKeyword, kName };

        VampireRace JudgeRace(const RE::TESRace* race)
        {
            if (!race) return VampireRace::kNo;
            if (race->HasKeywordString(kVampireKeyword)) return VampireRace::kKeyword;
            const char* editorId = race->GetFormEditorID();
            if (editorId && AsciiText::ToLower(editorId).contains(kVampireRaceName)) return VampireRace::kName;
            return VampireRace::kNo;
        }

        // The races an NPC's traits come from: its own, or those of the NPCs its template leads to.
        // Many NPCs share one template (a leveled list of bandits, draugr...), so each template's walk is
        // kept by its FormID and done once per collection. Memory grows with the distinct templates
        // (one short list of race pointers each); the whole collection's time is logged.
        const std::vector<RE::TESRace*>& TraitRaces(RE::TESNPC* npc,
            std::unordered_map<RE::FormID, std::vector<RE::TESRace*>>& templateRaces,
            std::vector<RE::TESRace*>& own)
        {
            using Flag = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG;
            own.clear();
            const bool traitsFromTemplate = npc->UsesTemplate() && npc->baseTemplateForm &&
                npc->actorData.templateUseFlags.any(Flag::kTraits);
            if (traitsFromTemplate) {
                const auto [it, added] = templateRaces.try_emplace(npc->baseTemplateForm->GetFormID());
                if (added) {
                    std::vector<RE::TESNPC*> npcs;
                    // depth 1: the same walk CollectSummonedNPCs(npc) makes below a template NPC
                    CollectSummonedNPCs(npc->baseTemplateForm, npcs, 1);
                    for (auto* source : npcs) {
                        auto* race = source->GetRace();
                        if (race && std::ranges::find(it->second, race) == it->second.end()) it->second.push_back(race);
                    }
                }
                if (!it->second.empty()) return it->second;
                // a template chain that leads nowhere: the NPC's own race
            }
            if (auto* race = npc->GetRace()) own.push_back(race);
            return own;
        }

        struct VampireWalk
        {
            std::unordered_map<RE::FormID, VampireRace> races;  // each race judged once
            std::unordered_map<RE::FormID, std::vector<RE::TESRace*>> templateRaces;  // each template walked once
            std::unordered_set<RE::FormID> seenLists;
            std::unordered_set<RE::FormID> seenRaces;
            std::vector<RE::TESRace*> ownRace;
            std::vector<RE::TESRace*> vampireRaces;
            std::size_t vampires = 0;
        };

        void AddIfVampire(RE::TESNPC* npc, VampireWalk& walk)
        {
            walk.vampireRaces.clear();
            for (auto* race : TraitRaces(npc, walk.templateRaces, walk.ownRace)) {
                const auto [it, added] = walk.races.try_emplace(race->GetFormID(), VampireRace::kNo);
                if (added) {
                    it->second = JudgeRace(race);
                    if (it->second == VampireRace::kName) {
                        logger::debug("SpellScanner: vampire race by its editor id only: {}", race->GetFormEditorID());
                    }
                }
                if (it->second != VampireRace::kNo) walk.vampireRaces.push_back(race);
            }
            if (walk.vampireRaces.empty() && !npc->HasKeywordString(kVampireKeyword)) return;

            ++walk.vampires;
            // An NPC that takes its spells from its template does not use its own list in game; the
            // template's NPCs are in this loop themselves, so their spells are not lost
            const bool spellsFromTemplate = npc->UsesTemplate() && npc->baseTemplateForm &&
                npc->actorData.templateUseFlags.any(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kSpells);
            if (!spellsFromTemplate) AddSpellList(npc->actorEffects, walk.seenLists);
            for (auto* race : walk.vampireRaces) {
                if (walk.seenRaces.insert(race->GetFormID()).second) AddSpellList(race->actorEffects, walk.seenLists);
            }
        }

        // True when the walk got through every NPC. One NPC that cannot be read (a
        // fault in its record or its template's, caught under /EHa) is skipped and
        // logged with its FormID, and the rest are still read: a load order with one
        // broken NPC would otherwise be scanned again at every game start. A template
        // whose walk faulted part way keeps what it had (maybe nothing), so its other
        // NPCs may fall back to their own race.
        bool CollectVampireSpellsImpl()
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                logger::warn("SpellScanner: no data handler - the spells vampires cast are not known");
                return false;
            }
            const auto started = std::chrono::steady_clock::now();

            VampireWalk walk;
            const auto& races = walk.races;
            std::size_t skipped = 0;
            for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                if (!npc) continue;
                RE::FormID formId = 0;
                try {
                    formId = npc->GetFormID();
                    AddIfVampire(npc, walk);
                } catch (...) {
                    ++skipped;
                    logger::warn("SpellScanner: skipped NPC {:08X} while collecting the spells vampires cast (its "
                                 "record could not be read)", formId);
                }
            }
            const std::size_t vampires = walk.vampires;
            const auto byKeyword = std::ranges::count_if(races, [](const auto& r) { return r.second == VampireRace::kKeyword; });
            const auto byName = std::ranges::count_if(races, [](const auto& r) { return r.second == VampireRace::kName; });
            const std::size_t distributed = ReadDistrFiles();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            logger::info("SpellScanner: {} vampire NPCs carry {} spells ({} more handed out by SPID ini files); "
                         "vampire races: {} by the Vampire keyword, {} by an editor id holding \"vamp\"; "
                         "{} NPCs skipped; {} ms",
                vampires, g_vampireSpells.size(), distributed, byKeyword, byName, skipped, elapsedMs);
            return true;
        }
    }

    namespace
    {
        // What the vampire check adds is a bonus tag: when reading it fails the scan goes on without it
        // (and the next scan this session does not try again, so a broken file is not hit on every
        // scan). A failed collection makes every full scan of the session incomplete
        // (VampireCastersState, ScanRecordRules.h): its catalog is written but not recorded, so the
        // next game start scans again.
        void CollectVampireSpells()
        {
            using ScanRecordRules::Casters;
            Casters state = Casters::kFailed;
            try {
                if (CollectVampireSpellsImpl()) state = Casters::kComplete;
            } catch (const std::exception& e) {
                logger::error("SpellScanner: could not collect the spells vampires cast: {}", e.what());
            } catch (...) {
                logger::error("SpellScanner: could not collect the spells vampires cast (a fault outside one NPC)");
            }
            g_vampireState.store(state);
        }
    }

    ScanRecordRules::Casters VampireCastersState() noexcept
    {
        return g_vampireState.load();
    }

    bool IsCastByVampires(RE::FormID spellFormId)
    {
        std::call_once(g_vampireOnce, CollectVampireSpells);
        return g_vampireSpells.contains(spellFormId);
    }
}
