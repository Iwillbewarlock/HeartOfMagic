#include "librarian/PerkAdapterPatch.h"
#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/PerkAdapters.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <set>
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
        constexpr const char* CONFIG_FILE = "config.json";
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

        RE::BGSListForm* ResolveList(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<RE::BGSListForm>(formId) : nullptr;
        }

        // Absent or unreadable config means on: the point is perks that work
        // for a player who never opens the settings.
        bool Enabled()
        {
            json config;
            if (!ReadJsonFile(DataPath(CONFIG_FILE), config) || !config.is_object()) return true;
            const auto section = config.find(CONFIG_SECTION);
            if (section == config.end() || !section->is_object()) return true;
            const auto enabled = section->find("enabled");
            return !(enabled != section->end() && enabled->is_boolean() && !enabled->get<bool>());
        }

        bool PluginLoaded(RE::TESDataHandler* dataHandler, const std::string& name)
        {
            return dataHandler->LookupLoadedModByName(name) || dataHandler->LookupLoadedLightModByName(name);
        }

        bool FileApplies(RE::TESDataHandler* dataHandler, const Plan::File& file)
        {
            if (file.always) return true;
            return std::ranges::any_of(file.requiresAny,
                [&](const std::string& name) { return PluginLoaded(dataHandler, name); });
        }

        std::vector<Plan::File> LoadFiles(RE::TESDataHandler* dataHandler, json& report)
        {
            std::vector<Plan::File> files;
            std::vector<std::string> problems;
            const auto directory = RulesPath() / ADAPTER_SUBDIR;

            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                if (entry.path().extension() != ".json") continue;
                json document;
                const std::string name = PathText::Utf8(entry.path().filename());
                if (!ReadJsonFile(entry.path(), document)) {
                    problems.push_back(name + ": not readable JSON");
                    continue;
                }
                Plan::File file;
                if (!Plan::ParseFile(document, name, file, problems)) continue;
                if (!FileApplies(dataHandler, file)) {
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
        // The load order as the planner sees it
        // ---------------------------------------------------------------------

        class LoadOrderSource
        {
        public:
            explicit LoadOrderSource(const json& catalogSpells) :
                m_catalog(catalogSpells)
            {
                m_fields.editorId = false;
                m_fields.magickaCost = false;
                m_fields.castingType = true;
                m_fields.effects = true;
                m_fields.keywords = true;
                m_fields.effectDetails = true;
                IndexUsers();

                // Resolved once: the planner walks the catalog once per line.
                for (const auto& [id, entry] : m_catalog.items()) {
                    const RE::FormID formId = SpellScanner::ResolvePersistentFormId(id);
                    auto* spell = formId ? RE::TESForm::LookupByID<RE::SpellItem>(formId) : nullptr;
                    if (spell) m_catalogSpells.push_back({ id, spell, &entry });
                }
            }

            void ForEachCatalogSpell(const std::function<void(const std::string&, const json&, const json&)>& visit)
            {
                for (const auto& catalogSpell : m_catalogSpells) {
                    visit(catalogSpell.id, SpellJson(catalogSpell.spell), *catalogSpell.entry);
                }
            }

            std::vector<Plan::EffectUser> UsersOf(const std::string& effectKey)
            {
                std::vector<Plan::EffectUser> result;
                const auto found = m_users.find(EffectFormId(effectKey));
                if (found == m_users.end()) return result;

                // An item the planner cannot judge blocks the write whatever the
                // rest are, so hand it over alone rather than building the JSON
                // of every spell that shares the effect. By form type, not
                // As<SpellItem>(): a scroll derives from SpellItem and blocks.
                // A runtime spell (0xFF) has no persistent id and blocks too.
                std::vector<std::pair<RE::SpellItem*, std::string>> spells;
                for (auto* item : found->second) {
                    std::string id = SpellScanner::GetPersistentFormId(item->GetFormID());
                    if (item->GetFormType() != RE::FormType::Spell || id.empty()) {
                        result.push_back({ std::move(id), nullptr, nullptr });
                        return result;
                    }
                    spells.emplace_back(static_cast<RE::SpellItem*>(item), std::move(id));
                }
                for (auto& [spell, id] : spells) {
                    Plan::EffectUser user;
                    user.spell = &SpellJson(spell);
                    const auto entry = m_catalog.find(id);
                    user.catalogEntry = entry == m_catalog.end() ? nullptr : &*entry;
                    user.id = std::move(id);
                    result.push_back(std::move(user));
                }
                return result;
            }

            RE::EffectSetting* Effect(const std::string& effectKey)
            {
                const RE::FormID formId = EffectFormId(effectKey);
                return formId ? RE::TESForm::LookupByID<RE::EffectSetting>(formId) : nullptr;
            }

        private:
            struct CatalogSpell
            {
                std::string id;
                RE::SpellItem* spell = nullptr;
                const json* entry = nullptr;
            };

            // Every magic item by the effects it carries: spells, and the
            // enchantments, scrolls, potions and ingredients that block a write.
            void IndexUsers()
            {
                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                const auto add = [&](RE::MagicItem* item) {
                    if (!item) return;
                    for (const auto* effect : item->effects) {
                        if (!effect || !effect->baseEffect) continue;
                        auto& list = m_users[effect->baseEffect->GetFormID()];
                        if (list.empty() || list.back() != item) list.push_back(item);
                    }
                };
                for (auto* item : dataHandler->GetFormArray<RE::SpellItem>()) add(item);
                for (auto* item : dataHandler->GetFormArray<RE::EnchantmentItem>()) add(item);
                for (auto* item : dataHandler->GetFormArray<RE::ScrollItem>()) add(item);
                for (auto* item : dataHandler->GetFormArray<RE::AlchemyItem>()) add(item);
                for (auto* item : dataHandler->GetFormArray<RE::IngredientItem>()) add(item);
            }

            // An effect key is the scan's "form", a persistent id. A runtime
            // form (0xFF) has none and resolves to 0: nothing is written to it.
            RE::FormID EffectFormId(const std::string& effectKey)
            {
                const auto cached = m_effectIds.find(effectKey);
                if (cached != m_effectIds.end()) return cached->second;
                const RE::FormID formId = SpellScanner::ResolvePersistentFormId(effectKey);
                m_effectIds.emplace(effectKey, formId);
                return formId;
            }

            // Built once per spell, the scan's own shape, so the planner reads
            // exactly the fields it was measured on. The scanner leaves out the
            // keywords added here, so a spell's JSON is as its plugins wrote it.
            const json& SpellJson(RE::SpellItem* spell)
            {
                auto& slot = m_spellJson[spell->GetFormID()];
                if (!slot) {
                    slot = std::make_unique<json>(SpellScanner::BuildSpellJson(spell, spell->GetFormID(), m_fields));
                }
                return *slot;
            }

            const json& m_catalog;
            SpellScanner::FieldConfig m_fields;
            std::vector<CatalogSpell> m_catalogSpells;
            std::unordered_map<RE::FormID, std::vector<RE::MagicItem*>> m_users;
            std::unordered_map<RE::FormID, std::unique_ptr<json>> m_spellJson;
            std::unordered_map<std::string, RE::FormID> m_effectIds;
        };

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

        // Nothing is wanted: take back everything, say why, write the report.
        // Perk FormLists only on the kDataLoaded run, as in ApplyImpl.
        void Stop(json& report, std::string_view why, bool listsNow)
        {
            const Reconciled result = Reconcile({});
            const Reconciled lists = listsNow ? ReconcileLists({}) : Reconciled{};
            report["stopped"] = std::string(why);
            report["listsDeferred"] = !listsNow;
            report["removed"] = result.removed;
            report["listRemoved"] = lists.removed;
            WriteReport(report);
            logger::info("PerkAdapters: {} ({} keywords and {} list entries taken back)", why, result.removed, lists.removed);
        }

        void ApplyImpl(std::string_view reason)
        {
            const auto started = std::chrono::steady_clock::now();

            json report;
            report["reason"] = std::string(reason);

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return;

            if (!Enabled()) {
                report["enabled"] = false;
                Stop(report, "switched off in config.json", reason == kDataLoadedReason);
                return;
            }
            report["enabled"] = true;

            json catalog;
            if (!LoadCatalog(catalog) || !catalog.is_object() || !catalog.contains("spells") || !catalog["spells"].is_object()) {
                Stop(report, "no catalog yet - a full scan makes one", reason == kDataLoadedReason);
                return;
            }

            std::vector<Plan::File> files = LoadFiles(dataHandler, report);
            if (files.empty()) {
                Stop(report, "no adapter file applies to this load order", reason == kDataLoadedReason);
                return;
            }

            auto keywords = KeywordsByEditorId(dataHandler);
            ResolveKeywordForms(files, keywords);
            LoadOrderSource loadOrder(catalog["spells"]);

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
            report["elapsedMs"] = elapsed;
            report["files"] = Plan::PlanReport(files, plan);
            WriteReport(report);

            logger::info("PerkAdapters: {} - {} keywords on effects ({} new, {} kept, {} taken back, {} did not take, "
                         "{} unresolved), {} spells in perk FormLists ({} new, {} taken back) from {} adapter files in {} ms",
                reason, g_injected.size(), result.added, result.kept, result.removed, result.failed, unresolved,
                g_listed.size(), lists.added, lists.removed, files.size(), elapsed);
            if (!listsNow) logger::info("PerkAdapters: FormLists follow this plan at the next game start");
        }
    }

    bool IsInjected(RE::FormID effect, RE::FormID keyword)
    {
        return !g_injected.empty() && g_injected.contains({ effect, keyword });
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
