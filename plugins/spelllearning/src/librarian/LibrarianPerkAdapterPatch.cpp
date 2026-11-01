#include "librarian/PerkAdapterPatch.h"
#include "librarian/Librarian.h"
#include "librarian/JsonPreread.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/LoadOrderSource.h"
#include "librarian/PerkAdapters.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"
#include "StepTimes.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// =============================================================================
// LibrarianPerkAdapterPatch - applying the perk adapter plan to the game
//
// The planner (LibrarianAdapters.cpp) decides; this file supplies what it asks
// about from the running game and carries the decision out. See
// PerkAdapterPatch.h for the contract and docs/librarian/PERK_ADAPTERS.md for
// the rules.
// =============================================================================

namespace Librarian::PerkAdapters
{
    using namespace Librarian::Detail;
    namespace Plan = Librarian::Adapters;

    namespace
    {
        constexpr const char* ADAPTER_SUBDIR = "adapters";
        constexpr const char* REPORT_FILE = "perk_adapters_report.json";
        constexpr const char* PARITY_FILE = "perk_adapters_parity.json";
        constexpr const char* CONFIG_SECTION = "perkAdapters";

        using KeywordPair = std::pair<RE::FormID, RE::FormID>;   // effect, keyword

        // What is on the effects because of this file, to reconcile on the
        // next run and to keep out of scans. Game thread only, like everything here.
        std::set<KeywordPair> g_injected;

        // Spells put into perk mods' FormLists, the same way: (list, spell).
        // Added to the list record's own array in memory, not through the
        // script path, so nothing reaches a save.
        using ListPair = std::pair<RE::FormID, RE::FormID>;
        std::set<ListPair> g_listed;

        // Half-cost perks given to spells that had none: spell -> perk. The
        // perk is one pointer on the spell record, so it can change in play
        // too (unlike a FormList's array).
        std::unordered_map<RE::FormID, RE::FormID> g_castingPerks;

        RE::BGSListForm* ResolveList(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<RE::BGSListForm>(formId) : nullptr;
        }

        std::vector<Plan::File> LoadFiles(RE::TESDataHandler* dataHandler, json& report)
        {
            std::vector<Plan::File> files;
            std::vector<std::string> problems;
            const auto directory = RulesPath() / ADAPTER_SUBDIR;
            // A file's "requires": {"config": ...} reads config.json the way
            // perkAdapters.enabled is read: unreadable or missing means on
            json config;
            if (!ReadJsonFile(DataPath("config.json"), config)) config = json::object();
            const auto pluginLoaded = [&](const std::string& name) { return PluginLoaded(dataHandler, name); };

            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                if (entry.path().extension() != ".json") continue;
                json document;
                const std::string name = PathText::Utf8(entry.path().filename());
                if (!Preread::Take(entry.path(), document) && !ReadJsonFile(entry.path(), document)) {
                    problems.push_back(name + ": not readable JSON");
                    continue;
                }
                Plan::File file;
                if (!Plan::ParseFile(document, name, file, problems)) continue;
                // The config gate as this run judged it, for readers that compare
                // the report with a config.json edited since (the 1.6 self-test)
                if (!file.requiresConfig.empty()) {
                    report["configJudged"][file.requiresConfig] = Plan::ConfigFlagOn(config, file.requiresConfig);
                }
                if (!Plan::FileApplies(file, config, pluginLoaded)) {
                    report["filesNotApplying"].push_back(name);
                    continue;
                }
                files.push_back(std::move(file));
            }
            Plan::SortFiles(files);

            for (const auto& problem : problems) logger::warn("PerkAdapters: {}", problem);
            report["problems"] = problems;
            return files;
        }

        // Keywords keep their editor ids at runtime, unlike most forms.
        std::unordered_map<std::string, RE::BGSKeyword*> KeywordsByEditorId(RE::TESDataHandler* dataHandler)
        {
            std::unordered_map<std::string, RE::BGSKeyword*> keywords;
            for (auto* keyword : dataHandler->GetFormArray<RE::BGSKeyword>()) {
                if (!keyword) continue;
                const char* editorId = keyword->GetFormEditorID();
                if (editorId && editorId[0] != '\0') keywords.emplace(editorId, keyword);
            }
            return keywords;
        }

        // A line naming its keyword by FormID takes the name the loaded form
        // carries: the plan and the scan's keyword lists both go by that name
        void ResolveKeywordForms(std::vector<Plan::File>& files,
            std::unordered_map<std::string, RE::BGSKeyword*>& keywords)
        {
            for (auto& file : files) {
                for (auto& line : file.lines) {
                    if (line.keywordForm.empty()) continue;
                    const RE::FormID formId = SpellScanner::ResolvePersistentFormId(line.keywordForm);
                    auto* keyword = formId ? RE::TESForm::LookupByID<RE::BGSKeyword>(formId) : nullptr;
                    if (!keyword) {
                        logger::info("PerkAdapters: {} not loaded, {} keeps its name", line.keywordForm, line.keyword);
                        continue;
                    }
                    const char* editorId = keyword->GetFormEditorID();
                    if (editorId && editorId[0] != '\0') line.keyword = editorId;
                    keywords.emplace(line.keyword, keyword);
                }
            }
        }

        // ---------------------------------------------------------------------
        // Applying
        // ---------------------------------------------------------------------

        struct Reconciled
        {
            std::size_t kept = 0;       // added before and still wanted
            std::size_t added = 0;
            std::size_t removed = 0;    // added before, no longer wanted
            std::size_t failed = 0;     // did not take
        };

        // Brings the effects to `wanted`: takes off what an earlier run added
        // and is no longer wanted, adds what is new. A pair a plugin wrote
        // itself is never added nor taken off.
        // g_injected follows every single change as it happens, so a throw
        // halfway leaves it describing the effects exactly - an addition it
        // lost track of would reach the next scan as if a plugin wrote it.
        Reconciled Reconcile(const std::set<KeywordPair>& wanted)
        {
            Reconciled result;
            const std::vector<KeywordPair> previous(g_injected.begin(), g_injected.end());
            for (const auto& pair : previous) {
                if (wanted.contains(pair)) continue;
                auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(pair.first);
                auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(pair.second);
                if (effect && keyword && effect->RemoveKeywords({ keyword })) ++result.removed;
                g_injected.erase(pair);
            }
            for (const auto& pair : wanted) {
                auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(pair.first);
                auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(pair.second);
                if (!effect || !keyword) continue;

                const bool ours = g_injected.contains(pair);
                if (effect->HasKeyword(keyword)) {
                    // Still there from an earlier run, or a plugin wrote it
                    // (then it is not ours to add or take off)
                    if (ours) ++result.kept;
                    continue;
                }
                // Ours but gone - another mod rebuilt the effect's keyword list
                if (ours) g_injected.erase(pair);

                // AddKeywords reports success whatever happens, so count instead.
                const std::uint32_t before = effect->GetNumKeywords();
                effect->AddKeywords({ keyword });
                if (effect->GetNumKeywords() > before) {
                    g_injected.insert(pair);
                    ++result.added;
                } else {
                    ++result.failed;
                }
            }
            return result;
        }

        // The FormList side of Reconcile: the same rules, on list entries.
        Reconciled ReconcileLists(const std::set<ListPair>& wanted)
        {
            Reconciled result;
            const std::vector<ListPair> previous(g_listed.begin(), g_listed.end());
            for (const auto& pair : previous) {
                if (wanted.contains(pair)) continue;
                auto* list = RE::TESForm::LookupByID<RE::BGSListForm>(pair.first);
                auto* spell = RE::TESForm::LookupByID(pair.second);
                if (list && spell) {
                    const auto found = std::find(list->forms.begin(), list->forms.end(), spell);
                    if (found != list->forms.end()) {
                        list->forms.erase(found);
                        ++result.removed;
                    }
                }
                g_listed.erase(pair);
            }
            for (const auto& pair : wanted) {
                auto* list = RE::TESForm::LookupByID<RE::BGSListForm>(pair.first);
                auto* spell = RE::TESForm::LookupByID(pair.second);
                if (!list || !spell) continue;
                const bool ours = g_listed.contains(pair);
                if (list->HasForm(spell)) {
                    if (ours) ++result.kept;
                    continue;
                }
                if (ours) g_listed.erase(pair);
                list->forms.push_back(spell);
                g_listed.insert(pair);
                ++result.added;
            }
            return result;
        }

        // The casting perk side: a spell gets one only while it has none, and
        // loses one only while it still carries the one given here.
        Reconciled ReconcileCastingPerks(const std::unordered_map<RE::FormID, RE::FormID>& wanted)
        {
            Reconciled result;
            const std::vector<std::pair<RE::FormID, RE::FormID>> previous(g_castingPerks.begin(), g_castingPerks.end());
            for (const auto& [spellId, perkId] : previous) {
                const auto want = wanted.find(spellId);
                if (want != wanted.end() && want->second == perkId) continue;
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
                if (spell && spell->data.castingPerk && spell->data.castingPerk->GetFormID() == perkId) {
                    spell->data.castingPerk = nullptr;
                    ++result.removed;
                }
                g_castingPerks.erase(spellId);
            }
            for (const auto& [spellId, perkId] : wanted) {
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellId);
                auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkId);
                // Only what the player casts from a hand: a power, ability or
                // scroll has no casting perk to speak of
                if (!spell || !perk || spell->data.spellType != RE::MagicSystem::SpellType::kSpell) {
                    ++result.failed;
                    continue;
                }
                if (g_castingPerks.contains(spellId)) {
                    if (spell->data.castingPerk == perk) {
                        ++result.kept;
                        continue;
                    }
                    g_castingPerks.erase(spellId);    // someone else set the field since
                }
                if (spell->data.castingPerk) continue;  // the plugin's own, or another mod's
                spell->data.castingPerk = perk;
                g_castingPerks.emplace(spellId, perkId);
                ++result.added;
            }
            return result;
        }

        void WriteReport(const json& report)
        {
            const auto path = DataPath(REPORT_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                logger::warn("PerkAdapters: could not write '{}'", PathText::Utf8(path));
                return;
            }
            file << JsonText::Dump(report, 2);
        }

        // Debug installs (debug.json, as for the scan dump): every pair this run
        // wanted and every pair now applied, sorted, so two runs on one load order
        // can be compared write for write - the report keeps only counts and examples
        template <class Pairs>
        json PairList(const Pairs& pairs)
        {
            std::vector<std::string> lines;
            for (const auto& [first, second] : pairs) lines.push_back(std::format("{:08X} {:08X}", first, second));
            std::sort(lines.begin(), lines.end());
            return lines;
        }

        void WriteParity(std::string_view reason, const std::set<KeywordPair>& wanted, const std::set<ListPair>& wantedLists,
            const std::unordered_map<RE::FormID, RE::FormID>& wantedPerks, std::size_t unresolved)
        {
            if (!SpellScanner::DebugScanDumpEnabled()) return;
            json parity;
            parity["reason"] = std::string(reason);
            parity["unresolved"] = unresolved;
            parity["wanted"] = { { "effectKeyword", PairList(wanted) }, { "listSpell", PairList(wantedLists) },
                { "spellPerk", PairList(wantedPerks) } };
            parity["applied"] = { { "effectKeyword", PairList(g_injected) }, { "listSpell", PairList(g_listed) },
                { "spellPerk", PairList(g_castingPerks) } };
            const auto path = DataPath(PARITY_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                logger::warn("PerkAdapters: could not write '{}'", PathText::Utf8(path));
                return;
            }
            file << JsonText::Dump(parity, 2);
        }

        // Nothing is wanted: take back everything, say why, write the report.
        // Perk FormLists only on the kDataLoaded run, as in ApplyImpl.
        void Stop(json& report, std::string_view why, bool listsNow)
        {
            const Reconciled result = Reconcile({});
            const Reconciled lists = listsNow ? ReconcileLists({}) : Reconciled{};
            const Reconciled perks = ReconcileCastingPerks({});
            report["stopped"] = std::string(why);
            report["listsDeferred"] = !listsNow;
            report["removed"] = result.removed;
            report["listRemoved"] = lists.removed;
            report["castingPerkRemoved"] = perks.removed;
            WriteReport(report);
            WriteParity(why, {}, {}, {}, 0);   // nothing wanted: an older run's file must not stand in
            logger::info("PerkAdapters: {} ({} keywords, {} list entries and {} casting perks taken back)",
                why, result.removed, lists.removed, perks.removed);
        }

        void ApplyImpl(std::string_view reason)
        {
            const auto started = std::chrono::steady_clock::now();
            StepTimes stages;

            json report;
            report["reason"] = std::string(reason);

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return;

            if (!ConfigSwitchOn(CONFIG_SECTION)) {
                report["enabled"] = false;
                Stop(report, "switched off in config.json", reason == kDataLoadedReason);
                return;
            }
            report["enabled"] = true;

            // On the heap: freed on a worker at the end (below)
            auto catalogOwner = std::make_unique<json>();
            json& catalog = *catalogOwner;
            if ((!Preread::Take(CatalogFilePath(), catalog) && !LoadCatalog(catalog)) || !catalog.is_object()
                || !catalog.contains("spells") || !catalog["spells"].is_object()) {
                Stop(report, "no catalog yet - a full scan makes one", reason == kDataLoadedReason);
                return;
            }
            stages.Mark("catalog");

            std::vector<Plan::File> files = LoadFiles(dataHandler, report);
            if (files.empty()) {
                Stop(report, "no adapter file applies to this load order", reason == kDataLoadedReason);
                return;
            }
            stages.Mark("files");

            auto keywords = KeywordsByEditorId(dataHandler);
            ResolveKeywordForms(files, keywords);
            stages.Mark("keywords");
            auto loadOrderOwner = std::make_unique<LoadOrderSource>(catalog["spells"]);
            LoadOrderSource& loadOrder = *loadOrderOwner;
            stages.Mark("source");

            Plan::PlanSource source;
            source.forEachCatalogSpell = [&](const auto& visit) { loadOrder.ForEachCatalogSpell(visit); };
            source.usersOf = [&](const std::string& key) { return loadOrder.UsersOf(key); };
            source.keywordDefined = [&](const std::string& keyword) { return keywords.contains(keyword); };
            // A formList line asks about every catalog spell: resolve each list once
            std::unordered_map<std::string, RE::BGSListForm*> resolvedLists;
            const auto listOf = [&](const std::string& formList) {
                const auto found = resolvedLists.find(formList);
                if (found != resolvedLists.end()) return found->second;
                return resolvedLists.emplace(formList, ResolveList(formList)).first->second;
            };
            source.formListDefined = [&](const std::string& formList) { return listOf(formList) != nullptr; };
            // As the plugins (and scripts) left it: an entry this file put there does not count
            source.inFormList = [&](const std::string& formList, const std::string& spellId) {
                auto* list = listOf(formList);
                auto* spell = RE::TESForm::LookupByID(SpellScanner::ResolvePersistentFormId(spellId));
                if (!list || !spell) return false;
                return list->HasForm(spell) && !g_listed.contains({ list->GetFormID(), spell->GetFormID() });
            };

            const Plan::Plan plan = Plan::BuildPlan(files, source);
            stages.Mark("plan");

            std::set<KeywordPair> wanted;
            std::size_t unresolved = 0;
            for (const auto& write : plan.writes) {
                auto* effect = loadOrder.Effect(write.effectKey);
                const auto keyword = keywords.find(write.keyword);
                if (!effect || keyword == keywords.end()) {
                    ++unresolved;
                    continue;
                }
                wanted.insert({ effect->GetFormID(), keyword->second->GetFormID() });
            }
            const Reconciled result = Reconcile(wanted);

            std::set<ListPair> wantedLists;
            for (const auto& write : plan.listWrites) {
                auto* list = listOf(write.formList);
                auto* spell = RE::TESForm::LookupByID(SpellScanner::ResolvePersistentFormId(write.spellId));
                if (!list || !spell) {
                    ++unresolved;
                    continue;
                }
                wantedLists.insert({ list->GetFormID(), spell->GetFormID() });
            }
            const bool listsNow = reason == kDataLoadedReason;
            const Reconciled lists = listsNow ? ReconcileLists(wantedLists) : Reconciled{};
            report["listsDeferred"] = !listsNow;

            std::unordered_map<RE::FormID, RE::FormID> wantedPerks;
            for (const auto& write : plan.castingPerkWrites) {
                const RE::FormID spellId = SpellScanner::ResolvePersistentFormId(write.spellId);
                const RE::FormID perkId = SpellScanner::ResolvePersistentFormId(write.perk);
                if (spellId && perkId) wantedPerks.emplace(spellId, perkId);
            }
            // A write that did not resolve, or not a hand-cast spell: in "failed"
            Reconciled perks = ReconcileCastingPerks(wantedPerks);
            perks.failed += plan.castingPerkWrites.size() - wantedPerks.size();
            stages.Mark("reconcile");

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            report["added"] = result.added;
            report["kept"] = result.kept;
            report["removed"] = result.removed;
            report["failed"] = result.failed;
            report["unresolved"] = unresolved;
            report["listAdded"] = lists.added;
            report["listKept"] = lists.kept;
            report["listRemoved"] = lists.removed;
            report["castingPerkAdded"] = perks.added;
            report["castingPerkKept"] = perks.kept;
            report["castingPerkRemoved"] = perks.removed;
            report["castingPerkFailed"] = perks.failed;
            report["elapsedMs"] = elapsed;
            report["entriesWithoutSources"] = plan.entriesWithoutSources;
            report["files"] = Plan::PlanReport(files, plan);
            WriteReport(report);
            stages.Mark("report");
            WriteParity(reason, wanted, wantedLists, wantedPerks, unresolved);
            stages.Mark("parity");

            logger::info("PerkAdapters: {} - {} keywords on effects ({} new, {} kept, {} taken back, {} did not take, "
                         "{} unresolved), {} spells in perk FormLists ({} new, {} taken back), {} casting perks "
                         "({} new, {} taken back) from {} adapter files in {} ms",
                reason, g_injected.size(), result.added, result.kept, result.removed, result.failed, unresolved,
                g_listed.size(), lists.added, lists.removed, g_castingPerks.size(), perks.added, perks.removed,
                files.size(), elapsed);
            if (!listsNow) logger::info("PerkAdapters: FormLists follow this plan at the next game start");
            // The ms above stop before the report; these run to the end (the catalog
            // and the spell JSON are freed after it, on a worker)
            const auto& timings = loadOrder.GetTimings();
            const auto& parts = timings.spellJsonParts;
            logger::info("PerkAdapters: stages (ms) - {}; in source and plan: tomes {:.0f}, catalog ids {:.0f}, "
                         "spell JSON {:.0f} (name {:.0f}, tier {:.0f}, effects {:.0f}, keywords {:.0f}, evidence {:.0f}, "
                         "traits {:.0f}), effect users {:.0f}; {:.0f} ms in all",
                stages.Text(), timings.tomes, timings.resolve, timings.spellJson, parts.name, parts.tier, parts.effects,
                parts.keywords, parts.evidence, parts.traits, timings.users, stages.TotalMs());

            // Thousands of small JSON objects took about 110 ms to free on the game thread (the
            // 2026-11-01 timers). Plain memory, no game form is touched: freed on a worker.
            std::thread([catalogOwner = std::move(catalogOwner), loadOrderOwner = std::move(loadOrderOwner)]() mutable {
                loadOrderOwner.reset();   // holds a reference into the catalog: first
                catalogOwner.reset();
            }).detach();
        }
    }

    void ReadFilesAhead()
    {
        // Only a head start: any failure here leaves Apply to read the files as before
        try {
            std::vector<std::filesystem::path> paths{ CatalogFilePath() };
            std::error_code error;
            for (std::filesystem::directory_iterator it(RulesPath() / ADAPTER_SUBDIR, error), end; !error && it != end;
                 it.increment(error)) {
                if (it->path().extension() == ".json") paths.push_back(it->path());
            }
            Preread::Start(std::move(paths));
        } catch (const std::exception& e) {
            logger::warn("PerkAdapters: could not list the files to read ahead ({})", e.what());
        }
    }

    bool IsInjected(RE::FormID effect, RE::FormID keyword)
    {
        return !g_injected.empty() && g_injected.contains({ effect, keyword });
    }

    RE::BGSPerk* OriginalCastingPerk(const RE::SpellItem* spell)
    {
        if (!spell || !spell->data.castingPerk) return nullptr;
        const auto given = g_castingPerks.find(spell->GetFormID());
        if (given != g_castingPerks.end() && given->second == spell->data.castingPerk->GetFormID()) return nullptr;
        return spell->data.castingPerk;
    }

    void Reassert(std::string_view reason)
    {
        std::size_t restored = 0;
        std::size_t failed = 0;
        for (const auto& [effectId, keywordId] : g_injected) {
            auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(effectId);
            auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(keywordId);
            if (!effect || !keyword || effect->HasKeyword(keyword)) continue;
            // AddKeywords reports success whatever happens, so count instead.
            const std::uint32_t before = effect->GetNumKeywords();
            effect->AddKeywords({ keyword });
            if (effect->GetNumKeywords() > before) {
                ++restored;
            } else {
                ++failed;
            }
        }
        if (restored || failed) {
            logger::info("PerkAdapters: {} - {} keywords another mod took off put back ({} did not take)",
                reason, restored, failed);
        }
    }

    void Apply(std::string_view reason)
    {
        // Runs right after a scan and at load; a failure here must not take
        // either down, and leaves whatever was already applied.
        try {
            ApplyImpl(reason);
        } catch (const std::exception& e) {
            logger::error("PerkAdapters: {} failed - {}", reason, e.what());
        }
    }
}
