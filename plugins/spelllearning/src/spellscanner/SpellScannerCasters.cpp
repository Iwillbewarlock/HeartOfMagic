#include "Common.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "SpellScanner.h"
#include "PathText.h"
#include "ThreadUtils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
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
// the records). Read once a session, in slices from the main menu on
// (StepVampireCasters, VampirePrefetch in Main.cpp), or all at once by the
// first IsCastByVampires that finds it unfinished.
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
        constexpr std::size_t kSkippedLogMax = 10;   // skipped NPCs logged one by one; the rest only counted

        using VampireSpells = std::unordered_set<RE::FormID>;

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
        std::size_t ReadDistrFile(const std::filesystem::path& path, VampireSpells& vampireSpells)
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
                    added += vampireSpells.insert(spell->GetFormID()).second ? 1 : 0;
                }
            }
            return added;
        }

        std::size_t ReadDistrFiles(VampireSpells& vampireSpells)
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
                        added += ReadDistrFile(it->path(), vampireSpells);
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

        void AddLeveled(RE::TESLevSpell* list, std::unordered_set<RE::FormID>& seenLists, VampireSpells& vampireSpells)
        {
            if (!list || !seenLists.insert(list->GetFormID()).second) return;
            for (auto* form : list->GetContainedForms()) {
                if (!form) continue;
                if (auto* spell = form->As<RE::SpellItem>()) {
                    vampireSpells.insert(spell->GetFormID());
                } else if (auto* nested = form->As<RE::TESLevSpell>()) {
                    AddLeveled(nested, seenLists, vampireSpells);
                }
            }
        }

        void AddSpellList(const RE::TESSpellList::SpellData* data, std::unordered_set<RE::FormID>& seenLists,
            VampireSpells& vampireSpells)
        {
            if (!data) return;
            for (std::uint32_t i = 0; data->spells && i < data->numSpells; ++i) {
                if (data->spells[i]) vampireSpells.insert(data->spells[i]->GetFormID());
            }
            for (std::uint32_t i = 0; data->levSpells && i < data->numlevSpells; ++i) {
                AddLeveled(data->levSpells[i], seenLists, vampireSpells);
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
                const RE::FormID templateId = npc->baseTemplateForm->GetFormID();
                auto it = templateRaces.find(templateId);
                if (it == templateRaces.end()) {
                    // Walked into a local list and kept only once whole: a fault part way (caught by the
                    // caller) leaves nothing behind, so the template's next NPC walks it again
                    std::vector<RE::TESNPC*> npcs;
                    std::vector<RE::TESRace*> found;
                    // depth 1: the same walk CollectSummonedNPCs(npc) makes below a template NPC
                    CollectSummonedNPCs(npc->baseTemplateForm, npcs, 1);
                    for (auto* source : npcs) {
                        auto* race = source->GetRace();
                        if (race && std::ranges::find(found, race) == found.end()) found.push_back(race);
                    }
                    it = templateRaces.emplace(templateId, std::move(found)).first;
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
            VampireSpells spells;
        };

        void AddIfVampire(RE::TESNPC* npc, VampireWalk& walk)
        {
            walk.vampireRaces.clear();
            for (auto* race : TraitRaces(npc, walk.templateRaces, walk.ownRace)) {
                const RE::FormID raceId = race->GetFormID();
                auto it = walk.races.find(raceId);
                if (it == walk.races.end()) {
                    // Judged first, kept after: a race whose judging faults is not cached as "no"
                    const VampireRace verdict = JudgeRace(race);
                    if (verdict == VampireRace::kName) {
                        logger::debug("SpellScanner: vampire race by its editor id only: {}", race->GetFormEditorID());
                    }
                    it = walk.races.emplace(raceId, verdict).first;
                }
                if (it->second != VampireRace::kNo) walk.vampireRaces.push_back(race);
            }
            if (walk.vampireRaces.empty() && !npc->HasKeywordString(kVampireKeyword)) return;

            ++walk.vampires;
            // An NPC that takes its spells from its template does not use its own list in game; the
            // template's NPCs are in this loop themselves, so their spells are not lost
            const bool spellsFromTemplate = npc->UsesTemplate() && npc->baseTemplateForm &&
                npc->actorData.templateUseFlags.any(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kSpells);
            if (!spellsFromTemplate) AddSpellList(npc->actorEffects, walk.seenLists, walk.spells);
            for (auto* race : walk.vampireRaces) {
                if (walk.seenRaces.insert(race->GetFormID()).second) AddSpellList(race->actorEffects, walk.seenLists, walk.spells);
            }
        }

        using Clock = std::chrono::steady_clock;
        using ScanRecordRules::Casters;

        double MsBetween(Clock::time_point from, Clock::time_point to)
        {
            return std::chrono::duration<double, std::milli>(to - from).count();
        }

        // The walk in slices (StepVampireCasters): the NPCs by index, the race, template and list caches
        // kept between slices, the SPID files read in a slice of their own after them. One NPC that
        // cannot be read (a fault in its record or its template's, caught under /EHa) is skipped and
        // logged with its FormID (the first kSkippedLogMax), and the rest are still read: a load order
        // with one broken NPC would otherwise be scanned again at every game start. More than half of
        // them skipped is a failed walk (ScanRecordRules::CasterWalkFailed). The race and template
        // caches take an entry only once it is worked out whole, so a fault leaves none behind.
        class VampireCollection
        {
        public:
            explicit VampireCollection(bool log) : m_log(log) {}

            // True once done, complete or failed. An NPC is read whole within one slice, so a slice
            // can run past its deadline by one NPC's walk.
            bool Step(Clock::time_point deadline)
            {
                if (m_phase == Phase::kDone) return true;
                const auto started = Clock::now();
                if (m_phase == Phase::kStart) m_started = started;
                ++m_slices;
                // What the vampire check adds is a bonus tag: when reading it fails the scan goes on
                // without it, and the session does not try again (a broken file is not hit on every
                // scan). A failed collection makes every full scan of the session incomplete
                // (VampireCastersState, ScanRecordRules.h): its catalog is written but not recorded,
                // so the next game start scans again.
                try {
                    StepImpl(deadline, started);
                } catch (const std::exception& e) {
                    if (m_log) {
                        logger::error("SpellScanner: could not collect the spells vampires cast ({}: {}) - the list is "
                                      "INCOMPLETE, {} spells found before it", typeid(e).name(), e.what(), m_walk.spells.size());
                    }
                    m_state = Casters::kFailed;
                    m_phase = Phase::kDone;
                } catch (...) {
                    if (m_log) logger::error("SpellScanner: could not collect the spells vampires cast (a fault outside one NPC)");
                    m_state = Casters::kFailed;
                    m_phase = Phase::kDone;
                }
                m_workMs += MsBetween(started, Clock::now());
                return m_phase == Phase::kDone;
            }

            [[nodiscard]] Casters State() const noexcept { return m_state; }
            [[nodiscard]] const VampireSpells& Spells() const noexcept { return m_walk.spells; }

        private:
            enum class Phase { kStart, kNpcs, kDistr, kDone };

            void StepImpl(Clock::time_point deadline, Clock::time_point sliceStarted)
            {
                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                if (!dataHandler) {
                    if (m_log) logger::warn("SpellScanner: no data handler - the spells vampires cast are not known");
                    m_state = Casters::kFailed;
                    m_phase = Phase::kDone;
                    return;
                }
                if (m_phase == Phase::kStart) m_phase = Phase::kNpcs;
                if (m_phase == Phase::kNpcs) {
                    // By index, its size read again each slice: the walk sees the array as it is then
                    const auto& npcs = dataHandler->GetFormArray<RE::TESNPC>();
                    while (m_index < npcs.size()) {
                        auto* npc = npcs[m_index++];
                        if (!npc) continue;
                        ++m_npcs;
                        RE::FormID formId = 0;
                        try {
                            formId = npc->GetFormID();
                            AddIfVampire(npc, m_walk);
                        } catch (...) {
                            if (++m_skipped <= kSkippedLogMax && m_log) {
                                logger::warn("SpellScanner: skipped NPC {:08X} while collecting the spells vampires cast (its "
                                             "record could not be read){}", formId,
                                    m_skipped == kSkippedLogMax ? " - further skipped NPCs are only counted" : "");
                            }
                        }
                        if (Clock::now() >= deadline) return;
                    }
                    if (ScanRecordRules::CasterWalkFailed(m_npcs, m_skipped)) {
                        if (m_log) {
                            logger::error("SpellScanner: {} of {} NPCs could not be read - the spells vampires cast are not known",
                                m_skipped, m_npcs);
                        }
                        m_state = Casters::kFailed;
                        m_phase = Phase::kDone;
                        return;
                    }
                    m_phase = Phase::kDistr;
                    if (Clock::now() >= deadline) return;
                }
                // auto-scan's file-level skips (a file that faults is skipped and logged); a fault outside them
                // fails the collection in Step (kFailed: the scan is incomplete, not recorded)
                const auto distrStarted = Clock::now();
                const std::size_t distributed = ReadDistrFiles(m_walk.spells);
                const auto finished = Clock::now();
                m_state = Casters::kComplete;
                m_phase = Phase::kDone;
                if (!m_log) return;
                const auto& races = m_walk.races;
                const auto byKeyword = std::ranges::count_if(races, [](const auto& r) { return r.second == VampireRace::kKeyword; });
                const auto byName = std::ranges::count_if(races, [](const auto& r) { return r.second == VampireRace::kName; });
                logger::info("SpellScanner: {} vampire NPCs carry {} spells ({} more handed out by SPID ini files); "
                             "vampire races: {} by the Vampire keyword, {} by an editor id holding \"vamp\"; "
                             "{} NPCs skipped; {:.0f} ms of work in {} slices (SPID files {:.0f}), {:.0f} ms from first to last",
                    m_walk.vampires, m_walk.spells.size(), distributed, byKeyword, byName, m_skipped,
                    m_workMs + MsBetween(sliceStarted, finished), m_slices,
                    MsBetween(distrStarted, finished), MsBetween(m_started, finished));
            }

            bool m_log;
            Phase m_phase = Phase::kStart;
            Casters m_state = Casters::kNotRun;
            VampireWalk m_walk;
            std::uint32_t m_index = 0;   // BSTArray size_type
            std::size_t m_npcs = 0;
            std::size_t m_skipped = 0;
            std::size_t m_slices = 0;
            double m_workMs = 0.0;
            Clock::time_point m_started;
        };

        // The session's collection. Stepped under the mutex (game thread, and a Papyrus scan's thread);
        // read without it once done, when it no longer changes.
        std::mutex g_vampireMutex;
        VampireCollection g_vampires{ true };
        std::atomic<bool> g_vampiresDone{ false };
        // How the one collection of the session went (VampireCastersState)
        std::atomic<Casters> g_vampireState{ Casters::kNotRun };

        bool StepSessionCollection(Clock::time_point deadline)
        {
            if (g_vampiresDone.load(std::memory_order_acquire)) return true;
            std::lock_guard lock(g_vampireMutex);
            if (!g_vampires.Step(deadline)) return false;
            g_vampireState.store(g_vampires.State());
            g_vampiresDone.store(true, std::memory_order_release);
            return true;
        }
    }

    ScanRecordRules::Casters VampireCastersState() noexcept
    {
        return g_vampireState.load();
    }

    bool StepVampireCasters(std::chrono::steady_clock::time_point deadline)
    {
        return StepSessionCollection(deadline);
    }

    void StartVampirePrefetch()
    {
        constexpr auto kSlice = std::chrono::milliseconds(4);           // AutoScan's default slice
        constexpr auto kBetweenPosts = std::chrono::milliseconds(2);
        constexpr auto kGiveUpAfter = std::chrono::seconds(60);         // a slice not run by then: no task queue
        static std::atomic<bool> s_started{ false };
        if (s_started.exchange(true)) return;
        // Only a head start: a failure anywhere here leaves IsCastByVampires to walk the rest at once
        const auto run = [=] {
            try {
                auto lastStamp = std::make_shared<std::uint32_t>(0);
                while (!g_vampiresDone.load(std::memory_order_acquire)) {
                    auto answer = std::make_shared<std::promise<void>>();
                    auto future = answer->get_future();
                    AddTaskToGameThread("VampirePrefetch", [answer, lastStamp, kSlice]() {
                        // One slice a frame: a task run twice in the frame of the last slice does nothing
                        const std::uint32_t stamp = RE::GetDurationOfApplicationRunTime();
                        if (stamp != *lastStamp) {
                            *lastStamp = stamp;
                            StepSessionCollection(Clock::now() + kSlice);
                        }
                        answer->set_value();
                    });
                    if (future.wait_for(kGiveUpAfter) != std::future_status::ready) {
                        logger::warn("SpellScanner: the vampire walk's slices stopped running - the first scan walks the rest");
                        return;
                    }
                    std::this_thread::sleep_for(kBetweenPosts);
                }
            } catch (const std::exception& e) {
                logger::warn("SpellScanner: the vampire walk's slices stopped ({}) - the first scan walks the rest", e.what());
            }
        };
        try {
            std::thread(run).detach();
        } catch (const std::exception& e) {
            logger::warn("SpellScanner: no thread for the vampire walk's slices ({}) - the first scan walks it", e.what());
        }
    }

    bool IsCastByVampires(RE::FormID spellFormId)
    {
        StepSessionCollection(Clock::time_point::max());
        return g_vampires.Spells().contains(spellFormId);
    }

    bool VampireCastersMatchOneGo(std::string& detail)
    {
        if (!g_vampiresDone.load(std::memory_order_acquire)) {
            detail = "the session's collection is not done";
            return false;
        }
        VampireCollection oneGo{ false };
        oneGo.Step(Clock::time_point::max());
        const auto& stepped = g_vampires.Spells();
        const auto& again = oneGo.Spells();
        std::size_t onlyStepped = 0;
        std::size_t onlyAgain = 0;
        RE::FormID first = 0;
        for (const auto id : stepped) {
            if (!again.contains(id)) {
                ++onlyStepped;
                if (!first) first = id;
            }
        }
        for (const auto id : again) {
            if (!stepped.contains(id)) {
                ++onlyAgain;
                if (!first) first = id;
            }
        }
        const bool same = onlyStepped == 0 && onlyAgain == 0 && oneGo.State() == g_vampireState.load();
        detail = std::format("stepped {} spells, one go {} spells; only stepped {}, only one go {}{}; state {} / {}",
            stepped.size(), again.size(), onlyStepped, onlyAgain,
            first ? std::format(" (first {:08X})", first) : std::string(),
            static_cast<int>(g_vampireState.load()), static_cast<int>(oneGo.State()));
        return same;
    }
}
