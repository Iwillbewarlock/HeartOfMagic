#include "game/GameSelfTestState.h"

#include "Common.h"

#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSlots.h"
#include "game/GameNpcs.h"
#include "PluginNames.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SelfTest = NpcSpellsGame::SelfTest;

namespace
{
    using IdSet = std::unordered_set<std::uint32_t>;
    using SpellById = std::unordered_map<std::uint32_t, RE::SpellItem*>;

    constexpr std::size_t kMaxLevDepth = 4;     // nested leveled spell lists followed in other lists (vanilla nests one level)
    constexpr std::size_t kMaxNames = 5;        // spells named per list line (hits, picks)

    std::string LevelText(std::uint16_t level)
    {
        return level == NpcSpells::kSelfTestNoLevel ? std::string("none") : std::to_string(level);
    }

    std::string SpellText(const RE::SpellItem* spell)
    {
        const char* name = spell->GetName();  // RE/T/TESForm.h:308; system code page, the log is UTF-8
        return std::format("0x{:08X} {}", spell->GetFormID(),
            EncodingUtils::SanitizeToUTF8(std::string(name ? name : ""), PluginNames::kSystemCodePage));
    }

    std::string Names(const std::vector<std::uint32_t>& ids, const SpellById& spellById, const char* label)
    {
        std::string out;
        for (std::size_t i = 0; i < ids.size() && i < kMaxNames; ++i) {
            out += std::format("{}{}", i == 0 ? label : ", ", SpellText(spellById.at(ids[i])));
        }
        return out;
    }

    // RE/T/TESLeveledList.h:70-73. data(), not operator[]: our lists' arrays have no size head (GameLists.cpp), and
    // operator[] asserts through size()
    void AddLevEntries(const RE::TESLevSpell* lev, IdSet& out, std::size_t depth)
    {
        const auto* entries = lev ? lev->entries.data() : nullptr;
        if (!entries || depth > kMaxLevDepth) {
            return;
        }
        for (std::uint8_t i = 0; i < lev->numEntries; ++i) {
            const auto* form = entries[i].form;
            if (!form) {
                continue;
            }
            if (const auto* nested = form->As<RE::TESLevSpell>()) {
                AddLevEntries(nested, out, depth + 1);
            } else {
                out.insert(form->GetFormID());
            }
        }
    }

    // What a spell list (an NPC's or a race's) gives besides our lists: its static spells and its other leveled lists'
    // entries (RE/T/TESSpellList.h:44-49)
    void AddOtherSpells(const RE::TESSpellList::SpellData* d, const SelfTest::OurLists& ours, IdSet& out)
    {
        if (!d) {
            return;
        }
        for (std::uint32_t i = 0; d->spells && i < d->numSpells; ++i) {
            if (d->spells[i]) {
                out.insert(d->spells[i]->GetFormID());
            }
        }
        for (std::uint32_t i = 0; d->levSpells && i < d->numlevSpells; ++i) {
            if (d->levSpells[i] && !ours.Find(d->levSpells[i])) {
                AddLevEntries(d->levSpells[i], out, 0);
            }
        }
    }

    struct ListInfo
    {
        std::string editorId;
        NpcSpells::School school{};
        std::vector<std::uint32_t> spells;               // entry order, ids of SpellItem entries (ours hold no nested lists)
        std::vector<NpcSpells::SelfTestEntry> entries;   // the same entries with their levels
        bool allLevels = false;                          // llFlags kCalculateFromAllLevelsLTOrEqPCLevel (RE/T/TESLeveledList.h:47)
    };

    ListInfo ReadOurList(RE::TESLevSpell* lev, const SelfTest::OurLists& ours, SpellById& spellById)
    {
        ListInfo info;
        const auto at = ours.Find(lev);
        const auto& key = NpcSpells::SlotTable()[at->first].key;
        info.editorId = NpcSpells::EditorIdFor(key, at->second);
        info.school = key.school;
        auto* entries = lev->entries.data();
        for (std::uint8_t i = 0; entries && i < lev->numEntries; ++i) {
            auto* spell = entries[i].form ? entries[i].form->As<RE::SpellItem>() : nullptr;
            if (!spell) {
                continue;
            }
            info.spells.push_back(spell->GetFormID());
            info.entries.push_back({ spell->GetFormID(), entries[i].level });
            spellById.emplace(spell->GetFormID(), spell);
        }
        // GameLists.cpp writes 0 or kAllLevelsFlag: both kinds are on NPCs ("below" rungs carry the flag)
        info.allLevels = (lev->llFlags & RE::TESLeveledList::kCalculateFromAllLevelsLTOrEqPCLevel) != 0;
        return info;
    }

    // A loaded record, not one made at runtime (0xFF..., no file): only those have a template chain worth walking
    RE::TESNPC* FileNpc(RE::TESForm* form)
    {
        auto* npc = form ? form->As<RE::TESNPC>() : nullptr;
        return npc && npc->GetFile(0) ? npc : nullptr;  // RE/T/TESForm.h:275
    }

    // The spawned actor's bases. GetTemplateBase (RE/A/Actor.h:604, Actor.cpp:754) is ExtraLeveledCreature::templateBase
    // when the actor has that extra (RE/E/ExtraLeveledCreature.h:24-25), else the actor's base
    struct Bases
    {
        RE::TESNPC* actorBase = nullptr;
        RE::TESNPC* templateBase = nullptr;
        const RE::ExtraLeveledCreature* leveled = nullptr;
        RE::TESNPC* originalBase = nullptr;
    };

    Bases ReadBases(RE::Actor* actor)
    {
        Bases b;
        b.actorBase = actor->GetActorBase();  // RE/A/Actor.h:556
        b.templateBase = actor->GetTemplateBase();
        b.leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>();  // RE/T/TESObjectREFR.h:508
        b.originalBase = b.leveled && b.leveled->originalBase ? b.leveled->originalBase->As<RE::TESNPC>() : nullptr;
        return b;
    }

    // A leveled sample, or one whose chain has a leveled link before its holder: the holder from the spawned actor's
    // real records (the concrete template it drew, the record it was placed from, its base), the first whose
    // template walk resolves. Without ExtraLeveledCreature the template base is the actor base (in-game run 8: the console's
    // placeatme of an LVLN gave the drawn entry NPC as the base) and there is no original base: the walk starts from the
    // actor base, named as such.
    void ResolveHolderAfterSpawn(SelfTest::SampleRun& r, const Bases& b)
    {
        const std::array<std::pair<RE::TESNPC*, const char*>, 3> starts{ {
            { b.leveled ? FileNpc(b.templateBase) : nullptr, "templateBase" }, { FileNpc(b.originalBase), "originalBase" },
            { FileNpc(b.actorBase), "actorBase" } } };
        for (const auto& [start, name] : starts) {
            if (!start) {
                continue;
            }
            bool unresolvable = false;
            auto* holder = NpcSpellsGame::SpellListHolder(start, unresolvable);
            if (!unresolvable) {
                r.holder = holder;
                r.holderStart = start;
                r.holderFrom = name;
                r.unresolvable = false;
                return;
            }
        }
        r.unresolvable = true;
    }

    // Actor::VisitSpells (RE/A/Actor.h:706, ID 38781) walks every spell the actor has and builds the leveled spell cache
    // on the way; a visitor that never stops (RE/B/BSContainer.h) lets it reach the leveled part whatever comes first
    class VisitAllSpells final : public RE::Actor::ForEachSpellVisitor  // RE/A/Actor.h:278-288
    {
    public:
        RE::BSContainer::ForEachResult Visit(RE::SpellItem*) override { return RE::BSContainer::ForEachResult::kContinue; }
    };

    // HighProcessData::leveledSpellList (RE/H/HighProcessData.h:307): the spells the engine rolled from the actor's
    // leveled spell lists. Built on demand by Actor::VisitSpells; null when not built yet.
    bool ReadLeveledCache(RE::Actor* actor, IdSet& out, std::size_t& count)
    {
        const auto* high = actor->GetHighProcess();
        auto* cache = high ? high->leveledSpellList : nullptr;  // not const: BSSimpleList's const begin() does not compile
        if (!cache) {
            return false;
        }
        for (const auto* spell : *cache) {  // RE/B/BSSimpleList.h:299-306
            if (spell) {
                out.insert(spell->GetFormID());
                ++count;
            }
        }
        return true;
    }
}

void SelfTest::Check(State& s, SampleRun& r, RE::Actor* actor, std::size_t waitedTicks)
{
    const auto b = ReadBases(actor);
    if (!r.holder) {
        ResolveHolderAfterSpawn(r, b);
    }
    const auto* base = b.actorBase;
    const auto holderLists = r.holder ? OurListsOn(r.holder->actorEffects, s.ours) : std::vector<RE::TESLevSpell*>{};
    const auto baseLists = base ? OurListsOn(base->actorEffects, s.ours) : std::vector<RE::TESLevSpell*>{};
    // The engine builds the leveled spell cache from the actor's base: its lists are the ones checked. The holder's count
    // only for the verdict "not propagated" (JudgeSample), and as the lists checked when the base has none.
    const bool fromBase = !baseLists.empty();
    const auto& checked = fromBase ? baseLists : holderLists;
    using Flag = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG;  // RE/T/TESActorBaseData.h:46-62, the field at :74
    Log(s, std::format("SELFTEST sample {} kind={} placed {} actorBase {} templateBase {} extraLeveled={} originalBase {} copiedTemplate={} "
                       "holder {} holderFrom={} inherited={} chain={} holderLists={} actorBaseLists={} listsFrom={} 3D=loaded high=yes waitedMs={}",
        r.label, r.npc ? "npc" : "leveled", FormText(r.placed), FormText(base), FormText(b.templateBase), b.leveled ? "yes" : "no",
        FormText(b.originalBase), base ? (base->actorData.templateUseFlags.all(Flag::kCopiedTemplate) ? "yes" : "no") : "n/a",
        FormText(r.holder), r.holderFrom, r.holder && r.holder != r.holderStart ? "yes" : "no",
        r.unresolvable ? "leveled-link-before-holder" : "resolved", holderLists.size(), baseLists.size(),
        fromBase ? "actorBase" : (checked.empty() ? "none" : "holder"), waitedTicks * NpcSpells::kSelfTestPollMs));
    if (checked.empty()) {
        Record(s, r, NpcSpells::SelfTestVerdict::kFail, r.unresolvable
            ? "template chain has a leveled link before the spell list holder, and the spawned actor's base has no HoMNS list"
            : std::format("spell list holder {} has no HoMNS list, nor has the spawned actor's base", FormText(r.holder)));
        return;
    }

    IdSet excluded;
    std::vector<RE::TESNPC*> sources{ r.holder, r.npc, b.originalBase };
    for (auto* start : { b.templateBase, b.actorBase }) {  // each with every record of its template chain
        if (start) {
            const auto chain = NpcSpellsGame::TemplateChain(start);
            sources.insert(sources.end(), chain.begin(), chain.end());
        }
    }
    for (const auto* npc : sources) {
        AddOtherSpells(npc ? npc->actorEffects : nullptr, s.ours, excluded);
    }
    if (const auto* race = actor->GetRace()) {  // RE/T/TESRace.h:144, a TESSpellList: static spells and leveled lists
        AddOtherSpells(race->actorEffects, s.ours, excluded);
    }
    for (auto* spell : actor->GetActorRuntimeData().addedSpells) {  // RE/A/Actor.h:744
        if (spell) {
            excluded.insert(spell->GetFormID());
        }
    }

    SpellById spellById;
    std::vector<ListInfo> lists;
    for (auto* lev : checked) {
        lists.push_back(ReadOurList(lev, s.ours, spellById));
    }
    // One full walk first (it builds the leveled spell cache when missing), then the cache is read as it is. The HasSpell
    // calls below (RE/A/Actor.h:620) go through the same walk and see the same roll.
    VisitAllSpells visitAll;
    actor->VisitSpells(visitAll);
    IdSet cache;
    std::size_t cacheCount = 0;
    const bool cacheBuilt = ReadLeveledCache(actor, cache, cacheCount);
    Log(s, cacheBuilt ? std::format("SELFTEST cache {} leveledSpellList={} spells", r.label, cacheCount)
                      : std::format("SELFTEST cache {} leveledSpellList=null", r.label));

    auto* avOwner = actor->AsActorValueOwner();  // RE/A/Actor.h:778; GetActorValue = current value (RE/A/ActorValueOwner.h:16)
    std::array<IdSet, NpcSpells::kSchoolCount> schoolHits, schoolPicked;
    std::array<std::size_t, NpcSpells::kSchoolCount> schoolLists{};
    IdSet evidence;
    std::vector<NpcSpells::SelfTestListOutcome> outcomes;
    std::vector<std::pair<std::string, bool>> listPicked;  // for the repeated sample's tally
    for (std::size_t i = 0; i < lists.size(); ++i) {
        const auto& list = lists[i];
        IdSet others;
        for (std::size_t j = 0; j < lists.size(); ++j) {
            if (j != i) {
                others.insert(lists[j].spells.begin(), lists[j].spells.end());
            }
        }
        const auto witnesses = NpcSpells::WitnessSpells(list.spells, excluded);
        const IdSet witnessSet(witnesses.begin(), witnesses.end());
        const auto school = static_cast<std::size_t>(list.school);
        // RE/A/ActorValues.h:26-30: kAlteration..kRestoration are 18..22 in School order (static_assert in GamePool.cpp)
        const auto skill = avOwner ? avOwner->GetActorValue(static_cast<RE::ActorValue>(static_cast<int>(RE::ActorValue::kAlteration) + static_cast<int>(school))) : 0.0f;
        std::vector<std::uint32_t> hits;
        for (const auto id : witnesses) {
            if (actor->HasSpell(spellById.at(id))) {
                hits.push_back(id);
                schoolHits[school].insert(id);
                evidence.insert(id);
            }
        }
        const auto reach = NpcSpells::WitnessReach(list.entries, list.allLevels, skill, witnessSet);
        const auto picked = NpcSpells::PickedFrom(list.spells, cache);
        std::size_t pickedWitnesses = 0;
        for (const auto id : picked) {
            schoolPicked[school].insert(id);
            if (witnessSet.contains(id)) {
                ++pickedWitnesses;
                evidence.insert(id);
            }
        }
        ++schoolLists[school];
        outcomes.push_back({ witnesses.size(), reach.witnessReachable, picked.size(), pickedWitnesses });
        listPicked.emplace_back(list.editorId, !picked.empty());
        Log(s, std::format("SELFTEST list {} {} school={} skill={:.0f} entries={} levels={} minLevel={} witnessMinLevel={} topLevel={} witnessReachable={} "
                           "excluded={} witnesses={} shared={} hits={} picked={} pickedWitnesses={}{}{}",
            r.label, list.editorId, NpcSpells::kSchoolNames[school], skill, list.spells.size(), list.allLevels ? "all" : "highest",
            LevelText(reach.minLevel), LevelText(reach.witnessMinLevel), LevelText(reach.topLevel), reach.witnessReachable ? "yes" : "no",
            NpcSpells::CountExcluded(list.spells, excluded), witnesses.size(),
            NpcSpells::CountShared(witnesses, others), hits.size(), picked.size(), pickedWitnesses,
            Names(hits, spellById, " hitSpells: "), Names(picked, spellById, " picked: ")));
    }
    for (std::size_t sc = 0; sc < NpcSpells::kSchoolCount; ++sc) {
        if (schoolLists[sc] > 0) {
            Log(s, std::format("SELFTEST school {} {} lists={} distinctHits={} distinctPicked={}", r.label, NpcSpells::kSchoolNames[sc],
                schoolLists[sc], schoolHits[sc].size(), schoolPicked[sc].size()));
        }
    }
    r.tally.AddRun(listPicked);
    std::string reason;
    const auto verdict = NpcSpells::JudgeSample(outcomes, { holderLists.size(), baseLists.size() }, evidence.size(), reason);
    Record(s, r, verdict, reason);  // here, not on entry: a throw above leaves the sample to RunTick's Fail, so DONE counts every sample
}
