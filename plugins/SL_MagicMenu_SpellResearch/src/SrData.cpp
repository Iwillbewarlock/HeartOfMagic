#include "Common.h"

#include "SrData.h"

#include "Addon.h"
#include "Settings.h"

#include <array>
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

        RE::BGSListForm* g_archetypeList = nullptr;
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

        // The FormLists a_list holds, in order; empty (logged) when one is not a FormList or the count is not a_expected
        std::vector<RE::BGSListForm*> SubLists(const RE::BGSListForm& a_list, std::size_t a_expected, const char* a_name)
        {
            std::vector<RE::BGSListForm*> lists;
            bool allLists = true;
            EachFormId(a_list, [&](RE::FormID a_id, bool) {
                auto* sub = RE::TESForm::LookupByID<RE::BGSListForm>(a_id);
                if (!sub) allLists = false;
                lists.push_back(sub);
            });
            if (!allLists || lists.size() != a_expected) {
                logger::error("SrData: {} holds {} entries{}, expected {} FormLists - the block is off", a_name, lists.size(),
                    allLists ? "" : " (some not FormLists)", a_expected);
                return {};
            }
            return lists;
        }

        template <class T>
        T* Lookup(RE::TESDataHandler& a_data, RE::FormID a_id, const char* a_name)
        {
            auto* form = a_data.LookupForm<T>(a_id, kSrPlugin);
            if (!form) logger::error("SrData: {} ({:06X} in {}) not found or of another type - the block is off", a_name, a_id, kSrPlugin);
            return form;
        }

        void SelfTestLine(const char* a_id, const char* a_verdict, const std::string& a_detail, int& a_pass, int& a_fail, int& a_skip)
        {
            logger::info("[selftest-sr] {} {} {}", a_id, a_verdict, a_detail);
            const std::string_view verdict(a_verdict);
            (verdict == "PASS" ? a_pass : verdict == "FAIL" ? a_fail : a_skip) += 1;
        }

        // Spec "Self-test": at the first menu open, on the first snapshot (the drawn-id check is BlockDraw's)
        void SelfTest(const Snapshot& a_snap)
        {
            int pass = 0, fail = 0, skip = 0;
            // Init refuses any other shape, so reaching here means the shapes held
            SelfTestLine("lists", "PASS", std::format("_SR_ListSpellsArchetypes {} lists, _SR_ListAllSpells {}, _SR_ListGlobalsArchetypes {} x {}",
                                              g_archetypes.size(), g_tiers.size(), g_globals.size(), SrBlock::kTierCount),
                pass, fail, skip);
            if (a_snap.ready) {
                SelfTestLine("snapshot", "PASS", std::format("ready: {} spells, {} archetype entries ({} script-added)", a_snap.spells.size(),
                                                     a_snap.archetypeEntries, a_snap.scriptAdded),
                    pass, fail, skip);
            } else {
                SelfTestLine("snapshot", "SKIP", std::format("not ready: _SR_GlobalImporting {}, _SR_GlobalIsSetup {}", g_importing->value,
                                                     g_isSetup->value),
                    pass, fail, skip);
            }

            auto* data = RE::TESDataHandler::GetSingleton();
            const RE::FormID flames = data ? data->LookupFormID(kFlamesId, kSkyrim) : 0;
            if (!a_snap.ready || a_snap.listsEmpty) {
                SelfTestLine("flames", "SKIP", a_snap.ready ? "SR's lists are empty (Import never run)" : "snapshot not ready", pass, fail, skip);
            } else if (flames == 0) {
                SelfTestLine("flames", "FAIL", std::format("Flames {:06X} not found in {}", kFlamesId, kSkyrim), pass, fail, skip);
            } else {
                const auto it = a_snap.spells.find(flames);
                const bool fire = it != a_snap.spells.end() && it->second.archetypes.test(kFireArchetype);
                const bool destruction = it != a_snap.spells.end() && it->second.archetypes.test(kDestructionArchetype);
                SelfTestLine("flames", fire && destruction ? "PASS" : "FAIL",
                    std::format("{:08X}: {}, Fire [{}] {}, Destruction [{}] {}, tier {}", flames, it != a_snap.spells.end() ? "filed" : "NOT filed",
                        kFireArchetype, fire, kDestructionArchetype, destruction, it != a_snap.spells.end() ? it->second.tier : -1),
                    pass, fail, skip);
            }

            if (!Addon::HomInstalled()) {
                SelfTestLine("librarian", "SKIP", "Heart of Magic (SpellLearning) is not loaded", pass, fail, skip);
            } else if (!a_snap.ready) {
                SelfTestLine("librarian", "SKIP", "snapshot not ready", pass, fail, skip);
            } else if (a_snap.firstScriptAdded == 0) {
                SelfTestLine("librarian", "SKIP", "no script-added spell in SR's archetype lists (the Librarian's Spell Research patch is off or has not run)",
                    pass, fail, skip);
            } else {
                const auto it = a_snap.spells.find(a_snap.firstScriptAdded);
                const bool found = it != a_snap.spells.end() && it->second.archetypes.test(static_cast<std::size_t>(a_snap.firstScriptAddedArchetype));
                SelfTestLine("librarian", found ? "PASS" : "FAIL",
                    std::format("script-added {:08X} in archetype list {} {} the snapshot ({} script-added entries)", a_snap.firstScriptAdded,
                        a_snap.firstScriptAddedArchetype, found ? "found in" : "MISSING from", a_snap.scriptAdded),
                    pass, fail, skip);
            }
            logger::info("[selftest-sr] DONE pass={} fail={} skip={} (the per-redraw 'drawn' lines follow as the block is drawn)", pass, fail, skip);
            FlushLog();
        }

        std::shared_ptr<const Snapshot> Build()
        {
            auto snap = std::make_shared<Snapshot>();
            snap->ready = g_importing->value <= 0.0f && g_isSetup->value != 0.0f;
            if (!snap->ready) return snap;
            for (std::size_t a = 0; a < g_archetypes.size(); ++a) {
                EachFormId(*g_archetypes[a], [&](RE::FormID a_id, bool a_scriptAdded) {
                    snap->spells[a_id].archetypes.set(a);
                    ++snap->archetypeEntries;
                    if (!a_scriptAdded) return;
                    if (snap->scriptAdded++ == 0) {
                        snap->firstScriptAdded = a_id;
                        snap->firstScriptAddedArchetype = static_cast<int>(a);
                    }
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
    }

    bool Init()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            logger::error("SrData: no TESDataHandler - the block is off");
            return false;
        }
        if (!data->LookupModByName(kSrPlugin)) {
            logger::info("SrData: {} is not loaded - the block is off (Spell Research is required)", kSrPlugin);
            return false;
        }
        g_archetypeList = Lookup<RE::BGSListForm>(*data, kArchetypeListsId, "_SR_ListSpellsArchetypes");
        auto* tierList = Lookup<RE::BGSListForm>(*data, kTierListsId, "_SR_ListAllSpells");
        auto* globalList = Lookup<RE::BGSListForm>(*data, kGlobalListsId, "_SR_ListGlobalsArchetypes");
        g_importing = Lookup<RE::TESGlobal>(*data, kImportingId, "_SR_GlobalImporting");
        g_isSetup = Lookup<RE::TESGlobal>(*data, kIsSetupId, "_SR_GlobalIsSetup");
        if (!g_archetypeList || !tierList || !globalList || !g_importing || !g_isSetup) return false;

        const auto archetypes = SubLists(*g_archetypeList, SrBlock::kArchetypeCount, "_SR_ListSpellsArchetypes");
        const auto tiers = SubLists(*tierList, SrBlock::kTierCount, "_SR_ListAllSpells");
        const auto globals = SubLists(*globalList, SrBlock::kArchetypeCount, "_SR_ListGlobalsArchetypes");
        if (archetypes.empty() || tiers.empty() || globals.empty()) return false;
        std::ranges::copy(archetypes, g_archetypes.begin());
        std::ranges::copy(tiers, g_tiers.begin());
        for (std::size_t a = 0; a < globals.size(); ++a) {
            std::vector<RE::TESGlobal*> values;
            EachFormId(*globals[a], [&](RE::FormID a_id, bool) { values.push_back(RE::TESForm::LookupByID<RE::TESGlobal>(a_id)); });
            if (values.size() != SrBlock::kTierCount || std::ranges::find(values, static_cast<RE::TESGlobal*>(nullptr)) != values.end()) {
                logger::error("SrData: _SR_ListGlobalsArchetypes [{}] holds {} entries, expected {} GlobalVariables - the block is off", a,
                    values.size(), SrBlock::kTierCount);
                return false;
            }
            std::ranges::copy(values, g_globals[a].begin());
        }
        logger::info("SrData: {} - {} archetype lists, {} tier lists, {} x {} knowledge globals", kSrPlugin, g_archetypes.size(), g_tiers.size(),
            g_globals.size(), SrBlock::kTierCount);
        return true;
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
}
