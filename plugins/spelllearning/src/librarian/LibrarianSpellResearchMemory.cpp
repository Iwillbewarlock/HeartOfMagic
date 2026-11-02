#include "librarian/SpellResearchMemory.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/LoadOrderSource.h"
#include "librarian/SpellResearch.h"
#include "SpellScanner.h"

#include <algorithm>
#include <array>
#include <format>

// =============================================================================
// LibrarianSpellResearchMemory - practice and memory, the game side
//
// See SpellResearchMemory.h for what runs when, SpellResearchMemoryMath.h for the
// numbers and docs/librarian/SPELL_RESEARCH.md for SR's lists and globals.
// =============================================================================

namespace Librarian::SpellResearchMemory
{
    using namespace Librarian::Detail;
    namespace MM = Librarian::SpellResearchMemoryMath;
    namespace SR = Librarian::SpellResearch;

    namespace
    {
        constexpr const char* SR_PLUGIN = "SpellResearch.esp";
        constexpr const char* RULES_FILE = "compat/spellresearch.json";
        constexpr float kSetUp = 1.0f;       // _SR_GlobalIsSetup once an Import finished
        constexpr float kNotImporting = 0.0f;

        using RankGlobals = std::array<RE::TESGlobal*, MM::kRanks>;

        bool g_installed = false;
        std::array<RE::BGSListForm*, MM::kRanks> g_tierLists{};
        RE::TESGlobal* g_isSetup = nullptr;
        RE::TESGlobal* g_importing = nullptr;
        RE::TESGlobal* g_timeBase = nullptr;
        RE::TESGlobal* g_craftDifficulty = nullptr;   // optional: MM::kDefaultCraftDifficulty without it
        // _SR_ListSpellsArchetypes and _SR_ListGlobalsArchetypes, in the same archetype order.
        // An archetype whose globals did not resolve to five TESGlobal has a null spell list.
        std::vector<RE::BGSListForm*> g_archetypeSpells;
        std::vector<RankGlobals> g_archetypeGlobals;

        // Written by ApplyConfig, read by the cast hooks: both on the game thread, no lock
        MM::Settings g_settings;

        RE::FormID Resolve(const std::string& persistentId)
        {
            return persistentId.empty() ? 0 : SpellScanner::ResolvePersistentFormId(persistentId);
        }

        template <class T>
        T* ResolveAs(const std::string& persistentId)
        {
            const RE::FormID formId = Resolve(persistentId);
            return formId ? RE::TESForm::LookupByID<T>(formId) : nullptr;
        }

        std::string Text(const nlohmann::json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string()) ? found->get<std::string>() : std::string();
        }

        // A FormList's slots by index: the record entries (forms[i]), then the script-added ones as stored.
        // A null or unresolved entry keeps its slot (nullptr): ForEachForm skips it, which would move every
        // later archetype onto its neighbour's globals. SR's outer and per-archetype lists come from the ESP;
        // the index order of script-added entries is not verified, so any there are counted for a warning.
        std::vector<RE::TESForm*> Slots(const RE::BGSListForm* list, std::size_t& scriptAdded)
        {
            std::vector<RE::TESForm*> slots;
            if (!list) return slots;
            for (std::uint32_t i = 0; i < list->forms.size(); ++i) slots.push_back(list->forms[i]);
            if (list->scriptAddedTempForms) {
                for (const RE::FormID id : *list->scriptAddedTempForms) {
                    slots.push_back(RE::TESForm::LookupByID(id));
                    ++scriptAdded;
                }
            }
            return slots;
        }

        // The 60 x 5 memory globals and the 60 spell lists, paired by index. False (with a
        // warning) when the two outer lists do not line up: an archetype's globals would be
        // another archetype's.
        bool ResolveArchetypes(RE::BGSListForm* spellsOuter, RE::BGSListForm* globalsOuter)
        {
            std::size_t scriptAdded = 0;
            const auto spellLists = Slots(spellsOuter, scriptAdded);
            const auto globalLists = Slots(globalsOuter, scriptAdded);
            if (spellLists.empty() || spellLists.size() != globalLists.size()) {
                logger::warn("SpellResearch memory: {} archetype spell lists but {} archetype global lists - off",
                    spellLists.size(), globalLists.size());
                return false;
            }
            std::size_t incomplete = 0;
            for (std::size_t a = 0; a < spellLists.size(); ++a) {
                auto* spells = spellLists[a] ? spellLists[a]->As<RE::BGSListForm>() : nullptr;
                RankGlobals globals{};
                const auto* inner = globalLists[a] ? globalLists[a]->As<RE::BGSListForm>() : nullptr;
                const auto entries = Slots(inner, scriptAdded);
                bool whole = spells && entries.size() == MM::kRanks;
                for (std::size_t t = 0; whole && t < MM::kRanks; ++t) {
                    globals[t] = entries[t] ? entries[t]->As<RE::TESGlobal>() : nullptr;
                    whole = globals[t] != nullptr;
                }
                if (!whole) {
                    ++incomplete;
                    spells = nullptr;
                    globals = {};
                }
                g_archetypeSpells.push_back(spells);
                g_archetypeGlobals.push_back(globals);
            }
            if (incomplete > 0) {
                logger::warn("SpellResearch memory: {} of {} archetypes have no spell list or not five globals - left out",
                    incomplete, spellLists.size());
            }
            if (scriptAdded > 0) {
                logger::warn("SpellResearch memory: {} script-added entries in SR's archetype lists (expected none: the "
                             "ESP fills them) - their order is taken as stored", scriptAdded);
            }
            return incomplete < spellLists.size();
        }

        std::string SpellName(const RE::SpellItem* spell)
        {
            const char* name = spell ? spell->GetName() : nullptr;
            return name && *name ? std::string(name) : std::string("?");
        }
    }

    void Install()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !PluginLoaded(dataHandler, SR_PLUGIN)) return;

        nlohmann::json rules;
        SR::Lists lists;
        std::vector<std::string> problems;
        if (!ReadJsonFile(RulesPath() / RULES_FILE, rules) || !SR::ParseLists(rules, lists, problems)) {
            logger::warn("SpellResearch memory: {} is missing or incomplete - practice and memory off", RULES_FILE);
            return;
        }
        const auto section = rules.find("lists");
        const std::string spellsId = Text(*section, "archetypeSpells");
        const std::string globalsId = Text(*section, "archetypeGlobals");
        const std::string timeBaseId = Text(rules, "researchTimeBase");
        const std::string difficultyId = Text(rules, "craftSpellDifficulty");

        for (std::size_t t = 0; t < MM::kRanks; ++t) g_tierLists[t] = ResolveAs<RE::BGSListForm>(lists.allSpells[t]);
        g_isSetup = ResolveAs<RE::TESGlobal>(lists.isSetup);
        g_importing = ResolveAs<RE::TESGlobal>(lists.importing);
        g_timeBase = ResolveAs<RE::TESGlobal>(timeBaseId);
        g_craftDifficulty = ResolveAs<RE::TESGlobal>(difficultyId);
        if (!g_craftDifficulty) {
            logger::warn("SpellResearch memory: craftSpellDifficulty '{}' not found - the practice boost uses difficulty {:.1f}",
                difficultyId, MM::kDefaultCraftDifficulty);
        }
        auto* spellsOuter = ResolveAs<RE::BGSListForm>(spellsId);
        auto* globalsOuter = ResolveAs<RE::BGSListForm>(globalsId);
        const bool tiers = std::ranges::all_of(g_tierLists, [](const auto* list) { return list != nullptr; });
        if (!tiers || !g_isSetup || !g_importing || !g_timeBase || !spellsOuter || !globalsOuter) {
            logger::warn("SpellResearch memory: {} does not have the forms {} names (lists.archetypeSpells '{}', "
                         "lists.archetypeGlobals '{}', researchTimeBase '{}') - practice and memory off",
                SR_PLUGIN, RULES_FILE, spellsId, globalsId, timeBaseId);
            return;
        }
        if (!ResolveArchetypes(spellsOuter, globalsOuter)) return;
        g_installed = true;
        logger::info("SpellResearch memory: {} archetypes x {} ranks of memory globals found - practice and memory on "
                     "once SR's Import has run", g_archetypeGlobals.size(), MM::kRanks);
    }

    void ApplyConfig(const nlohmann::json& config)
    {
        static bool applied = false;   // the first call (game start) logs at info
        const MM::Settings next = MM::ReadSettings(config);
        const bool changed = !applied || next.practiceToMemory != g_settings.practiceToMemory ||
                             next.memoryMinutes != g_settings.memoryMinutes ||
                             next.memoryToPractice != g_settings.memoryToPractice ||
                             next.maxPracticeBoost != g_settings.maxPracticeBoost ||
                             next.cheatMode != g_settings.cheatMode;
        applied = true;
        g_settings = next;
        // Every panel save comes through here: info only when a value changed
        const std::string line = std::format("SpellResearch memory: settings - practice to memory {} ({:.0f} research "
            "minutes per 15%), memory to practice {} (up to +{:.0f}%){}", g_settings.practiceToMemory,
            g_settings.memoryMinutes, g_settings.memoryToPractice, g_settings.maxPracticeBoost,
            g_settings.cheatMode ? " - both off while cheat mode is on" : "");
        if (changed) {
            logger::info("{}", line);
        } else {
            logger::debug("{}", line);
        }
    }

    bool Installed()
    {
        return g_installed;
    }

    float CraftDifficulty()
    {
        return MM::CraftDifficulty(g_craftDifficulty ? g_craftDifficulty->value : 0.0f);
    }

    bool Ready(std::string& why)
    {
        if (!g_installed) {
            why = "Spell Research not loaded or its lists not found";
            return false;
        }
        if (g_isSetup->value != kSetUp) {
            why = "Spell Research's Import has not run";
            return false;
        }
        if (g_importing->value != kNotImporting) {
            why = "Spell Research's Import is running";
            return false;
        }
        return true;
    }

    std::optional<SpellMemory> Lookup(const RE::SpellItem* spell)
    {
        if (!g_installed || !spell) return std::nullopt;
        SpellMemory memory;
        // A spell in two tier lists takes the lowest: the first SR's own lookups would meet
        const auto tier = std::ranges::find_if(g_tierLists, [&](const auto* list) { return list->HasForm(spell); });
        if (tier == g_tierLists.end()) return std::nullopt;
        memory.rank = static_cast<std::size_t>(tier - g_tierLists.begin());
        for (std::size_t a = 0; a < g_archetypeSpells.size(); ++a) {
            if (g_archetypeSpells[a] && g_archetypeSpells[a]->HasForm(spell)) memory.archetypes.push_back(a);
        }
        if (memory.archetypes.empty()) return std::nullopt;
        return memory;
    }

    RE::TESGlobal* Global(std::size_t archetype, std::size_t rank)
    {
        if (archetype >= g_archetypeGlobals.size() || rank >= MM::kRanks) return nullptr;
        return g_archetypeGlobals[archetype][rank];
    }

    float PaymentValue()
    {
        return g_timeBase ? MM::ResearchValue(g_timeBase->value, g_settings.memoryMinutes) : 0.0f;
    }

    std::size_t Pay(const SpellMemory& memory, int payments, float value)
    {
        const auto gains = MM::GainTable(memory.rank, value);
        std::size_t changed = 0;
        for (const std::size_t a : memory.archetypes) {
            for (std::size_t t = 0; t < MM::kRanks; ++t) {
                auto* global = Global(a, t);
                if (!global || !(gains[t] > 0.0f)) continue;
                // LIMIT: SR's Papyrus GlobalVariable.Mod is GetValue then SetValue; a write here landing
                // between the two loses one of them (only while SR research runs and a cast crosses a
                // boundary: one payment at most). Not guarded; SR's own writes would need a lock it lacks.
                const float before = global->value;
                float after = before;
                for (int p = 0; p < payments; ++p) after = MM::ApplyGain(after, gains[t]);
                if (after != before) {
                    global->value = after;
                    ++changed;
                }
            }
        }
        return changed;
    }

    float KnownShare(const SpellMemory& memory)
    {
        std::vector<float> values;
        values.reserve(memory.archetypes.size());
        for (const std::size_t a : memory.archetypes) {
            const auto* global = Global(a, memory.rank);
            values.push_back(global ? global->value : 0.0f);
        }
        return MM::KnownShare(values.data(), values.size(), memory.rank, CraftDifficulty());
    }

    MM::Settings CurrentSettings()
    {
        return g_settings;
    }

    void OnPracticeProgress(RE::FormID spellId, float progressBefore, float progressAfter)
    {
        // Cheat mode (unlock, relock, XP edits) is not practice: nothing becomes memory while it is on
        if (!g_settings.practiceToMemory || g_settings.cheatMode) return;
        // LIMIT: no per-spell record of what was paid. Progress from other sources inside a chunk is paid
        // by the next cast that crosses its boundary, and a boundary crossed by a non-cast source (a tome,
        // passive, ISL study) pays nothing. A progress reset pays again: RelockSpell (cheat mode) or
        // SetSpellXP(spell, 0) (Papyrus/C++ API, the panel's XP edit) puts the spell back below its
        // boundaries, and practising it again pays its 7 payments a second time. Upgrade path: a per-spell
        // "highest boundary paid" byte in the co-save progress record (a version bump), checked here.
        const int crossed = MM::BoundariesCrossed(progressBefore, progressAfter);
        if (crossed == 0) return;
        const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
        std::string why;
        if (!Ready(why)) {
            logger::debug("SpellResearch memory: {} ({:08X}) crossed {} boundaries - nothing paid: {}",
                SpellName(spell), spellId, crossed, why);
            return;
        }
        const auto memory = Lookup(spell);
        if (!memory) {
            logger::debug("SpellResearch memory: {} ({:08X}) crossed {} boundaries - nothing paid: in no SR tier "
                          "list or no archetype list", SpellName(spell), spellId, crossed);
            return;
        }
        const float value = PaymentValue();
        if (!(value > 0.0f)) {
            logger::debug("SpellResearch memory: {} ({:08X}) - nothing paid: v is 0 (memoryMinutes {:.0f})",
                SpellName(spell), spellId, g_settings.memoryMinutes);
            return;
        }
        const std::size_t changed = Pay(*memory, crossed, value);
        logger::info("SpellResearch memory: {} ({:08X}) {:.1f}% -> {:.1f}%, {} boundaries crossed - {} payment(s) of "
                     "v {:.2f} at rank {} into {} archetypes ({} globals changed)",
            SpellName(spell), spellId, progressBefore * 100.0f, progressAfter * 100.0f, crossed, crossed, value,
            memory->rank, memory->archetypes.size(), changed);
    }

    float PracticeMultiplier(RE::FormID spellId)
    {
        if (!g_settings.memoryToPractice || g_settings.cheatMode || !(g_settings.maxPracticeBoost > 0.0f)) return 1.0f;
        std::string why;
        if (!Ready(why)) {
            logger::trace("SpellResearch memory: no practice boost for {:08X}: {}", spellId, why);
            return 1.0f;
        }
        const auto memory = Lookup(RE::TESForm::LookupByID<RE::SpellItem>(spellId));
        if (!memory) {
            logger::trace("SpellResearch memory: no practice boost for {:08X}: in no SR tier or archetype list", spellId);
            return 1.0f;
        }
        const float share = KnownShare(*memory);
        const float multiplier = MM::PracticeMultiplier(g_settings.maxPracticeBoost, share);
        logger::debug("SpellResearch memory: {:08X} rank {}, {:.0f}% of {} archetypes remembered - casting XP x{:.3f}",
            spellId, memory->rank, share * 100.0f, memory->archetypes.size(), multiplier);
        return multiplier;
    }

    RE::SpellItem* SampleSpell()
    {
        for (const auto* list : g_tierLists) {
            RE::SpellItem* found = nullptr;
            if (!list) continue;
            list->ForEachForm([&](RE::TESForm* form) {
                auto* spell = form->As<RE::SpellItem>();
                if (spell && Lookup(spell)) {
                    found = spell;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            if (found) return found;
        }
        return nullptr;
    }

    void OnGameLoaded()
    {
        std::string why;
        if (!Ready(why)) {
            if (g_installed) logger::debug("SpellResearch memory: save loaded - {}", why);
            return;
        }
        double sum = 0.0;
        std::size_t above = 0;
        std::size_t total = 0;
        for (const auto& ranks : g_archetypeGlobals) {
            for (const auto* global : ranks) {
                if (!global) continue;
                ++total;
                sum += global->value;
                if (global->value > 0.0f) ++above;
            }
        }
        logger::info("SpellResearch memory: save loaded - {} of {} memory globals above 0, sum {:.2f}", above, total, sum);
    }
}
