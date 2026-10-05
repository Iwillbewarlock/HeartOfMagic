#include "librarian/SpellResearchPatch.h"
#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/LoadOrderSource.h"
#include "librarian/PerkAdapters.h"
#include "librarian/SpellResearch.h"
#include "HudNotify.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"
#include "tomereach/TomeReach.h"
#include "uimanager/UIManager.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// LibrarianSpellResearchPatch - registering catalog spells with Spell Research
//
// See SpellResearchPatch.h for when this runs and SpellResearch.h for what a
// registration holds.
// =============================================================================

namespace Librarian::SpellResearchPatch
{
    using namespace Librarian::Detail;
    namespace Adapters = Librarian::Adapters;
    namespace SR = Librarian::SpellResearch;

    namespace
    {
        constexpr const char* SR_PLUGIN = "SpellResearch.esp";
        constexpr const char* RULES_FILE = "compat/spellresearch.json";
        constexpr const char* REPORT_FILE = "spell_research_report.json";
        constexpr const char* CONFIG_SECTION = "spellResearch";

        bool g_installed = false;
        SR::Lists g_lists;

        // Entries in the five tier lists after the last run, for the save
        // loaded now. Same count at the next message box: no Import ran since.
        std::size_t g_lastSeen = 0;

        // Spells whose tier list would not take them: left out of the check
        // for missing spells, or every load would plan them again.
        std::unordered_set<RE::FormID> g_failed;

        // What the last run found (StatusJson): written on the game thread, read
        // when the panel opens
        std::mutex g_statusLock;
        json g_status;

        void SetStatus(const char* state, std::string_view reason, std::size_t srSpells = 0, std::size_t added = 0)
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            char clock[8] = {};
            std::strftime(clock, sizeof(clock), "%H:%M", &local);
            {
                std::lock_guard lock(g_statusLock);
                g_status = { { "state", state }, { "reason", std::string(reason) }, { "srSpells", srSpells },
                             { "added", added }, { "time", clock } };
            }
            // The panel's line, if it is open (a scan from the panel ends here)
            UIManager::GetSingleton()->NotifySpellResearchStatus();
        }

        RE::BGSListForm* ResolveList(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<RE::BGSListForm>(formId) : nullptr;
        }

        float GlobalValue(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            auto* global = formId ? RE::TESForm::LookupByID<RE::TESGlobal>(formId) : nullptr;
            return global ? global->value : 0.0f;
        }

        // The plugin's entries and the ones scripts added, as Papyrus GetSize counts them
        std::size_t Count(const RE::BGSListForm* list)
        {
            if (!list) return 0;
            return list->forms.size() + (list->scriptAddedTempForms ? list->scriptAddedTempForms->size() : 0);
        }

        // Every spell in any of SR's tier lists, and how many entries they hold
        struct Registered
        {
            std::unordered_set<RE::FormID> spells;
            std::size_t entries = 0;
        };

        // Entries in the tier lists: what tells an Import or a load apart from
        // any other message box, before anything costlier is read
        std::size_t CountRegistered()
        {
            std::size_t entries = 0;
            for (const auto& id : g_lists.allSpells) entries += Count(ResolveList(id));
            return entries;
        }

        Registered ReadRegistered()
        {
            Registered result;
            for (const auto& id : g_lists.allSpells) {
                const auto* list = ResolveList(id);
                if (!list) continue;
                result.entries += Count(list);
                list->ForEachForm([&](RE::TESForm* form) {
                    result.spells.insert(form->GetFormID());
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            return result;
        }

        bool HasSchoolAndTier(const json& entry)
        {
            const auto text = [&](const char* key) {
                const auto found = entry.find(key);
                return (found != entry.end() && found->is_string()) ? found->get<std::string>() : std::string();
            };
            return g_lists.school.contains(text("school"))
                && std::ranges::find(SR::kTiers, text("tier")) != SR::kTiers.end();
        }

        // Tome spells the tree leaves out and SR is not given, by why (MissingSpells)
        struct Skipped
        {
            std::size_t voiceSlot = 0;
            std::size_t freeSpell = 0;      // the spell gate's "out" (free, a summon that stays, a rule)
            std::size_t duplicate = 0;
            std::size_t unreachable = 0;
            std::size_t playerLeftOut = 0;   // the player's spell blacklist or a plugin turned off
        };

        // What the panel's builder leaves out on the player's say (proceduralTreeBuilder.js:
        // the spell blacklist and the plugins turned off in the plugin whitelist), read from
        // config.json where the panel saves them, so SR gets the builder's spells exactly
        struct PlayerFilter
        {
            std::unordered_set<std::string> blacklisted;      // "plugin.esp|<local id>", lowercase plugin
            std::unordered_set<RE::FormID> blacklistedIds;     // entries that only carry a formId
            std::unordered_set<std::string> pluginsOff;        // lowercase
        };

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // "plugin|<local id as a number>" for a plugin name and a hex local id ("0x00080B" or "00080b")
        std::string LocalKey(const std::string& plugin, const std::string& localHex)
        {
            try {
                return Lower(plugin) + "|" + std::to_string(std::stoul(localHex, nullptr, 16));
            } catch (const std::exception&) {
                return {};
            }
        }

        PlayerFilter ReadPlayerFilter()
        {
            PlayerFilter filter;
            json config;
            if (!ReadJsonFile(DataPath("config.json"), config) || !config.is_object()) return filter;
            const auto text = [](const json& entry, const char* key) {
                const auto found = entry.find(key);
                return (found != entry.end() && found->is_string()) ? found->get<std::string>() : std::string();
            };
            if (const auto list = config.find("spellBlacklist"); list != config.end() && list->is_array()) {
                for (const auto& entry : *list) {
                    if (!entry.is_object()) continue;
                    const auto key = LocalKey(text(entry, "plugin"), text(entry, "localFormId"));
                    if (!text(entry, "plugin").empty() && !key.empty()) {
                        filter.blacklisted.insert(key);
                    } else if (const auto id = SpellScanner::ResolvePersistentFormId(text(entry, "formId")); id) {
                        filter.blacklistedIds.insert(id);
                    } else {
                        try {
                            filter.blacklistedIds.insert(static_cast<RE::FormID>(std::stoul(text(entry, "formId"), nullptr, 16)));
                        } catch (const std::exception&) {}
                    }
                }
            }
            if (const auto list = config.find("pluginWhitelist"); list != config.end() && list->is_array()) {
                for (const auto& entry : *list) {
                    const auto enabled = entry.is_object() ? entry.find("enabled") : entry.end();
                    if (entry.is_object() && enabled != entry.end() && enabled->is_boolean() && !enabled->get<bool>()) {
                        filter.pluginsOff.insert(Lower(text(entry, "plugin")));
                    }
                }
            }
            return filter;
        }

        bool PlayerLeftOut(const std::string& persistentId, RE::FormID formId, const PlayerFilter& filter)
        {
            const auto pipe = persistentId.find('|');
            if (pipe == std::string::npos) return filter.blacklistedIds.contains(formId);
            const std::string plugin = persistentId.substr(0, pipe);
            return filter.pluginsOff.contains(Lower(plugin)) || filter.blacklistedIds.contains(formId) ||
                filter.blacklisted.contains(LocalKey(plugin, persistentId.substr(pipe + 1)));
        }

        // Another plugin's copy of a spell (duplicateOf in the scan), or the kept
        // record of a group SR already holds a member of: each spell is in SR once
        bool DuplicateLeftOut(RE::FormID formId, const Registered& registered)
        {
            const auto group = SpellScanner::DuplicateGroupOf(formId);
            if (group.empty()) return false;
            if (group.front() != formId) return true;
            return std::ranges::any_of(group, [&](RE::FormID member) { return registered.spells.contains(member); });
        }

        // The catalog spells to register: a tome teaches them (the catalog also
        // holds NPC, quest and creature spells, which SR must not hand out), and
        // the rest exactly as the panel's builder takes spells into the tree
        // (2026-10-05): whose tome something hands out (not tomeUnreachable), not
        // in the voice slot, not left out by the spell gate (JudgeSpell), not another plugin's copy
        // (duplicateOf), not blacklisted and not from a plugin turned off in the
        // whitelist. SR can file them by school and tier, and no tier list has them yet.
        json MissingSpells(const json& catalogSpells, const Registered& registered,
            const std::unordered_map<RE::FormID, RE::TESObjectBOOK*>& tomes,
            Skipped& skipped)
        {
            std::unordered_set<RE::FormID> unreachable;
            TomeReach::UnreachableSpells(unreachable);   // empty until the pass is done, as the scan's marks
            const PlayerFilter playerFilter = ReadPlayerFilter();
            json missing = json::object();
            for (const auto& [id, entry] : catalogSpells.items()) {
                if (!HasSchoolAndTier(entry)) continue;
                const RE::FormID formId = SpellScanner::ResolvePersistentFormId(id);
                if (!formId || !tomes.contains(formId) || registered.spells.contains(formId) || g_failed.contains(formId)) continue;
                auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
                if (!spell) continue;
                if (SpellScanner::IsEquippedInVoiceSlot(spell)) {
                    ++skipped.voiceSlot;
                } else if (unreachable.contains(formId)) {
                    ++skipped.unreachable;
                } else if (PlayerLeftOut(id, formId, playerFilter)) {
                    ++skipped.playerLeftOut;
                } else if (SpellScanner::JudgeSpell(spell).band == "out") {
                    ++skipped.freeSpell;
                } else if (DuplicateLeftOut(formId, registered)) {
                    ++skipped.duplicate;
                } else {
                    missing[id] = entry;
                }
            }
            return missing;
        }

        void WriteReport(const json& report)
        {
            const auto path = DataPath(REPORT_FILE);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                logger::warn("SpellResearch: could not write '{}'", PathText::Utf8(path));
                return;
            }
            file << JsonText::Dump(report, 2);
        }

        struct Totals
        {
            std::size_t spells = 0;
            std::size_t entries = 0;
            std::size_t tomes = 0;
            std::size_t failed = 0;
            std::size_t tomeTiersSkipped = 0;
        };

        // Adds the registrations (spells no tier list has yet), SR's order.
        Totals Register(const SR::Registrations& registrations, json& report)
        {
            Totals totals;
            std::unordered_map<std::string, RE::BGSListForm*> resolved;
            const auto listOf = [&](const std::string& id) {
                const auto found = resolved.find(id);
                if (found != resolved.end()) return found->second;
                return resolved.emplace(id, ResolveList(id)).first->second;
            };

            // A tier's tome lists are paired by index: add to them only while
            // they line up - the same count, and as many record entries ahead
            // of the script-added ones - or SR would hand out the wrong book.
            std::array<bool, SR::kTiers.size()> tomesPaired{};
            for (std::size_t t = 0; t < SR::kTiers.size(); ++t) {
                const auto* bookList = listOf(g_lists.tomes[t]);
                const auto* spellList = listOf(g_lists.tomeSpells[t]);
                const std::size_t books = Count(bookList);
                const std::size_t taught = Count(spellList);
                tomesPaired[t] = bookList && spellList && books == taught
                    && bookList->forms.size() == spellList->forms.size();
                if (!tomesPaired[t]) {
                    ++totals.tomeTiersSkipped;
                    report["tomeTiersSkipped"].push_back({ { "tier", std::string(SR::kTiers[t]) }, { "tomes", books }, { "spells", taught } });
                }
            }

            bool firstAdd = true;
            json perList = json::object();
            for (const auto& registration : registrations.spells) {
                const RE::FormID formId = SpellScanner::ResolvePersistentFormId(registration.spellId);
                auto* spell = formId ? RE::TESForm::LookupByID<RE::SpellItem>(formId) : nullptr;
                if (!spell) continue;

                for (const auto& id : registration.lists) {
                    auto* list = listOf(id);
                    if (!list || list->HasForm(spell)) continue;
                    if (firstAdd) {
                        // AddForm must take the script path: Revert() clears only that
                        const auto before = list->scriptAddedFormCount;
                        list->AddForm(spell);
                        logger::info("SpellResearch: first entry - script-added count {} -> {} (forms {})",
                            before, list->scriptAddedFormCount, list->forms.size());
                        firstAdd = false;
                    } else {
                        list->AddForm(spell);
                    }
                    ++totals.entries;
                    const std::string name = SR::ArchetypeOf(g_lists, id);
                    perList[name.empty() ? id : name] = perList.value(name.empty() ? id : name, 0) + 1;
                }

                auto* tierList = listOf(g_lists.allSpells[registration.tier]);
                if (!tierList || !tierList->HasForm(spell)) {
                    g_failed.insert(formId);
                    ++totals.failed;
                    continue;
                }
                ++totals.spells;

                if (registration.tome.empty() || !tomesPaired[registration.tier]) continue;
                auto* tome = RE::TESForm::LookupByID<RE::TESObjectBOOK>(SpellScanner::ResolvePersistentFormId(registration.tome));
                if (!tome) continue;
                auto* books = listOf(g_lists.tomes[registration.tier]);
                auto* taught = listOf(g_lists.tomeSpells[registration.tier]);
                if (books->HasForm(tome) || taught->HasForm(spell)) continue;
                books->AddForm(tome);
                taught->AddForm(spell);
                if (Count(books) != Count(taught)) {
                    // One of the two did not take: stop this tier before the pairs drift further
                    tomesPaired[registration.tier] = false;
                    logger::warn("SpellResearch: {} tome lists out of step after {} - no more tomes there",
                        SR::kTiers[registration.tier], registration.spellId);
                    continue;
                }
                ++totals.tomes;
            }
            report["perList"] = std::move(perList);
            return totals;
        }

        void Reconcile(std::string_view reason)
        {
            if (!g_installed) return;

            // Only once an Import finished, never while one runs, and only when
            // the tier lists changed since the last run (an Import, a load)
            if (GlobalValue(g_lists.isSetup) != 1.0f) {
                SetStatus("notImported", reason);
                return;
            }
            if (GlobalValue(g_lists.importing) != 0.0f) return;
            const std::size_t entries = CountRegistered();
            if (entries == g_lastSeen) return;
            g_lastSeen = entries;
            if (!ConfigSwitchOn(CONFIG_SECTION)) {
                SetStatus("off", reason);
                return;
            }

            const auto started = std::chrono::steady_clock::now();
            json catalog;
            if (!LoadCatalog(catalog) || !catalog.is_object() || !catalog.contains("spells") || !catalog["spells"].is_object()) {
                logger::info("SpellResearch: {} - no catalog yet, a full scan makes one", reason);
                SetStatus("noCatalog", reason);
                return;
            }
            const Registered registered = ReadRegistered();
            const auto tomes = SpellScanner::TomesBySpell();
            Skipped skipped;
            const json missing = MissingSpells(catalog["spells"], registered, tomes, skipped);
            if (skipped.voiceSlot || skipped.freeSpell || skipped.duplicate || skipped.unreachable || skipped.playerLeftOut) {
                logger::info("SpellResearch: {} - left out as the tree leaves them: {} voice slot spells, "
                             "{} the spell gate leaves out, {} copies from another plugin, {} whose tomes nothing hands out, "
                             "{} blacklisted or from a plugin turned off", reason, skipped.voiceSlot,
                             skipped.freeSpell, skipped.duplicate, skipped.unreachable, skipped.playerLeftOut);
            }
            if (missing.empty()) {
                SetStatus("upToDate", reason, registered.spells.size());
                return;
            }

            json rules;
            std::vector<std::string> problems;
            std::vector<Adapters::File> files(1);
            if (!ReadJsonFile(RulesPath() / RULES_FILE, rules) || !Adapters::ParseFile(rules, "spellresearch.json", files[0], problems)) {
                logger::warn("SpellResearch: {} not readable - nothing registered", RULES_FILE);
                return;
            }
            for (const auto& problem : problems) logger::warn("SpellResearch: {}", problem);

            // The SR file alone: formListsNone reads the marks of this one plan.
            // Only the missing spells: a line's pick is per spell, so the rest
            // would change nothing and cost their JSON.
            LoadOrderSource loadOrder(missing);
            std::unordered_map<std::string, RE::BGSListForm*> lists;
            const auto listOf = [&](const std::string& id) {
                const auto found = lists.find(id);
                if (found != lists.end()) return found->second;
                return lists.emplace(id, ResolveList(id)).first->second;
            };
            Adapters::PlanSource source;
            source.forEachCatalogSpell = [&](const auto& visit) { loadOrder.ForEachCatalogSpell(visit); };
            source.usersOf = [&](const std::string& key) { return loadOrder.UsersOf(key); };
            source.formListDefined = [&](const std::string& id) { return listOf(id) != nullptr; };
            source.inFormList = [&](const std::string& id, const std::string& spellId) {
                auto* list = listOf(id);
                auto* spell = RE::TESForm::LookupByID(SpellScanner::ResolvePersistentFormId(spellId));
                return list && spell && list->HasForm(spell);
            };
            const Adapters::Plan plan = Adapters::BuildPlan(files, source);
            const SR::Registrations registrations = SR::Build(g_lists, plan, missing, [&](const std::string& id) {
                const auto tome = tomes.find(SpellScanner::ResolvePersistentFormId(id));
                return tome == tomes.end() ? std::string() : SpellScanner::GetPersistentFormId(tome->second->GetFormID());
            });

            json report;
            report["reason"] = std::string(reason);
            report["problems"] = problems;
            report["alreadyRegistered"] = registered.spells.size();
            report["noSchoolOrTier"] = registrations.noSchoolOrTier;
            report["leftOut"] = { { "voiceSlot", skipped.voiceSlot }, { "gate", skipped.freeSpell },
                                  { "duplicate", skipped.duplicate }, { "tomeUnreachable", skipped.unreachable },
                                  { "player", skipped.playerLeftOut } };
            const Totals totals = Register(registrations, report);
            g_lastSeen = CountRegistered();

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            report["registered"] = totals.spells;
            report["entries"] = totals.entries;
            report["tomes"] = totals.tomes;
            report["failed"] = totals.failed;
            report["elapsedMs"] = elapsed;
            report["lines"] = Adapters::PlanReport(files, plan);
            WriteReport(report);

            logger::info("SpellResearch: {} - {} spells registered ({} list entries, {} tomes), {} already known to SR, "
                         "{} would not take, {} tome tiers out of step, in {} ms",
                reason, totals.spells, totals.entries, totals.tomes, registered.spells.size(), totals.failed,
                totals.tomeTiersSkipped, elapsed);

            SetStatus(totals.spells > 0 ? "added" : "upToDate", reason, registered.spells.size() + totals.spells, totals.spells);
            // SR's own import quests say what they brought in ("Importing 46 Forms from
            // Dawnguard"); without this the player never saw that these spells came in too
            if (totals.spells > 0) {
                HudNotify::Show("hud.spellResearchAdded", "Spell Research: {{count}} spells added by Heart of Magic",
                    { { "count", std::to_string(totals.spells) } });
            }
        }

        void SafeReconcile(std::string_view reason)
        {
            try {
                Reconcile(reason);
            } catch (const std::exception& e) {
                logger::error("SpellResearch: {} failed - {}", reason, e.what());
            }
        }

        // SR shows "All Spells Imported" in a message box when an Import ends.
        // Any message box opening or closing is checked; the tier lists' size
        // tells. On opening: the Import's globals are set before SR asks for the
        // box, so the spells are in (and announced) while the player reads it.
        class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuSink* GetSingleton()
            {
                static MenuSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (event && event->menuName == RE::MessageBoxMenu::MENU_NAME) {
                    SKSE::GetTaskInterface()->AddTask([] { SafeReconcile("import"); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            MenuSink(const MenuSink&) = delete;
            MenuSink& operator=(const MenuSink&) = delete;

        private:
            MenuSink() = default;
        };
    }

    void Install()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || !PluginLoaded(dataHandler, SR_PLUGIN)) return;

        json rules;
        std::vector<std::string> problems;
        if (!ReadJsonFile(RulesPath() / RULES_FILE, rules) || !SR::ParseLists(rules, g_lists, problems)) {
            for (const auto& problem : problems) logger::warn("SpellResearch: {}", problem);
            logger::warn("SpellResearch: {} is loaded but {} is missing or incomplete - no registrations", SR_PLUGIN, RULES_FILE);
            return;
        }
        if (!ResolveList(g_lists.allSpells[0]) || !SpellScanner::ResolvePersistentFormId(g_lists.isSetup)) {
            logger::warn("SpellResearch: {} does not have the lists {} names - another version? No registrations",
                SR_PLUGIN, RULES_FILE);
            return;
        }

        RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        g_installed = true;
        logger::info("SpellResearch: {} found - catalog spells are registered when its Import finishes and at save load",
            SR_PLUGIN);
    }

    void OnGameLoaded()
    {
        g_lastSeen = 0;
        g_failed.clear();
        SafeReconcile("load");
    }

    void OnNewGame()
    {
        g_lastSeen = 0;
        g_failed.clear();
        std::lock_guard lock(g_statusLock);
        g_status = json();
    }

    void OnCatalogChanged()
    {
        g_lastSeen = 0;
        g_failed.clear();
        SafeReconcile("scan");
    }
}

namespace Librarian::SpellResearchPatch
{
    std::string StatusJson()
    {
        if (!g_installed) return {};
        std::lock_guard lock(g_statusLock);
        return g_status.is_null() ? std::string() : JsonText::Dump(g_status);
    }
}
