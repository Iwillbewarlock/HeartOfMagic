#include "Common.h"

#include "SrData.h"

#include "Addon.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <format>
#include <string>
#include <vector>

namespace SrData
{
    namespace
    {
        // SpellResearch.esp (checked with houseCARL and SR's winning scripts; add-on design "Spell Research data")
        constexpr std::string_view kSrPlugin = "SpellResearch.esp";
        constexpr RE::FormID kArchetypeListsId = 0x000D9E;  // _SR_ListSpellsArchetypes: 60 FormLists of spells
        constexpr RE::FormID kTierListsId = 0x001343;       // _SR_ListAllSpells: 5 FormLists, [0] Novice .. [4] Master
        constexpr RE::FormID kGlobalListsId = 0x00133D;     // _SR_ListGlobalsArchetypes: 60 FormLists of 5 GlobalVariables
        constexpr RE::FormID kImportingId = 0x004C8D;       // _SR_GlobalImporting: > 0 while SR imports
        constexpr RE::FormID kIsSetupId = 0x00624F;         // _SR_GlobalIsSetup: 0 until SR finished setting up
        // Self-test: Flames (Skyrim.esm 012FCD) is Fire and Destruction in SR's lists
        constexpr std::string_view kSkyrim = "Skyrim.esm";
        constexpr RE::FormID kFlamesId = 0x012FCD;
        constexpr int kFireArchetype = 12, kDestructionArchetype = 38;
        // Self-test "outside": spells from plugins other than SR's own and the game's masters, which only something that adds
        // them at runtime (the Librarian, SR's Import of another mod) files in SR's lists
        constexpr std::array<std::string_view, 6> kOwnPlugins{ "SpellResearch.esp", "Skyrim.esm", "Update.esm", "Dawnguard.esm",
            "HearthFires.esm", "Dragonborn.esm" };
        constexpr std::size_t kOutsideSamples = 3;

        RE::BGSListForm* g_archetypeList = nullptr;
        RE::BGSListForm* g_tierList = nullptr;
        RE::BGSListForm* g_globalList = nullptr;
        std::array<RE::BGSListForm*, SrBlock::kArchetypeCount> g_archetypes{};
        std::array<RE::BGSListForm*, SrBlock::kTierCount> g_tiers{};
        std::array<std::array<RE::TESGlobal*, SrBlock::kTierCount>, SrBlock::kArchetypeCount> g_globals{};
        RE::TESGlobal* g_importing = nullptr;
        RE::TESGlobal* g_isSetup = nullptr;

        std::atomic<std::shared_ptr<const Snapshot>> g_current;
        bool g_selfTestDone = false;  // game thread only (the build task)

        // a_list's entries as they are now: the record's forms, then the script-added FormIDs (BGSListForm::HasForm's two
        // places). No lookup: the snapshot keys by FormID
        template <class Fn>
        void EachFormId(const RE::BGSListForm& a_list, Fn&& a_fn)
        {
            for (const auto* form : a_list.forms) {
                if (form) a_fn(form->GetFormID(), false);
            }
            if (a_list.scriptAddedTempForms) {
                for (const auto id : *a_list.scriptAddedTempForms) {
                    if (id != 0) a_fn(id, true);
                }
            }
        }

        std::size_t CountEntries(const RE::BGSListForm& a_list)
        {
            std::size_t count = 0;
            EachFormId(a_list, [&](RE::FormID, bool) { ++count; });
            return count;
        }

        // The FormLists a_list holds, in order; empty (a_why filled) when one is not a FormList or the count is not a_expected
        std::vector<RE::BGSListForm*> SubLists(const RE::BGSListForm& a_list, std::size_t a_expected, const char* a_name, std::string& a_why)
        {
            std::vector<RE::BGSListForm*> lists;
            bool allLists = true;
            EachFormId(a_list, [&](RE::FormID a_id, bool) {
                auto* sub = RE::TESForm::LookupByID<RE::BGSListForm>(a_id);
                if (!sub) allLists = false;
                lists.push_back(sub);
            });
            if (!allLists || lists.size() != a_expected) {
                a_why = std::format("{} holds {} entries{}, expected {} FormLists", a_name, lists.size(), allLists ? "" : " (some not FormLists)", a_expected);
                return {};
            }
            return lists;
        }

        template <class T>
        T* Lookup(RE::TESDataHandler& a_data, RE::FormID a_id, const char* a_name, std::string& a_why)
        {
            auto* form = a_data.LookupForm<T>(a_id, kSrPlugin);
            if (!form && a_why.empty()) a_why = std::format("{} ({:06X} in {}) not found or of another type", a_name, a_id, kSrPlugin);
            return form;
        }

        struct Tally
        {
            int pass = 0, fail = 0, skip = 0;

            void Line(const char* a_id, const char* a_verdict, const std::string& a_detail)
            {
                logger::info("[selftest-sr] {} {} {}", a_id, a_verdict, a_detail);
                const std::string_view verdict(a_verdict);
                (verdict == "PASS" ? pass : verdict == "FAIL" ? fail : skip) += 1;
            }
        };

        // The list sizes counted now (not the arrays Init filled, which hold the expected sizes by construction)
        void TestListSizes(Tally& a_tally)
        {
            const auto archetypes = CountEntries(*g_archetypeList), tiers = CountEntries(*g_tierList), globals = CountEntries(*g_globalList);
            std::size_t badGlobals = 0;
            EachFormId(*g_globalList, [&](RE::FormID a_id, bool) {
                const auto* sub = RE::TESForm::LookupByID<RE::BGSListForm>(a_id);
                if (!sub || CountEntries(*sub) != SrBlock::kTierCount) ++badGlobals;
            });
            const bool ok = archetypes == SrBlock::kArchetypeCount && tiers == SrBlock::kTierCount && globals == SrBlock::kArchetypeCount && badGlobals == 0;
            a_tally.Line("lists", ok ? "PASS" : "FAIL",
                std::format("_SR_ListSpellsArchetypes {} (expect {}), _SR_ListAllSpells {} (expect {}), _SR_ListGlobalsArchetypes {} (expect {}), "
                            "{} knowledge list(s) without {} globals",
                    archetypes, SrBlock::kArchetypeCount, tiers, SrBlock::kTierCount, globals, SrBlock::kArchetypeCount, badGlobals,
                    SrBlock::kTierCount));
        }

        bool OwnPlugin(const RE::TESForm& a_form)
        {
            const auto* file = a_form.GetFile(0);
            if (!file) return true;  // made at runtime: not a plugin's spell
            const auto name = file->GetFilename();
            return std::ranges::any_of(kOwnPlugins, [&](std::string_view a_own) {
                return name.size() == a_own.size() && std::equal(name.begin(), name.end(), a_own.begin(),
                                                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            });
        }

        // The engine's own HasForm on every archetype and tier list against the snapshot's entry (nullptr: not filed)
        std::string Disagreement(RE::TESForm& a_form, const SrBlock::SpellEntry* a_entry)
        {
            std::string why;
            for (std::size_t a = 0; a < g_archetypes.size(); ++a) {
                const bool engine = g_archetypes[a]->HasForm(&a_form);
                const bool snap = a_entry && a_entry->archetypes.test(a);
                if (engine != snap) why += std::format(" archetype {}: HasForm {} snapshot {};", a, engine, snap);
            }
            int lowest = -1;
            for (std::size_t t = 0; t < g_tiers.size(); ++t) {
                if (lowest < 0 && g_tiers[t]->HasForm(&a_form)) lowest = static_cast<int>(t);
            }
            const int snapTier = a_entry ? a_entry->tier : -1;
            if (lowest != snapTier) why += std::format(" tier: HasForm {} snapshot {};", lowest, snapTier);
            return why;
        }

        // Independent of how the snapshot was built: BGSListForm::HasForm (the engine's own search) on spells from outside SR
        // and the game's masters that the snapshot files, and on one spell it does not file
        void TestAgainstEngine(const Snapshot& a_snap, Tally& a_tally)
        {
            if (!a_snap.ready) {
                a_tally.Line("outside", "SKIP", "snapshot not ready");
                a_tally.Line("not-filed", "SKIP", "snapshot not ready");
                return;
            }
            std::vector<RE::FormID> ids;
            for (const auto& [id, entry] : a_snap.spells) ids.push_back(id);
            std::ranges::sort(ids);  // the same picks every run on the same load order
            std::size_t checked = 0, broken = 0;
            std::string detail;
            for (const auto id : ids) {
                if (checked == kOutsideSamples) break;
                auto* form = RE::TESForm::LookupByID(id);
                if (!form || !form->Is(RE::FormType::Spell) || OwnPlugin(*form)) continue;
                ++checked;
                const auto why = Disagreement(*form, &a_snap.spells.at(id));
                if (!why.empty()) ++broken;
                detail += std::format(" {:08X} ({}){}", id, form->GetFile(0) ? form->GetFile(0)->GetFilename() : "?", why.empty() ? " agrees" : why);
            }
            if (checked == 0) {
                a_tally.Line("outside", "SKIP", std::format("no spell from another plugin is filed in SR's lists (Heart of Magic {})",
                                                    Addon::HomInstalled() ? "loaded - its Librarian's Spell Research patch off or not run" : "not loaded"));
            } else {
                a_tally.Line("outside", broken == 0 ? "PASS" : "FAIL", std::format("HasForm vs the snapshot on {} spell(s):{}", checked, detail));
            }

            auto* data = RE::TESDataHandler::GetSingleton();
            RE::SpellItem* unfiled = nullptr;
            if (data) {
                for (auto* spell : data->GetFormArray<RE::SpellItem>()) {
                    if (spell && !a_snap.spells.contains(spell->GetFormID())) {
                        unfiled = spell;
                        break;
                    }
                }
            }
            if (!unfiled) {
                a_tally.Line("not-filed", "SKIP", "every spell is filed");
            } else {
                const auto why = Disagreement(*unfiled, nullptr);
                a_tally.Line("not-filed", why.empty() ? "PASS" : "FAIL",
                    std::format("{:08X} not in the snapshot: HasForm false on every list{}", unfiled->GetFormID(), why.empty() ? "" : " - NO:" + why));
            }
        }

        // Spec "Self-test": at the first menu open, on the first snapshot (the drawn-id check is BlockDraw's)
        void SelfTest(const Snapshot& a_snap)
        {
            Tally tally;
            TestListSizes(tally);
            if (a_snap.ready) {
                tally.Line("snapshot", "PASS", std::format("ready: {} spells, {} archetype entries ({} script-added)", a_snap.spells.size(),
                                                    a_snap.archetypeEntries, a_snap.scriptAdded));
            } else {
                tally.Line("snapshot", "SKIP",
                    std::format("{}: _SR_GlobalImporting {}, _SR_GlobalIsSetup {}",
                        a_snap.importing ? "importing - SR's Import is running" :
                                           "not set up - SR's Import has not been run in this save (MCM Import Spells, or a research journal)",
                        g_importing->value, g_isSetup->value));
            }

            auto* data = RE::TESDataHandler::GetSingleton();
            const RE::FormID flames = data ? data->LookupFormID(kFlamesId, kSkyrim) : 0;
            if (!a_snap.ready || a_snap.listsEmpty) {
                tally.Line("flames", "SKIP", a_snap.ready ? "SR's lists are empty (Import never run)" : "snapshot not ready");
            } else if (flames == 0) {
                tally.Line("flames", "FAIL", std::format("Flames {:06X} not found in {}", kFlamesId, kSkyrim));
            } else {
                const auto it = a_snap.spells.find(flames);
                const bool filed = it != a_snap.spells.end();
                const bool fire = filed && it->second.archetypes.test(kFireArchetype);
                const bool destruction = filed && it->second.archetypes.test(kDestructionArchetype);
                tally.Line("flames", fire && destruction ? "PASS" : "FAIL",
                    std::format("{:08X}: {}, Fire [{}] {}, Destruction [{}] {}, tier {}", flames, filed ? "filed" : "NOT filed", kFireArchetype, fire,
                        kDestructionArchetype, destruction, filed ? it->second.tier : -1));
            }
            TestAgainstEngine(a_snap, tally);
            logger::info("[selftest-sr] DONE pass={} fail={} skip={} (the per-redraw 'drawn' lines follow as the block is drawn)", tally.pass,
                tally.fail, tally.skip);
            FlushLog();
        }

        std::shared_ptr<const Snapshot> Build()
        {
            auto snap = std::make_shared<Snapshot>();
            snap->importing = g_importing->value > 0.0f;
            snap->setUp = g_isSetup->value != 0.0f;
            snap->ready = !snap->importing && snap->setUp;
            if (!snap->ready) return snap;
            for (std::size_t a = 0; a < g_archetypes.size(); ++a) {
                EachFormId(*g_archetypes[a], [&](RE::FormID a_id, bool a_scriptAdded) {
                    snap->spells[a_id].archetypes.set(a);
                    ++snap->archetypeEntries;
                    if (a_scriptAdded) ++snap->scriptAdded;
                });
            }
            snap->listsEmpty = snap->archetypeEntries == 0;
            std::size_t twoTiers = 0;
            for (std::size_t t = 0; t < g_tiers.size(); ++t) {
                EachFormId(*g_tiers[t], [&](RE::FormID a_id, bool) {
                    auto& entry = snap->spells[a_id];
                    if (entry.tier < 0) {
                        entry.tier = static_cast<int>(t);
                    } else {
                        ++twoTiers;  // in two tier lists: the lower tier is kept
                    }
                });
            }
            for (std::size_t a = 0; a < g_globals.size(); ++a) {
                for (std::size_t t = 0; t < SrBlock::kTierCount; ++t) snap->knowledge[a][t] = g_globals[a][t]->value;
            }
            if (twoTiers > 0) logger::info("SrData: {} spell(s) in two tier lists - the lower tier is used", twoTiers);
            return snap;
        }

        InitResult Broken(const std::string& a_why)
        {
            logger::error("SrData: {} - the block is off", a_why);
            return InitResult::kBroken;
        }
    }

    InitResult Init(std::string& a_why)
    {
        a_why.clear();
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            a_why = "no TESDataHandler";
            return Broken(a_why);
        }
        if (!data->LookupModByName(kSrPlugin)) {
            a_why = std::format("{} is not loaded (Spell Research is required)", kSrPlugin);
            logger::info("SrData: {} - the block is off", a_why);
            return InitResult::kNoSpellResearch;
        }
        g_archetypeList = Lookup<RE::BGSListForm>(*data, kArchetypeListsId, "_SR_ListSpellsArchetypes", a_why);
        g_tierList = Lookup<RE::BGSListForm>(*data, kTierListsId, "_SR_ListAllSpells", a_why);
        g_globalList = Lookup<RE::BGSListForm>(*data, kGlobalListsId, "_SR_ListGlobalsArchetypes", a_why);
        g_importing = Lookup<RE::TESGlobal>(*data, kImportingId, "_SR_GlobalImporting", a_why);
        g_isSetup = Lookup<RE::TESGlobal>(*data, kIsSetupId, "_SR_GlobalIsSetup", a_why);
        if (!a_why.empty()) return Broken(a_why);

        const auto archetypes = SubLists(*g_archetypeList, SrBlock::kArchetypeCount, "_SR_ListSpellsArchetypes", a_why);
        if (archetypes.empty()) return Broken(a_why);
        const auto tiers = SubLists(*g_tierList, SrBlock::kTierCount, "_SR_ListAllSpells", a_why);
        if (tiers.empty()) return Broken(a_why);
        const auto globals = SubLists(*g_globalList, SrBlock::kArchetypeCount, "_SR_ListGlobalsArchetypes", a_why);
        if (globals.empty()) return Broken(a_why);
        std::ranges::copy(archetypes, g_archetypes.begin());
        std::ranges::copy(tiers, g_tiers.begin());
        for (std::size_t a = 0; a < globals.size(); ++a) {
            std::vector<RE::TESGlobal*> values;
            EachFormId(*globals[a], [&](RE::FormID a_id, bool) { values.push_back(RE::TESForm::LookupByID<RE::TESGlobal>(a_id)); });
            if (values.size() != SrBlock::kTierCount || std::ranges::find(values, static_cast<RE::TESGlobal*>(nullptr)) != values.end()) {
                a_why = std::format("_SR_ListGlobalsArchetypes [{}] holds {} entries, expected {} GlobalVariables", a, values.size(), SrBlock::kTierCount);
                return Broken(a_why);
            }
            std::ranges::copy(values, g_globals[a].begin());
        }
        logger::info("SrData: {} - {} archetype lists, {} tier lists, {} x {} knowledge globals", kSrPlugin, g_archetypes.size(), g_tiers.size(),
            g_globals.size(), SrBlock::kTierCount);
        return InitResult::kReady;
    }

    void RequestBuild(void (*a_onBuilt)())
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error("SrData: no SKSE task interface - no snapshot (the block says Spell Research is getting ready)");
            return;
        }
        tasks->AddTask([a_onBuilt] {
            auto snap = Build();
            const bool first = !g_selfTestDone;
            g_selfTestDone = true;
            if (first && Settings::SelfTest()) SelfTest(*snap);
            g_current.store(std::move(snap));
            if (a_onBuilt) a_onBuilt();
        });
    }

    std::shared_ptr<const Snapshot> Current()
    {
        return g_current.load();
    }

    void Clear()
    {
        g_current.store(nullptr);
    }
}
